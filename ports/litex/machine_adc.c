// Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
// License: BSD-2-Clause
//
// machine.ADC — generic ADC wrapper for the XADC / System Monitor family.
//
// Upstream LiteX ships one ADC core today: `litex.soc.cores.xadc`, with
// per-FPGA variants:
//
//     * `S7SystemMonitor`     (Xilinx 7-series XADC,            12-bit)
//     * `USSystemMonitor`     (Xilinx UltraScale System Monitor,10-bit)
//     * `USPSystemMonitor`    (Xilinx UltraScale+ System Monitor,10-bit)
//     * `ZynqUSPSystemMonitor`(adds vccpsint{lp,fp}, vccpsaux,  10-bit)
//
// All of them register a fixed set of `<prefix>_<channel>` CSRStatus
// registers (each channel auto-sequenced internally — no channel-select
// CSR needed). The prefix defaults to `xadc` or `sysmon` but is
// user-configurable via `soc.add_module(name=..., ...)`.
//
// Rather than hard-code knowledge of each variant, machine.ADC accepts
// the CSR name directly and resolves it through the build-time CSR
// lookup table. Numeric ids and a small set of well-known symbolic
// aliases are kept as syntactic sugar for the common channels.
//
//     >>> import machine
//     >>> a = machine.ADC('xadc_temperature')   # any RO ADC CSR
//     >>> a = machine.ADC('temperature')        # XADC alias (if present)
//     >>> a = machine.ADC(0)                    # XADC channel 0
//     >>> a = machine.ADC('myadc_value', bits=10)  # custom CSR-mapped ADC
//
// read() returns the raw N-bit sample. read_u16() shifts so the result
// spans the full 0..65535 range, matching machine.ADC on every other
// MicroPython port.

#include <stdint.h>
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"
#include "extmod/modmachine.h"

#include <generated/csr.h>

// We need the CSR lookup for name -> address resolution. The table itself
// lives in modlitex.c; expose just the lookup helper here.
#include "genhdr/litex_csr_table.h"
extern const litex_csr_entry_t *litex_csr_lookup(const char *name);

// At least one ADC core is present if any of these well-known CSRs exist.
// Without that, machine.ADC isn't built (and isn't exposed on the machine
// module by modmachine.c).
#if defined(CSR_XADC_TEMPERATURE_ADDR) \
    || defined(CSR_SYSMON_TEMPERATURE_ADDR)
#define LITEX_HAS_ADC 1
#endif

#ifdef LITEX_HAS_ADC

typedef struct _machine_adc_obj_t {
    mp_obj_base_t base;
    uint32_t csr_addr;
    uint16_t bits;          // sample width in bits, e.g. 12 for XADC
    uint16_t shift;         // (16 - bits), pre-computed for read_u16
} machine_adc_obj_t;

// Symbolic aliases. Each maps a short name (or numeric id) to a CSR name
// that the lookup table will resolve to an address. Aliases are bundled
// per-core; a SoC built with XADC sees the XADC names, etc.
typedef struct {
    const char *alias;      // user-facing name (NULL = match by id)
    int id;                 // numeric id, or -1 for name-only entries
    const char *csr_name;   // CSR to read
    uint16_t bits;          // sample width
} machine_adc_alias_t;

static const machine_adc_alias_t machine_adc_aliases[] = {
    #ifdef CSR_XADC_TEMPERATURE_ADDR
    { "temperature", 0, "xadc_temperature", 12 },
    #endif
    #ifdef CSR_XADC_VCCINT_ADDR
    { "vccint",      1, "xadc_vccint",      12 },
    #endif
    #ifdef CSR_XADC_VCCAUX_ADDR
    { "vccaux",      2, "xadc_vccaux",      12 },
    #endif
    #ifdef CSR_XADC_VCCBRAM_ADDR
    { "vccbram",     3, "xadc_vccbram",     12 },
    #endif
    #ifdef CSR_SYSMON_TEMPERATURE_ADDR
    { "temperature", 0, "sysmon_temperature", 12 },
    #endif
    #ifdef CSR_SYSMON_VCCINT_ADDR
    { "vccint",      1, "sysmon_vccint",      12 },
    #endif
    #ifdef CSR_SYSMON_VCCAUX_ADDR
    { "vccaux",      2, "sysmon_vccaux",      12 },
    #endif
    // ZynqUSPSystemMonitor adds three Zynq-specific power rails. They
    // share `temperature`/`vccint`/`vccaux` aliases with the base
    // SystemMonitor (already covered above) and add these:
    #ifdef CSR_SYSMON_VCCPSINTLP_ADDR
    { "vccpsintlp", 4, "sysmon_vccpsintlp", 12 },
    #endif
    #ifdef CSR_SYSMON_VCCPSINTFP_ADDR
    { "vccpsintfp", 5, "sysmon_vccpsintfp", 12 },
    #endif
    #ifdef CSR_SYSMON_VCCPSAUX_ADDR
    { "vccpsaux",   6, "sysmon_vccpsaux",   12 },
    #endif
    // For SoCs that wrap an external/custom ADC core whose CSRs follow a
    // different naming pattern, just instantiate by CSR name:
    //   machine.ADC('myadc_value', bits=N)
    { NULL, -1, NULL, 0 },  // sentinel
};

