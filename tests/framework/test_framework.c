/**
 * \file
 * \brief Implementation of the host test framework.
 *
 * \ingroup HOSTTESTS
 */

#include "test_framework.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** \addtogroup HOSTTESTS
 *  @{
 */

/** \brief Where \ref test_fail returns to: the runner's loop over the cases. */
static jmp_buf test_abandon_case;

/** \brief Whether a case is running, and \ref test_abandon_case is therefore armed. */
static int test_case_is_running;

/** \brief Diagnostic for the case that just failed, or an empty string. */
static char test_diagnostic[512];

/** \brief Buffers behind \ref test_describe_char, one per slot. */
static char test_char_description[2][16];

const char *test_describe_char(int value, int slot)
{
	char *const buffer = test_char_description[slot == 0 ? 0 : 1];
	const size_t size = sizeof(test_char_description[0]);

	switch (value) {
	case '\r':
		snprintf(buffer, size, "'\\r'");
		break;
	case '\n':
		snprintf(buffer, size, "'\\n'");
		break;
	case '\0':
		snprintf(buffer, size, "'\\0'");
		break;
	default:
		if (value >= 0x20 && value < 0x7f) {
			snprintf(buffer, size, "'%c' (0x%02x)", value, (unsigned)value);
		} else {
			snprintf(buffer, size, "0x%02x", (unsigned)value);
		}
		break;
	}

	return buffer;
}

int test_strings_differ(const char *expected, const char *actual)
{
	if (expected == NULL || actual == NULL) {
		return expected != actual;
	}

	return strcmp(expected, actual) != 0;
}

size_t test_first_difference(const void *expected, const void *actual, size_t length)
{
	const unsigned char *const wanted = expected;
	const unsigned char *const got = actual;
	size_t offset;

	for (offset = 0; offset < length; offset++) {
		if (wanted[offset] != got[offset]) {
			break;
		}
	}

	return offset;
}

void test_fail(const char *file, int line, const char *format, ...)
{
	int written;
	va_list arguments;

	written = snprintf(test_diagnostic, sizeof(test_diagnostic), "%s:%d: ", file, line);
	if (written < 0 || (size_t)written >= sizeof(test_diagnostic)) {
		written = 0;
	}

	va_start(arguments, format);
	vsnprintf(test_diagnostic + written, sizeof(test_diagnostic) - (size_t)written,
		format, arguments);
	va_end(arguments);

	if (test_case_is_running) {
		longjmp(test_abandon_case, 1);
	}

	/* A failure reported from outside a case has nowhere to jump to; say so
	 * loudly rather than carrying on with a half-run suite. */
	fprintf(stderr, "Bail out! %s\n", test_diagnostic);
	fflush(stderr);
	exit(2);
}

int test_run_suite(const char *suite_name, const TTestCase *cases, size_t count)
{
	/* Both counters are read after a longjmp, so they have to survive it. */
	volatile size_t index;
	volatile size_t failures = 0;

	printf("# %s\n", suite_name);
	printf("TAP version 13\n");
	printf("1..%zu\n", count);

	for (index = 0; index < count; index++) {
		test_diagnostic[0] = '\0';
		test_case_is_running = 1;

		if (setjmp(test_abandon_case) == 0) {
			cases[index].run();
			printf("ok %zu - %s\n", index + 1, cases[index].name);
		} else {
			failures++;
			printf("not ok %zu - %s\n", index + 1, cases[index].name);
			printf("  ---\n");
			printf("  %s\n", test_diagnostic);
			printf("  ...\n");
		}

		test_case_is_running = 0;
	}

	if (failures != 0) {
		printf("# %s: %zu of %zu cases failed\n", suite_name, failures, count);
	}

	fflush(stdout);
	return failures == 0 ? 0 : 1;
}

/** @} */
