/**
 * \file
 * \brief Host tests for the operator-text parsers.
 *
 * The unit under test is \c iTelex/Parsing.c, extracted from \c iTelex.c
 * because it is the one part of that file that touches no firmware state.
 *
 * The cases below matter most for \ref DetermineBaudRate, which reads the
 * baud-rate table an operator types into the web interface — a small string
 * format ("70-79:75,*:50") with no other check on it anywhere in the
 * firmware. Several of its answers are surprising, and are pinned here
 * deliberately rather than left to be rediscovered.
 *
 * \ingroup HOSTTESTS
 */

#include <string.h>

#include "test_framework.h"

#include "iTelex/Parsing.h"

/** \addtogroup HOSTTESTS
 *  @{
 */

/*
 * WahlZuAdresse() lives in BusKomm.c, which will not compile on a host: the
 * same file carries the TWI interrupt handler and reaches for the ATmega's
 * TWI registers. This stand-in does two jobs. It records what it was handed,
 * which is what the ParseExtensionAddress cases below assert on, because
 * choosing the number and the digit count is all that function decides. And
 * it reproduces the mapping documented in BusKomm.c, so that the
 * DetermineBaudRate cases can be written with the addresses the firmware
 * really uses.
 *
 * Reproducing it makes this a copy that could drift from the original. The
 * mapping is the i-Telex bus addressing scheme and does not move; a change to
 * it would be a change to the bus, not a refactoring. Linking the real
 * function instead would mean shimming the whole TWI peripheral.
 */

static uint8_t stub_last_number;
static uint8_t stub_last_digits;
static unsigned stub_calls;

uint8_t WahlZuAdresse(uint8_t Wahl, uint8_t AnzZiffern);

uint8_t WahlZuAdresse(uint8_t Wahl, uint8_t AnzZiffern)
{
	stub_last_number = Wahl;
	stub_last_digits = AnzZiffern;
	stub_calls++;

	if (AnzZiffern == 1 && Wahl <= 9) {
		return (uint8_t)(((Wahl == 0) ? 110 : (100 + Wahl)) << 1);
	}
	if (AnzZiffern == 2 && Wahl <= 99) {
		return (uint8_t)(((Wahl == 0) ? 100 : Wahl) << 1);
	}
	return 0xff; /* BusAdrUngueltig */
}

/** \brief Runs ParseInt16 over a writable copy of \a text. */
static bool parse_int16(const char *text, int16_t *value, size_t *consumed)
{
	static char buffer[80];
	char *cursor = buffer;
	bool result;

	strncpy(buffer, text, sizeof(buffer) - 1);
	buffer[sizeof(buffer) - 1] = '\0';

	result = ParseInt16(&cursor, value);
	*consumed = (size_t)(cursor - buffer);
	return result;
}

/** \brief Runs ParseExtensionAddress over a writable copy of \a text. */
static bool parse_extension(const char *text, uint8_t *address, size_t *consumed)
{
	static char buffer[80];
	char *cursor = buffer;
	bool result;

	strncpy(buffer, text, sizeof(buffer) - 1);
	buffer[sizeof(buffer) - 1] = '\0';

	stub_calls = 0;
	stub_last_number = 0;
	stub_last_digits = 0;

	result = ParseExtensionAddress(&cursor, address);
	*consumed = (size_t)(cursor - buffer);
	return result;
}

/** \brief Runs DetermineBaudRate over a writable copy of \a table. */
static int16_t determine_baud_rate(uint8_t extension, const char *table)
{
	static char buffer[80];

	strncpy(buffer, table, sizeof(buffer) - 1);
	buffer[sizeof(buffer) - 1] = '\0';

	return DetermineBaudRate(extension, buffer);
}

