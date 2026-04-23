// Copyright (c) 2026 Florent Kermarrec <florent@enjoy-digital.fr>
// License: BSD-2-Clause
//
// litex.SPIFlash() — block-device wrapper around the memory-mapped LiteSPI
// flash core (gated on CSR_SPIFLASH_BASE). Reads are zero-copy from the
// XIP region at SPIFLASH_BASE; writes go through liblitespi's
// spiflash_write_stream() / spiflash_erase_range() so the device's program
// + erase timings are honoured.
//
// Implements the MicroPython block-device protocol with a 4096-byte block
// size (== SPI flash 4 KiB sector erase unit) so it can be passed to
// uos.VfsFat.mkfs() / uos.mount() directly:
//
//     import os, litex
//     f = litex.SPIFlash()
//     # one-time, wipes the flash partition:
//     # os.VfsFat.mkfs(f)
//     os.mount(f, "/flash")
//
// Note: the MicroPython firmware itself usually lives at the start of
// SPIFLASH_BASE (loaded by the LiteX BIOS at boot). Mounting the whole
// flash and writing to it WILL overwrite the firmware. Most users will
// want to carve off a partition above the firmware — see the .offset()
// constructor arg.

#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/obj.h"
#include "py/binary.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "extmod/vfs.h"

#include <generated/csr.h>
#include <generated/mem.h>
#include <generated/soc.h>
#include <system.h>

#if defined(CSR_SPIFLASH_BASE) && defined(SPIFLASH_BASE)

// liblitespi headers come from -I$(SOC_DIRECTORY)/software (set in Makefile).
#include <liblitespi/spiflash.h>

// MicroPython block-device protocol ioctl codes (extmod/vfs.h).
#ifndef MP_BLOCKDEV_IOCTL_INIT
#define MP_BLOCKDEV_IOCTL_INIT          (1)
#define MP_BLOCKDEV_IOCTL_DEINIT        (2)
#define MP_BLOCKDEV_IOCTL_SYNC          (3)
#define MP_BLOCKDEV_IOCTL_BLOCK_COUNT   (4)
#define MP_BLOCKDEV_IOCTL_BLOCK_SIZE    (5)
#define MP_BLOCKDEV_IOCTL_BLOCK_ERASE   (6)
#endif

// Match liblitespi's native erase unit (SPI_FLASH_ERASE_SIZE = 64 KiB).
// We tried 4 KiB but liblitespi's spiflash_erase_range() uses the 64 KiB
// sector-erase opcode (0xd8) and its post-erase verify loop checks 64 KiB
// regardless of `len`, so a 4 KiB request reads back 60 KiB of NOT-erased
// data and falsely reports failure. Using 64 KiB blocks makes the erase
// match the verify; FatFs handles the larger sector size transparently.
// Trade-off: a single dirty byte in a partition costs a 64 KiB erase
// cycle, which on a typical SPI flash is ~150 ms — fine for config
// storage, slow for log-heavy workloads.
#define LITEX_SPIFLASH_BLOCK_SIZE (64 * 1024)

typedef struct _litex_spiflash_obj_t {
    mp_obj_base_t base;
    uint32_t offset;        // bytes into SPIFLASH_BASE the block device starts at
    uint32_t size;          // bytes the block device covers (offset..offset+size)
    bool initialised;       // set after first successful ioctl(INIT)
} litex_spiflash_obj_t;

const mp_obj_type_t litex_spiflash_type;

// litex.SPIFlash(offset=0, size=None) — create a block-device view onto
// the SPI flash. With defaults, the view covers the whole memory-mapped
// region (DANGER: includes the firmware's own backing pages). Pass a
// safe offset (e.g. 0x100000 = 1 MiB above firmware) to carve off a
// partition for user storage.
static mp_obj_t litex_spiflash_make_new(const mp_obj_type_t *type, size_t n_args,
    size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_offset, ARG_size };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_offset, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_size,   MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    uint32_t offset = (uint32_t)args[ARG_offset].u_int;
    uint32_t size;
    if (args[ARG_size].u_obj == mp_const_none) {
        size = (uint32_t)SPIFLASH_SIZE - offset;
    } else {
        size = (uint32_t)mp_obj_get_int(args[ARG_size].u_obj);
    }

    if (offset % LITEX_SPIFLASH_BLOCK_SIZE != 0
        || size % LITEX_SPIFLASH_BLOCK_SIZE != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT(
            "offset and size must be multiples of 4096 (SPI flash sector)"));
    }
    if ((uint64_t)offset + (uint64_t)size > (uint64_t)SPIFLASH_SIZE) {
        mp_raise_ValueError(MP_ERROR_TEXT("offset+size beyond flash end"));
    }

    litex_spiflash_obj_t *self = mp_obj_malloc(litex_spiflash_obj_t, type);
    self->offset = offset;
    self->size = size;
    self->initialised = false;
    return MP_OBJ_FROM_PTR(self);
}

static void litex_spiflash_print(const mp_print_t *print, mp_obj_t self_in,
    mp_print_kind_t kind) {
    (void)kind;
    litex_spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "SPIFlash(offset=0x%lx, size=0x%lx)",
        (unsigned long)self->offset, (unsigned long)self->size);
}

