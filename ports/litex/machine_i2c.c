// Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
// License: BSD-2-Clause
//
// machine.I2C(id=0, freq=400000) — hardware I2C wrapper for LiteX's
// LiteI2C master core (instantiated with SoC.add_i2c_master()).
//
// LiteI2C exposes a streaming transaction model at the CSR level
// (litei2c/litei2c/core/master.py):
//   CSR_I2CMASTER_ACTIVE        1 = master enabled
//   CSR_I2CMASTER_SETTINGS      bit [2:0]=len_tx, [10:8]=len_rx, [16]=recover
//   CSR_I2CMASTER_ADDR          7-bit device address
//   CSR_I2CMASTER_RXTX          32-bit data register; write to push a TX,
//                               read to consume an RX
//   CSR_I2CMASTER_STATUS        bit 0=tx_ready, 1=rx_ready, 8=nack,
//                               16=tx_unfinished, 17=rx_unfinished
//
// A single CSR_I2CMASTER_RXTX write commits one I2C transaction:
// START + addr(W/R) + up to 4 TX bytes + optional repeated-START + up
// to 4 RX bytes + STOP. Values of len_tx/len_rx > 4 announce "more data
// to follow" so larger transfers can be chained across multiple rxtx
// writes — that path is NOT implemented here yet; we chunk
// byte-at-a-time so each MicroPython call becomes a sequence of
// standalone I2C transactions. Slower than a single multi-byte burst,
// but correct and easy to reason about. Revisit if the performance hit
// matters.
//
// NOTE (scope): this code compiles against any SoC built with
// add_i2c_master(name="i2cmaster") but was not verified on hardware
// in the session that added it — the stock digilent_arty target
// doesn't route I2C pads to the add_i2c_master() helper. A user
// running this on a custom target with I2C pads wired up should expect
// to iterate the first time.

#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "extmod/modmachine.h"

#include <generated/csr.h>
#include <system.h>

#ifdef CSR_I2CMASTER_BASE

// Bit positions in CSR_I2CMASTER_SETTINGS.
#define LITEI2C_SETTINGS_LEN_TX_SHIFT   0
#define LITEI2C_SETTINGS_LEN_RX_SHIFT   8
#define LITEI2C_SETTINGS_RECOVER_BIT    (1u << 16)

// Bit positions in CSR_I2CMASTER_STATUS.
#define LITEI2C_STATUS_TX_READY         (1u << 0)
#define LITEI2C_STATUS_RX_READY         (1u << 1)
#define LITEI2C_STATUS_NACK             (1u << 8)

typedef struct _machine_i2c_obj_t {
    mp_obj_base_t base;
    uint32_t freq;      // Nominal clock rate; LiteI2C's actual rate is set
                        // at SoC gen time, so this is informational only.
} machine_i2c_obj_t;

const mp_obj_type_t machine_i2c_type;

static machine_i2c_obj_t machine_i2c_obj = { { &machine_i2c_type }, 400000 };

static void i2c_wait_tx_ready(void) {
    while (!(i2cmaster_status_read() & LITEI2C_STATUS_TX_READY)) {
        mp_event_handle_nowait();
    }
}

static void i2c_wait_rx_ready(void) {
    while (!(i2cmaster_status_read() & LITEI2C_STATUS_RX_READY)) {
        mp_event_handle_nowait();
    }
}

// Run one I2C transaction: START + addr + `tx_len` TX bytes from `tx` +
// optional repeated-START + `rx_len` RX bytes into `rx`. Each of tx_len,
// rx_len must be in [0, 4]. Returns 0 on success, -MP_EIO on NACK.
static int i2c_transact(uint8_t addr, const uint8_t *tx, unsigned tx_len,
    uint8_t *rx, unsigned rx_len) {
    if (tx_len > 4 || rx_len > 4) {
        return -MP_EINVAL;
    }

    i2c_wait_tx_ready();

    // Pack up to 4 TX bytes into the 32-bit rxtx word. Byte 0 first.
    uint32_t w = 0;
    for (unsigned i = 0; i < tx_len; i++) {
        w |= ((uint32_t)tx[i]) << (i * 8);
    }

    i2cmaster_addr_write(addr);
    i2cmaster_settings_write(
        (tx_len << LITEI2C_SETTINGS_LEN_TX_SHIFT) |
        (rx_len << LITEI2C_SETTINGS_LEN_RX_SHIFT));
    i2cmaster_rxtx_write(w);                 // triggers the I2C transaction

    if (rx_len > 0) {
        i2c_wait_rx_ready();
        uint32_t r = i2cmaster_rxtx_read();
        for (unsigned i = 0; i < rx_len; i++) {
            rx[i] = (r >> (i * 8)) & 0xff;
        }
    } else {
        // Even a pure write completes via rx_ready going high on the
        // ack/nack byte — drain the rxtx word to clear the FIFO.
        i2c_wait_rx_ready();
        (void)i2cmaster_rxtx_read();
    }

    if (i2cmaster_status_read() & LITEI2C_STATUS_NACK) {
        return -MP_EIO;
    }
    return 0;
}

