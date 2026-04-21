/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
 * Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <generated/csr.h>

#ifdef CSR_GPIO_BASE

#include <stdio.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "modmachine.h"
#include "mphalport.h"
#include "extmod/virtpin.h"

// Used to implement a range of pull capabilities
#define GPIO_PULL_DOWN (1)
#define GPIO_PULL_UP   (2)
#define GPIO_PULL_HOLD (4)

#ifdef ESP32
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "machine_rtc.h"
#include "modesp32.h"

#if CONFIG_IDF_TARGET_ESP32
#define GPIO_FIRST_NON_OUTPUT (34)
#elif CONFIG_IDF_TARGET_ESP32S2
#define GPIO_FIRST_NON_OUTPUT (46)
#endif

#else

typedef enum {
 GPIO_MODE_INPUT = 0,
 GPIO_MODE_INPUT_OUTPUT = 1,
 GPIO_MODE_INPUT_OUTPUT_OD = 2,
} GPIO_MODE; //should match CSR constants

typedef mp_hal_pin_obj_t t, gpio_num_t;

static inline void gpio_set_direction(t id, GPIO_MODE mode)
{
  switch(mode)
  {
    case GPIO_MODE_INPUT: 
        mp_hal_pin_input(id);
        break;
    case GPIO_MODE_INPUT_OUTPUT_OD: 
        mp_hal_pin_open_drain(id);
        break;
    case GPIO_MODE_INPUT_OUTPUT: 
        mp_hal_pin_output(id);
        break;
    default:
        mp_raise_ValueError(MP_ERROR_TEXT("mode should be Pin.IN, Pin.OUT or Pin.OPEN_DRAIN"));
  }
}

static inline bool gpio_get_level(t id) { return mp_hal_pin_read(id); }
static inline void gpio_set_level(t id, bool value) { mp_hal_pin_write(id, value); }
static inline void gpio_pulldown_en(t id) { mp_raise_ValueError(MP_ERROR_TEXT("Pulldown not supported")); }
static inline void gpio_pulldown_dis(t id) { }
static inline void gpio_pullup_en(t id) { mp_raise_ValueError(MP_ERROR_TEXT("Pullup not supported")); }
static inline void gpio_pullup_dis(t id) { }
static inline void gpio_hold_en(t id) { mp_raise_ValueError(MP_ERROR_TEXT("Hold not supported")); }
static inline void gpio_hold_dis(t id) {}
#define GPIO_IS_VALID_OUTPUT_GPIO(t) true

#endif

typedef struct _machine_pin_obj_t {
    mp_obj_base_t base;
    gpio_num_t id;
} machine_pin_obj_t;

STATIC const machine_pin_obj_t machine_pin_obj[] = {
    {{&machine_pin_type}, 0},
    {{&machine_pin_type}, 1},
    {{&machine_pin_type}, 2},
    {{&machine_pin_type}, 3},
    {{&machine_pin_type}, 4},
    {{&machine_pin_type}, 5},
    {{&machine_pin_type}, 6},
    {{&machine_pin_type}, 7},
    {{&machine_pin_type}, 8},
    {{&machine_pin_type}, 9},
    {{&machine_pin_type}, 10},
    {{&machine_pin_type}, 11},
    {{&machine_pin_type}, 12},
    {{&machine_pin_type}, 13},
    {{&machine_pin_type}, 14},
    {{&machine_pin_type}, 15},
    {{&machine_pin_type}, 16},
    {{&machine_pin_type}, 17},
    {{&machine_pin_type}, 18},
    {{&machine_pin_type}, 19},
    {{&machine_pin_type}, 20},
    {{&machine_pin_type}, 21},
    {{&machine_pin_type}, 22},
    {{&machine_pin_type}, 23},
    {{&machine_pin_type}, 24},
    {{&machine_pin_type}, 25},
    {{&machine_pin_type}, 26},
    {{&machine_pin_type}, 27},
    {{&machine_pin_type}, 28},
    {{&machine_pin_type}, 29},
    {{&machine_pin_type}, 30},
    {{&machine_pin_type}, 31}, //FIXME: should support the amount defined in CSRs
};

#ifdef ESP32
// forward declaration
STATIC const machine_pin_irq_obj_t machine_pin_irq_object[];

void machine_pins_init(void) {
    static bool did_install = false;
    if (!did_install) {
        gpio_install_isr_service(0);
        did_install = true;
    }
    memset(&MP_STATE_PORT(machine_pin_irq_handler[0]), 0, sizeof(MP_STATE_PORT(machine_pin_irq_handler)));
}
#else
void machine_pins_init(void) {
}
#endif

