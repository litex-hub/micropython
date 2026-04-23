# Persistent boot counter on the LiteSPI flash.
#
# Mounts /flash from the upper 8 MiB of the Arty's 16 MiB Quad-SPI flash
# (safely above the bitstream and firmware regions), reads `count.txt`,
# prints + increments the counter, and writes it back. Survives power
# cycles — no SD card required.
#
# Run once to create the file; subsequent boots show the count climbing.
# First run auto-runs mkfs if the partition has never been formatted; if
# you have unrelated data on the partition you'd rather keep, set
# MKFS_IF_MISSING = False.
#
# Requires a SoC built with --with-spi-flash.
#
# Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

import os
import litex

MKFS_IF_MISSING = True
PARTITION_OFFSET = 0x800000  # 8 MiB into flash — above firmware/bitstream
PARTITION_SIZE = 0x800000  # upper 8 MiB of a 16 MiB device

flash = litex.SPIFlash(offset=PARTITION_OFFSET, size=PARTITION_SIZE)
flash.ioctl(1, 0)  # INIT — runs liblitespi's spiflash_init

try:
    os.mount(flash, "/flash")
except OSError:
    # /flash was left mounted by a previous (failed) run, OR there's no
    # valid FatFs on the partition yet.
    try:
        os.umount("/flash")
        os.mount(flash, "/flash")
    except OSError:
        if not MKFS_IF_MISSING:
            raise
        print("no filesystem on /flash — running mkfs (one-time, ~60 erases)")
        os.VfsFat.mkfs(flash)
        os.mount(flash, "/flash")

try:
    with open("/flash/count.txt") as f:
        count = int(f.read().strip())
except (OSError, ValueError):
    count = 0

count += 1
with open("/flash/count.txt", "w") as f:
    f.write(str(count))

print("boot #%d (persistent)" % count)
os.umount("/flash")
