// This file is Copyright (c) 2017-2021 Fupy/LiteX-MicroPython Developers
// Copyright (C) 2021-2022 Victor Suarez Rovere <suarezvictor@gmail.com>
// Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
// License: BSD-2-Clause

#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"
#include "py/mphal.h"

#include <generated/csr.h>
#include <generated/mem.h>
#include <generated/git.h>

// litex_csr_table[] — an auto-generated sorted list of every CSR in the SoC
// this firmware was built against, with its address, width, and r/w type.
// Emitted by tools/gen_csr_table.py from the SoC's csr.json.
#include "genhdr/litex_csr_table.h"

extern const mp_obj_type_t litex_led_type;
extern const mp_obj_type_t litex_dmawriter_type;
extern const mp_obj_type_t litex_dmareader_type;
extern const mp_obj_type_t litex_video_type;

// litex.read32(addr) / litex.write32(addr, value) — raw MMIO. Thin wrappers
// over machine.mem32[addr], but named to make CSR bring-up scripts readable:
//     litex.write32(litex.CSR_BASE + 0x0, 0xdeadbeef)
// Addresses are not range-checked: users poke real hardware here.
static mp_obj_t litex_read32(mp_obj_t addr_obj) {
    uint32_t addr = mp_obj_get_int_truncated(addr_obj);
    return mp_obj_new_int_from_uint(MMPTR(addr));
}
static MP_DEFINE_CONST_FUN_OBJ_1(litex_read32_obj, litex_read32);

static mp_obj_t litex_write32(mp_obj_t addr_obj, mp_obj_t value_obj) {
    uint32_t addr = mp_obj_get_int_truncated(addr_obj);
    uint32_t value = mp_obj_get_int_truncated(value_obj);
    MMPTR(addr) = value;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(litex_write32_obj, litex_write32);

// litex.git_sha1() / litex.bus_standard() — strings embedded in the firmware
// image by the LiteX generation step. Exposed as zero-arg functions because
// the values are runtime string literals, not qstrs.
static mp_obj_t litex_git_sha1(void) {
    return mp_obj_new_str(LITEX_GIT_SHA1, strlen(LITEX_GIT_SHA1));
}
static MP_DEFINE_CONST_FUN_OBJ_0(litex_git_sha1_obj, litex_git_sha1);

static mp_obj_t litex_bus_standard(void) {
    return mp_obj_new_str(CONFIG_BUS_STANDARD, strlen(CONFIG_BUS_STANDARD));
}
static MP_DEFINE_CONST_FUN_OBJ_0(litex_bus_standard_obj, litex_bus_standard);

// Look up a CSR by name in the build-time table. Returns NULL if the name
// isn't in the SoC, otherwise a pointer into litex_csr_table[].
static const litex_csr_entry_t *litex_csr_lookup(const char *name) {
    // Table is sorted by name — a linear scan is fine for the ~200 CSRs a
    // typical LiteX SoC has, and simpler than dragging bsearch() in from
    // picolibc.
    for (size_t i = 0; i < LITEX_CSR_TABLE_SIZE; i++) {
        if (strcmp(litex_csr_table[i].name, name) == 0) {
            return &litex_csr_table[i];
        }
    }
    return NULL;
}

// litex.csr_read(name) — return the current value of the named CSR.
static mp_obj_t litex_csr_read(mp_obj_t name_obj) {
    const char *name = mp_obj_str_get_str(name_obj);
    const litex_csr_entry_t *e = litex_csr_lookup(name);
    if (e == NULL) {
        mp_raise_msg_varg(&mp_type_KeyError,
            MP_ERROR_TEXT("no CSR named '%s' in this SoC"), name);
    }
    if (e->type == 2) {
        // write-only CSR — reading is nonsensical and on some LiteX builds
        // actually traps. Raise instead of returning bogus data.
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("CSR '%s' is write-only"), name);
    }
    if (e->size == 1) {
        return mp_obj_new_int_from_uint(MMPTR(e->addr));
    }
    // Multi-word CSRs (size > 1) are stored big-endian across consecutive
    // data-width slots on LiteX's default CSR bus. Reassemble as a single
    // Python int so callers don't have to care.
    mp_uint_t value = 0;
    for (uint8_t i = 0; i < e->size; i++) {
        value = (value << 32) | MMPTR(e->addr + 4 * i);
    }
    return mp_obj_new_int_from_ull(value);
}
static MP_DEFINE_CONST_FUN_OBJ_1(litex_csr_read_obj, litex_csr_read);

