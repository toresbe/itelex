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
	int ListeIdx; //!< Welcher der Einträge aus TeilnehmerServerAdresse ist verbunden.
		//!< -1 bei kommenden Verbindungen.
	uint32_t NutzungZaehler; //!< Zählt, wie oft dieser Kanal geöffnet wurde.
	uint32_t SyncAusgabeStichzeit;
		//!< Für den aktuell laufenden Synchronisationsvorgang gültiges "Grenzdatum" für
		//!< zu sendende Einträge.
	bool Freigabe; //!< Korrekte Autentifizierung empfangen.
	bool IstInitialAbfrage; //!< Dieser Kanal wurde geöffnet, um das Teilnehmer-Verzeichnis nach 
						    //!< Neustart zu initialisieren.
	bool AusgabeGestartet; //!< gespeicherte Adressen werden an den Gegenüber gesendet.
	TTlnListerDat AusgabeLister; //!< Daten für die Ausgabe (welcher Datensatz wurde zuletzt gesendet)
	long AusgabeStichdatum; //!< Nur Einträge, die neuer sind als X werden gesendet.
	bool Fertig; //!< true, wenn Auftrag erfüllt. Sonst wäre ein vorzeitiges Ende ein Fehler.
	uint8_t SendeFehlerZaehler;	//!< Zählt bis 10 bei nicht erfolgreichen Sendeversuchen auf dem Socket.
	TKurzTimer WiederholungVerzoegerung;
		//!< Bei spontanem Verbindungsabbau oder Sendestörung wird 2 Sekunden auf den nächsten 
		//!< Versuch gewartet.
	} TTlnServKanal;


enum { AnzTlnServKanaele = 4 } ;


static TTlnServKanal TlnServer[AnzTlnServKanaele];
	//!< Teilnehmer-Server kanäle ein- und ausgehende TlnAbfrage-Verbindungen.
	//!< Für ausgehende Verbindungen wird nur Index 0 verwendet, für kommende
	//!< Verbindungen der jeweils freie.
	
		
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
	//!< \todo: Für jeden TlnServer ab und zu (Tage) eine Komplett-Abfrage durchführen.
	
	
	
static void KanalInit(TTlnServKanal* k, int aSocket)
	{
	k->Socket = aSocket;
	k->ListeIdx = -1;
	k->Freigabe = false;
	k->AusgabeGestartet = false;
	k->IstInitialAbfrage = false;
	k->Fertig = false;
	k->SendeFehlerZaehler = 0;
	k->NutzungZaehler++;
	}
	
	
//! Leitet eine Protokollzeile des Teilnehmerservers ein.

static void ProtokollierenTlnSrv(TTlnServKanal *Kanal)
	{
	if (Kanal != NULL)
		{
		ProtokollierenInt_P(PSTR("TlnSrv(%d,"), Kanal - TlnServer);
		if (Kanal->ListeIdx >= 0)
			ProtokollierenInt_P(PSTR("%c): "), 'A' + Kanal->ListeIdx);
		else
			Protokollieren_P(PSTR("?): "));
		}
	else
		Protokollieren_P(PSTR("TlnSrv(-): "));
	}
	
	
static void ProtokollierenTlnServ_P(TTlnServKanal *Kanal, PGM_P s)
	{
	ProtokollierenTlnSrv(Kanal);
	Protokollieren_P(s);
	}


static void ProtokollierenTlnServInt_P(TTlnServKanal *Kanal, PGM_P s, long i)
	{
	ProtokollierenTlnSrv(Kanal);
	ProtokollierenInt_P(s, i);
	}


//! Bearbeitet die Aktualisierungsmeldung im eigenen Telefonbuch.
//---------------------------------------------------------------
//! \retval true, wenn Meldung akzeptiert wurde.
	