static const machine_adc_alias_t *machine_adc_resolve_alias(mp_obj_t arg) {
    if (mp_obj_is_str(arg)) {
        const char *name = mp_obj_str_get_str(arg);
        for (const machine_adc_alias_t *a = machine_adc_aliases; a->csr_name; a++) {
            if (a->alias && strcmp(a->alias, name) == 0) {
                return a;
            }
        }
    } else {
        int id = mp_obj_get_int(arg);
        for (const machine_adc_alias_t *a = machine_adc_aliases; a->csr_name; a++) {
            if (a->id == id) {
                return a;
            }
        }
    }
    return NULL;
}

enum { ARG_id, ARG_bits };
static const mp_arg_t machine_adc_init_args[] = {
    { MP_QSTR_id,   MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_bits, MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = 12} },
};

static mp_obj_t machine_adc_make_new(const mp_obj_type_t *type, size_t n_args,
    size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_adc_init_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
        MP_ARRAY_SIZE(machine_adc_init_args), machine_adc_init_args, args);

    uint32_t csr_addr;
    int bits;

    // Symbolic alias first (covers XADC/SysMon shortcuts and numeric ids).
    const machine_adc_alias_t *alias = machine_adc_resolve_alias(args[ARG_id].u_obj);
    if (alias) {
        const litex_csr_entry_t *e = litex_csr_lookup(alias->csr_name);
        if (e == NULL) {
            // Shouldn't happen — the alias table is gated on the same CSR
            // macros — but be defensive.
            mp_raise_msg_varg(&mp_type_ValueError,
                MP_ERROR_TEXT("ADC alias '%s' resolves to missing CSR '%s'"),
                alias->alias ? alias->alias : "?", alias->csr_name);
        }
        csr_addr = e->addr;
        bits = alias->bits;
    } else if (mp_obj_is_str(args[ARG_id].u_obj)) {
        // Direct CSR name. Resolve via the build-time table.
        const char *name = mp_obj_str_get_str(args[ARG_id].u_obj);
        const litex_csr_entry_t *e = litex_csr_lookup(name);
        if (e == NULL) {
            mp_raise_msg_varg(&mp_type_ValueError,
                MP_ERROR_TEXT("no CSR named '%s'"), name);
        }
        csr_addr = e->addr;
        bits = args[ARG_bits].u_int;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("ADC: unknown channel"));
    }

    if (bits < 1 || bits > 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("ADC bits must be in 1..16"));
    }

    machine_adc_obj_t *self = mp_obj_malloc(machine_adc_obj_t, type);
    self->csr_addr = csr_addr;
    self->bits = (uint16_t)bits;
    self->shift = (uint16_t)(16 - bits);
    return MP_OBJ_FROM_PTR(self);
}

static void machine_adc_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    machine_adc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "ADC(csr=0x%08x, bits=%u)",
        (unsigned int)self->csr_addr, self->bits);
}

// machine.ADC.read() — raw N-bit sample.
static mp_obj_t machine_adc_read(mp_obj_t self_in) {
    machine_adc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t mask = (self->bits == 32) ? 0xffffffffu : ((1u << self->bits) - 1u);
    return MP_OBJ_NEW_SMALL_INT(MMPTR(self->csr_addr) & mask);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_adc_read_obj, machine_adc_read);

// machine.ADC.read_u16() — left-shifted to span the 0..65535 range that
// machine.ADC uses on every other MicroPython port.
static mp_obj_t machine_adc_read_u16(mp_obj_t self_in) {
    machine_adc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t mask = (self->bits == 32) ? 0xffffffffu : ((1u << self->bits) - 1u);
    uint32_t raw = MMPTR(self->csr_addr) & mask;
    return MP_OBJ_NEW_SMALL_INT((raw << self->shift) & 0xffff);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_adc_read_u16_obj, machine_adc_read_u16);

static const mp_rom_map_elem_t machine_adc_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_read),     MP_ROM_PTR(&machine_adc_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_u16), MP_ROM_PTR(&machine_adc_read_u16_obj) },
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

#endif // LITEX_HAS_ADC