// litex.csr_write(name, value) — write `value` to the named CSR. For
// multi-word CSRs the value is shifted out MSB-first to match LiteX's bus
// packing.
static mp_obj_t litex_csr_write(mp_obj_t name_obj, mp_obj_t value_obj) {
    const char *name = mp_obj_str_get_str(name_obj);
    const litex_csr_entry_t *e = litex_csr_lookup(name);
    if (e == NULL) {
        mp_raise_msg_varg(&mp_type_KeyError,
            MP_ERROR_TEXT("no CSR named '%s' in this SoC"), name);
    }
    if (e->type == 0) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("CSR '%s' is read-only"), name);
    }
    mp_uint_t value = mp_obj_get_int_truncated(value_obj);
    for (int i = e->size - 1; i >= 0; i--) {
        MMPTR(e->addr + 4 * i) = value & 0xffffffff;
        value >>= 32;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(litex_csr_write_obj, litex_csr_write);

// litex.EventManager("<prefix>") — wrap a LiteX EventManager CSR block.
//
// LiteX peripherals expose IRQ-related state via a triple of CSRs named
// <prefix>_ev_pending / <prefix>_ev_enable / <prefix>_ev_status. This class
// resolves those names through the build-time CSR table and exposes the
// common operations (poll pending bits, clear them, enable/disable
// sources) as methods — so user code doesn't have to reach for
// csr_read/csr_write for every edge of an event.
//
// IRQ dispatch to a Python callback from the port's C-level isr() is not
// wired up yet; handlers are scheduled via polling for now (either from a
// machine.Timer, a tight loop, or a REPL prompt).
typedef struct _litex_event_manager_obj_t {
    mp_obj_base_t base;
    uint32_t pending_addr;
    uint32_t enable_addr;
    uint32_t status_addr;     // 0 if this peripheral has no ev_status
    const char *prefix;
} litex_event_manager_obj_t;

extern const mp_obj_type_t litex_event_manager_type;

static uint32_t litex_lookup_csr_addr(const char *prefix, const char *suffix) {
    // Compose "<prefix>_ev_<suffix>" and look it up.
    char name[64];
    size_t lp = strlen(prefix);
    size_t ls = strlen(suffix);
    if (lp + 4 + ls + 1 > sizeof(name)) {
        return 0;
    }
    memcpy(name, prefix, lp);
    memcpy(name + lp, "_ev_", 4);
    memcpy(name + lp + 4, suffix, ls + 1);
    const litex_csr_entry_t *e = litex_csr_lookup(name);
    return e ? e->addr : 0;
}

static mp_obj_t litex_event_manager_make_new(const mp_obj_type_t *type,
        size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    const char *prefix = mp_obj_str_get_str(args[0]);
    uint32_t pending = litex_lookup_csr_addr(prefix, "pending");
    uint32_t enable = litex_lookup_csr_addr(prefix, "enable");
    if (!pending || !enable) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("no EventManager CSRs for peripheral '%s'"), prefix);
    }
    litex_event_manager_obj_t *self = m_new_obj(litex_event_manager_obj_t);
    self->base.type = type;
    self->pending_addr = pending;
    self->enable_addr = enable;
    self->status_addr = litex_lookup_csr_addr(prefix, "status");  // may be 0
    self->prefix = prefix;  // lifetime: interned by qstr or caller-owned
    return MP_OBJ_FROM_PTR(self);
}

static void litex_event_manager_print(const mp_print_t *print, mp_obj_t self_in,
                                      mp_print_kind_t kind) {
    (void)kind;
    litex_event_manager_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "EventManager('%s', pending=0x%08x, enable=0x%08x)",
        self->prefix, (unsigned int)self->pending_addr,
        (unsigned int)self->enable_addr);
}

