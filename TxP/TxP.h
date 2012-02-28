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
	// #include <bool.h>
	#include "system/shell/shell.h"
	#include "config.h"

	//! Der TCP-Port für die TelexPhone-Kommunikation
	#define TXP_PORT 134
	
	void txp_init( void );
	void txp_thread( void );

	#endif //def TELEXPHONE
	
#endif /* _TXP_H_ */
//@}
