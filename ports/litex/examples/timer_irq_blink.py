# litex.EventManager + machine.Timer demo: blink LED 0 at exactly 2 Hz
# from a periodic timer IRQ dispatched into a Python callback.
#
# Same plumbing test_hw_arty.py uses for the timer-IRQ check, but in the
# user-facing form: arm the timer with reload != 0 (periodic mode), wire
# its EventManager to a Python handler, and let the C-level isr() bridge
# in litex_isr.h schedule the callback via mp_sched_schedule.
#
# Requires a SoC built with --timer-uptime (default in litex_boards
# Arty target). Visible LED requires --with-led-chaser or any GPIO with
# a CSR_LEDS_BASE.
#
# Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

import time
import litex

LED_INDEX = 0
HZ = 2

led = litex.LED(LED_INDEX)
state = [0]

# Arm timer0 in periodic mode at HZ. CONFIG_CLOCK_FREQUENCY ticks/s.
period = litex.sys_clk_freq // (HZ * 2)  # toggle each half-period
litex.csr_write("timer0_load", 0)
litex.csr_write("timer0_reload", period)
litex.csr_write("timer0_en", 1)


def on_tick(owner):
    state[0] ^= 1
    if state[0]:
        led.on()
    else:
        led.off()


ev = litex.EventManager("timer0")
ev.irq(on_tick)
ev.enable(1)

# Idle the main task — the IRQ handler does the work.
print("Blinking LED %d at %d Hz from timer IRQ. Ctrl-C to stop." % (LED_INDEX, HZ))
try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    pass
finally:
    ev.irq(None)
    litex.csr_write("timer0_en", 0)
    led.off()
