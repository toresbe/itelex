/*! \file TxP.h \brief TelexPhone Definitionen */
/***************************************************************************
 *            TxP.h
 *
 ****************************************************************************/
///	\ingroup software
///	\defgroup 
///	\code #include "txp.h" \endcode
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
#ifndef _TXP_H_
	#define _TXP_H_
	
	#include "config.h"

	#ifdef TELEXPHONE
	
	#include <avr/pgmspace.h>  
	#include "system/shell/shell.h"
	#include "config.h"

	#include "Defports.h"
	
	DEFPORTINPULL(Taste, B, 3);
	
	typedef enum { 
		NichtGedr, //!< nicht gedrückt.
		Kurz, //!< kurz gedrückt ( < 0,8 Sekunden)
		Lang  //!< lang gedrückt ( > 0,8 Sekunden)
		} TTastendruck; //!< Art des Tastendrucks

	//! Der TCP-Port für die TelexPhone-Kommunikation
	#define TXP_PORT 134
	
	extern void txp_init( void );
	extern void txp_thread( void );
	extern TTastendruck Tastendruck;
	extern bool WarteTaste();
	
	
	#endif //def TELEXPHONE
	
#endif /* _TXP_H_ */
//@}
