/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 "Eric Poulsen" <eric@zyxod.com>
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
 *
 */
//portions from spisdcard.c
// This file is Copyright (c) 2020 Florent Kermarrec <florent@enjoy-digital.fr>
// This file is Copyright (c) 2020 Rob Shelton <rob.s.ng15@googlemail.com>
// License: BSD

//This module, meant for LiteX, is based on machine_hw_spi.c from the ESP32 port.
//Since most code is kept without modification, a generic module with
//basic functionality could be written that only depends on a thin HAL
//to ease code reuse and porting, useful for both for ESP32 and LiteX
//implementation or for future platforms
//
//SPI functions for accessing CSR register are from spisdcard.c
//ideally should be also merged (spisdcard_* functions are replaced
//by spi0_*)

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/stream.h"
#include "py/mphal.h"
#include "extmod/machine_spi.h"

//NOTE: if support for ESP32 in same will never be required, all code
//within all references to "#ifdef ESP32" can be removed
#ifdef ESP32
#include "modmachine.h"
#include "driver/spi_master.h"
#define USE_HARDWARE_SPI
#else
extern const mp_obj_type_t machine_hw_spi_type; //forward declaration
#ifdef CSR_SPI_BASE
//rename methdos: TODO: better, define a SDK module
#define spi0_cs_write spi_cs_write
#define spi0_cs_read spi_cs_read
#define spi0_control_write spi_control_write
#define spi0_status_read spi_status_read
#define spi0_miso_read spi_miso_read
#define spi0_mosi_write spi_mosi_write
#define spi0_clk_divider_write spi_clk_divider_write
#endif
#endif

#ifdef USE_HARDWARE_SPI

// Default pins for SPI(0), can be overridden by a board
#ifndef MICROPY_HW_SPI1_SCK
#define MICROPY_HW_SPI1_SCK (0)
#define MICROPY_HW_SPI1_MOSI (1)
#define MICROPY_HW_SPI1_MISO (2)
#endif

#ifndef ESP32
//TODO: any micropyhon SPI implementation could be refactored to require
//only the following functions as hardware specialization:
//spi_set_clk_freq, spi_xfer, spi_select, spi_deselect

/*-----------------------------------------------------------------------*/
/* SPI Master Flags                                                      */
/*-----------------------------------------------------------------------*/

#define SPI_CS_HIGH (0 << 0)
#define SPI_CS_LOW  (1 << 0)
#define SPI_CS_MANUAL  (1 << 16)
#define SPI_START   (1 << 0)
#define SPI_DONE    (1 << 0)
#define SPI_LENGTH  (1 << 8)

/*-----------------------------------------------------------------------*/
/* SPI SDCard clocker functions                                          */
/*-----------------------------------------------------------------------*/

static void spi_set_clk_freq(uint32_t clk_freq) {
    uint32_t divider;
    divider = CONFIG_CLOCK_FREQUENCY/clk_freq + 1;
    divider = divider < 2 ? 2:divider; //min
    divider = divider > 256 ? 256:divider; //max
#ifdef _DEBUG
    printf("Setting SPI clk freq to ");
    if (clk_freq > 1000000)
        printf("%ld MHz\n", (CONFIG_CLOCK_FREQUENCY/divider)/1000000);
    else
        printf("%ld KHz\n", (CONFIG_CLOCK_FREQUENCY/divider)/1000);
#endif
    spi0_clk_divider_write(divider);
}

/*-----------------------------------------------------------------------*/
/* SPI Select/Deselect functions                                         */
/*-----------------------------------------------------------------------*/

static int spi_select(void) {
    /* Set SPI CS Low */
    spi0_cs_write(SPI_CS_LOW | SPI_CS_MANUAL);
    return 0;
}

static void spi_deselect(void) {
    /* Set SPI CS High */
    spi0_cs_write(SPI_CS_HIGH | SPI_CS_MANUAL);
}

/*-----------------------------------------------------------------------*/
/* SPI bytes Xfer functions                                              */
/*-----------------------------------------------------------------------*/

static uint8_t spi_xfer(uint8_t byte) {
    /* Write byte on MOSI */
    spi0_mosi_write(byte);
    /* Initiate SPI Xfer */
    spi0_control_write(8*SPI_LENGTH | SPI_START);
    /* Wait SPI Xfer to be done */
    while(spi0_status_read() != SPI_DONE);
    /* Read MISO and return it */
    return spi0_miso_read();
}

