/**
 * \file
 * \brief Host tests for the framing of the data received on the i-Telex socket.
 *
 * The unit under test is \ref ScanFrame in \c iTelex/FrameScanner.h, carved
 * out of \c ITelexOderAsciiEmpfangVerarbeiten in \c iTelex.c. That function
 * reads a TCP stream from another i-Telex, a Centralex relay or a plain
 * telnet client, and nothing else in the firmware checks what arrives: a
 * peer that sends a short, a malformed or an unknown block gets whatever the
 * decoder happens to do with it.
 *
 * These cases are written to describe the behaviour the firmware has, not the
 * behaviour it ought to have. Three of them pin down answers that look like
 * defects and are recorded here rather than corrected, because correcting
 * them changes what the firmware does on the wire:
 *
 * - A block whose payload has not all arrived is not held back for the rest
 *   of the stream. The cursor advances past the end of the received data, and
 *   the caller then discards the receive buffer. Only #ITELEXC_BAUDOT_DATA,
 *   which reports \ref TFrame::Complete to a caller that waits, escapes this.
 * - A disconnect block cut down to what arrived wraps to a payload of 255
 *   bytes when not even its length byte has been received.
 * - On a connection known to carry ASCII, the command codes that share a
 *   value with a control code the ASCII protocol carries cannot be received
 *   at all.
 *
 * \ingroup HOSTTESTS
 */

#include <string.h>

#include "test_framework.h"

#include "iTelex/FrameScanner.h"

/** \addtogroup HOSTTESTS
 *  @{
 */

/*
 * The firmware declares its receive buffer four bytes longer than it ever
 * fills, which is what keeps the decoder's reads past the received data
 * inside the array. The fixture reproduces that, and fills the slack with a
 * value no case sends, so that a read past the data shows up as this value
 * rather than as a stale byte from an earlier case.
 */
enum { SCAN_SLACK = 4, SLACK_FILL = 0x5a };

static char scan_buffer[64 + SCAN_SLACK];

/** \brief Runs \ref ScanFrame over \a bytes with the fixture's buffer. */
static TFrame scan_at(const char *bytes, size_t used, uint16_t cursor,
	TFrameProtocol protocol)
{
	TFrame frame;

	memset(scan_buffer, SLACK_FILL, sizeof scan_buffer);
	memcpy(scan_buffer, bytes, used);
	ScanFrame(scan_buffer, (uint16_t)used, cursor, protocol, &frame);

	return frame;
}

/** \brief Runs \ref ScanFrame over the first frame of \a bytes. */
static TFrame scan(const char *bytes, size_t used, TFrameProtocol protocol)
{
	return scan_at(bytes, used, 0, protocol);
}


/* ------------------------------------------------------------------ text */

TEST_CASE(printable_characters_are_single_byte_text_frames)
{
	static const char stream[] = "Ab9 ~";
	size_t at;

	for (at = 0; at < sizeof stream - 1; at++) {
		const TFrame frame = scan_at(stream, sizeof stream - 1, (uint16_t)at,
			FrameProtocolAscii);

		TEST_ASSERT_EQ_UINT(FrameText, frame.Kind);
		TEST_ASSERT_EQ_UINT(1, frame.Length);
		TEST_ASSERT(frame.Complete);
		TEST_ASSERT_EQ_CHAR(stream[at], frame.Text);
	}
}

TEST_CASE(carriage_return_and_line_feed_are_text_whatever_the_protocol_is)
{
	static const char stream[] = { '\r', '\n' };
	static const TFrameProtocol protocols[] = {
		FrameProtocolUnknown, FrameProtocolITelex, FrameProtocolAscii,
	};
	size_t p;

	for (p = 0; p < TEST_COUNT(protocols); p++) {
		TEST_ASSERT_EQ_UINT(FrameText, scan_at(stream, 2, 0, protocols[p]).Kind);
		TEST_ASSERT_EQ_UINT(FrameText, scan_at(stream, 2, 1, protocols[p]).Kind);
	}
}

