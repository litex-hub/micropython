# Simple Timer test.
# Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
#
# SPDX-License-Identifier: BSD-2-Clause

import usys
from umachine import Timer

n      = 0
timer0 = Timer(0)

# Timer Callback

def timer_callback():
	global n
	global timer0
	print("Timer Callback {}".format(n))
	n += 1
	if n >= 8:
		usys.exit(0)

# Test One-Shot mode.

print("Test One-Shot mode...")
timer0.init(freq=0.5, mode=Timer.ONE_SHOT, callback=lambda t: timer_callback())

# Test Periodic mode.

print("Test Periodic mode...")
timer0.init(period=1000, mode=Timer.PERIODIC, callback=lambda t:timer_callback())
