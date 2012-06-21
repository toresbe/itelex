/*! \file TlnServer.c \brief Anwendung zur Einbettung in das TxP2-System */
//***************************************************************************
//*            TlnServer.c
//*
//****************************************************************************/
///	\ingroup software
///	\defgroup TxP Hauptfunktion dieser Applikation: Als Server für die Herstellung 
/// von Verbindungen dienen (Teilnehmerauskunft). Teilfunktion ist auch die ???
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
#include <avr/pgmspace.h>
#include <avr/version.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <bool.h>

#include "config.h"

#ifdef TELEXPHONE

// #include "hardware/led/led_core.h"

#include "system/net/ip.h"
#include "system/net/tcp.h"
#include "system/net/ethernet.h"
#include "system/net/dns.h"
#include "system/thread/thread.h"
// #include "system/config/eeconfig.h"
#include "system/clock/clock.h"
// #include "system/softreset/softreset.h"

// #include "apps/httpd/cgibin/cgi-bin.h"
// #include "apps/httpd/httpd2_pharse.h"

// #include "hardware/timer0/timer0.h"

// #include "CgiFormTools.h"
#include "TxP.h"
#include "TlnBuch.h"
#include "TlnServer.h"

// #include "BusKomm.h"
#include "TxP2-Defs.h"
// #include "FifoPuffer.h"
// #include "BaudotCode.h"
#include "Protokoll.h"


static int TlnServerInSocket;
	//!< Handle für eingehende TlnAbfrage-Verbindungen ("Server")
	
static int TlnServerOutSocket;
	//!< Handle für ausgehende TlnServer-Verbindungen ("Client") -> für Synchronisation mehrerer Server

static uint16_t SocketSendeSperrZaehler;
	//!< Zählt nach Sendefehlern herunter und verhindert solange neue Sendeversuche.
	
static uint8_t SocketSendeFehlerZaehler;
	//!< Zählt bis 10 bei nicht erfolgreichen Sendeversuchen auf dem Socket.
	
	
	
enum { TlnServBufMax = 250 } ; //!< Größe des TCP-Puffers

static char TlnServBuf[TlnServBufMax]; //!< TCP-Empfangspuffer und Sendepuffer
	//!< Dieser Puffer wird nur lokal in SocketBearbeiten benutzt. Alle eingehenden
	//!< Daten werden sofort weiterverarbeitet, alle ausgehenden Daten sofort gesendet.
	

//! Socket für Teilnehmerauskunft-Server bearbeiten.
//---------------------------------------------------------------------------
//! \par - Empfangene Daten in den Socket-Empfangspuffer schreiben
//! \par - Daten vom Socket-Empfangspuffer übersetzen in den SendePuffer (zum Endgerät).
//! \par - Daten des Empfangspuffers (Endgerät) in den Socket-Sendepuffer übersetzen
//! \par - Daten des Socket-Sendepuffers ggf. senden
//! \par - Schlusszeichen bearbeiten

static void SocketBearbeiten(int *Socket, bool IstVerbunden)
	{
	if (*Socket == NO_SOCKET_USED)
		return;
		
	if (SocketSendeSperrZaehler > 0)
		SocketSendeSperrZaehler--; //! \todo dies gehört eigentlich in eine Timer-Funktion
	
	// Auf neue Daten testen
	// ---------------------------------
	int InCount = GetBytesInSocketData(*Socket);
	uint16_t OutCount = 0;
	
	if (InCount > 0) 
		{
		int Res = GetSocketData(*Socket, InCount, TlnServBuf);
		
		if (ProtokollLevel == 3) // Daten explizit
			{
			ProtokollierenInt_P(PSTR("TlnSrv: Socket Empfang: (%d/" ), InCount);
			ProtokollierenInt_P(PSTR("%d)"), Res);
			for (uint16_t i = 0 ; i < Res ; i++)
				ProtokollierenInt_P(PSTR(" %02X"), TlnServBuf[i]);
			Protokollieren_P(PSTR("\r\n"));
			}		
		
		// Daten des Socket-Empfangspuffer interpretieren
		// ----------------------------------------------
		// es wird immer nur ein Telegramm gesendet und empfangen

		// TODO
		
		} // if GetBytesInSocketData > 0
		
	/*/ ggf Lebenszeichen erzeugen
	// --------------------------
	if (SocketLebenszeichenZaehler > 4 * TxpTimerFreq 
	    && SocketOutBufUsed == 0
		&& SocketSendeSperrZaehler == 0)
		{ // alle 4 Sekunden ein Lebenszeichen
		SocketOutBuf[0] = TXPC_NULL;
		SocketOutBuf[1] = 0;
		SocketOutBufUsed = 2;
		} */

	// soll offene Verbindung geschlossen werden?
	if (CheckSocketState(*Socket) == SOCKET_NOT_USE)
		{
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TlnSrv: Socket wurde von Gegenstelle geschlossen\r\n" ));
		CloseTCPSocket(*Socket);
		*Socket = NO_SOCKET_USED;
		return;
		}
		
	// Daten ggf. ins Netz senden
	// --------------------------------------------------
	if (OutCount > 0 && SocketSendeSperrZaehler == 0) //! \todo && !HaltSocketOut
		{
		int Res = PutSocketData_RPE(*Socket, OutCount, TlnServBuf, RAM);
		// SocketLebenszeichenZaehler = 0; 

		if (ProtokollLevel == 3)
			{
			ProtokollierenInt_P(PSTR("TlnSrv: Socket Sendung: (%d)" ), OutCount);
			for (uint16_t i = 0 ; i < OutCount ; i++)
				ProtokollierenInt_P(PSTR(" %02X"), TlnServBuf[i]);
			ProtokollierenInt_P(PSTR(" --> Res %d\r\n" ), Res);
			}

		if (Res <= 0)
			{
			SocketSendeFehlerZaehler++;
			if (SocketSendeFehlerZaehler >= 10)
				{
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TlnSrv: Mehrfache Fehler beim Senden ins Netz, Socket wird geschlossen\r\n" ));
				CloseTCPSocket(*Socket);
				*Socket = NO_SOCKET_USED;
				}
			else
				SocketSendeSperrZaehler = 1000; // 1 Sekunde für nächsten Versuch warten. 
					//! \todo Wirkliche Zeitabhängigkeit
			}
		else if (Res < OutCount)
			{
			Protokollieren_P(PSTR("TlnSrv: FEHLER: Sendung war nicht vollständig\r\n" ));
			}
		} // if es gibt was zu senden
	
	} // SocketBearbeiten()

	
