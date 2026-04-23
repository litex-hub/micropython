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
} GPIO_MODE; // should match CSR constants

// gpio_num_t kept for ESP32 compatibility (this file's heritage); on
// LiteX it's just an alias for the small-int pin id.
typedef mp_hal_pin_obj_t gpio_num_t;

static inline void gpio_set_direction(gpio_num_t id, GPIO_MODE mode) {
    switch (mode)
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

static inline bool gpio_get_level(gpio_num_t id) {
    return mp_hal_pin_read(id);
}
static inline void gpio_set_level(gpio_num_t id, bool value) {
    mp_hal_pin_write(id, value);
}
static inline void gpio_pulldown_en(gpio_num_t id) {
    mp_raise_ValueError(MP_ERROR_TEXT("Pulldown not supported"));
}
static inline void gpio_pulldown_dis(gpio_num_t id) {
}
static inline void gpio_pullup_en(gpio_num_t id) {
    mp_raise_ValueError(MP_ERROR_TEXT("Pullup not supported"));
}
static inline void gpio_pullup_dis(gpio_num_t id) {
}
static inline void gpio_hold_en(gpio_num_t id) {
    mp_raise_ValueError(MP_ERROR_TEXT("Hold not supported"));
}
static inline void gpio_hold_dis(gpio_num_t id) {
}
#define GPIO_IS_VALID_OUTPUT_GPIO(t) true

#endif

typedef struct _machine_pin_obj_t {
    mp_obj_base_t base;
    gpio_num_t id;
} machine_pin_obj_t;

static const machine_pin_obj_t machine_pin_obj[] = {
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
    {{&machine_pin_type}, 31}, // FIXME: should support the amount defined in CSRs
};

#ifdef ESP32
// forward declaration
static const machine_pin_irq_obj_t machine_pin_irq_object[];

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
        if (machine_pin_obj[i].id != (gpio_num_t) - 1) {
            #ifdef ESP32
            gpio_isr_handler_remove(machine_pin_obj[i].id);
            #endif
        }
    }
}

gpio_num_t machine_pin_get_id(const mp_obj_t pin_in) {
    // If pin is SMALL_INT
    if (mp_obj_is_small_int(pin_in)) {
        mp_hal_pin_obj_t value = MP_OBJ_SMALL_INT_VALUE(pin_in);
        return value;
    }

    if (mp_obj_get_type(pin_in) != &machine_pin_type) {
        mp_raise_ValueError(MP_ERROR_TEXT("expecting a pin"));
    }
    machine_pin_obj_t *self = (machine_pin_obj_t *)pin_in;
    return self->id;
}

static void machine_pin_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_pin_obj_t *self = self_in;
    mp_printf(print, "Pin(%u)", self->id);
}

// pin.init(mode, pull=None, *, value)
static mp_obj_t machine_pin_obj_init_helper(const machine_pin_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
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
static mp_obj_t machine_pin_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
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
static mp_obj_t machine_pin_obj_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    return machine_pin_obj_init_helper(args[0], n_args - 1, args + 1, kw_args);
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_pin_init_obj, 1, machine_pin_obj_init);

// pin.value([value])
static mp_obj_t machine_pin_value(size_t n_args, const mp_obj_t *args) {
    return machine_pin_call(args[0], n_args - 1, 0, args + 1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_pin_value_obj, 1, 2, machine_pin_value);

// pin.off()
static mp_obj_t machine_pin_off(mp_obj_t self_in) {
    machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    gpio_set_level(self->id, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_off_obj, machine_pin_off);

// pin.on()
static mp_obj_t machine_pin_on(mp_obj_t self_in) {
    machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    gpio_set_level(self->id, 1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_on_obj, machine_pin_on);

static const mp_rom_map_elem_t machine_pin_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_pin_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_value), MP_ROM_PTR(&machine_pin_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&machine_pin_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&machine_pin_on_obj) },
    #if defined(ESP32) || (defined(CSR_GPIO_EV_ENABLE_ADDR) && defined(GPIO_INTERRUPT))
    { MP_ROM_QSTR(MP_QSTR_irq), MP_ROM_PTR(&machine_pin_irq_obj) },
    #endif
    #if defined(CSR_GPIO_EV_ENABLE_ADDR) && defined(GPIO_INTERRUPT)
    { MP_ROM_QSTR(MP_QSTR_IRQ_RISING),  MP_ROM_INT(LITEX_PIN_IRQ_RISING) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_FALLING), MP_ROM_INT(LITEX_PIN_IRQ_FALLING) },
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