TEST_CASE(text_settles_the_protocol_on_ascii)
{
	static const char stream[] = { 'A' };

	TEST_ASSERT_EQ_UINT(FrameProtocolAscii,
		scan(stream, 1, FrameProtocolUnknown).Protocol);
	/* Even on a connection that had been carrying blocks. ID#246 ID#344 */
	TEST_ASSERT_EQ_UINT(FrameProtocolAscii,
		scan(stream, 1, FrameProtocolITelex).Protocol);
}

TEST_CASE(the_percent_sign_arrives_as_a_bell)
{
	static const char stream[] = { AsciiProtZeichenKlingel };
	const TFrame frame = scan(stream, 1, FrameProtocolAscii);

	TEST_ASSERT_EQ_UINT(FrameText, frame.Kind);
	TEST_ASSERT_EQ_CHAR(CodeChrKlingel, frame.Text);
}

TEST_CASE(the_at_sign_arrives_as_a_who_are_you)
{
	static const char stream[] = { AsciiProtZeichenWerDa };
	const TFrame frame = scan(stream, 1, FrameProtocolAscii);

	TEST_ASSERT_EQ_UINT(FrameText, frame.Kind);
	TEST_ASSERT_EQ_CHAR(CodeChrWerDa, frame.Text);
}

TEST_CASE(a_control_code_is_text_only_once_the_socket_is_known_to_carry_ascii)
{
	/* A backspace followed by a printable character. */
	static const char stream[] = { '\010', 'A' };

	TEST_ASSERT_EQ_UINT(FrameText, scan(stream, 2, FrameProtocolAscii).Kind);

	/*
	 * The same two bytes on a connection that has not settled on ASCII are
	 * read as the start of a block: 0x08 is #ITELEXC_SELBSTANRUF.
	 */
	TEST_ASSERT_EQ_UINT(FrameSelfCall, scan(stream, 2, FrameProtocolUnknown).Kind);
	TEST_ASSERT_EQ_UINT(FrameSelfCall, scan(stream, 2, FrameProtocolITelex).Kind);
}

TEST_CASE(a_control_code_is_text_when_it_is_the_last_byte_received)
{
	static const char stream[] = { 'A', '\033' };

	TEST_ASSERT_EQ_UINT(FrameText,
		scan_at(stream, 2, 1, FrameProtocolAscii).Kind);

	/* With one more byte behind it that is not text, it is a block again. */
	{
		static const char longer[] = { 'A', '\033', '\002' };

		TEST_ASSERT_EQ_UINT(FrameUnknown,
			scan_at(longer, 3, 1, FrameProtocolAscii).Kind);
	}
}

TEST_CASE(ascii_mode_hides_the_self_call_and_remote_config_commands)
{
	/*
	 * 0x05, 0x07, 0x08 and 0x09 are control codes the ASCII protocol carries
	 * and at the same time command codes. On an ASCII connection the text
	 * reading wins, so those commands cannot be received at all. Recorded,
	 * not corrected.
	 */
	static const char version[] = { ITELEXC_VERSION, ' ' };
	static const char self_call[] = { ITELEXC_SELBSTANRUF, ' ' };
	static const char remote_config[] = { ITELEXC_FERNKONFIG, ' ' };

	TEST_ASSERT_EQ_UINT(FrameText, scan(version, 2, FrameProtocolAscii).Kind);
	TEST_ASSERT_EQ_UINT(FrameText, scan(self_call, 2, FrameProtocolAscii).Kind);
	TEST_ASSERT_EQ_UINT(FrameText, scan(remote_config, 2, FrameProtocolAscii).Kind);

	TEST_ASSERT_EQ_UINT(FrameVersion, scan(version, 2, FrameProtocolITelex).Kind);
	TEST_ASSERT_EQ_UINT(FrameSelfCall, scan(self_call, 2, FrameProtocolITelex).Kind);
	TEST_ASSERT_EQ_UINT(FrameRemoteConfig, scan(remote_config, 2, FrameProtocolITelex).Kind);
}

TEST_CASE(high_bytes_are_text_in_ascii_mode_but_a_block_otherwise)
{
	static const char umlaut[] = { (char)0xc3, (char)0xa4 };

	TEST_ASSERT_EQ_UINT(FrameText, scan(umlaut, 2, FrameProtocolAscii).Kind);
	TEST_ASSERT_EQ_UINT(FrameUnknown, scan(umlaut, 2, FrameProtocolITelex).Kind);
}

