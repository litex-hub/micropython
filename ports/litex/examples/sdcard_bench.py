# SD benchmark functions
# sequential and non-sequential access and different block sizes
# Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
#
# SPDX-License-Identifier: BSD-2-Clause

import umachine
import utime

sd = umachine.SDCard()
r = sd.readblocks(0, bytearray(512)) #do a reading for initializing
assert(r)
print(sd)

SIZES = 14
MAXITER = 10
blocks = [2**n for n in range(SIZES)]

def sd_benchmark():
  tims = [0]*SIZES
  for iter in range(MAXITER):
    block = iter*16384 #read different parts of memory
    for n in range(len(tims)):
      buf = bytearray(blocks[n]*512)
      t0 = utime.ticks_us()
      r = sd.readblocks(block, buf)
      #r = True; utime.sleep_us(len(buf)//10) #synthetic test: 10MB/s
      t = utime.ticks_diff(utime.ticks_us(), t0)
      print("read block", block, len(buf), "bytes")
      if r:
        tims[n] += t
      else:
        print("ERROR: block", block)
        return None
  return tims

def sd_benchmark_seq():
  tims = [0]*SIZES
  block = 0
  for n in range(len(tims)):
    for iter in range(MAXITER):
      
      buf = bytearray(blocks[n]*512)
      t0 = utime.ticks_us()
      r = sd.readblocks(block, buf)
      t = utime.ticks_diff(utime.ticks_us(), t0)
      print("read block", block, len(buf), "bytes")
      block += blocks[n]
      if r:
        tims[n] += t
      else:
        print("ERROR: block", block)
        return None
  return tims

def toMBs(tims):
  return {blocks[n]: 512*MAXITER*blocks[n]/tims[n]  for n in range(len(tims))}

print("Reading performance MB/s (non-sequential)...")
mb_per_s = toMBs(sd_benchmark()) #takes about 1 minute
for keys, value in  sorted(mb_per_s.items()):
    print(keys, "\t", value)

print()
print("Reading performance MB/s (sequential)...")
mb_per_s = toMBs(sd_benchmark_seq()) #takes about 1 minute
for keys, value in  sorted(mb_per_s.items()):
    print(keys, "\t", value)