void machine_pins_deinit(void) {
    for (int i = 0; i < MP_ARRAY_SIZE(machine_pin_obj); ++i) {
        if (machine_pin_obj[i].id != (t)-1) {
#ifdef ESP32
            gpio_isr_handler_remove(machine_pin_obj[i].id);
#endif
        }
    }
}

t machine_pin_get_id(const mp_obj_t pin_in) {
    // If pin is SMALL_INT
    if (mp_obj_is_small_int(pin_in)) {
        mp_hal_pin_obj_t value = MP_OBJ_SMALL_INT_VALUE(pin_in);
        return value;
    }

    if (mp_obj_get_type(pin_in) != &machine_pin_type) {
        mp_raise_ValueError(MP_ERROR_TEXT("expecting a pin"));
    }
    machine_pin_obj_t *self = (machine_pin_obj_t *) pin_in;
    return self->id;
}

STATIC void machine_pin_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_pin_obj_t *self = self_in;
    mp_printf(print, "Pin(%u)", self->id);
}

// pin.init(mode, pull=None, *, value)
STATIC mp_obj_t machine_pin_obj_init_helper(const machine_pin_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_mode, ARG_pull, ARG_value };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode, MP_ARG_OBJ, {.u_obj = mp_const_none}},
        { MP_QSTR_pull, MP_ARG_OBJ, {.u_obj = MP_OBJ_NEW_SMALL_INT(-1)}},
        { MP_QSTR_value, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
#ifdef ESP32
    // reset the pin to digital if this is a mode-setting init (grab it back from ADC)
    if (args[ARG_mode].u_obj != mp_const_none) {
        if (rtc_gpio_is_valid_gpio(self->id)) {
            rtc_gpio_deinit(self->id);
        }
    }

    // configure the pin for gpio
    gpio_pad_select_gpio(self->id);
#endif
    // set initial value (do this before configuring mode/pull)
    if (args[ARG_value].u_obj != MP_OBJ_NULL) {
        gpio_set_level(self->id, mp_obj_is_true(args[ARG_value].u_obj));
    }

    // configure mode
    if (args[ARG_mode].u_obj != mp_const_none) {
        mp_int_t pin_io_mode = mp_obj_get_int(args[ARG_mode].u_obj);
#ifdef ESP32
        if (self->id >= GPIO_PIN_COUNT && (pin_io_mode & GPIO_MODE_DEF_OUTPUT)) {
#else
        if (!GPIO_IS_VALID_OUTPUT_GPIO(self->id) && (pin_io_mode != GPIO_MODE_INPUT)) {
#endif
            mp_raise_ValueError(MP_ERROR_TEXT("pin can only be input"));
        } else {
            gpio_set_direction(self->id, pin_io_mode);
        }
    }

    // configure pull
    if (args[ARG_pull].u_obj != MP_OBJ_NEW_SMALL_INT(-1)) {
        int mode = 0;
        if (args[ARG_pull].u_obj != mp_const_none) {
            mode = mp_obj_get_int(args[ARG_pull].u_obj);
        }
        if (mode & GPIO_PULL_DOWN) {
            gpio_pulldown_en(self->id);
        } else {
            gpio_pulldown_dis(self->id);
        }
        if (mode & GPIO_PULL_UP) {
            gpio_pullup_en(self->id);
        } else {
            gpio_pullup_dis(self->id);
        }
        if (mode & GPIO_PULL_HOLD) {
            gpio_hold_en(self->id);
        } else if (GPIO_IS_VALID_OUTPUT_GPIO(self->id)) {
            gpio_hold_dis(self->id);
        }
    }

    return mp_const_none;
}

// constructor(id, ...)
mp_obj_t mp_pin_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);

    // get the wanted pin object
    int wanted_pin = mp_obj_get_int(args[0]);
    const machine_pin_obj_t *self = NULL;
    if (0 <= wanted_pin && wanted_pin < MP_ARRAY_SIZE(machine_pin_obj)) {
        self = (machine_pin_obj_t *)&machine_pin_obj[wanted_pin];
    }
    if (self == NULL || self->base.type == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid pin"));
    }

    if (n_args > 1 || n_kw > 0) {
        // pin mode given, so configure this GPIO
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
        machine_pin_obj_init_helper(self, n_args - 1, args + 1, &kw_args);
    }

    return MP_OBJ_FROM_PTR(self);
}

// fast method for getting/setting pin value
STATIC mp_obj_t machine_pin_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    machine_pin_obj_t *self = self_in;
    if (n_args == 0) {
        // get pin
        return MP_OBJ_NEW_SMALL_INT(gpio_get_level(self->id));
    } else {
        // set pin
        gpio_set_level(self->id, mp_obj_is_true(args[0]));
        return mp_const_none;
    }
}

