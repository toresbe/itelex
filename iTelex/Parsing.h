/*! \file Parsing.h \brief Parsing of operator-entered text */
/***************************************************************************
 *            Parsing.h
 *
 ****************************************************************************/
///	\ingroup software
///	\defgroup Parsing Parsing of operator-entered text
///	\code #include "Parsing.h" \endcode
//****************************************************************************/
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
//@{

#ifndef _PARSING_H_
#define _PARSING_H_

#include <inttypes.h>
#include <stdbool.h>

//! \brief Reads a signed decimal number.
//! \param[in,out] pp Cursor into the string. Advanced past what was read,
//!                   and left untouched when nothing could be read.
//! \param[out] val The number read. Untouched when the call fails.
//! \retval true At least one digit was read.
//! \retval false The cursor is not on a number.

extern bool ParseInt16(char **pp, int16_t *val);

//! \brief Reads an extension number and converts it to a TWI address.
//! \param[in,out] pp Cursor into the string. Advanced past what was read,
//!                   and left untouched when nothing could be read.
//! \param[out] address The TWI address for the number read.
//! \retval true An extension number was read.
//! \retval false The cursor is not on a valid extension number.

extern bool ParseExtensionAddress(char **pp, uint8_t *address);
	// ehem. ParseNstAddresse

//! \brief Advances the cursor past any blanks.
//! \param[in,out] pp Cursor into the string.

extern void ParseSkipSpace(char **pp);

//! \brief Picks the baud rate to use for an extension out of a table.
//! \param[in] extension TWI address of the extension to look up.
//! \param[in] aBaudTable The table to read, as a NUL-terminated string.
//! \retval >1 The baud rate for that extension.
//! \retval 1 The end of the table was reached without a match. Callers treat
//!           this as a failure too and fall back to 50 baud.
//! \retval <=0 The position in \a aBaudTable at which parsing failed,
//!             negated.

extern int16_t DetermineBaudRate(uint8_t extension, char *aBaudTable);
	// ehem. BaudrateErmitteln

#endif /* _PARSING_H_ */

//@}
