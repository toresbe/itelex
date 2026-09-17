/*! \file Parsing.c \brief Parsing of operator-entered text */
/***************************************************************************
 *            Parsing.c
 *
 ****************************************************************************/
///	\ingroup Parsing
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

#include <inttypes.h>
#include <stdbool.h>

#include "BusKomm.h"

#include "Parsing.h"


bool ParseInt16(char **pp, int16_t *val)
	{
	char *p;
	int8_t Sign;		// ehem. Vorz
	int16_t Value;		// ehem. Wert
	bool Res;

	p = *pp;
	Res = false;
	Value = 0;
	Sign = 0;
	while (true)
		{
		if (*p == '-')
			if (Sign == 0) // not set yet
				Sign = -1;
			else // already had a digit or a sign
				break;
		else if (*p == '+')
			if (Sign == 0) // not set yet
				Sign = 1;
			else // already had a digit or a sign
				break;
		else if (*p >= '0' && *p <= '9')
			{
			Value = 10 * Value + (*p) - '0';
			if (Sign == 0)
				Sign = 1;
			Res = true;
			}
		else 
			break;
		p++;
		}
		
	if (!Res)
		return false;
	
	*pp = p;	
	*val = Sign * Value;
	return true;
	}
			
	
bool ParseExtensionAddress(char **pp, uint8_t *address) // ehem. ParseNstAddresse
	{
	char *p2;
	int16_t nr;
	
	p2 = *pp;
	if (p2[0] == '-')
		{
		*address = 0;
		(*pp)++;
		return true;
		}
		
	if (!ParseInt16(&p2, &nr))
		return false;
	
	if (nr < 0 || nr > 99 || p2 - (*pp) > 2)
						//   ^^^^^^^^^^^^^^ more than 2 digits
		return false;
	
	*address = WahlZuAdresse(nr, p2 - (*pp));
	*pp = p2;
	return true;
	}


void ParseSkipSpace(char **pp)
	{
	while (**pp == ' ')
		(*pp)++;
	}


// The contract is documented in Parsing.h. The table is the one the operator
// edits as BaudrateListe, for example "70-78:75,19:100,*:50": extensions 70 to
// 78 run at 75 baud, number 19 at 100 baud, everything else at 50 baud.
//
// Note that the scan returns as soon as an entry matches, so anything after
// the matching entry is never checked. That is why the CGI handler validates
// a new table by looking it up for extension 0, which no entry can match.

int16_t DetermineBaudRate(uint8_t extension, char *aBaudTable) // ehem. BaudrateErmitteln
	{
	char *p;
	bool InRange;		// ehem. BereichJa
	uint8_t address2;	// ehem. nst2
	int16_t baud;
	
	p = aBaudTable;
	while (true)
		{
		ParseSkipSpace(&p);
		if (*p == '*')
			{
			InRange = (extension != 0);
			p++;
			}
		else 
			{
			if (!ParseExtensionAddress(&p, &address2))
				return -(p - aBaudTable);

			ParseSkipSpace(&p);
			if (*p == '-')
				{
				InRange = (extension >= address2);
				p++;
				ParseSkipSpace(&p);
				if (!ParseExtensionAddress(&p, &address2))
					return -(p - aBaudTable);
					
				InRange &= (extension <= address2);
				}
			else
				InRange = (extension == address2);
			}
		
		ParseSkipSpace(&p);
		
		if (*p != ':')
			return -(p - aBaudTable);

		p++;
		ParseSkipSpace(&p);
		
		if (!ParseInt16(&p, &baud))
			return -(p - aBaudTable);
		
		if (InRange)
			return baud;
		
		ParseSkipSpace(&p);
		
		if (*p == '\0')
			return 1; // reached the end of the string cleanly
		
		if (*p != ',')
			// only a comma is allowed as the list separator
			return -(p - aBaudTable);
			
		p++;
		}
	} // DetermineBaudRate(uint8_t extension, char *aBaudTable)

//@}