TEST_CASE(parse_int16_reads_a_plain_number)
{
	int16_t value = 0;
	size_t consumed = 0;

	TEST_ASSERT(parse_int16("123", &value, &consumed));
	TEST_ASSERT_EQ_INT(123, value);
	TEST_ASSERT_EQ_UINT(3, consumed);

	TEST_ASSERT(parse_int16("0", &value, &consumed));
	TEST_ASSERT_EQ_INT(0, value);
	TEST_ASSERT_EQ_UINT(1, consumed);

	TEST_ASSERT(parse_int16("32767", &value, &consumed));
	TEST_ASSERT_EQ_INT(32767, value);
	TEST_ASSERT_EQ_UINT(5, consumed);
}

TEST_CASE(parse_int16_reads_a_sign)
{
	int16_t value = 0;
	size_t consumed = 0;

	TEST_ASSERT(parse_int16("-45", &value, &consumed));
	TEST_ASSERT_EQ_INT(-45, value);
	TEST_ASSERT_EQ_UINT(3, consumed);

	TEST_ASSERT(parse_int16("+7", &value, &consumed));
	TEST_ASSERT_EQ_INT(7, value);
	TEST_ASSERT_EQ_UINT(2, consumed);
}

TEST_CASE(parse_int16_ignores_leading_zeros)
{
	int16_t value = 0;
	size_t consumed = 0;

	/* The digit count still grows, which is what makes "007" three digits
	 * and therefore not an extension number. See the case below. */
	TEST_ASSERT(parse_int16("007", &value, &consumed));
	TEST_ASSERT_EQ_INT(7, value);
	TEST_ASSERT_EQ_UINT(3, consumed);
}

TEST_CASE(parse_int16_stops_at_the_first_character_it_cannot_use)
{
	int16_t value = 0;
	size_t consumed = 0;

	TEST_ASSERT(parse_int16("12abc", &value, &consumed));
	TEST_ASSERT_EQ_INT(12, value);
	TEST_ASSERT_EQ_UINT(2, consumed);

	/* A sign after a digit ends the number rather than starting a new one,
	 * which is what lets DetermineBaudRate read "70-79" as a range. */
	TEST_ASSERT(parse_int16("5-3", &value, &consumed));
	TEST_ASSERT_EQ_INT(5, value);
	TEST_ASSERT_EQ_UINT(1, consumed);
}

TEST_CASE(parse_int16_leaves_the_cursor_alone_when_it_fails)
{
	static const char *const rejected[] = {
		"",     /* nothing at all */
		"abc",  /* not a number */
		" 12",  /* no blanks are skipped; that is ParseSkipSpace's job */
		"-",    /* a sign on its own is not a number */
		"--5",  /* two signs */
		"+-5",
	};
	unsigned index;

	for (index = 0; index < TEST_COUNT(rejected); index++) {
		int16_t value = -9999;
		size_t consumed = 99;

		TEST_ASSERT_FALSE(parse_int16(rejected[index], &value, &consumed));
		TEST_ASSERT_EQ_UINT(0, consumed);
		/* The out parameter is left untouched on failure, so a caller
		 * that ignores the return value keeps whatever it had. */
		TEST_ASSERT_EQ_INT(-9999, value);
	}
}

TEST_CASE(skip_space_advances_over_blanks_only)
{
	static char buffer[] = "   \tx";
	char *cursor = buffer;

	ParseSkipSpace(&cursor);
	TEST_ASSERT_EQ_UINT(3, (unsigned)(cursor - buffer));

	/* A tab is not a blank here, so the cursor stops on it. */
	TEST_ASSERT_EQ_CHAR('\t', *cursor);

	ParseSkipSpace(&cursor);
	TEST_ASSERT_EQ_UINT(3, (unsigned)(cursor - buffer));
}

TEST_CASE(extension_address_converts_one_and_two_digit_numbers)
{
	uint8_t address = 0;
	size_t consumed = 0;

	TEST_ASSERT(parse_extension("45", &address, &consumed));
	TEST_ASSERT_EQ_UINT(2, consumed);
	TEST_ASSERT_EQ_UINT(1, stub_calls);
	TEST_ASSERT_EQ_UINT(45, stub_last_number);
	TEST_ASSERT_EQ_UINT(2, stub_last_digits);

	TEST_ASSERT(parse_extension("5", &address, &consumed));
	TEST_ASSERT_EQ_UINT(1, consumed);
	TEST_ASSERT_EQ_UINT(5, stub_last_number);
	TEST_ASSERT_EQ_UINT(1, stub_last_digits);

	/* The digit count is what separates "05" from "5": one is extension 5
	 * on the bus, the other is the single-digit dial code. */
	TEST_ASSERT(parse_extension("05", &address, &consumed));
	TEST_ASSERT_EQ_UINT(5, stub_last_number);
	TEST_ASSERT_EQ_UINT(2, stub_last_digits);
}