//all following functions are not platform depenent
static void spi_write_bytes(const uint8_t* buf, size_t n) {
    size_t i;
    for (i=0; i<n; i++)
        spi_xfer(buf[i]);
}

static void spi_read_bytes(uint8_t* buf, size_t n) {
    size_t i;
    for (i=0; i<n; i++)
        buf[i] = spi_xfer(0xff);
}

static void spi_readwrite_bytes(uint8_t* dest, const uint8_t *src, size_t n) {
    size_t i;
    for (i=0; i<n; i++)
        dest[i] = spi_xfer(src[i]);
}

#else

#define MP_HW_SPI_MAX_XFER_BYTES (4092)
#define MP_HW_SPI_MAX_XFER_BITS (MP_HW_SPI_MAX_XFER_BYTES * 8) // Has to be an even multiple of 8

#endif //ESP32

typedef struct _machine_hw_spi_default_pins_t {
    int8_t sck;
    int8_t mosi;
    int8_t miso;
} machine_hw_spi_default_pins_t;

#ifndef ESP32
typedef uint8_t spi_host_device_t;
#endif

typedef struct _machine_hw_spi_obj_t {
    mp_obj_base_t base;
    spi_host_device_t host;
    uint32_t baudrate;
    uint8_t polarity;
    uint8_t phase;
    uint8_t bits;
    uint8_t firstbit;
    int8_t sck;
    int8_t mosi;
    int8_t miso;
#ifdef ESP32
    spi_device_handle_t spi;
#endif
    enum {
        MACHINE_HW_SPI_STATE_NONE,
        MACHINE_HW_SPI_STATE_INIT,
        MACHINE_HW_SPI_STATE_DEINIT
    } state;
} machine_hw_spi_obj_t;

// Default pin mappings for the hardware SPI instances
STATIC const machine_hw_spi_default_pins_t machine_hw_spi_default_pins[] = {
    { .sck = MICROPY_HW_SPI1_SCK, .mosi = MICROPY_HW_SPI1_MOSI, .miso = MICROPY_HW_SPI1_MISO },
#ifdef MICROPY_HW_SPI2_SCK
    { .sck = MICROPY_HW_SPI2_SCK, .mosi = MICROPY_HW_SPI2_MOSI, .miso = MICROPY_HW_SPI2_MISO },
#endif
};

#define SPI_COUNT (sizeof(machine_hw_spi_default_pins)/sizeof(*machine_hw_spi_default_pins))
// Static objects mapping to HSPI and VSPI hardware peripherals
STATIC machine_hw_spi_obj_t machine_hw_spi_obj[SPI_COUNT];

STATIC void machine_hw_spi_deinit_internal(machine_hw_spi_obj_t *self) {
#ifndef ESP32
    //printf("in machine_hw_spi_deinit_internal()");
    spi_deselect(); //force deselect

#else
    switch (spi_bus_remove_device(self->spi)) {
        case ESP_ERR_INVALID_ARG:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("invalid configuration"));
            return;

        case ESP_ERR_INVALID_STATE:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("SPI device already freed"));
            return;
    }
    switch (spi_bus_free(self->host)) {
        case ESP_ERR_INVALID_ARG:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("invalid configuration"));
            return;

        case ESP_ERR_INVALID_STATE:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("SPI bus already freed"));
            return;
    }
#endif
#ifndef ESP32
    if(self->host >= 0) //hardware SPI doesn't need to set GPIO
        return;
#endif
    int8_t pins[3] = {self->miso, self->mosi, self->sck};
    printf("in machine_hw_spi_deinit_internal(), sck=%d, mosi=%d, miso=%d\n", self->sck, self->mosi, self->miso);

    for (int i = 0; i < 3; i++) {
        if (pins[i] != -1) {
#ifdef ESP32
            gpio_pad_select_gpio(pins[i]);
            gpio_matrix_out(pins[i], SIG_GPIO_OUT_IDX, false, false);
            gpio_set_direction(pins[i], GPIO_MODE_INPUT);
#else
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("spi pin deinitialization not implemented"));
#endif
        }
    }
}


