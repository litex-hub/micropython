# This file is Copyright (c) 2017-2021 Fupy/LiteX-MicroPython Developers
# This file is Copyright (c) 2021-2022 Victor Suarez Rovere <suarezvictor@gmail.com>
# License: BSD-2-Clause

# Check LiteX's BUILD_DIRECTORY definition.

BUILD_DIRECTORY ?= .
ifeq ($(wildcard $(BUILD_DIRECTORY)/software/include/generated/variables.mak),)
define ERROR_MSG

The LiteX port of MicroPython relies on software files generated
during the LiteX build. Please specify your build directory, ex:
$ export BUILD_DIRECTORY=build/digilent_arty

endef
$(error $(ERROR_MSG))
endif

LIBLITESDCARD_DIRECTORY = liblitesdcard

# Import LiteX's variables.mak
include $(BUILD_DIRECTORY)/software/include/generated/variables.mak

# Include the core environment definitions; this will set $(TOP).
include ../../py/mkenv.mk

#set misc variables (used by py.mk)
MICROPY_VFS_FAT = $(shell egrep "(CSR_SDCORE_BASE|CSR_SPISDCARD_BASE)" ${BUILD_DIRECTORY}/software/include/generated/csr.h | wc -l)
#$(info $$MICROPY_VFS_FAT is: ${MICROPY_VFS_FAT})

# Detect LiteEth presence in the SoC and pull in lwIP only when it's there.
# extmod/extmod.mk keys off MICROPY_PY_LWIP=1 to compile + link the lwIP
# core; we additionally route the port-specific lwipopts.h through
# lwip_inc/. Whichever value we set here also gets reflected as a CFLAG
# (-DMICROPY_PY_LWIP=...) so mpconfigport.h can gate MICROPY_PY_NETWORK
# / MICROPY_PY_SOCKET / etc. on it.
MICROPY_PY_LWIP := $(shell grep -q "CSR_ETHMAC_BASE" ${BUILD_DIRECTORY}/software/include/generated/csr.h 2>/dev/null && echo 1 || echo 0)
ifeq ($(MICROPY_PY_LWIP),1)
INC += -I$(TOP)/ports/litex/lwip_inc
endif

#TODO:it's unclear why py.mk (that has this logic) doesn't get called
################################################################################
# VFS FAT FS

OOFATFS_DIR = lib/oofatfs

# this sets the config file for FatFs
CFLAGS_MOD += -DFFCONF_H=\"$(OOFATFS_DIR)/ffconf.h\"

ifeq ($(MICROPY_VFS_FAT),1)
CFLAGS_MOD += -DMICROPY_VFS_FAT=1
SRC_MOD += $(addprefix $(OOFATFS_DIR)/,\
	ff.c \
	ffunicode.c \
	)
endif
CFLAGS += $(CFLAGS_MOD)

# Include py core make definitions
include $(TOP)/py/py.mk
# Pull in the extmod sources (extmod/modmachine.c, extmod/modtime.c,
# extmod/modos.c, the shared/ machine_* drivers, ...). Without this, the
# QSTR / module-registration scan never sees MP_REGISTER_EXTENSIBLE_MODULE
# for machine/time/os and 'import machine' fails at runtime with
# 'ImportError: module not found' even though MICROPY_PY_MACHINE=1.
include $(TOP)/extmod/extmod.mk

# Set Cross-Compiler.

CROSS_COMPILE ?= $(TRIPLE)-

# Set ASFLAGS / CFLAGS.

ASFLAGS += -I$(BUILD_DIRECTORY)/software/include # For crt0.S.

CFLAGS += $(CPUFLAGS)
#CFLAGS += $(INC) -Wall -Werror -std=gnu11 -ggdb $(COPT)
CFLAGS += $(INC) -Wall -Wno-error -std=gnu11 -ggdb $(COPT)
CFLAGS += -Og -Wdouble-promotion

# Set Include directories.
INC += -I.
INC += -I$(BUILD)
INC += -I$(TOP)
INC += -I$(BUILD_DIRECTORY)/software/include
INC += -I$(SOC_DIRECTORY)/software/include
INC += -I$(CPU_DIRECTORY)

# Set LDFLAGS.
#
# Follow the LiteX BIOS link recipe: link against the LiteX-built libraries
# (picolibc, libcompiler_rt, libbase, and optional cores like liblitesdcard)
# rather than the host toolchain's libc. Required because:
#  - Host riscv64-unknown-elf toolchains often ship a 64-bit libc that cannot
#    link against -march=rv32i objects ("ELFCLASS64 incompatible with
#    ELFCLASS32").
#  - LiteX integrates picolibc with CPU-appropriate multilib already.
# $(LIBS) comes from $(BUILD_DIRECTORY)/software/include/generated/variables.mak
# and is the space-separated list of LiteX libraries for this SoC
# (e.g. "libc libcompiler_rt libbase libfatfs liblitespi ...").
LITEX_LIB_DIRS = $(foreach lib,$(LIBS),-L$(BUILD_DIRECTORY)/software/$(lib))
LITEX_LIB_LDFLAGS = $(LIBS:lib%=-l%)

LDFLAGS += -nostdlib -nodefaultlibs
LDFLAGS += -Wl,--no-dynamic-linker -Wl,--build-id=none
LDFLAGS += -T linker.ld -Wl,-Map=$@.map -Wl,--cref
LDFLAGS += -L$(BUILDINC_DIRECTORY)
LDFLAGS += $(filter -march=% -mabi=%, $(CPUFLAGS)) # CPU specific.

