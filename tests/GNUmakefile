# Host unit tests for the pure-logic parts of the i-Telex firmware.
#
# These are built by the host compiler, not avr-gcc, and run on the build
# machine. Run them with `make test` from the repository root, or `make test`
# here. See README.md in this directory for what is covered and why.

SHELL := /bin/sh

HOST_CC ?= cc
BUILD_DIR ?= build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

REPO_ROOT := ..
COMMON_DIR := $(REPO_ROOT)/dependencies/itelex-misc/Gemeinsam
AVR_CLIBS_DIR := $(REPO_ROOT)/dependencies/avr-clibs

# Flags that have to match the firmware build, because they change what the
# code under test means:
#   -funsigned-char         avr-gcc's default, and the firmware relies on it.
#                           A host `char` is signed, which would make the tests
#                           exercise different code than the one that ships.
#   -std=gnu99              as in the firmware's COMMON_CFLAGS.
ABI_CFLAGS := -std=gnu99 -funsigned-char

# AVR-Clibs is on the list because BusKomm.h, which Parsing.c needs for the
# extension-to-address conversion, includes bits.h from there.
INCLUDES := -Ishim -Iframework -I$(REPO_ROOT) -I$(COMMON_DIR) -I$(AVR_CLIBS_DIR)

# New code — the framework and the suites — is held to a strict warning level.
TEST_CFLAGS := $(ABI_CFLAGS) $(INCLUDES) -g -O1 -Wall -Wextra -Wshadow -Werror

# Firmware code under test is compiled at the warning level the firmware
# itself uses, so that a test run does not turn into a lint report for code
# the tests are not changing.
UNIT_CFLAGS := $(ABI_CFLAGS) $(INCLUDES) -g -O1 -Wall

# One entry per suite: <name>_UNITS lists the firmware sources the suite links
# against, and the suite body itself is suites/test_<name>.c.
SUITES := baudot_code fifo_buffer crc8 math base64 string_utils parsing frame_scanner

baudot_code_UNITS  := $(COMMON_DIR)/BaudotCode.c
fifo_buffer_UNITS  := $(COMMON_DIR)/FifoPuffer.c shim/avr_shim.c
crc8_UNITS         := $(REPO_ROOT)/system/math/crc8.c
math_UNITS         := $(REPO_ROOT)/system/math/math.c
base64_UNITS       := $(REPO_ROOT)/system/base64/base64.c
string_utils_UNITS := $(REPO_ROOT)/system/string/string.c
parsing_UNITS      := $(REPO_ROOT)/iTelex/Parsing.c
# FrameScanner is a header-only module: ScanFrame is force-inlined into its
# one call site in iTelex.c, so there is no translation unit to link.
frame_scanner_UNITS :=

BINARIES := $(addprefix $(BIN_DIR)/,$(SUITES))

.PHONY: all test list check-dependencies clean help

all: $(BINARIES)

help:
	@echo "Targets: all (build the suites), test (build and run them), list, clean"
	@echo "Override HOST_CC to build the suites with a different host compiler."

list:
	@for suite in $(SUITES); do echo "$$suite"; done

check-dependencies:
	@test -f "$(COMMON_DIR)/BaudotCode.c" || { \
		echo "error: missing itelex-misc submodule; run 'make bootstrap' in the repository root" >&2; \
		exit 1; }
	@test -f "$(AVR_CLIBS_DIR)/bits.h" || { \
		echo "error: missing avr-clibs submodule; run 'make bootstrap' in the repository root" >&2; \
		exit 1; }

# Runs every suite even when an earlier one fails, so that one run reports the
# whole picture, then fails the build if any suite did.
test: check-dependencies $(BINARIES)
	@failed=''; \
	for suite in $(SUITES); do \
		$(BIN_DIR)/$$suite || failed="$$failed $$suite"; \
	done; \
	if [ -n "$$failed" ]; then \
		echo "# failing suites:$$failed" >&2; \
		exit 1; \
	fi; \
	echo "# all suites passed"

# $(1) is the suite name, $(2) the firmware source under test.
define unit_rule
$$(OBJ_DIR)/$(1)/$$(notdir $$(patsubst %.c,%.o,$(2))): $(2)
	@mkdir -p $$(@D)
	$$(HOST_CC) $$(UNIT_CFLAGS) -MMD -MP -c $$< -o $$@
endef

# $(1) is the suite name.
define suite_rules
$(1)_OBJECTS := $$(OBJ_DIR)/$(1)/test_$(1).o $$(OBJ_DIR)/$(1)/test_framework.o \
	$$(foreach unit,$$($(1)_UNITS),$$(OBJ_DIR)/$(1)/$$(notdir $$(patsubst %.c,%.o,$$(unit))))

$$(OBJ_DIR)/$(1)/test_$(1).o: suites/test_$(1).c
	@mkdir -p $$(@D)
	$$(HOST_CC) $$(TEST_CFLAGS) -MMD -MP -c $$< -o $$@

$$(OBJ_DIR)/$(1)/test_framework.o: framework/test_framework.c
	@mkdir -p $$(@D)
	$$(HOST_CC) $$(TEST_CFLAGS) -MMD -MP -c $$< -o $$@

$$(foreach unit,$$($(1)_UNITS),$$(eval $$(call unit_rule,$(1),$$(unit))))

$$(BIN_DIR)/$(1): $$($(1)_OBJECTS)
	@mkdir -p $$(@D)
	$$(HOST_CC) $$(TEST_CFLAGS) $$^ -o $$@

-include $$($(1)_OBJECTS:.o=.d)
endef

$(foreach suite,$(SUITES),$(eval $(call suite_rules,$(suite))))

clean:
	$(RM) -r $(BUILD_DIR)
