/*! \file TlnServer.c \brief Anwendung zur Einbettung in das TxP2-System */
//***************************************************************************
//*            TlnServer.c
//*
//****************************************************************************/
///	\ingroup software
///	\defgroup TlnServer
/// Hauptfunktion dieser Applikation: Als Server für die Herstellung 
/// von Verbindungen dienen (Teilnehmerauskunft). \par
/// Grundsätzlicher Telegrammaufbau siehe #TTlnServBuf.
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

#include "system/net/ip.h"
#include "system/net/tcp.h"
#include "system/net/ethernet.h"
#include "system/net/dns.h"
#include "system/thread/thread.h"
// #include "system/config/eeconfig.h"
#include "system/clock/clock.h"

// #include "apps/httpd/cgibin/cgi-bin.h"
// #include "apps/httpd/httpd2_pharse.h"

// #include "hardware/timer0/timer0.h"

#include "TxP.h"

#ifdef TXP_TLNSERVER

#include "TlnBuch.h"
#include "TlnServer.h"

extern struct TCP_SOCKET TCP_sockettable[];
	// explizit, weil in keiner Header-Datei enthalten.

// #include "CgiFormTools.h"
// #include "BusKomm.h"
// #include "TxP2-Defs.h"
// #include "FifoPuffer.h"
// #include "BaudotCode.h"
#include "Protokoll.h"


typedef struct
	{
	int Socket; //!< der Handle zum Socket
	uint32_t SyncAusgabeStichzeit;
		//!< Für den aktuell laufenden Synchronisationsvorgang gültiges "Grenzdatum" für
		//!< zu sendende Einträge.
	bool Freigabe; //!< Korrekte Autentifizierung empfangen.
	bool AusgabeGestartet; //!< gespeicherte Adressen werden an den Gegenüber gesendet.
	TTlnListerDat AusgabeLister; //!< Daten für die Ausgabe (welcher Datensatz wurde zuletzt gesendet)
	long AusgabeStichdatum; //!< Nur Einträge, die neuer sind als X werden gesendet.
	} TTlnServKanal;


static TTlnServKanal TlnServerIn;
	//!< Handle für eingehende TlnAbfrage-Verbindungen ("Server")
	
static TTlnServKanal TlnServerOut;
	//!< Handle für ausgehende TlnServer-Verbindungen ("Client") -> für Synchronisation mehrerer Server

static uint16_t SocketSendeSperrZaehler;
	//!< Zählt nach Sendefehlern herunter und verhindert solange neue Sendeversuche.
	
static uint8_t SocketSendeFehlerZaehler;
	//!< Zählt bis 10 bei nicht erfolgreichen Sendeversuchen auf dem Socket.
		
static TTlnServBuf TlnServBuf;
	//!< Lokaler Puffer für alle Anfragen an den Teilnehmerauskunft-Server. 
	//!< Muss sofort bearbeitet / geleert werden, da Puffer für jeden Kanal verwendet wird.

uint32_t TlnServSyncGeheimzahl;	
	//!< Geheimzahl zur generellen Freigabe der Aufnahme von Teilnehmer-Einträgen beim
	//!< Empfangenden Server.
	
static uint32_t TlnServSyncStichzeit[ANZ_TEILNEHMER_SERVER];
	//!< Wann wurden zuletzt Teilnehmer-Einträge an den jeweiligen anderen Server 
	//!< \b gesendet.
	
static uint32_t TlnBuchLetzteAenderung;
	//!< Wann wurden zuletzt Teilnehmer-Einträge geändert, die zu synchronisieren sind.


static TKurzTimer SyncWarteTimer;
	//!< Wartezeit bis zur nächsten aktiven Aktion des Teilnehmerauskunft-Servers.
	
