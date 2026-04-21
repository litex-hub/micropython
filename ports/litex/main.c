// This file is Copyright (c) 2017-2021 Fupy/LiteX-MicroPython Developers
// This file is Copyright (c) 2021 Victor Suarez Rovere <suarezvictor@gmail.com>
// License: BSD-2-Clause

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/stream.h"
#include "lib/utils/pyexec.h"

#include "extmod/vfs.h"
#include "extmod/vfs_fat.h"

#include "irq.h"
#include "mphalport.h"
#include "modmachine.h"

extern uint8_t _start, _fstack; //linker variables

#ifdef CSR_TIMER0_UPTIME_CYCLES_ADDR
//TODO: move misc functions to other module (i.e. use LiteX SDK implementation)
//TODO: implement litex_delay_cycles in SDK
void litex_delay_cycles(uint64_t c)
{
  timer0_uptime_latch_write(1);
  uint64_t t1 = timer0_uptime_cycles_read() + c;
  //printf("us=%d, e=%ld, f=%d\n", us, (long)c, CONFIG_CLOCK_FREQUENCY);
  while((int64_t)(timer0_uptime_cycles_read() - t1) < 0)
    timer0_uptime_latch_write(1);

}
#endif

void mp_keyboard_interrupt()
{
    MP_STATE_VM(mp_kbd_exception) = (MP_STATE_PORT(mp_kbd_exception));
}

#if MICROPY_ENABLE_COMPILER
void do_str(const char *src, mp_parse_input_kind_t input_kind) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, input_kind);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        // uncaught exception
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}
#endif

static char *stack_top;
#ifdef MICROPY_HW_SDRAM_AVAIL
extern char _edata_rom, _emain_ram;
#endif

#if MICROPY_HW_SDCARD_MOUNT_AT_BOOT
//FIXME: move logic to sdcard module
void sdcard_init_vfs(fs_user_mount_t *vfs, int part);
bool sdcard_is_present(void);
STATIC bool init_sdcard_fs(void)
{
    //FIXME: not callable after soft reset...
    bool first_part = true;
    //if(first_part) return true; //already init

    for (int part_num = 1; part_num <= 4; ++part_num) {
        //printf("Trying partition %d\n", part_num);
        // create vfs object
        fs_user_mount_t *vfs_fat = m_new_obj_maybe(fs_user_mount_t);
        mp_vfs_mount_t *vfs = m_new_obj_maybe(mp_vfs_mount_t);
        if (vfs == NULL || vfs_fat == NULL) {
            //printf("Error: No vfs nor vfs_fat\n");
            break;
        }
        vfs_fat->blockdev.flags = MP_BLOCKDEV_FLAG_FREE_OBJ;
        //printf("Calling sdcard_init_vfs()\n");
        sdcard_init_vfs(vfs_fat, part_num);

        // try to mount the partition
        FRESULT res = f_mount(&vfs_fat->fatfs);

        if (res != FR_OK) {
            // couldn't mount
            //printf("f_mount returns error: %d\n", res);
            m_del_obj(fs_user_mount_t, vfs_fat);
            m_del_obj(mp_vfs_mount_t, vfs);
        } else {
            // mounted via FatFs, now mount the SD partition in the VFS
            if (first_part) {
                // the first available partition is traditionally called "sd" for simplicity
                vfs->str = "/sd";
                vfs->len = 3;
            } else {
                // subsequent partitions are numbered by their index in the partition table
                if (part_num == 2) {
                    vfs->str = "/sd2";
                } else if (part_num == 3) {
                    vfs->str = "/sd3";
                } else {
                    vfs->str = "/sd4";
                }
                vfs->len = 4;
            }
            //printf("f_mount OK, mounted as: %s\n", vfs->str);
            vfs->obj = MP_OBJ_FROM_PTR(vfs_fat);
            vfs->next = NULL;
#if 0
//TODO: needs checking
            for (mp_vfs_mount_t **m = &MP_STATE_VM(vfs_mount_table);; m = &(*m)->next) {
                if (*m == NULL) {
                    *m = vfs;
                    break;
                }
            }
#endif
            {
                if (first_part) {
                    // use SD card as current directory
                    MP_STATE_PORT(vfs_cur) = vfs;
                    //printf("partition set as current directory\n", vfs->str);
                }
            }
            first_part = false;
            break; //done after first mount
        }
    }

    if (first_part) {
        //printf("MPY: can't mount SD card\n");
        return false;
    } 
    //printf("MPY: SD card mounted!\n");
    return true;
}

STATIC void deinit_sdcard_fs(void)
{
    //mp_vfs_mount_t cur = MP_STATE_PORT(vfs_cur);
}

#else //not MICROPY_HW_SDCARD_MOUNT_AT_BOOT
#if MICROPY_HW_ENABLE_SDCARD
#warning Mounting the SD card at boot is disabled, enable it by defining macro MICROPY_HW_SDCARD_MOUNT_AT_BOOT 
#endif
#endif


void NORETURN hard_reset() { ctrl_reset_write(1); for(;;); } //TODO: move to SDK
int upython_main(int argc, char **argv);

