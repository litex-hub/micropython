# Hardware Pin.irq() smoke test.
#
# Requires a SoC built with a GPIO core passing with_irq=True to
# GPIOIn / GPIOTristate (so gpio_mode / gpio_edge / gpio_ev_* CSRs
# are generated and GPIO_INTERRUPT is wired). The stock digilent_arty
# target does NOT pass with_irq=True, so on the reference board this
# test will skip cleanly.
#
# Meant as a template / regression probe for when someone builds a
# custom target that enables GPIO interrupts.
#
# Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

import time
import machine

if not hasattr(machine, "Pin") or not hasattr(machine.Pin, "IRQ_RISING"):
    print("machine.Pin.irq not present — SoC built without GPIO with_irq=True; skipping")
else:
    print("=== machine.Pin.irq ===")
    p = machine.Pin(0, machine.Pin.IN)

    fired = [0]

    def on_edge(pin):
        fired[0] += 1

    # Register for both edges. On a real board you'd jumper-wire this
    # pin to an output that toggles; here we just verify the registration
    # path doesn't crash and spin briefly to let any spurious edges fire.
    p.irq(handler=on_edge, trigger=machine.Pin.IRQ_RISING | machine.Pin.IRQ_FALLING)
    time.sleep_ms(100)
    p.irq(None)  # disarm
    print("registration + disarm OK, edges seen: %d" % fired[0])
    print("hw_arty_pin_irq OK")