static mp_uint_t pin_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
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

static MP_DEFINE_CONST_DICT(machine_pin_locals_dict, machine_pin_locals_dict_table);

static const mp_pin_p_t pin_pin_p = {
    .ioctl = pin_ioctl,
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_pin_type,
    MP_QSTR_Pin,
    MP_TYPE_FLAG_NONE,
    make_new, mp_pin_make_new,
    print, machine_pin_print,
    call, machine_pin_call,
    protocol, &pin_pin_p,
    locals_dict, &machine_pin_locals_dict
    );

/******************************************************************************/
// Pin IRQ object

#ifdef ESP32
static void machine_pin_isr_handler(void *arg) {
    machine_pin_obj_t *self = arg;
    mp_obj_t handler = MP_STATE_PORT(machine_pin_irq_handler)[self->id];
    mp_sched_schedule(handler, MP_OBJ_FROM_PTR(self));
    mp_hal_wake_main_task_from_isr();
}

// pin.irq(handler=None, trigger=IRQ_FALLING|IRQ_RISING)
static mp_obj_t machine_pin_irq(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
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
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_pin_irq_obj, 1, machine_pin_irq);

static const mp_obj_type_t machine_pin_irq_type;


typedef struct _machine_pin_irq_obj_t {
    mp_obj_base_t base;
    gpio_num_t id;
} machine_pin_irq_obj_t;

static const machine_pin_irq_obj_t machine_pin_irq_object[] = {
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

static mp_obj_t machine_pin_irq_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    machine_pin_irq_obj_t *self = self_in;
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    machine_pin_isr_handler((void *)&machine_pin_obj[self->id]);
    return mp_const_none;
}

static mp_obj_t machine_pin_irq_trigger(size_t n_args, const mp_obj_t *args) {
    machine_pin_irq_obj_t *self = args[0];
    uint32_t orig_trig = GPIO.pin[self->id].int_type;
    if (n_args == 2) {
        // set trigger
        gpio_set_intr_type(self->id, mp_obj_get_int(args[1]));
    }
    // return original trigger value
    return MP_OBJ_NEW_SMALL_INT(orig_trig);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_pin_irq_trigger_obj, 1, 2, machine_pin_irq_trigger);

static const mp_rom_map_elem_t machine_pin_irq_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_trigger), MP_ROM_PTR(&machine_pin_irq_trigger_obj) },
};
static MP_DEFINE_CONST_DICT(machine_pin_irq_locals_dict, machine_pin_irq_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    machine_pin_irq_type,
    MP_QSTR_IRQ,
    MP_TYPE_FLAG_NONE,
    call, machine_pin_irq_call,
    locals_dict, &machine_pin_irq_locals_dict
    );

// ---------------------------------------------------------------------------
// LiteX Pin.irq() — bridges LiteX GPIO's EventManager (enabled when the
// SoC is built with with_irq=True on its GPIOIn / GPIOTristate) into
// MicroPython's standard machine.Pin.irq(handler, trigger) API.
//
// LiteX GPIO IRQ model (from litex/soc/cores/gpio.py::_GPIOIRQ.add_irq):
//   gpio_mode[n]        0 = edge-triggered, 1 = any-change (level flip)
//   gpio_edge[n]        edge mode only; 0 = rising, 1 = falling
//   gpio_ev_pending[n]  W1C on trigger
//   gpio_ev_enable[n]   mask bit
//   GPIO_INTERRUPT      single CPU IRQ bit shared by all pins
//
// One CPU IRQ, many pins → we maintain a per-pin Python-handler table
// and fan out from a C dispatcher called by isr.c on every GPIO IRQ.
//
// Build-time gating: the mode/edge/ev_* CSRs only exist when with_irq=True
// on the LiteX side. CSR_GPIO_EV_ENABLE_ADDR is the canonical "GPIO IRQs
// are wired" marker; GPIO_INTERRUPT is the CPU-level IRQ number.
//
// NOTE (scope of this change): the code below compiles against any
// with_irq=True GPIO SoC but was not verified on real hardware in the
// session that added it (the Arty's stock digilent_arty target doesn't
// expose a GPIO with IRQ). Expect to iterate the first time a user tries
// it on a board that has one.
#elif defined(CSR_GPIO_EV_ENABLE_ADDR) && defined(GPIO_INTERRUPT)

// Trigger flags — bitmask, so (IRQ_RISING | IRQ_FALLING) == both-edges.
#define LITEX_PIN_IRQ_RISING  0x01
#define LITEX_PIN_IRQ_FALLING 0x02

