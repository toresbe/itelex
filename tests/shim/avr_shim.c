/**
 * \file
 * \brief Storage for the host build shims.
 *
 * \ingroup HOSTTESTS
 */

#include <avr/io.h>

/**
 * \brief The AVR status register stand-in.
 *
 * Starts out with interrupts enabled, which is the state the firmware runs in
 * once \c init() has finished, so a suite that never touches \ref SREG still
 * exercises the interesting path through a critical section.
 */
uint8_t avr_shim_sreg = _BV(SREG_I);
