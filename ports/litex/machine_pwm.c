/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
 * Copyright (c) 2021 Fupy/LiteX-MicroPython Developers
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

#include <stdio.h>
#ifdef ESP32
#include "driver/ledc.h"
#include "esp_err.h"
#endif

#include "py/nlr.h"
#include "py/runtime.h"
#include "modmachine.h"
#include "mphalport.h"

#ifdef CSR_LEDS_PWM_ENABLE_ADDR //TODO: change led control to general PWM array when implemented
// Forward dec'l
extern const mp_obj_type_t machine_pwm_type;

typedef struct _pwm_obj_t {
    mp_obj_base_t base;
    mp_hal_pin_obj_t pin;
    uint8_t active;
    uint8_t channel;
} pwm_obj_t;
#endif

#ifdef ESP32 //see notes in machine_hw_spi.c about reusing code from ESP32 port
// Which channel has which GPIO pin assigned?
// (-1 if not assigned)
#define PWM_CHANNEL_MAX LEDC_CHANNEL_MAX

// Params for PW operation
// 5khz
#define PWFREQ (5000)
// High speed mode
#if CONFIG_IDF_TARGET_ESP32
#define PWMODE (LEDC_HIGH_SPEED_MODE)
#else
#define PWMODE (LEDC_LOW_SPEED_MODE)
#endif
// 10-bit resolution (compatible with esp8266 PWM)
#define PWRES (LEDC_TIMER_10_BIT)
// Timer 1
#define PWTIMER (LEDC_TIMER_1)

// Config of timer upon which we run all PWM'ed GPIO pins
STATIC ledc_timer_config_t timer_cfg = {
    .duty_resolution = PWRES,
    .freq_hz = PWFREQ,
    .speed_mode = PWMODE,
    .timer_num = PWTIMER
};
#else

//TODO: these macros could be more general
#define LITEX_FREQ_TO_CYCLES(f) (CONFIG_CLOCK_FREQUENCY/(f))
#define LITEX_CYCLES_TO_FREQ(c) (CONFIG_CLOCK_FREQUENCY/(c))
#define LITEX_NS_TO_CYCLES(n) (((n)*(CONFIG_CLOCK_FREQUENCY/1000)*65536/1000000)/65536) //for large values use 64-bit
#define LITEX_NS_TO_CYCLES_64BIT(n) ((uint64_t)(n)*CONFIG_CLOCK_FREQUENCY/1000000000) //LITEX_NS_TO_CYCLES((uint64_t)n)
#define LITEX_CYCLES_TO_NS(c) ((1000000000/(CONFIG_CLOCK_FREQUENCY/65536))*(c)/65536) //for large values use 64-bit
#define LITEX_CYCLES_TO_NS_64BIT(c) (1000000000*(uint64_t)(c)/CONFIG_CLOCK_FREQUENCY) //LITEX_CYCLES_TO_NS((uint64_t)c)
#define DUTY16_TO_CYCLES(d, period) (period*(uint64_t)(d)/65536)
#define CYCLES_TO_DUTY16(w, period) (uint16_t)(65536*(uint64_t)(w)/period)

#ifdef CSR_LEDS_PWM_ENABLE_ADDR
#define LEDS_VIRTUAL_PIN (-1)
//todo: support pwm pins
#define PWM_CHANNEL_MAX 1
#define pwm_internal_enable()            litex_pwm_enable()
#define pwm_internal_disable()           litex_pwm_disable()
#define pwm_internal_set_period(pin, v)  litex_pwm_set_period(pin, v)
#define pwm_internal_get_period(pin)     litex_pwm_get_period(pin)
#define pwm_internal_set_width(pin, v)   litex_pwm_set_width(pin, v)
#define pwm_internal_get_width(pin)      litex_pwm_get_width(pin)
#define pwm_internal_init_pin(pin) //TODO: implement
#define pwm_internal_deinit_pin(pin) //TODO: implement

//C API
static inline void litex_pwm_enable(void) { leds_pwm_enable_write(1); }
static inline void litex_pwm_disable(void) { leds_pwm_enable_write(0); }
static inline void litex_pwm_set_period(int pin, uint32_t v) { leds_pwm_period_write(v); }
static inline uint32_t litex_pwm_get_period(int pin) { return leds_pwm_period_read(); }
static inline void litex_pwm_set_width(int pin, uint32_t v) { leds_pwm_width_write(v); }
static inline uint32_t litex_pwm_get_width(int pin) { return leds_pwm_width_read(); }


#endif //CSR_LEDS_PWM_ENABLE_ADDR

#endif

#ifdef PWM_CHANNEL_MAX //only enable if used

STATIC bool pwm_inited = false;
STATIC int chan_gpio[PWM_CHANNEL_MAX];

STATIC void pwm_init_internal(void) {
#ifdef _DEBUG
    printf("in pwm_init_internal: PWM disable\n");
#endif

    // Initial condition: no channels assigned
    for (int x = 0; x < PWM_CHANNEL_MAX; ++x) {
        chan_gpio[x] = -1;
    }

    // Init with default timer params
#ifdef ESP32
    ledc_timer_config(&timer_cfg);
#else
    pwm_internal_disable(); //PWM starts disabled
#endif
}

