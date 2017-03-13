/*! \file TlnServer.c \brief Anwendung zur Einbettung in das iTelex-System */
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

//*****************************************************************************
// Typische Abläufe mit dem Teilnehmer-Server:
// *******************************************
//
// 1. Meldung einer Endstelle zur Ermittlung der IP-Adresse der Endstelle.
// =======================================================================
// Endstelle (Client)				 -		Server
// ------------------------------------------------
// TLNSERV_SELBSTAKT (01)			-->		
//									<--		TLNSERV_IPRUECKMELD (02)
// (schließt Verbindung)
// 
// 2. Abfrage einer konkteten Rufnummer
// ====================================
// Endstelle (Client)	 			 -		Server
// ------------------------------------------------
//	TLNSERV_ABFRAGE (03)	        -->
//									<--		TLNSERV_AUSKUNFT_NICHTVERG (04)
//											(wenn nicht bekannt)
//												oder
//									<--		TLNSERV_AUSKUNFT_VERSION1 (05)
//											(wenn gefunden)
// (schließt Verbindung)
//
// 3. Voll-Abfrage Server - Server
// ================================
// Server (als Client)	 			 -		Server
// ------------------------------------------------
// TLNSERV_SYNC_VOLLABFRAGE (06)	-->
//									<--		TLNSERV_AUSKUNFT_VERSION1 (05)
// TLNSERV_SYNC_QUITTUNG (08)		-->
//									<--		TLNSERV_AUSKUNFT_VERSION1 (05)
//									...
// TLNSERV_SYNC_QUITTUNG (08)		-->
//									<--		TLNSERV_SYNC_ENDE (09)
// (schließt Verbindung)
//							
// 
// 4. Meldung von Änderungen von Server an Server (Sync-Meldung)
// ==============================================
// Server (als Client)	 			 -		Server
// ------------------------------------------------
// TLNSERV_SYNC_ANMELDUNG (07)		-->
//									<--		TLNSERV_SYNC_QUITTUNG (08)
// TLNSERV_AUSKUNFT_VERSION1 (05)	-->
//									<--		TLNSERV_SYNC_QUITTUNG (08)
//									...
// TLNSERV_AUSKUNFT_VERSION1 (05)	-->
//									<--		TLNSERV_SYNC_QUITTUNG (08)
// TLNSERV_SYNC_ENDE (09)			-->
// 											(schließt Verbindung)
//
// 5. Suchabfrage von Teilnehmer an Server ("Auskunft")
// ====================================================
// Endstelle (Client)	 			 -		Server
// ------------------------------------------------
// TLNSERV_SUCHE (0A)				-->
//									<--		TLNSERV_AUSKUNFT_VERSION1 (05)
// TLNSERV_SYNC_QUITTUNG (08)		-->
//									<--		TLNSERV_AUSKUNFT_VERSION1 (05)
//									...
// TLNSERV_SYNC_QUITTUNG (08)		-->
//									<--		TLNSERV_SYNC_ENDE (09)
// (schließt Verbindung)
//
// 
//*****************************************************************************


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

#ifdef iTelex

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

#include "iTelex.h"

#include "StringTab.h"


#ifdef ITELEX_TLNSERVER

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
	bool Freigabe; //!< Korrekte Autentifizierung empfangen.
	bool IstVollAbfrage; //!< Dieser Kanal wurde geöffnet, um das Teilnehmer-Verzeichnis (z.B. nach 
						    //!< Neustart) vollständig abzufragen.
	bool AusgabeGestartet; //!< gespeicherte Adressen werden an den Gegenüber gesendet.
	uint16_t AnzahlAktualisiert; //!< Zählt die durch den Abgleich geänderten Einträge.
	TTlnListerDat AusgabeLister; //!< Daten für die Ausgabe (welcher Datensatz wurde zuletzt gesendet)
	long AusgabeStichdatum; //!< Nur Einträge, die neuer oder gleich alt als X sind werden gesendet.
	char SuchMuster[TlnNameMax]; //!< Nur Einträge, die zum Suchmuster passen, werden gesendet.
						//!< Wenn leerer String, werden alle gesendet.
	bool Fertig; //!< true, wenn Auftrag erfüllt. Sonst wäre ein vorzeitiges Ende ein Fehler.
	uint8_t SendeFehlerZaehler;	//!< Zählt bis 10 bei nicht erfolgreichen Sendeversuchen auf dem Socket.
	TKurzTimer WiederholungVerzoegerung;
		//!< Bei spontanem Verbindungsabbau oder Sendestörung wird 2 Sekunden auf den nächsten 
		//!< Versuch gewartet.
	TKurzTimer SelbstAbbauVerzoegerung;
		//!< Wenn #AbbauStatus = #WarteEnde ist, wird nach 5 Sekunden selber die Verbindung getrennt.
	} TTlnServKanal;


enum { AnzTlnServKanaele = 4 } ;
	//!< Anzahl der möglichen TCP-Sockets, die gleichzeitig in Verbindung mit 
	//!< Teilnehmer-Server Aufgaben geöffnet sein können.


static TTlnServKanal TlnServer[AnzTlnServKanaele];
	//!< Teilnehmer-Server kanäle ein- und ausgehende TlnAbfrage-Verbindungen (Sockets).
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
	//!< \b gesendet. Beim nächsten Synchronisieren werden die Einträge gesendet, die 
	//!< neuer oder gleich alt sind.
	
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

	
static TLangTimer VollAbfrageTimer;
	//!< nach Reset 1 Minute, bis erfolgreich von einem anderen Teilnehmerauskunft-Server 
	//!< alle Daten abgeholt worden sind.
	//!< Danach 2 bis 4 Tage für regelmäßige Vergleiche "zur Sicherheit"
	
uint16_t VollAbfrageTimerEnde; // für HACK bei Taste wurde static entfernt.
	//!< Ende der Wartezeit für Vollabfrage. Siehe #VollAbfrageTimer.
	
static uint8_t VollAbfrageServerIndex;
	//!< Partner für die nächste Vollabfrage.
		

static uint32_t TlnServAbfrageZaehler;
	//!< Zählt wie oft der Teilnehmerserver von Nutzern abgefragt wird.
	
	
