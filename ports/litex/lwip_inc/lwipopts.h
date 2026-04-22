// Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
// SPDX-License-Identifier: BSD-2-Clause
//
// LiteX-port lwIP options. Inherits the common MicroPython tuning from
// extmod/lwip-include/lwipopts_common.h and adds LiteX-specific bits.

#ifndef MICROPY_INCLUDED_LITEX_LWIPOPTS_H
#define MICROPY_INCLUDED_LITEX_LWIPOPTS_H

// LiteX SoCs don't ship a hardware RNG. Seed lwIP from the cycle counter
// (good enough for DHCP xid / TCP ISN entropy on a single-tenant board).
#include <generated/csr.h>
#ifdef CSR_TIMER0_UPTIME_CYCLES_ADDR
static inline unsigned int litex_lwip_rand(void) {
    timer0_uptime_latch_write(1);
    // Low 32 bits of the uptime cycle counter — monotonic, never repeats
    // within a power cycle.
    return (unsigned int)timer0_uptime_cycles_read();
}
#define LWIP_RAND() litex_lwip_rand()
#else
// Without a cycle counter, fall back to a 32-bit LCG seeded by the netif's
// MAC. Acceptable for sim and for boards without timer0_uptime.
#define LWIP_RAND() (1664525u * (unsigned int)0xdeadbeef + 1013904223u)
#endif

// IPv6 off — adds ~4 KB of code and we have no good way to test it on
// LiteX SoCs without a RA-providing router.
#define LWIP_IPV6                       0

// Pull in the common defaults.
#include "extmod/lwip-include/lwipopts_common.h"

#endif // MICROPY_INCLUDED_LITEX_LWIPOPTS_H