static mp_obj_t litex_event_manager_pending(mp_obj_t self_in) {
    litex_event_manager_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int_from_uint(MMPTR(self->pending_addr));
}
static MP_DEFINE_CONST_FUN_OBJ_1(litex_event_manager_pending_obj,
                                 litex_event_manager_pending);

static mp_obj_t litex_event_manager_status(mp_obj_t self_in) {
    litex_event_manager_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->status_addr) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("peripheral has no ev_status"));
    }
    return mp_obj_new_int_from_uint(MMPTR(self->status_addr));
}
static MP_DEFINE_CONST_FUN_OBJ_1(litex_event_manager_status_obj,
                                 litex_event_manager_status);

// Write-1-to-clear: writing 1 bits to ev_pending acknowledges those events.
static mp_obj_t litex_event_manager_clear(mp_obj_t self_in, mp_obj_t mask_obj) {
    litex_event_manager_obj_t *self = MP_OBJ_TO_PTR(self_in);
    MMPTR(self->pending_addr) = mp_obj_get_int_truncated(mask_obj);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(litex_event_manager_clear_obj,
                                 litex_event_manager_clear);

static mp_obj_t litex_event_manager_enable(mp_obj_t self_in, mp_obj_t mask_obj) {
    litex_event_manager_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t mask = mp_obj_get_int_truncated(mask_obj);
    MMPTR(self->enable_addr) = MMPTR(self->enable_addr) | mask;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(litex_event_manager_enable_obj,
                                 litex_event_manager_enable);

static mp_obj_t litex_event_manager_disable(mp_obj_t self_in, mp_obj_t mask_obj) {
    litex_event_manager_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t mask = mp_obj_get_int_truncated(mask_obj);
    MMPTR(self->enable_addr) = MMPTR(self->enable_addr) & ~mask;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(litex_event_manager_disable_obj,
                                 litex_event_manager_disable);

static const mp_rom_map_elem_t litex_event_manager_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_pending), MP_ROM_PTR(&litex_event_manager_pending_obj) },
    { MP_ROM_QSTR(MP_QSTR_status),  MP_ROM_PTR(&litex_event_manager_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear),   MP_ROM_PTR(&litex_event_manager_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_enable),  MP_ROM_PTR(&litex_event_manager_enable_obj) },
    { MP_ROM_QSTR(MP_QSTR_disable), MP_ROM_PTR(&litex_event_manager_disable_obj) },
};
static MP_DEFINE_CONST_DICT(litex_event_manager_locals_dict,
                            litex_event_manager_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    litex_event_manager_type,
    MP_QSTR_EventManager,
    MP_TYPE_FLAG_NONE,
    make_new, litex_event_manager_make_new,
    print, litex_event_manager_print,
    locals_dict, &litex_event_manager_locals_dict
    );