static uint16_t SyncWarteEnde;
	//!< Wie lange soll bis zur nächsten Aktion gewartet werden.
	//!< \n 20 Sekunden bis zur initialen Abholung von den Akuellen Daten nach Neustart eines
	//!< Teilnehmerauskunft-Servers.
	//!< \n 30 Sekunden bis zur Sendung von aktualisierten Teilnehmer-Daten an den folgenden 
	//!< Teilnehmerauskunft-Server.
	//!< \n 10 Sekunden nach einem nicht erfolgreichen Verbindungsaufbau zu einem
	//!< Partner-Teilnehmerauskunft-Server.
	//!< \n 8 Minuten nach erfolgreicher Synchronisation an den ersten 
	//!< Partner-Teilnehmerauskunft-Server bis zur Sendung an den nächsten Partner-Teilnehmerauskunft-Server.

static bool InitialAbfrageStarten;
	//!< nach Reset true, bis erfolgreich von einem anderen Teilnehmerauskunft-Server 
	//!< alle Daten abgeholt worden sind.
	
	
	
static void KanalInit(TTlnServKanal* k, int aSocket)
	{
	k->Socket = aSocket;
	k->Freigabe = false;
	k->AusgabeGestartet = false;
	}
	

//! Bearbeitet die Aktualisierungsmeldung im eigenen Telefonbuch.
//---------------------------------------------------------------
//! \retval true, wenn Meldung akzeptiert wurde.
	
static bool TlnAktualisierung(TTlnServBuf *tsb, long TlnIP)
	{
	TTlnDaten TD;
	
	if (TlnSuche(tsb->SelbstAkt.RufNr, false, &TD))
		{ // Eintrag ist schon vorhanden
		if (TD.Flags & TlnFlag_Lokal)
			{
			if (ProtokollLevelTlnServ >= 1)
				ProtokollierenInt_P(PSTR("TlnSrv: ! Teilnehmer %ld schon vorhanden, aber LOKAL\r\n"), TD.Nummer);
			return false;
			}
		else if (TD.Flags & TlnFlag_Gesperrt)
			{
			if (ProtokollLevelTlnServ >= 1)
				ProtokollierenInt_P(PSTR("TlnSrv: ! Teilnehmer %ld schon vorhanden, aber noch nicht freigegeben\r\n"), TD.Nummer);
			return false;
			}
		else if (TD.AdrArt != TxpDynIP)
			{
			if (ProtokollLevelTlnServ >= 1)
				ProtokollierenInt_P(PSTR("TlnSrv: ! Teilnehmer %ld schon vorhanden, aber nicht Typ 'dynamisch'\r\n"), TD.Nummer);
			return false;
			}
		else if (tsb->SelbstAkt.Pin != TD.DynPin)
			{
			ProtokollierenInt_P(PSTR("TlnSrv: ! Teilnehmer %ld schon vorhanden, aber falsche Pin gesendet\r\n"), TD.Nummer);
				// immer speichern, auch bei abgeschaltetem Protokoll.
			return false;
			}
		else if (tsb->SelbstAkt.Port == TD.Port && TlnIP == TD.IPAdr)
			{
			if (ProtokollLevelTlnServ >= 2)
				ProtokollierenInt_P(PSTR("TlnSrv: Teilnehmer %ld schon vorhanden, unveraenderte Daten\r\n"), TD.Nummer);
			return true; // da zulässige Aktualisierung
			}
		else
			{ // jetzt wirklich aktualisieren
			TD.IPAdr = TlnIP;
			TD.Port = tsb->SelbstAkt.Port;
			
			if (TlnHinzufuegen(&TD, true) >= 0)
				{
				if (ProtokollLevelTlnServ >= 1)
					{
					ProtokollierenInt_P(PSTR("TlnSrv: Teilnehmer %ld erfolgreich aktualisiert: IP "), TD.Nummer);
					ProtokollierenIPAdr(TlnIP);
					ProtokollierenInt_P(PSTR(" Port %u\r\n"), TD.Port);
					}
				TlnServTlnbuchEintragGeaendert(&TD);
				return true;
				}
			else
				{ // speichern war nicht erfolgreich
				ProtokollierenInt_P(PSTR("TlnSrv: ! Aenderung Teilnehmer %ld konnte nicht gespeichert werden\r\n"), TD.Nummer);
				return false;
				}
			} // Aktualisierung erforderlich
		} // Eintrag schon vorhanden
		
	else
		{ // noch kein Eintrag vorhanden, neuen anlegen, aber gesperrt.
		TlnDatenInit(&TD);
		TD.Nummer = tsb->SelbstAkt.RufNr;
		TD.Name[0] = '?';
		TD.Name[1] = '\0';
		TD.Flags = TlnFlag_Gesperrt;
		TD.AdrArt = TxpDynIP;
		TD.IPAdr = TlnIP;
		TD.Port = tsb->SelbstAkt.Port;
		TD.DynPin = tsb->SelbstAkt.Pin;

		if (TlnHinzufuegen(&TD, true) >= 0)
			{
			if (ProtokollLevelTlnServ >= 1)
				{
				ProtokollierenInt_P(PSTR("TlnSrv: Teilnehmer %ld erfolgreich angelegt: IP "), TD.Nummer);
				ProtokollierenIPAdr(TlnIP);
				ProtokollierenInt_P(PSTR(" Port %u (noch gesperrt!)\r\n"), TD.Port);
				}
			Diagnoseausgabe_P(PSTR("Neuer Teilnehmer angemeldet"), 1);
			TlnServTlnbuchEintragGeaendert(&TD); // da er neu war, muss er geändert worden sein.
			return true;
			}
		else
			{ // speichern war nicht erfolgreich
			ProtokollierenInt_P(PSTR("TlnSrv: ! Neuer Teilnehmer %ld konnte nicht gespeichert werden\r\n"), TD.Nummer);
			return false;
			}
		} // Neuanlage erforderlich
		
	return true;
	}
	
	
