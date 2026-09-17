/**
 * \file
 * \brief Host build shim for <avr/io.h>.
 *
 * The suites do not touch peripheral registers — code that does is not pure
 * logic and does not belong in a host test — but they do exercise code that
 * saves and restores the AVR status register around a critical section. This
 * shim therefore models \c SREG as an ordinary byte of storage, so a suite can
 * assert that a function left the interrupt-enable bit as it found it.
 *
 * \ingroup HOSTTESTS
 */

#ifndef TEST_SHIM_AVR_IO_H
#define TEST_SHIM_AVR_IO_H

#include <stdint.h>

/** \addtogroup HOSTTESTS
 *  @{
 */

/**
 * \brief Stand-in for the AVR status register.
 *
 * Only the interrupt-enable bit \ref SREG_I is meaningful here; nothing in the
 * shim sets the flag bits an ALU operation would.
 */
extern uint8_t avr_shim_sreg;

/** \brief Names the status register the way AVR code spells it. */
#define SREG avr_shim_sreg

/** \brief Bit position of the global interrupt-enable flag in \ref SREG. */
#define SREG_I 7

/** \brief Bit value from a bit position, as avr-libc defines it. */
#ifndef _BV
#define _BV(bit) (1 << (bit))
#endif

/** @} */

#endif /* TEST_SHIM_AVR_IO_H */