STATIC int set_freq(int pin, int newval) {
    if (newval <= 0) {
        newval = 1;
    }
#ifdef ESP32
    // Find the highest bit resolution for the requested frequency
    int ores = timer_cfg.duty_resolution;
    int oval = timer_cfg.freq_hz;
    unsigned int res = 0;
    for (unsigned int i = LEDC_APB_CLK_HZ / newval; i > 1; i >>= 1, ++res) {
    }
    if (res == 0) {
        res = 1;
    } else if (res > PWRES) {
        // Limit resolution to PWRES to match units of our duty
        res = PWRES;
    }

    // Configure the new resolution and frequency
    timer_cfg.duty_resolution = res;
    timer_cfg.freq_hz = newval;
    if (ledc_timer_config(&timer_cfg) != ESP_OK) {
        timer_cfg.duty_resolution = ores;
        timer_cfg.freq_hz = oval;
        return 0;
    }
#else
    pwm_internal_set_period(pin, LITEX_FREQ_TO_CYCLES(newval));
#endif
    return 1;
}

/******************************************************************************/
// MicroPython bindings for PWM

STATIC void pwm_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "PWM(%u", self->pin);
    if (self->active) {
#ifdef ESP32
        mp_printf(print, ", freq=%u, duty=%u", timer_cfg.freq_hz,
            ledc_get_duty(PWMODE, self->channel));
#else
        mp_printf(print, ", period=%u cycles, duty=%u cycles", pwm_internal_get_period(self->pin), pwm_internal_get_width(self->pin));
#endif
    }
    mp_printf(print, ")");
}

STATIC void pwm_init_helper(pwm_obj_t *self,
    size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_freq, ARG_duty };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_freq, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_duty, MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int channel;
    int avail = -1;

    // Find a free PWM channel, also spot if our pin is
    //  already mentioned.
    for (channel = 0; channel < PWM_CHANNEL_MAX; ++channel) {
        if (chan_gpio[channel] == self->pin) {
            break;
        }
        if ((avail == -1) && (chan_gpio[channel] == -1)) {
            avail = channel;
        }
    }

    if (channel >= PWM_CHANNEL_MAX) {
        if (avail == -1) {
            mp_raise_ValueError(MP_ERROR_TEXT("out of PWM channels"));
        }
        channel = avail;
    }
    self->channel = channel;

    // New PWM assignment
    self->active = 1;
    int tval = args[ARG_freq].u_int;
    int dval = args[ARG_duty].u_int;
    if (chan_gpio[channel] == -1) {
#ifdef ESP32
        ledc_channel_config_t cfg = {
            .channel = channel,
            .duty = (1 << timer_cfg.duty_resolution) / 2,
            .gpio_num = self->pin,
            .intr_type = LEDC_INTR_DISABLE,
            .speed_mode = PWMODE,
            .timer_sel = PWTIMER,
        };
        if (ledc_channel_config(&cfg) != ESP_OK) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("PWM not supported on pin %d"), self->pin);
        }
#else
        pwm_internal_init_pin(self->pin);
#endif
        chan_gpio[channel] = self->pin;
    }

#ifdef _DEBUG
    printf("in pwm_init_helper, channel=%d, pin=%d, tval=%d, dval=%d\n", self->channel, self->pin, tval, dval);
#endif

    // Maybe change PWM timer
    if (tval != -1) {
#ifdef ESP32
        if (tval != timer_cfg.freq_hz)
#endif
        {
            if (!set_freq(self->pin, tval)) {
                mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("bad frequency %d"), tval);
            }
        }
    }

    // Set duty cycle?
    if (dval != -1) {
#ifdef ESP32
        dval &= ((1 << PWRES) - 1);
        dval >>= PWRES - timer_cfg.duty_resolution;
        ledc_set_duty(PWMODE, channel, dval);
        ledc_update_duty(PWMODE, channel);
#else
        pwm_internal_set_width(self->pin, LITEX_NS_TO_CYCLES_64BIT(dval));
#endif
    }

#ifdef _DEBUG
    printf("PWM enable\n");
#endif
    pwm_internal_enable(); //always enable PWM even if frequency or duty cycle not set (will use hardware defaults)
}


STATIC mp_obj_t pwm_make_new(const mp_obj_type_t *type,
    size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);
    mp_hal_pin_obj_t pin_id = machine_pin_get_id(args[0]);

#ifdef _DEBUG
    printf("in pwm_make_new, pin=%d\n", pin_id);
