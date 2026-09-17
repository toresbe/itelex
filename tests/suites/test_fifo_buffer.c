/**
 * \file
 * \brief Host tests for the shared FIFO ring buffer.
 *
 * The unit under test is \c dependencies/itelex-misc/Gemeinsam/FifoPuffer.c,
 * the ring buffer every character crossing a serial line or the backplane
 * passes through. Its producer usually runs in an interrupt while its consumer
 * runs in the main loop, so two things matter and are pinned here: that the
 * indices wrap without losing or duplicating a byte, and that the accessors
 * leave the interrupt-enable bit as they found it.
 *
 * The buffer reports itself full while two slots are still free — a deliberate
 * margin, documented in the unit — and refuses a store only when one slot is
 * left, so its usable capacity is \c MaxPuffer-1. The cases below are written
 * in terms of \c MaxPuffer rather than the value it currently has, so they
 * keep their meaning if \c FIFOPUFFER_MAX is ever set.
 *
 * \ingroup HOSTTESTS
 */

#include "test_framework.h"

#include "FifoPuffer.h"

#include <avr/io.h>

/** \addtogroup HOSTTESTS
 *  @{
 */

/** \brief Bytes a buffer accepts before a store is refused. */
#define USABLE_CAPACITY (MaxPuffer - 1)

/**
 * \brief Fills a freshly initialised buffer with \p count ascending bytes.
 * \param buffer Buffer to fill.
 * \param count  Number of bytes to store; must not exceed \ref USABLE_CAPACITY.
 */
static void fill_with_ascending_bytes(TPuffer *buffer, unsigned count)
{
	unsigned index;

	for (index = 0; index < count; index++) {
		TEST_ASSERT(PufferSpeich(buffer, (uint8_t)index));
	}
}

TEST_CASE(a_fresh_buffer_is_empty)
{
	TPuffer buffer;

	PufferInit(&buffer);

	TEST_ASSERT(PufferLeer(&buffer));
	TEST_ASSERT_FALSE(PufferVoll(&buffer));
	TEST_ASSERT_EQ_UINT(0, PufferAnzahl(&buffer));
}

TEST_CASE(a_stored_byte_comes_back_out)
{
	TPuffer buffer;

	PufferInit(&buffer);

	TEST_ASSERT(PufferSpeich(&buffer, 42));
	TEST_ASSERT_FALSE(PufferLeer(&buffer));
	TEST_ASSERT_EQ_UINT(1, PufferAnzahl(&buffer));
	TEST_ASSERT_EQ_UINT(42, PufferAusg(&buffer));
	TEST_ASSERT(PufferLeer(&buffer));
}

TEST_CASE(bytes_come_back_in_the_order_they_went_in)
{
	TPuffer buffer;
	unsigned index;

	PufferInit(&buffer);
	fill_with_ascending_bytes(&buffer, 10);

	for (index = 0; index < 10; index++) {
		TEST_ASSERT_EQ_UINT(index, PufferAusg(&buffer));
	}

	TEST_ASSERT(PufferLeer(&buffer));
}

TEST_CASE(peeking_does_not_consume_the_byte)
{
	TPuffer buffer;

	PufferInit(&buffer);
	TEST_ASSERT(PufferSpeich(&buffer, 7));

	TEST_ASSERT_EQ_UINT(7, PufferZeig(&buffer));
	TEST_ASSERT_EQ_UINT(7, PufferZeig(&buffer));
	TEST_ASSERT_EQ_UINT(1, PufferAnzahl(&buffer));
	TEST_ASSERT_EQ_UINT(7, PufferAusg(&buffer));
	TEST_ASSERT(PufferLeer(&buffer));
}

TEST_CASE(the_count_tracks_stores_and_fetches)
{
	TPuffer buffer;
	unsigned index;

	PufferInit(&buffer);

	for (index = 1; index <= 5; index++) {
		TEST_ASSERT(PufferSpeich(&buffer, (uint8_t)index));
		TEST_ASSERT_EQ_UINT(index, PufferAnzahl(&buffer));
	}

	for (index = 4; index > 0; index--) {
		(void)PufferAusg(&buffer);
		TEST_ASSERT_EQ_UINT(index, PufferAnzahl(&buffer));
	}
}

TEST_CASE(a_store_is_refused_once_one_slot_is_left)
{
	TPuffer buffer;

	PufferInit(&buffer);
	fill_with_ascending_bytes(&buffer, USABLE_CAPACITY);

	TEST_ASSERT_EQ_UINT(USABLE_CAPACITY, PufferAnzahl(&buffer));
	TEST_ASSERT_FALSE(PufferSpeich(&buffer, 0xff));

	/* A refused store must not disturb what is already queued. */
	TEST_ASSERT_EQ_UINT(USABLE_CAPACITY, PufferAnzahl(&buffer));
	TEST_ASSERT_EQ_UINT(0, PufferZeig(&buffer));
}

TEST_CASE(the_full_report_keeps_a_two_slot_margin)
{
	TPuffer buffer;

	PufferInit(&buffer);
	fill_with_ascending_bytes(&buffer, MaxPuffer - 3);

	/* Three slots free: still room by either measure. */
	TEST_ASSERT_FALSE(PufferVoll(&buffer));

	TEST_ASSERT(PufferSpeich(&buffer, 0));

	/* Two slots free: reported full, although a store still succeeds. The
	 * margin is what lets a caller stop offering work before the producer
	 * runs out of room mid-character. */
	TEST_ASSERT(PufferVoll(&buffer));
	TEST_ASSERT(PufferSpeich(&buffer, 0));
	TEST_ASSERT(PufferVoll(&buffer));
}

