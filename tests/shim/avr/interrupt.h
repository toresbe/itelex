/**
 * \file
 * \brief Host build shim for <avr/interrupt.h>.
 *
 * \c cli() and \c sei() clear and set the interrupt-enable bit of the
 * \ref avr_shim_sreg stand-in rather than emitting instructions, which keeps
 * the save/restore pattern used by the firmware's critical sections
 * observable from a test.
 *
 * \ingroup HOSTTESTS
 */

#ifndef TEST_SHIM_AVR_INTERRUPT_H
#define TEST_SHIM_AVR_INTERRUPT_H

#include <avr/io.h>

/** \addtogroup HOSTTESTS
 *  @{
 */

/** \brief Clears the interrupt-enable bit of the \ref SREG stand-in. */
#define cli() ((void)(SREG &= (uint8_t)~_BV(SREG_I)))

/** \brief Sets the interrupt-enable bit of the \ref SREG stand-in. */
#define sei() ((void)(SREG |= (uint8_t)_BV(SREG_I)))

/** @} */

#endif /* TEST_SHIM_AVR_INTERRUPT_H */
