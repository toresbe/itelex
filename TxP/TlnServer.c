/*! \file TlnServer.c \brief Anwendung zur Einbettung in das TxP2-System */
//***************************************************************************
//*            TlnServer.c
//*
//****************************************************************************/
///	\ingroup software
///	\defgroup TlnServer
/// Hauptfunktion dieser Applikation: Als Server für die Herstellung 
/// von Verbindungen dienen (Teilnehmerauskunft). \par
/// Grundsätzlicher Telegrammaufbau: \par
/// * 1 Byte Telegrammtyp (siehe TLNSERV_*) \par
/// * 1 Byte Gesamtlänge der Daten \par
/// * n Byte Daten \par
/// es ist nur ein Telegramm per IP-Packet erlaubt! \par 
/// Folgende Telegramme werden beherrscht: \par
/// TLNSERV_SELBSTAKT: Aktualisierung durch normalen IP-Telex-Teilnehmer. Daten: \par
/// * 4 Byte eigene Rufnummer \par
/// * 2 Byte Authentifizierungs-Code (eine Art Prüfsumme der Rufnummer) \par
/// * 2 Byte gewünschte Port-Nummer für Anrufe \par
/// TLNSERV_IPRUECKMELD: Rückmeldung des Servers nach TLNSERV_SELBSTAKT. Daten: \par
/// * 4 Byte aktuelle IP-Adresse des Teilnehmers (nicht des Servers). 0 wenn 
///          Aktualiserung nicht zulässig (weil z.B. falscher Authentifizierungs-Code) \par
/// TLNSERV_ABFRAGE: Abfrage eines Teilnehmers. Daten: \par
/// * 4 Byte gewünschte Rufnummer. \par
/// TLNSERV_AUSKUNFT: Daten des gefundenen Teilnehmers nach TLNSERV_ABFRAGE. Daten: \par
/// * 1 Byte Versionsnummer / Art der Antwort. 0 bei nicht gefundenem Teilnehmer. \par
/// * 4 Byte IP-Adresse (bei Version 1) \par
/// * 2 Byte Port-Nummer (bei Version 1) \par
/// TLNSERV_FEHLER: Fehlermeldung aller Art \par
/// * 1 Byte Fehlercode \par
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

extern struct TCP_SOCKET TCP_sockettable[];
	// explizit, weil in keiner Header-Datei enthalten.

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
		
static TTlnServBuf TlnServBuf;
	//!< Lokaler Puffer für alle Anfragen an den Teilnehmerauskunft-Server

	
static uint8_t FehlerRueckmelden(PGM_P Text, int val)
	{
	TlnServBuf.Code = TLNSERV_FEHLER;
	sprintf_P(TlnServBuf.PureData, Text, val);
	TlnServBuf.DataLen = strlen(TlnServBuf.PureData) + 1;
	return TlnServBuf.DataLen + 3; // Code + Len + Text + \0
	}
	
	
//! Socket für Teilnehmerauskunft-Server bearbeiten.
//---------------------------------------------------------------------------
//! Aufgaben:
//! \par - Empfangene Daten in den Socket-Empfangspuffer schreiben
//! \par - Daten vom Socket-Empfangspuffer übersetzen in den SendePuffer (zum Endgerät).
//! \par - Daten des Empfangspuffers (Endgerät) in den Socket-Sendepuffer übersetzen
//! \par - Daten des Socket-Sendepuffers ggf. senden
//! \par - Schlusszeichen bearbeiten

