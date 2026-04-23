# Hardware SDCard smoke test for Digilent Arty A7.
#
# Requires a SoC built with --with-sdcard --sdcard-adapter=digilent and
# a Digilent PmodSD on connector JD with a FAT/FAT32-formatted microSD
# card inserted.
#
# Exercises:
#   * machine.SDCard()                 — card init + CSD readout
#   * .ioctl(BLOCKSIZE), .ioctl(NUMBLOCKS) — geometry sanity
#   * uos.VfsFat.mkfs is *not* run (so we don't wipe the card); we
#     mount whatever's already there.
#   * uos.mount() / .umount()           — VFS mount/unmount
#   * write a file, read it back, list dir, remove file — round trip
#     through FatFS + the SDCard block driver.
#
# Designed for: ports/litex/tools/run_hw.py test/test_hw_arty_sdcard.py
#
# Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

import os
import machine

print("=== machine.SDCard ===")
sd = machine.SDCard()
bs = sd.ioctl(5, 0)   # MP_BLOCKDEV_IOCTL_BLOCK_SIZE
nb = sd.ioctl(4, 0)   # MP_BLOCKDEV_IOCTL_BLOCK_COUNT
size_mib = (bs * nb) / (1024 * 1024)
print("block_size=%d num_blocks=%d => %.1f MiB" % (bs, nb, size_mib))
assert bs == 512, "expected 512-byte blocks, got %d" % bs
assert nb > 0, "card reports zero blocks — is one inserted?"
print("SDCard probe OK")

print("=== mount /sd ===")
os.mount(sd, "/sd")
print("mounted; listing /sd:")
for name in os.listdir("/sd"):
    print("  ", name)

print("=== file round-trip ===")
fname = "/sd/litex_hw_test.txt"
payload = b"Hello from MicroPython on LiteX/Arty A7!\n"
with open(fname, "wb") as f:
    written = f.write(payload)
assert written == len(payload), "short write %d/%d" % (written, len(payload))

with open(fname, "rb") as f:
    got = f.read()
assert got == payload, "readback mismatch: %r" % got

# Clean up.
os.remove(fname)
assert fname.split("/")[-1] not in os.listdir("/sd"), "remove failed"
print("file round-trip OK (%d bytes)" % len(payload))

os.umount("/sd")
print("hw_arty_sdcard OK")
