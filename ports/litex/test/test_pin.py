# Simple Pin test.
# Requirements:
# - External loopback between GPIO3/4.
# Copyright (C) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
#
# SPDX-License-Identifier: BSD-2-Clause

from umachine import Pin

# Create Pins.
inp  = Pin(3, Pin.IN)
outp = Pin(4, Pin.OUT)

# Set Output low.
outp.off()

# Read Input value.
a = inp.value()

# Set Output high.
outp.on()

# Read Input value.
b = inp.value()

# Check.
assert(a != b)
print(a, b)


