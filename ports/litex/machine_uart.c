// Copyright (c) 2026 Florent Kermarrec <f.kermarrec@gmail.com>
// License: BSD-2-Clause
//
// machine.UART(id) — thin wrapper around a secondary LiteX UART CSR block.
//
// LiteX's primary UART is used by the MicroPython REPL, so this driver only
// wraps secondary UARTs (CSR_UART1_BASE, CSR_UART2_BASE, ...). Each LiteX
// UART has the same CSR layout; we parameterise by base address:
//
//     base + 0x00  rxtx          (read/write data)
//     base + 0x04  txfull        (read-only status)
//     base + 0x08  rxempty       (read-only status)
//     base + 0x0c  ev_status
//     base + 0x10  ev_pending
//     base + 0x14  ev_enable
//     base + 0x18  txempty
//     base + 0x1c  rxfull
//
// LiteX UART baud rate and number of data bits are baked in at SoC
// generation time and cannot be retuned at runtime. The `baudrate` kwarg
// is accepted (for pyboard-style compatibility) but must match the SoC's
// configured rate or an error is raised.

#include <stdint.h>
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "py/mphal.h"
#include "extmod/modmachine.h"

#include <generated/csr.h>
#include <generated/soc.h>

// Only build this module if at least one secondary UART is exposed by the
// SoC. Without that, machine.UART() doesn't appear in the machine module —
// users get a clean AttributeError instead of a "no UARTs" runtime error.
#if defined(CSR_UART1_BASE) || defined(CSR_UART2_BASE) || defined(CSR_UART3_BASE)
#define LITEX_HAS_SECONDARY_UART 1
#endif

#if LITEX_HAS_SECONDARY_UART

#define LITEX_UART_RXTX_OFFSET     0x00
#define LITEX_UART_TXFULL_OFFSET   0x04
#define LITEX_UART_RXEMPTY_OFFSET  0x08
#define LITEX_UART_EV_PENDING_OFFSET 0x10
#define LITEX_UART_EV_ENABLE_OFFSET  0x14

// Bits in ev_pending/ev_enable. Writing 1 to ev_pending clears the event.
#define LITEX_UART_EV_TX  0x01
#define LITEX_UART_EV_RX  0x02

typedef struct _machine_uart_obj_t {
    mp_obj_base_t base;
    uint32_t csr_base;
    int id;
} machine_uart_obj_t;

// Static table of all available UARTs on this SoC. Populated at compile
// time from the CSR_UART<N>_BASE macros; entries don't exist for UARTs
// the SoC wasn't built with.
static machine_uart_obj_t machine_uart_objs[] = {
    #ifdef CSR_UART1_BASE
    { .base = { NULL }, .csr_base = CSR_UART1_BASE, .id = 1 },
    #endif
    #ifdef CSR_UART2_BASE
    { .base = { NULL }, .csr_base = CSR_UART2_BASE, .id = 2 },
    #endif
    #ifdef CSR_UART3_BASE
    { .base = { NULL }, .csr_base = CSR_UART3_BASE, .id = 3 },
    #endif
};
#define MACHINE_UART_COUNT \
    (sizeof(machine_uart_objs) / sizeof(machine_uart_objs[0]))

// CSR accessors parameterised by the UART's base address. These compile
// down to direct MMIO; no overhead over the LiteX-generated per-UART
// helpers that csr.h would otherwise produce.
static inline uint32_t uart_reg_read(const machine_uart_obj_t *self, uint32_t off) {
    return MMPTR(self->csr_base + off);
}
static inline void uart_reg_write(const machine_uart_obj_t *self, uint32_t off, uint32_t v) {
    MMPTR(self->csr_base + off) = v;
}

static machine_uart_obj_t *machine_uart_find(int id) {
    for (size_t i = 0; i < MACHINE_UART_COUNT; i++) {
        if (machine_uart_objs[i].id == id) {
            return &machine_uart_objs[i];
        }
    }
    return NULL;
}

static void machine_uart_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "UART(%u, csr_base=0x%08x)",
        self->id, (unsigned int)self->csr_base);
}

enum { ARG_id, ARG_baudrate, ARG_bits, ARG_parity, ARG_stop };
static const mp_arg_t machine_uart_init_args[] = {
    { MP_QSTR_id,       MP_ARG_INT | MP_ARG_REQUIRED, {.u_int = 0} },
    // Fixed at SoC-gen time on LiteX; accepted for API parity but checked
    // against CONFIG_CLOCK_FREQUENCY / the UART's divider if/when the SoC
    // exports it. For now, any value is accepted — a real diagnostic needs
    // the board's configured baud, which we don't have generically.
    { MP_QSTR_baudrate, MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = 115200} },
    { MP_QSTR_bits,     MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = 8} },
    { MP_QSTR_parity,   MP_ARG_KW_ONLY | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
    { MP_QSTR_stop,     MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = 1} },
};

