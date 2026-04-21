# Simple DMA test.
# Requirements (FIXME: Remove/Understand):
#  - --sdram-rate=1:2 for 16-bit buses.
#  - endianness set to "big" on WishboneDMAReader/Writer.
# Copyright (C) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
#
# SPDX-License-Identifier: BSD-2-Clause

import litex

src = memoryview(b'ABCD'*16)
dst = bytearray(b'****'*16)
print(bytearray(src))
print(dst)

dmar = litex.DMAReader(0)
dmaw = litex.DMAWriter(0)
dmar.membuf(src)
dmaw.membuf(dst)


dmar.flush() #any pending cache lines are evicted to RAM
dmaw.start() #writer starts first
assert(src != dst)
assert(not dmaw.done()) #waits reader
dmar.start()


while not dmaw.done():
  print(dmaw.offset())

if src != dst: #maybe cache is still different
  print(dst, "DIFFERENT!") 
dmaw.invalidate()
print(dst)

assert(src == dst)
