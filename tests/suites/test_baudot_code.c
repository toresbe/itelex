/**
 * \file
 * \brief Host tests for the Baudot/ITA2 character conversion.
 *
 * The unit under test is \c dependencies/itelex-misc/Gemeinsam/BaudotCode.c,
 * which converts between the five-bit telegraph alphabet a teleprinter speaks
 * and the ASCII the rest of the firmware works in. It is the layer every
 * printed and received character passes through, and it is pure table lookup
 * plus shift-state bookkeeping, so it is the natural first thing to pin down.
 *
 * The expected values below come from the ITA2 alphabet as published, not from
 * reading the tables in the unit: a test that echoes the implementation cannot
 * catch a wrong table entry. The letter codes are the standard ones read with
 * bit 5 as the most significant bit (A = 11000 = 24, E = 10000 = 16), and the
 * figures case is the European CCITT-2 assignment.
 *
 * The suite builds without any \c TTYCODE_* define, which selects the same
 * default ITA2 tables the \c standard and \c light firmware builds use.
 *
 * \ingroup HOSTTESTS
 */

#include "test_framework.h"

#include "BaudotCode.h"

/** \addtogroup HOSTTESTS
 *  @{
 */

/**
 * \brief ITA2 letters-case code for each letter of the alphabet, \c A first.
 *
 * Read from the published alphabet, most significant bit first.
 */
static const uint8_t ita2_letter_codes[26] = {
	24, /* A = 11000 */
	19, /* B = 10011 */
	14, /* C = 01110 */
	18, /* D = 10010 */
	16, /* E = 10000 */
	22, /* F = 10110 */
	11, /* G = 01011 */
	5,  /* H = 00101 */
	12, /* I = 01100 */
	26, /* J = 11010 */
	30, /* K = 11110 */
	9,  /* L = 01001 */
	7,  /* M = 00111 */
	6,  /* N = 00110 */
	3,  /* O = 00011 */
	13, /* P = 01101 */
	29, /* Q = 11101 */
	10, /* R = 01010 */
	20, /* S = 10100 */
	1,  /* T = 00001 */
	28, /* U = 11100 */
	15, /* V = 01111 */
	25, /* W = 11001 */
	23, /* X = 10111 */
	21, /* Y = 10101 */
	17  /* Z = 10001 */
};

/**
 * \brief ITA2 figures-case code for each decimal digit, \c 0 first.
 */
static const uint8_t ita2_digit_codes[10] = {
	13, /* 0 */
	29, /* 1 */
	25, /* 2 */
	16, /* 3 */
	10, /* 4 */
	1,  /* 5 */
	21, /* 6 */
	28, /* 7 */
	12, /* 8 */
	3   /* 9 */
};

TEST_CASE(decodes_every_letter_of_the_alphabet)
{
	unsigned letter;

	for (letter = 0; letter < 26; letter++) {
		TBaudotMode mode = BaudotMode_BuchstabenEmpfangen;

		/* The firmware works in lower case; the alphabet is the same. */
		TEST_ASSERT_EQ_CHAR('a' + letter,
			CodeZuZeichen(ita2_letter_codes[letter], &mode));
	}
}

TEST_CASE(decodes_every_digit_in_figures_case)
{
	unsigned digit;

	for (digit = 0; digit < 10; digit++) {
		TBaudotMode mode = BaudotMode_ZiffernEmpfangen;

		TEST_ASSERT_EQ_CHAR('0' + digit,
			CodeZuZeichen(ita2_digit_codes[digit], &mode));
	}
}

TEST_CASE(decodes_the_control_codes_shared_by_both_cases)
{
	TBaudotMode mode = BaudotMode_BuchstabenEmpfangen;

	TEST_ASSERT_EQ_CHAR('\r', CodeZuZeichen(TtyCodeWR, &mode));
	TEST_ASSERT_EQ_CHAR('\n', CodeZuZeichen(TtyCodeZL, &mode));
	TEST_ASSERT_EQ_CHAR(' ', CodeZuZeichen(TtyCodeLeer, &mode));

	BaudotMode_SetZiffern(mode);
	TEST_ASSERT_EQ_CHAR('\r', CodeZuZeichen(TtyCodeWR, &mode));
	TEST_ASSERT_EQ_CHAR('\n', CodeZuZeichen(TtyCodeZL, &mode));
	TEST_ASSERT_EQ_CHAR(' ', CodeZuZeichen(TtyCodeLeer, &mode));
}

