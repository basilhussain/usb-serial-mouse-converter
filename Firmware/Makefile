################################################################################
# BUILD OPTIONS
################################################################################

# What level of verbosity to use for output of debug messages. Valid values are
# 'OFF', 'ERROR', 'WARN', 'INFO', 'TRACE'.
DEBUG_VERBOSITY ?= INFO

# Where debug messages should be output to. Valid values are 'SDI', 'UART'.
DEBUG_OUT ?= UART

# Whether to enable the PFIC hardware prologue/epilogue feature (HPE). Should be
# enabled when using WCH fork of GCC and 'interrupt("WCH-Interrupt-fast")' ISR
# attributes.
PFIC_HPE ?= disabled

# Whether to include a call to a function named 'SystemInit' before 'main'. Use
# this when using the WCH 'EVT' library.
CALL_SYSINIT ?= disabled

# Whether to run by default in RISC-V lower-privilege User mode, or highest-
# privilege Machine mode. User mode restricts access to CSRs and memory
# according to PMP.
PRIV_MACHINE ?= disabled

# Prefix of the toolchain executables.
TOOL_PREFIX ?= riscv-none-elf

# Characteristics of the device - which package, clock speed, etc.
DEVICE ?= CH32X035G8R6
# DEVICE ?= CH32X035C8T6
SYSCLK ?= 48000000
HCLK ?= $(SYSCLK)

################################################################################

CC = $(TOOL_PREFIX)-gcc
CFLAGS = -march=rv32imc_zicsr_zifencei -misa-spec=2.2 -mabi=ilp32 -msave-restore -fdata-sections -ffunction-sections -funsigned-char -std=c23 -Os -g -Wall -Wstack-usage=2048 -specs=nano.specs -MMD -MP
ifeq ($(PFIC_HPE),enabled)
	CFLAGS += -DSTARTUP_ENABLE_HPE
endif
ifeq ($(PRIV_MACHINE),enabled)
	CFLAGS += -DSTARTUP_PRIV_MACHINE_MODE -DINTERRUPT_PRIV_MODE=INTERRUPT_PRIV_MODE_MACHINE
endif
ifeq ($(CALL_SYSINIT),enabled)
	CFLAGS += -DSTARTUP_CALL_SYSINIT
endif
CFLAGS += -D$(DEVICE) -DSYSCLK=$(SYSCLK) -DHCLK=$(HCLK) -DPRINTF_$(DEBUG_OUT) -DDEBUG_VERBOSITY=DEBUG_VERBOSITY_$(DEBUG_VERBOSITY)

LD = $(TOOL_PREFIX)-gcc
LDFLAGS = -march=rv32imc -mabi=ilp32 -nostartfiles -Wl,--gc-sections -specs=nano.specs

OD = $(TOOL_PREFIX)-objdump
OC = $(TOOL_PREFIX)-objcopy
SZ = $(TOOL_PREFIX)-size
DB = $(TOOL_PREFIX)-gdb

ifeq ($(OS),Windows_NT)
	# Can't run the xPack mkdir executable directly because "mkdir" will just
	# run Windows shell command. Use indirectly via central busybox executable.
	RM = busybox rm -fr
	MKDIR = busybox mkdir -p
else
	RM = rm -fr
	MKDIR = mkdir -p
endif

SRCS = \
	libs/nanoprintf/printf.c \
	ch32x035_startup.S \
	sdi.c \
	debug.c \
	tstamp.c \
	event.c \
	usb.c \
	hid.c \
	uart.c \
	uart_mouse.c \
	uart_debug.c \
	main.c

OBJDIR = obj
OBJS = $(patsubst %.S,%.o,$(patsubst %.c,%.o,$(addprefix $(OBJDIR)/,$(SRCS))))

DEPS = $(patsubst %.o,%.d,$(OBJS))

LDSCRIPT = ch32x035.ld

BINDIR = bin
BINARY = $(BINDIR)/usb2serial.elf

.PHONY: all clean debug flash

all: $(BINARY)

$(BINARY): $(OBJS) $(LDSCRIPT) | $(BINDIR)
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) -Wl,-Map,"$(addsuffix .map,$(basename $@))" -o $@ $(OBJS)
	$(OD) --section-headers --disassemble-all --source --visualize-jumps $@ > $(addsuffix .lst,$(basename $@))
	$(OC) -O ihex $@ $(addsuffix .hex,$(basename $@))
	$(SZ) -d $@

$(OBJS): | $(OBJDIR)

$(OBJDIR)/%.o: %.c
	$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.S
	$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR) $(BINDIR):
	$(MKDIR) $@

clean:
	$(RM) $(OBJDIR)
	$(RM) $(BINDIR)

debug: $(BINARY)
	debug.bat $<
#	start /min openocd -f interface/wlink.cfg -f target/wch-riscv.cfg
#	$(DB) -ex "target extended-remote localhost:3333" $<

flash: $(BINARY)
	openocd -f interface/wlink.cfg -f target/wch-riscv.cfg -c "program $(<) verify reset exit"

-include $(DEPS)