STATIC void machine_hw_spi_init_internal(
    machine_hw_spi_obj_t *self,
    int8_t host,
    int32_t baudrate,
    int8_t polarity,
    int8_t phase,
    int8_t bits,
    int8_t firstbit,
    int8_t sck,
    int8_t mosi,
    int8_t miso) {

    // if we're not initialized, then we're
    // implicitly 'changed', since this is the init routine
    bool changed = self->state != MACHINE_HW_SPI_STATE_INIT;

#ifdef ESP32
    esp_err_t ret;
#endif

    machine_hw_spi_obj_t old_self = *self;

    if (host != -1 && host != self->host) {
        self->host = host;
        changed = true;
    }

    if (baudrate != -1) {
#ifdef ESP32
        // calculate the actual clock frequency that the SPI peripheral can produce
        baudrate = spi_get_actual_clock(APB_CLK_FREQ, baudrate, 0);
#endif
        if (baudrate != self->baudrate) {
            self->baudrate = baudrate;
            changed = true;
        }
    }

    if (polarity != -1 && polarity != self->polarity) {
        self->polarity = polarity;
        changed = true;
    }

    if (phase != -1 && phase != self->phase) {
        self->phase = phase;
        changed = true;
    }

    if (bits != -1 && bits != self->bits) {
        self->bits = bits;
        changed = true;
    }

    if (firstbit != -1 && firstbit != self->firstbit) {
        self->firstbit = firstbit;
        changed = true;
    }

    if (sck != -2 && sck != self->sck) {
        self->sck = sck;
        changed = true;
    }

    if (mosi != -2 && mosi != self->mosi) {
        self->mosi = mosi;
        changed = true;
    }

    if (miso != -2 && miso != self->miso) {
        self->miso = miso;
        changed = true;
    }

    if (
#ifdef ESP32
        self->host != HSPI_HOST
#else
        self->host >= SPI_COUNT
#endif
        #ifdef VSPI_HOST
        && self->host != VSPI_HOST
        #endif
        ) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("SPI(%d) doesn't exist"), self->host);
    }

    if (changed) {
        if (self->state == MACHINE_HW_SPI_STATE_INIT) {
            self->state = MACHINE_HW_SPI_STATE_DEINIT;
            machine_hw_spi_deinit_internal(&old_self);
        }
    } else {
        return; // no changes
    }

#ifdef ESP32
    spi_bus_config_t buscfg = {
        .miso_io_num = self->miso,
        .mosi_io_num = self->mosi,
        .sclk_io_num = self->sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = self->baudrate,
        .mode = self->phase | (self->polarity << 1),
        .spics_io_num = -1, // No CS pin
        .queue_size = 2,
        .flags = self->firstbit == MICROPY_PY_MACHINE_SPI_LSB ? SPI_DEVICE_TXBIT_LSBFIRST | SPI_DEVICE_RXBIT_LSBFIRST : 0,
        .pre_cb = NULL
    };

    // Initialize the SPI bus

    // Select DMA channel based on the hardware SPI host
    int dma_chan = 0;
    if (self->host == HSPI_HOST) {
        dma_chan = 1;
    #ifdef VSPI_HOST
    } else if (self->host == VSPI_HOST) {
        dma_chan = 2;
    #endif
    }

    ret = spi_bus_initialize(self->host, &buscfg, dma_chan);
    switch (ret) {
        case ESP_ERR_INVALID_ARG:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("invalid configuration"));
            return;

        case ESP_ERR_INVALID_STATE:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("SPI host already in use"));
            return;
    }

    ret = spi_bus_add_device(self->host, &devcfg, &self->spi);
    switch (ret) {
        case ESP_ERR_INVALID_ARG:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("invalid configuration"));
            spi_bus_free(self->host);
            return;

        case ESP_ERR_NO_MEM:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("out of memory"));
            spi_bus_free(self->host);
            return;

        case ESP_ERR_NOT_FOUND:
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no free slots"));
            spi_bus_free(self->host);
            return;
    }
#else
    if(bits != 8)
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Only 8 bits are supported"));

    spi_deselect();
    spi_set_clk_freq(self->baudrate);
    //printf("in machine_hw_spi_init_internal(), baudrate=%ld\n", self->baudrate);
#endif
    self->state = MACHINE_HW_SPI_STATE_INIT;
}

STATIC void machine_hw_spi_deinit(mp_obj_base_t *self_in) {
    machine_hw_spi_obj_t *self = (machine_hw_spi_obj_t *)self_in;
    if (self->state == MACHINE_HW_SPI_STATE_INIT) {
        self->state = MACHINE_HW_SPI_STATE_DEINIT;
        machine_hw_spi_deinit_internal(self);
    }
}

