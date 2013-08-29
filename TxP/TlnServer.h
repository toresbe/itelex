/*! \file TlnServer.h \brief TelexPhone Definitionen */
/***************************************************************************
 *            TlnServer.h
 *
 ****************************************************************************/
///	\ingroup software
///	\defgroup 
///	\code #include "TlnServer.h" \endcode
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
#ifndef _TLNSERVER_H_
	#define _TLNSERVER_H_
	
	#include "TxP.h"
	
	#ifdef ITELEX_TLNSERVER
	
	extern uint32_t TlnServSyncGeheimzahl;
	
	extern void txp_tlnserv_thread( void );
	extern void txp_tlnserv_init( void );
	
	extern void TlnServTlnbuchEintragGeaendert(TTlnDaten *Tln, int8_t VonServer);
	
	extern void TlnServDebugPrint();
	
	#endif //def ITELEX_TLNSERVER
	
#endif /* _TLNSERVER_H_ */

//@}
