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
# Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
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
        chunk = s.read(4096)
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


def upload_firmware(s, firmware_bytes, base_addr, log):
    """Send an SFL upload + jump for `firmware_bytes` to base_addr.
    Caller must have already seen SERIALBOOT_MAGIC and ACKed it.
    Replicates the protocol used by litex_term --serial-boot."""
    chunk_size = 251  # max payload that fits one SFL frame's length byte
    written = 0
    while written < len(firmware_bytes):
        chunk = firmware_bytes[written : written + chunk_size]
        payload = struct.pack(">I", base_addr + written) + chunk
        s.write(sfl_frame(SFL_FRAME_LOAD, payload))
        # BIOS ACKs each frame; consume the ACK byte to flow-control.
        # Don't mirror the per-frame K — it'd be one K per ~250 bytes,
        # drowning out anything else in the terminal.
        ack = read_until(s, [b"K", b"C", b"E"], timeout=5, log=log, mirror=False)
        if ack is None:
            return False
        if ack[-1:] != b"K":
            # 'C' or 'E' means CRC / error — bail.
            return False
        written += len(chunk)
        if written % 4096 == 0:
            sys.stderr.write(f"\r[run_hw] upload {written}/{len(firmware_bytes)}")
            sys.stderr.flush()
    sys.stderr.write(f"\r[run_hw] upload {written}/{len(firmware_bytes)}\n")
    # Jump to the firmware.
    s.write(sfl_frame(SFL_FRAME_JUMP, struct.pack(">I", base_addr)))
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
    s.write(script.encode() if isinstance(script, str) else script)
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
    parser.add_argument("--baudrate", type=int, default=115200)
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
        "-q", "--quiet",
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
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        ).returncode
        if rc != 0:
            sys.exit(f"openFPGALoader failed (exit {rc})")

    if args.firmware:
        # Don't race the BIOS's 5-second auto-serialboot window. Wait
        # for it to fall through every boot method and land in its
        # interactive `litex>` console — at that point we can issue
        # `serialboot` ourselves whenever we're ready, with no race
        # and no \r\n vs \n parsing fragility.
        print("[run_hw] waiting for BIOS console prompt", file=sys.stderr)
        # The console prompt is ANSI-coloured (`\x1b[92;1mlitex\x1b[0m>`),
        # so match the bare "litex" substring instead of "litex>".
        if read_until(s, b"litex", timeout=60, log=log) is None:
            sys.exit("[run_hw] BIOS console prompt not seen — bitstream loaded?")
        # Tiny pause so the prompt's `>` byte arrives before we type.
        time.sleep(0.2)
        print("[run_hw] sending `serialboot`", file=sys.stderr)
        s.write(b"serialboot\n")
        # BIOS replies with the SFL magic; match on a substring that's
        # present whether the BIOS uses \n or \r\n.
        if read_until(s, b"sL5DdSMmkekro", timeout=15, log=log) is None:
            sys.exit("[run_hw] BIOS did not enter serialboot")
        s.write(SFL_MAGIC_ACK)
        firmware_bytes = open(args.firmware, "rb").read()
        if not upload_firmware(s, firmware_bytes, args.kernel_adr, log):
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
