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
// Don't use long double for float formatting: the RISC-V SoftABI emits tf*
// helpers (__trunctfdf2, __addtf3, ...) that are neither in picolibc nor in
// LiteX's libcompiler_rt, and linking host-toolchain libgcc is non-trivial
// because '-march=rv32i2p0_m' does not resolve to a multilib on gcc <= 11.
#define MICROPY_FLOAT_FORMAT_IMPL   (MICROPY_FLOAT_FORMAT_IMPL_APPROX)
// Don't use _Float16 either: it pulls in __extendhfdf2/__truncsfhf2 which
// are also absent from picolibc/libcompiler_rt. Fall back to MicroPython's
// bit-manipulation half-float encoder/decoder.
#define MICROPY_FLOAT_USE_NATIVE_FLT16 (0)

// Python builtins, classes, modules, etc. features.
#define MICROPY_PY_MATH             (1)
#define MICROPY_PY_CMATH            (1)
#define MICROPY_PY_SYS              (1)
#define MICROPY_PY_MACHINE          (1)
#define MICROPY_KBD_EXCEPTION       (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW (1)

// extended modules
#define MICROPY_PY_MACHINE_INCLUDEFILE      "ports/litex/modmachine.c"
#define MICROPY_PY_MACHINE_RESET            (1)
// MICROPY_PY_MACHINE_SPI / _I2C would expose a hardware machine.SPI /
// machine.I2C from extmod, expecting the port to provide
// machine_spi_type / machine_i2c_type. The LiteX port's hardware SPI is
// exposed under that name via MICROPY_PY_MACHINE_EXTRA_GLOBALS in
// modmachine.c, and there is no hardware I2C — only SoftI2C. So leave
// the extmod-side flags off; SOFTSPI / SOFTI2C give us the bit-banged
// classes machine.SoftSPI / machine.SoftI2C anyway.
#define MICROPY_PY_MACHINE_SPI_MSB          (1)
#define MICROPY_PY_MACHINE_SPI_LSB          (0)
#define MICROPY_PY_MACHINE_SOFTSPI          (1)
#define MICROPY_PY_MACHINE_SOFTI2C          (1)
#define MICROPY_PY_TIME                     (1)
// _GMTIME_LOCALTIME_MKTIME and _TIME_TIME_NS need an RTC accessor
// (mp_time_localtime_get / mp_time_time_get). LiteX SoCs don't ship a
// generic RTC peripheral, so leave them off — time.ticks_ms / .sleep
// still work via mp_hal_ticks_ms / mp_hal_delay_ms in mphalport.c.
#define MICROPY_PY_OS                       (1)
#define MICROPY_PY_OS_UNAME                 (1)

// Networking — pulled in when the SoC has a LiteEth core (the Makefile
// detects CSR_ETHMAC_BASE in csr.h and sets MICROPY_PY_LWIP=1, which
// then gates everything else here).
#ifndef MICROPY_PY_LWIP
#define MICROPY_PY_LWIP                     (0)
#endif
#if MICROPY_PY_LWIP
#define MICROPY_PY_NETWORK                  (1)
#define MICROPY_PY_SOCKET                   (1)
#define MICROPY_PY_LWIP_SOCK_RAW            (1)
// Hostname used by lwIP's DHCP DISCOVER. Boards can override.
#ifndef MICROPY_PY_NETWORK_HOSTNAME_DEFAULT
#define MICROPY_PY_NETWORK_HOSTNAME_DEFAULT "litex"
#endif
// Drive RX polling + sys_check_timeouts from the VM loop. Cheap when
// the netif is inactive (one bool check). Until ETHMAC_INTERRUPT
// dispatch is wired this is the only thing pumping lwIP.
extern void litex_lwip_poll(void);
#define MICROPY_VM_HOOK_LOOP litex_lwip_poll();
#define MICROPY_PORT_NETWORK_INTERFACES \
    { MP_ROM_QSTR(MP_QSTR_LAN), MP_ROM_PTR(&network_lan_type) },
extern const struct _mp_obj_type_t network_lan_type;
#endif

// Type definitions for the specific machine

typedef intptr_t mp_int_t;
typedef uintptr_t mp_uint_t;
typedef long mp_off_t;


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

#if defined(CSR_SPI_BASE) || defined(CSR_SPI0_BASE)
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
// #define MICROPY_HW_SDCARD_MOUNT_AT_BOOT (MICROPY_HW_ENABLE_SDCARD) //not enabled by default
#endif

#define MICROPY_FATFS_MULTI_PARTITION (1)
#else // not MICROPY_VFS_FAT
#undef MICROPY_VFS_FAT  // if there's no SD core, disable VFS fat
#define MICROPY_VFS_FAT (0)
#endif // MICROPY_VFS_FAT

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

#define MICROPY_PY_SYS_PLATFORM "LiteX (" CONFIG_BUS_STANDARD " bus)" // TODO: use board name from SoC generation

#define TIMER0_POLLING // interrupt handing not enabled yet
#ifdef CSR_TIMER0_UPTIME_LATCH_ADDR
// TODO: use SDK
static inline uint64_t litex_uptime() {
    timer0_uptime_latch_write(1);
    return timer0_uptime_cycles_read();
}
#else
// calibrated for sleep_ms / sleep
static inline uint64_t litex_uptime() {
    static uint64_t uptime = 0;
    return uptime += 250;
}
#endif

#ifdef CSR_TIMER0_UPTIME_CYCLES_ADDR
void litex_delay_cycles(uint64_t c); // TODO: maybe a faster implementation can be limited to 32 bits
static inline void mp_hal_delay_us_fast(mp_uint_t us) {
    uint64_t c = us;
    c *= CONFIG_CLOCK_FREQUENCY;
    c /= 1000000;
    litex_delay_cycles(c);
}
#else
static inline void mp_hal_delay_us_fast(mp_uint_t us) {
    us *= 4;
    volatile static uint8_t t;
    while (us--) {
        ++t;
    }
}
#endif
#define mp_hal_delay_us(us)   mp_hal_delay_us_fast(us)


// The MICROPY_PORT_BUILTINS / MICROPY_PORT_BUILTIN_MODULES macros were
// replaced in 1.20 by MP_REGISTER_MODULE()s at the bottom of each module's
// .c file. The 'machine' and 'time' modules now come from extmod/modmachine.c
// and extmod/modtime.c respectively, keyed off the MICROPY_PY_MACHINE /
// MICROPY_PY_TIME flags above. The port-specific 'litex' module registers
// itself from modlitex.c.

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
#endif

// Root pointers for GC tracing are declared via MP_REGISTER_ROOT_POINTER()
// at the bottom of the file that owns the pointer (see machine_timer.c).
// The readline history also moved to a shared/readline/readline-provided
// root pointer, so the port no longer declares it here.