TEST_CASE(the_delete_code_is_never_text)
{
	/* 0x7f is one past the printable range and not a code ASCII carries. */
	static const char stream[] = { (char)0x7f, (char)0x7f };

	TEST_ASSERT_EQ_UINT(FrameUnknown, scan(stream, 2, FrameProtocolAscii).Kind);
	TEST_ASSERT_EQ_UINT(1, scan(stream, 2, FrameProtocolAscii).Length);
}


/* ---------------------------------------------------------------- filler */

TEST_CASE(a_null_byte_is_a_one_byte_filler_and_settles_nothing)
{
	static const char stream[] = { ITELEXC_NULL, ITELEXC_NULL };
	const TFrame frame = scan(stream, 2, FrameProtocolUnknown);

	TEST_ASSERT_EQ_UINT(FrameFiller, frame.Kind);
	TEST_ASSERT_EQ_UINT(1, frame.Length);
	TEST_ASSERT_EQ_UINT(0, frame.PayloadLength);
	TEST_ASSERT(frame.Complete);
	TEST_ASSERT_EQ_UINT(FrameProtocolUnknown, frame.Protocol);
}


/* ---------------------------------------------------------------- blocks */

TEST_CASE(an_extension_block_reports_its_payload)
{
	static const char stream[] = { ITELEXC_DURCHWAHL, 1, 11 };
	const TFrame frame = scan(stream, 3, FrameProtocolUnknown);

	TEST_ASSERT_EQ_UINT(FrameExtension, frame.Kind);
	TEST_ASSERT_EQ_UINT(1, frame.PayloadLength);
	TEST_ASSERT_EQ_UINT(3, frame.Length);
	TEST_ASSERT(frame.Complete);
}

TEST_CASE(a_baudot_block_reports_its_payload)
{
	static const char stream[] = { ITELEXC_BAUDOT_DATA, 3, 0x1f, 0x02, 0x08 };
	const TFrame frame = scan(stream, 5, FrameProtocolUnknown);

	TEST_ASSERT_EQ_UINT(FrameBaudotData, frame.Kind);
	TEST_ASSERT_EQ_UINT(3, frame.PayloadLength);
	TEST_ASSERT_EQ_UINT(5, frame.Length);
	TEST_ASSERT(frame.Complete);
}

TEST_CASE(a_disconnect_block_reports_its_reason)
{
	static const char stop[] = { ITELEXC_STOP, 3, 'o', 'c', 'c' };
	static const char end[] = { ITELEXC_ENDE, 0 };

	TEST_ASSERT_EQ_UINT(FrameDisconnect, scan(stop, 5, FrameProtocolITelex).Kind);
	TEST_ASSERT_EQ_UINT(3, scan(stop, 5, FrameProtocolITelex).PayloadLength);
	TEST_ASSERT_EQ_UINT(FrameDisconnect, scan(end, 2, FrameProtocolITelex).Kind);
	TEST_ASSERT_EQ_UINT(0, scan(end, 2, FrameProtocolITelex).PayloadLength);
}

TEST_CASE(an_acknowledge_block_reports_its_payload)
{
	static const char stream[] = { ITELEXC_QUITT, 1, 42 };
	const TFrame frame = scan(stream, 3, FrameProtocolITelex);

	TEST_ASSERT_EQ_UINT(FrameAcknowledge, frame.Kind);
	TEST_ASSERT_EQ_UINT(1, frame.PayloadLength);
	TEST_ASSERT_EQ_UINT(3, frame.Length);
}

TEST_CASE(a_version_block_reports_its_payload)
{
	static const char stream[] = { ITELEXC_VERSION, 3, 1, 'x', '\0' };
	const TFrame frame = scan(stream, 5, FrameProtocolITelex);

	TEST_ASSERT_EQ_UINT(FrameVersion, frame.Kind);
	TEST_ASSERT_EQ_UINT(3, frame.PayloadLength);
	TEST_ASSERT_EQ_UINT(5, frame.Length);
}