static bool TlnAktualisierung(TTlnServKanal *Kanal, TTlnServBuf *tsb, long TlnIP)
	{
	TTlnDaten TD;
	
	if (tsb->SelbstAkt.RufNr < GlobRufnrMinWert)
		{
		if (ProtokollLevelTlnServ >= 1)
			ProtokollierenTlnServInt_P(Kanal, PSTR("! Rufnummer %lu zu wenig Ziffern\r\n"), tsb->SelbstAkt.RufNr);
		return false;
		}
	else if (TlnSuche(tsb->SelbstAkt.RufNr, false, &TD))
		{ // Eintrag ist schon vorhanden
		if (TD.Flags & TlnFlag_Lokal)
			{
			if (ProtokollLevelTlnServ >= 1)
				ProtokollierenTlnServInt_P(Kanal, PSTR("! Teilnehmer %lu schon vorhanden, aber LOKAL\r\n"), TD.Nummer);
			return false;
			}
		else if (TD.Flags & TlnFlag_Gesperrt)
			{
			if (ProtokollLevelTlnServ >= 1)
				ProtokollierenTlnServInt_P(Kanal, PSTR("! Teilnehmer %lu schon vorhanden, aber noch nicht freigegeben\r\n"), TD.Nummer);
			return false;
			}
		else if (TD.AdrArt != TxpDynIP)
			{
			if (ProtokollLevelTlnServ >= 1)
				ProtokollierenTlnServInt_P(Kanal, PSTR("! Teilnehmer %lu schon vorhanden, aber nicht Typ 'dynamisch'\r\n"), TD.Nummer);
			return false;
			}
		else if (tsb->SelbstAkt.Pin != TD.DynPin)
			{
			ProtokollierenTlnServInt_P(Kanal, PSTR("! Teilnehmer %lu schon vorhanden, aber falsche Pin gesendet\r\n"), TD.Nummer);
				// immer speichern, auch bei abgeschaltetem Protokoll.
			return false;
			}
		else if (tsb->SelbstAkt.Port == TD.Port && TlnIP == TD.IPAdr)
			{
			if (ProtokollLevelTlnServ >= 2)
				ProtokollierenTlnServInt_P(Kanal, PSTR("Teilnehmer %lu schon vorhanden, unveraenderte Daten\r\n"), TD.Nummer);
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
					ProtokollierenTlnServInt_P(Kanal, PSTR("Teilnehmer %lu erfolgreich aktualisiert: IP "), TD.Nummer);
					ProtokollierenIPAdr(TlnIP);
					ProtokollierenInt_P(PSTR(" Port %u\r\n"), TD.Port);
					}
				TlnServTlnbuchEintragGeaendert(&TD, -1); // -1: Änderung kommt von keinem Server
				return true;
				}
			else
				{ // speichern war nicht erfolgreich
				ProtokollierenTlnServInt_P(Kanal, PSTR("! Aenderung Teilnehmer %lu konnte nicht gespeichert werden\r\n"), TD.Nummer);
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
				ProtokollierenTlnServInt_P(Kanal, PSTR("Teilnehmer %lu erfolgreich angelegt: IP "), TD.Nummer);
				ProtokollierenIPAdr(TlnIP);
				ProtokollierenInt_P(PSTR(" Port %u (noch gesperrt!)\r\n"), TD.Port);
				}
			Diagnoseausgabe_P(PSTR("Neuer Teilnehmer angemeldet"), 1);
			TlnServTlnbuchEintragGeaendert(&TD, -1); // da er neu war, muss er geändert worden sein.
				// -1: Kein Sync-Vorgang
			return true;
			}
		else
			{ // speichern war nicht erfolgreich
			ProtokollierenTlnServInt_P(Kanal, PSTR("! Neuer Teilnehmer %lu konnte nicht gespeichert werden\r\n"), TD.Nummer);
			return false;
			}
		} // Neuanlage erforderlich
		
	return true;
	}
	
	
static void FehlerRueckmelden(PGM_P Text, int val)
	{
	TlnServBuf.Code = TLNSERV_FEHLER;
	sprintf_P(TlnServBuf.PureData, Text, val);
	TlnServBuf.DataLen = strlen(TlnServBuf.PureData) + 1;
	}
	
	