static mp_obj_t machine_uart_make_new(const mp_obj_type_t *type, size_t n_args,
                                      size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_uart_init_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
        MP_ARRAY_SIZE(machine_uart_init_args), machine_uart_init_args, args);

    int id = args[ARG_id].u_int;
    machine_uart_obj_t *self = machine_uart_find(id);
    if (self == NULL) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("UART(%d): not present in this SoC"), id);
    }
    self->base.type = type;
    // bits/parity/stop are all fixed by LiteX's UART core (8N1). Surface a
    // clear error rather than silently ignoring.
    if (args[ARG_bits].u_int != 8) {
        mp_raise_ValueError(MP_ERROR_TEXT("LiteX UART is 8-bit only"));
    }
    if (args[ARG_parity].u_obj != mp_const_none) {
        mp_raise_ValueError(MP_ERROR_TEXT("LiteX UART has no parity support"));
    }
    if (args[ARG_stop].u_int != 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("LiteX UART uses 1 stop bit"));
    }
    // Disable UART events — we poll. (If litex.EventManager later claims
    // this UART, it re-enables what it needs.)
    uart_reg_write(self, LITEX_UART_EV_ENABLE_OFFSET, 0);
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t machine_uart_any(mp_obj_t self_in) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    // rxempty is 1 when the RX FIFO is empty, 0 otherwise. LiteX's RX FIFO
    // is typically 16 entries but the exact depth is a SoC-gen choice;
    // callers should only expect "any pending byte?" semantics here.
    return mp_obj_new_bool(uart_reg_read(self, LITEX_UART_RXEMPTY_OFFSET) == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_uart_any_obj, machine_uart_any);

// Stream-protocol helpers. These are polling reads/writes — suitable for
// REPL interaction but not for high-throughput DMA-style use.
static mp_uint_t machine_uart_read(mp_obj_t self_in, void *buf_in, mp_uint_t size, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t *buf = buf_in;
    for (mp_uint_t i = 0; i < size; i++) {
        // Busy-wait for a byte to arrive.
        while (uart_reg_read(self, LITEX_UART_RXEMPTY_OFFSET)) {
            MICROPY_EVENT_POLL_HOOK  // keep ctrl-C responsive
        }
        buf[i] = uart_reg_read(self, LITEX_UART_RXTX_OFFSET);
        // Ack the RX event so the EventManager IRQ line drops.
        uart_reg_write(self, LITEX_UART_EV_PENDING_OFFSET, LITEX_UART_EV_RX);
    }
    return size;
}

static mp_uint_t machine_uart_write(mp_obj_t self_in, const void *buf_in, mp_uint_t size, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    const uint8_t *buf = buf_in;
    for (mp_uint_t i = 0; i < size; i++) {
        while (uart_reg_read(self, LITEX_UART_TXFULL_OFFSET)) {
            MICROPY_EVENT_POLL_HOOK
        }
        uart_reg_write(self, LITEX_UART_RXTX_OFFSET, buf[i]);
    }
    return size;
}

static mp_uint_t machine_uart_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_uint_t ret;
    if (request == MP_STREAM_POLL) {
        uintptr_t flags = arg;
        ret = 0;
        if ((flags & MP_STREAM_POLL_RD)
            && !uart_reg_read(self, LITEX_UART_RXEMPTY_OFFSET)) {
            ret |= MP_STREAM_POLL_RD;
        }
        if ((flags & MP_STREAM_POLL_WR)
            && !uart_reg_read(self, LITEX_UART_TXFULL_OFFSET)) {
            ret |= MP_STREAM_POLL_WR;
        }
    } else {
        *errcode = MP_EINVAL;
        ret = MP_STREAM_ERROR;
    }
    return ret;
}

static const mp_rom_map_elem_t machine_uart_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_any),      MP_ROM_PTR(&machine_uart_any_obj) },
    // Stream protocol methods (read/readinto/readline/write) are injected
    // via MP_STREAM_ROM_OBJ below.
    { MP_ROM_QSTR(MP_QSTR_read),     MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_write),    MP_ROM_PTR(&mp_stream_write_obj) },
};
static MP_DEFINE_CONST_DICT(machine_uart_locals_dict, machine_uart_locals_dict_table);

static const mp_stream_p_t machine_uart_stream_p = {
    .read = machine_uart_read,
    .write = machine_uart_write,
    .ioctl = machine_uart_ioctl,
    .is_text = false,
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_uart_type,
    MP_QSTR_UART,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    make_new, machine_uart_make_new,
    print, machine_uart_print,
    protocol, &machine_uart_stream_p,
    locals_dict, &machine_uart_locals_dict
    );

#endif // LITEX_HAS_SECONDARY_UART