TEST_CASE(the_indices_wrap_without_losing_a_byte)
{
	TPuffer buffer;
	unsigned round;

	PufferInit(&buffer);

	/* Push several buffers' worth of traffic through in small batches, so
	 * that both indices wrap repeatedly and out of step with each other. */
	for (round = 0; round < MaxPuffer * 3; round++) {
		uint8_t expected = (uint8_t)round;
		uint8_t next = (uint8_t)(round + 1);

		TEST_ASSERT(PufferSpeich(&buffer, expected));
		TEST_ASSERT(PufferSpeich(&buffer, next));
		TEST_ASSERT_EQ_UINT(2, PufferAnzahl(&buffer));
		TEST_ASSERT_EQ_UINT(expected, PufferAusg(&buffer));
		TEST_ASSERT_EQ_UINT(next, PufferAusg(&buffer));
		TEST_ASSERT(PufferLeer(&buffer));
	}
}

TEST_CASE(the_count_is_right_when_the_stored_data_straddles_the_wrap)
{
	TPuffer buffer;
	unsigned index;

	PufferInit(&buffer);

	/* Advance both indices most of the way round, then leave a run of
	 * bytes spanning the end of the array. */
	fill_with_ascending_bytes(&buffer, MaxPuffer - 5);
	for (index = 0; index < MaxPuffer - 5; index++) {
		(void)PufferAusg(&buffer);
	}

	for (index = 0; index < 8; index++) {
		TEST_ASSERT(PufferSpeich(&buffer, (uint8_t)(100 + index)));
	}

	TEST_ASSERT_EQ_UINT(8, PufferAnzahl(&buffer));

	for (index = 0; index < 8; index++) {
		TEST_ASSERT_EQ_UINT(100 + index, PufferAusg(&buffer));
	}

	TEST_ASSERT(PufferLeer(&buffer));
	TEST_ASSERT_EQ_UINT(0, PufferAnzahl(&buffer));
}

TEST_CASE(emptying_and_refilling_leaves_no_stale_bytes)
{
	TPuffer buffer;

	PufferInit(&buffer);
	fill_with_ascending_bytes(&buffer, 5);

	while (!PufferLeer(&buffer)) {
		(void)PufferAusg(&buffer);
	}

	TEST_ASSERT(PufferSpeich(&buffer, 0xa5));
	TEST_ASSERT_EQ_UINT(1, PufferAnzahl(&buffer));
	TEST_ASSERT_EQ_UINT(0xa5, PufferAusg(&buffer));
}

TEST_CASE(a_store_restores_the_interrupt_enable_bit)
{
	TPuffer buffer;

	PufferInit(&buffer);

	/* The store runs with interrupts on, as it does in the main loop. */
	SREG |= (uint8_t)_BV(SREG_I);
	TEST_ASSERT(PufferSpeich(&buffer, 1));
	TEST_ASSERT((SREG & _BV(SREG_I)) != 0);

	/* And with interrupts off, as it does inside a handler: the accessor
	 * must not switch them back on behind the handler's back. */
	SREG &= (uint8_t)~_BV(SREG_I);
	TEST_ASSERT(PufferSpeich(&buffer, 2));
	TEST_ASSERT((SREG & _BV(SREG_I)) == 0);

	SREG |= (uint8_t)_BV(SREG_I);
}

TEST_CASE(a_fetch_restores_the_interrupt_enable_bit)
{
	TPuffer buffer;

	PufferInit(&buffer);
	TEST_ASSERT(PufferSpeich(&buffer, 1));
	TEST_ASSERT(PufferSpeich(&buffer, 2));

	SREG |= (uint8_t)_BV(SREG_I);
	TEST_ASSERT_EQ_UINT(1, PufferAusg(&buffer));
	TEST_ASSERT((SREG & _BV(SREG_I)) != 0);

	SREG &= (uint8_t)~_BV(SREG_I);
	TEST_ASSERT_EQ_UINT(2, PufferAusg(&buffer));
	TEST_ASSERT((SREG & _BV(SREG_I)) == 0);

	SREG |= (uint8_t)_BV(SREG_I);
}

TEST_CASE(two_buffers_do_not_share_state)
{
	TPuffer first;
	TPuffer second;

	PufferInit(&first);
	PufferInit(&second);

	TEST_ASSERT(PufferSpeich(&first, 11));

	TEST_ASSERT(PufferLeer(&second));
	TEST_ASSERT_EQ_UINT(0, PufferAnzahl(&second));
	TEST_ASSERT_EQ_UINT(1, PufferAnzahl(&first));
}

/**
 * \brief Runs the FIFO ring buffer suite.
 * \return 0 when every case passed, 1 otherwise.
 */
int main(void)
{
	static const TTestCase cases[] = {
		TEST_ENTRY(a_fresh_buffer_is_empty),
		TEST_ENTRY(a_stored_byte_comes_back_out),
		TEST_ENTRY(bytes_come_back_in_the_order_they_went_in),
		TEST_ENTRY(peeking_does_not_consume_the_byte),
		TEST_ENTRY(the_count_tracks_stores_and_fetches),
		TEST_ENTRY(a_store_is_refused_once_one_slot_is_left),
		TEST_ENTRY(the_full_report_keeps_a_two_slot_margin),
		TEST_ENTRY(the_indices_wrap_without_losing_a_byte),
		TEST_ENTRY(the_count_is_right_when_the_stored_data_straddles_the_wrap),
		TEST_ENTRY(emptying_and_refilling_leaves_no_stale_bytes),
		TEST_ENTRY(a_store_restores_the_interrupt_enable_bit),
		TEST_ENTRY(a_fetch_restores_the_interrupt_enable_bit),
		TEST_ENTRY(two_buffers_do_not_share_state),
	};

	return test_run_suite("FifoPuffer", cases, TEST_COUNT(cases));
}

/** @} */
