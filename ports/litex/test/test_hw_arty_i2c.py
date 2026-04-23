# Hardware I2C smoke test.
#
# Requires a SoC built with SoC.add_i2c_master() wiring up real I2C
# pads. The stock digilent_arty target does NOT do this, so on the
# reference board this test will skip cleanly (machine.I2C won't be
# in the machine module). Meant as a template / regression probe the
# day someone builds a custom target with --with-i2c (or equivalent)
# and wants to sanity-check the driver.
#
# Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

import machine

if not hasattr(machine, "I2C"):
    print("machine.I2C not present — SoC built without add_i2c_master(); skipping")
else:
    print("=== machine.I2C ===")
    i2c = machine.I2C(0, freq=400000)
    print(i2c)
    devices = i2c.scan()
    print("scan found %d device(s):" % len(devices), [hex(a) for a in devices])
    # Basic API shape — don't actually talk to a specific device since we
    # don't know what's on the bus. A user adapting this test should pick
    # an address from `devices` and exercise writeto/readfrom.
    print("hw_arty_i2c OK")
