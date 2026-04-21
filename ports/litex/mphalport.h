// This file is Copyright (c) 2017-2021 Fupy/LiteX-MicroPython Developers
// This file is Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
// License: BSD-2-Clause

#ifndef INCLUDED_MPHALPORT_H
#define INCLUDED_MPHALPORT_H

#include <uart.h>
#include "lib/utils/interrupt_char.h"

// Receive single character, blocking until one is available.
static inline int mp_hal_stdin_rx_chr(void) {
    char c;
    c = uart_read();
    /* \n to \r conversion for litex_term */
    if (c == '\n')
        return '\r';
    else
        return c;
}

// Send the string of given length.
static inline void mp_hal_stdout_tx_strn(const char *str, mp_uint_t len) {
    while (len--) {
        uart_write(*str++);
    }
}


// C-level pin HAL
#include "py/obj.h"
#include "liblitesdk/litesdk_gpio.h"

#define MP_HAL_PIN_FMT "%u"
#define mp_hal_pin_name(p)      (p)
#define mp_hal_pin_obj_t litegpio_pin_t


#ifdef LITEGPIO_ENABLED

//these macros reference main GPIO instance
#define mp_hal_pin_input(p)      litegpio_mode_input(litegpio0, p)
#define mp_hal_pin_output(p)     litegpio_mode_output(litegpio0, p)
#define mp_hal_pin_low(p)        litegpio_set_low(litegpio0, p)
#define mp_hal_pin_high(p)       litegpio_set_high(litegpio0, p)
#define mp_hal_pin_read(p)       litegpio_read(litegpio0, p)
#define mp_hal_pin_write(p, v)   litegpio_write(litegpio0, p, v)

#define mp_hal_pin_open_drain(p) litegpio_mode_open_drain(litegpio0, p)
#define mp_hal_pin_od_low(p)     litegpio_od_low(litegpio0, p)
#define mp_hal_pin_od_high(p)    litegpio_od_high(litegpio0, p)



mp_hal_pin_obj_t machine_pin_get_id(const mp_obj_t pin_in);
#define mp_hal_get_pin_obj(o) machine_pin_get_id(o)
//#define machine_pin_get_id(o) mp_hal_get_pin_obj(o) //deprecated


#else

//no GPIO enabled in SoC definition
#define mp_hal_pin_input(p) (void)(p)
#define mp_hal_pin_output(p) (void)(p)
#define mp_hal_pin_open_drain(p) (void)(p)
#define mp_hal_pin_od_low(p) (void)(p)
#define mp_hal_pin_od_high(p) (void)(p)
#define mp_hal_get_pin_obj(p) (-1) //no pins possible
#define mp_hal_pin_read(p) 0
#define mp_hal_pin_write(p, v) (void)(p)

#endif

#endif // INCLUDED_MPHALPORT_H