TEST_CASE(extension_address_reads_a_lone_dash_as_zero)
{
	uint8_t address = 99;
	size_t consumed = 0;

	/* A dash yields address 0 and consumes exactly one character, without
	 * consulting WahlZuAdresse at all. */
	TEST_ASSERT(parse_extension("-", &address, &consumed));
	TEST_ASSERT_EQ_UINT(0, address);
	TEST_ASSERT_EQ_UINT(1, consumed);
	TEST_ASSERT_EQ_UINT(0, stub_calls);

	/* So "-5" is a dash followed by a leftover 5, not minus five. */
	TEST_ASSERT(parse_extension("-5", &address, &consumed));
	TEST_ASSERT_EQ_UINT(0, address);
	TEST_ASSERT_EQ_UINT(1, consumed);
}

TEST_CASE(extension_address_rejects_more_than_two_digits)
{
	uint8_t address = 42;
	size_t consumed = 99;

	TEST_ASSERT_FALSE(parse_extension("100", &address, &consumed));
	TEST_ASSERT_EQ_UINT(0, consumed);
	TEST_ASSERT_EQ_UINT(42, address);

	TEST_ASSERT_FALSE(parse_extension("007", &address, &consumed));
	TEST_ASSERT_EQ_UINT(0, consumed);

	TEST_ASSERT_FALSE(parse_extension("abc", &address, &consumed));
	TEST_ASSERT_EQ_UINT(0, consumed);
}

TEST_CASE(extension_address_accepts_a_leading_plus_as_a_second_digit)
{
	uint8_t address = 0;
	size_t consumed = 0;

	/* Almost certainly not intended: the two-character "+4" passes the
	 * "at most two digits" test, so it reaches WahlZuAdresse as the
	 * two-digit number 4 — the same as "04", not the same as "4".
	 * Recorded because an operator who types it gets a valid table. */
	TEST_ASSERT(parse_extension("+4", &address, &consumed));
	TEST_ASSERT_EQ_UINT(2, consumed);
	TEST_ASSERT_EQ_UINT(4, stub_last_number);
	TEST_ASSERT_EQ_UINT(2, stub_last_digits);
}

TEST_CASE(baud_rate_table_matches_a_range_and_a_wildcard)
{
	/* Two-digit extension n sits at bus address 2n, so 70-79 covers
	 * addresses 140 to 158. */
	TEST_ASSERT_EQ_INT(75, determine_baud_rate(150, "70-79:75,*:50"));
	TEST_ASSERT_EQ_INT(75, determine_baud_rate(140, "70-79:75,*:50"));
	TEST_ASSERT_EQ_INT(75, determine_baud_rate(158, "70-79:75,*:50"));
	TEST_ASSERT_EQ_INT(50, determine_baud_rate(160, "70-79:75,*:50"));

	/* A single number rather than a range. */
	TEST_ASSERT_EQ_INT(100, determine_baud_rate(38, "19:100,*:50"));
	TEST_ASSERT_EQ_INT(50, determine_baud_rate(40, "19:100,*:50"));
}

TEST_CASE(baud_rate_table_allows_blanks_around_every_token)
{
	TEST_ASSERT_EQ_INT(75, determine_baud_rate(150, " 70 - 79 : 75 , * : 50 "));
	TEST_ASSERT_EQ_INT(50, determine_baud_rate(160, " 70 - 79 : 75 , * : 50 "));
}

