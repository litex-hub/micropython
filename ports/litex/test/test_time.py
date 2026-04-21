# Simple Time test.
# Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
#
# SPDX-License-Identifier: BSD-2-Clause

import umachine
from utime import *

print("Test ms precision...")
t0 = ticks_ms()
sleep_ms(1000)
t = ticks_diff(ticks_ms(), t0)
assert(t < 1001 and t > 999)
print("ms precision: {}.".format(t/1000))

print("Test us precision...")
t0 = ticks_us()
sleep_us(1000000)
t = ticks_diff(ticks_us(), t0)
assert(t < 1001000 and t > 999000)
print("us precision: {}.".format(t/1000000))

print("Test ns precision...")
t0 = time_ns()
sleep_ms(1000)
t1 = time_ns()
if t1 < t0: t1 += 2**32 # Ticks_diff doesn't work well
t = t1 - t0
assert(t < 1001000000 and t > 999000000)
print("ns precision : {}.".format(t/1000000000))

print("Test Tick CPU prevision...")
f = umachine.freq()
t0 = ticks_cpu()
sleep_us(1000000)
t = ticks_diff(ticks_cpu(), t0)
assert(t > 0.999*f and t < 1.001*f)
print("tick_cpu precision: {}.".format(t/f))
