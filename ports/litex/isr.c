// This file is Copyright (c) 2017-2021 Fupy/LiteX-MicroPython Developers
// This file is Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
// This file is Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
// License: BSD-2-Clause

#include <generated/csr.h>
#include <irq.h>
#include <uart.h>
#include <timer.h>

#include "litex_isr.h"

#ifdef CONFIG_CPU_HAS_INTERRUPT
void isr(void) {
    unsigned int irqs = irq_pending() & irq_getmask();

    // C-level handlers first: the BIOS UART RX FIFO and (optionally) the
    // legacy timer0 path own these unconditionally so libbase keeps
    // working under us.
    #ifdef TIMER0_INTERRUPT
    #ifndef TIMER0_POLLING
    if (irqs & (1 << TIMER0_INTERRUPT)) {
        timer0_isr();
    }
    #endif
    #endif

    #ifdef UART_INTERRUPT
    if (irqs & (1 << UART_INTERRUPT)) {
        uart_isr();
    }
    #endif

    // GPIO has a per-pin fan-out that doesn't fit the single-handler
    // litex_isr_register model (one CPU IRQ bit, many registered pin
    // handlers). When present, route the GPIO IRQ through its dedicated
    // dispatcher in machine_pin.c before the generic path.
    #if defined(GPIO_INTERRUPT) && defined(CSR_GPIO_EV_ENABLE_ADDR)
    if (irqs & (1 << GPIO_INTERRUPT)) {
        extern void machine_pin_isr_dispatch(void);
        machine_pin_isr_dispatch();
    }
    #endif

    // Then dispatch any IRQ bits with a registered Python handler. The
    // dispatcher write-1-clears each peripheral's ev_pending so the IRQ
    // line drops, and schedules the handler via mp_sched_schedule for
    // execution back in the main task context.
    litex_isr_dispatch(irqs);
}

#else
#warning SoC should have interrupts enabled
void isr(void) {
}
#endif
