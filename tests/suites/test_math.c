/**
 * \file
 * \brief Host tests for the byte-order and BCD helpers.
 *
 * The unit under test is \c system/math/math.c. On an AVR the byte-order
 * functions are hand-written assembler; off the AVR they fall back to a C
 * expression, and it is that fallback the host compiles. The cases below
 * therefore serve two purposes: they pin the contract both implementations
 * owe their callers, and they check that the C fallback still agrees with it.
 *
 * \c ChangeEndian16bit takes an \c unsigned \c int and \c ChangeEndian32bit an
 * \c unsigned \c long, which are 16 and 32 bits wide on the AVR but wider on a
 * host. Every value used here fits the AVR width, so both targets produce the
 * same answer; a case that passed a wider value would be testing something the
 * firmware can never see.
 *
 * \ingroup HOSTTESTS
 */

#include "test_framework.h"

#include "system/math/math.h"

/** \addtogroup HOSTTESTS
 *  @{
 */

TEST_CASE(swaps_the_two_bytes_of_a_16_bit_value)
{
	TEST_ASSERT_EQ_UINT(0x3412, ChangeEndian16bit(0x1234));
	TEST_ASSERT_EQ_UINT(0xff00, ChangeEndian16bit(0x00ff));
	TEST_ASSERT_EQ_UINT(0x00ff, ChangeEndian16bit(0xff00));
	TEST_ASSERT_EQ_UINT(0x0000, ChangeEndian16bit(0x0000));
	TEST_ASSERT_EQ_UINT(0xffff, ChangeEndian16bit(0xffff));
}

TEST_CASE(swaps_the_four_bytes_of_a_32_bit_value)
{
	TEST_ASSERT_EQ_UINT(0x78563412UL, ChangeEndian32bit(0x12345678UL));
	TEST_ASSERT_EQ_UINT(0x000000ffUL, ChangeEndian32bit(0xff000000UL));
	TEST_ASSERT_EQ_UINT(0xff000000UL, ChangeEndian32bit(0x000000ffUL));
	TEST_ASSERT_EQ_UINT(0x00000000UL, ChangeEndian32bit(0x00000000UL));
	TEST_ASSERT_EQ_UINT(0xffffffffUL, ChangeEndian32bit(0xffffffffUL));
}

TEST_CASE(swapping_twice_returns_the_original_value)
{
	static const unsigned int words[] = { 0x0000, 0x0001, 0x1234, 0x8000, 0xabcd, 0xffff };
	static const unsigned long longs[] = {
		0x00000000UL, 0x00000001UL, 0x12345678UL, 0x80000000UL, 0xdeadbeefUL, 0xffffffffUL
	};
	unsigned index;

	for (index = 0; index < TEST_COUNT(words); index++) {
		TEST_ASSERT_EQ_UINT(words[index], ChangeEndian16bit(ChangeEndian16bit(words[index])));
	}

	for (index = 0; index < TEST_COUNT(longs); index++) {
		TEST_ASSERT_EQ_UINT(longs[index], ChangeEndian32bit(ChangeEndian32bit(longs[index])));
	}
}

TEST_CASE(converts_packed_bcd_to_binary)
{
	/* The real-time clock and the DCF77 decoder hand the firmware packed
	 * BCD, one decimal digit per nibble. */
	TEST_ASSERT_EQ_UINT(0, (unsigned char)bcd2bin(0x00));
	TEST_ASSERT_EQ_UINT(9, (unsigned char)bcd2bin(0x09));
	TEST_ASSERT_EQ_UINT(10, (unsigned char)bcd2bin(0x10));
	TEST_ASSERT_EQ_UINT(42, (unsigned char)bcd2bin(0x42));
	TEST_ASSERT_EQ_UINT(59, (unsigned char)bcd2bin(0x59));
	TEST_ASSERT_EQ_UINT(99, (unsigned char)bcd2bin(0x99));
}

TEST_CASE(converts_binary_to_packed_bcd)
{
	TEST_ASSERT_EQ_UINT(0x00, (unsigned char)bin2bcd(0));
	TEST_ASSERT_EQ_UINT(0x09, (unsigned char)bin2bcd(9));
	TEST_ASSERT_EQ_UINT(0x10, (unsigned char)bin2bcd(10));
	TEST_ASSERT_EQ_UINT(0x42, (unsigned char)bin2bcd(42));
	TEST_ASSERT_EQ_UINT(0x59, (unsigned char)bin2bcd(59));
	TEST_ASSERT_EQ_UINT(0x99, (unsigned char)bin2bcd(99));
}

TEST_CASE(bcd_conversion_round_trips_across_its_whole_range)
{
	unsigned value;

	/* Two packed digits hold 0 to 99, which covers every field the clock
	 * hands over: seconds, minutes, hours, day, month and two-digit year. */
	for (value = 0; value <= 99; value++) {
		const unsigned char packed = (unsigned char)bin2bcd((char)value);

		TEST_ASSERT_EQ_UINT(value, (unsigned char)bcd2bin((char)packed));
	}
}

TEST_CASE(bcd_conversion_does_not_validate_its_input)
{
	/* Neither direction range-checks, so a nibble above 9 or a value above
	 * 99 produces a number rather than an error. Callers that can see
	 * corrupt clock data have to range-check before converting; this case
	 * records what they would get if they did not. */
	TEST_ASSERT_EQ_UINT(15, (unsigned char)bcd2bin(0x0f));
	TEST_ASSERT_EQ_UINT(0xa0, (unsigned char)bin2bcd(100));
}

/**
 * \brief Runs the byte-order and BCD suite.
 * \return 0 when every case passed, 1 otherwise.
 */
int main(void)
{
	static const TTestCase cases[] = {
		TEST_ENTRY(swaps_the_two_bytes_of_a_16_bit_value),
		TEST_ENTRY(swaps_the_four_bytes_of_a_32_bit_value),
		TEST_ENTRY(swapping_twice_returns_the_original_value),
		TEST_ENTRY(converts_packed_bcd_to_binary),
		TEST_ENTRY(converts_binary_to_packed_bcd),
		TEST_ENTRY(bcd_conversion_round_trips_across_its_whole_range),
		TEST_ENTRY(bcd_conversion_does_not_validate_its_input),
	};

	return test_run_suite("math", cases, TEST_COUNT(cases));
}

/** @} */
