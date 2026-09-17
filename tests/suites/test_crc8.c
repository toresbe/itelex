/**
 * \file
 * \brief Host tests for the CRC-8 used on the backplane.
 *
 * The unit under test is \c system/math/crc8.c. Its inner loop is the
 * reflected CRC-8 with polynomial x^8+x^5+x^4+1 and a zero initial value —
 * the Dallas/Maxim CRC-8 that 1-Wire devices use — so the expected values
 * below are the ones the published definition gives, including its documented
 * check value for the ASCII string "123456789".
 *
 * \ingroup HOSTTESTS
 */

#include "test_framework.h"

#include "system/math/crc8.h"

#include <string.h>

/** \addtogroup HOSTTESTS
 *  @{
 */

/**
 * \brief Computes the CRC of a NUL-terminated string.
 * \param text Text to checksum, excluding its terminator.
 * \return The CRC.
 */
static uint8_t crc8_of_text(const char *text)
{
	return (uint8_t)crc8((char *)text, (char)strlen(text));
}

TEST_CASE(matches_the_published_check_value)
{
	/* The check value the CRC-8/MAXIM definition publishes for "123456789". */
	TEST_ASSERT_EQ_UINT(0xa1, crc8_of_text("123456789"));
}

TEST_CASE(an_empty_message_has_the_initial_value)
{
	TEST_ASSERT_EQ_UINT(0x00, crc8_of_text(""));
}

TEST_CASE(known_single_byte_messages)
{
	char byte;

	byte = 0x00;
	TEST_ASSERT_EQ_UINT(0x00, (uint8_t)crc8(&byte, 1));

	byte = (char)0xff;
	TEST_ASSERT_EQ_UINT(0x35, (uint8_t)crc8(&byte, 1));
}

TEST_CASE(a_known_multi_byte_message)
{
	char message[7] = { 0x02, 0x1c, (char)0xb8, 0x01, 0x00, 0x00, 0x00 };

	TEST_ASSERT_EQ_UINT(0xa2, (uint8_t)crc8(message, sizeof(message)));
}

TEST_CASE(appending_the_crc_makes_the_message_check_to_zero)
{
	/* The defining property of this CRC family, and the reason a receiver
	 * can check a frame by running the CRC over the frame plus its CRC
	 * byte and testing for zero. */
	char message[8] = { 'i', '-', 't', 'e', 'l', 'e', 'x', 0 };

	message[7] = crc8(message, 7);

	TEST_ASSERT_EQ_UINT(0xfa, (uint8_t)message[7]);
	TEST_ASSERT_EQ_UINT(0x00, (uint8_t)crc8(message, sizeof(message)));
}

TEST_CASE(the_order_of_the_bytes_matters)
{
	char forward[2] = { 'a', 'b' };
	char backward[2] = { 'b', 'a' };

	TEST_ASSERT_EQ_UINT(0x47, (uint8_t)crc8(forward, sizeof(forward)));
	TEST_ASSERT_EQ_UINT(0xf0, (uint8_t)crc8(backward, sizeof(backward)));
}

TEST_CASE(every_single_bit_error_changes_the_crc)
{
	static const char message[8] = { 0x10, 0x42, 0x7f, (char)0x80, 0x01, 0x00, (char)0xff, 0x5a };
	const uint8_t expected = (uint8_t)crc8((char *)message, sizeof(message));
	unsigned position;

	/* A CRC that failed to notice a flipped bit would be worse than no
	 * CRC at all on a bus that shares a backplane with relay coils. */
	for (position = 0; position < sizeof(message) * 8; position++) {
		char corrupted[sizeof(message)];

		memcpy(corrupted, message, sizeof(corrupted));
		corrupted[position / 8] ^= (char)(1u << (position % 8));

		if ((uint8_t)crc8(corrupted, sizeof(corrupted)) == expected) {
			TEST_FAIL("flipping bit %u left the CRC at 0x%02x", position, expected);
		}
	}
}

TEST_CASE(trailing_zero_bytes_change_the_crc)
{
	char without[3] = { 'a', 'b', 'c' };
	char with[4] = { 'a', 'b', 'c', 0x00 };

	/* True of this CRC because it has no final xor and a zero initial
	 * value only hides leading zeroes, not trailing ones. */
	TEST_ASSERT(crc8(without, sizeof(without)) != crc8(with, sizeof(with)));
}

TEST_CASE(leading_zero_bytes_do_not_change_the_crc)
{
	char without[3] = { 'a', 'b', 'c' };
	char with[4] = { 0x00, 'a', 'b', 'c' };

	/* The flip side of a zero initial value with no input reflection: a
	 * frame that gains a leading zero byte checks out unchanged, so frame
	 * length has to be carried separately rather than inferred. */
	TEST_ASSERT_EQ_UINT((uint8_t)crc8(without, sizeof(without)),
		(uint8_t)crc8(with, sizeof(with)));
}

/**
 * \brief Runs the CRC-8 suite.
 * \return 0 when every case passed, 1 otherwise.
 */
int main(void)
{
	static const TTestCase cases[] = {
		TEST_ENTRY(matches_the_published_check_value),
		TEST_ENTRY(an_empty_message_has_the_initial_value),
		TEST_ENTRY(known_single_byte_messages),
		TEST_ENTRY(a_known_multi_byte_message),
		TEST_ENTRY(appending_the_crc_makes_the_message_check_to_zero),
		TEST_ENTRY(the_order_of_the_bytes_matters),
		TEST_ENTRY(every_single_bit_error_changes_the_crc),
		TEST_ENTRY(trailing_zero_bytes_change_the_crc),
		TEST_ENTRY(leading_zero_bytes_do_not_change_the_crc),
	};

	return test_run_suite("crc8", cases, TEST_COUNT(cases));
}

/** @} */