TEST_CASE(a_self_call_block_reports_its_payload)
{
	static const char stream[] = { ITELEXC_SELBSTANRUF, 2, 0x12, 0x34 };
	const TFrame frame = scan(stream, 4, FrameProtocolITelex);

	TEST_ASSERT_EQ_UINT(FrameSelfCall, frame.Kind);
	TEST_ASSERT_EQ_UINT(2, frame.PayloadLength);
	TEST_ASSERT_EQ_UINT(4, frame.Length);
}

TEST_CASE(a_remote_config_block_leaves_the_protocol_state_alone)
{
	static const char stream[] = { ITELEXC_FERNKONFIG, 3, 0x34, 0x12, 0x11 };

	TEST_ASSERT_EQ_UINT(FrameRemoteConfig, scan(stream, 5, FrameProtocolUnknown).Kind);
	/* The one block that does not settle the protocol. */
	TEST_ASSERT_EQ_UINT(FrameProtocolUnknown,
		scan(stream, 5, FrameProtocolUnknown).Protocol);
	TEST_ASSERT_EQ_UINT(FrameProtocolAscii,
		scan(stream, 5, FrameProtocolAscii).Protocol);
}

TEST_CASE(every_block_but_remote_config_settles_the_protocol_on_itelex)
{
	static const char codes[] = {
		ITELEXC_DURCHWAHL, ITELEXC_BAUDOT_DATA, ITELEXC_ENDE, ITELEXC_STOP,
		ITELEXC_QUITT, ITELEXC_VERSION, ITELEXC_SELBSTANRUF,
	};
	size_t n;

	for (n = 0; n < sizeof codes; n++) {
		const char stream[] = { codes[n], 0 };

		TEST_ASSERT_EQ_UINT(FrameProtocolITelex,
			scan(stream, 2, FrameProtocolUnknown).Protocol);
	}
}

TEST_CASE(a_zero_length_block_is_two_bytes_long)
{
	static const char stream[] = { ITELEXC_ENDE, 0 };
	const TFrame frame = scan(stream, 2, FrameProtocolITelex);

	TEST_ASSERT_EQ_UINT(2, frame.Length);
	TEST_ASSERT_EQ_UINT(0, frame.PayloadLength);
	TEST_ASSERT(frame.Complete);
}


/* --------------------------------------------------------- fragmentation */

TEST_CASE(a_block_whose_payload_has_not_all_arrived_is_incomplete)
{
	/* Declares four payload bytes, two of which have been received. */
	static const char stream[] = { ITELEXC_BAUDOT_DATA, 4, 0x1f, 0x02 };
	const TFrame frame = scan(stream, 4, FrameProtocolITelex);

	TEST_ASSERT_EQ_UINT(FrameBaudotData, frame.Kind);
	TEST_ASSERT_FALSE(frame.Complete);
	/* The declared length is reported unchanged, not the part that arrived. */
	TEST_ASSERT_EQ_UINT(4, frame.PayloadLength);
	TEST_ASSERT_EQ_UINT(6, frame.Length);
}

TEST_CASE(an_incomplete_block_still_reports_the_length_it_would_consume)
{
	/*
	 * Every block but #ITELEXC_BAUDOT_DATA is acted on by the decoder whether
	 * or not it is complete, and the length reported here sends the cursor
	 * past the received data, which makes the caller discard the buffer. A
	 * fragmented extension or acknowledge block is therefore lost rather than
	 * waited for. Recorded, not corrected.
	 */
	static const char stream[] = { ITELEXC_DURCHWAHL, 1 };
	const TFrame frame = scan(stream, 2, FrameProtocolITelex);

	TEST_ASSERT_EQ_UINT(FrameExtension, frame.Kind);
	TEST_ASSERT_FALSE(frame.Complete);
	TEST_ASSERT_EQ_UINT(3, frame.Length);
	TEST_ASSERT(frame.Length > 2);
}

