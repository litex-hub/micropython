// This file is Copyright (c) 2017-2021 Fupy/LiteX-MicroPython Developers
// This file is Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
// This file is Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
// License: BSD-2-Clause
//
// This file is never compiled standalone: it's included directly from
// extmod/modmachine.c via MICROPY_PY_MACHINE_INCLUDEFILE.

#include <generated/csr.h>
#include <generated/soc.h>
#include <generated/mem.h>

// Forward declarations for port-specific peripheral types (defined in sibling
// .c files) — referenced from MICROPY_PY_MACHINE_EXTRA_GLOBALS below.
#ifdef CSR_GPIO_BASE
extern const mp_obj_type_t machine_pin_type;
#endif
#ifdef USE_HARDWARE_SPI
extern const mp_obj_type_t machine_hw_spi_type;
#endif
#ifdef CSR_TIMER0_BASE
extern const mp_obj_type_t machine_timer_type;
#endif
#if defined(CSR_UART1_BASE) || defined(CSR_UART2_BASE) || defined(CSR_UART3_BASE)
extern const mp_obj_type_t machine_uart_type;
#define LITEX_HAS_SECONDARY_UART 1
#endif
// machine_adc.c gates itself on every known LiteX ADC core's CSR; mirror
// the same composite condition here so the type is exposed iff one of those
// cores is present.
#if defined(CSR_XADC_TEMPERATURE_ADDR) \
    || defined(CSR_SYSMON_TEMPERATURE_ADDR)
extern const mp_obj_type_t machine_adc_type;
#define LITEX_HAS_ADC 1
#endif
#ifdef CSR_LEDS_PWM_ENABLE_ADDR
extern const mp_obj_type_t machine_pwm_type;
#endif
#if MICROPY_HW_ENABLE_SDCARD
extern const mp_obj_type_t machine_sdcard_type;
#endif

// machine.identifier() — read the LiteX identifier CSR ROM as a bytes object.
static mp_obj_t machine_identifier(void) {
    uint8_t id[256];
    size_t n = 0;
    for (size_t i = 0; i < sizeof(id); i++) {
        uint8_t c = MMPTR(CSR_IDENTIFIER_MEM_BASE + 4 * i);
        if (!c) {
            break;
        }
        id[n++] = c;
    }
    return mp_obj_new_bytes(id, n);
}
static MP_DEFINE_CONST_FUN_OBJ_0(machine_identifier_obj, machine_identifier);

// machine.freq() — LiteX SoCs have a single fixed sys_clk_freq baked in at
// SoC-generation time; expose it but don't pretend we can retune it.
//
// Named *_litex_freq_obj rather than machine_freq_obj to dodge a name
// collision with extmod/modmachine.h, which declares machine_freq_obj
// extern (only defined when MICROPY_PY_MACHINE_BARE_METAL_FUNCS=1, which
// we don't enable). The user-visible attribute is still 'freq' via the
// EXTRA_GLOBALS table below.
static mp_obj_t litex_machine_freq(void) {
    return MP_OBJ_NEW_SMALL_INT(CONFIG_CLOCK_FREQUENCY);
}
static MP_DEFINE_CONST_FUN_OBJ_0(litex_machine_freq_obj, litex_machine_freq);

// Per-SoC peripheral entries, each gated by the corresponding CSR so the
// Python-visible module always matches the hardware the SoC was built with.
#ifdef CSR_GPIO_BASE
#define MACHINE_PIN_ENTRY \
    { MP_ROM_QSTR(MP_QSTR_Pin), MP_ROM_PTR(&machine_pin_type) },
#else
#define MACHINE_PIN_ENTRY
#endif

#ifdef USE_HARDWARE_SPI
#define MACHINE_SPI_ENTRY \
    { MP_ROM_QSTR(MP_QSTR_SPI), MP_ROM_PTR(&machine_hw_spi_type) },
#else
#define MACHINE_SPI_ENTRY
#endif

#ifdef CSR_TIMER0_BASE
#define MACHINE_TIMER_ENTRY \
    { MP_ROM_QSTR(MP_QSTR_Timer), MP_ROM_PTR(&machine_timer_type) },
#else
#define MACHINE_TIMER_ENTRY
#endif

#ifdef LITEX_HAS_SECONDARY_UART
#define MACHINE_UART_ENTRY \
    { MP_ROM_QSTR(MP_QSTR_UART), MP_ROM_PTR(&machine_uart_type) },
#else
#define MACHINE_UART_ENTRY
#endif

#ifdef LITEX_HAS_ADC
#define MACHINE_ADC_ENTRY \
    { MP_ROM_QSTR(MP_QSTR_ADC), MP_ROM_PTR(&machine_adc_type) },
#else
#define MACHINE_ADC_ENTRY
#endif

#ifdef CSR_LEDS_PWM_ENABLE_ADDR
#define MACHINE_PWM_ENTRY \
    { MP_ROM_QSTR(MP_QSTR_PWM), MP_ROM_PTR(&machine_pwm_type) },
#else
#define MACHINE_PWM_ENTRY
#endif

#if MICROPY_HW_ENABLE_SDCARD
#define MACHINE_SDCARD_ENTRY \
    { MP_ROM_QSTR(MP_QSTR_SDCard), MP_ROM_PTR(&machine_sdcard_type) },
#else
#define MACHINE_SDCARD_ENTRY
#endif

// machine.unique_id() is the upstream-standard MicroPython API for
// "give me a bytes object identifying this hardware". On LiteX SoCs the
// natural source is the IDENTIFIER_MEM CSR ROM (built-in serial-no /
// build-tag string), so we expose the same bytes under both the
// LiteX-historical name (machine.identifier()) and the upstream-standard
// one (machine.unique_id()) — same C function, two QSTRs.
#define MICROPY_PY_MACHINE_EXTRA_GLOBALS \
    { MP_ROM_QSTR(MP_QSTR_identifier), MP_ROM_PTR(&machine_identifier_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_unique_id),  MP_ROM_PTR(&machine_identifier_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_freq),       MP_ROM_PTR(&litex_machine_freq_obj) }, \
    MACHINE_PIN_ENTRY                                                   \
    MACHINE_SPI_ENTRY                                                   \
    MACHINE_TIMER_ENTRY                                                 \
    MACHINE_PWM_ENTRY                                                   \
    MACHINE_SDCARD_ENTRY                                                \
    MACHINE_UART_ENTRY                                                  \
    MACHINE_ADC_ENTRY

// Port callbacks required by extmod/modmachine.c.
//
// Modern LiteX no longer generates per-bitfield writers, so we use the whole
// ctrl.reset register plus the CSR-provided offset macro so the correct bit
// is set regardless of future additions to ctrl.reset (cpu_rst, ...).
MP_NORETURN static void mp_machine_reset(void) {
    ctrl_reset_write(1 << CSR_CTRL_RESET_SOC_RST_OFFSET);
    for (;;) {
    }
}

static mp_int_t mp_machine_reset_cause(void) {
    // LiteX doesn't expose a reset-cause latch in the standard ctrl block.
    return 0;
}

static void mp_machine_idle(void) {
    // VexRiscv 'minimal' has no WFI; nothing useful to do here.
}