static void SocketBearbeiten(int *Socket)
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
		TTlnDaten TD;
		TlnDatenInit(&TD);
		
		int Res = GetSocketData(*Socket, InCount, TlnServBuf.Buf);
		
		if (ProtokollLevel >= 2) // Daten explizit
			{
			ProtokollierenInt_P(PSTR("TlnSrv: Socket Empfang: (%d/" ), InCount);
			ProtokollierenInt_P(PSTR("%d)"), Res);
			for (uint16_t i = 0 ; i < Res ; i++)
				ProtokollierenInt_P(PSTR(" %02X"), TlnServBuf.Buf[i]);
			Protokollieren_P(PSTR("\r\n"));
			}		
		
		// Daten des Socket-Empfangspuffer interpretieren
		// ----------------------------------------------
		// es wird immer nur ein Telegramm gesendet und empfangen
		switch (TlnServBuf.Code)
			{
			case TLNSERV_SELBSTAKT:
				if (TlnServBuf.DataLen < sizeof(TlnServBuf.SelbstAkt))
					OutCount = FehlerRueckmelden(PSTR("update not enough data: %d"), TlnServBuf.DataLen);
				else
					{
					uint32_t RufNr = TlnServBuf.SelbstAkt.RufNr;
					uint16_t GeheimNr = TlnServBuf.SelbstAkt.Pin;
					uint16_t PortNr = TlnServBuf.SelbstAkt.Port;
					long MeldeIP = TCP_sockettable[*Socket].SourceIP;
					ProtokollierenInt_P(PSTR("TlnSrv: Aktualisierung empfangen. Nummer %ld " ), RufNr);
					ProtokollierenInt_P(PSTR("Auth %d " ), GeheimNr);
					ProtokollierenInt_P(PSTR("Port %d\r\n" ), PortNr);
					//! \todo PIN pruefen
					//! \todo Speichern
					// Antwort generieren:
					TlnServBuf.Code = TLNSERV_IPRUECKMELD;
					TlnServBuf.DataLen = sizeof(TlnServBuf.IpRueckm);
					TlnServBuf.IpRueckm.EmpfIP = MeldeIP;
					OutCount = 2 + TlnServBuf.DataLen;
					}
				break;
				
			case TLNSERV_ABFRAGE:
				if (TlnServBuf.DataLen < sizeof(TlnServBuf.TlnAbfr))
					OutCount = FehlerRueckmelden(PSTR("request not enough data: %d"), TlnServBuf.DataLen);
				else
					{
					uint32_t RufNr = TlnServBuf.TlnAbfr.RufNr;
					if (ProtokollLevel >= 1) 
						ProtokollierenInt_P(PSTR("TlnSrv: Abfrage empfangen. Nummer %ld: "), RufNr);
						
					// Telefonbuch abfragen
					if (TlnSuche(RufNr, false, &TD)
						&& !(TD.Flags & TlnFlag_Lokal)
						&& !(TD.Flags & TlnFlag_Gesperrt))
						{ // gefunden
						// Antwort generieren:
						switch (TD.AdrArt)
							{
							case TxpUrl:
							case AsciiUrl:
								TlnServBuf.Code = TLNSERV_AUSKUNFT_URL;
								TlnServBuf.DataLen = sizeof(TlnServBuf.TlnAuskunftUrl);
								strncpy(TlnServBuf.TlnAuskunftUrl.Url, TD.Adresse, sizeof(TlnServBuf.TlnAuskunftUrl.Url));
								TlnServBuf.TlnAuskunftUrl.RufNr = TD.Nummer;
								TlnServBuf.TlnAuskunftUrl.Port = TD.Port;
								TlnServBuf.TlnAuskunftUrl.Ascii = (TD.AdrArt == AsciiUrl);
								TlnServBuf.TlnAuskunftUrl.Durchwahl = TD.Durchwahl;
								break;
							
							case TxpIP:
							case TxpDynIP:
							case AsciiIP:
								TlnServBuf.Code = TLNSERV_AUSKUNFT_IP;
								TlnServBuf.DataLen = sizeof(TlnServBuf.TlnAuskunftIP);
								TlnServBuf.TlnAuskunftIP.RufNr = TD.Nummer;
								TlnServBuf.TlnAuskunftIP.IP = TD.IPAdr;
								TlnServBuf.TlnAuskunftIP.Port = TD.Port;
								TlnServBuf.TlnAuskunftIP.Ascii = (TD.AdrArt == AsciiIP);
								TlnServBuf.TlnAuskunftIP.Durchwahl = TD.Durchwahl;
								break;

							default:
								TlnServBuf.Code = TLNSERV_AUSKUNFT_NICHTVERG;
								TlnServBuf.DataLen = 0;
								ProtokollierenInt_P(PSTR("Ungültiger Typ in Teilnehmerliste Eintrag %ld\r\n"), RufNr);
								break;
							} // switch TD.AdrArt
							
						OutCount = 2 + TlnServBuf.DataLen;
						if (ProtokollLevel >= 1 && OutCount > 2)
							Protokollieren_P(PSTR(" ...gefunden\r\n"));
						}
					else
						{ // Nummer nicht gefunden
						TlnServBuf.Code = TLNSERV_AUSKUNFT_NICHTVERG;
						TlnServBuf.DataLen = 0;
						OutCount = 2 + TlnServBuf.DataLen;
						if (ProtokollLevel >= 1) 
							Protokollieren_P(PSTR(" ...nicht gefunden oder gesperrt\r\n"));
						}
					}
				break;
	
			case TLNSERV_IPRUECKMELD: // ist ein Fehler, da dieses Telegramm nur eine Antwort des Servers sein kann.
			case TLNSERV_AUSKUNFT_NICHTVERG: // ist ein Fehler, da dieses Telegramm nur eine Antwort des Servers sein kann.
			case TLNSERV_AUSKUNFT_URL: // ist ein Fehler, da dieses Telegramm nur eine Antwort des Servers sein kann.
			case TLNSERV_AUSKUNFT_IP: // ist ein Fehler, da dieses Telegramm nur eine Antwort des Servers sein kann.
			default:
				OutCount = FehlerRueckmelden(PSTR("unknown code %02X"), TlnServBuf.Code);
				break;
			}
			
		if (ProtokollLevel >= 1 && OutCount > 0 && TlnServBuf.Code == TLNSERV_FEHLER)
			{
			Protokollieren_P(PSTR("TlnServ: Error "));
			Protokollieren(TlnServBuf.PureData);
			Protokollieren_P(PSTR("\r\n"));
			}

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
		int Res = PutSocketData_RPE(*Socket, OutCount, TlnServBuf.Buf, RAM);
		// SocketLebenszeichenZaehler = 0; 

		if (ProtokollLevel >= 2)
			{
			ProtokollierenInt_P(PSTR("TlnSrv: Socket Sendung: (%d)" ), OutCount);
			for (uint16_t i = 0 ; i < OutCount ; i++)
				ProtokollierenInt_P(PSTR(" %02X"), TlnServBuf.Buf[i]);
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
	// ======================================================================
	// Socket Empfang und Sendung
	// ======================================================================

	if (TlnServerOutSocket != NO_SOCKET_USED)
		SocketBearbeiten(&TlnServerOutSocket);
		
	if (TlnServerInSocket != NO_SOCKET_USED)
		SocketBearbeiten(&TlnServerInSocket);
		
	// ==========================================================================
	// Neuer Anruf vom Ethernet?
	// ==========================================================================

	// auf neue Verbindungsanfrage testen
	int NewServerSocket = CheckPortRequest(TXP_TLNSERV_PORT);
	if (NewServerSocket != NO_SOCKET_USED)
		{
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
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR(" ...ABGEWIESEN\r\n" ));
			uint8_t OutCount = FehlerRueckmelden(PSTR("occupied"), 0);
			PutSocketData_RPE(NewServerSocket, OutCount, TlnServBuf.Buf, RAM);
			CloseTCPSocket(NewServerSocket);
			}
		}

	// ==========================================================================
	// Timeouts?
	// ==========================================================================

	//! \todo Prüfen, ob die System-Timeouts immer richtig wirken...
	
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
	/*
	
	char Buf[TlnAdresseMax];
	uint8_t i;

	if (readConfig_P(EigeneNummer_P, Buf) == 1)
		BusEigenAdresse = WahlZuAdresse(atoi(Buf), strlen(Buf));
	else
		BusEigenAdresse = 22 << 1;
	*/
	
	/*

	timer0_init(TxpTimerFreq); 
	if (!timer0_RegisterCallbackFunction(txp_timerEvent))
		return;
	*/
	
	/*
	cgi_RegisterCGI( txp_cgi_msg_In, PSTR("txp-msg-in.cgi"));
	*/

	TlnServerInSocket = NO_SOCKET_USED;
	TlnServerOutSocket = NO_SOCKET_USED;
	SocketSendeSperrZaehler = 0;
	SocketSendeFehlerZaehler = 0;
	
	RegisterTCPPort(TXP_TLNSERV_PORT);
	
	printf_P( PSTR("Txp TlnServer Port %d.\r\n") , TXP_TLNSERV_PORT );

	THREAD_RegisterThread( txp_tlnserv_thread, PSTR("TlnSrv"));
	}


#endif //def TELEXPHONE

			


