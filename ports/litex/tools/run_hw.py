#!/usr/bin/env python3
#
# Drive a real LiteX board over its UART to run MicroPython tests.
#
# Mirror of tools/run_sim.py but for hardware: connects to the board's
# serial port (default /dev/ttyUSB1, the FTDI's second channel on the
# Digilent Arty), enters raw REPL via tools/pyboard.py, runs each test
# script, prints stdout, exits non-zero on the first failure.
#
# Typical usage from ports/litex/:
#
#     # 1) load the bitstream + firmware once (see README)
#     # 2) test/test_hw_arty.py covers the new functionality:
#     tools/run_hw.py --port /dev/ttyUSB1 test/test_hw_arty.py
#
# Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import importlib.util
import os
import subprocess
import sys
from pathlib import Path

PORT_DIR = Path(__file__).resolve().parent.parent
TOP = PORT_DIR.parent.parent
PYBOARD = TOP / "tools" / "pyboard.py"


def run_one(port, baudrate, script):
    """Run one test script on the board via pyboard.py. Returns True on pass."""
    cmd = [
        sys.executable,
        str(PYBOARD),
        "--device",
        port,
        "--baudrate",
        str(baudrate),
        str(script),
    ]
    print(f"[run_hw] {script}", file=sys.stderr)
    proc = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(proc.stdout)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
    return proc.returncode == 0


def main():
    parser = argparse.ArgumentParser(
        description="Run MicroPython tests on a real LiteX board over UART.",
    )
    parser.add_argument(
        "tests", nargs="+", help="Python test files to run on the board."
    )
    parser.add_argument(
        "--port",
        default=os.environ.get("LITEX_HW_PORT", "/dev/ttyUSB1"),
        help="Serial device the MicroPython REPL is on "
        "(default: /dev/ttyUSB1 — Arty FTDI channel 1; override "
        "via LITEX_HW_PORT).",
    )
    parser.add_argument(
        "--baudrate",
        type=int,
        default=115200,
        help="Serial baudrate (default 115200, matches LiteX UART default).",
    )
    args = parser.parse_args()

    if not PYBOARD.is_file():
        sys.exit(f"pyboard.py not found at {PYBOARD}")
    if importlib.util.find_spec("serial") is None:
        sys.exit("pyserial not installed (pip install pyserial)")
    if not Path(args.port).exists():
        sys.exit(
            f"serial port {args.port} not found — is the board powered up "
            "and the FTDI driver loaded?"
        )

    failures = []
    for test in args.tests:
        if not run_one(args.port, args.baudrate, test):
            failures.append(test)

    if failures:
        print(f"[run_hw] FAILED: {len(failures)}/{len(args.tests)}", file=sys.stderr)
        for t in failures:
            print(f"  - {t}", file=sys.stderr)
        sys.exit(1)
    print(f"[run_hw] OK: {len(args.tests)}/{len(args.tests)}", file=sys.stderr)


if __name__ == "__main__":
    main()
