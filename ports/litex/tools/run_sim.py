#!/usr/bin/env python3
#
# Run a MicroPython test (or set of tests) in a LiteX simulation.
#
# The harness spawns `litex_sim` with `--uart-pty --non-interactive`, waits
# for the UART pseudo-terminal to appear, waits for the MicroPython REPL
# banner, then drives each test through the raw REPL protocol. On first
# failure it prints the captured output and exits non-zero.
#
# We don't reuse tools/pyboard.py because its read_until() uses a 10 s
# timeout per step, which is often too tight for the sim: even with
# --fast-sim the Verilator wall-clock step rate makes a script-paste +
# parse + execute round-trip easily exceed that. This client uses generous
# sim-scale timeouts and is otherwise a minimal raw-REPL driver.
#
# Typical usage from ports/litex/:
#
#     tools/run_sim.py --firmware build/firmware.bin test/test_hello_world.py
#
# Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import atexit
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

PORT_DIR = Path(__file__).resolve().parent.parent

DEFAULT_PTY = "/tmp/litex_pty0"
DEFAULT_OUTPUT_DIR = "/tmp/litex_mpy_sim"
DEFAULT_FIRMWARE = PORT_DIR / "build" / "firmware.bin"
# Sim startup time is dominated by one-time Verilator C++ compilation, which
# on a typical laptop takes ~2 minutes the first time and is free afterward
# (litex_sim reuses the obj_dir). The REPL wait only starts once Verilator
# is done building, so the short timeout there is fine.
SIM_BUILD_TIMEOUT_S = 600
# Enlarged for the ethernet SoC variant: the LiteEth MAC + PHY adds enough
# Verilator work that the firmware takes ~6 min of wall-clock to boot
# from Liftoff to the MicroPython REPL, vs ~seconds for the base SoC.
REPL_READY_TIMEOUT_S = 900
PTY_APPEAR_TIMEOUT_S = 300
# LiteX's RS232PHYModel is a byte-level valid/ready stream (no baud), but
# Verilator's wall-clock step rate still bounds throughput, and a full
# round-trip through MicroPython's parser + compiler + execute under
# Verilator can easily take several minutes on the ethernet SoC. Be
# generous.
RAW_REPL_STEP_TIMEOUT_S = 900
TEST_EXEC_TIMEOUT_S = 900
# Log substring printed when the Verilator `make` recursion for the gateware
# directory exits — the last build-time message before litex_sim launches the
# Vsim binary. We look for "Leaving directory '<output_dir>/gateware'" so we
# don't fire early on any of the software/lib* leaves. Matches "make:",
# "make[1]:", etc. so it works at any recursion depth.
SIM_BUILD_DONE_SUFFIX = "/gateware'"
# REPL tokens we key off of.
FRIENDLY_PROMPT = b">>> "
RAW_REPL_BANNER = b"raw REPL; CTRL-B to exit\r\n>"
SOFT_REBOOT = b"soft reboot\r\n"


