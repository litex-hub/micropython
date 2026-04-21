// This file is Copyright (c) 2021 Fupy/LiteX-MicroPython Developers
// This file is Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
// License: BSD-2-Clause

#ifndef MICROPY_INCLUDED_LITEX_MODMACHINE_H
#define MICROPY_INCLUDED_LITEX_MODMACHINE_H

extern const mp_obj_type_t machine_hw_spi_type;
extern const mp_obj_type_t machine_timer_type;
extern const mp_obj_type_t machine_pwm_type;
extern const mp_obj_type_t machine_sdcard_type;
extern const mp_obj_type_t machine_pin_type;

void machine_timer_deinit_all(void);

#endif //MICROPY_INCLUDED_LITEX_MODMACHINE_H