static void KanalInit(TTlnServKanal* k, int aSocket)
	{
	k->Socket = aSocket;
	k->ListeIdx = -1;
	k->Freigabe = false;
	k->AusgabeGestartet = false;
	k->AusgabeStichdatum = 0;
	k->IstVollAbfrage = false;
	k->Fertig = false;
	k->SendeFehlerZaehler = 0;
	k->AnzahlAktualisiert = 0;
	k->NutzungZaehler++;
	k->SuchMuster[0] = '\0'; // alle ausgeben
	StartKurzTimer(&k->WiederholungVerzoegerung);
	StartKurzTimer(&k->SelbstAbbauVerzoegerung);
	}


static void KanalFertig(TTlnServKanal* k)
	{
	if (!k->Fertig)
		StartKurzTimer(&k->SelbstAbbauVerzoegerung);
	k->Fertig = true;
	}
	
	
//! Leitet eine Protokollzeile des Teilnehmerservers ein.
//-------------------------------------------------------
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
		if (ProtokollLevelTlnServ >= NurFehler)
			ProtokollierenTlnServInt_P(Kanal, PSTR("* Rufnummer %lu zu wenig Ziffern\r\n"), tsb->SelbstAkt.RufNr);
		return false;
		}
	else if (TlnSuche(tsb->SelbstAkt.RufNr, false, &TD))
		{ // Eintrag ist schon vorhanden
		if (TD.Flags & TlnFlag_Lokal)
			{
			if (ProtokollLevelTlnServ >= NurFehler)
				ProtokollierenTlnServInt_P(Kanal, PSTR("! Teilnehmer %lu schon vorhanden, aber LOKAL\r\n"), TD.Nummer);
			return false;
			}
		else if (TD.Flags & TlnFlag_Gesperrt)
			{
			if (ProtokollLevelTlnServ >= NurFehler)
				ProtokollierenTlnServInt_P(Kanal, PSTR("* Teilnehmer %lu schon vorhanden, aber noch nicht freigegeben\r\n"), TD.Nummer);
			return false;
			}
		else if (TD.AdrArt != iTelexDynIP)
			{
			if (ProtokollLevelTlnServ >= NurFehler)
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
			if (ProtokollLevelTlnServ >= AblaufInfo)
				ProtokollierenTlnServInt_P(Kanal, PSTR("Teilnehmer %lu schon vorhanden, unveraenderte Daten\r\n"), TD.Nummer);
			return true; // da zulässige Aktualisierung
			}
		else
			{ // jetzt wirklich aktualisieren
			TD.IPAdr = TlnIP;
			TD.Port = tsb->SelbstAkt.Port;
			
			if (TlnHinzufuegen(&TD, TlnHinzDatumAktualisieren) >= 0)
				{
				if (ProtokollLevelTlnServ >= NurFehler) // ausnahmsweise
					{
					ProtokollierenTlnServInt_P(Kanal, PSTR("Teilnehmer %lu erfolgreich aktualisiert: IP "), TD.Nummer);
					ProtokollierenIPAdr(TlnIP);
					ProtokollierenInt_P(PSTR(" Port %u\r\n"), TD.Port);
					}
				TlnServTlnbuchEintragGeaendert(&TD, -1); // -1: Änderung kommt von keinem Server
				Kanal->AnzahlAktualisiert++;
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
		TD.AdrArt = iTelexDynIP;
		TD.IPAdr = TlnIP;
		TD.Port = tsb->SelbstAkt.Port;
		TD.DynPin = tsb->SelbstAkt.Pin;

		if (TlnHinzufuegen(&TD, TlnHinzDatumAktualisieren) >= 0)
			{
			if (ProtokollLevelTlnServ >= NurFehler) // ausnahmsweise
				{
				ProtokollierenTlnServInt_P(Kanal, PSTR("Teilnehmer %lu erfolgreich angelegt: IP "), TD.Nummer);
				ProtokollierenIPAdr(TlnIP);
				ProtokollierenInt_P(PSTR(" Port %u (noch gesperrt!)\r\n"), TD.Port);
				}
			Diagnoseausgabe_P(ISTR(NeuTeilnehmer, LokaleSprache), 1);
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
	

//! Erstellt eine Fehlermeldung im "codierten" Modus des #TlnServBuf
//------------------------------------------------------------------
	
static void FehlerRueckmelden(PGM_P Text, int val)
	{
	TlnServBuf.Code = TLNSERV_FEHLER;
	sprintf_P(TlnServBuf.PureData, Text, val);
	TlnServBuf.DataLen = strlen(TlnServBuf.PureData) + 1;
	}


//! Erstellt die Rückmeldung zu einer Teilnehmer-Auskunft im Ascii-Modus
//----------------------------------------------------------------------

static void AsciiTlnAuskunft(TTlnDaten *td)
	{
	switch (td->AdrArt)
		{
		sprintf_P(TlnServBuf.Buf, PSTR("ok\r\n%lu\r\n%s\r\n%d\r\n"), td->Nummer, td->Name, td->AdrArt); // wird ggf. wieder überschrieben.
		case iTelexHostname:
		case AsciiHostname:
			sprintf_P(TlnServBuf.Buf + strlen(TlnServBuf.Buf), PSTR("%s\r\n%d\r\n%d\r\n+++\r\n"), td->Adresse, td->Port, td->Durchwahl);
			break;
		case iTelexDynIP:
		case iTelexIP:
		case AsciiIP:
			iptostr(td->IPAdr, TlnServBuf.Buf + strlen(TlnServBuf.Buf));
			sprintf_P(TlnServBuf.Buf + strlen(TlnServBuf.Buf), PSTR("\r\n%d\r\n%d\r\n+++\r\n"), td->Port, td->Durchwahl);
			break;
		case eMail:
			sprintf_P(TlnServBuf.Buf + strlen(TlnServBuf.Buf), PSTR("%s\r\n+++\r\n"), td->Adresse);
			break;
		default:
			sprintf_P(TlnServBuf.Buf, PSTR("fail\r\n%lu\r\nwrong type\r\n+++\r\n"), td->Nummer); 
			break;
		}
	} // AsciiTlnAuskunft()



	
	
//! Wird aufgerufen, wenn ein Eintrag im eigenen Telefonbuch geändert wird.
//---------------------------------------------------------------------------
//! Ziel ist, die Synchronisation dieses geänderten Eintrags anzustoßen.
//! \param Tln: Zeiger auf den geänderten Datensatz. Es wird nur TlnFlag_Lokal und Datum verwendet.
//! \param VonServer: Von welchem Teilnehmer-Server kommt die Änderung, oder -1 für unbekannte Quelle.
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
		// empfangen worden sind, müssten eigentlich vollständig aktuell sein.
	    
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
				 && TlnServSyncStichzeit[i] <= Tln->Datum)
			TlnServSyncStichzeit[i] = Tln->Datum + 1;
			// dieser Server muss nicht aktualisiert werden, da von diesem Server
			// gerade die Daten empfangen werden und er vorher aus eigener Sicht 
			// keine Aktualisierung empfangen brauchte.
		
		}
		
	} // TlnServTlnbuchEintragGeaendert()
	
	
//! Den nächsten Eintrag des Teilnehmer-Verzeichnisses senden.
// ----------------------------------------------------------
//! ...wenn nicht lokal und Datum jünger als #AusgabeStichdatum und wenn Muster zum Namen passt.
	
static void TlnDatensatzSyncSenden(TTlnServKanal *Kanal)
	{
	if (!Kanal->AusgabeGestartet)
		{
		TlnListerStart(&Kanal->AusgabeLister);
		if (ProtokollLevelTlnServ >= AblaufInfo)
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
		    && TlnServBuf.TlnAuskunft.Datum >= Kanal->AusgabeStichdatum
			&& TlnSuchMusterPasst(Kanal->SuchMuster, &TlnServBuf.TlnAuskunft))
			{
			// aber Nicht senden, wenn gelöscht und Löschdatum älter als 20 Tage
			if (TlnServBuf.TlnAuskunft.AdrArt == Geloescht 
				&& TlnServBuf.TlnAuskunft.Datum + 20L * 24 * 60 * 60 < TlnBuchLetzteAenderung)
				{
				ProtokollierenTlnServInt_P(Kanal, PSTR("Als geloescht markierter Teilnehmer-Eintrag %lu uebersprungen\r\n"), TlnServBuf.TlnAuskunft.Nummer);
				continue;
				}
		
			if (!Kanal->Freigabe)
				{
				TlnServBuf.TlnAuskunft.DynPin = 0; // Datenschutz
				}
				
			if (ProtokollLevelTlnServ >= AblaufInfo)
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
	if (ProtokollLevelTlnServ >= AblaufInfo)
		{
		ProtokollierenTlnServ_P(Kanal, PSTR("Sende Ende-Kennung der Teilnehmer-Eintraege\r\n"));
		}
	TlnServBuf.Code = TLNSERV_SYNC_ENDE;
	TlnServBuf.PureData[0] = '\0';
	TlnServBuf.DataLen = 1;
	KanalFertig(Kanal);
	} // TlnDatensatzSyncSenden()
		
		
//! Senden der Daten zum Socket.
//------------------------------

static void SocketDatenSenden(TTlnServKanal *Kanal)
	{
	if (Kanal->SendeFehlerZaehler == 0
		|| KurzTimerVal(&Kanal->WiederholungVerzoegerung) > KurzTimerFreq * 15/10)

		{
		uint16_t LenToSend;
		if (TlnServBuf.Buf[0] >= 'a' && TlnServBuf.Buf[0] <= 'z') // Ascii-Daten senden
			LenToSend = strlen(TlnServBuf.Buf);
		else
			LenToSend = 2 + TlnServBuf.DataLen;
		
		int Res = PutSocketData_RPE(Kanal->Socket, LenToSend, TlnServBuf.Buf, RAM);
		// SocketLebenszeichenZaehler = 0; 

		if (ProtokollLevelTlnServ >= DatenDetailliert)
			{
			ProtokollierenTlnServInt_P(Kanal, PSTR("Socket Sendung: (%lu)" ), LenToSend);
			ProtokollierenPuffer(TlnServBuf.Buf, LenToSend);
			ProtokollierenInt_P(PSTR(" --> Res %d\r\n" ), Res);
			}

		if (Res <= 0)
			{
			Kanal->SendeFehlerZaehler++;
			if (Kanal->SendeFehlerZaehler >= 10)
				{
				if (ProtokollLevelTlnServ >= NurFehler)
					ProtokollierenTlnServ_P(Kanal, PSTR("! Mehrfache Fehler beim Senden ins Netz, Socket wird geschlossen\r\n" ));
				CloseTCPSocket(Kanal->Socket);
				Kanal->Socket = NO_SOCKET_USED;
				SyncWarteEnde = (60 - Zufallswert(0xF)) * KurzTimerFreq; // 60 Sekunden warten.
				TeilnehmerServerFehlerSpeichern(Kanal->ListeIdx);
				}
			StartKurzTimer(&Kanal->WiederholungVerzoegerung);				
			}
		else if (Res < LenToSend)
			{
			ProtokollierenTlnServ_P(Kanal, PSTR("! Sendung war NICHT VOLLSTAENDIG\r\n" ));
			}

		} // if es gibt was zu senden
	} // SocketDatenSenden()
		
	
//! Socket für Teilnehmerauskunft-Server bearbeiten.
//---------------------------------------------------------------------------
//! Aufgaben:
//! - Empfangene Daten in den Socket-Empfangspuffer schreiben
//! - Daten vom Socket-Empfangspuffer übersetzen in den SendePuffer (zum Endgerät).
//! - Daten des Empfangspuffers (Endgerät) in den Socket-Sendepuffer übersetzen
//! - Daten des Socket-Sendepuffers ggf. senden
//! - Schlusszeichen bearbeiten

static void SocketBearbeiten(TTlnServKanal *Kanal)
	{
	if (Kanal->Socket == NO_SOCKET_USED)
		return;

	// Auf neue Daten testen
	// ---------------------------------
	int InCount = GetBytesInSocketData(Kanal->Socket);
	bool Senden = false;

	if (InCount > sizeof(TlnServBuf))
		{
		int Res = GetSocketData(Kanal->Socket, sizeof(TlnServBuf), TlnServBuf.Buf);
		if (ProtokollLevelTlnServ >= NurFehler)
			{
			ProtokollierenTlnServInt_P(Kanal, PSTR("! Socket Empfang UEBERLAUF (%ld)" ), InCount);
			if (Res > 0)
				ProtokollierenPuffer(TlnServBuf.Buf, Res);
			Protokollieren_P(PSTR(" -> verworfen, Socket geschlossen\r\n"));
			}
		CloseTCPSocket(Kanal->Socket);
		Kanal->Socket = NO_SOCKET_USED;
		} // InCount zu groß
		
	else if (InCount > 0) 
		{
		TTlnDaten TD;
		TlnDatenInit(&TD);
		
		int Res = GetSocketData(Kanal->Socket, InCount, TlnServBuf.Buf);
		
		if (ProtokollLevelTlnServ >= (Kanal->Fertig ? NurFehler : DatenDetailliert)) 
			{ // Protokollieren der Daten, wenn alles Protokolliert werden soll (3) oder
			  // im Falle des Fehlers (Fertig = true) Protokollierung nicht ganz abgeschaltet ist (0)
			ProtokollierenTlnServInt_P(Kanal, PSTR("Socket Empfang: (%ld/" ), InCount);
			ProtokollierenInt_P(PSTR("%d)"), Res);
			if (Res > 0)
				ProtokollierenPuffer(TlnServBuf.Buf, Res);
			if (Kanal->Fertig)
				Protokollieren_P(PSTR("! unerwartet wegen Fertig=TRUE\r\n"));
			else
				Protokollieren_P(PSTR("\r\n"));
			}		
		
		Kanal->Fertig = false;
			
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
					if (ProtokollLevelTlnServ >= DatenKurz)
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
						} // TlnAktualisierung(...) = true
					else
						{
						FehlerRueckmelden(PSTR("forbidden"), 0);
						Senden = true;
						if (ProtokollLevelTlnServ >= NurFehler)
							{
							ProtokollierenTlnServ_P(Kanal, PSTR("! abgewiesene Anfrage war von IP "));
							ProtokollierenIPAdr(MeldeIP);
							Protokollieren_P(PSTR("\r\n"));
							}
						} // TlnAktualisierung(..) = false --> nicht erlaubt
					} // else ausreichend Daten erhalten
					
				if (Senden)
					KanalFertig(Kanal); // mehr wird da nicht kommen.
					
				break; // TlnServBuf.Code == TLNSERV_SELBSTAKT
				
			case TLNSERV_ABFRAGE:
				if (TlnServBuf.DataLen < sizeof(TlnServBuf.TlnAbfr) - 1) 
					// - 1, da ohne das letzte Byte ( = Version) von Version 1 ausgegangen wird.
					{
					FehlerRueckmelden(PSTR("request not enough data: %u"), TlnServBuf.DataLen);
					Senden = true;
					}
				else
					{
					uint32_t RufNr = TlnServBuf.TlnAbfr.RufNr;
					if (TlnServBuf.DataLen <= sizeof(TlnServBuf.TlnAbfr) - 1)
						TlnServBuf.TlnAbfr.Version = 1;
						
					if (ProtokollLevelTlnServ >= AblaufInfo) 
						ProtokollierenTlnServInt_P(Kanal, PSTR("Abfrage empfangen. Nummer %lu: "), RufNr);
						
					// Telefonbuch abfragen
					if (TlnSuche(RufNr, false, &TD)
						&& !(TD.Flags & TlnFlag_Lokal)
						&& !(TD.Flags & TlnFlag_Gesperrt))
						{ // gefunden
						// Antwort generieren:
						TlnServBuf.Code = TLNSERV_AUSKUNFT_VERSION1;
						TlnServBuf.TlnAuskunft = TD;
						TlnServBuf.TlnAuskunft.DynPin = 0; // Datenschutz
						TlnServBuf.DataLen = sizeof(TlnServBuf.TlnAuskunft);
						Senden = true;
						TlnServAbfrageZaehler++; // für die Statistik
						if (ProtokollLevelTlnServ >= AblaufInfo)
							Protokollieren_P(PSTR(" ...gefunden\r\n"));
						}
					else
						{ // Nummer nicht gefunden
						TlnServBuf.Code = TLNSERV_AUSKUNFT_NICHTVERG;
						TlnServBuf.DataLen = 0;
						Senden = true;
						if (ProtokollLevelTlnServ >= AblaufInfo) 
							Protokollieren_P(PSTR("* ...nicht gefunden oder gesperrt\r\n"));
						} 
					} // else ausreichend Daten erhalten
					
				if (Senden)
					KanalFertig(Kanal); // mehr wird da nicht kommen.
					
				break; // TlnServBuf.Code == TLNSERV_ABFRAGE_VERSION1

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
					Res = TlnHinzufuegen(&TlnServBuf.TlnAuskunft, TlnHinzNurNeuereUebernehmen);
					if (Res < 0)
						{
						ProtokollierenTlnServInt_P(Kanal, 
							PSTR("! Datensatz vom Teilnehmer-Server mit Nr %lu konnte nicht gespeichert werden\r\n"), 
							TlnServBuf.TlnAuskunft.Nummer);
						Diagnoseausgabe_P(ISTR(TeilnehmerlisteVoll, LokaleSprache), 2); 
						FehlerRueckmelden(PSTR("abort"), 0);	
						Senden = true;
						}
					else
						{ // noch alles gut
						if (Res == 1)
							{ // gespeichert...
							if (ProtokollLevelTlnServ >= AblaufInfo)
								ProtokollierenTlnServInt_P(Kanal, 
									PSTR("* Datensatz vom Teilnehmer-Server mit Nr %lu empfangen und gespeichert \r\n"), 
									TlnServBuf.TlnAuskunft.Nummer);
							TlnServTlnbuchEintragGeaendert(&TlnServBuf.TlnAuskunft, Kanal->ListeIdx);
							Kanal->AnzahlAktualisiert++;
							}
						else if (Res == 2)
							{ // vorhandener ist aktueller (neuer) als gesendeter!
							if (ProtokollLevelTlnServ >= NurFehler)
								ProtokollierenTlnServInt_P(Kanal, 
									PSTR("! veralteter Datensatz vom Teilnehmer-Server mit Nr %lu empfangen\r\n"), 
									TlnServBuf.TlnAuskunft.Nummer);
								
							TlnServTlnbuchEintragGeaendert(&TlnServBuf.TlnAuskunft, -1); 
								// bewirkt, dass das Stichdatum ALLER TlnServer zurückgesetzt wird,
								// damit der Server, der den veralteten Datensatz gesendet hat
								// auch den korrekten Stand erhält.
							}
						else // Res == 0
							{
							if (ProtokollLevelTlnServ >= DatenKurz)
								ProtokollierenTlnServInt_P(Kanal, 
									PSTR("Datensatz vom Teilnehmer-Server mit Nr %lu empfangen, keine Aenderung\r\n"), 
									TlnServBuf.TlnAuskunft.Nummer); 
							}
							
						// Antwort generieren:
						TlnServBuf.Code = TLNSERV_SYNC_QUITTUNG;
						TlnServBuf.DataLen = 0;
						Senden = true;
						}
					} // SyncFreigabe ok
				break; // TlnServBuf.Code == TLNSERV_AUSKUNFT_VERSION1
				
			case TLNSERV_SYNC_VOLLABFRAGE:
				if (TlnServBuf.DataLen < sizeof(TlnServBuf.SyncAnmeldung))
					{
					FehlerRueckmelden(PSTR("login not enough data: %u"), TlnServBuf.DataLen);
					Senden = true;
					}
				else if (TlnServBuf.SyncAnmeldung.Geheimzahl == 0) 
					{ // anonyme Anmeldung, es werden keine DynPin gesendet.
					Kanal->Freigabe = false;
					Kanal->AusgabeStichdatum = 0; // = alle 
					Kanal->AusgabeGestartet = false; // wird aber gleich gestartet
					TlnDatensatzSyncSenden(Kanal);
					Senden = true;
					}
				else if (TlnServBuf.SyncAnmeldung.Geheimzahl != TlnServSyncGeheimzahl)
					{
					Diagnoseausgabe_P(ISTR(ServerAnmeldungFalscheGeheimzahl, LokaleSprache), 1);
					FehlerRueckmelden(PSTR("wrong authentification"), 0);
					Senden = true;
					}
				else
					{
					Kanal->Freigabe = true;
					Kanal->AusgabeStichdatum = 0; // = alle 
					Kanal->AusgabeGestartet = false; // wird aber gleich gestartet
					TlnDatensatzSyncSenden(Kanal);
					Senden = true;
					}
				break; // TlnServBuf.Code == TLNSERV_SYNC_VOLLABFRAGE

			case TLNSERV_SUCHE:
				if (TlnServBuf.DataLen < sizeof(TlnServBuf.TlnSuche))
					{
					FehlerRueckmelden(PSTR("search not enough data: %u"), TlnServBuf.DataLen);
					Senden = true;
					}
				else
					{
					Kanal->AusgabeStichdatum = 0; // = alle 
					Kanal->AusgabeGestartet = false; // wird aber gleich gestartet
					Kanal->Freigabe = false; // sicherheitshalber
					strncpy(Kanal->SuchMuster, TlnServBuf.TlnSuche.SuchMuster, TlnNameMax);
					Kanal->SuchMuster[TlnNameMax-1] = '\0'; // begrenzen
					TlnDatensatzSyncSenden(Kanal);
					Senden = true;
					}
				break; // TlnServBuf.Code == TLNSERV_SUCHE
			
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
					Diagnoseausgabe_P(ISTR(ServerAnmeldungFalscheGeheimzahl, LokaleSprache), 1);
					}
				else
					{
					Kanal->Freigabe = true;
					
					// Quittung senden:
					TlnServBuf.Code = TLNSERV_SYNC_QUITTUNG;
					TlnServBuf.DataLen = 0;
					Senden = true;
					}
				break; // TlnServBuf.Code == TLNSERV_SYNC_ANMELDUNG
			
			case TLNSERV_SYNC_QUITTUNG:
				if (!Kanal->AusgabeGestartet)
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
				
				break; // TlnServBuf.Code == TLNSERV_SYNC_QUITTUNG
			
			case TLNSERV_SYNC_ENDE:
				if (ProtokollLevelTlnServ >= AblaufInfo) 
					{
					ProtokollierenTlnServ_P(Kanal, PSTR("Ende Kennung empfangen, Socket wird geschlossen\r\n"));
					}

				CloseTCPSocket(Kanal->Socket);
				Kanal->Socket = NO_SOCKET_USED;
					
				if (Kanal->IstVollAbfrage)
					{ 
					if (ProtokollLevelTlnServ >= AblaufInfo) 
						{
						ProtokollierenTlnServ_P(Kanal, PSTR("Vollabfrage erfolgreich beendet"));
						ProtokollierenInt_P(PSTR(" mit %d geaenderten / aktualisierten Eintraegen\r\n"), Kanal->AnzahlAktualisiert);
						TeilnehmerServerErfolgSpeichern(Kanal->ListeIdx); 
						}		
						
					StartLangTimer(&VollAbfrageTimer);
					VollAbfrageTimerEnde = 20 * 60 * LangTimerMinuteFaktor 
											+ Zufallswert(0x1F) * 30 * LangTimerMinuteFaktor;
						// nächste Voll-Abfrage in 20,0 bis 35,5 Stunden.
					} // if (Kanal->IstVollAbfrage)
					
				SyncWarteEnde = (120 - Zufallswert(0x3F)) * KurzTimerFreq; // 2 Minuten warten.
				break; // TlnServBuf.Code == TLNSERV_SYNC_ENDE

			case TLNSERV_ASCII_ABFRAGE:
				// ASCII-Abfrage in der Form "q12345" oder ähnlich. 
				if (InCount < 1 + GlobRufnrMinZiffern) // mindestens "q" eine hinreichend lange Rufnummer und <LF>
					{
					FehlerRueckmelden(PSTR("ascii-query too short: %u"), InCount);
					Senden = true;
					}
				else
					{
					uint32_t RufNr = atol(TlnServBuf.Buf + 1); // Nach dem 'q' beginnen
					if (ProtokollLevelTlnServ >= AblaufInfo) 
						ProtokollierenTlnServInt_P(Kanal, PSTR("Ascii-Abfrage empfangen. Nummer %lu: "), RufNr);
						
					// Telefonbuch abfragen
					if (TlnSuche(RufNr, false, &TD)
						&& !(TD.Flags & TlnFlag_Lokal)
						&& !(TD.Flags & TlnFlag_Gesperrt))
						{ // gefunden
						// Antwort generieren:
						AsciiTlnAuskunft(&TD); // erstellt die Teilnehmer-Auskunft als ASCII-Daten
						Senden = true;
						TlnServAbfrageZaehler++; // für die Statistik
						if (ProtokollLevelTlnServ >= AblaufInfo)
							Protokollieren_P(PSTR(" ...gefunden\r\n"));
						}
					else
						{ // Nummer nicht gefunden
						sprintf_P(TlnServBuf.Buf, PSTR("fail\r\n%lu\r\nunknown\r\n+++\r\n"), RufNr);
						Senden = true;
						if (ProtokollLevelTlnServ >= AblaufInfo) 
							Protokollieren_P(PSTR("* ...nicht gefunden oder gesperrt\r\n"));
						} 
					} // else ausreichend Daten erhalten
					
				if (Senden)
					KanalFertig(Kanal); // mehr wird da nicht kommen.
					
				break; // TlnServBuf.Code == TLNSERV_ASCII_ABFRAGE
				
			case TLNSERV_FEHLER:
				CloseTCPSocket(Kanal->Socket);
				Kanal->Socket = NO_SOCKET_USED;
				if (ProtokollLevelTlnServ >= NurFehler) 
					{
					ProtokollierenTlnServ_P(Kanal, PSTR("! Fehlermeldung empfangen, Socket wird geschlossen: "));
					Protokollieren(TlnServBuf.PureData);
					Protokollieren_P(PSTR("\r\n"));
					}		
				SyncWarteEnde = (30 + Zufallswert(0xF)) * KurzTimerFreq; // 30 Sekunden warten.

				if (Kanal->ListeIdx >= 0 
					&& Kanal->AusgabeStichdatum > 0
					&& TlnServSyncStichzeit[Kanal->ListeIdx] > Kanal->AusgabeStichdatum) // nur wenn es eine aktive Sync-Ausgabe war.
					TlnServSyncStichzeit[Kanal->ListeIdx] = Kanal->AusgabeStichdatum; 
						// wegen des Fehlers alles noch mal senden.
					
				TeilnehmerServerFehlerSpeichern(Kanal->ListeIdx);
					// und Fehler merken, so dass bei Wiederholung der Fehlermeldung 
					// dieser Server bald nicht mehr berücksichtigt wird.
					
				break; // TlnServBuf.Code == TLNSERV_FEHLER
			
			case TLNSERV_IPRUECKMELD: // ist ein Fehler, da dieses Telegramm nur eine Antwort des Servers sein kann.
			case TLNSERV_AUSKUNFT_NICHTVERG: // ist ein Fehler, da dieses Telegramm nur eine Antwort des Servers sein kann.
			default:
				FehlerRueckmelden(PSTR("unknown code %02X "), TlnServBuf.Code);
				Senden = true;
				break; // TlnServBuf.Code ist was anderes.

			} // switch (TlnServBuf.Code)
			
		if (ProtokollLevelTlnServ >= NurFehler && Senden && TlnServBuf.Code == TLNSERV_FEHLER)
			{
			ProtokollierenTlnServ_P(Kanal, PSTR("! Error "));
			Protokollieren(TlnServBuf.PureData);
			Protokollieren_P(PSTR("\r\n"));
			}

		} // if InCount = GetBytesInSocketData > 0
		
	// soll offene Verbindung geschlossen werden?
	if (Kanal->Socket != NO_SOCKET_USED && CheckSocketState(Kanal->Socket) == SOCKET_NOT_USE)
		{
		if (ProtokollLevelTlnServ >= AblaufInfo)
			ProtokollierenTlnServ_P(Kanal, PSTR("Socket wurde von Gegenstelle geschlossen\r\n"));
		CloseTCPSocket(Kanal->Socket);
		Kanal->Socket = NO_SOCKET_USED;
		if (Kanal->ListeIdx >= 0 && Kanal->ListeIdx < ANZ_TEILNEHMER_SERVER)
			{ // noch Statistik führen
			if (Kanal->Fertig)
				{ // erwartetes Ende
				TeilnehmerServerErfolgSpeichern(Kanal->ListeIdx);
				}
			else
				{ // unerwartetes Ende...
				if (Kanal->AusgabeGestartet 
					&& Kanal->AusgabeStichdatum > 0
					&& TlnServSyncStichzeit[Kanal->ListeIdx] > Kanal->AusgabeStichdatum)
					TlnServSyncStichzeit[Kanal->ListeIdx] = Kanal->AusgabeStichdatum; 
						// wegen des Fehlers alles noch mal senden.
					
				TeilnehmerServerFehlerSpeichern(Kanal->ListeIdx);
				if (ProtokollLevelTlnServ >= NurFehler)
					ProtokollierenTlnServ_P(Kanal, PSTR("! Socket wurde von Gegenstelle UNERWARTET geschlossen\r\n"));
				}
			}

		// da ursache nicht bekannt, kein SyncWarteEnde = X * KurzTimerFreq; // X Sekunden warten.		
		return;
		}
		
	// Verzögerten Abbau des Kanals durch Gegenstelle selbst nachholen.
	if (Kanal->Socket != NO_SOCKET_USED && Kanal->Fertig && KurzTimerVal(&Kanal->SelbstAbbauVerzoegerung) >= 5 * KurzTimerFreq)
		{
		if (ProtokollLevelTlnServ >= AblaufInfo)
			ProtokollierenTlnServ_P(Kanal, PSTR("* Schliessen des Socket nach Timeout\r\n"));
		CloseTCPSocket(Kanal->Socket);
		Kanal->Socket = NO_SOCKET_USED;
		if (Kanal->ListeIdx >= 0 && Kanal->ListeIdx < ANZ_TEILNEHMER_SERVER)
			{ // noch Statistik führen, erwartetes Ende
			TeilnehmerServerErfolgSpeichern(Kanal->ListeIdx);
			}
			
		return;
		}
		
	// Daten ggf. ins Netz senden
	// --------------------------------------------------
	if (Senden && Kanal->Socket != NO_SOCKET_USED)
		SocketDatenSenden(Kanal);
	
	} // SocketBearbeiten()

	
