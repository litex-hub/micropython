// This file is Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
// This file is Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
// License: BSD-2-Clause
//
// Non-inline HAL functions for the LiteX port. Inline bits
// (mp_hal_stdin_rx_chr, mp_hal_stdout_tx_strn, delay_us_fast, GPIO pin
// helpers) stay in mphalport.h; anything that walks the CSR address space
// or is called from extmod/modtime.c lives here.

#include "py/mphal.h"
#include "shared/timeutils/timeutils.h"
#include "liblitesdk/litesdk_timer.h"

mp_uint_t mp_hal_ticks_us(void) {
    return LITETIMER_PERIOD_FROM_CYCLES64(litex_uptime(), 1000000);
}

mp_uint_t mp_hal_ticks_ms(void) {
    return LITETIMER_PERIOD_FROM_CYCLES64(litex_uptime(), 1000);
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return litex_uptime();
}

uint64_t mp_hal_time_ns(void) {
    return LITETIMER_PERIOD_FROM_CYCLES64(litex_uptime(), 1000000000ull);
}

void mp_hal_delay_ms(mp_uint_t ms) {
    mp_uint_t start = mp_hal_ticks_ms();
    while (mp_hal_ticks_ms() - start < ms) {
    }
}