TEST_CASE(decodes_the_punctuation_of_the_figures_case)
{
	TBaudotMode mode = BaudotMode_ZiffernEmpfangen;

	TEST_ASSERT_EQ_CHAR(',', CodeZuZeichen(6, &mode));
	TEST_ASSERT_EQ_CHAR('.', CodeZuZeichen(TtyCodeZiPunkt, &mode));
	TEST_ASSERT_EQ_CHAR(')', CodeZuZeichen(9, &mode));
	TEST_ASSERT_EQ_CHAR('(', CodeZuZeichen(30, &mode));
	TEST_ASSERT_EQ_CHAR(':', CodeZuZeichen(TtyCodeZiDoppelpunkt, &mode));
	TEST_ASSERT_EQ_CHAR('=', CodeZuZeichen(TtyCodeZiIstgleich, &mode));
	TEST_ASSERT_EQ_CHAR('+', CodeZuZeichen(17, &mode));
	TEST_ASSERT_EQ_CHAR('?', CodeZuZeichen(19, &mode));
	TEST_ASSERT_EQ_CHAR('\'', CodeZuZeichen(20, &mode));
	TEST_ASSERT_EQ_CHAR('/', CodeZuZeichen(TtyCodeZiSchraegstrich, &mode));
	TEST_ASSERT_EQ_CHAR('-', CodeZuZeichen(24, &mode));
}

TEST_CASE(decodes_the_bell_and_who_are_you_codes_to_their_ascii_stand_ins)
{
	TBaudotMode mode = BaudotMode_ZiffernEmpfangen;

	/* Neither has an ASCII equivalent, so the unit substitutes control
	 * characters the rest of the firmware recognises. */
	TEST_ASSERT_EQ_CHAR(CodeChrKlingel, CodeZuZeichen(TtyCodeZiKlingel, &mode));
	TEST_ASSERT_EQ_CHAR(CodeChrWerDa, CodeZuZeichen(TtyCodeZiWerDa, &mode));
}

TEST_CASE(decodes_unassigned_figures_case_codes_as_hash)
{
	TBaudotMode mode = BaudotMode_ZiffernEmpfangen;

	/* Codes 5, 11 and 22 are national-use positions that the default ITA2
	 * table leaves unassigned. */
	TEST_ASSERT_EQ_CHAR('#', CodeZuZeichen(5, &mode));
	TEST_ASSERT_EQ_CHAR('#', CodeZuZeichen(11, &mode));
	TEST_ASSERT_EQ_CHAR('#', CodeZuZeichen(22, &mode));
}

TEST_CASE(shift_codes_change_the_case_without_producing_a_character)
{
	TBaudotMode mode = BaudotMode_BuchstabenEmpfangen;

	TEST_ASSERT_EQ_CHAR('\0', CodeZuZeichen(TtyCodeZiUm, &mode));
	TEST_ASSERT(BaudotMode_IstZiffern(mode));

	TEST_ASSERT_EQ_CHAR('\0', CodeZuZeichen(TtyCodeBuUm, &mode));
	TEST_ASSERT(BaudotMode_IstBuchstaben(mode));
}

TEST_CASE(the_same_code_decodes_differently_in_each_case)
{
	TBaudotMode mode = BaudotMode_BuchstabenEmpfangen;

	/* Code 1 is T in letters case and 5 in figures case: the shift state is
	 * the whole difference, which is why losing it garbles a message. */
	TEST_ASSERT_EQ_CHAR('t', CodeZuZeichen(1, &mode));

	(void)CodeZuZeichen(TtyCodeZiUm, &mode);
	TEST_ASSERT_EQ_CHAR('5', CodeZuZeichen(1, &mode));
}

TEST_CASE(decoding_a_character_marks_the_mode_as_receiving)
{
	TBaudotMode mode = BaudotMode_BuchstabenGesendet;

	(void)CodeZuZeichen(ita2_letter_codes[0], &mode);

	/* The send flag has to drop, so that the next character sent out is
	 * preceded by a fresh shift code rather than assuming the far end still
	 * shares our idea of the case. */
	TEST_ASSERT(BaudotMode_IstEmpfangen(mode));
}

TEST_CASE(encodes_letters_and_digits_in_the_matching_case)
{
	unsigned index;

	for (index = 0; index < 26; index++) {
		TEST_ASSERT_EQ_UINT(ita2_letter_codes[index],
			ZeichenZuCode((char)('a' + index), BaudotMode_BuchstabenEmpfangen));
	}

	for (index = 0; index < 10; index++) {
		TEST_ASSERT_EQ_UINT(ita2_digit_codes[index],
			ZeichenZuCode((char)('0' + index), BaudotMode_ZiffernBitMaske));
	}
}