// 32 matches the static machine_pin_obj[] at the top of this file.
#define LITEX_PIN_COUNT 32

// Per-pin Python handler table. Root pointers so GC doesn't collect the
// callables while a pin is armed.
typedef struct _litex_pin_handler_t {
    mp_obj_t handler;
    mp_obj_t owner;     // Pin object passed as the single arg to the handler
} litex_pin_handler_t;

MP_REGISTER_ROOT_POINTER(litex_pin_handler_t machine_pin_handlers[LITEX_PIN_COUNT]);

// Called from isr.c on every GPIO IRQ. Walks ev_pending, schedules each
// pending pin's handler, then W1Cs all the pending bits we handled.
void machine_pin_isr_dispatch(void) {
    uint32_t pending = gpio_ev_pending_read();
    if (pending == 0) {
        return;
    }
    for (int i = 0; i < LITEX_PIN_COUNT; i++) {
        if (!(pending & (1u << i))) {
            continue;
        }
        mp_obj_t h = MP_STATE_PORT(machine_pin_handlers)[i].handler;
        if (h != MP_OBJ_NULL && h != mp_const_none) {
            mp_sched_schedule(h, MP_STATE_PORT(machine_pin_handlers)[i].owner);
        }
    }
    // W1C every bit we just processed so the IRQ line drops.
    gpio_ev_pending_write(pending);
}

static mp_obj_t machine_pin_irq_litex(size_t n_args, const mp_obj_t *pos_args,
    mp_map_t *kw_args) {
    enum { ARG_handler, ARG_trigger };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_handler, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_trigger, MP_ARG_INT,
          {.u_int = LITEX_PIN_IRQ_RISING | LITEX_PIN_IRQ_FALLING} },
    };
    machine_pin_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if ((uint32_t)self->id >= LITEX_PIN_COUNT) {
        mp_raise_ValueError(MP_ERROR_TEXT("pin id out of range for IRQ table"));
    }
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_obj_t handler = args[ARG_handler].u_obj;
    uint32_t trigger = args[ARG_trigger].u_int;
    uint32_t bit = 1u << self->id;

    // Mask this pin's event while we rewrite config so we don't race a
    // transition between the mode/edge change and the ev_enable flip.
    gpio_ev_enable_write(gpio_ev_enable_read() & ~bit);

    if (handler == mp_const_none) {
        MP_STATE_PORT(machine_pin_handlers)[self->id].handler = MP_OBJ_NULL;
        MP_STATE_PORT(machine_pin_handlers)[self->id].owner   = MP_OBJ_NULL;
        return mp_const_none;
    }

    // Decode trigger → (mode, edge) bits in the shared gpio_mode / gpio_edge
    // registers. Both-edges is the "change" mode; single-edge modes use
    // mode=0 and select the polarity via edge.
    uint32_t mode_bits = gpio_mode_read();
    uint32_t edge_bits = gpio_edge_read();
    if ((trigger & (LITEX_PIN_IRQ_RISING | LITEX_PIN_IRQ_FALLING))
        == (LITEX_PIN_IRQ_RISING | LITEX_PIN_IRQ_FALLING)) {
        mode_bits |= bit;                       // any-change
    } else if (trigger & LITEX_PIN_IRQ_FALLING) {
        mode_bits &= ~bit;
        edge_bits |= bit;                       // falling-only
    } else if (trigger & LITEX_PIN_IRQ_RISING) {
        mode_bits &= ~bit;
        edge_bits &= ~bit;                      // rising-only
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("trigger must include IRQ_RISING and/or IRQ_FALLING"));
    }
    gpio_mode_write(mode_bits);
    gpio_edge_write(edge_bits);

    MP_STATE_PORT(machine_pin_handlers)[self->id].handler = handler;
    MP_STATE_PORT(machine_pin_handlers)[self->id].owner   = pos_args[0];

    // Clear any stale pending, then unmask.
    gpio_ev_pending_write(bit);
    gpio_ev_enable_write(gpio_ev_enable_read() | bit);

    // Make sure the CPU-level GPIO IRQ is unmasked too (harmless if
    // already on; idempotent).
    #ifdef CONFIG_CPU_HAS_INTERRUPT
    irq_setmask(irq_getmask() | (1u << GPIO_INTERRUPT));
    #endif
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_pin_irq_obj, 1, machine_pin_irq_litex);

#endif
#endif // CSR_GPIO_BASE
