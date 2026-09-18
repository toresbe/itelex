# Portable i-Telex AVR firmware build.
# GNU make prefers this file over the retained legacy `makefile`.

SHELL := /bin/sh

AVR_TOOLCHAIN_ROOT ?=
AVR_PREFIX := $(if $(AVR_TOOLCHAIN_ROOT),$(patsubst %/,%,$(AVR_TOOLCHAIN_ROOT))/bin/,)avr-
CC      := $(AVR_PREFIX)gcc
OBJCOPY := $(AVR_PREFIX)objcopy
OBJDUMP := $(AVR_PREFIX)objdump
SIZE    := $(AVR_PREFIX)size

BUILD_ROOT ?= build
TESTS_DIR ?= tests
ITELEX_MISC_DIR ?= dependencies/itelex-misc
COMMON_DIR ?= $(ITELEX_MISC_DIR)/Gemeinsam
AVR_CLIBS_DIR ?= dependencies/avr-clibs
ITELEX_TRACE_LEVEL ?= 255
AVR_CLIBS_URL := https://svn.code.sf.net/p/fredslibraries/avr-clibs/
AVR_CLIBS_REV := 13
VERSION ?= $(shell git describe --always --dirty --tags 2>/dev/null || echo unknown)

LOCAL_SOURCES := \
	apps/apps_init.c apps/cron/cron.c \
	apps/modules/cmd_arp.c apps/modules/cmd_cron.c apps/modules/cmd_dns.c \
	apps/modules/cmd_dyndns.c apps/modules/cmd_eemem.c apps/modules/cmd_foo.c \
	apps/modules/cmd_ifconfig.c apps/modules/cmd_ntp.c apps/modules/cmd_reset.c \
	apps/modules/cmd_stats.c apps/modules/cmd_twi.c apps/modul_init.c apps/telnet/telnet.c \
	apps/httpd/files.c apps/httpd/httpd2_pharse.c apps/httpd/httpd2.c \
	apps/httpd/cgibin/cgi-bin.c hardware/ext_int/ext_int.c hardware/timer1/timer1.c \
	hardware/uart/mega/uart_0.c hardware/uart/mega/uart_1.c hardware/uart/uart_core.c \
	hardware/network/enc28j60.c hardware/spi/spi_0.c hardware/spi/spi_1.c \
	hardware/spi/spi_2.c hardware/timer0/timer0.c hardware/led/led_core.c \
	hardware/memory/xram.c hardware/spi/spi_core.c hardware/gpio/gpio_core.c \
	hardware/gpio/gpio_out.c hardware/gpio/gpio_in.c hardware/sd_raw/sd_raw.c \
	iTelex/Centralex.c iTelex/CgiFormTools.c iTelex/ConfigNtp.c iTelex/eMail.c iTelex/IspMaster.c \
	iTelex/MissbrauchSperre.c iTelex/RamCorrTest.c iTelex/StringTab.c iTelex/SwTwi.c \
	iTelex/Parsing.c iTelex/Protokoll.c iTelex/TlnBuch.c iTelex/TlnServer.c iTelex/iTelex.c \
	system/base64/base64.c system/buffer/fifo.c system/config/eeconfig.c \
	system/filesystem/byteordering.c system/filesystem/fat.c system/filesystem/filesystem.c \
	system/filesystem/partition.c system/math/checksum.c system/math/crc8.c system/math/math.c \
	system/net/arp.c system/net/dhcpc.c system/net/dns.c system/net/endian.c \
	system/net/ethernet.c system/net/icmp.c system/net/ip.c system/net/network.c \
	system/net/ntp.c system/net/udp.c system/net/tcp.c system/net/dyndns.c \
	system/shell/shell.c system/stdout/stdout.c system/string/string.c system/thread/thread.c \
	system/clock/clock.c system/init.c system/softreset/softreset.c main.c

COMMON_SOURCES := $(COMMON_DIR)/BaudotCode.c $(COMMON_DIR)/BusKomm.c $(COMMON_DIR)/FifoPuffer.c
SOURCES := $(LOCAL_SOURCES) $(COMMON_SOURCES)
VPATH := $(sort $(dir $(SOURCES)))

COMMON_CFLAGS := -Os -gdwarf-2 -std=gnu99 -fgnu89-inline -Wall \
	-funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums \
	-ffunction-sections -fdata-sections -mrelax \
	-DF_CPU=16000000UL -DITELEX_TRACE_LEVEL=$(ITELEX_TRACE_LEVEL) \
	-I. -I$(COMMON_DIR) -I$(AVR_CLIBS_DIR)

.PHONY: all standard light fastboot bootstrap check-dependencies clean \
	clean-standard clean-light clean-fastboot clean-test clean-test-avr \
	test test-avr help

all: standard light fastboot

help:
	@echo "Targets: bootstrap, standard, light, fastboot, all, test, test-avr, clean"
	@echo "Override AVR_TOOLCHAIN_ROOT to use a non-PATH AVR GCC toolchain."
	@echo "Override ITELEX_TRACE_LEVEL (default 255) to compile out higher trace levels."

# Host unit tests for the firmware's pure logic. Built by the host compiler,
# not by avr-gcc, so this target needs no AVR toolchain. See tests/README.md.
test:
	$(MAKE) -C $(TESTS_DIR) test

