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

//! \name Scanning text that an operator typed
//! These functions read a caller-owned, NUL-terminated string through a
//! cursor and never touch firmware state, so they can be exercised by the
//! host unit tests in tests/suites/test_parsing.c.
//! \{

extern bool ParseInt16(char **pp, int16_t *val);
extern bool ParseNstAddresse(char **pp, uint8_t *nst);
extern void ParseSkipSpace(char **pp);
extern int16_t BaudrateErmitteln(uint8_t nst, char *aBaudTab);

//! \}

#endif /* _PARSING_H_ */

//@}
