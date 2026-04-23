#!/usr/bin/env python3
#
# Drive a real LiteX board over its UART to upload MicroPython firmware
# and run tests against the live REPL — all from a single open serial
# fd so DTR/RTS never toggle (which on Arty-class boards reaches the
# FPGA reset line and would wipe SDRAM between upload and test).
#
# Typical usage from ports/litex/, with the Arty plugged into
# /dev/ttyUSB1 (FTDI channel 1):
#
#     ports/litex/tools/run_hw.py \
#         --bitstream /tmp/arty_eth/gateware/digilent_arty.bit \
#         --firmware  ports/litex/build/firmware.bin \
#         ports/litex/test/test_hw_arty.py
#
# Drop --bitstream when the FPGA is already loaded with a fresh
# bitstream and the BIOS is in its 5-second serialboot wait. Drop
# --firmware when the MicroPython REPL is already up.
#
# Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

PORT_DIR = Path(__file__).resolve().parent.parent

# SFL ACK we send back when we see the BIOS's "sL5DdSMmkekro" magic.
# (litex/tools/litex_term.py uses the matching pair.)
SFL_MAGIC_ACK = b"z6IHG7cYDID6o\n"
# SFL (Serial File Loader) command codes.
SFL_FRAME_LOAD = 0x01
SFL_FRAME_JUMP = 0x02
SFL_FRAME_ABORT = 0x00


def crc16(data):
    """CCITT-FALSE CRC-16, the variant LiteX BIOS verifies for SFL frames."""
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc


def sfl_frame(cmd, payload):
    length = len(payload)
    body = bytes([cmd]) + payload
    crc = crc16(body)
    return bytes([length]) + struct.pack(">H", crc) + body


def open_port(device, baudrate):
    import serial

    s = serial.Serial(device, baudrate=baudrate, timeout=0.1)
    return s


MIRROR = True  # set False by --quiet


def read_until(s, marker, timeout, log=None, mirror=None):
    """Read from the serial port until `marker` (or any in a list of
    markers) appears in the buffer. Every byte received goes both to
    `log` (a file we keep for post-mortem) and — if `mirror` is True —
    to stderr in real time, so the user sees BIOS output, MicroPython
    REPL banner, and test prints as they happen.

    `mirror` defaults to the module-level MIRROR (toggled by --quiet)
    but call sites that produce noisy per-byte ACK traffic can force
    it off by passing mirror=False."""
    if mirror is None:
        mirror = MIRROR
    deadline = time.monotonic() + timeout
    buf = b""
    while time.monotonic() < deadline:
        # `read(n, timeout=T)` blocks until n bytes OR T expires — so
        # read(4096) on a single-byte ACK would always burn the full
        # timeout. Use in_waiting to grab everything queued and only
        # block (for one byte) when nothing's there.
        n = max(s.in_waiting, 1)
        chunk = s.read(n)
        if chunk:
            buf += chunk
            if log is not None:
                log.write(chunk)
                log.flush()
            if mirror:
                sys.stderr.buffer.write(chunk)
                sys.stderr.buffer.flush()
            if isinstance(marker, (bytes, bytearray)):
                if marker in buf:
                    return buf
            else:
                if any(m in buf for m in marker):
                    return buf
    return None