// pin.init(mode, pull)
STATIC mp_obj_t machine_pin_obj_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    return machine_pin_obj_init_helper(args[0], n_args - 1, args + 1, kw_args);
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_pin_init_obj, 1, machine_pin_obj_init);

// pin.value([value])
STATIC mp_obj_t machine_pin_value(size_t n_args, const mp_obj_t *args) {
    return machine_pin_call(args[0], n_args - 1, 0, args + 1);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_pin_value_obj, 1, 2, machine_pin_value);

// pin.off()
STATIC mp_obj_t machine_pin_off(mp_obj_t self_in) {
    machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    gpio_set_level(self->id, 0);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_off_obj, machine_pin_off);

// pin.on()
STATIC mp_obj_t machine_pin_on(mp_obj_t self_in) {
    machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    gpio_set_level(self->id, 1);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_on_obj, machine_pin_on);

STATIC const mp_rom_map_elem_t machine_pin_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_pin_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_value), MP_ROM_PTR(&machine_pin_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&machine_pin_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&machine_pin_on_obj) },
#ifdef ESP32
    { MP_ROM_QSTR(MP_QSTR_irq), MP_ROM_PTR(&machine_pin_irq_obj) },
#endif
    // class constants
    { MP_ROM_QSTR(MP_QSTR_IN), MP_ROM_INT(GPIO_MODE_INPUT) },
    { MP_ROM_QSTR(MP_QSTR_OUT), MP_ROM_INT(GPIO_MODE_INPUT_OUTPUT) },
    { MP_ROM_QSTR(MP_QSTR_OPEN_DRAIN), MP_ROM_INT(GPIO_MODE_INPUT_OUTPUT_OD) },
#ifdef GPIO_PULL_UP
    { MP_ROM_QSTR(MP_QSTR_PULL_UP), MP_ROM_INT(GPIO_PULL_UP) },
#endif
#ifdef GPIO_PULL_DOWN
    { MP_ROM_QSTR(MP_QSTR_PULL_DOWN), MP_ROM_INT(GPIO_PULL_DOWN) },
#endif
#ifdef GPIO_PULL_HOLD
    { MP_ROM_QSTR(MP_QSTR_PULL_HOLD), MP_ROM_INT(GPIO_PULL_HOLD) },
#endif
#ifdef GPIO_PIN_INTR_POSEDGE
    { MP_ROM_QSTR(MP_QSTR_IRQ_RISING), MP_ROM_INT(GPIO_PIN_INTR_POSEDGE) },
#endif
#ifdef GPIO_PIN_INTR_NEGEDGE
    { MP_ROM_QSTR(MP_QSTR_IRQ_FALLING), MP_ROM_INT(GPIO_PIN_INTR_NEGEDGE) },
#endif
#ifdef GPIO_PIN_INTR_LOLEVEL
    { MP_ROM_QSTR(MP_QSTR_WAKE_LOW), MP_ROM_INT(GPIO_PIN_INTR_LOLEVEL) },
#endif
#ifdef GPIO_PIN_INTR_HILEVEL
    { MP_ROM_QSTR(MP_QSTR_WAKE_HIGH), MP_ROM_INT(GPIO_PIN_INTR_HILEVEL) },
#endif
};

STATIC mp_uint_t pin_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    (void)errcode;
    machine_pin_obj_t *self = self_in;

    switch (request) {
        case MP_PIN_READ: {
            return gpio_get_level(self->id);
        }
        case MP_PIN_WRITE: {
            gpio_set_level(self->id, arg);
            return 0;
        }
    }
    return -1;
}

STATIC MP_DEFINE_CONST_DICT(machine_pin_locals_dict, machine_pin_locals_dict_table);

STATIC const mp_pin_p_t pin_pin_p = {
    .ioctl = pin_ioctl,
};

const mp_obj_type_t machine_pin_type = {
    { &mp_type_type },
    .name = MP_QSTR_Pin,
    .print = machine_pin_print,
    .make_new = mp_pin_make_new,
    .call = machine_pin_call,
    .protocol = &pin_pin_p,
    .locals_dict = (mp_obj_t)&machine_pin_locals_dict,
};

/******************************************************************************/
// Pin IRQ object

#ifdef ESP32
STATIC void machine_pin_isr_handler(void *arg) {
    machine_pin_obj_t *self = arg;
    mp_obj_t handler = MP_STATE_PORT(machine_pin_irq_handler)[self->id];
    mp_sched_schedule(handler, MP_OBJ_FROM_PTR(self));
    mp_hal_wake_main_task_from_isr();
}

