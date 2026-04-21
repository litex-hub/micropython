# ADC128S022 SPI ADC demo
# Copyright (C) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
#
# SPDX-License-Identifier: BSD-2-Clause

from umachine import Pin, SoftSPI, SPI
import ustruct as struct

SOFTSPI = False #select SoftSPI or hardware SPI
cs = Pin(3, Pin.OUT)
sck = Pin(4, Pin.OUT)
mosi = Pin(5, Pin.OUT)
miso = Pin(6, Pin.IN)

SPS = 50000 #samples per second, 50K to 200K

if SOFTSPI:
  spi = SoftSPI(baudrate=SPS*16, sck=sck, mosi=mosi, miso=miso)
else:
  spi = SPI(0, baudrate=SPS*16)


cs.off() #CS control is automatic in hardware spi (no need for this)
spi.write(b'\x00') #set next channel to 0
cs.on()

for chan in range(8):
  cs.off() #only for SoftSPI
  nextchan = (chan+1) % 8
  buf = spi.read(2, nextchan*8) #read 2 bytes while sending next channel (2nd sent byte ignored)
  cs.on()
  value, = struct.unpack('>H', buf) #unsigned 16-bit big endian
  print("channel", chan, "=", value)


