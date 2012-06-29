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
	
	#ifdef TELEXPHONE
	
	extern void txp_tlnserv_thread( void );
	extern void txp_tlnserv_init( void );
	
	#define TLNSERV_SELBSTAKT 0x01
	#define TLNSERV_IPRUECKMELD 0x02
	#define TLNSERV_ABFRAGE 0x03
	#define TLNSERV_AUSKUNFT 0x04
	#define TLNSERV_FEHLER 0xFF

	typedef union
		{
		char Buf[255];
		struct
			{
			uint8_t Code;
			uint8_t DataLen;
			union
				{
				char PureData[1];
				struct 
					{
					uint32_t RufNr;
					uint16_t Pin; // Personal Ident Nr.
					uint16_t Port;
					} SelbstAkt;
				struct
					{
					long EmpfIP;
					} IpRueckm;
				struct 
					{
					uint32_t RufNr;
					} TlnAbfr;
				struct 
					{
					uint8_t AuskTyp;
					long IP;
					uint16_t Port;
					} TlnAuskunft;
				} ;
			} ;
		} TTlnServBuf; 
	
	#endif //def TELEXPHONE
	
#endif /* _TLNSERVER_H_ */
//@}
