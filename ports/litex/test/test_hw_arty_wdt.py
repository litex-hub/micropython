# Hardware watchdog test for Digilent Arty A7.
#
# Requires a SoC built with --with-watchdog. Exercises:
#   * machine.WDT(timeout=...)         — constructor arms the watchdog
#   * wdt.feed()                       — resets the countdown
#
# We deliberately do NOT exercise the timeout-reboots-the-SoC path here
# because that would yank the REPL out from under run_hw.py mid-test.
# Instead we pick a 500 ms timeout and feed() for ~1 s — if the feed
# works, the SoC stays alive; if it doesn't, run_hw.py will see the REPL
# go away (effectively a self-check on the feed path).
#
# Designed for: ports/litex/tools/run_hw.py test/test_hw_arty_wdt.py
#
# Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

import time
import machine

print("=== machine.WDT ===")
wdt = machine.WDT(timeout=500)
print(wdt)

# Keep feeding for ~1 s. If feed() silently no-ops the SoC will reset at
# the 500 ms mark and the REPL goes away (run_hw.py will time out).
t0 = time.ticks_ms()
feeds = 0
while time.ticks_diff(time.ticks_ms(), t0) < 1000:
    wdt.feed()
    feeds += 1
    time.sleep_ms(100)

print("survived %d ms, %d feeds" % (time.ticks_diff(time.ticks_ms(), t0), feeds))
print("hw_arty_wdt OK")
