// Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
// License: BSD-2-Clause
//
// machine.WDT(id=0, timeout=ms) — hardware watchdog wrapper for LiteX's
// generic Watchdog core (litex/soc/cores/watchdog.py, enabled on the SoC
// side with --with-watchdog).
//
// CSR layout (from the LiteX core):
//   CSR_WATCHDOG_CONTROL      control word, bitfields:
//     bit  0 : feed (pulse; write 1 to feed)
//     bit  8 : enable
//     bit 16 : reset (reset the SoC when the countdown expires)
//     bit 24 : pause_halted (stop counting when CPU halted)
//   CSR_WATCHDOG_CYCLES       cycles until timeout (32-bit by default)
//   CSR_WATCHDOG_REMAINING    read-only, cycles left in the current window
//
// Idiomatic MicroPython behaviour (matches stm32 / rp2 / esp32):
//   - Constructor arms the watchdog immediately; there is no separate
//     .start() method. Once armed, the watchdog cannot be disabled on
//     most ports — we follow that convention and don't expose .deinit().
//   - .feed() resets the countdown to the configured timeout.
//   - .timeout_ms() / .timeout_ms(new_ms) is an optional extension on
//     some ports; not exposed here because LiteX's CSR_WATCHDOG_CYCLES
//     is sampled only on feed, so changing mid-flight has surprising
//     edge cases.

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"

#include <generated/csr.h>
#include <generated/soc.h>

#ifdef CSR_WATCHDOG0_BASE

// Control-word bit positions — mirror the CSRField offsets in
// litex/soc/cores/watchdog.py. Also happen to match the standard LiteX
// convention of one bit-field per byte of a 32-bit control register.
#define LITEX_WDT_CTRL_FEED_BIT         (1u << 0)
#define LITEX_WDT_CTRL_ENABLE_BIT       (1u << 8)
#define LITEX_WDT_CTRL_RESET_BIT        (1u << 16)

typedef struct _machine_wdt_obj_t {
    mp_obj_base_t base;
    uint32_t timeout_ms;
} machine_wdt_obj_t;

const mp_obj_type_t machine_wdt_type;

static machine_wdt_obj_t machine_wdt_obj = { { &machine_wdt_type }, 0 };

static uint32_t machine_wdt_ms_to_cycles(uint32_t ms) {
    // Clamp at UINT32_MAX to avoid silent wrap when the user asks for
    // something larger than the watchdog counter can represent (typical
    // 32-bit counter at 100 MHz sysclk maxes at ~42.9 s).
    uint64_t cycles = (uint64_t)ms * ((uint64_t)CONFIG_CLOCK_FREQUENCY / 1000);
    return cycles > 0xffffffffULL ? 0xffffffffUL : (uint32_t)cycles;
}

static mp_obj_t machine_wdt_make_new(const mp_obj_type_t *type, size_t n_args,
    size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_id, ARG_timeout };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id,      MP_ARG_INT,                  {.u_int = 0} },
        { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 5000} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_id].u_int != 0) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("WDT(%d): only id 0 exists on LiteX"),
            args[ARG_id].u_int);
    }
    if (args[ARG_timeout].u_int <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("WDT timeout must be > 0 ms"));
    }

    machine_wdt_obj.timeout_ms = (uint32_t)args[ARG_timeout].u_int;

    // Program cycles first, then enable with reset=1 so a timeout
    // reboots the SoC (vs. just raising an IRQ which the user might
    // miss). Feed once at arm so the first window is the full timeout.
    watchdog0_cycles_write(machine_wdt_ms_to_cycles(machine_wdt_obj.timeout_ms));
    watchdog0_control_write(
        LITEX_WDT_CTRL_ENABLE_BIT | LITEX_WDT_CTRL_RESET_BIT | LITEX_WDT_CTRL_FEED_BIT);
    return MP_OBJ_FROM_PTR(&machine_wdt_obj);
}

static void machine_wdt_print(const mp_print_t *print, mp_obj_t self_in,
    mp_print_kind_t kind) {
    (void)kind;
    machine_wdt_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "WDT(0, timeout=%u)", (unsigned int)self->timeout_ms);
}

// wdt.feed() — reset the countdown to the configured timeout. Must be
// called at least once per `timeout` ms or the SoC resets.
static mp_obj_t machine_wdt_feed(mp_obj_t self_in) {
    (void)self_in;
    // The FEED bit is declared as a CSRField with pulse=True on the LiteX
    // side, so a level-1 write auto-clears on the next cycle. We OR with
    // ENABLE|RESET so a feed doesn't inadvertently disarm the watchdog
    // (control is a single register — the bits aren't independently
    // addressable).
    watchdog0_control_write(
        LITEX_WDT_CTRL_ENABLE_BIT | LITEX_WDT_CTRL_RESET_BIT | LITEX_WDT_CTRL_FEED_BIT);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_wdt_feed_obj, machine_wdt_feed);

static const mp_rom_map_elem_t machine_wdt_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_feed), MP_ROM_PTR(&machine_wdt_feed_obj) },
};
static MP_DEFINE_CONST_DICT(machine_wdt_locals_dict,
    machine_wdt_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_wdt_type,
    MP_QSTR_WDT,
    MP_TYPE_FLAG_NONE,
    make_new, machine_wdt_make_new,
    print, machine_wdt_print,
    locals_dict, &machine_wdt_locals_dict
    );

#endif // CSR_WATCHDOG0_BASE