static bool VollAbfrageKanalOeffnen()
	{
	if (TlnServer[0].Socket != NO_SOCKET_USED)
		return false;

	bool Res = false;
	int NewSock = TeilnehmerServerSocketOeffnen1(VollAbfrageServerIndex, PSTR("Vollabfrage")); 
		// da wird auch Protokoll geschrieben.
		
	if (NewSock != -1)
		{
		KanalInit(&TlnServer[0], NewSock); 
		TlnServer[0].ListeIdx = VollAbfrageServerIndex; 
		if (ProtokollLevelTlnServ >= AblaufInfo)
			{
			ProtokollierenTlnServ_P(&TlnServer[0], PSTR("Client-Socket "));
			ProtokollierenInt_P(PSTR("#%d geoeffnet, Vollabfrage begonnen\r\n"), NewSock);
			}
		TlnServBuf.Code = TLNSERV_SYNC_VOLLABFRAGE;
		TlnServBuf.DataLen = sizeof(TlnServBuf.SyncAnmeldung);
		TlnServBuf.SyncAnmeldung.Version = 1; // gibt erst mal nix anderes.
		TlnServBuf.SyncAnmeldung.Geheimzahl = TlnServSyncGeheimzahl;
		SocketDatenSenden(&TlnServer[0]);
		TlnServer[0].Freigabe = true; // wer anruft weiß wen er anruft.
		TlnServer[0].IstVollAbfrage = true;
		Res = true;
		}

	VollAbfrageServerIndex++;
	if (VollAbfrageServerIndex >= ANZ_TEILNEHMER_SERVER)
		VollAbfrageServerIndex = 0;
		
	return Res;
	}