def upload_firmware(s, firmware_bytes, base_addr, log, chunk_size=251, batch_frames=16):
    """Batched SFL upload of `firmware_bytes` to base_addr. We send a
    burst of `batch_frames` SFL LOAD frames in a single s.write() so
    Python loop overhead and USB write syscalls amortise across the
    burst, then drain all the K acks before the next burst. Same
    trick litex_term uses to approach line rate.
    Caller must have already ACKed the BIOS magic."""
    sent = 0
    last_print = 0
    total = len(firmware_bytes)
    start = time.monotonic()
    # Rough upload estimate: 8N1 framing → 10 bits/byte, so bytes/sec
    # is baud/10. Real throughput is a bit lower because of SFL frame
    # overhead and ack round-trips, but it's close enough for a banner.
    bytes_per_sec = max(s.baudrate, 1) // 10
    sys.stderr.write(
        f"[run_hw] uploading {total} B "
        f"(~{total / bytes_per_sec:.0f} s at {s.baudrate} baud, "
        f"batches of {batch_frames})\n"
    )
    sys.stderr.flush()
    while sent < total:
        # Build up to `batch_frames` SFL frames into one buffer.
        burst = bytearray()
        n_frames = 0
        burst_start = sent
        while sent < total and n_frames < batch_frames:
            chunk = firmware_bytes[sent : sent + chunk_size]
            payload = struct.pack(">I", base_addr + sent) + chunk
            burst += sfl_frame(SFL_FRAME_LOAD, payload)
            sent += len(chunk)
            n_frames += 1
        s.write(bytes(burst))
        s.flush()
        # Now read N acks (one K per frame). They arrive interleaved
        # with the BIOS's own throughput, so we just keep reading bytes
        # until we've collected n_frames K's.
        ks_seen = 0
        deadline = time.monotonic() + 10
        while ks_seen < n_frames and time.monotonic() < deadline:
            n = max(s.in_waiting, 1)
            chunk = s.read(n)
            if not chunk:
                continue
            log.write(chunk)
            log.flush()
            for b in chunk:
                if b == ord("K"):
                    ks_seen += 1
                elif b in (ord("C"), ord("E")):
                    sys.stderr.write(
                        f"\n[run_hw] upload {chr(b)} at {burst_start + ks_seen * chunk_size}/{total}\n"
                    )
                    return False
        if ks_seen < n_frames:
            sys.stderr.write(
                f"\n[run_hw] upload stalled — got {ks_seen}/{n_frames} acks "
                f"in burst at {burst_start}/{total}\n"
            )
            return False
        # Progress every ~16 KiB.
        if sent - last_print >= 16384 or sent == total:
            elapsed = time.monotonic() - start
            kbps = sent / max(elapsed, 0.001) / 1024
            sys.stderr.write(
                f"\r[run_hw] upload {sent}/{total}  ({kbps:.1f} KiB/s, {elapsed:.1f} s)"
            )
            sys.stderr.flush()
            last_print = sent
    sys.stderr.write("\n")
    # Jump to the firmware.
    s.write(sfl_frame(SFL_FRAME_JUMP, struct.pack(">I", base_addr)))
    s.flush()
    return True


