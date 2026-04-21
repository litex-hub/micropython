// This file is Copyright (c) 2017-2021 Fupy/LiteX-MicroPython Developers
// This file is Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
// License: BSD-2-Clause

#include <stdint.h>

// Python internal features.
#define MICROPY_ENABLE_COMPILER     (1)
#define MICROPY_ENABLE_GC           (1)
#define MICROPY_HELPER_REPL         (1)
#define MICROPY_ERROR_REPORTING     (MICROPY_ERROR_REPORTING_TERSE)
#define MICROPY_LONGINT_IMPL        (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_FLOAT_IMPL          (MICROPY_FLOAT_IMPL_DOUBLE)

// Python builtins, classes, modules, etc. features.
#define MICROPY_PY_MATH             (1)
#define MICROPY_PY_CMATH            (1)
#define MICROPY_PY_SYS              (1)
#define MICROPY_PY_MACHINE          (1)
#define MICROPY_KBD_EXCEPTION       (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW (1)

// extended modules
#define MICROPY_PY_MACHINE_SPI              (1)
#define MICROPY_PY_MACHINE_SPI_MSB          (1)
#define MICROPY_PY_MACHINE_SPI_LSB          (0)
#define MICROPY_PY_MACHINE_I2C              (1)
#define MICROPY_PY_UTIME_MP_HAL             (1)

// Type definitions for the specific machine

typedef intptr_t  mp_int_t;
typedef uintptr_t mp_uint_t;
typedef long      mp_off_t;


#include <generated/csr.h>
#include <generated/mem.h>

#ifdef MAIN_RAM_BASE
#define MICROPY_HW_SDRAM_AVAIL (1)
#define MICROPY_HW_SDRAM_BASE MAIN_RAM_BASE
#define MICROPY_HW_SDRAM_SIZE MAIN_RAM_SIZE
#endif
#ifdef CSR_VIDEO_FRAMEBUFFER_BASE
#define MICROPY_PY_FRAMEBUF (1)
#endif

#if defined(CSR_SPI_BASE) || defined (CSR_SPI0_BASE)
#define USE_HARDWARE_SPI
#endif

#if MICROPY_VFS_FAT
#define MICROPY_HW_ENABLE_SDCARD            (1)
#define MICROPY_FATFS_RPATH            (2)
#ifdef MICROPY_FATFS_RPATH
#define FF_FS_RPATH (MICROPY_FATFS_RPATH)
#else
#define FF_FS_RPATH 0
#endif

// Whether to automatically mount (and boot from) the SD card if it's present
#ifndef MICROPY_HW_SDCARD_MOUNT_AT_BOOT
//#define MICROPY_HW_SDCARD_MOUNT_AT_BOOT (MICROPY_HW_ENABLE_SDCARD) //not enabled by default
#endif

#define MICROPY_FATFS_MULTI_PARTITION (1)
#else //not MICROPY_VFS_FAT
#undef MICROPY_VFS_FAT  //if there's no SD core, disable VFS fat
#define MICROPY_VFS_FAT (0)
#endif //MICROPY_VFS_FAT

#if MICROPY_VFS_FAT
#define MICROPY_VFS MICROPY_VFS_FAT
#define MICROPY_PY_IO               (1)
#define MICROPY_PY_IO_IOBASE        (1)
#define MICROPY_PY_SYS_STDFILES     (1)
#define MICROPY_PY_IO_FILEIO        (MICROPY_VFS_FAT || MICROPY_VFS_LFS1 || MICROPY_VFS_LFS2)
#define mp_type_fileio mp_type_vfs_fat_fileio
#define mp_type_textio mp_type_vfs_fat_textio
#define MICROPY_FATFS_EXFAT         (1)
#define MICROPY_FATFS_ENABLE_LFN    (1)
// use vfs's functions for import stat and builtin open
#define mp_import_stat mp_vfs_import_stat
#define mp_builtin_open mp_vfs_open
#define mp_builtin_open_obj mp_vfs_open_obj
#endif
#define MICROPY_READER_VFS              (MICROPY_VFS_FAT)

#define MICROPY_PY_SYS_PLATFORM "LiteX (" CONFIG_BUS_STANDARD " bus)" //TODO: use board name from SoC generation

#define TIMER0_POLLING //interrupt handing not enabled yet
#ifdef CSR_TIMER0_UPTIME_LATCH_ADDR
//TODO: use SDK
static inline uint64_t litex_uptime() {  timer0_uptime_latch_write(1); return timer0_uptime_cycles_read(); }
#else
//calibrated for sleep_ms / sleep
static inline uint64_t litex_uptime() { static uint64_t uptime = 0; return uptime+=250; }
#endif

#ifdef CSR_TIMER0_UPTIME_CYCLES_ADDR
void litex_delay_cycles(uint64_t c); //TODO: maybe a faster implementation can be limited to 32 bits
static inline void mp_hal_delay_us_fast(mp_uint_t us) {  uint64_t c = us; c *= CONFIG_CLOCK_FREQUENCY; c /= 1000000; litex_delay_cycles(c); }
#else
static inline void mp_hal_delay_us_fast(mp_uint_t us) { us*=4; volatile static uint8_t t; while(us--) ++t; }
#endif
#define mp_hal_delay_us(us)   mp_hal_delay_us_fast(us)


// Extra built in names to add to the global namespace
#define MICROPY_PORT_BUILTINS \
    { MP_OBJ_NEW_QSTR(MP_QSTR_open), (mp_obj_t)&mp_builtin_open_obj },

extern const struct _mp_obj_module_t mp_module_machine;
extern const struct _mp_obj_module_t mp_module_litex;
extern const struct _mp_obj_module_t mp_module_utime;
extern const struct _mp_obj_module_t uos_module;
#define MICROPY_PORT_BUILTIN_MODULES \
    { MP_ROM_QSTR(MP_QSTR_umachine), MP_ROM_PTR(&mp_module_machine) }, \
    { MP_ROM_QSTR(MP_QSTR_litex),    MP_ROM_PTR(&mp_module_litex)   }, \
    { MP_ROM_QSTR(MP_QSTR_utime), MP_ROM_PTR(&mp_module_utime) }, \
    { MP_ROM_QSTR(MP_QSTR_uos), MP_ROM_PTR(&uos_module) }, \

// We need to provide a declaration/definition of alloca()
#include <alloca.h>

#define MICROPY_HW_BOARD_NAME "LiteX SoC"

#ifdef __lm32__
#define MICROPY_HW_MCU_NAME "LM32 CPU"
#elif __or1k__
#define MICROPY_HW_MCU_NAME "Mork1x CPU"
#elif __vexriscv__
#define MICROPY_HW_MCU_NAME "VexRiscv CPU"
#else
#error "Unknown MCU."
#endif

#define MP_STATE_PORT MP_STATE_VM

#ifdef CSR_TIMER0_BASE
#define MICROPY_ENABLE_SCHEDULER                (1)

#define MICROPY_PORT_ROOT_POINTERS \
    struct _machine_timer_obj_t *machine_timer_obj_head; \
    const char *readline_hist[8];
#else
#define MICROPY_PORT_ROOT_POINTERS \
    const char *readline_hist[8];
#endif //CSR_TIMER0_BASE
