// This file is Copyright (c) 2017-2021 Fupy/LiteX-MicroPython Developers
// This file is Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
// License: BSD-2-Clause

#include <generated/csr.h>
#include <irq.h>
#include <uart.h>
#include <timer.h>

#ifdef CONFIG_CPU_HAS_INTERRUPT
void isr(void)
{
	unsigned int irqs;

	irqs = irq_pending() & irq_getmask();
#ifdef TIMER0_INTERRUPT
#ifndef TIMER0_POLLING
	if(irqs & (1 << TIMER0_INTERRUPT))
		timer0_isr();
#endif
#endif

#ifdef UART_INTERRUPT
	if(irqs & (1 << UART_INTERRUPT))
		uart_isr();
#endif
}

#else
#warning SoC should have interrupts enabled
void isr(void) {}
#endif