//! Wird aufgerufen, wenn ein Eintrag im eigenen Telefonbuch geändert wird.
//---------------------------------------------------------------------------
//! Ziel ist, die Synchronisation dieses geänderten Eintrags anzustoßen.
void TlnServTlnbuchEintragGeaendert(TTlnDaten *Tln, int8_t VonServer)
	{
	if (Tln->Flags & TlnFlag_Lokal)
		return;
		
	bool SyncStichzeitWarOk = false;
	if (VonServer >= 0 
		&& VonServer < ANZ_TEILNEHMER_SERVER
		&& TlnServSyncStichzeit[VonServer] > TlnBuchLetzteAenderung)
		SyncStichzeitWarOk = true; 
		// d.h. die Daten des Servers, von dem eine ggf. Aktualisierung gerade
		// empfangen worden sind, waren bisher aktuell.
	    
	if (TlnBuchLetzteAenderung < Tln->Datum)
		TlnBuchLetzteAenderung = Tln->Datum;
		
	for (uint8_t i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
		{
		if (TlnServSyncStichzeit[i] > Tln->Datum && i != VonServer)
			TlnServSyncStichzeit[i] = Tln->Datum; 
			// dies kommt nur dann vor, wenn bereits eine Synchronisation stattfand,
			// die Datenbasis dafür aber bereits veraltet war.
			// -> SyncStichzeit wird nach hinten (alt) korrigiert.
			
		else if (i == VonServer 
				 && SyncStichzeitWarOk
				 && TlnServSyncStichzeit[i] <= TlnBuchLetzteAenderung)
			TlnServSyncStichzeit[i] = TlnBuchLetzteAenderung + 1;
			// dieser Server muss nicht aktualisiert werden, da von diesem Server
			// gerade die Daten empfangen werden und er vorher aus eigener Sicht 
			// keine Aktualisierung empfangen brauchte.
		
		}
		
	} // TlnServTlnbuchEintragGeaendert()
	
	
// Den nächsten Eintrag des Teilnehmer-Verzeichnisses senden.
// ----------------------------------------------------------
// ...wenn nicht lokal und Datum jünger als #AusgabeStichdatum.
	
static void TlnDatensatzSyncSenden(TTlnServKanal *Kanal)
	{
	if (!Kanal->AusgabeGestartet)
		{
		TlnListerStart(&Kanal->AusgabeLister);
		if (ProtokollLevelTlnServ >= 2)
			{
			ProtokollierenTlnServ_P(Kanal, PSTR("Starte Ausgabe der Teilnehmer-Eintraege\r\n"));
			}
		Kanal->AusgabeGestartet = true;
		}
		
	while (TlnListerNaechster(&Kanal->AusgabeLister, &TlnServBuf.TlnAuskunft))
		{
		// wenn nicht lokal UND gültige Nummer UND Datum jünger als Grenzwert, dann senden...
		if ((TlnServBuf.TlnAuskunft.Flags & TlnFlag_Lokal) == 0
			&& TlnServBuf.TlnAuskunft.Nummer >= GlobRufnrMinWert
		    && TlnServBuf.TlnAuskunft.Datum >= Kanal->AusgabeStichdatum)
			{
			// aber Nicht senden, wenn gelöscht und Löschdatum älter als 30 Tage
			if (TlnServBuf.TlnAuskunft.AdrArt == Geloescht 
				&& TlnServBuf.TlnAuskunft.Datum + 30L * 24 * 60 * 60 < TlnBuchLetzteAenderung)
				{
				ProtokollierenTlnServInt_P(Kanal, PSTR("Als geloescht markierter Teilnehmer-Eintrag %lu uebersprungen\r\n"), TlnServBuf.TlnAuskunft.Nummer);
				continue;
				}
			
			if (ProtokollLevelTlnServ >= 2)
				{
				ProtokollierenTlnServInt_P(Kanal, PSTR("Sende Teilnehmer-Eintrag %lu\r\n"), TlnServBuf.TlnAuskunft.Nummer);
				}
			TlnServBuf.Code = TLNSERV_AUSKUNFT_VERSION1;
			// TlnServBuf-TlnAuskunft ist bereits gefüllt.
			TlnServBuf.DataLen = sizeof(TlnServBuf.TlnAuskunft);
			return;
			}
		}

	// Kein zu sendender Datensatz mehr da...
	if (ProtokollLevelTlnServ >= 2)
		{
		ProtokollierenTlnServ_P(Kanal, PSTR("Sende Ende-Kennung der Teilnehmer-Eintraege\r\n"));
		}
	TlnServBuf.Code = TLNSERV_SYNC_ENDE;
	TlnServBuf.PureData[0] = '\0';
	TlnServBuf.DataLen = 1;
	Kanal->Fertig = true;
	} // TlnDatensatzSyncSenden()
		
		
//! Senden der Daten zum Socket.

static void SocketDatenSenden(TTlnServKanal *Kanal)
	{
	if (Kanal->SendeFehlerZaehler == 0
		|| KurzTimerVal(&Kanal->WiederholungVerzoegerung) > KurzTimerFreq * 15/10)

		{
		int Res = PutSocketData_RPE(Kanal->Socket, 2 + TlnServBuf.DataLen, TlnServBuf.Buf, RAM);
		// SocketLebenszeichenZaehler = 0; 

		if (ProtokollLevelTlnServ >= 3)
			{
			ProtokollierenTlnServInt_P(Kanal, PSTR("Socket Sendung: (%lu)" ), 2 + TlnServBuf.DataLen);
			ProtokollierenPuffer(TlnServBuf.Buf, 2 + TlnServBuf.DataLen);
			ProtokollierenInt_P(PSTR(" --> Res %d\r\n" ), Res);
			}

		if (Res <= 0)
			{
			Kanal->SendeFehlerZaehler++;
			if (Kanal->SendeFehlerZaehler >= 10)
				{
				if (ProtokollLevelTlnServ >= 1)
					ProtokollierenTlnServ_P(Kanal, PSTR("! Mehrfache Fehler beim Senden ins Netz, Socket wird geschlossen\r\n" ));
				CloseTCPSocket(Kanal->Socket);
				Kanal->Socket = NO_SOCKET_USED;
				SyncWarteEnde = (60 - Zufallswert(0xF)) * KurzTimerFreq; // 60 Sekunden warten.
				TeilnehmerServerFehlerSpeichern(Kanal->ListeIdx);
				}
			StartKurzTimer(&Kanal->WiederholungVerzoegerung);				
			}
		else if (Res < 2 + TlnServBuf.DataLen)
			{
			ProtokollierenTlnServ_P(Kanal, PSTR("! Sendung war NICHT VOLLSTAENDIG\r\n" ));
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

	// Auf neue Daten testen
	// ---------------------------------
	int InCount = GetBytesInSocketData(Kanal->Socket);
	bool Senden = false;

	if (InCount > 0) 
		{
		TTlnDaten TD;
		TlnDatenInit(&TD);
		
		int Res = GetSocketData(Kanal->Socket, InCount, TlnServBuf.Buf);
		
		if (ProtokollLevelTlnServ >= 3) // Daten explizit
			{
			ProtokollierenTlnServInt_P(Kanal, PSTR("Socket Empfang: (%ld/" ), InCount);
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
					{
					FehlerRueckmelden(PSTR("update not enough data: %u"), TlnServBuf.DataLen);
					Senden = true;
					}
				else
					{
					long MeldeIP = TCP_sockettable[Kanal->Socket].SourceIP;
					if (ProtokollLevelTlnServ >= 2)
						{
						ProtokollierenTlnServInt_P(Kanal, PSTR("Aktualisierung empfangen. Nummer %lu " ), TlnServBuf.SelbstAkt.RufNr);
						ProtokollierenInt_P(PSTR("Auth %u " ), TlnServBuf.SelbstAkt.Pin);
						ProtokollierenInt_P(PSTR("Port %u\r\n" ), TlnServBuf.SelbstAkt.Port);
						}
					
					if (TlnAktualisierung(Kanal, &TlnServBuf, MeldeIP))
						{
						// Antwort generieren:
						TlnServBuf.Code = TLNSERV_IPRUECKMELD;
						TlnServBuf.DataLen = sizeof(TlnServBuf.IpRueckm);
						TlnServBuf.IpRueckm.EmpfIP = MeldeIP;
						Senden = true;
						}
					else
						{
						FehlerRueckmelden(PSTR("forbidden"), 0);
						Senden = true;
						if (ProtokollLevelTlnServ >= 1)
							{
							ProtokollierenTlnServ_P(Kanal, PSTR("! abgewiesene Anfrage war von IP "));
							ProtokollierenIPAdr(MeldeIP);
							Protokollieren_P(PSTR("\r\n"));
							}
						}
					}
				Kanal->Fertig = true; // mehr wird da nicht kommen.
				break;
				
			case TLNSERV_ABFRAGE_VERSION1:
				if (TlnServBuf.DataLen < sizeof(TlnServBuf.TlnAbfr))
					{
					FehlerRueckmelden(PSTR("request not enough data: %u"), TlnServBuf.DataLen);
					Senden = true;
					}
				else
					{
					uint32_t RufNr = TlnServBuf.TlnAbfr.RufNr;
					if (ProtokollLevelTlnServ >= 2) 
						ProtokollierenTlnServInt_P(Kanal, PSTR("Abfrage empfangen. Nummer %lu: "), RufNr);
						
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
						Senden = true;
						if (ProtokollLevelTlnServ >= 2)
							Protokollieren_P(PSTR(" ...gefunden\r\n"));
						}
					else
						{ // Nummer nicht gefunden
						TlnServBuf.Code = TLNSERV_AUSKUNFT_NICHTVERG;
						TlnServBuf.DataLen = 0;
						Senden = true;
						if (ProtokollLevelTlnServ >= 2) 
							Protokollieren_P(PSTR("! ...nicht gefunden oder gesperrt\r\n"));
						}
					}
				Kanal->Fertig = true; // mehr wird da nicht kommen.
				break;

			case TLNSERV_AUSKUNFT_VERSION1:
				if (TlnServBuf.DataLen < sizeof(TlnServBuf.TlnAuskunft))
					{
					FehlerRueckmelden(PSTR("update not enough data: %u"), TlnServBuf.DataLen);
					Senden = true;
					}
				else if (!Kanal->Freigabe)
					{
					FehlerRueckmelden(PSTR("no authentification"), 0);
					Senden = true;
					}
				else
					{
					Res = TlnHinzufuegen(&TlnServBuf.TlnAuskunft, false);
					if (Res < 0)
						{
						ProtokollierenTlnServInt_P(Kanal, PSTR("! Datensatz vom Teilnehmer-Server mit Nr %lu konnte nicht gespeichert werden\r\n"), TlnServBuf.TlnAuskunft.Nummer);
						Diagnoseausgabe_P(PSTR("internes Rufnummern-Verzeichnis voll"), 2);
						FehlerRueckmelden(PSTR("abort"), 0);	
						Senden = true;
						}
					else
						{ // noch alles gut
						if (Res > 0)
							{
							if (ProtokollLevelTlnServ >= 1)
								ProtokollierenTlnServInt_P(Kanal, PSTR("Datensatz vom Teilnehmer-Server mit Nr %lu empfangen und gespeichert\r\n"), TlnServBuf.TlnAuskunft.Nummer);
							TlnServTlnbuchEintragGeaendert(&TlnServBuf.TlnAuskunft, Kanal->ListeIdx);
							}
						else // Res == 0
							{
							if (ProtokollLevelTlnServ >= 2)
								ProtokollierenTlnServInt_P(Kanal, PSTR("Datensatz vom Teilnehmer-Server mit Nr %lu empfangen, keine Aenderung\r\n"), TlnServBuf.TlnAuskunft.Nummer);
							}
						// Antwort generieren:
						TlnServBuf.Code = TLNSERV_SYNC_QUITTUNG;
						TlnServBuf.DataLen = 0;
						Senden = true;
						}
					} // SyncFreigabe ok
				break;
				
			case TLNSERV_SYNC_TOTALABFRAGE:
				if (TlnServBuf.DataLen < sizeof(TlnServBuf.SyncAnmeldung))
					{
					FehlerRueckmelden(PSTR("login not enough data: %u"), TlnServBuf.DataLen);
					Senden = true;
					}
				else if (TlnServBuf.SyncAnmeldung.Geheimzahl != TlnServSyncGeheimzahl)
					{
					FehlerRueckmelden(PSTR("wrong authentification"), 0);
					Senden = true;
					Diagnoseausgabe_P(PSTR("Teilnehmer-Server Anmeldung mit falscher Geheimzahl"), 1);
					}
				else
					{
					Kanal->Freigabe = true;
					Kanal->AusgabeStichdatum = 0; // = alle 
					Kanal->AusgabeGestartet = false; // wird aber gleich gestartet
					TlnDatensatzSyncSenden(Kanal);
					Senden = true;
					}
				break; 
				
			case TLNSERV_SYNC_ANMELDUNG:
				if (TlnServBuf.DataLen < sizeof(TlnServBuf.SyncAnmeldung))
					{
					FehlerRueckmelden(PSTR("login not enough data: %u"), TlnServBuf.DataLen);
					Senden = true;
					}
				else if (TlnServBuf.SyncAnmeldung.Geheimzahl != TlnServSyncGeheimzahl)
					{
					FehlerRueckmelden(PSTR("wrong authentification"), 0);
					Senden = true;
					Diagnoseausgabe_P(PSTR("Teilnehmer-Server Anmeldung mit falscher Geheimzahl"), 1);
					}
				else
					{
					Kanal->Freigabe = true;
					
					// Quittung senden:
					TlnServBuf.Code = TLNSERV_SYNC_QUITTUNG;
					TlnServBuf.DataLen = 0;
					Senden = true;
					}
				break;
			
			case TLNSERV_SYNC_QUITTUNG:
				if (!Kanal->Freigabe)
					{
					FehlerRueckmelden(PSTR("no authentification"), 0);
					Senden = true;
					}
				else if (!Kanal->AusgabeGestartet)
					{
					FehlerRueckmelden(PSTR("unexpected acknowledge"), 0);
					TeilnehmerServerFehlerSpeichern(Kanal->ListeIdx);
					Senden = true;
					}
				else
					{
					TlnDatensatzSyncSenden(Kanal);
					Senden = true;
					}
				
				break;
			
			case TLNSERV_SYNC_ENDE:
				CloseTCPSocket(Kanal->Socket);
				Kanal->Socket = NO_SOCKET_USED;
				if (ProtokollLevelTlnServ >= 2) 
					{
					ProtokollierenTlnServ_P(Kanal, PSTR("Ende Kennung empfangen, Socket wird geschlossen\r\n"));
					}		
				if (Kanal->IstInitialAbfrage)
					{
					struct TIME CurTime;
					CLOCK_GetTime(&CurTime);
					for (uint8_t i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
						{
						if (i == Kanal->ListeIdx)
							TlnServSyncStichzeit[i] = CurTime.time; 
						else
							TlnServSyncStichzeit[i] = CurTime.time - 60 * 60; 
							// relativ neue Einträge (nicht älter als eine Stunde) 
							// doch weiterverteilen.
						}
					InitialAbfrageStarten = false;
					if (ProtokollLevelTlnServ >= 1) 
						{
						ProtokollierenTlnServ_P(Kanal, PSTR("Initiale Abfrage erfolgreich beendet\r\n"));
						}		
					}
				SyncWarteEnde = (120 - Zufallswert(0x3F)) * KurzTimerFreq; // 2 Minuten warten.
				break;
				
			case TLNSERV_FEHLER:
				CloseTCPSocket(Kanal->Socket);
				Kanal->Socket = NO_SOCKET_USED;
				if (ProtokollLevelTlnServ >= 1) 
					{
					ProtokollierenTlnServ_P(Kanal, PSTR("Fehlermeldung empfangen, Socket wird geschlossen: "));
					Protokollieren(TlnServBuf.PureData);
					Protokollieren_P(PSTR("\r\n"));
					}		
				SyncWarteEnde = (30 + Zufallswert(0xF)) * KurzTimerFreq; // 30 Sekunden warten.
				if (Kanal->ListeIdx >= 0)
					{
					TlnServSyncStichzeit[Kanal->ListeIdx] = Kanal->AusgabeStichdatum; 
						// wegen des Fehlers alles noch mal senden.
					TeilnehmerServerFehlerSpeichern(Kanal->ListeIdx);
						// und Fehler merken, so dass bei Wiederholung der Fehlermeldung 
						// dieser Server bald nicht mehr berücksichtigt wird.
					}
					
				break;
			
			case TLNSERV_IPRUECKMELD: // ist ein Fehler, da dieses Telegramm nur eine Antwort des Servers sein kann.
			case TLNSERV_AUSKUNFT_NICHTVERG: // ist ein Fehler, da dieses Telegramm nur eine Antwort des Servers sein kann.
			default:
				FehlerRueckmelden(PSTR("unknown code %02X"), TlnServBuf.Code);
				Senden = true;
				break;

			}
			
		if (ProtokollLevelTlnServ >= 1 && Senden && TlnServBuf.Code == TLNSERV_FEHLER)
			{
			ProtokollierenTlnServ_P(Kanal, PSTR("! Error "));
			Protokollieren(TlnServBuf.PureData);
			Protokollieren_P(PSTR("\r\n"));
			}

		} // if GetBytesInSocketData > 0
		
	// soll offene Verbindung geschlossen werden?
	if (CheckSocketState(Kanal->Socket) == SOCKET_NOT_USE)
		{
		if (ProtokollLevelTlnServ >= 2)
			ProtokollierenTlnServ_P(Kanal, PSTR("Socket wurde von Gegenstelle geschlossen\r\n"));
		CloseTCPSocket(Kanal->Socket);
		Kanal->Socket = NO_SOCKET_USED;
		if (!Kanal->Fertig && Kanal->ListeIdx >= 0)
			{ // unerwartetes Ende...
			if (Kanal->AusgabeGestartet)
				TlnServSyncStichzeit[Kanal->ListeIdx] = Kanal->AusgabeStichdatum; 
				//!< wegen des Fehlers alles noch mal senden.
				
			TeilnehmerServerFehlerSpeichern(Kanal->ListeIdx);
			}
		
		// da ursache nicht bekannt, kein SyncWarteEnde = X * KurzTimerFreq; // X Sekunden warten.		
		return;
		}
		
	// Daten ggf. ins Netz senden
	// --------------------------------------------------
	if (Senden)
		SocketDatenSenden(Kanal);
	
	} // SocketBearbeiten()

	

static bool InitialAbfrageKanalOeffnen()
	{
	if (TlnServer[0].Socket != NO_SOCKET_USED)
		return false;
	
	for (int8_t i = ANZ_TEILNEHMER_SERVER - 1 ; i >= 0 ; i--)
		{
		int NewSock = TeilnehmerServerSocketOeffnen1(i, PSTR("Initialabfrage")); 
			// da wird auch Protokoll geschrieben.
			
		if (NewSock != -1)
			{
			KanalInit(&TlnServer[0], NewSock); 
			TlnServer[0].ListeIdx = i; 
			if (ProtokollLevelTlnServ >= 2)
				{
				ProtokollierenTlnServ_P(&TlnServer[0], PSTR("Socket geoeffnet, initiale Abfrage nach Reset begonnen\r\n"));
				}
			TlnServBuf.Code = TLNSERV_SYNC_TOTALABFRAGE;
			TlnServBuf.DataLen = sizeof(TlnServBuf.SyncAnmeldung);
			TlnServBuf.SyncAnmeldung.Version = 1; // gibt erst mal nix anderes.
			TlnServBuf.SyncAnmeldung.Geheimzahl = TlnServSyncGeheimzahl;
			SocketDatenSenden(&TlnServer[0]);
			TlnServer[0].Freigabe = true; // wer anruft weiß wen er anruft.
			TlnServer[0].IstInitialAbfrage = true;
			return true;
			}
		}
		
	return false;
	}


static bool SyncMeldungKanalOeffnen()
	{
	uint8_t i;
	
	if (TlnServer[0].Socket != NO_SOCKET_USED)
		return false;
		
	for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
		{
		if (TlnServSyncStichzeit[i] > TlnBuchLetzteAenderung)
			continue;
			
		if (TeilnehmerServerAdresse[i][0] == '\0')
			continue;

		int NewSock = TeilnehmerServerSocketOeffnen1(i, PSTR("Sync-Meldung"));
			// da wird auch Protokoll geschrieben.
			
		if (NewSock != -1)
			{
			struct TIME CurTime;
			CLOCK_GetTime(&CurTime);
			KanalInit(&TlnServer[0], NewSock); 
			TlnServer[0].ListeIdx = i;
			TlnServer[0].Freigabe = true; // der Anrufer ist immer ok
			TlnServer[0].AusgabeStichdatum = TlnServSyncStichzeit[i];
			TlnServer[0].AusgabeGestartet = true;
			TlnServSyncStichzeit[i] = CurTime.time; 
				//! Wird wieder auf AusgabeStichdatum zurückgesetzt werden, wenn Fehler passiert.
			TlnListerStart(&TlnServer[0].AusgabeLister);
			if (ProtokollLevelTlnServ >= 2)
				{
				ProtokollierenTlnServ_P(&TlnServer[0], PSTR("Socket geoeffnet zur Ausgabe der geaenderten Teilnehmer-Eintraege\r\n"));
				}
				
			TlnServBuf.Code = TLNSERV_SYNC_ANMELDUNG;
			TlnServBuf.DataLen = sizeof(TlnServBuf.SyncAnmeldung);
			TlnServBuf.SyncAnmeldung.Version = 1; // gibt erst mal nix anderes.
			TlnServBuf.SyncAnmeldung.Geheimzahl = TlnServSyncGeheimzahl;
			SocketDatenSenden(&TlnServer[0]);
			return true;
			}
		// sonst den nächsten probieren...
		}
		
	return false; // weil nix zu tun ist...
	}
	
	
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
	uint8_t i;
	
	// ======================================================================
	// Socket Empfang und Sendung
	// ======================================================================

	for (i = 0 ; i < AnzTlnServKanaele ; i++)
		if (TlnServer[i].Socket != NO_SOCKET_USED)
			SocketBearbeiten(&TlnServer[i]);
		
	// ==========================================================================
	// Neuer Anruf vom Ethernet?
	// ==========================================================================

	// auf neue Verbindungsanfrage testen
	int NewServerSocket = CheckPortRequest(TXP_TLNSERV_PORT);
	bool Ok = false;
	
	if (NewServerSocket != NO_SOCKET_USED)
		{
		for (i = 0 ; i < AnzTlnServKanaele ; i++)
			if (TlnServer[i].Socket == NO_SOCKET_USED)
				break;
				
		if (i >= AnzTlnServKanaele)
			{
			FehlerRueckmelden(PSTR("occupied"), 0);
			PutSocketData_RPE(NewServerSocket, 2 + TlnServBuf.DataLen, TlnServBuf.Buf, RAM);
			Ok = false;
			}
		else
			{
			KanalInit(&TlnServer[i], NewServerSocket);
			for (uint8_t k = 0 ; k < ANZ_TEILNEHMER_SERVER ; k++)
				if (TeilnehmerServerIP[k] == TCP_sockettable[NewServerSocket].SourceIP)
					{
					TlnServer[i].ListeIdx = k;
					break;
					}
			Ok = true;
			}

		if (ProtokollLevelTlnServ >= (Ok ? 2 : 1))
			{
			ProtokollierenTlnServ_P(&TlnServer[i], PSTR("Server-Socket geoeffnet von IP "));
			ProtokollierenIPAdr(TCP_sockettable[NewServerSocket].SourceIP);
			Protokollieren_P(PSTR(" / MAC "));
			ProtokollierenMAC(TCP_sockettable[NewServerSocket].MACadress);
			Protokollieren_P(Ok ? PSTR(" ...ok\r\n") : PSTR("! ...ABGEWIESEN wegen alle besetzt\r\n" ));
			}
			
		if (!Ok)
			CloseTCPSocket(NewServerSocket);

		}

	// ==========================================================================
	// Timeouts?
	// ==========================================================================

	//! \todo Prüfen, ob die System-Timeouts immer richtig wirken...
	
	// ==========================================================================
	// Neue Aktionen starten?
	// ==========================================================================

	if (TlnServer[0].Socket != NO_SOCKET_USED 
		|| TlnServer[1].Socket != NO_SOCKET_USED
		|| TlnServSyncGeheimzahl == 0) // ist nicht als Teilnehmer-Server konfiguriert
		StartKurzTimer(&SyncWarteTimer); // Warten bis mindestens zwei Verbindungen geschlossen sind

	else if (KurzTimerVal(&SyncWarteTimer) > SyncWarteEnde)
		{
		if (InitialAbfrageStarten)
			{
			if (!InitialAbfrageKanalOeffnen())
				{
				SyncWarteEnde = (30 + Zufallswert(0xF)) * KurzTimerFreq;
				}
			StartKurzTimer(&SyncWarteTimer);
			}
		else
			{
			if (!SyncMeldungKanalOeffnen())
				{
				SyncWarteEnde = (20 + Zufallswert(0x7)) * KurzTimerFreq;
				}
			StartKurzTimer(&SyncWarteTimer);
			}
		} // kein Socket offen und Wartezeit abgelaufen.
	
	} // txp_tlnserv_thread
	
	
// ================================================================================	

//! Gibt relevante Prozessdaten auf der HTML-Seite "Debug-Info" aus
void TlnServDebugPrint()
	{
	
#define PRINTVAL(Var) printf_P(PSTR("<br>" #Var " = %u"), Var)
	
	struct TIME Time;
	uint8_t i;
	
	CLOCK_GetTime(&Time); // holt auch die aktuelle Zeitzone

	for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
		{
		Time.time = TlnServSyncStichzeit[i];
		CLOCK_decode_time(&Time);
			
		printf_P(PSTR("<br>TlnServSyncStichzeit(%s) = %02u.%02u.%04u %02d:%02d:%02d"), TeilnehmerServerAdresse[i], Time.DD, Time.MM, Time.YY, Time.hh, Time.mm, Time.ss);
		}
		
	for (i = 0 ; i < AnzTlnServKanaele ; i++)
		printf_P(PSTR("<br>TlnServerKanal %d wurde %lu mal genutzt."), i, TlnServer[i].NutzungZaehler);
		
	Time.time = TlnBuchLetzteAenderung;
	CLOCK_decode_time(&Time);
			
	printf_P(PSTR("<br>TlnBuchLetzteAenderung = %02u.%02u.%04u %02d:%02d:%02d"), Time.DD, Time.MM, Time.YY, Time.hh, Time.mm, Time.ss);
	
	PRINTVAL(KurzTimerVal(&SyncWarteTimer));
	PRINTVAL(SyncWarteEnde);
	PRINTVAL(InitialAbfrageStarten);
#undef PRINTVAL
	}
	


/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Initialisiert den Teilnehmerauskunft-Server-clinet und registriert den Port auf welchen dieser lauschen soll.
 * \param 	NONE
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void txp_tlnserv_init()
	{
	uint8_t i;
	
	// EEPROM auslesen
	/*
	
	char Buf[TlnAdresseMax];

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

	for (i = 0 ; i < AnzTlnServKanaele ; i++)
		{
		TlnServer[i].Socket = NO_SOCKET_USED;
		TlnServer[i].NutzungZaehler = 0;
		}
	
	RegisterTCPPort(TXP_TLNSERV_PORT);
	
	printf_P( PSTR("Txp TlnServer Port %u.\r\n") , TXP_TLNSERV_PORT );
	
	for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
		{
		TlnServSyncStichzeit[i] = 0;
		}
		
		
	TlnBuchLetzteAenderung = 0;
	TTlnDaten TD;
	TlnDatenInit(&TD);

	TTlnListerDat LD;
	if (TlnListerStart(&LD))
		{
		while (TlnListerNaechster(&LD, &TD))
			{
			if (TD.Datum > TlnBuchLetzteAenderung && ((TD.Flags & TlnFlag_Lokal) == 0))
				TlnBuchLetzteAenderung = TD.Datum;
			}
		}

	StartKurzTimer(&SyncWarteTimer);
	SyncWarteEnde = 10 * KurzTimerFreq; // 10 Sekunden
	InitialAbfrageStarten = true;
	
	THREAD_RegisterThread( txp_tlnserv_thread, PSTR("TlnSrv"));
	}


#endif //def TXP_TLNSERVER

#endif //def TELEXPHONE

