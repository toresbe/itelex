/*! \file eMail.h \brief iTelex Definitionen */
/***************************************************************************
 *            eMail.h
 *
 ****************************************************************************/
///	\ingroup software
///	\defgroup 
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

#ifdef ITELEX_EMAIL
 
#include "CgiFormTools.h"
#include "iTelex.h"
#include "TlnBuch.h"
#include "BusKomm.h"
#include "TxP2-Defs.h"

extern void itelex_email_init();

extern void POP3DatenVerarbeiten();
			
extern void SMTPDatenVerarbeiten();

extern bool SMTPOeffnen(char *EmfaengerName);

extern void SMTPSchliessen();

extern void POP3Einleiten();

extern void POP3Abbrechen();


#endif //def ITELEX_EMAIL