TEST_CASE(a_disconnect_block_is_cut_down_to_the_payload_that_arrived)
{
	/* Declares ten payload bytes, three of which have been received. */
	static const char stream[] = { ITELEXC_STOP, 10, 'o', 'c', 'c' };
	const TFrame frame = scan(stream, 5, FrameProtocolITelex);

	TEST_ASSERT_EQ_UINT(FrameDisconnect, frame.Kind);
	TEST_ASSERT_FALSE(frame.Complete);
	TEST_ASSERT_EQ_UINT(3, frame.PayloadLength);
	TEST_ASSERT_EQ_UINT(5, frame.Length);
}

TEST_CASE(a_disconnect_command_byte_on_its_own_wraps_its_length_to_255)
{
	/*
	 * With only the command byte received there is nothing to cut the length
	 * down to, and the subtraction ends one short of zero. The decoder then
	 * reads 255 bytes of "reason" from beyond the data and advances the
	 * cursor by 257. Recorded, not corrected.
	 */
	static const char stream[] = { 'A', ITELEXC_ENDE };
	const TFrame frame = scan_at(stream, 2, 1, FrameProtocolITelex);

	TEST_ASSERT_EQ_UINT(FrameDisconnect, frame.Kind);
	TEST_ASSERT_FALSE(frame.Complete);
	TEST_ASSERT_EQ_UINT(255, frame.PayloadLength);
	TEST_ASSERT_EQ_UINT(257, frame.Length);
}

TEST_CASE(a_block_code_as_the_last_byte_received_reads_the_length_from_beyond_the_data)
{
	/*
	 * The length byte is read whether or not it has arrived; the firmware's
	 * four bytes of slack past the receive buffer are what keeps that read
	 * inside the array.
	 */
	static const char stream[] = { ITELEXC_QUITT };
	const TFrame frame = scan(stream, 1, FrameProtocolITelex);

	TEST_ASSERT_EQ_UINT(FrameAcknowledge, frame.Kind);
	TEST_ASSERT_FALSE(frame.Complete);
	TEST_ASSERT_EQ_UINT(SLACK_FILL, frame.PayloadLength);
	TEST_ASSERT_EQ_UINT(2 + SLACK_FILL, frame.Length);
}


/* ----------------------------------------------------------- concatenation */

TEST_CASE(concatenated_frames_are_scanned_one_after_another)
{
	static const char stream[] = {
		ITELEXC_VERSION, 1, 1,
		ITELEXC_DURCHWAHL, 1, 0,
		ITELEXC_BAUDOT_DATA, 2, 0x1f, 0x02,
		'H', 'i',
		ITELEXC_NULL,
		ITELEXC_ENDE, 0,
	};
	static const TFrameKind expected_kinds[] = {
		FrameVersion, FrameExtension, FrameBaudotData,
		FrameText, FrameText, FrameFiller, FrameDisconnect,
	};
	static const uint16_t expected_lengths[] = { 3, 3, 4, 1, 1, 1, 2 };

	TFrameProtocol protocol = FrameProtocolUnknown;
	uint16_t cursor = 0;
	size_t n;

	for (n = 0; n < TEST_COUNT(expected_kinds); n++) {
		const TFrame frame = scan_at(stream, sizeof stream, cursor, protocol);

		if (frame.Kind != expected_kinds[n]) {
			TEST_FAIL("frame %zu at offset %u: expected kind %u, got %u",
				n, (unsigned)cursor, (unsigned)expected_kinds[n],
				(unsigned)frame.Kind);
		}
		TEST_ASSERT_EQ_UINT(expected_lengths[n], frame.Length);
		TEST_ASSERT(frame.Complete);

		protocol = frame.Protocol;
		cursor = (uint16_t)(cursor + frame.Length);
	}

	TEST_ASSERT_EQ_UINT(sizeof stream, cursor);
}

TEST_CASE(text_in_the_middle_of_a_stream_switches_the_protocol_to_ascii)
{
	static const char stream[] = { ITELEXC_VERSION, 1, 1, 'H' };

	TEST_ASSERT_EQ_UINT(FrameProtocolITelex,
		scan_at(stream, 4, 0, FrameProtocolUnknown).Protocol);
	TEST_ASSERT_EQ_UINT(FrameProtocolAscii,
		scan_at(stream, 4, 3, FrameProtocolITelex).Protocol);
}


