# IRQ-driven Python callback smoke test.
#
# Sets up timer0 in one-shot mode, registers a litex.EventManager handler
# for it, lets the handler fire, then asserts it ran exactly once. Hits
# the full path: peripheral event -> CPU IRQ -> isr() in C ->
# litex_isr_dispatch() -> mp_sched_schedule() -> Python callback in
# main-task context.
#
# Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

import litex

# One-shot timer: load=100, reload=0 means "count 100 cycles then stop".
# That gives us exactly one event to dispatch.
litex.csr_write("timer0_load", 100)
litex.csr_write("timer0_reload", 0)
litex.csr_write("timer0_en", 1)

ev = litex.EventManager("timer0")

n_fired = [0]


def handler(owner):
    n_fired[0] += 1


ev.irq(handler)
ev.enable(1)

# Spin so the scheduled callback gets a chance to run between bytecodes.
# 1000 nop iterations is well over a millisecond on any real CPU and
# plenty of simulated time on the sim.
for _ in range(1000):
    pass

ev.irq(None)  # unregister + mask the IRQ at the CPU.

assert n_fired[0] == 1, "expected exactly one IRQ, got %d" % n_fired[0]
print("irq OK")