//! Ermittelt, welcher Teilnehmer-Server-Index für die nächste "aktive" Synchronisation dran ist.
//-----------------------------------------------------------------------------------------------
//! \retval -1 wenn alle aktuell oder gesperrt.
static int8_t NaechsterAktivSyncTlnServerIndex()
	{
	for (uint8_t i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
		{
		if (TlnServSyncStichzeit[i] > TlnBuchLetzteAenderung)
			continue;
			
		if (!TeilnehmerServerVerfuegbar(i, NULL))
			continue;

		return i;
		}
		
	return -1;
	}

	
//! Öffnet den vorbestimmten Kanal zum aktiven Synchronisieren.
//-------------------------------------------------------------
static bool AktivSyncMeldungKanalOeffnen(uint8_t ServerI)
	{
	if (TeilnehmerServerAdresse[ServerI][0] == '\0')
		return false;

	int NewSock = TeilnehmerServerSocketOeffnen1(ServerI, PSTR("Sync-Meldung"));
	// da wird auch Protokoll geschrieben.
			
	if (NewSock != -1)
		{
		struct TIME CurTime;
		CLOCK_GetTime(&CurTime);
		KanalInit(&TlnServer[0], NewSock); 
		TlnServer[0].ListeIdx = ServerI;
		TlnServer[0].Freigabe = true; // der Anrufer ist immer ok
		TlnServer[0].AusgabeStichdatum = TlnServSyncStichzeit[ServerI];
		TlnServer[0].AusgabeGestartet = true;
		TlnServer[0].IstVollAbfrage = false;
		TlnServSyncStichzeit[ServerI] = CurTime.time; 
			//! Wird wieder auf AusgabeStichdatum zurückgesetzt werden, wenn Fehler passiert.
		TlnListerStart(&TlnServer[0].AusgabeLister);
		if (ProtokollLevelTlnServ >= AblaufInfo)
			{
			ProtokollierenTlnServ_P(&TlnServer[0], PSTR("Client-Socket "));
			ProtokollierenInt_P(PSTR("#%d geoeffnet zur Ausgabe der geaenderten Teilnehmer-Eintraege\r\n"), NewSock);
			}
			
		TlnServBuf.Code = TLNSERV_SYNC_ANMELDUNG;
		TlnServBuf.DataLen = sizeof(TlnServBuf.SyncAnmeldung);
		TlnServBuf.SyncAnmeldung.Version = 1; // gibt erst mal nix anderes.
		TlnServBuf.SyncAnmeldung.Geheimzahl = TlnServSyncGeheimzahl;
		SocketDatenSenden(&TlnServer[0]);
		return true;
		}
		
	return false; // Fehler beim Öffnen.
	}
	
	
//! Der iTelex-Rufnummernserver-Client an sich.
//------------------------------------------------------------------------------------------------------------
//! Diese Funktion wird zyklisch aufgerufen und hat folgende Aufgaben:
//! \par - Nachschauen, ob eine Verbindung auf den registrierten Port eingegangen ist. Wenn ja 
//! holt er sich die Socketnummer der Verbindung und speichert diese.
//! \par - Wenn eine Verbindung zustande gekommen ist wird diese wiederrum zyklisch nach neuen Daten abgefragt und entsprechend
//! reagiert.
//! \param 	NONE
//! \return	NONE

void itelex_tlnserv_thread()
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
	int NewServerSocket = CheckPortRequest(ITELEX_TLNSERV_PORT);
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

		if (ProtokollLevelTlnServ >= (Ok ? AblaufInfo : NurFehler))
			{
			ProtokollierenTlnServ_P(&TlnServer[i], PSTR("Server-Socket geoeffnet von IP "));
			ProtokollierenIPAdr(TCP_sockettable[NewServerSocket].SourceIP);
			Protokollieren_P(PSTR(" / MAC "));
			ProtokollierenMAC(TCP_sockettable[NewServerSocket].MACadress);
			if (Ok)
				ProtokollierenInt_P(PSTR(" ...ok #%d\r\n"), NewServerSocket);
			else
				Protokollieren_P(PSTR("! ...ABGEWIESEN wegen alle besetzt\r\n"));
			}
			
		if (!Ok)
			CloseTCPSocket(NewServerSocket);

		}

	// ==========================================================================
	// Timeouts?
	// ==========================================================================

	// Sind in SocketBearbeiten() behandelt.
	
	
	// ==========================================================================
	// Neue Aktionen starten?
	// ==========================================================================

	if (TlnServer[0].Socket != NO_SOCKET_USED 
		|| TlnServer[1].Socket != NO_SOCKET_USED
		|| TlnServSyncGeheimzahl == 0) // ist nicht als Teilnehmer-Server konfiguriert
		StartKurzTimer(&SyncWarteTimer); // Warten bis mindestens zwei Verbindungen geschlossen sind

	else if (KurzTimerVal(&SyncWarteTimer) > SyncWarteEnde)
		{
		int8_t AktivSyncServerI = NaechsterAktivSyncTlnServerIndex();
		if (AktivSyncServerI >= 0
			&& LangTimerVal(&VollAbfrageTimer) < VollAbfrageTimerEnde + 60 * LangTimerMinuteFaktor)
			{ // erst mal neue Einträge weiter melden, es sei denn dass Vollabfrage mehr als eine Stunde überfällig
			if (!AktivSyncMeldungKanalOeffnen(AktivSyncServerI))
				{
				SyncWarteEnde = (20 + Zufallswert(0x7)) * KurzTimerFreq; // bei Fehler möglichst bald den nächsten benutzen.
				}
			StartKurzTimer(&SyncWarteTimer);
			}
			
		else if (LangTimerVal(&VollAbfrageTimer) >= VollAbfrageTimerEnde)
			{ // mal zur Sicherheit andere Server befragen.
			StartLangTimer(&VollAbfrageTimer);
			VollAbfrageTimerEnde = 5 * LangTimerMinuteFaktor; 
				// die fünf Minuten gelten nur im Fehlerfall, im Erfolgsfall wird ein Tag gewartet.
			
			if (!VollAbfrageKanalOeffnen())
				{
				SyncWarteEnde = (30 + Zufallswert(0xF)) * KurzTimerFreq; // bei Fehler möglichst bald den nächsten benutzen.
				}
			StartKurzTimer(&SyncWarteTimer);
			}
					
		} // kein Socket offen und Wartezeit abgelaufen.
	
	} // itelex_tlnserv_thread
	
	
