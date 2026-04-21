// This file is Copyright (c) 2017-2021 Fupy/LiteX-MicroPython Developers
// This file is Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
// License: BSD-2-Clause

#include "py/nlr.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "py/objexcept.h"

#include <generated/csr.h>

#if !defined(CSR_LEDS_BASE)
static inline unsigned char leds_out_read(void) {
	return 0;
}

static inline void leds_out_write(unsigned char value) {
}
#endif

const mp_obj_type_t litex_led_type;

typedef struct _litex_led_obj_t {
    mp_obj_base_t base;
    int num;
} litex_led_obj_t;

STATIC litex_led_obj_t litex_leds[8] = {
	{{&litex_led_type}, 0},
	{{&litex_led_type}, 1},
	{{&litex_led_type}, 2},
	{{&litex_led_type}, 3},
	{{&litex_led_type}, 4},
	{{&litex_led_type}, 5},
	{{&litex_led_type}, 6},
	{{&litex_led_type}, 7}
};

STATIC mp_obj_t litex_led_make_new(const mp_obj_type_t *type_in,
		size_t n_args, size_t n_kw, const mp_obj_t *args) {
	mp_arg_check_num(n_args, n_kw, 1, 1, false);

	mp_uint_t led_num = mp_obj_get_int(args[0]);

	switch (led_num) {
	case 0 ... 7:
		return &litex_leds[led_num];
	default:
		nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
			"not a valid LED number: %d", led_num));
	}
}

void litex_led_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
	litex_led_obj_t *self = self_in;
	mp_printf(print, "LED(%u)", self->num);
}

STATIC mp_obj_t litex_led_on(mp_obj_t self_in) {
	litex_led_obj_t *led = self_in;
	char value = leds_out_read();

	leds_out_write(value | (1 << led->num));

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_led_on_obj, litex_led_on);

STATIC mp_obj_t litex_led_off(mp_obj_t self_in) {
	litex_led_obj_t *led = self_in;
	char value = leds_out_read();

	leds_out_write(value & ~(1 << led->num));

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_led_off_obj, litex_led_off);

STATIC mp_obj_t litex_led_toggle(mp_obj_t self_in) {
	litex_led_obj_t *led = self_in;
	char value = leds_out_read();

	leds_out_write(value ^ (1 << led->num));

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(litex_led_toggle_obj, litex_led_toggle);

STATIC const mp_map_elem_t litex_led_locals_dict_table[] = {
	{ MP_OBJ_NEW_QSTR(MP_QSTR_on),     (mp_obj_t)&litex_led_on_obj   },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_off),    (mp_obj_t)&litex_led_off_obj  },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_toggle), (mp_obj_t)&litex_led_toggle_obj },
};
STATIC MP_DEFINE_CONST_DICT(litex_led_locals_dict, litex_led_locals_dict_table);

const mp_obj_type_t litex_led_type = {
	{ &mp_type_type },
	.name = MP_QSTR_LED,
	.print = litex_led_print,
	.make_new = litex_led_make_new,
	.locals_dict = (mp_obj_t)&litex_led_locals_dict,
};