// pin.irq(handler=None, trigger=IRQ_FALLING|IRQ_RISING)
STATIC mp_obj_t machine_pin_irq(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_handler, ARG_trigger, ARG_wake };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_handler, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_trigger, MP_ARG_INT, {.u_int = GPIO_PIN_INTR_POSEDGE | GPIO_PIN_INTR_NEGEDGE} },
        { MP_QSTR_wake, MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };
    machine_pin_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (n_args > 1 || kw_args->used != 0) {
        // configure irq
        mp_obj_t handler = args[ARG_handler].u_obj;
        uint32_t trigger = args[ARG_trigger].u_int;
        mp_obj_t wake_obj = args[ARG_wake].u_obj;

        if ((trigger == GPIO_PIN_INTR_LOLEVEL || trigger == GPIO_PIN_INTR_HILEVEL) && wake_obj != mp_const_none) {
            mp_int_t wake;
            if (mp_obj_get_int_maybe(wake_obj, &wake)) {
                if (wake < 2 || wake > 7) {
                    mp_raise_ValueError(MP_ERROR_TEXT("bad wake value"));
                }
            } else {
                mp_raise_ValueError(MP_ERROR_TEXT("bad wake value"));
            }
            if (machine_rtc_config.wake_on_touch) { // not compatible
                mp_raise_ValueError(MP_ERROR_TEXT("no resources"));
            }

            if (!RTC_IS_VALID_EXT_PIN(self->id)) {
                mp_raise_ValueError(MP_ERROR_TEXT("invalid pin for wake"));
            }

            if (machine_rtc_config.ext0_pin == -1) {
                machine_rtc_config.ext0_pin = self->id;
            } else if (machine_rtc_config.ext0_pin != self->id) {
                mp_raise_ValueError(MP_ERROR_TEXT("no resources"));
            }

            machine_rtc_config.ext0_level = trigger == GPIO_PIN_INTR_LOLEVEL ? 0 : 1;
            machine_rtc_config.ext0_wake_types = wake;
        } else {
            if (machine_rtc_config.ext0_pin == self->id) {
                machine_rtc_config.ext0_pin = -1;
            }

            if (handler == mp_const_none) {
                handler = MP_OBJ_NULL;
                trigger = 0;
            }
            gpio_isr_handler_remove(self->id);
            MP_STATE_PORT(machine_pin_irq_handler)[self->id] = handler;
            gpio_set_intr_type(self->id, trigger);
            gpio_isr_handler_add(self->id, machine_pin_isr_handler, (void *)self);
        }
    }

    // return the irq object
    return MP_OBJ_FROM_PTR(&machine_pin_irq_object[self->id]);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_pin_irq_obj, 1, machine_pin_irq);

STATIC const mp_obj_type_t machine_pin_irq_type;


typedef struct _machine_pin_irq_obj_t {
    mp_obj_base_t base;
    t id;
} machine_pin_irq_obj_t;

