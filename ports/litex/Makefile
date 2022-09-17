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



# Import LiteX's variables.mak
include $(BUILD_DIRECTORY)/software/include/generated/variables.mak
LIBLITESDCARD_DIRECTORY = liblitesdcard
CPUFLAGS=-march=rv32im -mabi=ilp32 -D__vexriscv__

# Include the core environment definitions; this will set $(TOP).
include ../../py/mkenv.mk

#set misc variables (used by py.mk)
MICROPY_VFS_FAT = $(shell egrep "(CSR_SDCORE_BASE|CSR_SPISDCARD_BASE)" ${BUILD_DIRECTORY}/software/include/generated/csr.h | wc -l)
#$(info $$MICROPY_VFS_FAT is: ${MICROPY_VFS_FAT})

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
LDFLAGS += -nostartfiles -T linker.ld -Wl,-Map=$@.map -Wl,--cref
LDFLAGS += -L$(BUILDINC_DIRECTORY)
LDFLAGS += $(filter -march=% -mabi=%, $(CPUFLAGS)) # CPU specific.

# Set Libraries.
LIBS = -Wl,-lc -Wl,-lm

# Set Sources.
SRC_C = \
	main.c \
	isr.c \
	modmachine.c \
	moduos.c \
	modutime.c \
	modlitex.c \
	litex_led.c \
	litex_dma.c \
	litex_video.c \
	machine_hw_spi.c \
	machine_timer.c \
	machine_pwm.c \
	machine_sdcard.c \
	machine_pin.c \
	lib/utils/sys_stdio_mphal.c \
	lib/utils/stdout_helpers.c \
	lib/utils/interrupt_char.c \
	lib/utils/pyexec.c \
	lib/timeutils/timeutils.c \
	lib/mp-readline/readline.c

#lib sources with no need of QSTR extraction
SRC_LITEX = \
	$(LIBBASE_DIRECTORY)/system.c \
	$(LIBBASE_DIRECTORY)/uart.c \
	$(LIBLITESDCARD_DIRECTORY)/spisdcard.c \
	$(LIBLITESDCARD_DIRECTORY)/sdcard.c \

SRC_C += $(SRC_MOD)

DRIVERS_SRC_C = $(addprefix drivers/,\
	bus/softspi.c \
)

EXTMOD_SRC_C = $(addprefix extmod/,\
	machine_spi.c \
	machine_i2c.c \
)

CRT0_S ?= $(CPU_DIRECTORY)/crt0.S
CRT0_O = $(BUILD)/software/libbase/crt0.o

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
	$(Q)$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(Q)$(SIZE) $@

$(BUILD)/firmware.bin: $(BUILD)/firmware.elf
	$(Q)$(OBJCOPY) -O binary $^ $(BUILD)/firmware.bin

include ../../py/mkrules.mk