// ================================================================================	

//! Gibt relevante Prozessdaten auf der HTML-Seite "Debug-Info" aus
void TlnServDebugPrint()
	{
	
#define PRINTVAL(Var) printf_P(PSTR("<br>" #Var " = %lu"), (unsigned long) Var)
	
	struct TIME Time;
	uint8_t i;
	
	CLOCK_GetTime(&Time); // holt auch die aktuelle Zeitzone

	PRINTVAL(TlnServAbfrageZaehler);
	
	for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
		{
		Time.time = TlnServSyncStichzeit[i];
		CLOCK_decode_time(&Time);
			
		printf_P(PSTR("<br>TlnServSyncStichzeit(%s) = %02u.%02u.%04u %02d:%02d:%02d"), TeilnehmerServerAdresse[i], Time.DD, Time.MM, Time.YY, Time.hh, Time.mm, Time.ss);
		}
		
	for (i = 0 ; i < AnzTlnServKanaele ; i++)
		{
		printf_P(PSTR("<br>TlnServerKanal %d wurde %lu mal genutzt."), i, TlnServer[i].NutzungZaehler);
		if (TlnServer[i].Socket != NO_SOCKET_USED)
			{
			printf_P(PSTR(" Momentan ge&ouml;ffnet (SocketID=%d, ListeIdx=%d, Freigabe=%d, "
						  "AusgabeGestartet=%d, AnzahlAktualisiert=%d, Fertig=%d, Timeoutcounter=%d)"), 
				TlnServer[i].Socket, TlnServer[i].ListeIdx, TlnServer[i].Freigabe, 
				TlnServer[i].AusgabeGestartet, TlnServer[i].AnzahlAktualisiert, TlnServer[i].Fertig,
				TCP_sockettable[TlnServer[i].Socket].Timeoutcounter);
			}
		}
		
	Time.time = TlnBuchLetzteAenderung;
	CLOCK_decode_time(&Time);
			
	printf_P(PSTR("<br>TlnBuchLetzteAenderung = %02u.%02u.%04u %02d:%02d:%02d"), Time.DD, Time.MM, Time.YY, Time.hh, Time.mm, Time.ss);
	
	PRINTVAL(KurzTimerVal(&SyncWarteTimer));
	PRINTVAL(SyncWarteEnde);
	PRINTVAL(LangTimerVal(&VollAbfrageTimer));
	PRINTVAL(VollAbfrageTimerEnde);
	PRINTVAL(VollAbfrageServerIndex);

#undef PRINTVAL
	}
	