// Python-level: machine.I2C(id, freq=)
static mp_obj_t machine_i2c_make_new(const mp_obj_type_t *type, size_t n_args,
    size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_id, ARG_freq };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id,   MP_ARG_INT,                  {.u_int = 0} },
        { MP_QSTR_freq, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 400000} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_id].u_int != 0) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("I2C(%d): only id 0 on LiteX"), args[ARG_id].u_int);
    }
    machine_i2c_obj.freq = (uint32_t)args[ARG_freq].u_int;

    i2cmaster_active_write(1);
    return MP_OBJ_FROM_PTR(&machine_i2c_obj);
}

static void machine_i2c_print(const mp_print_t *print, mp_obj_t self_in,
    mp_print_kind_t kind) {
    (void)kind;
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "I2C(0, freq=%u)", (unsigned int)self->freq);
}

// i2c.scan() — probe addresses 0x08..0x77, return list of those that ACK.
static mp_obj_t machine_i2c_scan(mp_obj_t self_in) {
    (void)self_in;
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (int addr = 0x08; addr <= 0x77; addr++) {
        // Probe with a 0-byte write — cheapest "is anyone there?" query.
        if (i2c_transact((uint8_t)addr, NULL, 0, NULL, 0) == 0) {
            mp_obj_list_append(list, MP_OBJ_NEW_SMALL_INT(addr));
        }
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_i2c_scan_obj, machine_i2c_scan);

// i2c.writeto(addr, buf) — writes buf in 4-byte chunks; each chunk is a
// separate I2C transaction with its own START/STOP. Larger atomic writes
// require the multi-word chaining path that isn't wired up yet.
static mp_obj_t machine_i2c_writeto(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    uint8_t addr = mp_obj_get_int(args[1]);
    mp_buffer_info_t buf;
    mp_get_buffer_raise(args[2], &buf, MP_BUFFER_READ);

    size_t written = 0;
    const uint8_t *p = (const uint8_t *)buf.buf;
    while (written < buf.len) {
        size_t chunk = buf.len - written;
        if (chunk > 4) {
            chunk = 4;
        }
        int rc = i2c_transact(addr, p + written, chunk, NULL, 0);
        if (rc != 0) {
            if (rc == -MP_EIO) {
                break;         // NACK — fall through to "short write"
            }
            mp_raise_OSError(-rc);
        }
        written += chunk;
    }
    return MP_OBJ_NEW_SMALL_INT(written);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_i2c_writeto_obj, 3, 4,
    machine_i2c_writeto);

// i2c.readfrom(addr, nbytes) — same chunking caveat as writeto.
static mp_obj_t machine_i2c_readfrom(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    uint8_t addr = mp_obj_get_int(args[1]);
    mp_int_t n = mp_obj_get_int(args[2]);
    if (n < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("nbytes must be >= 0"));
    }

    vstr_t vstr;
    vstr_init_len(&vstr, (size_t)n);
    uint8_t *dst = (uint8_t *)vstr.buf;

    size_t read = 0;
    while ((mp_int_t)read < n) {
        size_t chunk = (size_t)n - read;
        if (chunk > 4) {
            chunk = 4;
        }
        int rc = i2c_transact(addr, NULL, 0, dst + read, chunk);
        if (rc != 0) {
            if (rc == -MP_EIO) {
                break;
            }
            vstr_clear(&vstr);
            mp_raise_OSError(-rc);
        }
        read += chunk;
    }
    vstr.len = read;
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_i2c_readfrom_obj, 3, 4,
    machine_i2c_readfrom);

static const mp_rom_map_elem_t machine_i2c_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_scan),     MP_ROM_PTR(&machine_i2c_scan_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeto),  MP_ROM_PTR(&machine_i2c_writeto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readfrom), MP_ROM_PTR(&machine_i2c_readfrom_obj) },
};
static MP_DEFINE_CONST_DICT(machine_i2c_locals_dict,
    machine_i2c_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_i2c_type,
    MP_QSTR_I2C,
    MP_TYPE_FLAG_NONE,
    make_new, machine_i2c_make_new,
    print, machine_i2c_print,
    locals_dict, &machine_i2c_locals_dict
    );

#endif // CSR_I2CMASTER_BASE