TEST_CASE(baud_rate_table_returns_one_when_nothing_matched)
{
	/* 1 is not a baud rate. It means the table was read to its end without
	 * a match, and SeriellUmsetzInit treats everything <= 1 as a failure
	 * and falls back to 50 baud. */
	TEST_ASSERT_EQ_INT(1, determine_baud_rate(160, "70-79:75"));
}

TEST_CASE(the_wildcard_never_matches_extension_zero)
{
	/* This is what makes validation possible: the CGI handler that saves a
	 * new table looks it up for extension 0, which cannot match anything,
	 * so the whole string has to be read before the answer comes back.
	 * A return of 1 there means "well formed", not "no entry". */
	TEST_ASSERT_EQ_INT(1, determine_baud_rate(0, "*:50"));
	TEST_ASSERT_EQ_INT(1, determine_baud_rate(0, "70-79:75,*:50"));
}

TEST_CASE(baud_rate_table_reports_where_it_gave_up)
{
	/* A negative answer is the offset of the offending character, negated. */
	TEST_ASSERT_EQ_INT(-5, determine_baud_rate(0, "70-79;75"));   /* ';' at 5 */
	TEST_ASSERT_EQ_INT(-6, determine_baud_rate(0, "70-79:"));     /* no rate */
	TEST_ASSERT_EQ_INT(-8, determine_baud_rate(0, "70-79:75;*:50"));

	/* An error at the very first character negates to zero, which is
	 * equally "not a baud rate" to both callers but carries no position. */
	TEST_ASSERT_EQ_INT(0, determine_baud_rate(0, "xx:50"));
	TEST_ASSERT_EQ_INT(0, determine_baud_rate(0, ""));
}

TEST_CASE(a_match_stops_the_scan_before_any_later_error)
{
	/* Validation only reaches the junk if nothing matches first, which is
	 * the other half of why the CGI handler looks up extension 0. Asked
	 * for a real extension, the same malformed table looks fine. */
	TEST_ASSERT_EQ_INT(75, determine_baud_rate(150, "70-79:75;*:50"));
	TEST_ASSERT_EQ_INT(-8, determine_baud_rate(0, "70-79:75;*:50"));
}

TEST_CASE(a_reversed_range_matches_nothing)
{
	/* "79-70" is read without complaint and simply never matches, so an
	 * operator who inverts a range gets the fallback rate and no error. */
	TEST_ASSERT_EQ_INT(50, determine_baud_rate(150, "79-70:75,*:50"));
	TEST_ASSERT_EQ_INT(1, determine_baud_rate(0, "79-70:75"));
}

/**
 * \brief Runs the operator-text parsing suite.
 * \return 0 when every case passed, 1 otherwise.
 */
int main(void)
{
	static const TTestCase cases[] = {
		TEST_ENTRY(parse_int16_reads_a_plain_number),
		TEST_ENTRY(parse_int16_reads_a_sign),
		TEST_ENTRY(parse_int16_ignores_leading_zeros),
		TEST_ENTRY(parse_int16_stops_at_the_first_character_it_cannot_use),
		TEST_ENTRY(parse_int16_leaves_the_cursor_alone_when_it_fails),
		TEST_ENTRY(skip_space_advances_over_blanks_only),
		TEST_ENTRY(extension_address_converts_one_and_two_digit_numbers),
		TEST_ENTRY(extension_address_reads_a_lone_dash_as_zero),
		TEST_ENTRY(extension_address_rejects_more_than_two_digits),
		TEST_ENTRY(extension_address_accepts_a_leading_plus_as_a_second_digit),
		TEST_ENTRY(baud_rate_table_matches_a_range_and_a_wildcard),
		TEST_ENTRY(baud_rate_table_allows_blanks_around_every_token),
		TEST_ENTRY(baud_rate_table_returns_one_when_nothing_matched),
		TEST_ENTRY(the_wildcard_never_matches_extension_zero),
		TEST_ENTRY(baud_rate_table_reports_where_it_gave_up),
		TEST_ENTRY(a_match_stops_the_scan_before_any_later_error),
		TEST_ENTRY(a_reversed_range_matches_nothing),
	};

	return test_run_suite("parsing", cases, TEST_COUNT(cases));
}

/** @} */
