// Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
// License: BSD-2-Clause
//
// C-level glue between LiteX's isr() entry point and the Python-side
// handler table maintained in modlitex.c. Shared so isr.c can call into
// the dispatcher without pulling in the rest of the litex module.

#ifndef MICROPY_INCLUDED_LITEX_ISR_H
#define MICROPY_INCLUDED_LITEX_ISR_H

#include <stdint.h>
#include "py/obj.h"

// Register a Python handler against a CPU IRQ bit. The handler will be
// scheduled (via mp_sched_schedule) every time isr() fires for this bit.
// Before scheduling, the dispatcher write-1-clears the peripheral's
// ev_pending so the IRQ deasserts; the handler can re-read state via the
// owner object passed back to it.
//
// Passing a None handler removes any existing registration.
void litex_isr_register(int irq_bit, uint32_t ev_pending_addr,
                        mp_obj_t handler, mp_obj_t owner);

// Walk the registration table for the given pending mask and schedule
// matching Python handlers. Called from isr() after the existing C-level
// peripheral handlers run.
void litex_isr_dispatch(uint32_t pending_irqs);

#endif // MICROPY_INCLUDED_LITEX_ISR_H