def wait_for_path(path, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if os.path.exists(path):
            return True
        time.sleep(0.2)
    return False


def wait_for_log_marker(log_path, marker_predicate, timeout):
    """Tail a log file until a line matching the predicate appears."""
    deadline = time.monotonic() + timeout
    seen = ""
    while not os.path.exists(log_path) and time.monotonic() < deadline:
        time.sleep(0.2)
    if not os.path.exists(log_path):
        return False
    with open(log_path, "r") as f:
        while time.monotonic() < deadline:
            chunk = f.read()
            if chunk:
                seen += chunk
                for line in seen.splitlines():
                    if marker_predicate(line):
                        return True
            else:
                time.sleep(0.5)
    return False


class PtyRepl:
    """Minimal raw-REPL client over a PTY, tuned for slow simulators."""

    def __init__(self, path, verbose=False):
        self.path = path
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.verbose = verbose
        self.rx_buf = b""

    def close(self):
        os.close(self.fd)

    def write(self, data):
        # Split into modest chunks so that we never push more bytes than the
        # firmware's UART RX FIFO + libbase software ring buffer can hold
        # while we wait for the reader-side ISR to drain. The firmware's
        # libbase ring buffer is 128 bytes and the hardware FIFO is 16 by
        # default, but the LiteX sim's UART model can deliver bytes faster
        # than the ISR runs, so any extra in flight gets dropped silently
        # by the firmware. Chunking + a tiny inter-chunk yield keeps the
        # writer paced to whatever the firmware can absorb.
        if self.verbose:
            sys.stderr.write(f"[tx {data!r}]\n")
            sys.stderr.flush()
        chunk_size = 64
        for i in range(0, len(data), chunk_size):
            chunk = data[i : i + chunk_size]
            view = memoryview(chunk)
            while view:
                try:
                    n = os.write(self.fd, view)
                    view = view[n:]
                except BlockingIOError:
                    self._read_available()
                    time.sleep(0.01)
            # Yield so the firmware's RX ISR has a chance to drain the
            # FIFO before we pile in another chunk. 50 ms is comfortably
            # above the worst-case Verilator drain latency we've measured;
            # on hardware it's invisible.
            if i + chunk_size < len(data):
                time.sleep(0.05)

    def _read_available(self):
        try:
            chunk = os.read(self.fd, 65536)
        except BlockingIOError:
            chunk = b""
        if chunk and self.verbose:
            sys.stderr.buffer.write(chunk)
            sys.stderr.flush()
        return chunk

    def read_until(self, token, timeout):
        """Keep reading until `token` has been seen, or timeout expires."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            # Check the buffer first: multiple tokens can arrive in a single
            # read, and after consuming one the next may already be waiting.
            if token in self.rx_buf:
                pos = self.rx_buf.index(token) + len(token)
                consumed, self.rx_buf = self.rx_buf[:pos], self.rx_buf[pos:]
                return consumed
            chunk = self._read_available()
            if chunk:
                self.rx_buf += chunk
            else:
                time.sleep(0.1)
        return None

    def drain(self, duration=0.5):
        """Read and discard whatever is currently buffered."""
        end = time.monotonic() + duration
        while time.monotonic() < end:
            chunk = self._read_available()
            if not chunk:
                time.sleep(0.1)
        self.rx_buf = b""

    def wait_for_friendly_repl(self, timeout):
        """Wait until we see a friendly-REPL banner or prompt."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            chunk = self._read_available()
            if chunk:
                self.rx_buf += chunk
                if b"MicroPython" in self.rx_buf or FRIENDLY_PROMPT in self.rx_buf:
                    return True
            else:
                time.sleep(0.1)
        return False

    def enter_raw_repl(self, step_timeout):
        # Ensure we're in friendly REPL, interrupt any running code, then
        # switch to raw REPL. We intentionally skip pyboard.py's optional
        # ctrl-D soft-reset step: the REPL has just booted so there's
        # nothing to clear, and the soft-reset path triggers a sim-side
        # boot loop that adds tens of seconds before the next prompt.
        self.write(b"\r\x02")  # ctrl-B: exit raw REPL if we were in one
        self.write(b"\r\x03\x03")  # ctrl-C twice: interrupt any running code
        self.drain(duration=2.0)
        self.write(b"\r\x01")  # ctrl-A: enter raw REPL
        if self.read_until(RAW_REPL_BANNER, step_timeout) is None:
            return False
        return True

    def exec_script(self, source, step_timeout, exec_timeout):
        """Send a script via raw REPL, return (stdout, stderr)."""
        # Paste the script.
        data = source.encode("utf-8") if isinstance(source, str) else source
        # Use raw-paste by default (faster for large scripts), but keep simple
        # enough: we just write all bytes then ctrl-D.
        self.write(data)
        self.write(b"\x04")
        # Expect the leading 'OK' that signals script accepted.
        if self.read_until(b"OK", step_timeout) is None:
            return None, b"[run_sim] timed out waiting for 'OK' after script"
        # stdout runs until 0x04 (end-of-stdout), then stderr until the next
        # 0x04, then the '>' prompt marking end of exec.
        stdout = self.read_until(b"\x04", exec_timeout)
        if stdout is None:
            return None, b"[run_sim] timed out waiting for stdout EOT"
        stdout = stdout[:-1]  # strip the 0x04
        stderr = self.read_until(b"\x04", exec_timeout)
        if stderr is None:
            return stdout, b"[run_sim] timed out waiting for stderr EOT"
        stderr = stderr[:-1]
        # Next '>' is the raw-REPL prompt, ready for another command.
        self.read_until(b">", step_timeout)
        return stdout, stderr


def spawn_sim(args):
    if args.fast_sim:
        # Use the local wrapper that monkey-patches litex_sim's hard-coded
        # sys_clk_freq down and uncomments the BIOS_NO_DELAYS / NO_PROMPT
        # configs so the BIOS skips its serialboot timeout. Cuts REPL-up
        # time on the sim from ~30 s to under 5 s.
        cmd = [
            sys.executable,
            str(PORT_DIR / "tools" / "litex_sim_fast.py"),
            "--sys-clk-freq",
            str(args.sys_clk_freq),
            "--skip-bios-boot",
        ]
        # Auto-pass --with-uart1 / --with-ethernet if the firmware's csr.h
        # has the matching CSR_*_BASE. litex_sim regenerates the SoC on
        # every launch, so we need to feed the same SoC topology in or
        # the sim's CSR layout won't match what the firmware was
        # compiled against.
        csr_h = Path(args.output_dir) / "software" / "include" / "generated" / "csr.h"
        csr_text = csr_h.read_text() if csr_h.is_file() else ""

        with_uart1 = args.with_uart1 or "CSR_UART1_BASE" in csr_text
        if with_uart1:
            cmd.append("--with-uart1")

        with_ethernet = args.with_ethernet or "CSR_ETHMAC_BASE" in csr_text
        if with_ethernet:
            cmd.append("--with-ethernet")
            # The default `sim` PHY model needs a tap interface on the
            # host (typically `litex-sim` or `tap0` with the gateway IP
            # routed). Without one the sim will print a libevent error
            # but still boot — the netif comes up disconnected which is
            # fine for API smoke tests.
    else:
        cmd = [sys.executable, "-m", "litex.tools.litex_sim"]
        # Vanilla litex_sim doesn't auto-detect — pass through whatever
        # csr.h says the SoC has, so the regen here matches the SoC the
        # firmware was built against.
        csr_h = Path(args.output_dir) / "software" / "include" / "generated" / "csr.h"
        csr_text = csr_h.read_text() if csr_h.is_file() else ""
        if args.with_ethernet or "CSR_ETHMAC_BASE" in csr_text:
            cmd.append("--with-ethernet")
    cmd += [
        "--cpu-type",
        args.cpu_type,
        "--integrated-main-ram-size",
        hex(args.ram_size),
        "--libc-mode",
        "full",
        "--output-dir",
        args.output_dir,
        "--ram-init",
        str(args.firmware),
        "--uart-pty",
        "--uart-pty-path",
        args.pty,
        "--non-interactive",
        "--opt-level",
        args.opt_level,
        # Single-threaded Verilator runtime: for a small SoC the inter-thread
        # coordination overhead easily dominates, making 1 thread faster than
        # many.
        "--threads",
        str(args.threads),
    ]
    log = open(args.log, "w")
    print("[run_sim] launching:", " ".join(cmd), file=sys.stderr)
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.DEVNULL,
        stdout=log,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    return proc, log


def kill_sim(proc):
    if proc is None or proc.poll() is not None:
        return
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)


