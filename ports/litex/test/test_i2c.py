# Simple I2C test
# Copyright (C) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
#
# SPDX-License-Identifier: BSD-2-Clause

from umachine import Pin, SoftI2C

# Create Pins.
scl = Pin(0, Pin.OUT)
sda = Pin(1, Pin.OUT)

# Create I2C.
i2c = SoftI2C(scl, sda, freq=100000)

# Scan I2C.
scan = i2c.scan()
assert(len(scan) != 0)
print(scan)

# Scanned devices can now be read/written.
