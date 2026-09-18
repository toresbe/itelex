# AVR integration tests executed by simavr.

SHELL := /bin/sh

BUILD_DIR ?= build
AVR_TOOLCHAIN_ROOT ?=
AVR_PREFIX := $(if $(AVR_TOOLCHAIN_ROOT),$(patsubst %/,%,$(AVR_TOOLCHAIN_ROOT))/bin/,)avr-
AVR_CC ?= $(AVR_PREFIX)gcc
AVR_NM ?= $(AVR_PREFIX)nm
HOST_CC ?= cc
ITELEX_TRACE_LEVEL ?= 255
SIMAVR_ROOT ?= $(shell command -v brew >/dev/null 2>&1 && brew --prefix simavr 2>/dev/null)
LIBELF_ROOT ?= $(shell command -v brew >/dev/null 2>&1 && brew --prefix libelf 2>/dev/null)

AVR_CFLAGS := -mmcu=atmega1284p -DF_CPU=16000000UL -Os -gdwarf-2 \
	-std=gnu99 -funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums \
	-fgnu89-inline -ffunction-sections -fdata-sections -mrelax \
	-I../.. -I../../dependencies/itelex-misc/Gemeinsam \
	-I../../dependencies/avr-clibs
AVR_LDFLAGS := -mmcu=atmega1284p -Wl,--gc-sections -mrelax

SIMAVR_CFLAGS := $(if $(SIMAVR_ROOT),-I$(SIMAVR_ROOT)/include/simavr,-I/usr/include/simavr)
SIMAVR_LDFLAGS := $(if $(SIMAVR_ROOT),-L$(SIMAVR_ROOT)/lib,) \
	$(if $(LIBELF_ROOT),-L$(LIBELF_ROOT)/lib,) -lsimavr -lelf
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror $(SIMAVR_CFLAGS)
SERIAL_AVR_CFLAGS := $(AVR_CFLAGS) -iquote $(BUILD_DIR)/generated \
	-DiTelex_Light -DITELEX_SIM_TEST -DITELEX_TRACE_LEVEL=$(ITELEX_TRACE_LEVEL) \
	-DPROG_ID_ZUSATZ='"SIMAVR"'

TIMER_FIXTURE := $(BUILD_DIR)/timer0_fixture.elf
SERIAL_TX_FIXTURE := $(BUILD_DIR)/serial_tx_fixture.elf
SERIAL_RX_FIXTURE := $(BUILD_DIR)/serial_rx_fixture.elf
RUNNER := $(BUILD_DIR)/simavr_tests

.PHONY: all test clean

all: $(TIMER_FIXTURE) $(SERIAL_TX_FIXTURE) $(SERIAL_RX_FIXTURE) $(RUNNER)

test: all
	$(RUNNER) $(TIMER_FIXTURE) $(SERIAL_TX_FIXTURE) $(SERIAL_RX_FIXTURE) \
		0x$$($(AVR_NM) $(SERIAL_RX_FIXTURE) | awk '$$3 == "BusEmpfMark" { print $$1 }') \
		0x$$($(AVR_NM) $(SERIAL_RX_FIXTURE) | awk '$$3 == "EmpfPuffer" { print $$1 }')

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/timer0_fixture.o: fixtures/timer0_fixture.c | $(BUILD_DIR)
	$(AVR_CC) $(AVR_CFLAGS) -c $< -o $@

$(BUILD_DIR)/timer0.o: ../../hardware/timer0/timer0.c | $(BUILD_DIR)
	$(AVR_CC) $(AVR_CFLAGS) -c $< -o $@

$(BUILD_DIR)/generated/config.h: ../../iTelex.config.h | $(BUILD_DIR)
	mkdir -p $(@D)
	cp $< $@

$(BUILD_DIR)/generated/SvnVersion.h: ../../scripts/gen-version-header.sh | $(BUILD_DIR)
	mkdir -p $(@D)
	sh $< "simavr-test" $@

$(BUILD_DIR)/itelex.o: ../../iTelex/iTelex.c $(BUILD_DIR)/generated/config.h $(BUILD_DIR)/generated/SvnVersion.h | $(BUILD_DIR)
	$(AVR_CC) $(SERIAL_AVR_CFLAGS) -c $< -o $@

$(BUILD_DIR)/fifo_puffer.o: ../../dependencies/itelex-misc/Gemeinsam/FifoPuffer.c | $(BUILD_DIR)
	$(AVR_CC) $(AVR_CFLAGS) -c $< -o $@

$(BUILD_DIR)/serial_stubs.o: fixtures/serial_stubs.c $(BUILD_DIR)/generated/config.h | $(BUILD_DIR)
	$(AVR_CC) $(SERIAL_AVR_CFLAGS) -c $< -o $@

$(BUILD_DIR)/serial_tx_fixture.o: fixtures/serial_fixture.c $(BUILD_DIR)/generated/config.h | $(BUILD_DIR)
	$(AVR_CC) $(SERIAL_AVR_CFLAGS) -DSERIAL_TEST_TRANSMIT -c $< -o $@

$(BUILD_DIR)/serial_rx_fixture.o: fixtures/serial_fixture.c $(BUILD_DIR)/generated/config.h | $(BUILD_DIR)
	$(AVR_CC) $(SERIAL_AVR_CFLAGS) -c $< -o $@

$(TIMER_FIXTURE): $(BUILD_DIR)/timer0_fixture.o $(BUILD_DIR)/timer0.o
	$(AVR_CC) $(AVR_LDFLAGS) $^ -o $@

SERIAL_COMMON_OBJECTS := $(BUILD_DIR)/itelex.o $(BUILD_DIR)/timer0.o \
	$(BUILD_DIR)/fifo_puffer.o $(BUILD_DIR)/serial_stubs.o

$(SERIAL_TX_FIXTURE): $(BUILD_DIR)/serial_tx_fixture.o $(SERIAL_COMMON_OBJECTS)
	$(AVR_CC) $(AVR_LDFLAGS) $^ -o $@

$(SERIAL_RX_FIXTURE): $(BUILD_DIR)/serial_rx_fixture.o $(SERIAL_COMMON_OBJECTS)
	$(AVR_CC) $(AVR_LDFLAGS) $^ -o $@

$(RUNNER): runner/simavr_tests.c | $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $< -o $@ $(SIMAVR_LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
