/**
 * \file
 * \brief A small assertion and reporting framework for the host test suites.
 *
 * The firmware pins its dependencies deliberately, so the test suites bring no
 * third-party framework with them: everything here is plain C99 that any host
 * compiler already has. A suite declares its cases with \ref TEST_CASE, lists
 * them in a table built from \ref TEST_ENTRY, and hands the table to
 * \ref test_run_suite from \c main.
 *
 * A failing assertion prints a diagnostic and abandons the current case, but
 * the suite continues with the next one, so one broken function does not hide
 * the state of the others. Output is TAP version 13, which reads well in a
 * terminal and is understood by CI consumers without further tooling.
 *
 * \code
 * TEST_CASE(letters_shift_maps_code_24_to_a)
 * {
 *     TBaudotMode mode = BaudotMode_BuchstabenEmpfangen;
 *     TEST_ASSERT_EQ_CHAR('a', CodeZuZeichen(24, &mode));
 * }
 *
 * int main(void)
 * {
 *     static const TTestCase cases[] = {
 *         TEST_ENTRY(letters_shift_maps_code_24_to_a),
 *     };
 *     return test_run_suite("BaudotCode", cases, TEST_COUNT(cases));
 * }
 * \endcode
 *
 * \ingroup HOSTTESTS
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stddef.h>

/**
 * \defgroup HOSTTESTS Host test suites
 *
 * Unit tests for the parts of the firmware that are pure logic: character-set
 * conversion, the ring buffer, the checksums and the small string and encoding
 * helpers. They are compiled by the host compiler, not by avr-gcc, and run on
 * the build machine, which is why they can run on every push without any
 * hardware attached.
 *
 * Nothing here is linked into the firmware. See \c tests/README.md for what is
 * covered, what deliberately is not, and how to add a suite.
 *
 * @{
 */

/**
 * \brief One test case: a human-readable name and the function that runs it.
 */
typedef struct {
	const char *name;   /**< \brief Case name, as it appears in the TAP output. */
	void (*run)(void);  /**< \brief The case body. */
} TTestCase;

/**
 * \brief Opens the body of a test case.
 *
 * The case name doubles as the function name and as the description in the
 * report, so spell it as a sentence about the behaviour under test —
 * \c stores_bytes_until_one_slot_remains rather than \c test_buffer_3.
 *
 * \param name Identifier for the case.
 */
#define TEST_CASE(name) static void name(void)

/**
 * \brief Produces a \ref TTestCase table entry for a case declared with \ref TEST_CASE.
 * \param name Identifier of the case function.
 */
#define TEST_ENTRY(name) { #name, name }

/**
 * \brief Number of entries in a statically sized case table.
 * \param table The table.
 */
#define TEST_COUNT(table) (sizeof(table) / sizeof((table)[0]))

/**
 * \brief Runs every case in a table and reports the result as TAP.
 *
 * \param suite_name Name of the suite, printed as a TAP comment.
 * \param cases      The case table.
 * \param count      Number of entries in \p cases.
 * \return Process exit status: 0 when every case passed, 1 otherwise.
 */
int test_run_suite(const char *suite_name, const TTestCase *cases, size_t count);

/**
 * \brief Records a failure for the running case and abandons it.
 *
 * Called by the assertion macros; a suite can call it directly for a condition
 * the macros do not cover. Does not return.
 *
 * \param file    Source file of the failure.
 * \param line    Source line of the failure.
 * \param format  \c printf format describing the failure.
 * \param ...     Arguments for \p format.
 */
void test_fail(const char *file, int line, const char *format, ...)
#if defined(__GNUC__)
	__attribute__((format(printf, 3, 4), noreturn))
#endif
	;

/**
 * \brief Fails the running case unconditionally.
 * \param ... \c printf format and arguments describing why.
 */
#define TEST_FAIL(...) test_fail(__FILE__, __LINE__, __VA_ARGS__)

/**
 * \brief Asserts that \p condition holds.
 * \param condition Expression expected to be true.
 */