# Set Sources.
#
# modmachine.c is NOT compiled standalone — it's #included from
# extmod/modmachine.c via MICROPY_PY_MACHINE_INCLUDEFILE (see mpconfigport.h).
# The stock time module (extmod/modtime.c) also replaces the port's old
# modutime.c; any port-specific mp_hal_ticks_*/mp_hal_delay_* hooks live in
# mphalport.h.
SRC_C = \
	main.c \
	isr.c \
	mphalport.c \
	modlitex.c \
	litex_led.c \
	litex_dma.c \
	litex_video.c \
	machine_hw_spi.c \
	machine_timer.c \
	machine_pwm.c \
	machine_sdcard.c \
	machine_pin.c \
	machine_uart.c \
	machine_adc.c \
	liteeth_netif.c \
	network_lan.c \
	shared/runtime/sys_stdio_mphal.c \
	shared/runtime/stdout_helpers.c \
	shared/runtime/interrupt_char.c \
	shared/runtime/pyexec.c \
	shared/timeutils/timeutils.c \
	shared/readline/readline.c

#lib sources with no need of QSTR extraction
SRC_LITEX = \
	$(LIBBASE_DIRECTORY)/system.c \
	$(LIBBASE_DIRECTORY)/uart.c \

# LiteX's libcompiler_rt build ships a curated subset of LLVM compiler-rt
# builtins (see litex/soc/software/libcompiler_rt/Makefile). MicroPython's
# float ABI (MICROPY_FLOAT_IMPL_DOUBLE) pulls in double<->float conversions
# that aren't in that subset. Compile just those two translation units from
# the same compiler-rt source tree that LiteX uses, so we stay consistent
# without pulling in host-toolchain libgcc (which is usually the wrong ABI
# on riscv64-unknown-elf multilib setups).
SRC_LITEX += \
	$(COMPILER_RT_DIRECTORY)/lib/builtins/truncdfsf2.c \
	$(COMPILER_RT_DIRECTORY)/lib/builtins/extendsfdf2.c \
	$(COMPILER_RT_DIRECTORY)/lib/builtins/floatundidf.c \

# SDCard sources are only needed when the SoC exposes an SDCore or SPISDCard
# peripheral (i.e. MICROPY_VFS_FAT=1). Sim targets without --with-sdcard would
# otherwise fail to find <libfatfs/ff.h> since libfatfs is only pulled in when
# MICROPY_VFS_FAT is enabled.
ifeq ($(MICROPY_VFS_FAT),1)
SRC_LITEX += \
	$(LIBLITESDCARD_DIRECTORY)/spisdcard.c \
	$(LIBLITESDCARD_DIRECTORY)/sdcard.c \

endif

SRC_C += $(SRC_MOD)

DRIVERS_SRC_C = $(addprefix drivers/,\
	bus/softspi.c \
)

# extmod/machine_spi.c and extmod/machine_i2c.c are already built via py.mk
# (see EXTMOD_SRC_C there), so we don't list them again here.
EXTMOD_SRC_C =

CRT0_S ?= $(CPU_DIRECTORY)/crt0.S
CRT0_O = $(BUILD)/software/libbase/crt0.o

# Auto-generated CSR name -> address table consumed by modlitex.c. Built
# from the SoC's csr.json at the same location as the rest of the generated
# software headers.
LITEX_CSR_TABLE_H = $(BUILD)/genhdr/litex_csr_table.h
$(LITEX_CSR_TABLE_H): $(BUILD_DIRECTORY)/csr.json tools/gen_csr_table.py
	$(ECHO) "GEN $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(PYTHON) tools/gen_csr_table.py $< $@
# Both modlitex.o and the QSTR extraction preprocess depend on the CSR
# table header: modlitex.c #include's it at file scope, and the QSTR pass
# preprocesses every source file, so the header must exist before that
# runs.
$(BUILD)/modlitex.o: $(LITEX_CSR_TABLE_H)
$(BUILD)/genhdr/qstr.i.last: $(LITEX_CSR_TABLE_H)

OBJ =  $(CRT0_O) $(addprefix $(BUILD)/, $(SRC_S:.S=.o)) $(PY_O) $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(DRIVERS_SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(EXTMOD_SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_LITEX:.c=.o))

all: $(BUILD)/firmware.bin

# List of sources for qstr extraction
SRC_QSTR += $(SRC_C) $(SRC_LIB)

# CRT0 override for normal assembler rule.
$(CRT0_O): $(CRT0_S)
	$(CC) -c $(ASFLAGS) $(CFLAGS) $(CRT0FLAGS) -o $@ $<

$(BUILD)/firmware.elf: $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(CC) $(LDFLAGS) -o $@ $(OBJ) \
		$(LITEX_LIB_DIRS) \
		-Wl,--start-group $(LITEX_LIB_LDFLAGS) -Wl,--end-group \
		-Wl,--gc-sections
	$(Q)$(SIZE) $@

$(BUILD)/firmware.bin: $(BUILD)/firmware.elf
	$(Q)$(OBJCOPY) -O binary $^ $(BUILD)/firmware.bin

# Run MicroPython tests under litex_sim. By default runs the hello-world
# smoke test; override TESTS="..." for a different set, e.g.
#   make test TESTS="test/test_machine.py test/test_time.py"
# Requires Verilator + socat + the `litex` Python package on $PATH.
TESTS ?= test/test_hello_world.py
test: $(BUILD)/firmware.bin
	$(Q)tools/run_sim.py --firmware $(BUILD)/firmware.bin --output-dir $(BUILD_DIRECTORY) $(TESTS)

.PHONY: test

include ../../py/mkrules.mk