static uint8_t FehlerRueckmelden(PGM_P Text, int val)
	{
	TlnServBuf.Code = TLNSERV_FEHLER;
	sprintf_P(TlnServBuf.PureData, Text, val);
	TlnServBuf.DataLen = strlen(TlnServBuf.PureData) + 1;
	return TlnServBuf.DataLen + 3; // Code + Len + Text + \0
	}
	
	
//! Wird aufgerufen, wenn ein Eintrag im eigenen Telefonbuch geändert wird.
//---------------------------------------------------------------------------
//! Ziel ist, die Synchronisation dieses geänderten Eintrags anzustoßen.
void TlnServTlnbuchEintragGeaendert(TTlnDaten *Tln)
	{
	if (Tln->Flags & TlnFlag_Lokal)
		return;
		
	if (TlnBuchLetzteAenderung < Tln->Datum)
		TlnBuchLetzteAenderung = Tln->Datum;
		
	for (uint8_t i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
		{
		if (TlnServSyncStichzeit[i] > Tln->Datum)
			TlnServSyncStichzeit[i] = Tln->Datum; 
			// dies kommt nur dann vor, wenn bereits eine Synchronisation stattfand,
			// die Datenbasis dafür aber bereits veraltet war.
		}
		
	//! \todo bei Empfang von Synchronisation den Sender "ausklammern". 
		
	} // TlnServTlnbuchEintragGeaendert()
	
	
// Den nächsten Eintrag des Teilnehmer-Verzeichnisses senden.
// ----------------------------------------------------------
// ...wenn nicht lokal und Datum jünger als #AusgabeStichdatum.
// \retval Anzahl der zu sendenden Bytes.
	
static int TlnDatensatzSyncSenden(TTlnServKanal *Kanal)
	{
	if (!Kanal->AusgabeGestartet)
		{
		TlnListerStart(&Kanal->AusgabeLister);
		if (ProtokollLevelTlnServ >= 2)
			{
			Protokollieren_P(PSTR("TlnSrv: Starte Ausgabe der Teilnehmer-Eintraege\r\n"));
			}
		Kanal->AusgabeGestartet = true;
		}
		
	while (TlnListerNaechster(&Kanal->AusgabeLister, &TlnServBuf.TlnAuskunft))
		{
		// wenn nicht lokal, dann senden...
		if ((TlnServBuf.TlnAuskunft.Flags & TlnFlag_Lokal) == 0
		    && TlnServBuf.TlnAuskunft.Datum >= Kanal->AusgabeStichdatum)
			{
			if (ProtokollLevelTlnServ >= 2)
				{
				ProtokollierenInt_P(PSTR("TlnSrv: Sende Teilnehmer-Eintrag %lu\r\n"), TlnServBuf.TlnAuskunft.Nummer);
				}
			TlnServBuf.Code = TLNSERV_AUSKUNFT_VERSION1;
			// TlnServBuf-TlnAuskunft ist bereits gefüllt.
			TlnServBuf.DataLen = sizeof(TlnServBuf.TlnAuskunft);
			return 2 + TlnServBuf.DataLen;
			}
		}

	// Kein zu sendender Datensatz mehr da...
	if (ProtokollLevelTlnServ >= 2)
		{
		Protokollieren_P(PSTR("TlnSrv: Sende Ende-Kennung der Teilnehmer-Eintraege\r\n"));
		}
	TlnServBuf.Code = TLNSERV_SYNC_ENDE;
	TlnServBuf.PureData[0] = '\0';
	TlnServBuf.DataLen = 1;
	return 2 + TlnServBuf.DataLen;
	} // TlnDatensatzSyncSenden()
		
		
//! Senden der Daten zum Socket.

static void SocketDatenSenden(TTlnServKanal *Kanal, uint16_t OutCount)
	{
	if (OutCount > 0 && SocketSendeSperrZaehler == 0) //! \todo && !HaltSocketOut
		{
		int Res = PutSocketData_RPE(Kanal->Socket, OutCount, TlnServBuf.Buf, RAM);
		// SocketLebenszeichenZaehler = 0; 

		if (ProtokollLevelTlnServ >= 3)
			{
			ProtokollierenInt_P(PSTR("TlnSrv: Socket Sendung: (%u)" ), OutCount);
			ProtokollierenPuffer(TlnServBuf.Buf, OutCount);
			ProtokollierenInt_P(PSTR(" --> Res %d\r\n" ), Res);
			}

		if (Res <= 0)
			{
			SocketSendeFehlerZaehler++;
			if (SocketSendeFehlerZaehler >= 10)
				{
				if (ProtokollLevelTlnServ >= 1)
					Protokollieren_P(PSTR("TlnSrv: ! Mehrfache Fehler beim Senden ins Netz, Socket wird geschlossen\r\n" ));
				CloseTCPSocket(Kanal->Socket);
				Kanal->Socket = NO_SOCKET_USED;
				}
			else
				SocketSendeSperrZaehler = 1000; // 1 Sekunde für nächsten Versuch warten. 
					//! \todo Wirkliche Zeitabhängigkeit
			}
		else if (Res < OutCount)
			{
			Protokollieren_P(PSTR("TlnSrv: ! Sendung war NICHT VOLLSTAENDIG\r\n" ));
			}

		TCP_sockettable[Kanal->Socket].Timeoutcounter = 5; // Timeout auf 5 Sekunden verkürzen, da meist nur eine Anfrage.
			
		} // if es gibt was zu senden
	} // SocketDatenSenden()
		
	
//! Socket für Teilnehmerauskunft-Server bearbeiten.
//---------------------------------------------------------------------------
//! Aufgaben:
//! \par - Empfangene Daten in den Socket-Empfangspuffer schreiben
//! \par - Daten vom Socket-Empfangspuffer übersetzen in den SendePuffer (zum Endgerät).
//! \par - Daten des Empfangspuffers (Endgerät) in den Socket-Sendepuffer übersetzen
//! \par - Daten des Socket-Sendepuffers ggf. senden
//! \par - Schlusszeichen bearbeiten

static void SocketBearbeiten(TTlnServKanal *Kanal)
	{
	if (Kanal->Socket == NO_SOCKET_USED)
		return;
		
	if (SocketSendeSperrZaehler > 0)
		SocketSendeSperrZaehler--; //! \todo dies gehört eigentlich in eine Timer-Funktion
	
	// Auf neue Daten testen
	// ---------------------------------
	int InCount = GetBytesInSocketData(Kanal->Socket);
	uint16_t OutCount = 0;
	
	if (InCount > 0) 
		{
		TTlnDaten TD;
		TlnDatenInit(&TD);
		
		int Res = GetSocketData(Kanal->Socket, InCount, TlnServBuf.Buf);
		
		if (ProtokollLevelTlnServ >= 3) // Daten explizit
			{
			ProtokollierenInt_P(PSTR("TlnSrv: Socket Empfang: (%d/" ), InCount);
			ProtokollierenInt_P(PSTR("%d)"), Res);
			ProtokollierenPuffer(TlnServBuf.Buf, Res);
			Protokollieren_P(PSTR("\r\n"));
			}		
		
		// Daten des Socket-Empfangspuffer interpretieren
		// ----------------------------------------------
		// es wird immer nur ein Telegramm gesendet und empfangen
		switch (TlnServBuf.Code)
			{
			case TLNSERV_SELBSTAKT:
				if (TlnServBuf.DataLen < sizeof(TlnServBuf.SelbstAkt))
					OutCount = FehlerRueckmelden(PSTR("update not enough data: %u"), TlnServBuf.DataLen);
				else
					{
					long MeldeIP = TCP_sockettable[Kanal->Socket].SourceIP;
					if (ProtokollLevelTlnServ >= 2)
						{
						ProtokollierenInt_P(PSTR("TlnSrv: Aktualisierung empfangen. Nummer %lu " ), TlnServBuf.SelbstAkt.RufNr);
						ProtokollierenInt_P(PSTR("Auth %u " ), TlnServBuf.SelbstAkt.Pin);
						ProtokollierenInt_P(PSTR("Port %u\r\n" ), TlnServBuf.SelbstAkt.Port);
						}
					
					if (TlnAktualisierung(&TlnServBuf, MeldeIP))
						{
						// Antwort generieren:
						TlnServBuf.Code = TLNSERV_IPRUECKMELD;
						TlnServBuf.DataLen = sizeof(TlnServBuf.IpRueckm);
						TlnServBuf.IpRueckm.EmpfIP = MeldeIP;
						OutCount = 2 + TlnServBuf.DataLen;
						}
					else
						{
						OutCount = FehlerRueckmelden(PSTR("forbidden"), 0);	
						if (ProtokollLevelTlnServ >= 1)
							{
							Protokollieren_P(PSTR("TlnSrv: ! abgewiesene Anfrage war von IP "));
							ProtokollierenIPAdr(MeldeIP);
							Protokollieren_P(PSTR("\r\n"));
							}
						}
					}
				break;
				
			case TLNSERV_ABFRAGE_VERSION1:
				if (TlnServBuf.DataLen < sizeof(TlnServBuf.TlnAbfr))
					OutCount = FehlerRueckmelden(PSTR("request not enough data: %u"), TlnServBuf.DataLen);
				else
					{
					uint32_t RufNr = TlnServBuf.TlnAbfr.RufNr;
					if (ProtokollLevelTlnServ >= 2) 
						ProtokollierenInt_P(PSTR("TlnSrv: Abfrage empfangen. Nummer %ld: "), RufNr);
						
					// Telefonbuch abfragen
					if (TlnSuche(RufNr, false, &TD)
						&& !(TD.Flags & TlnFlag_Lokal)
						&& !(TD.Flags & TlnFlag_Gesperrt))
						{ // gefunden
						// Antwort generieren:
						TlnServBuf.Code = TLNSERV_AUSKUNFT_VERSION1;
						TlnServBuf.TlnAuskunft = TD;
						if (TlnServBuf.TlnAuskunft.AdrArt == TxpDynIP)
							TlnServBuf.TlnAuskunft.AdrArt = TxpIP;
						TlnServBuf.TlnAuskunft.DynPin = 0; // Datenschutz
						TlnServBuf.DataLen = sizeof(TlnServBuf.TlnAuskunft);
						OutCount = 2 + TlnServBuf.DataLen;
						if (ProtokollLevelTlnServ >= 2 && OutCount > 2)
							Protokollieren_P(PSTR(" ...gefunden\r\n"));
						}
					else
						{ // Nummer nicht gefunden
						TlnServBuf.Code = TLNSERV_AUSKUNFT_NICHTVERG;
						TlnServBuf.DataLen = 0;
						OutCount = 2 + TlnServBuf.DataLen;
						if (ProtokollLevelTlnServ >= 2) 
							Protokollieren_P(PSTR(" ! ...nicht gefunden oder gesperrt\r\n"));
						}
					}
				break;

			case TLNSERV_AUSKUNFT_VERSION1:
				if (TlnServBuf.DataLen < sizeof(TlnServBuf.TlnAuskunft))
					OutCount = FehlerRueckmelden(PSTR("update not enough data: %u"), TlnServBuf.DataLen);
				else if (!Kanal->Freigabe)
					OutCount = FehlerRueckmelden(PSTR("no authentification"), 0);
				else
					{
					Res = TlnHinzufuegen(&TlnServBuf.TlnAuskunft, false);
					if (Res < 0)
						{
						ProtokollierenInt_P(PSTR("TlnSrv: ! Datensatz vom Teilnehmer-Server mit Nr %ld konnte nicht gespeichert werden\r\n"), TlnServBuf.TlnAuskunft.Nummer);
						Diagnoseausgabe_P(PSTR("internes Rufnummern-Verzeichnis voll"), 2);
						OutCount = FehlerRueckmelden(PSTR("abort"), 0);	
						}
					else
						{ // noch alles gut
						if (Res > 0)
							{
							if (ProtokollLevelTlnServ >= 1)
								ProtokollierenInt_P(PSTR("TlnSrv: Datensatz vom Teilnehmer-Server mit Nr %ld empfangen und gespeichert\r\n"), TlnServBuf.TlnAuskunft.Nummer);
							TlnServTlnbuchEintragGeaendert(&TlnServBuf.TlnAuskunft);
							}
						else // Res == 0
							{
							if (ProtokollLevelTlnServ >= 2)
								ProtokollierenInt_P(PSTR("TlnSrv: Datensatz vom Teilnehmer-Server mit Nr %ld empfangen, keine Aenderung\r\n"), TlnServBuf.TlnAuskunft.Nummer);
							}
						// Antwort generieren:
						TlnServBuf.Code = TLNSERV_SYNC_QUITTUNG;
						TlnServBuf.DataLen = 0;
						OutCount = 2 + TlnServBuf.DataLen;
						}
					} // SyncFreigabe ok
				break;
				
			case TLNSERV_SYNC_TOTALABFRAGE:
				if (TlnServBuf.DataLen < sizeof(TlnServBuf.SyncAnmeldung))
					OutCount = FehlerRueckmelden(PSTR("login not enough data: %u"), TlnServBuf.DataLen);
				else if (TlnServBuf.SyncAnmeldung.Geheimzahl != TlnServSyncGeheimzahl)
					{
					OutCount = FehlerRueckmelden(PSTR("wrong authentification"), 0);
					Diagnoseausgabe_P("Teilnehmer-Server Anmeldung mit falscher Geheimzahl", 1);
					}
				else
					{
					Kanal->Freigabe = true;
					Kanal->AusgabeStichdatum = 0; // = alle 
					Kanal->AusgabeGestartet = false; // wird aber gleich gestartet
					OutCount = TlnDatensatzSyncSenden(Kanal);
					}
				break; 
				
			case TLNSERV_SYNC_ANMELDUNG:
				if (TlnServBuf.DataLen < sizeof(TlnServBuf.SyncAnmeldung))
					OutCount = FehlerRueckmelden(PSTR("login not enough data: %u"), TlnServBuf.DataLen);
				else if (TlnServBuf.SyncAnmeldung.Geheimzahl != TlnServSyncGeheimzahl)
					{
					OutCount = FehlerRueckmelden(PSTR("wrong authentification"), 0);
					Diagnoseausgabe_P("Teilnehmer-Server Anmeldung mit falscher Geheimzahl", 1);
					}
				else
					{
					Kanal->Freigabe = true;
					
					// Quittung senden:
					TlnServBuf.Code = TLNSERV_SYNC_QUITTUNG;
					TlnServBuf.DataLen = 0;
					OutCount = 2 + TlnServBuf.DataLen;
					}
				break;
			
			case TLNSERV_SYNC_QUITTUNG:
				if (!Kanal->Freigabe)
					OutCount = FehlerRueckmelden(PSTR("no authentification"), 0);
				else if (!Kanal->AusgabeGestartet)
					OutCount = FehlerRueckmelden(PSTR("unexpected acknowledge"), 0);
				else
					OutCount = TlnDatensatzSyncSenden(Kanal);
				
				break;
			
			case TLNSERV_SYNC_ENDE:
				if (ProtokollLevelTlnServ >= 2) 
					{
					Protokollieren_P(PSTR("TlnSrv: Ende Kennung empfangen, Socket wird geschlossen\r\n"));
					}		
				if (Kanal == &TlnServerOut)
					InitialAbfrageStarten = false;
				CloseTCPSocket(Kanal->Socket);
				Kanal->Socket = NO_SOCKET_USED;
				break;
				
			case TLNSERV_FEHLER:
				if (ProtokollLevelTlnServ >= 1) 
					{
					Protokollieren_P(PSTR("TlnSrv: Fehlermeldung empfangen, Socket wird geschlossen: "));
					Protokollieren(TlnServBuf.PureData);
					Protokollieren_P(PSTR("\r\n"));
					}		
				CloseTCPSocket(Kanal->Socket);
				Kanal->Socket = NO_SOCKET_USED;
				break;
			
			case TLNSERV_IPRUECKMELD: // ist ein Fehler, da dieses Telegramm nur eine Antwort des Servers sein kann.
			case TLNSERV_AUSKUNFT_NICHTVERG: // ist ein Fehler, da dieses Telegramm nur eine Antwort des Servers sein kann.
			default:
				OutCount = FehlerRueckmelden(PSTR("unknown code %02X"), TlnServBuf.Code);
				break;

			}
			
		if (ProtokollLevelTlnServ >= 1 && OutCount > 0 && TlnServBuf.Code == TLNSERV_FEHLER)
			{
			Protokollieren_P(PSTR("TlnSrv: ! Error "));
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
	if (CheckSocketState(Kanal->Socket) == SOCKET_NOT_USE)
		{
		if (ProtokollLevelTlnServ >= 2)
			Protokollieren_P(PSTR("TlnSrv: Socket wurde von Gegenstelle geschlossen\r\n" ));
		CloseTCPSocket(Kanal->Socket);
		Kanal->Socket = NO_SOCKET_USED;
		return;
		}
		
	// Daten ggf. ins Netz senden
	// --------------------------------------------------
	SocketDatenSenden(Kanal, OutCount);
	
	} // SocketBearbeiten()

	
//! Verbindung zu einem Teilnehmer-Server öffnen
static bool TlnServSyncOeffnen()
	{
	int NewSock = TeilnehmerServerSocketOeffnen1(2); // HACK erst mal nur den dritten anwählen...
		// da wird auch Protokoll geschrieben.
		
	if (NewSock != -1)
		{
		KanalInit(&TlnServerOut, NewSock); 
		return true;
		}
	else
		return false;
	
	} // TlnServSyncOeffnen()
	
	
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

	if (TlnServerOut.Socket != NO_SOCKET_USED)
		SocketBearbeiten(&TlnServerOut);
		
	if (TlnServerIn.Socket != NO_SOCKET_USED)
		SocketBearbeiten(&TlnServerIn);
		
	// ==========================================================================
	// Neuer Anruf vom Ethernet?
	// ==========================================================================

	// auf neue Verbindungsanfrage testen
	int NewServerSocket = CheckPortRequest(TXP_TLNSERV_PORT);
	if (NewServerSocket != NO_SOCKET_USED)
		{
		if (ProtokollLevelTlnServ >= 2
			|| (ProtokollLevelTlnServ >= 1 && TlnServerIn.Socket != NO_SOCKET_USED))
			{
			Protokollieren_P(PSTR("TlnSrv: Server-Socket geoeffnet von IP "));
			ProtokollierenIPAdr(TCP_sockettable[NewServerSocket].SourceIP);
			Protokollieren_P(PSTR(" / MAC "));
			ProtokollierenMAC(TCP_sockettable[NewServerSocket].MACadress);
			}
		
		if (TlnServerIn.Socket == NO_SOCKET_USED)
			{
			if (ProtokollLevelTlnServ >= 2)
				Protokollieren_P(PSTR(" ...ok\r\n"));
			KanalInit(&TlnServerIn, NewServerSocket);
			SocketSendeFehlerZaehler = 0;
			SocketSendeSperrZaehler = 0;
			}
		else
			{ 
			if (ProtokollLevelTlnServ >= 1)
				Protokollieren_P(PSTR(" ! ...ABGEWIESEN\r\n" ));
			uint8_t OutCount = FehlerRueckmelden(PSTR("occupied"), 0);
			PutSocketData_RPE(NewServerSocket, OutCount, TlnServBuf.Buf, RAM);
			CloseTCPSocket(NewServerSocket);
			}
		}

	// ==========================================================================
	// Timeouts?
	// ==========================================================================

	//! \todo Prüfen, ob die System-Timeouts immer richtig wirken...
	
	// ==========================================================================
	// Neue Aktionen starten?
	// ==========================================================================
	if (KurzTimerVal(&SyncWarteTimer) > SyncWarteEnde)
		{
		if (TlnServerOut.Socket == NO_SOCKET_USED
			&& TlnServerIn.Socket == NO_SOCKET_USED)
			{
			if (InitialAbfrageStarten)
				{
				if (TlnServSyncOeffnen())
					{
					TlnServBuf.Code = TLNSERV_SYNC_TOTALABFRAGE;
					TlnServBuf.DataLen = sizeof(TlnServBuf.SyncAnmeldung);
					TlnServBuf.SyncAnmeldung.Version = 1; // gibt erst mal nix anderes.
					TlnServBuf.SyncAnmeldung.Geheimzahl = TlnServSyncGeheimzahl;
					SocketDatenSenden(&TlnServerOut, 2 + TlnServBuf.DataLen);
					TlnServerOut.Freigabe = true; // wer anruft weiß wen er anruft.
					}
				else
					{
					StartKurzTimer(&SyncWarteTimer);
					SyncWarteEnde = 30 * KurzTimerFreq; // 30 Sekunden
					}
				} // if InitialAbfrageStarten
			else
				{
				// ermitteln, welcher Server als nächstes Daten zugeschickt bekommt.
				// Daten zuschicken.
				//KanalInit(&TlnServerOut, 0); 
				}
			} // kein Socket offen 
		else
			StartKurzTimer(&SyncWarteTimer); // wieder Warten bis alle Verbindungen geschlossen
		} // if Wartezeit abgelaufen.
	
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

	TlnServerIn.Socket = NO_SOCKET_USED;
	TlnServerIn.Freigabe = false;
	TlnServerOut.Socket = NO_SOCKET_USED;
	TlnServerOut.Freigabe = false;
	
	SocketSendeSperrZaehler = 0;
	SocketSendeFehlerZaehler = 0;
	
	RegisterTCPPort(TXP_TLNSERV_PORT);
	
	printf_P( PSTR("Txp TlnServer Port %u.\r\n") , TXP_TLNSERV_PORT );
	
	for (uint8_t i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
		{
		TlnServSyncStichzeit[i] = 0;
		}
	TlnBuchLetzteAenderung = 0;

	StartKurzTimer(&SyncWarteTimer);
	SyncWarteEnde = 10 * KurzTimerFreq; // 10 Sekunden
	InitialAbfrageStarten = true;
	
	THREAD_RegisterThread( txp_tlnserv_thread, PSTR("TlnSrv"));
	}


#endif //def TXP_TLNSERVER

#endif //def TELEXPHONE

			


