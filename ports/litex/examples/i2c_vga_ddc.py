# Reads and decodes DDC (Display Data Channel) information from a PC monitor
# prints native resolution and screen size in cm, using SoftI2C
#
# WIRING:
# -Connect GPIO0[3] to VGA pin 12, or DVI pin 6, or HDMI pin 15
# -Connect GPIO0[4] to VGA pin 15, or DVI pin 7, or HDMI pin 16
#
# Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
# SPDX-License-Identifier: BSD-2-Clause

from umachine import Pin, SoftI2C
scl = Pin(3, Pin.OUT) 
sda = Pin(4, Pin.OUT) 
i2c = SoftI2C(scl, sda, freq=100000)

scan = i2c.scan() 
assert(0x50 in scan)
bytes = i2c.readfrom(0x50, 128)
assert(len(bytes) == 128 and sum(bytes) % 256 == 0) #CRC check

for n in range(1, 5):
  i = n * 18 + 36
  if bytes[i] != 0 or bytes[i+1] != 0:
    b = bytes[i:i+18]
    h = b[2] | ((b[4]&0xF0) << 4)
    v = b[5] | ((b[7]&0xF0) << 4)
    clk = (b[0] | (b[1]<<8)) / 100.0
    print('Native resolution: %dx%d, pixel clock %3.1f MHz' % (h, v, clk))

print('Screen size: %dx%dcm' % (bytes[21], bytes[22]))