/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Initialisiert den Teilnehmerauskunft-Server-clinet und registriert den Port auf welchen dieser lauschen soll.
 * \param 	NONE
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void itelex_tlnserv_init()
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

	timer0_init(iTelexTimerFreq); 
	if (!timer0_RegisterCallbackFunction(itelex_timerEvent))
		return;
	*/
	
	/*
	cgi_RegisterCGI( itelex_cgi_msg_In, PSTR("itelex-msg-in.cgi"));
	*/

	for (i = 0 ; i < AnzTlnServKanaele ; i++)
		{
		TlnServer[i].Socket = NO_SOCKET_USED;
		TlnServer[i].NutzungZaehler = 0;
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

	for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
		{
		TlnServSyncStichzeit[i] = TlnBuchLetzteAenderung + 1; // 1, damit nach Reset scheinbar keine Synchronisationen erforderlich sind.
		}
		
	StartKurzTimer(&SyncWarteTimer);
	SyncWarteEnde = 10 * KurzTimerFreq; // 10 Sekunden
	
	StartLangTimer(&VollAbfrageTimer);
	VollAbfrageTimerEnde = 1 * LangTimerMinuteFaktor;
	VollAbfrageServerIndex = 0;

	TlnServAbfrageZaehler = 0;
	
	struct TIME Time;
	CLOCK_GetTime(&Time); // holt auch die aktuelle Zeitzone
	if (Time.YY < 2000)
		{
		if (TlnServSyncGeheimzahl != 0)
			Diagnoseausgabe_P(ISTR(ServerAusWegenFehlenderUhrzeit, LokaleSprache), 1);
		return; // der Teilnehmerserver ist auf ein korrektes Datum angewiesen
		}
		
	// Wenn die folgenden Funktionen nicht aufgerufen werden, passiert gar nix als Teilnehmer-Server Funktion.
	RegisterTCPPort(ITELEX_TLNSERV_PORT);
	printf_P(PSTR("iTelex TlnServer Port %u.\r\n") , ITELEX_TLNSERV_PORT );

	THREAD_RegisterThread( itelex_tlnserv_thread, PSTR("TlnSrv"));
	}


#endif //def ITELEX_TLNSERVER

#endif //def iTelex