def raw_repl_send(s, script, exec_timeout, log):
    """Execute `script` via raw REPL on the open serial port `s`.
    Returns (stdout_bytes, stderr_bytes) or (None, error_str)."""
    # Make sure the friendly REPL is responsive: ctrl-C twice then
    # ctrl-A to enter raw REPL.
    s.write(b"\r\x03\x03")
    time.sleep(0.2)
    s.read(8192)  # drain
    s.write(b"\r\x01")
    if read_until(s, b"raw REPL; CTRL-B to exit\r\n>", timeout=5, log=log) is None:
        return None, "did not enter raw REPL"
    payload = script.encode() if isinstance(script, str) else script
    # Try raw-paste mode (MicroPython's windowed flow-control protocol —
    # the host only sends as much as the device's allocated window, the
    # device sends \x01 to grant another window's worth of credit). This
    # is the only paste path that survives high-baud links (2 Mbps+)
    # where the parser can't drain the UART byte-for-byte. Falls back to
    # naive chunked write if the device doesn't speak it (older firmware).
    s.write(b"\x05A\x01")
    s.flush()
    resp = s.read(2)
    if log is not None and resp:
        log.write(resp)
    if resp == b"R\x01":
        # Device speaks raw-paste. Read 2-byte window size, then push
        # the script in chunks bounded by the running window, listening
        # for \x01 (window credit) or \x04 (abrupt end) interleaved.
        hdr = s.read(2)
        if log is not None:
            log.write(hdr)
        if len(hdr) != 2:
            return None, "raw-paste: short window header"
        window_size = struct.unpack("<H", hdr)[0]
        window_remain = window_size
        i = 0
        while i < len(payload):
            while window_remain == 0 or s.in_waiting:
                ack = s.read(1)
                if log is not None and ack:
                    log.write(ack)
                if ack == b"\x01":
                    window_remain += window_size
                elif ack == b"\x04":
                    s.write(b"\x04")
                    s.flush()
                    sent = i + (window_size - window_remain) if window_remain <= window_size else i
                    return None, f"raw-paste: device ended early (after {sent}/{len(payload)} B)"
                elif ack == b"":
                    break  # short read; loop back to top to re-check
                else:
                    return None, "raw-paste: unexpected byte %r" % ack
            chunk = payload[i : i + min(window_remain, len(payload) - i)]
            s.write(chunk)
            s.flush()
            window_remain -= len(chunk)
            i += len(chunk)
        s.write(b"\x04")
        s.flush()
        # Device echoes \x04 as end-ack, then goes straight to stdout
        # (no "OK" marker in raw-paste mode — that's only in friendly
        # raw REPL). Drain past the end-ack so the stdout reader below
        # starts at the script's first print().
        if read_until(s, b"\x04", timeout=10, log=log) is None:
            return None, "raw-paste: no end-ack"
    else:
        # Fallback: naive write + "OK" marker check. Safe at 115200,
        # may drop bytes at higher baud where the parser can't keep up.
        for i in range(0, len(payload), 256):
            s.write(payload[i : i + 256])
            s.flush()
        s.write(b"\x04")
        if read_until(s, b"OK", timeout=10, log=log) is None:
            return None, "did not see OK after script paste"
    out = read_until(s, b"\x04", timeout=exec_timeout, log=log)
    if out is None:
        return None, "stdout EOT timeout"
    out = out[: out.rindex(b"\x04")]
    err = read_until(s, b"\x04", timeout=5, log=log)
    if err is None:
        return out, "stderr EOT timeout"
    err = err[: err.rindex(b"\x04")]
    read_until(s, b">", timeout=5, log=log)  # next prompt
    return out, err


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("tests", nargs="+", help="Python test files to run.")
    parser.add_argument(
        "--port",
        default=os.environ.get("LITEX_HW_PORT", "/dev/ttyUSB1"),
        help="MicroPython REPL serial device (default /dev/ttyUSB1).",
    )
    parser.add_argument("--baudrate", type=int, default=1_000_000)
    parser.add_argument(
        "--batch-frames", type=int, default=16, help="Number of SFL frames per write burst."
    )
    parser.add_argument(
        "--bitstream",
        default=None,
        help="If set, openFPGALoader-load this bitstream after the "
        "serial port is open. Required when the FPGA is empty or the "
        "BIOS is in console state — the BIOS serialboot window only "
        "lasts ~5 s.",
    )
    parser.add_argument(
        "--openfpgaloader-board",
        default="arty",
        help="-b argument to openFPGALoader (default 'arty').",
    )
    parser.add_argument(
        "--firmware",
        default=None,
        help="If set, SFL-upload this firmware to --kernel-adr after "
        "the BIOS prints its serialboot prompt.",
    )
    parser.add_argument("--kernel-adr", type=lambda s: int(s, 0), default=0x40000000)
    parser.add_argument("--log", default="/tmp/run_hw.log")
    parser.add_argument(
        "-q",
        "--quiet",
        action="store_true",
        help="Don't mirror serial traffic (BIOS output, REPL prints, "
        "live test stdout) to stderr. Use this in CI or when piping "
        "stdout. The full transcript is still captured in --log.",
    )
    args = parser.parse_args()
    global MIRROR
    MIRROR = not args.quiet

    try:
        import serial  # noqa: F401
    except ImportError:
        sys.exit("pyserial not installed (pip install pyserial)")

    if not Path(args.port).exists():
        sys.exit(f"serial port {args.port} not found")

    # Disable HUPCL so closing the fd at the end doesn't toggle
    # DTR/RTS — keeps MicroPython running for any follow-up session.
    subprocess.run(["stty", "-F", args.port, "-hupcl"], check=False)

    log = open(args.log, "wb")
    s = open_port(args.port, args.baudrate)

    # Optionally load the bitstream now that the serial port is open.
    if args.bitstream:
        print(f"[run_hw] loading {args.bitstream}", file=sys.stderr)
        rc = subprocess.run(
            ["openFPGALoader", "-b", args.openfpgaloader_board, args.bitstream],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode
        if rc != 0:
            sys.exit(f"openFPGALoader failed (exit {rc})")

    if args.firmware:
        # Don't race the BIOS's 5-second auto-serialboot window. Wait
        # for it to fall through every boot method and land in its
        # interactive `litex>` console — at that point we can issue
        # `serialboot` ourselves whenever we're ready, with no race
        # and no \r\n vs \n parsing fragility.
        #
        # Send Q periodically during the wait to abort every boot
        # method (serial, network, SDCard, flash) — the BIOS prints
        # "Press Q or ESC to abort boot completely." for each one,
        # and without this the SDCard-boot path would happily run
        # whatever boot.json we have on the card instead of dropping
        # into the console.
        print(
            "[run_hw] waiting for BIOS console prompt (sending Q to abort auto-boot)",
            file=sys.stderr,
        )
        deadline = time.monotonic() + 60
        seen = b""
        litex_prompt = b"litex"  # ANSI-wrapped, match the bare brand name
        while time.monotonic() < deadline and litex_prompt not in seen:
            s.write(b"Q\n")
            s.flush()
            chunk = read_until(s, [litex_prompt, b"booted program"], timeout=2, log=log)
            if chunk:
                seen += chunk
                if b"booted program" in chunk:
                    sys.exit(
                        "[run_hw] board started executing a SDCard/network "
                        "boot.json before we could abort — remove it and retry"
                    )
        if litex_prompt not in seen:
            sys.exit("[run_hw] BIOS console prompt not seen — bitstream loaded?")
        # Tiny pause so the prompt's `>` byte arrives before we type.
        time.sleep(0.2)
        print("[run_hw] sending `serialboot`", file=sys.stderr)
        s.write(b"serialboot\n")
        s.flush()
        # BIOS replies with the SFL magic; match on a substring that's
        # present whether the BIOS uses \n or \r\n.
        if read_until(s, b"sL5DdSMmkekro", timeout=15, log=log) is None:
            sys.exit("[run_hw] BIOS did not enter serialboot")
        # Drain any trailing bytes (the BIOS prints the magic with a
        # newline; we don't want stale prompt fragments confusing the
        # frame-ack reads further down).
        s.write(SFL_MAGIC_ACK)
        s.flush()
        time.sleep(0.1)
        firmware_bytes = open(args.firmware, "rb").read()
        if not upload_firmware(
            s, firmware_bytes, args.kernel_adr, log, batch_frames=args.batch_frames
        ):
            sys.exit("[run_hw] firmware upload failed")
        if read_until(s, b"MicroPython", timeout=30, log=log) is None:
            sys.exit("[run_hw] MicroPython REPL did not appear after upload")
        print("[run_hw] REPL up", file=sys.stderr)

    failures = []
    for test in args.tests:
        print(f"[run_hw] {test}", file=sys.stderr)
        script = Path(test).read_text()
        out, err = raw_repl_send(s, script, exec_timeout=120, log=log)
        if out is None:
            sys.stderr.write(f"  ! {err}\n")
            failures.append(test)
            continue
        if not MIRROR:
            # In quiet mode the live stream is suppressed, so emit the
            # captured stdout once at the end. With MIRROR on the user
            # already saw it streaming.
            sys.stdout.write(out.decode("utf-8", errors="replace"))
            sys.stdout.flush()
        if isinstance(err, str):
            # raw_repl_send returns a str when the stderr EOT read
            # itself failed (transient, harmless if the test printed
            # everything we expected). Surface it but don't fail.
            sys.stderr.write(f"  ! {err}\n")
        elif err:
            sys.stderr.write(err.decode("utf-8", errors="replace"))
            failures.append(test)

    log.close()
    s.close()
    if failures:
        print(f"[run_hw] FAILED: {len(failures)}/{len(args.tests)}", file=sys.stderr)
        for t in failures:
            print(f"  - {t}", file=sys.stderr)
        sys.exit(1)
    print(f"[run_hw] OK: {len(args.tests)}/{len(args.tests)}", file=sys.stderr)


if __name__ == "__main__":
    main()
