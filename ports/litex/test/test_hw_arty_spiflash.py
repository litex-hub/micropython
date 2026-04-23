# Hardware SPI-flash smoke test for Digilent Arty A7.
#
# Requires a SoC built with --with-spi-flash. Carves off the upper half
# of the on-board 16 MiB Quad-SPI flash (well above the bitstream and
# firmware regions) and exercises:
#   * litex.SPIFlash()                 — class exposed when CSR_SPIFLASH_BASE is built
#   * .ioctl(BLOCK_SIZE/COUNT)         — geometry sanity (64 KiB blocks)
#   * .ioctl(INIT)                     — runs liblitespi's spiflash_init
#   * .writeblocks() + .readblocks()   — write+verify on a single 64 KiB sector
#   * .ioctl(BLOCK_ERASE)              — erase + verify all 0xFF
#   * uos.VfsFat.mkfs() + os.mount()   — full FatFs round-trip
#
# Designed for: ports/litex/tools/run_hw.py test/test_hw_arty_spiflash.py
#
# Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

import os
import litex

# Upper 8 MiB of the 16 MiB Arty flash — well above bitstream/firmware.
PARTITION_OFFSET = 0x800000
PARTITION_SIZE = 0x800000

print("=== litex.SPIFlash ===")
f = litex.SPIFlash(offset=PARTITION_OFFSET, size=PARTITION_SIZE)
print(f, "size=%d KiB" % (f.size() // 1024))
f.ioctl(1, 0)  # INIT
bs = f.ioctl(5, 0)
nb = f.ioctl(4, 0)
print("BLOCK_SIZE=%d BLOCK_COUNT=%d" % (bs, nb))
assert bs == 65536, "expected 64 KiB blocks (LiteSPI native erase unit)"
assert nb == PARTITION_SIZE // bs, "block-count math wrong"
print("SPIFlash probe OK")

print("=== block round-trip ===")
SECTOR = 0
PAYLOAD = b"hello SPIFlash from MicroPython!\n"
buf = bytearray(b"\xff" * bs)
for i, b in enumerate(PAYLOAD):
    buf[i] = b
assert f.writeblocks(SECTOR, buf) == 0
back = bytearray(bs)
assert f.readblocks(SECTOR, back) == 0
assert bytes(back[: len(PAYLOAD)]) == PAYLOAD, "readback mismatch"
print("readback OK (%d B)" % len(PAYLOAD))

f.ioctl(6, SECTOR)  # BLOCK_ERASE
f.readblocks(SECTOR, back)
assert all(b == 0xFF for b in back), "erase did not return all 0xFF"
print("erase verified")

print("=== FatFs round-trip ===")
# Tolerate /flash being already mounted from a previous (failed) run.
try:
    os.umount("/flash")
except OSError:
    pass
os.VfsFat.mkfs(f)
os.mount(f, "/flash")
with open("/flash/hello.txt", "w") as fp:
    fp.write("Persistent storage on SPI flash, no SD card required!\n")
with open("/flash/hello.txt") as fp:
    assert "Persistent storage" in fp.read()
assert "hello.txt" in os.listdir("/flash")
os.umount("/flash")
print("FatFs round-trip OK")

print("hw_arty_spiflash OK")
