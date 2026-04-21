# SPI interfacing demo
# Reads and writes a 128-byte SPI EEPROM (93C46 / 93LC46)
# 16-bit word access (ORG=1)
#
# ** NOTE: ERASES ALL DATA **
#
# Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
# SPDX-License-Identifier: BSD-2-Clause

from umachine import Pin, SoftSPI
sck  = Pin(3, Pin.OUT)
mosi = Pin(4, Pin.OUT)
miso = Pin(5, Pin.IN)
cs   = Pin(6, Pin.OUT) # CS is active HIGH (set to 1 to select)

spi = SoftSPI(baudrate=100000, sck=sck, mosi=mosi, miso=miso) #MSB first

#OPCODE			6-bit ADDRESS			DIN	DOUT
READ  = 0b10_000000	# A5 A4   A3 A2 A1 A0	—	D7-D0 ...	Sequential read starting at A5-0
EWEN  = 0b00_110000	#  1  1    X  X  X  X	—	High-Z		Write enable
ERASE = 0b11_000000	# A5 A4   A3 A2 A1 A0	—	RDY/nBSY        Write all ones to A5-A0 address
ERAL  = 0b00_100000	#  1  0    X  X  X  X	—	RDY/nBSY	Erase all (fills with '1'), 8ms typ
WRITE = 0b01_000000	# A5 A4   A3 A2 A1 A0	D15-D0	RDY/nBSY	Write D15-0 to A4-0 address, 4ms typ
WRAL  = 0b00_010000	#  0  1    X  X  X  X	D15-D0	RDY/nBSY	Fills with specified byte, 16ms typ (no erase required)
EWDS  = 0b00_000000	#  0  0    X  X  X  X	— 	High-Z		Write disable
STB   = 0b00_000001	# start bit
AMASK = 0x3F


def activate():
  cs.on()
  spi.write(bytes([STB])) #sends start bit
  t = 0
  while(miso.value() == 0):
   t = t+1;
  return t

def deactivate():
  cs.off() #CS must go low (250 ns) between consecutive instructions


# Read contents
address = 0 #16-bit aligned
cmdrd = bytes([READ | (address & AMASK)])

activate()
spi.write(cmdrd); b = spi.read(128); #input latched at rising edge, output toggles at rising edge
deactivate()

# Write enable
cmde = bytes([EWEN])
activate(); spi.write(cmde); deactivate()

# Write all data
data = 0x4130 #'a0'
cmda = bytes([WRAL, data >> 8, data & 0xFF]) #big endian

activate();
spi.write(cmda);
deactivate(); t = activate(); #CS falling starts, if CS then rises RDY/nBSY signal appears
deactivate()

print("cycles to write ALL data:", t)
assert(t > 0)

# Write a word
address = 2
data = 0xBCDE
cmdw = bytes([WRITE | (address & AMASK), data >> 8, data & 0xFF]) #big endian

activate();
spi.write(cmdw);
deactivate(); t = activate();
deactivate()

print("cycles to write a single word:", t)
assert(t > 0)

# Read contents
address = 0
cmdrd = bytes([READ | (address & AMASK)])

activate()
spi.write(cmdrd); bnew = spi.read(128);
deactivate()

print("Original EEPROM contents:\n", b)
print("New EEPROM contents:\n", bnew)