/* --------------------------------------------------------------- unknown */

TEST_CASE(an_unknown_command_consumes_its_block_in_the_itelex_protocol)
{
	/*
	 * ID#245 ID#313 ID#346. 0x0b, because the two unassigned codes just
	 * either side of it are '\n' and '\r' and are read as text.
	 */
	static const char stream[] = { 0x0b, 3, 1, 2, 3 };
	const TFrame frame = scan(stream, 5, FrameProtocolITelex);

	TEST_ASSERT_EQ_UINT(FrameUnknown, frame.Kind);
	TEST_ASSERT_EQ_UINT(5, frame.Length);
	TEST_ASSERT_EQ_UINT(FrameProtocolITelex, frame.Protocol);
}

TEST_CASE(an_unknown_code_is_skipped_on_its_own_until_the_protocol_is_settled)
{
	static const char stream[] = { 0x0b, 3, 1, 2, 3 };

	TEST_ASSERT_EQ_UINT(1, scan(stream, 5, FrameProtocolUnknown).Length);
	TEST_ASSERT_EQ_UINT(1, scan(stream, 5, FrameProtocolAscii).Length);
	TEST_ASSERT_EQ_UINT(FrameProtocolUnknown,
		scan(stream, 5, FrameProtocolUnknown).Protocol);
}


int main(void)
{
	static const TTestCase cases[] = {
		TEST_ENTRY(printable_characters_are_single_byte_text_frames),
		TEST_ENTRY(carriage_return_and_line_feed_are_text_whatever_the_protocol_is),
		TEST_ENTRY(text_settles_the_protocol_on_ascii),
		TEST_ENTRY(the_percent_sign_arrives_as_a_bell),
		TEST_ENTRY(the_at_sign_arrives_as_a_who_are_you),
		TEST_ENTRY(a_control_code_is_text_only_once_the_socket_is_known_to_carry_ascii),
		TEST_ENTRY(a_control_code_is_text_when_it_is_the_last_byte_received),
		TEST_ENTRY(ascii_mode_hides_the_self_call_and_remote_config_commands),
		TEST_ENTRY(high_bytes_are_text_in_ascii_mode_but_a_block_otherwise),
		TEST_ENTRY(the_delete_code_is_never_text),
		TEST_ENTRY(a_null_byte_is_a_one_byte_filler_and_settles_nothing),
		TEST_ENTRY(an_extension_block_reports_its_payload),
		TEST_ENTRY(a_baudot_block_reports_its_payload),
		TEST_ENTRY(a_disconnect_block_reports_its_reason),
		TEST_ENTRY(an_acknowledge_block_reports_its_payload),
		TEST_ENTRY(a_version_block_reports_its_payload),
		TEST_ENTRY(a_self_call_block_reports_its_payload),
		TEST_ENTRY(a_remote_config_block_leaves_the_protocol_state_alone),
		TEST_ENTRY(every_block_but_remote_config_settles_the_protocol_on_itelex),
		TEST_ENTRY(a_zero_length_block_is_two_bytes_long),
		TEST_ENTRY(a_block_whose_payload_has_not_all_arrived_is_incomplete),
		TEST_ENTRY(an_incomplete_block_still_reports_the_length_it_would_consume),
		TEST_ENTRY(a_disconnect_block_is_cut_down_to_the_payload_that_arrived),
		TEST_ENTRY(a_disconnect_command_byte_on_its_own_wraps_its_length_to_255),
		TEST_ENTRY(a_block_code_as_the_last_byte_received_reads_the_length_from_beyond_the_data),
		TEST_ENTRY(concatenated_frames_are_scanned_one_after_another),
		TEST_ENTRY(text_in_the_middle_of_a_stream_switches_the_protocol_to_ascii),
		TEST_ENTRY(an_unknown_command_consumes_its_block_in_the_itelex_protocol),
		TEST_ENTRY(an_unknown_code_is_skipped_on_its_own_until_the_protocol_is_settled),
	};

	return test_run_suite("FrameScanner", cases, TEST_COUNT(cases));
}

/** @} */
