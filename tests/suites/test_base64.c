/**
 * \file
 * \brief Host tests for the Base64 codec.
 *
 * The unit under test is \c system/base64/base64.c, which the firmware uses to
 * build the credentials for POP3 and SMTP authentication and to read what a
 * mail server sends back. The expected values below are the RFC 4648 test
 * vectors.
 *
 * Two limits of the unit shape what is tested here:
 *
 * - \c base64_encode refuses to start unless the output buffer has room for
 *   the whole result and its terminator, so the size check is part of its
 *   contract and is exercised directly.
 * - \c base64_decode indexes its lookup table with <tt>character - 43</tt>
 *   without first checking that the character is in range, so any byte below
 *   \c '+' or above \c 'z' reads outside the table. The cases below stay
 *   inside the documented alphabet; feeding the decoder arbitrary input is a
 *   defect to fix in the unit, not a behaviour to pin down in a test.
 *
 * \ingroup HOSTTESTS
 */

#include "test_framework.h"

#include "system/base64/base64.h"

#include <string.h>

/** \addtogroup HOSTTESTS
 *  @{
 */

/** \brief Output buffer size that is ample for every vector used here. */
#define SCRATCH_SIZE 64

/**
 * \brief Encodes a NUL-terminated string into a caller-provided buffer.
 * \param out  Destination, at least \ref SCRATCH_SIZE bytes.
 * \param text Text to encode, excluding its terminator.
 * \return Whatever \c base64_encode returned.
 */
static char *encode_text(char *out, const char *text)
{
	return base64_encode(out, SCRATCH_SIZE, text, (int)strlen(text));
}

TEST_CASE(encodes_the_rfc_4648_vectors)
{
	char scratch[SCRATCH_SIZE];

	TEST_ASSERT_EQ_STR("", encode_text(scratch, ""));
	TEST_ASSERT_EQ_STR("Zg==", encode_text(scratch, "f"));
	TEST_ASSERT_EQ_STR("Zm8=", encode_text(scratch, "fo"));
	TEST_ASSERT_EQ_STR("Zm9v", encode_text(scratch, "foo"));
	TEST_ASSERT_EQ_STR("Zm9vYg==", encode_text(scratch, "foob"));
	TEST_ASSERT_EQ_STR("Zm9vYmE=", encode_text(scratch, "fooba"));
	TEST_ASSERT_EQ_STR("Zm9vYmFy", encode_text(scratch, "foobar"));
}

TEST_CASE(encodes_a_mail_login_the_way_a_server_expects_it)
{
	char scratch[SCRATCH_SIZE];

	TEST_ASSERT_EQ_STR("aXRlbGV4Om1vbQ==", encode_text(scratch, "itelex:mom"));
}

TEST_CASE(encoding_returns_a_pointer_to_the_callers_buffer)
{
	char scratch[SCRATCH_SIZE];

	/* Callers pass the result straight to the socket, so it has to be the
	 * buffer they own rather than anything with a shorter life. */
	TEST_ASSERT(encode_text(scratch, "foo") == scratch);
}

TEST_CASE(encoding_terminates_its_output)
{
	char scratch[SCRATCH_SIZE];

	memset(scratch, 'x', sizeof(scratch));
	(void)base64_encode(scratch, SCRATCH_SIZE, "foo", 3);

	TEST_ASSERT_EQ_CHAR('\0', scratch[4]);
}

TEST_CASE(encoding_pads_to_a_multiple_of_four_characters)
{
	char scratch[SCRATCH_SIZE];
	unsigned length;

	for (length = 1; length <= 12; length++) {
		TEST_ASSERT(base64_encode(scratch, SCRATCH_SIZE, "abcdefghijkl",
			(int)length) != NULL);
		TEST_ASSERT_EQ_UINT(0, strlen(scratch) % 4);
	}
}

TEST_CASE(encoding_refuses_an_output_buffer_that_is_too_small)
{
	char scratch[SCRATCH_SIZE];

	/* "foobar" needs eight characters and a terminator. */
	TEST_ASSERT(base64_encode(scratch, 8, "foobar", 6) == NULL);
	TEST_ASSERT(base64_encode(scratch, 9, "foobar", 6) != NULL);
}

