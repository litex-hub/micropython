# ADXL345 I2C accelerometer demo (for Terasic DE0-Nano board)
# Implements digital level: lights central leds when
# board is horizontal and displace them when inclined
#
# Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
# SPDX-License-Identifier: BSD-2-Clause

import umachine as machine
import ustruct as struct
import math
import litex

#config
SCLpin = machine.Pin(0, machine.Pin.OUT)
SDApin = machine.Pin(1, machine.Pin.OUT)
CSpin  = machine.Pin(2, machine.Pin.OUT)
ALT_ADDR = True  #should match board configuration (pin high or low)
I2C_FREQ = 100000 #I2C clock speed

#scan with CS HIGH
CSpin.on()
i2c = machine.SoftI2C(scl=SCLpin,sda=SDApin,freq=I2C_FREQ)
scan = i2c.scan()
print("I2C device addresses present: ", [hex(addr) for addr in scan])

#ADXL345 constants
device = 0x1D if ALT_ADDR else 0x53 #reflects ALT addr chip configuration pin
X0_ADDR = 0x32 #X-axis data byte 0 (LSB)
REG_POWER = 0x2D #power control register

#device init
i2c.writeto_mem(device, REG_POWER, b'\x08')

while True:
  #read 3 consecutive 16-bit signed words, little endian
  x, y, z = struct.unpack('3h', i2c.readfrom_mem(device, X0_ADDR, 6))
  roll = math.atan2(y, z) * 180 / math.pi
  pitch = math.atan2(-x, math.sqrt(y*y + z*z)) * 180 / math.pi
  print("roll, pitch (degrees):", int(roll), int(pitch))
  led = 4-int(pitch)
  for i in range(8): #each led means 1-degree
    if i == led or i == led-1: litex.LED(i).on()
    else: litex.LED(i).off()