// readblocks(block_num, buf) — copy from the XIP region. The CPU dcache
// has to be invalidated for the range first because the LiteSPI write
// path bypasses the cache, and a previous .writeblocks() may have left
// stale lines.
static mp_obj_t litex_spiflash_readblocks(mp_obj_t self_in, mp_obj_t block_num_in,
    mp_obj_t buf_in) {
    litex_spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t block = mp_obj_get_int(block_num_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);

    uint32_t off = self->offset + block * LITEX_SPIFLASH_BLOCK_SIZE;
    if (off + bufinfo.len > self->offset + self->size) {
        mp_raise_OSError(MP_EIO);
    }
    void *src = (void *)((uintptr_t)SPIFLASH_BASE + off);
    #ifndef CONFIG_CPU_HAS_DMA_BUS
    flush_cpu_dcache();
    #endif
    memcpy(bufinfo.buf, src, bufinfo.len);
    return MP_OBJ_NEW_SMALL_INT(0);     // 0 == success per blockdev protocol
}
static MP_DEFINE_CONST_FUN_OBJ_3(litex_spiflash_readblocks_obj,
    litex_spiflash_readblocks);

// writeblocks(block_num, buf) — erase the affected sectors then program
// via liblitespi. Caller is responsible for buf being a multiple of the
// block size (FatFs honours this).
static mp_obj_t litex_spiflash_writeblocks(mp_obj_t self_in, mp_obj_t block_num_in,
    mp_obj_t buf_in) {
    litex_spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t block = mp_obj_get_int(block_num_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);

    uint32_t off = self->offset + block * LITEX_SPIFLASH_BLOCK_SIZE;
    if (off + bufinfo.len > self->offset + self->size) {
        mp_raise_OSError(MP_EIO);
    }
    spiflash_erase_range(off, bufinfo.len);
    // Counter-intuitive return contract: spiflash_write_stream() returns
    // the number of bytes actually written, not a 0/non-zero error code
    // (it's the offset of the last byte programmed). A short write means
    // a programming failure mid-stream — surface as EIO.
    int written = spiflash_write_stream(off, (uint8_t *)bufinfo.buf, bufinfo.len);
    if ((uint32_t)written != bufinfo.len) {
        mp_raise_OSError(MP_EIO);
    }
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_3(litex_spiflash_writeblocks_obj,
    litex_spiflash_writeblocks);

static mp_obj_t litex_spiflash_ioctl(mp_obj_t self_in, mp_obj_t cmd_in,
    mp_obj_t arg_in) {
    litex_spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t cmd = mp_obj_get_int(cmd_in);
    switch (cmd) {
        case MP_BLOCKDEV_IOCTL_INIT:
            if (!self->initialised) {
                spiflash_init();
                self->initialised = true;
            }
            return MP_OBJ_NEW_SMALL_INT(0);

        case MP_BLOCKDEV_IOCTL_DEINIT:
            self->initialised = false;
            return MP_OBJ_NEW_SMALL_INT(0);

        case MP_BLOCKDEV_IOCTL_SYNC:
            return MP_OBJ_NEW_SMALL_INT(0);

        case MP_BLOCKDEV_IOCTL_BLOCK_COUNT:
            return MP_OBJ_NEW_SMALL_INT(self->size / LITEX_SPIFLASH_BLOCK_SIZE);

        case MP_BLOCKDEV_IOCTL_BLOCK_SIZE:
            return MP_OBJ_NEW_SMALL_INT(LITEX_SPIFLASH_BLOCK_SIZE);

        case MP_BLOCKDEV_IOCTL_BLOCK_ERASE: {
            uint32_t block = mp_obj_get_int(arg_in);
            uint32_t off = self->offset + block * LITEX_SPIFLASH_BLOCK_SIZE;
            if (off >= self->offset + self->size) {
                return MP_OBJ_NEW_SMALL_INT(-1);
            }
            // erase_range with len == BLOCK_SIZE matches the 64 KiB
            // hardware sector and its built-in verify loop; calling
            // erase_4k_sector here would silently corrupt because of the
            // size mismatch documented above LITEX_SPIFLASH_BLOCK_SIZE.
            spiflash_erase_range(off, LITEX_SPIFLASH_BLOCK_SIZE);
            return MP_OBJ_NEW_SMALL_INT(0);
        }

        default:
            return MP_OBJ_NEW_SMALL_INT(-1);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_3(litex_spiflash_ioctl_obj, litex_spiflash_ioctl);

// .size() — total bytes covered by this view. Useful for "where can I
// safely write?" sanity checks.
static mp_obj_t litex_spiflash_size(mp_obj_t self_in) {
    litex_spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(self->size);
}
static MP_DEFINE_CONST_FUN_OBJ_1(litex_spiflash_size_obj, litex_spiflash_size);

static const mp_rom_map_elem_t litex_spiflash_locals_dict_table[] = {
    // Block device protocol — what extmod/vfs_fat.c calls into.
    { MP_ROM_QSTR(MP_QSTR_readblocks),  MP_ROM_PTR(&litex_spiflash_readblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeblocks), MP_ROM_PTR(&litex_spiflash_writeblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_ioctl),       MP_ROM_PTR(&litex_spiflash_ioctl_obj) },
    // Convenience.
    { MP_ROM_QSTR(MP_QSTR_size),        MP_ROM_PTR(&litex_spiflash_size_obj) },
};
static MP_DEFINE_CONST_DICT(litex_spiflash_locals_dict,
    litex_spiflash_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    litex_spiflash_type,
    MP_QSTR_SPIFlash,
    MP_TYPE_FLAG_NONE,
    make_new, litex_spiflash_make_new,
    print, litex_spiflash_print,
    locals_dict, &litex_spiflash_locals_dict
    );

#endif // CSR_SPIFLASH_BASE
