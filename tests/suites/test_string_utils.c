/**
 * \file
 * \brief Host tests for the hex parsing helpers.
 *
 * The unit under test is \c system/string/string.c: \c atoh turns one hex
 * digit into its value, and \c strtobin parses a run of hex digits into bytes,
 * skipping \c ':' and \c '-' separators. Between them they read the MAC
 * address out of the stored configuration, the device signature and fuse bytes
 * an attached card reports over ISP, and percent-escapes in an HTTP request.
 *
 * Both return \c char. The firmware builds with \c -funsigned-char, as these
 * suites do, so the \c -1 both functions use internally to mean "bad input"
 * reaches the caller as 255 and never compares equal to \c -1. The cases below
 * record that as it stands today rather than as the code reads, because a
 * caller written against the apparent contract is the bug, not the test.
 *
 * \ingroup HOSTTESTS
 */

#include "test_framework.h"

#include "system/string/string.h"

/** \addtogroup HOSTTESTS
 *  @{
 */

/** \brief What the two helpers return for input they cannot parse. */
#define PARSE_FAILED 255

TEST_CASE(reads_the_decimal_digits)
{
	unsigned digit;

	for (digit = 0; digit <= 9; digit++) {
		TEST_ASSERT_EQ_UINT(digit, (unsigned char)atoh((char)('0' + digit)));
	}
}

TEST_CASE(reads_both_cases_of_the_letter_digits)
{
	unsigned offset;

	for (offset = 0; offset < 6; offset++) {
		TEST_ASSERT_EQ_UINT(10 + offset, (unsigned char)atoh((char)('a' + offset)));
		TEST_ASSERT_EQ_UINT(10 + offset, (unsigned char)atoh((char)('A' + offset)));
	}
}

TEST_CASE(rejects_characters_that_are_not_hex_digits)
{
	static const char rejected[] = { 'g', 'G', 'z', '/', ':', ' ', '\0', (char)0x80, (char)0xff };
	unsigned index;

	for (index = 0; index < TEST_COUNT(rejected); index++) {
		TEST_ASSERT_EQ_UINT(PARSE_FAILED, (unsigned char)atoh(rejected[index]));
	}
}

TEST_CASE(the_rejection_value_does_not_compare_negative)
{
	/* The unit assigns -1 for bad input, but returns it as an unsigned
	 * char, so a caller testing `atoh(c) < 0` never sees a failure and
	 * carries on with 255 as though it were a digit. Recorded here so that
	 * fixing the return type has to come past this case. */
	TEST_ASSERT_FALSE(atoh('z') < 0);
	TEST_ASSERT(atoh('z') == (char)PARSE_FAILED);
}

TEST_CASE(parses_a_mac_address_without_separators)
{
	char parsed[6];

	TEST_ASSERT_EQ_UINT(0, (unsigned char)strtobin("001122334455", parsed, 6));
	TEST_ASSERT_EQ_MEM("\x00\x11\x22\x33\x44\x55", parsed, sizeof(parsed));
}

TEST_CASE(parses_a_mac_address_with_either_separator)
{
	char parsed[6];

	TEST_ASSERT_EQ_UINT(0, (unsigned char)strtobin("00:11:22:33:44:55", parsed, 6));
	TEST_ASSERT_EQ_MEM("\x00\x11\x22\x33\x44\x55", parsed, sizeof(parsed));

	TEST_ASSERT_EQ_UINT(0, (unsigned char)strtobin("de-ad-be-ef-00-01", parsed, 6));
	TEST_ASSERT_EQ_MEM("\xde\xad\xbe\xef\x00\x01", parsed, sizeof(parsed));
}

TEST_CASE(parses_upper_and_lower_case_alike)
{
	char lower[6];
	char upper[6];

	TEST_ASSERT_EQ_UINT(0, (unsigned char)strtobin("aabbccddeeff", lower, 6));
	TEST_ASSERT_EQ_UINT(0, (unsigned char)strtobin("AABBCCDDEEFF", upper, 6));
	TEST_ASSERT_EQ_MEM(lower, upper, sizeof(lower));
}

TEST_CASE(parses_the_three_byte_fields_the_isp_master_reads)
{
	char signature[3];

	/* The signature bytes of an ATmega1284P, as an attached card reports
	 * them over ISP. */
	TEST_ASSERT_EQ_UINT(0, (unsigned char)strtobin("1e9705", signature, 3));
	TEST_ASSERT_EQ_MEM("\x1e\x97\x05", signature, sizeof(signature));
}

TEST_CASE(rejects_a_string_with_too_few_digits)
{
	char parsed[6];

	/* The length argument is a byte count and the input has to hold
	 * exactly twice that many digits, so a truncated MAC is refused
	 * rather than left half-filled. */
	TEST_ASSERT_EQ_UINT(PARSE_FAILED, (unsigned char)strtobin("00112233", parsed, 6));
}

TEST_CASE(rejects_a_string_with_too_many_digits)
{
	char parsed[8];

	/* The destination is deliberately larger than the six bytes asked
	 * for: the unit stops at the limit and reports failure, and this case
	 * would show it up if it wrote past the count instead. */
	parsed[6] = 0x5a;
	parsed[7] = 0x5a;

	TEST_ASSERT_EQ_UINT(PARSE_FAILED, (unsigned char)strtobin("0011223344556677", parsed, 6));
	TEST_ASSERT_EQ_UINT(0x5a, (unsigned char)parsed[6]);
	TEST_ASSERT_EQ_UINT(0x5a, (unsigned char)parsed[7]);
}

TEST_CASE(rejects_an_empty_string)
{
	char parsed[6];

	TEST_ASSERT_EQ_UINT(PARSE_FAILED, (unsigned char)strtobin("", parsed, 6));
}

TEST_CASE(the_rejection_value_does_not_compare_negative_either)
{
	char parsed[6];

	/* Same trap as \ref the_rejection_value_does_not_compare_negative:
	 * callers have to test against 0, not against a negative value. */
	TEST_ASSERT_FALSE(strtobin("00", parsed, 6) < 0);
	TEST_ASSERT(strtobin("00", parsed, 6) != 0);
}

/**
 * \brief Runs the hex parsing suite.
 * \return 0 when every case passed, 1 otherwise.
 */
int main(void)
{
	static const TTestCase cases[] = {
		TEST_ENTRY(reads_the_decimal_digits),
		TEST_ENTRY(reads_both_cases_of_the_letter_digits),
		TEST_ENTRY(rejects_characters_that_are_not_hex_digits),
		TEST_ENTRY(the_rejection_value_does_not_compare_negative),
		TEST_ENTRY(parses_a_mac_address_without_separators),
		TEST_ENTRY(parses_a_mac_address_with_either_separator),
		TEST_ENTRY(parses_upper_and_lower_case_alike),
		TEST_ENTRY(parses_the_three_byte_fields_the_isp_master_reads),
		TEST_ENTRY(rejects_a_string_with_too_few_digits),
		TEST_ENTRY(rejects_a_string_with_too_many_digits),
		TEST_ENTRY(rejects_an_empty_string),
		TEST_ENTRY(the_rejection_value_does_not_compare_negative_either),
	};

	return test_run_suite("string", cases, TEST_COUNT(cases));
}

/** @} */