TEST_CASE(decodes_the_rfc_4648_vectors)
{
	char scratch[SCRATCH_SIZE];
	int length;

	length = base64_decode(scratch, "Zm9vYmFy", SCRATCH_SIZE);
	TEST_ASSERT_EQ_INT(6, length);
	TEST_ASSERT_EQ_MEM("foobar", scratch, 6);

	length = base64_decode(scratch, "Zm9v", SCRATCH_SIZE);
	TEST_ASSERT_EQ_INT(3, length);
	TEST_ASSERT_EQ_MEM("foo", scratch, 3);

	length = base64_decode(scratch, "Zm8=", SCRATCH_SIZE);
	TEST_ASSERT_EQ_INT(2, length);
	TEST_ASSERT_EQ_MEM("fo", scratch, 2);

	length = base64_decode(scratch, "Zg==", SCRATCH_SIZE);
	TEST_ASSERT_EQ_INT(1, length);
	TEST_ASSERT_EQ_MEM("f", scratch, 1);

	length = base64_decode(scratch, "", SCRATCH_SIZE);
	TEST_ASSERT_EQ_INT(0, length);
}

TEST_CASE(decoding_handles_the_whole_alphabet)
{
	static const char encoded[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char scratch[SCRATCH_SIZE];
	int length;

	/* Every one of the 64 symbols, so a wrong entry anywhere in the
	 * reverse lookup table shows up. */
	length = base64_decode(scratch, encoded, SCRATCH_SIZE);
	TEST_ASSERT_EQ_INT(48, length);

	/* The symbols stand for the values 0..63 in order, so packing them
	 * into six-bit fields gives these bytes at the two ends of the run. */
	TEST_ASSERT_EQ_UINT(0x00, (unsigned char)scratch[0]);
	TEST_ASSERT_EQ_UINT(0x10, (unsigned char)scratch[1]);
	TEST_ASSERT_EQ_UINT(0x83, (unsigned char)scratch[2]);
	TEST_ASSERT_EQ_UINT(0xdf, (unsigned char)scratch[46]);
	TEST_ASSERT_EQ_UINT(0xbf, (unsigned char)scratch[47]);
}

TEST_CASE(a_round_trip_through_both_directions_preserves_the_bytes)
{
	static const char original[] = "iTelex 118221 mom pls +?-/=";
	char encoded[SCRATCH_SIZE];
	char decoded[SCRATCH_SIZE];
	const size_t length = sizeof(original) - 1;
	int decoded_length;

	TEST_ASSERT(base64_encode(encoded, SCRATCH_SIZE, original, (int)length) != NULL);

	decoded_length = base64_decode(decoded, encoded, SCRATCH_SIZE);
	TEST_ASSERT_EQ_INT((int)length, decoded_length);
	TEST_ASSERT_EQ_MEM(original, decoded, length);
}

TEST_CASE(a_round_trip_preserves_non_text_bytes)
{
	char original[16];
	char encoded[SCRATCH_SIZE];
	char decoded[SCRATCH_SIZE];
	unsigned index;
	int decoded_length;

	/* Credentials are text, but the codec is also handed raw bytes; with a
	 * signed char these would encode wrongly, which is one reason the
	 * suites build with -funsigned-char as the firmware does. */
	for (index = 0; index < sizeof(original); index++) {
		original[index] = (char)(index * 17);
	}

	TEST_ASSERT(base64_encode(encoded, SCRATCH_SIZE, original, (int)sizeof(original)) != NULL);

	decoded_length = base64_decode(decoded, encoded, SCRATCH_SIZE);
	TEST_ASSERT_EQ_INT((int)sizeof(original), decoded_length);
	TEST_ASSERT_EQ_MEM(original, decoded, sizeof(original));
}

/**
 * \brief Runs the Base64 suite.
 * \return 0 when every case passed, 1 otherwise.
 */
int main(void)
{
	static const TTestCase cases[] = {
		TEST_ENTRY(encodes_the_rfc_4648_vectors),
		TEST_ENTRY(encodes_a_mail_login_the_way_a_server_expects_it),
		TEST_ENTRY(encoding_returns_a_pointer_to_the_callers_buffer),
		TEST_ENTRY(encoding_terminates_its_output),
		TEST_ENTRY(encoding_pads_to_a_multiple_of_four_characters),
		TEST_ENTRY(encoding_refuses_an_output_buffer_that_is_too_small),
		TEST_ENTRY(decodes_the_rfc_4648_vectors),
		TEST_ENTRY(decoding_handles_the_whole_alphabet),
		TEST_ENTRY(a_round_trip_through_both_directions_preserves_the_bytes),
		TEST_ENTRY(a_round_trip_preserves_non_text_bytes),
	};

	return test_run_suite("base64", cases, TEST_COUNT(cases));
}

/** @} */