def strip_comments(source):
    """Drop full-line '#' comments and blank lines.

    Every byte we don't send is a Verilator step we don't pay for. We
    deliberately only strip *full-line* comments — inline comments and
    docstrings are left alone so we never alter the test's actual
    behavior or formatting.
    """
    kept = []
    for line in source.splitlines():
        stripped = line.lstrip()
        if stripped.startswith("#") or not stripped:
            continue
        kept.append(line)
    return "\n".join(kept) + "\n"


def run_tests(repl, tests, step_timeout, exec_timeout):
    failures = []
    for test in tests:
        source = strip_comments(Path(test).read_text())
        print(
            f"[run_sim] running {test} ({len(source)} bytes after stripping comments)",
            file=sys.stderr,
        )
        stdout, stderr = repl.exec_script(source, step_timeout, exec_timeout)
        if stdout is not None:
            sys.stdout.write(stdout.decode("utf-8", errors="replace"))
            sys.stdout.flush()
        if stderr:
            sys.stderr.write(stderr.decode("utf-8", errors="replace"))
            sys.stderr.flush()
        if stdout is None or stderr:
            failures.append(test)
    return failures


def main():
    parser = argparse.ArgumentParser(
        description="Run MicroPython tests inside a LiteX simulation."
    )
    parser.add_argument("tests", nargs="+", help="Python test files to execute on the sim.")
    parser.add_argument(
        "--firmware",
        default=str(DEFAULT_FIRMWARE),
        help=f"MicroPython firmware binary (default: {DEFAULT_FIRMWARE}).",
    )
    parser.add_argument(
        "--output-dir",
        default=DEFAULT_OUTPUT_DIR,
        help=f"litex_sim output dir (default: {DEFAULT_OUTPUT_DIR}).",
    )
    parser.add_argument(
        "--pty",
        default=DEFAULT_PTY,
        help=f"PTY path exposed by litex_sim (default: {DEFAULT_PTY}).",
    )
    parser.add_argument("--cpu-type", default="vexriscv")
    parser.add_argument(
        "--ram-size",
        type=lambda s: int(s, 0),
        default=0x01000000,
        help="Integrated main RAM size (default: 16 MiB). MicroPython "
        "zeroes a GC alloc table proportional to this size at "
        "startup; under Verilator that scales linearly, so keep this "
        "small unless a test actually needs the headroom.",
    )
    parser.add_argument(
        "--opt-level",
        default="O3",
        help="Verilator -O level for the compiled sim model (default: O3).",
    )
    parser.add_argument(
        "--threads",
        type=int,
        default=1,
        help="Verilator runtime thread count (default: 1; for "
        "a small SoC more threads usually hurt).",
    )
    parser.add_argument(
        "--fast-sim",
        action="store_true",
        default=True,
        help="Spawn the sim via tools/litex_sim_fast.py instead of upstream "
        "litex_sim. Lowers sys_clk_freq and turns on BIOS_NO_DELAYS so the "
        "REPL appears in seconds instead of tens of seconds (default: on).",
    )
    parser.add_argument(
        "--no-fast-sim",
        dest="fast_sim",
        action="store_false",
        help="Use upstream litex_sim verbatim (1 MHz reported sys_clk, full BIOS — REPL takes ~30 s).",
    )
    parser.add_argument(
        "--sys-clk-freq",
        type=lambda s: int(float(s)),
        default=100_000,
        help="With --fast-sim, the SoC clock to advertise (default: 100 kHz). "
        "Lower values make every sim-time delay (BIOS timeouts, "
        "mp_hal_delay_ms, time.sleep) faster in wall-clock; the actual "
        "Verilator step rate is unchanged.",
    )
    parser.add_argument(
        "--with-uart1",
        action="store_true",
        default=False,
        help="With --fast-sim, inject a second 'stub' UART into the SoC so "
        "machine.UART(1) and machine.UART(1).irq() are exercisable in sim. "
        "The UART has the standard CSR layout but no actual host-side "
        "endpoint — reads always block (rxempty=1).",
    )
    parser.add_argument(
        "--with-ethernet",
        action="store_true",
        default=False,
        help="Pass --with-ethernet through to litex_sim so the SoC includes "
        "a LiteEth MAC. Auto-enabled when csr.h has CSR_ETHMAC_BASE. The "
        "sim PHY model expects a host-side tap interface (`tap0` with a "
        "reachable gateway); without one the netif comes up disconnected "
        "but the firmware still boots, which is enough for API smoke "
        "tests.",
    )
    parser.add_argument(
        "--log",
        default="/tmp/litex_sim.log",
        help="File to redirect litex_sim stdout/stderr into.",
    )
    parser.add_argument(
        "--keep-sim",
        action="store_true",
        help="Leave litex_sim running after the tests (for debugging).",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Stream the sim UART bytes to stderr while driving the REPL.",
    )
    args = parser.parse_args()

    if not Path(args.firmware).is_file():
        sys.exit(f"firmware not found: {args.firmware} (did you run `make`?)")

    try:
        os.unlink(args.pty)
    except FileNotFoundError:
        pass

    proc, log = spawn_sim(args)
    if not args.keep_sim:
        atexit.register(kill_sim, proc)

    print(
        f"[run_sim] waiting for Verilator build to finish (up to "
        f"{SIM_BUILD_TIMEOUT_S}s, see {args.log})",
        file=sys.stderr,
    )

    def gateware_make_exited(line):
        # e.g. "make[1]: Leaving directory '/tmp/litex_mpy_sim/gateware'"
        return "Leaving directory " in line and line.rstrip().endswith(SIM_BUILD_DONE_SUFFIX)

    if not wait_for_log_marker(args.log, gateware_make_exited, SIM_BUILD_TIMEOUT_S):
        sys.exit(f"timed out waiting for Verilator build (see {args.log})")
    print("[run_sim] Verilator build done, waiting for REPL", file=sys.stderr)

    if not wait_for_path(args.pty, PTY_APPEAR_TIMEOUT_S):
        sys.exit(f"timed out waiting for {args.pty} to appear (see {args.log})")

    repl = PtyRepl(args.pty, verbose=args.verbose)
    try:
        if not repl.wait_for_friendly_repl(REPL_READY_TIMEOUT_S):
            sys.exit(f"timed out waiting for MicroPython REPL on {args.pty} (see {args.log})")
        print("[run_sim] REPL up, entering raw REPL", file=sys.stderr)
        if not repl.enter_raw_repl(RAW_REPL_STEP_TIMEOUT_S):
            sys.exit(f"failed to enter raw REPL (see {args.log})")
        print("[run_sim] raw REPL entered, executing tests", file=sys.stderr)

        failures = run_tests(repl, args.tests, RAW_REPL_STEP_TIMEOUT_S, TEST_EXEC_TIMEOUT_S)
    finally:
        repl.close()
        log.close()

    if failures:
        print(f"[run_sim] FAILED: {len(failures)}/{len(args.tests)}", file=sys.stderr)
        for t in failures:
            print(f"  - {t}", file=sys.stderr)
        sys.exit(1)
    print(f"[run_sim] OK: {len(args.tests)}/{len(args.tests)}", file=sys.stderr)


if __name__ == "__main__":
    main()