// litex.csrs() — return a list of every CSR name in the SoC.
static mp_obj_t litex_csrs(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < LITEX_CSR_TABLE_SIZE; i++) {
        mp_obj_list_append(list,
            mp_obj_new_str(litex_csr_table[i].name,
                strlen(litex_csr_table[i].name)));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(litex_csrs_obj, litex_csrs);

// litex.info() — human-friendly summary of this SoC build. Useful as a
// first REPL command to confirm the hardware layout matches expectations.
static mp_obj_t litex_info(void) {
    mp_printf(&mp_plat_print,
        "LiteX SoC\n"
        "  LiteX git: " LITEX_GIT_SHA1 "\n"
        "  bus:       " CONFIG_BUS_STANDARD "\n"
        "  clock:     %u Hz\n"
        "  CSR base:  0x%08x\n",
        (unsigned int)CONFIG_CLOCK_FREQUENCY,
        (unsigned int)CSR_BASE);
    #ifdef MAIN_RAM_BASE
    mp_printf(&mp_plat_print,
        "  RAM base:  0x%08x (size 0x%08x)\n",
        (unsigned int)MAIN_RAM_BASE, (unsigned int)MAIN_RAM_SIZE);
    #endif
    #ifdef ROM_BASE
    mp_printf(&mp_plat_print,
        "  ROM base:  0x%08x (size 0x%08x)\n",
        (unsigned int)ROM_BASE, (unsigned int)ROM_SIZE);
    #endif
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(litex_info_obj, litex_info);

static const mp_rom_map_elem_t litex_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),      MP_ROM_QSTR(MP_QSTR_litex) },

    // Build metadata. Mirrors what LiteX's generated headers expose to the
    // BIOS, so Python code can cross-check against the SoC it expects.
    { MP_ROM_QSTR(MP_QSTR_sys_clk_freq),  MP_ROM_INT(CONFIG_CLOCK_FREQUENCY) },
    { MP_ROM_QSTR(MP_QSTR_git_sha1),      MP_ROM_PTR(&litex_git_sha1_obj) },
    { MP_ROM_QSTR(MP_QSTR_bus_standard),  MP_ROM_PTR(&litex_bus_standard_obj) },

    // Raw MMIO helpers — stand-alone access without importing machine.mem32.
    { MP_ROM_QSTR(MP_QSTR_read32),        MP_ROM_PTR(&litex_read32_obj) },
    { MP_ROM_QSTR(MP_QSTR_write32),       MP_ROM_PTR(&litex_write32_obj) },

    // Name-resolved CSR access using the build-time-generated table. Each
    // entry carries size and r/w polarity, so csr_read raises on write-only
    // CSRs and csr_write raises on read-only ones.
    { MP_ROM_QSTR(MP_QSTR_csr_read),      MP_ROM_PTR(&litex_csr_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_csr_write),     MP_ROM_PTR(&litex_csr_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_csrs),          MP_ROM_PTR(&litex_csrs_obj) },

    { MP_ROM_QSTR(MP_QSTR_info),          MP_ROM_PTR(&litex_info_obj) },

    // Thin wrapper over a peripheral's <prefix>_ev_{pending,enable,status}
    // CSR triple, resolved through the build-time lookup table.
    { MP_ROM_QSTR(MP_QSTR_EventManager),  MP_ROM_PTR(&litex_event_manager_type) },

    // Base addresses from the LiteX generation.
    { MP_ROM_QSTR(MP_QSTR_CSR_BASE),      MP_ROM_INT(CSR_BASE) },
    #ifdef MAIN_RAM_BASE
    { MP_ROM_QSTR(MP_QSTR_MAIN_RAM_BASE), MP_ROM_INT(MAIN_RAM_BASE) },
    { MP_ROM_QSTR(MP_QSTR_MAIN_RAM_SIZE), MP_ROM_INT(MAIN_RAM_SIZE) },
    #endif
    #ifdef ROM_BASE
    { MP_ROM_QSTR(MP_QSTR_ROM_BASE),      MP_ROM_INT(ROM_BASE) },
    { MP_ROM_QSTR(MP_QSTR_ROM_SIZE),      MP_ROM_INT(ROM_SIZE) },
    #endif

    // Peripheral types (gated by the corresponding CSR so `litex.Video`
    // only exists on SoCs that actually ship a framebuffer).
    { MP_ROM_QSTR(MP_QSTR_LED),           MP_ROM_PTR(&litex_led_type) },
    #ifdef CSR_DMA_WRITER_BASE
    { MP_ROM_QSTR(MP_QSTR_DMAWriter),     MP_ROM_PTR(&litex_dmawriter_type) },
    #endif
    #ifdef CSR_DMA_READER_BASE
    { MP_ROM_QSTR(MP_QSTR_DMAReader),     MP_ROM_PTR(&litex_dmareader_type) },
    #endif
    #ifdef CSR_VIDEO_FRAMEBUFFER_BASE
    { MP_ROM_QSTR(MP_QSTR_Video),         MP_ROM_PTR(&litex_video_type) },
    #endif
};

static MP_DEFINE_CONST_DICT(litex_module_globals, litex_module_globals_table);

const mp_obj_module_t mp_module_litex = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&litex_module_globals,
};

// Register the module so `import litex` works out of the box.
MP_REGISTER_MODULE(MP_QSTR_litex, mp_module_litex);
