# machine.UART smoke test for LiteX secondary UARTs.
#
# Exercises the polling and IRQ-registration paths of machine.UART(1).
# The sim's stub UART (added by tools/litex_sim_fast.py --with-uart1)
# has no host-side endpoint, so RX always reports empty — that's still
# enough to verify the CSR layout, write path, and the IRQ glue around
# litex_isr_register.
#
# Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
# SPDX-License-Identifier: BSD-2-Clause

import machine

u = machine.UART(1)
print(u)

# The stub UART's TX never fills, so writes don't block.
u.write(b"hello\n")
u.write(b"world\n")

# RX is always empty on the stub.
assert u.any() is False, "stub UART RX should be empty"

# Constructor input validation (LiteX UART is fixed 8N1).
try:
    machine.UART(1, bits=7)
    raise AssertionError("bits=7 should have raised")
except ValueError:
    pass

try:
    machine.UART(1, parity=0)
    raise AssertionError("parity should have raised")
except ValueError:
    pass

# IRQ registration round-trip — handler doesn't have to fire, we just
# want to verify .irq() doesn't blow up on register/unregister and
# that the bitmask flags are exported.
assert machine.UART.IRQ_RX == 0x2
assert machine.UART.IRQ_TX == 0x1


def cb(uart):
    pass


u.irq(cb, machine.UART.IRQ_RX)
u.irq(None)

print("uart OK")