TEST_CASE(encodes_upper_case_letters_as_their_lower_case_codes)
{
	unsigned index;

	for (index = 0; index < 26; index++) {
		TEST_ASSERT_EQ_UINT(ita2_letter_codes[index],
			ZeichenZuCode((char)('A' + index), BaudotMode_BuchstabenEmpfangen));
	}
}

TEST_CASE(encoding_reports_255_for_a_character_absent_from_the_case)
{
	/* A digit has no letters-case code, and asking for one has to fail
	 * rather than return a plausible wrong code. */
	TEST_ASSERT_EQ_UINT(255, ZeichenZuCode('5', BaudotMode_BuchstabenEmpfangen));
	TEST_ASSERT_EQ_UINT(255, ZeichenZuCode('q', BaudotMode_ZiffernBitMaske));

	/* And nothing in ITA2 encodes these at all. */
	TEST_ASSERT_EQ_UINT(255, ZeichenZuCode('%', BaudotMode_BuchstabenEmpfangen));
	TEST_ASSERT_EQ_UINT(255, ZeichenZuCode('%', BaudotMode_ZiffernBitMaske));
}

TEST_CASE(encoding_a_run_of_letters_emits_no_redundant_shift)
{
	TBaudotMode mode = BaudotMode_BuchstabenGesendet;
	uint8_t first;
	uint8_t second;

	TEST_ASSERT(ZeichenZuCode2('a', &mode, &first, &second));
	TEST_ASSERT_EQ_UINT(ita2_letter_codes[0], first);
	TEST_ASSERT_EQ_UINT(255, second);

	TEST_ASSERT(ZeichenZuCode2('b', &mode, &first, &second));
	TEST_ASSERT_EQ_UINT(ita2_letter_codes[1], first);
	TEST_ASSERT_EQ_UINT(255, second);

	TEST_ASSERT(BaudotMode_IstBuchstaben(mode));
	TEST_ASSERT(BaudotMode_IstSenden(mode));
}

TEST_CASE(encoding_inserts_a_shift_when_the_case_changes)
{
	TBaudotMode mode = BaudotMode_BuchstabenGesendet;
	uint8_t first;
	uint8_t second;

	/* Going from letters to a digit costs a figures shift ahead of the
	 * character itself. */
	TEST_ASSERT(ZeichenZuCode2('7', &mode, &first, &second));
	TEST_ASSERT_EQ_UINT(TtyCodeZiUm, first);
	TEST_ASSERT_EQ_UINT(ita2_digit_codes[7], second);
	TEST_ASSERT(BaudotMode_IstZiffern(mode));

	/* A second digit rides the shift already sent. */
	TEST_ASSERT(ZeichenZuCode2('8', &mode, &first, &second));
	TEST_ASSERT_EQ_UINT(ita2_digit_codes[8], first);
	TEST_ASSERT_EQ_UINT(255, second);

	/* And coming back costs a letters shift. */
	TEST_ASSERT(ZeichenZuCode2('a', &mode, &first, &second));
	TEST_ASSERT_EQ_UINT(TtyCodeBuUm, first);
	TEST_ASSERT_EQ_UINT(ita2_letter_codes[0], second);
	TEST_ASSERT(BaudotMode_IstBuchstaben(mode));
}

TEST_CASE(encoding_after_receiving_always_sends_a_shift_first)
{
	TBaudotMode mode = BaudotMode_BuchstabenEmpfangen;
	uint8_t first;
	uint8_t second;

	/* The far end's shift state is unknown after a receive, so even a
	 * letter in the case we believe is current gets an explicit shift. */
	TEST_ASSERT(ZeichenZuCode2('a', &mode, &first, &second));
	TEST_ASSERT_EQ_UINT(TtyCodeBuUm, first);
	TEST_ASSERT_EQ_UINT(ita2_letter_codes[0], second);
	TEST_ASSERT(BaudotMode_IstSenden(mode));
}

TEST_CASE(encoding_passes_explicit_shift_requests_through)
{
	TBaudotMode mode = BaudotMode_BuchstabenGesendet;
	uint8_t first;
	uint8_t second;

	TEST_ASSERT(ZeichenZuCode2(CodeChrZiUm, &mode, &first, &second));
	TEST_ASSERT_EQ_UINT(TtyCodeZiUm, first);
	TEST_ASSERT_EQ_UINT(255, second);
	TEST_ASSERT(BaudotMode_IstZiffern(mode));

	TEST_ASSERT(ZeichenZuCode2(CodeChrBuUm, &mode, &first, &second));
	TEST_ASSERT_EQ_UINT(TtyCodeBuUm, first);
	TEST_ASSERT_EQ_UINT(255, second);
	TEST_ASSERT(BaudotMode_IstBuchstaben(mode));
}