int main(int argc, char **argv) {
    int stack_dummy;
    stack_top = (char*)&stack_dummy;
    //safe way to determine stack top: no other variables in this function //TODO: use alloca()

    irq_setmask(0);
    irq_setie(1);
    uart_init();

    while(upython_main(argc, argv) == 0)
        /*soft_reset()*/;

    irq_setie(0);
    irq_setmask(~0);

    hard_reset();
}

int upython_main(int argc, char **argv)
{
#if MICROPY_ENABLE_GC
    {
#ifdef MICROPY_HW_SDRAM_SIZE
        void *heap_start = &_edata_rom, *heap_end = &_emain_ram; //TODO: move this logic to the C SDK

        #ifdef CSR_VIDEO_FRAMEBUFFER_BASE
        #warning A ram region for the video framebuffer should be allocated in linker scripts
        void *video_base = (void *) video_framebuffer_dma_base_read(); //TODO: move framebuffer logic to the C SDK
        if(heap_start <= video_base && heap_end > video_base)
            heap_end = video_base; //check if framebuffer overlaps
        #endif

        #ifdef _DEBUG        
        printf("RAM base at 0x%p, heap at 0x%p, end=0x%p (%d KiB)\n", (void *)MICROPY_HW_SDRAM_BASE, heap_start, heap_end, ((char*)heap_end-(char*)heap_start)/1024);
        #endif
        gc_init(heap_start, heap_end);
#else
        static uint8_t heap[4096];
        gc_init(heap, heap + sizeof(heap));
#endif
    }
#endif

    mp_init();
    //printf("C stack at %p, original %p\n", stack_top, &_fstack);

    #if MICROPY_HW_SDCARD_MOUNT_AT_BOOT
    // if an SD card is present then mount it on /sd/
    if (sdcard_is_present()) {
        bool mounted_sdcard = init_sdcard_fs();
        /*
        printf("Mounting existing SD card at boot...\n");
        if(mounted_sdcard)
          printf("SD card mounted\n");
        else
          printf("SD card NOT mounted\n");
        */

        if(mounted_sdcard)
        {
            int resp = pyexec_file_if_exists("boot.py");
            if (resp & PYEXEC_FORCED_EXIT)
                return -1; //hard reset

            pyexec_file_if_exists("main.py");
        }
    }
    #endif


#if MICROPY_ENABLE_COMPILER
    #if !MICROPY_REPL_EVENT_DRIVEN
    for (;;)
    {
        int resp;
        if(pyexec_mode_kind == PYEXEC_MODE_RAW_REPL)
            resp = pyexec_raw_repl();
        else
            resp = pyexec_friendly_repl();

        switch(resp)
        {
            case 0: continue;
            case PYEXEC_FORCED_EXIT: //CTRL-D, do soft reset
                break;
            default:
                return -1; //do hard reset
        }
        break;
    }
    #else
    #error MICROPY_REPL_EVENT_DRIVEN is not tested!
    pyexec_event_repl_init();
    for (;;)
    {
        int c = mp_hal_stdin_rx_chr();
        if (pyexec_event_repl_process_char(c)) {
            break;
        }
    }
    #endif
#else
#error not enabling MICROPY_ENABLE_COMPILER is not tested!
#endif


#if MICROPY_HW_SDCARD_MOUNT_AT_BOOT
    deinit_sdcard_fs();
#endif

#ifdef CSR_TIMER0_UPTIME_CYCLES_ADDR
    machine_timer_deinit_all();
#endif

    gc_sweep_all();
    mp_hal_stdout_tx_strn("MPY: soft reboot\r\n", 18);
    mp_hal_delay_us(10000); // allow UART to flush output
    mp_deinit();
    #if MICROPY_REPL_EVENT_DRIVEN
    pyexec_event_repl_init();
    #endif

    return 0;
}

#if MICROPY_ENABLE_GC
void gc_collect(void) {
    // WARNING: This gc_collect implementation doesn't try to get root
    // pointers from CPU registers, and thus may function incorrectly.
    void *dummy;
    gc_collect_start();
    gc_collect_root(&dummy, ((mp_uint_t)stack_top - (mp_uint_t)&dummy) / sizeof(mp_uint_t));
    gc_collect_end();
    //gc_dump_info();
}
#endif

#if !MICROPY_READER_VFS
mp_lexer_t *mp_lexer_new_from_file(const char *filename) {
    //defined in case of not using the implementation in extmod/vfs_reader.c
    mp_raise_OSError(MP_ENOENT);
}
#endif

#if !MICROPY_VFS
mp_import_stat_t mp_import_stat(const char *path) {
    return MP_IMPORT_STAT_NO_EXIST;
}

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);
#endif

void nlr_jump_fail(void *val) {
    while (1) {
        ;
    }
}

void NORETURN __fatal_error(const char *msg) {
    while (1) {
        ;
    }
}

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    uintptr_t ret = 0;
    __fatal_error("mp_hal_stdio_poll not implemented");
//    if ((poll_flags & MP_STREAM_POLL_RD) && stdin_ringbuf.iget != stdin_ringbuf.iput)
//        ret |= MP_STREAM_POLL_RD;
    return ret;
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    printf("Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    __fatal_error("Assertion failed");
}
#endif

#ifndef CSR_TIMER0_UPTIME_CYCLES_ADDR
#warning DELAY FUNCTION IS NOT ACCURATE, use uptime method (--timer-uptime option at SoC generation)
#endif

