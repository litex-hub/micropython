# Write to SD card demo, raw mode (see FAT_write for filesystem mode)
# reads a zero-filled sector, changes first and bytes, writes and checks
# then restore values
# Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
#
# SPDX-License-Identifier: BSD-2-Clause

from umachine import SDCard
sd = SDCard()
"size " + str(sd.info()[0]/1e9) + " GB"

buf = bytearray(512)
block = 3 #usually zero filled

r = sd.readblocks(block, buf)
assert(r and set(buf) == {0})
buf0, bufL = buf[0], buf[-1] #save values
buf[0] = 0x01
buf[-1] = 0xFF

w = sd.writeblocks(block, buf)
assert(w)
r = sd.readblocks(block, buf)
assert(r and buf[0] == 0x01 and buf[-1] == 0xFF)

buf[0], buf[-1] = buf0, bufL
w = sd.writeblocks(block, buf)
assert(w)

r = sd.readblocks(block, buf)
assert(r and set(buf) == {0})
 