TEST_CASE(encoding_who_are_you_always_shifts_to_figures_first)
{
	TBaudotMode mode = BaudotMode_ZiffernGesendet;
	uint8_t first;
	uint8_t second;

	/* Even with figures case already sent, the who-are-you enquiry is
	 * preceded by its own shift: the unit excludes it from the
	 * shift-elision shortcut so a mis-tracked case cannot turn the enquiry
	 * into a printable character. */
	TEST_ASSERT(ZeichenZuCode2(CodeChrWerDa, &mode, &first, &second));
	TEST_ASSERT_EQ_UINT(TtyCodeZiUm, first);
	TEST_ASSERT_EQ_UINT(TtyCodeZiWerDa, second);
}

TEST_CASE(encoding_reports_failure_for_a_character_outside_the_alphabet)
{
	TBaudotMode mode = BaudotMode_BuchstabenGesendet;
	uint8_t first = 42;
	uint8_t second = 42;

	TEST_ASSERT_FALSE(ZeichenZuCode2('%', &mode, &first, &second));
}

TEST_CASE(a_round_trip_through_both_conversions_preserves_the_text)
{
	static const char message[] = "ruf 12345 = mom pls\r\n";
	TBaudotMode send_mode = BaudotMode_BuchstabenEmpfangen;
	TBaudotMode receive_mode = BaudotMode_BuchstabenEmpfangen;
	char received[sizeof(message)];
	size_t received_length = 0;
	size_t index;

	/* Encode the message one character at a time, feeding every code
	 * produced straight back into the decoder, the way a real connection
	 * does. What comes out the far end has to be what went in. */
	for (index = 0; message[index] != '\0'; index++) {
		uint8_t first;
		uint8_t second;
		char decoded;

		TEST_ASSERT(ZeichenZuCode2(message[index], &send_mode, &first, &second));

		decoded = CodeZuZeichen(first, &receive_mode);
		if (decoded != '\0') {
			TEST_ASSERT(received_length < sizeof(received) - 1);
			received[received_length++] = decoded;
		}

		if (second != 255) {
			decoded = CodeZuZeichen(second, &receive_mode);
			if (decoded != '\0') {
				TEST_ASSERT(received_length < sizeof(received) - 1);
				received[received_length++] = decoded;
			}
		}
	}

	received[received_length] = '\0';
	TEST_ASSERT_EQ_STR(message, received);
}

/**
 * \brief Runs the Baudot conversion suite.
 * \return 0 when every case passed, 1 otherwise.
 */
int main(void)
{
	static const TTestCase cases[] = {
		TEST_ENTRY(decodes_every_letter_of_the_alphabet),
		TEST_ENTRY(decodes_every_digit_in_figures_case),
		TEST_ENTRY(decodes_the_control_codes_shared_by_both_cases),
		TEST_ENTRY(decodes_the_punctuation_of_the_figures_case),
		TEST_ENTRY(decodes_the_bell_and_who_are_you_codes_to_their_ascii_stand_ins),
		TEST_ENTRY(decodes_unassigned_figures_case_codes_as_hash),
		TEST_ENTRY(shift_codes_change_the_case_without_producing_a_character),
		TEST_ENTRY(the_same_code_decodes_differently_in_each_case),
		TEST_ENTRY(decoding_a_character_marks_the_mode_as_receiving),
		TEST_ENTRY(encodes_letters_and_digits_in_the_matching_case),
		TEST_ENTRY(encodes_upper_case_letters_as_their_lower_case_codes),
		TEST_ENTRY(encoding_reports_255_for_a_character_absent_from_the_case),
		TEST_ENTRY(encoding_a_run_of_letters_emits_no_redundant_shift),
		TEST_ENTRY(encoding_inserts_a_shift_when_the_case_changes),
		TEST_ENTRY(encoding_after_receiving_always_sends_a_shift_first),
		TEST_ENTRY(encoding_passes_explicit_shift_requests_through),
		TEST_ENTRY(encoding_who_are_you_always_shifts_to_figures_first),
		TEST_ENTRY(encoding_reports_failure_for_a_character_outside_the_alphabet),
		TEST_ENTRY(a_round_trip_through_both_conversions_preserves_the_text),
	};

	return test_run_suite("BaudotCode", cases, TEST_COUNT(cases));
}

/** @} */