#ifdef ESP32
STATIC mp_uint_t gcd(mp_uint_t x, mp_uint_t y) {
    while (x != y) {
        if (x > y) {
            x -= y;
        } else {
            y -= x;
        }
    }
    return x;
}
#endif

STATIC void machine_hw_spi_transfer(mp_obj_base_t *self_in, size_t len, const uint8_t *src, uint8_t *dest) {
    machine_hw_spi_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->state == MACHINE_HW_SPI_STATE_DEINIT) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("transfer on deinitialized SPI"));
        return;
    }

    // Round to nearest whole set of bits
    int bits_to_send = len * 8 / self->bits * self->bits;

    if (!bits_to_send) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer too short"));
    }

#ifdef ESP32
    if (len <= 4) {
        spi_transaction_t transaction = { 0 };

        if (src != NULL) {
            memcpy(&transaction.tx_data, src, len);
        }

        transaction.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
        transaction.length = bits_to_send;

        spi_device_transmit(self->spi, &transaction);

        if (dest != NULL) {
            memcpy(dest, &transaction.rx_data, len);
        }
    } else {
        int offset = 0;
        int bits_remaining = bits_to_send;
        int optimum_word_size = 8 * self->bits / gcd(8, self->bits);
        int max_transaction_bits = MP_HW_SPI_MAX_XFER_BITS / optimum_word_size * optimum_word_size;
        spi_transaction_t *transaction, *result, transactions[2];
        int i = 0;

        spi_device_acquire_bus(self->spi, portMAX_DELAY);

        while (bits_remaining) {
            transaction = transactions + i++ % 2;
            memset(transaction, 0, sizeof(spi_transaction_t));

            transaction->length =
                bits_remaining > max_transaction_bits ? max_transaction_bits : bits_remaining;

            if (src != NULL) {
                transaction->tx_buffer = src + offset;
            }
            if (dest != NULL) {
                transaction->rx_buffer = dest + offset;
            }

            spi_device_queue_trans(self->spi, transaction, portMAX_DELAY);
            bits_remaining -= transaction->length;

            if (offset > 0) {
                // wait for previously queued transaction
                spi_device_get_trans_result(self->spi, &result, portMAX_DELAY);
            }

            // doesn't need ceil(); loop ends when bits_remaining is 0
            offset += transaction->length / 8;
        }

        // wait for last transaction
        spi_device_get_trans_result(self->spi, &result, portMAX_DELAY);
        spi_device_release_bus(self->spi);
    }
#else
#ifdef _DEBUG
    printf("in machine_hw_spi_transfer(), src=%p, dest=%p, len=%d\n", src, dest, len);
#endif
    spi_select();
    if(dest != NULL && src != NULL)
      spi_readwrite_bytes(dest, src, len);
    else if(src != NULL)
      spi_write_bytes(src, len);
    else if(dest != NULL)
      spi_read_bytes(dest, len);

    spi_deselect();
#endif
}

/******************************************************************************/
// MicroPython bindings for hw_spi

STATIC void machine_hw_spi_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_hw_spi_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "SPI(id=%u, baudrate=%u, polarity=%u, phase=%u, bits=%u, firstbit=%u, sck=%d, mosi=%d, miso=%d)",
        self->host, self->baudrate, self->polarity,
        self->phase, self->bits, self->firstbit,
        self->sck, self->mosi, self->miso);
}