STATIC const machine_pin_irq_obj_t machine_pin_irq_object[] = {
    #if CONFIG_IDF_TARGET_ESP32

    {{&machine_pin_irq_type}, 0},
    {{&machine_pin_irq_type}, 1},
    {{&machine_pin_irq_type}, 2},
    {{&machine_pin_irq_type}, 3},
    {{&machine_pin_irq_type}, 4},
    {{&machine_pin_irq_type}, 5},
    {{&machine_pin_irq_type}, 6},
    {{&machine_pin_irq_type}, 7},
    {{&machine_pin_irq_type}, 8},
    {{&machine_pin_irq_type}, 9},
    {{&machine_pin_irq_type}, 10},
    {{&machine_pin_irq_type}, 11},
    {{&machine_pin_irq_type}, 12},
    {{&machine_pin_irq_type}, 13},
    {{&machine_pin_irq_type}, 14},
    {{&machine_pin_irq_type}, 15},
    {{&machine_pin_irq_type}, 16},
    {{&machine_pin_irq_type}, 17},
    {{&machine_pin_irq_type}, 18},
    {{&machine_pin_irq_type}, 19},
    {{NULL}, -1},
    {{&machine_pin_irq_type}, 21},
    {{&machine_pin_irq_type}, 22},
    {{&machine_pin_irq_type}, 23},
    {{NULL}, -1},
    {{&machine_pin_irq_type}, 25},
    {{&machine_pin_irq_type}, 26},
    {{&machine_pin_irq_type}, 27},
    {{NULL}, -1},
    {{NULL}, -1},
    {{NULL}, -1},
    {{NULL}, -1},
    {{&machine_pin_irq_type}, 32},
    {{&machine_pin_irq_type}, 33},
    {{&machine_pin_irq_type}, 34},
    {{&machine_pin_irq_type}, 35},
    {{&machine_pin_irq_type}, 36},
    {{&machine_pin_irq_type}, 37},
    {{&machine_pin_irq_type}, 38},
    {{&machine_pin_irq_type}, 39},

    #elif CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3

    {{&machine_pin_irq_type}, 0},
    {{&machine_pin_irq_type}, 1},
    {{&machine_pin_irq_type}, 2},
    {{&machine_pin_irq_type}, 3},
    {{&machine_pin_irq_type}, 4},
    {{&machine_pin_irq_type}, 5},
    {{&machine_pin_irq_type}, 6},
    {{&machine_pin_irq_type}, 7},
    {{&machine_pin_irq_type}, 8},
    {{&machine_pin_irq_type}, 9},
    {{&machine_pin_irq_type}, 10},
    {{&machine_pin_irq_type}, 11},
    {{&machine_pin_irq_type}, 12},
    {{&machine_pin_irq_type}, 13},
    {{&machine_pin_irq_type}, 14},
    {{&machine_pin_irq_type}, 15},
    {{&machine_pin_irq_type}, 16},
    {{&machine_pin_irq_type}, 17},
    {{&machine_pin_irq_type}, 18},
    #if CONFIG_USB_CDC_ENABLED
    {{NULL}, -1}, // 19 is for native USB D-
    {{NULL}, -1}, // 20 is for native USB D-
    #else
    {{&machine_pin_irq_type}, 19},
    {{&machine_pin_irq_type}, 20},
    #endif
    {{&machine_pin_irq_type}, 21},
    {{NULL}, -1}, // 22 not a pin
    {{NULL}, -1}, // 23 not a pin
    {{NULL}, -1}, // 24 not a pin
    {{NULL}, -1}, // 25 not a pin
    {{NULL}, -1}, // 26 FLASH/PSRAM
    {{NULL}, -1}, // 27 FLASH/PSRAM
    {{NULL}, -1}, // 28 FLASH/PSRAM
    {{NULL}, -1}, // 29 FLASH/PSRAM
    {{NULL}, -1}, // 30 FLASH/PSRAM
    {{NULL}, -1}, // 31 FLASH/PSRAM
    {{NULL}, -1}, // 32 FLASH/PSRAM
    {{&machine_pin_irq_type}, 33},
    {{&machine_pin_irq_type}, 34},
    {{&machine_pin_irq_type}, 35},
    {{&machine_pin_irq_type}, 36},
    {{&machine_pin_irq_type}, 37},
    {{&machine_pin_irq_type}, 38},
    {{&machine_pin_irq_type}, 39},
    {{&machine_pin_irq_type}, 40},
    {{&machine_pin_irq_type}, 41},
    {{&machine_pin_irq_type}, 42},
    {{&machine_pin_irq_type}, 43},
    {{&machine_pin_irq_type}, 44},
    {{&machine_pin_irq_type}, 45},

    #endif
};

STATIC mp_obj_t machine_pin_irq_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    machine_pin_irq_obj_t *self = self_in;
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    machine_pin_isr_handler((void *)&machine_pin_obj[self->id]);
    return mp_const_none;
}

STATIC mp_obj_t machine_pin_irq_trigger(size_t n_args, const mp_obj_t *args) {
    machine_pin_irq_obj_t *self = args[0];
    uint32_t orig_trig = GPIO.pin[self->id].int_type;
    if (n_args == 2) {
        // set trigger
        gpio_set_intr_type(self->id, mp_obj_get_int(args[1]));
    }
    // return original trigger value
    return MP_OBJ_NEW_SMALL_INT(orig_trig);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_pin_irq_trigger_obj, 1, 2, machine_pin_irq_trigger);

STATIC const mp_rom_map_elem_t machine_pin_irq_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_trigger), MP_ROM_PTR(&machine_pin_irq_trigger_obj) },
};
STATIC MP_DEFINE_CONST_DICT(machine_pin_irq_locals_dict, machine_pin_irq_locals_dict_table);

STATIC const mp_obj_type_t machine_pin_irq_type = {
    { &mp_type_type },
    .name = MP_QSTR_IRQ,
    .call = machine_pin_irq_call,
    .locals_dict = (mp_obj_dict_t *)&machine_pin_irq_locals_dict,
};
#endif
#endif // CSR_GPIO_BASE