#define TEST_ASSERT(condition) \
	do { \
		if (!(condition)) { \
			TEST_FAIL("expected %s to hold", #condition); \
		} \
	} while (0)

/**
 * \brief Asserts that \p condition does not hold.
 * \param condition Expression expected to be false.
 */
#define TEST_ASSERT_FALSE(condition) \
	do { \
		if ((condition)) { \
			TEST_FAIL("expected %s not to hold", #condition); \
		} \
	} while (0)

/**
 * \brief Asserts equality of two signed integers.
 * \param expected Wanted value.
 * \param actual   Value produced by the code under test.
 */
#define TEST_ASSERT_EQ_INT(expected, actual) \
	do { \
		const long test_expected_ = (long)(expected); \
		const long test_actual_ = (long)(actual); \
		if (test_expected_ != test_actual_) { \
			TEST_FAIL("%s: expected %ld, got %ld", #actual, test_expected_, test_actual_); \
		} \
	} while (0)

/**
 * \brief Asserts equality of two unsigned integers, reporting both bases.
 *
 * Most values in this firmware are byte-sized codes that are clearest in hex
 * and in decimal at once, so the diagnostic prints both.
 *
 * \param expected Wanted value.
 * \param actual   Value produced by the code under test.
 */
#define TEST_ASSERT_EQ_UINT(expected, actual) \
	do { \
		const unsigned long test_expected_ = (unsigned long)(expected); \
		const unsigned long test_actual_ = (unsigned long)(actual); \
		if (test_expected_ != test_actual_) { \
			TEST_FAIL("%s: expected %lu (0x%lx), got %lu (0x%lx)", #actual, \
				test_expected_, test_expected_, test_actual_, test_actual_); \
		} \
	} while (0)

/**
 * \brief Asserts equality of two characters, printing them readably.
 * \param expected Wanted character.
 * \param actual   Character produced by the code under test.
 */
#define TEST_ASSERT_EQ_CHAR(expected, actual) \
	do { \
		const int test_expected_ = (unsigned char)(expected); \
		const int test_actual_ = (unsigned char)(actual); \
		if (test_expected_ != test_actual_) { \
			TEST_FAIL("%s: expected %s, got %s", #actual, \
				test_describe_char(test_expected_, 0), \
				test_describe_char(test_actual_, 1)); \
		} \
	} while (0)

/**
 * \brief Asserts that two NUL-terminated strings are equal.
 * \param expected Wanted string.
 * \param actual   String produced by the code under test.
 */
#define TEST_ASSERT_EQ_STR(expected, actual) \
	do { \
		const char *test_expected_ = (expected); \
		const char *test_actual_ = (actual); \
		if (test_strings_differ(test_expected_, test_actual_)) { \
			TEST_FAIL("%s: expected \"%s\", got \"%s\"", #actual, \
				test_expected_ ? test_expected_ : "(null)", \
				test_actual_ ? test_actual_ : "(null)"); \
		} \
	} while (0)

/**
 * \brief Asserts that two memory regions hold the same bytes.
 * \param expected Wanted bytes.
 * \param actual   Bytes produced by the code under test.
 * \param length   Number of bytes to compare.
 */
#define TEST_ASSERT_EQ_MEM(expected, actual, length) \
	do { \
		const void *test_expected_ = (expected); \
		const void *test_actual_ = (actual); \
		const size_t test_length_ = (size_t)(length); \
		const size_t test_offset_ = test_first_difference(test_expected_, test_actual_, test_length_); \
		if (test_offset_ != test_length_) { \
			TEST_FAIL("%s: byte %zu of %zu: expected 0x%02x, got 0x%02x", #actual, \
				test_offset_, test_length_, \
				((const unsigned char *)test_expected_)[test_offset_], \
				((const unsigned char *)test_actual_)[test_offset_]); \
		} \
	} while (0)

/**
 * \brief Renders a byte as a quoted character or an escape, for diagnostics.
 *
 * \param value Byte to describe.
 * \param slot  Which of the framework's two static buffers to use, so that one
 *              diagnostic can describe two bytes at once. Valid values are 0
 *              and 1.
 * \return Pointer to the framework's buffer for \p slot.
 */
const char *test_describe_char(int value, int slot);

/**
 * \brief Compares two strings, tolerating null pointers.
 * \param expected Wanted string, possibly NULL.
 * \param actual   String under test, possibly NULL.
 * \retval 1 The strings differ, or exactly one of them is NULL.
 * \retval 0 The strings are equal, or both are NULL.
 */
int test_strings_differ(const char *expected, const char *actual);

/**
 * \brief Finds the first byte at which two regions differ.
 * \param expected Wanted bytes.
 * \param actual   Bytes under test.
 * \param length   Number of bytes to compare.
 * \return Offset of the first difference, or \p length when the regions match.
 */
size_t test_first_difference(const void *expected, const void *actual, size_t length);

/** @} */

#endif /* TEST_FRAMEWORK_H */