STATIC void machine_hw_spi_init(mp_obj_base_t *self_in, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_hw_spi_obj_t *self = (machine_hw_spi_obj_t *)self_in;

    enum { ARG_id, ARG_baudrate, ARG_polarity, ARG_phase, ARG_bits, ARG_firstbit, ARG_sck, ARG_mosi, ARG_miso };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id,       MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_polarity, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_phase,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_bits,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_firstbit, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_sck,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_mosi,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_miso,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args),
        allowed_args, args);
    int8_t sck, mosi, miso;

    if (args[ARG_sck].u_obj == MP_OBJ_NULL) {
        sck = -2;
    } else if (args[ARG_sck].u_obj == mp_const_none) {
        sck = -1;
    } else {
        sck = mp_hal_get_pin_obj(args[ARG_sck].u_obj);
    }

    if (args[ARG_miso].u_obj == MP_OBJ_NULL) {
        miso = -2;
    } else if (args[ARG_miso].u_obj == mp_const_none) {
        miso = -1;
    } else {
        miso = mp_hal_get_pin_obj(args[ARG_miso].u_obj);
    }

    if (args[ARG_mosi].u_obj == MP_OBJ_NULL) {
        mosi = -2;
    } else if (args[ARG_mosi].u_obj == mp_const_none) {
        mosi = -1;
    } else {
        mosi = mp_hal_get_pin_obj(args[ARG_mosi].u_obj);
    }

    machine_hw_spi_init_internal(self, args[ARG_id].u_int, args[ARG_baudrate].u_int,
        args[ARG_polarity].u_int, args[ARG_phase].u_int, args[ARG_bits].u_int,
        args[ARG_firstbit].u_int, sck, mosi, miso);
}

mp_obj_t machine_hw_spi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    MP_MACHINE_SPI_CHECK_FOR_LEGACY_SOFTSPI_CONSTRUCTION(n_args, n_kw, all_args);

    enum { ARG_id, ARG_baudrate, ARG_polarity, ARG_phase, ARG_bits, ARG_firstbit, ARG_sck, ARG_mosi, ARG_miso };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id,       MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = 500000} },
        { MP_QSTR_polarity, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_phase,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_bits,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 8} },
        { MP_QSTR_firstbit, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = MICROPY_PY_MACHINE_SPI_MSB} },
        { MP_QSTR_sck,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_mosi,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_miso,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    machine_hw_spi_obj_t *self;
    const machine_hw_spi_default_pins_t *default_pins;
#ifdef ESP32
    if (args[ARG_id].u_int == HSPI_HOST) {
        self = &machine_hw_spi_obj[0];
        default_pins = &machine_hw_spi_default_pins[0];
    } else {
        self = &machine_hw_spi_obj[1];
        default_pins = &machine_hw_spi_default_pins[1];
    }
#else
    spi_host_device_t idspi = args[ARG_id].u_int;
    //printf("in machine_hw_spi_make_new(), id=%d\n", idspi);
    if(idspi < SPI_COUNT)
    {
        self = &machine_hw_spi_obj[idspi];
        default_pins = &machine_hw_spi_default_pins[idspi];
    }
    else
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid SPI id"));
#endif
    self->base.type = &machine_hw_spi_type;

    int8_t sck, mosi, miso;

    if (args[ARG_sck].u_obj == MP_OBJ_NULL) {
        sck = default_pins->sck;
    } else if (args[ARG_sck].u_obj == mp_const_none) {
        sck = -1;
    } else {
        sck = mp_hal_get_pin_obj(args[ARG_sck].u_obj);
    }

    if (args[ARG_mosi].u_obj == MP_OBJ_NULL) {
        mosi = default_pins->mosi;
    } else if (args[ARG_mosi].u_obj == mp_const_none) {
        mosi = -1;
    } else {
        mosi = mp_hal_get_pin_obj(args[ARG_mosi].u_obj);
    }

    if (args[ARG_miso].u_obj == MP_OBJ_NULL) {
        miso = default_pins->miso;
    } else if (args[ARG_miso].u_obj == mp_const_none) {
        miso = -1;
    } else {
        miso = mp_hal_get_pin_obj(args[ARG_miso].u_obj);
    }

    machine_hw_spi_init_internal(
        self,
        args[ARG_id].u_int,
        args[ARG_baudrate].u_int,
        args[ARG_polarity].u_int,
        args[ARG_phase].u_int,
        args[ARG_bits].u_int,
        args[ARG_firstbit].u_int,
        sck,
        mosi,
        miso);

    return MP_OBJ_FROM_PTR(self);
}

STATIC const mp_machine_spi_p_t machine_hw_spi_p = {
    .init = machine_hw_spi_init,
    .deinit = machine_hw_spi_deinit,
    .transfer = machine_hw_spi_transfer,
};

const mp_obj_type_t machine_hw_spi_type = {
    { &mp_type_type },
    .name = MP_QSTR_SPI,
    .print = machine_hw_spi_print,
    .make_new = machine_hw_spi_make_new,
    .protocol = &machine_hw_spi_p,
    .locals_dict = (mp_obj_dict_t *)&mp_machine_spi_locals_dict,
};

#endif //USE_HARDWARE_SPI

