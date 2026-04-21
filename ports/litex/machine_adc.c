// Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
// License: BSD-2-Clause
//
// machine.ADC(channel) — wraps the LiteX Xilinx XADC core when present.
//
// The XADC block (litex.soc.cores.xadc) exposes four standard system-monitor
// channels as CSRs:
//
//     0 = temperature    (12-bit raw, °C = value * 503.975 / 4096 - 273.15)
//     1 = vccint         (12-bit raw, V  = value * 3 / 4096)
//     2 = vccaux         (12-bit raw, V  = value * 3 / 4096)
//     3 = vccbram        (12-bit raw, V  = value * 3 / 4096)
//
// Symbolic aliases (machine.ADC("temperature"), ...) accept any of those four
// names in addition to numeric ids. read_u16() returns the 12-bit XADC
// reading shifted left 4 bits so the result spans the full 16-bit range, to
// match upstream's ADC convention.
//
// This module is gated on CSR_XADC_TEMPERATURE_ADDR; SoCs without XADC
// simply don't see machine.ADC in the machine module.

#include <stdint.h>
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"
#include "extmod/modmachine.h"

#include <generated/csr.h>

#ifdef CSR_XADC_TEMPERATURE_ADDR

typedef struct _machine_adc_obj_t {
    mp_obj_base_t base;
    uint32_t csr_addr;
    const char *name;
    uint8_t id;
} machine_adc_obj_t;

static const machine_adc_obj_t machine_adc_channels[] = {
    { { NULL }, CSR_XADC_TEMPERATURE_ADDR, "temperature", 0 },
    #ifdef CSR_XADC_VCCINT_ADDR
    { { NULL }, CSR_XADC_VCCINT_ADDR,      "vccint",      1 },
    #endif
    #ifdef CSR_XADC_VCCAUX_ADDR
    { { NULL }, CSR_XADC_VCCAUX_ADDR,      "vccaux",      2 },
    #endif
    #ifdef CSR_XADC_VCCBRAM_ADDR
    { { NULL }, CSR_XADC_VCCBRAM_ADDR,     "vccbram",     3 },
    #endif
};
#define MACHINE_ADC_COUNT \
    (sizeof(machine_adc_channels) / sizeof(machine_adc_channels[0]))

static void machine_adc_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    machine_adc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "ADC(%u, '%s')", self->id, self->name);
}

static mp_obj_t machine_adc_make_new(const mp_obj_type_t *type, size_t n_args,
                                     size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    const machine_adc_obj_t *picked = NULL;
    if (mp_obj_is_str(args[0])) {
        const char *name = mp_obj_str_get_str(args[0]);
        for (size_t i = 0; i < MACHINE_ADC_COUNT; i++) {
            if (strcmp(machine_adc_channels[i].name, name) == 0) {
                picked = &machine_adc_channels[i];
                break;
            }
        }
        if (picked == NULL) {
            mp_raise_msg_varg(&mp_type_ValueError,
                MP_ERROR_TEXT("no XADC channel named '%s'"), name);
        }
    } else {
        int id = mp_obj_get_int(args[0]);
        for (size_t i = 0; i < MACHINE_ADC_COUNT; i++) {
            if (machine_adc_channels[i].id == id) {
                picked = &machine_adc_channels[i];
                break;
            }
        }
        if (picked == NULL) {
            mp_raise_msg_varg(&mp_type_ValueError,
                MP_ERROR_TEXT("no XADC channel with id %d"), id);
        }
    }
    // We return a pointer to the const table; that's safe because
    // machine_adc_obj_t has no mutable runtime state. The 'type' slot is
    // filled in lazily here — the table literal can't reference
    // machine_adc_type because it's defined below.
    machine_adc_obj_t *out = (machine_adc_obj_t *)picked;
    out->base.type = type;
    return MP_OBJ_FROM_PTR(out);
}

// machine.ADC.read_u16() — 12-bit XADC reading shifted into the upper 12
// bits of a 16-bit return so the range matches machine.ADC on other ports
// (0..65535, full-scale = Vref).
static mp_obj_t machine_adc_read_u16(mp_obj_t self_in) {
    machine_adc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t raw12 = MMPTR(self->csr_addr) & 0xfff;
    return MP_OBJ_NEW_SMALL_INT(raw12 << 4);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_adc_read_u16_obj, machine_adc_read_u16);

// machine.ADC.read() — raw 12-bit XADC value, for callers who need the
// unscaled number (e.g. to apply the XADC temperature formula directly).
static mp_obj_t machine_adc_read(mp_obj_t self_in) {
    machine_adc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(MMPTR(self->csr_addr) & 0xfff);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_adc_read_obj, machine_adc_read);

static const mp_rom_map_elem_t machine_adc_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_read_u16), MP_ROM_PTR(&machine_adc_read_u16_obj) },
    { MP_ROM_QSTR(MP_QSTR_read),     MP_ROM_PTR(&machine_adc_read_obj) },
};
static MP_DEFINE_CONST_DICT(machine_adc_locals_dict, machine_adc_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_adc_type,
    MP_QSTR_ADC,
    MP_TYPE_FLAG_NONE,
    make_new, machine_adc_make_new,
    print, machine_adc_print,
    locals_dict, &machine_adc_locals_dict
    );

#endif // CSR_XADC_TEMPERATURE_ADDR
