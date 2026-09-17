/*! \file Parsing.c \brief Parsing of operator-entered text */
/***************************************************************************
 *            Parsing.c
 *
 ****************************************************************************/
///	\ingroup software
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
	int8_t Vorz;
	int16_t Wert;
	bool Res;

	p = *pp;
	Res = false;
	Wert = 0;
	Vorz = 0;
	while (true)
		{
		if (*p == '-')
			if (Vorz == 0) // noch nicht gesetzt
				Vorz = -1;
			else // schon eine Ziffer oder ein Vorzeichen gehabt
				break;
		else if (*p == '+')
			if (Vorz == 0) // noch nicht gesetzt
				Vorz = 1;
			else // schon eine Ziffer oder ein Vorzeichen gehabt
				break;
		else if (*p >= '0' && *p <= '9')
			{
			Wert = 10 * Wert + (*p) - '0';
			if (Vorz == 0)
				Vorz = 1;
			Res = true;
			}
		else 
			break;
		p++;
		}
		
	if (!Res)
		return false;
	
	*pp = p;	
	*val = Vorz * Wert;
	return true;
	}
			
	
bool ParseNstAddresse(char **pp, uint8_t *nst)
	{
	char *p2;
	int16_t nr;
	
	p2 = *pp;
	if (p2[0] == '-')
		{
		*nst = 0;
		(*pp)++;
		return true;
		}
		
	if (!ParseInt16(&p2, &nr))
		return false;
	
	if (nr < 0 || nr > 99 || p2 - (*pp) > 2)
						//   ^^^^^^^^^^^^^^ mehr als 2 Ziffern
		return false;
	
	*nst = WahlZuAdresse(nr, p2 - (*pp));
	*pp = p2;
	return true;
	}


void ParseSkipSpace(char **pp)
	{
	while (**pp == ' ')
		(*pp)++;
	}


//! Sucht aus der Tabelle die zu verwendende Baudrate aus.
//--------------------------------------------------------
//! Verwendet die globale Tabelle #BaudrateListe
//! \retval >0 Baudrate
//! \retval <=0 Position des Fehlers in der Zeichenkette #BaudrateListe
	
int16_t BaudrateErmitteln(uint8_t nst, char *aBaudTab)
	{
	char *p;
	bool BereichJa;
	uint8_t nst2;
	int16_t baud;
	
	p = aBaudTab;
	while (true)
		{
		ParseSkipSpace(&p);
		if (*p == '*')
			{
			BereichJa = (nst != 0);
			p++;
			}
		else 
			{
			if (!ParseNstAddresse(&p, &nst2))
				return -(p - aBaudTab);

			ParseSkipSpace(&p);
			if (*p == '-')
				{
				BereichJa = (nst >= nst2);
				p++;
				ParseSkipSpace(&p);
				if (!ParseNstAddresse(&p, &nst2))
					return -(p - aBaudTab);
					
				BereichJa &= (nst <= nst2);
				}
			else
				BereichJa = (nst == nst2);
			}
		
		ParseSkipSpace(&p);
		
		if (*p != ':')
			return -(p - aBaudTab);

		p++;
		ParseSkipSpace(&p);
		
		if (!ParseInt16(&p, &baud))
			return -(p - aBaudTab);
		
		if (BereichJa)
			return baud;
		
		ParseSkipSpace(&p);
		
		if (*p == '\0')
			return 1; // Ende des String korrekt erreicht
		
		if (*p != ',')
			// nur Komma als Aufzählungs-Trenner erlaubt
			return -(p - aBaudTab);
			
		p++;
		}
	} // BaudrateErmitteln(uint8_t nst, char *aBaudTab)

//@}