#endif

    // create PWM object from the given pin
    pwm_obj_t *self = m_new_obj(pwm_obj_t);
    self->base.type = &machine_pwm_type;
    self->pin = pin_id;
    self->active = 0;
    self->channel = -1;

    // start the PWM subsystem if it's not already running
    if (!pwm_inited) {
        pwm_init_internal();
        pwm_inited = true;
    }

    // start the PWM running for this channel
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    pwm_init_helper(self, n_args - 1, args + 1, &kw_args);

    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t pwm_init(size_t n_args,
    const mp_obj_t *args, mp_map_t *kw_args) {
    pwm_init_helper(args[0], n_args - 1, args + 1, kw_args);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(pwm_init_obj, 1, pwm_init);

STATIC mp_obj_t pwm_deinit(mp_obj_t self_in) {
    pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    int chan = self->channel;

    // Valid channel?
    if ((chan >= 0) && (chan < PWM_CHANNEL_MAX)) {
        // Mark it unused, and tell the hardware to stop routing
        chan_gpio[chan] = -1;
#ifdef _DEBUG
        printf("in pwm_deinit, chan=%d\n", chan);
#endif

#ifdef ESP32
        ledc_stop(PWMODE, chan, 0);
        gpio_matrix_out(self->pin, SIG_GPIO_OUT_IDX, false, false);
#else
        pwm_internal_deinit_pin(self->pin);
#endif
        int count = 0;
        for(int i = 0; i < PWM_CHANNEL_MAX; ++i)
            if(chan_gpio[i] != -1) ++count;

        if(count == 0)
        {
#ifdef _DEBUG
            printf("no PWM channels used, disable all\n");
#endif

            pwm_internal_disable();
        }
        self->active = 0;
        self->channel = -1;
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pwm_deinit_obj, pwm_deinit);

STATIC mp_obj_t pwm_freq(size_t n_args, const mp_obj_t *args) {
    pwm_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (n_args == 1) {
        // get
#ifdef ESP32
        int freq_hz = timer_cfg.freq_hz;
        return MP_OBJ_NEW_SMALL_INT(freq_hz);
#else
        uint32_t period = pwm_internal_get_period(self->pin);
        return MP_OBJ_NEW_SMALL_INT(LITEX_CYCLES_TO_FREQ(period));
#endif
    }

    // set
    int tval = mp_obj_get_int(args[1]);
    if (!set_freq(self->pin, tval)) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("bad frequency %d"), tval);
    }
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pwm_freq_obj, 1, 2, pwm_freq);

STATIC mp_obj_t pwm_duty_u16(size_t n_args, const mp_obj_t *args) {
    pwm_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int duty;

    if (n_args == 1) {
        // get
#ifdef ESP32
        duty = ledc_get_duty(PWMODE, self->channel);
        duty <<= PWRES - timer_cfg.duty_resolution;
#else
        duty = pwm_internal_get_width(self->pin);
        uint32_t period = pwm_internal_get_period(self->pin);
#endif
        return MP_OBJ_NEW_SMALL_INT(CYCLES_TO_DUTY16(duty, period));
    }

    // set
    duty = mp_obj_get_int(args[1]);
#ifdef ESP32
    duty &= ((1 << PWRES) - 1);
    duty >>= PWRES - timer_cfg.duty_resolution;
    ledc_set_duty(PWMODE, self->channel, duty);
    ledc_update_duty(PWMODE, self->channel);
#else
    uint32_t period = pwm_internal_get_period(self->pin);
    pwm_internal_set_width(self->pin, DUTY16_TO_CYCLES(duty, period));
#endif
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pwm_duty_u16_obj,
    1, 2, pwm_duty_u16);

STATIC mp_obj_t pwm_duty_ns(size_t n_args, const mp_obj_t *args) {
    pwm_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int duty;

    if (n_args == 1) {
        // get
#ifdef ESP32
        duty = ledc_get_duty(PWMODE, self->channel);
        duty <<= PWRES - timer_cfg.duty_resolution;
#else
        duty = pwm_internal_get_width(self->pin);
#endif
        return MP_OBJ_NEW_SMALL_INT(LITEX_CYCLES_TO_NS_64BIT(duty));
    }

    // set
    duty = mp_obj_get_int(args[1]);
#ifdef ESP32
    duty &= ((1 << PWRES) - 1);
    duty >>= PWRES - timer_cfg.duty_resolution;
    ledc_set_duty(PWMODE, self->channel, duty);
    ledc_update_duty(PWMODE, self->channel);
#else
    pwm_internal_set_width(self->pin, LITEX_NS_TO_CYCLES_64BIT(duty));
#endif
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pwm_duty_ns_obj,
    1, 2, pwm_duty_ns);

STATIC const mp_rom_map_elem_t pwm_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&pwm_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&pwm_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_freq), MP_ROM_PTR(&pwm_freq_obj) },
    { MP_ROM_QSTR(MP_QSTR_duty_u16), MP_ROM_PTR(&pwm_duty_u16_obj) },
    { MP_ROM_QSTR(MP_QSTR_duty_ns), MP_ROM_PTR(&pwm_duty_ns_obj) },
};

STATIC MP_DEFINE_CONST_DICT(pwm_locals_dict,
    pwm_locals_dict_table);

const mp_obj_type_t machine_pwm_type = {
    { &mp_type_type },
    .name = MP_QSTR_PWM,
    .print = pwm_print,
    .make_new = pwm_make_new,
    .locals_dict = (mp_obj_dict_t *)&pwm_locals_dict,
};
#endif
