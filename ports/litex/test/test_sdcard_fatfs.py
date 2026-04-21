# Simple SDCard/FatFS test.
# SDCard can be in SPI or SD (4-bit) mode.
# Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
#
# SPDX-License-Identifier: BSD-2-Clause

import uos as os
from umachine import SDCard

# Open SDCard.
sd = SDCard()
print("SDCard size: {}GB.".format(sd.info()[0]/1e9))

# Mount FileSystem.
vfs = os.VfsFat(sd)
os.mount(vfs, "/sd")
os.chdir("/sd")

# List files.
print("List files:")
prev_list = os.listdir()
print(prev_list)

# Create test file.
test_filename = "test_fatfs.txt"
print("Create {} file.".format(test_filename))

# Write contents to it.
with open(test_filename, "wt") as f:
  f.write("Hello World from Micropython running on your LiteX SoC!\n")

# List files.
print("List files:")
curr_list = os.listdir()
print(curr_list)

assert (prev_list != curr_list)

# Read test file contents.
with open(test_filename, "rt") as f:
  for l in f:
    print(l.rstrip())

# Remove test file.
os.remove(test_filename)

assert (prev_list == os.listdir())
