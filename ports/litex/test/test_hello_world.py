# Simple Raw REPL test.
# Copyright (C) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
#
# SPDX-License-Identifier: BSD-2-Clause

# Python script can be executed:
# - through pyboard: python3 ../../../tools/pyboard.py -d /dev/ttyUSBX test_xy.py
# - copied to board flash storage and configured to be run at boot.

print("Hello World from Micropython running on your LiteX SoC!")