//! Der TelexPhone-Rufnummernserver-Client an sich.
//------------------------------------------------------------------------------------------------------------
//! Diese Funktion wird zyklisch aufgerufen und hat folgende Aufgaben:
//! \par - Nachschauen, ob eine Verbindung auf den registrierten Port eingegangen ist. Wenn ja 
//! holt er sich die Socketnummer der Verbindung und speichert diese.
//! \par - Wenn eine Verbindung zustande gekommen ist wird diese wiederrum zyklisch nach neuen Daten abgefragt und entsprechend
//! reagiert.
//! \param 	NONE
//! \return	NONE

void txp_tlnserv_thread()
	{
	uint8_t Code;

#if defined(LEDROT_TXPTHREADBLOCK)
	//! \todo LED_off(ROT); 
#endif //defined(LEDROT_TXPTHREADBLOCK)
	
	// ======================================================================
	// Socket Empfang und Sendung
	// ======================================================================

	if (TlnServerOutSocket != NO_SOCKET_USED)
		SocketBearbeiten(&TlnServerOutSocket, true);
		
	if (TlnServerInSocket != NO_SOCKET_USED)
		SocketBearbeiten(&TlnServerInSocket, true);
		
	// ==========================================================================
	// Neuer Anruf vom Ethernet?
	// ==========================================================================

	// auf neue Verbindungsanfrage testen
	int NewServerSocket = CheckPortRequest(TXP_TLNSERV_PORT);
	if (NewServerSocket != NO_SOCKET_USED)
		{
		extern struct TCP_SOCKET TCP_sockettable[];

		if (ProtokollLevel >= 1)
			{
			Protokollieren_P(PSTR("TlnSrv: Server-Socket geoeffnet von IP "));
			ProtokollierenIPAdr(TCP_sockettable[NewServerSocket].SourceIP);
			Protokollieren_P(PSTR(" / MAC "));
			ProtokollierenMAC(TCP_sockettable[NewServerSocket].MACadress);
			}
		
		if (TlnServerInSocket == NO_SOCKET_USED)
			{
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR(" ...ok\r\n"));
			TlnServerInSocket = NewServerSocket;
			SocketSendeFehlerZaehler = 0;
			SocketSendeSperrZaehler = 0;
			}
		else
			{ 
			PutSocketData_RPE(NewServerSocket, 7, PSTR("\004\005occ\r\n"), FLASH); //! \todo Richtig???
			CloseTCPSocket(NewServerSocket);
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR(" ...ABGEWIESEN\r\n" ));
			}
		}

	// ==========================================================================
	// Timeouts?
	// ==========================================================================

	//! \todo
	
	} // txp_tlnserv_thread
	
	
// ================================================================================	

/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Initialisiert den Teilnehmerauskunft-Server-clinet und registriert den Port auf welchen dieser lauschen soll.
 * \param 	NONE
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void txp_tlnserv_init()
	{
	// EEPROM auslesen
	/*! \todo 
	
	char Buf[TlnAdresseMax];
	uint8_t i;

	if (readConfig_P(EigeneNummer_P, Buf) == 1)
		BusEigenAdresse = WahlZuAdresse(atoi(Buf), strlen(Buf));
	else
		BusEigenAdresse = 22 << 1;
	*/
	
	/*! \todo Timer?

	timer0_init(TxpTimerFreq); 
	if (!timer0_RegisterCallbackFunction(txp_timerEvent))
		return;
	*/
	
	/*! \todo cgi?
	cgi_RegisterCGI( txp_cgi_msg_In, PSTR("txp-msg-in.cgi"));
	*/

	TlnServerInSocket = NO_SOCKET_USED;
	TlnServerOutSocket = NO_SOCKET_USED;
	SocketSendeSperrZaehler = 0;
	SocketSendeFehlerZaehler = 0;
	
	RegisterTCPPort(TXP_TLNSERV_PORT);
	
	printf_P( PSTR("Txp TlnServer Port %d.\r\n") , TXP_PORT );

	THREAD_RegisterThread( txp_tlnserv_thread, PSTR("TlnSrv"));
	}


#endif //def TELEXPHONE

			


