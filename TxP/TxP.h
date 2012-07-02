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
	
	//! Der TCP-Port für die TelexPhone-Rumnummernverwaltung
	#define TXP_TLNSERV_PORT 11811

	// Daten / Datenstrukturen für Datenaustausch mit Teilnehmer-Server ("Auskunft")
	#define TLNSERV_SELBSTAKT 0x01
	#define TLNSERV_IPRUECKMELD 0x02
	#define TLNSERV_ABFRAGE 0x03
	#define TLNSERV_AUSKUNFT_NICHTVERG 0x04
	#define TLNSERV_AUSKUNFT_IP 0x05
	#define TLNSERV_AUSKUNFT_URL 0x06
	#define TLNSERV_FEHLER 0xFF
	
	#define TLNSERV_URLMAXLEN 50

	typedef union
		{
		char Buf[50]; // 50 Zeichen für Diagnosetexte...
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
					uint16_t Pin; //!< Geheimzahl für DynIP-Aktualisierung
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
				// für Code == TLNSERV_AUSKUNFT_NICHTVERG keine Daten.
				struct 
					{
					uint8_t Ascii; // eigentlich bool
					long IP;
					uint16_t Port;
					uint8_t Durchwahl;
					} TlnAuskunftIP;
				struct 
					{
					uint8_t Ascii; // eigentlich bool
					char Url[TLNSERV_URLMAXLEN]; 
					uint16_t Port;
					uint8_t Durchwahl;
					} TlnAuskunftUrl;
				} ;
			} ;
		} TTlnServBuf; 
	
	
	extern void txp_init( void );
	extern void txp_thread( void );
	extern TTastendruck Tastendruck;
	extern bool WarteTaste();
	
	// Was soll LED rot anzeigen?
	//---------------------------
	//#define LEDROT_EXTEEPROM
	//#define LEDROT_SDKARTE
	#define LEDROT_TXPTHREADBLOCK
	//#define LEDROT_UNERWARTET  // noch ungenutzt
	
	#endif //def TELEXPHONE
	
#endif /* _TXP_H_ */
//@}
