# Simple Led test.
# Copyright (C) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
#
# SPDX-License-Identifier: BSD-2-Clause

import utime
from litex import LED

# Create Led.
led = LED(0)

# Set Led On.
led.on()

# Toggle Led every 200ms.
for i in range(8):
	utime.sleep(0.2)
	led.toggle()

# Set Led Off.
led.off()