# Integration tests for register-level timing and the software serial path.
# These need avr-gcc, simavr, libsimavr headers and libelf.
test-avr: check-dependencies
	$(MAKE) -C $(TESTS_DIR)/avr test

bootstrap:
	@git submodule update --init --recursive
	@if [ ! -f "$(AVR_CLIBS_DIR)/TwiEvents.h" ]; then \
		command -v svn >/dev/null || { echo "error: svn is required by bootstrap" >&2; exit 1; }; \
		svn export --force -q -r $(AVR_CLIBS_REV) "$(AVR_CLIBS_URL)" "$(AVR_CLIBS_DIR)"; \
	fi

check-dependencies:
	@test -f "$(COMMON_DIR)/BusKomm.c" || { echo "error: missing itelex-misc submodule; run 'make bootstrap'" >&2; exit 1; }
	@test -f "$(AVR_CLIBS_DIR)/TwiEvents.h" || { echo "error: missing AVR-Clibs r$(AVR_CLIBS_REV); run 'make bootstrap'" >&2; exit 1; }

define firmware
$(1)_MCU := $(2)
$(1)_NAME := $(3)
$(1)_DIR := $(BUILD_ROOT)/$(1)
$(1)_GEN := $$($(1)_DIR)/generated
$(1)_OBJECTS := $$(addprefix $$($(1)_DIR)/obj/,$$(notdir $$(SOURCES:.c=.o)))
$(1)_CFLAGS := $$(COMMON_CFLAGS) -mmcu=$$($(1)_MCU) -iquote $$($(1)_GEN) $(4)
$(1)_LDFLAGS := -mmcu=$$($(1)_MCU) -Wl,--gc-sections -Wl,--noinhibit-exec -mrelax -Wl,-Map=$$($(1)_DIR)/$$($(1)_NAME).map $(5)

$(1): check-dependencies $$($(1)_DIR)/$$($(1)_NAME).hex $$($(1)_DIR)/$$($(1)_NAME).eep $$($(1)_DIR)/$$($(1)_NAME).lss
	@$$(SIZE) --format=avr --mcu=$$($(1)_MCU) $$($(1)_DIR)/$$($(1)_NAME).elf

$$($(1)_DIR)/obj/%.o: %.c $$($(1)_GEN)/config.h $$($(1)_GEN)/SvnVersion.h
	@mkdir -p $$(@D)
	$$(CC) $$($(1)_CFLAGS) -MMD -MP -MF $$(@:.o=.d) -c $$< -o $$@

$$($(1)_GEN)/config.h: iTelex.config.h
	@mkdir -p $$(@D)
	cp $$< $$@

$$($(1)_GEN)/SvnVersion.h: scripts/gen-version-header.sh
	@mkdir -p $$(@D)
	sh $$< "$(VERSION)" $$@

$$($(1)_DIR)/$$($(1)_NAME).elf: $$($(1)_OBJECTS)
	$$(CC) $$($(1)_LDFLAGS) $$^ -o $$@

$$($(1)_DIR)/$$($(1)_NAME).hex: $$($(1)_DIR)/$$($(1)_NAME).elf
	$$(OBJCOPY) -O ihex -R .eeprom -R .fuse -R .lock -R .signature -R .user_signatures $$< $$@

$$($(1)_DIR)/$$($(1)_NAME).eep: $$($(1)_DIR)/$$($(1)_NAME).elf
	$$(OBJCOPY) -j .eeprom --set-section-flags=.eeprom=alloc,load \
		--change-section-lma .eeprom=0 --no-change-warnings -O ihex $$< $$@ || true

$$($(1)_DIR)/$$($(1)_NAME).lss: $$($(1)_DIR)/$$($(1)_NAME).elf
	$$(OBJDUMP) -h -S $$< > $$@

clean-$(1):
	$$(RM) -r $$($(1)_DIR)

-include $$($(1)_OBJECTS:.o=.d)
endef

comma := ,
$(eval $(call firmware,standard,atmega2561,Main,-DPROG_ID_ZUSATZ='"ITA2"' -DiTelex -D__heap_end=0x80ffff,-Wl$(comma)--defsym=__stack=0x2000 -Wl$(comma)--section-start$(comma).data=0x802200$(comma)--defsym=__heap_end=0x80ffff$(comma)--defsym=__DATA_REGION_ORIGIN__=0x802200$(comma)--defsym=__DATA_REGION_LENGTH__=0xde00))
$(eval $(call firmware,light,atmega1284p,Main_Light,-DPROG_ID_ZUSATZ='"LIGHT-ITA2"' -DiTelex_Light,))

fastboot: check-dependencies
	$(MAKE) -C FastBoot all
	@mkdir -p $(BUILD_ROOT)/fastboot
	cp FastBoot/bootload.elf FastBoot/bootload.hex FastBoot/bootload.map $(BUILD_ROOT)/fastboot/

clean-fastboot:
	$(RM) FastBoot/bootload.o FastBoot/stub.o FastBoot/bootload.elf \
		FastBoot/bootload.map FastBoot/bootload.lst FastBoot/stub.lst
	$(RM) -r $(BUILD_ROOT)/fastboot

clean-test:
	$(MAKE) -C $(TESTS_DIR) clean

clean-test-avr:
	$(MAKE) -C $(TESTS_DIR)/avr clean

clean: clean-standard clean-light clean-fastboot clean-test clean-test-avr
