/*! \file iTelex.h \brief iTelex Definitionen */
/***************************************************************************
 *            iTelex.h
 *
 ****************************************************************************/
///	\ingroup software
///	\defgroup 
///	\code #include "iTelex.h" \endcode
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
#ifndef __ITELEX_H__

#define __ITELEX_H__

#include "config.h"

#ifdef iTelex

#if !defined(ITELEX_TLNSERVER) && !defined(ITELEX_ANSCHLUSS)
	#warning Kein iTelex-Modul aktiv!
#endif

#include <avr/pgmspace.h>  
#include <avr/interrupt.h>
#include "system/shell/shell.h"
#include "config.h"

#include "Defports.h"
#include "FifoPuffer.h"

#include "iTelex/StringTab.h"


// Einstellungen für Bedingte Kompilierung
// ================================================================

// Was soll LED rot anzeigen?
//---------------------------
//#define LEDROT_EXTEEPROM
//#define LEDROT_SDKARTE
#define LEDROT_ITELEXTHREADBLOCK
//#define LEDROT_SOCKETERROR
//#define LEDROT_UNERWARTET  // noch ungenutzt


// Port-Definitionen
// ================================================================

DEFPORTINPULL(Taste, B, 3);

DEFPORTOUT(RTS, D, 4)

DEFPORTIN(CTS, D, 5)


// LEDs
// ====
#define ROT 0
#define GELB 1
#define GRUEN 2
#define BLAU 3


// Konstanten
// ================================================================


//! Nur Konstanten-Definitionen.
enum { 
	ITELEX_PORT = 134,
	//!< Der TCP-Port für die iTelex-Kommunikation.

	ITELEX_TLNSERV_PORT = 11811,
	//!< Der TCP-Port für die Kommunikation mit den Teilnehmer-Servern.
	
	TlnAdresseMax = 40,
	//!< maximale Länge der Verbindungsadresse.
	//!< Nicht ändern, da auch der Datenaustausch mit dem Teilnehmer-Server
	//!< betroffen wäre (Kompatibilitätsprobleme) (siehe #TTlnDaten)
	
	TlnNameMax = 40, 
	//!< maximale Länge des Teilnehmer-Namens
	//!< Nicht ändern, da auch der Datenaustausch mit dem Teilnehmer-Server
	//!< betroffen wäre (Kompatibilitätsprobleme) (siehe #TTlnDaten)

	AsciiDruckPufferMax = 1000,
	//!< Puffergröße für Textpuffer bei Umsetzung ASCII -> Baudot.

	HtmlSendeTextMax = 400,
	//!< Puffergröße für Textpuffer bei HTML-Kommunikation (Druckspiegel)

	SocketInBufMax = 2500,
	//!< Größe des TCP-Empfangspuffers

	SocketOutBufMax = 2500,
	//!< Größe des TCP-Sendepuffers

	DiagnosePufferMax = 200,
	//!< Größe des Puffers für zu druckende Diagnosemeldungen.
	
	TlnFlag_Lokal = 1,
	//!< Diese Nummer wird nicht mit anderen Teilnehmern synchronisiert.
	//!< Für #Flags in #TTlnDaten. Bitmaske!
	
	TlnFlag_Gesperrt = 2,
	//!< Diese Nummer darf nicht bei Abfragen der Teilnehmerliste vom 
	//!< Teilnehmer-Server gemeldet werden.
	//!< Für #Flags in #TTlnDaten. Bitmaske!
	

	// Kommandos für Kommunikation mit Teilnehmer-Server ("Auskunft") und der
	// Teilnehmer-Server untereinander.

	TLNSERV_SELBSTAKT = 0x01,
	//!< Meldung eines Teilnehmer an den Teilnehmer-Server als Wunsch die eigene 
	//!< IP-Adresse zu aktualisieren.
	
	TLNSERV_IPRUECKMELD = 0x02,
	//!< Meldung des Teilnehmer-Server an den Teilnehmer als Rückmeldung der 
	//!< nun gespeicherten IP-Adresse. Antwort auf #TLNSERV_SELBSTAKT.

	TLNSERV_ABFRAGE = 0x03,
	//!< Meldung eines Teilnehmer an den Teilnehmer-Server als Wunsch die
	//!< IP-Adresse eines anderen Teilnehmers zu erfragen.
	
	TLNSERV_AUSKUNFT_NICHTVERG = 0x04,
	//!< Meldung des Teilnehmer-Server an den Teilnehmer als Rückmeldung dass
	//!< durch #TLNSERV_ABFRAGE gewünschter Teilnehmer nicht gespeichert ist.

	TLNSERV_AUSKUNFT_VERSION1 = 0x05,
	//!< Meldung des Teilnehmer-Server an den Teilnehmer als Rückmeldung der
	//!< kompletten Daten des durch #TLNSERV_ABFRAGE oder #TLNSERV_SUCHE
	//!< gewünschten Teilnehmers.
	//!< wird auch zur Synchronisation der Teilnehmer-Server untereinander
	//!< verwendet.
	
	TLNSERV_SYNC_VOLLABFRAGE = 0x06,
	//!< Leitet eine vollständige Abfrage aller synchronisationsrelevanten 
	//!< Einträge ein, z.B. weil ein Teilnehmer-Server neu gestartet wurde.
	
	TLNSERV_SYNC_ANMELDUNG = 0x07,
	//!< Erste Meldung eines Teilnehmer-Servers, der neue oder geänderte Einträge
	//!< weitermelden will.
	
	TLNSERV_SYNC_QUITTUNG = 0x08,
	//!< Rückmeldung des Teilnehmer-Servers, der einen Datensatz empfangen hat,
	//!< sobald er Empfangsbereit für weitere Datensätze ist.

	TLNSERV_SYNC_ENDE = 0x09,
	//!< Meldung des sendenden Teilnehmer-Servers, sobald keine weiteren Datensätze
	//!< zu senden sind. \n
	//!< Oder Meldung des empfangenden Teilnehmer-Servers, wenn dieser keine 
	//!< weiteren Datensätze verarbeiten kann.

	TLNSERV_SUCHE = 0x0a,
	//!< Meldung eines Teilnehmers an den Server zur Auflistung der vorhandenen 
	//!< Einträge gemäß des übermittelten Suchmusters.
	//!< Version1 bezieht sich auf die gewünschte Version der Rückmeldung.
	
	TLNSERV_FEHLER = 0xFF,
	//!< Allgemeine Fehlermeldung.

	
	LangTimerTakt = 10,
	//!< Takt in Sekunden des Langzeittimers. 
	//!< Muss ein Teiler von 60 sein.
	
	LangTimerMinuteFaktor = 60/LangTimerTakt,
	//!< Faktor zur Umrechnung Minuten -> Takte von #TLangTimer
	
	KurzTimerFreq = 100,
	//!< Frequenz (1/Takt) des Kurzzeittimers. 
	//!< Muss ein Teiler von #iTelexTimerFreq sein.

	ANZ_TEILNEHMER_SERVER = 3,
	//!< Anzahl der Links zu Teilnehmer-Servern.
	
	GlobRufnrMinZiffern = 5,
	//!< Mindestanzahl der Ziffern von global gültigen Rufnummern
	
	GlobRufnrMinWert = 10000UL,
	//!< Mindestwert der Nummer von global gültigen Rufnummern
	//!< Ziffernzahl muss #GlobRufnrMinZiffern entsprechen.
	
	} ; // Ende Konstanten
	

// Typdefinitionen
// ================================================================

//! Aktuelle Betriebsart der iTelex-Anwendung.
typedef enum
	{
	ModRuhe = 0, 
		//!< nichts läuft

	// Gehend = vom internen Anschluss zum Netz, Reservierung ist eingegangen
	ModGehendReserv = 1, 
		//!< Schnittstelle ist angesprochen worden, aber noch kein Einschaltkommando erhalten.
	ModGehendWaehlen = 2,
		//!< Einschaltkommando erhalten, Wahlaufforderung gesendet, 
		//!< ggf. auch schon Wahlziffern empfangen.
	ModGehendVerbunden = 4,
		//!< Wahl abgeschlossen, Socket geöffnet, Endgerät eingeschaltet.
	
	// Kommend = vom Netz zum internen Anschluss
	ModKommendVerbVorstufe = 11, 
		//!< es wird erst mal abgewartet, was aus der ankommenden Verbindung wird.
		//!< noch kein interner Teilnehmer angeschaltet.
	ModKommendEinschalten = 12, 
		//!< Es wurden Daten oder ein Einschaltkommando (Durchwahl) empfangen.
	ModKommendWarteEinQuitt = 13, 
		//!< Warte auf Einschalt-Quittung des Endgeräts
	ModKommendVerbunden = 14, 
	
	ModPufferDruckUndSchluss = 18,
	ModWarteSchlussQuitt = 19,
	
	// über HTML-Seite verursachte direkte Druckausgabe
	ModHtmlChatWarteEinQuitt = 21, //!< Warte auf Einschalt-Quittung des Endgeräts
	ModHtmlChatVerbunden = 22, 
	
	ModMeldungsdruckWarteEinQuitt = 25, //!< Danach kommt gleich PufferdruckUndSchluss.

	ModDeaktiviert = 31, //!< Durch Tastendruck ausgeschaltet.
	ModWarteGrundstellung = 32, 
		//!< Wartet darauf, dass nach Ausschaltung des lokalen Endgerätes der 
		//!< Socket wieder geschlossen ist und alles andere auch die Grundstellung hat.
	
	ModNamensucheEingabe = 41,
		//!< Nach Wahl von "0" wird die Abfrage eines Namens-Musters gestartet.
		
	ModNamensucheServerAbfrage = 42,
		//!< Name wurde eingegeben, Abfrage des Servers ist gestartet, warte auf 
		//!< Rückmeldungen.

	ModNamensucheAusgabe = 43,
		//!< Abfrage des Servers ist beendet, gebe Einträge aus dem Verzeichnis aus.
		
	ModEmailPOPVerbunden = 51,
		//!< Verbindung zum POP Dienst des Email-Servers ist hergestellt.
		//!< Keine Aussage über den Status der Abfrage selbst.
		//!< Dazu dient im Modul eMail.c die Variable #ProtokollPhase
		
	ModEmailPOPWarteEinQuitt = 52,
		//!< Verbindung zum POP Dienst des Email-Servers ist hergestellt.
		//!< Es ist eine Email (oder nur der Header zu drucken). Drucker
		//!< wurde eingeschaltet hat aber noch nicht quittiert
	
	ModEmailPOPDruckend = 53,
		//!< Verbindung zum POP Dienst des Email-Servers ist hergestellt,
		//!< Drucker ist eingeschaltet und Druckbereit
		
	} TModus;
	

//! Art des Tastendrucks.
typedef enum { 
	NichtGedr, //!< nicht gedrückt.
	Kurz, //!< kurz gedrückt ( < 0,8 Sekunden)
	Lang  //!< lang gedrückt ( > 0,8 Sekunden)
	} TTastendruck; 

	
//! Sollzustand der bestehenden iTelex-Verbindung (Socket)
typedef enum { 
	SocketIdle, //!< Unbenutzt
	SocketOriginate, //!< Ausgehende Verbindung
	SocketAnswer //!< Kommende Verbindung
	} TiTelexSocketMode; 
	
	
 //! Was geht über den Socket 'rüber.	
 typedef enum {
	iTelexProt,		//!< Das eigene Protokoll
	Ascii,			//!< Ascii, also telnet
#ifdef ITELEX_EMAIL
	POP3,			//!< Mail-Abfrage
	SMTP,			//!< Mail-Sendung
#endif //def ITELEX_EMAIL
	} TiTelexSocketProtokoll;
	

//! Typ eines Teilnehmers
typedef enum 
	{
	Geloescht = 0,
	iTelexHostname = 1,
	iTelexIP = 2,
	AsciiHostname = 3, //!< Telnet-ähnlich
	AsciiIP = 4,
	iTelexDynIP = 5,
		//!< diesen Typ gibt es nur beim Teilnehmer-Server. Bei Abfragen wird der 
		//!< Typ iTelexIP gemeldet.
	eMail = 6
	} TTlnAdresseArt;
	

//! Modus für die generierung Datum / Uhrzeit bei ankommenden Anrufen.	
typedef enum 
	{
	DatumDruckKein,
	DatumDruckLokal,
	DatumDruckAnrufer,
	DatumDruckBeide,
	} TDatumDruckModus;
	
	
//! Datenstruktur für alle Informationen eines Teilnehmers.
//! Achtung: Bei Änderungen berücksichtigen, dass auch der Datenaustausch 
//! mit dem Teilnehmer-Server über dieses Format läuft.
//! d.h. bei notwendigen Erweitungen diese Struktur als "Version1" beibehalten
//! und neue Struktur für "Version2" definieren.
	
typedef struct
	{
	uint32_t Nummer; //!< Die Rufnummer, darf keine führenden Nullen enthalten
	char Name[TlnNameMax]; //!< Ausführlicher Name
	uint16_t Flags; //!< Boolsche werte. Siehe TlnFlag_*
	TTlnAdresseArt AdrArt; //!< Was bedeutet die folgende Adresse
	char Adresse[TlnAdresseMax]; //!< URL, IP, eMail, ...
	long IPAdr; //!< bei eindeutiger IP-Adresse
	uint16_t Port; //!< bei abweichendem Port
	uint8_t Durchwahl; //!< interne Durchwahl bei "Nebenstellenanlagen"
	uint16_t DynPin; //!< Geheimzahl für DynIP-Aktualisierung
	uint32_t Datum; //!< letzte Änderung der Adresse
	} TTlnDaten;
	

//! Datenstruktur für alle Kommunikation mit Teilnehmer-Server.
// ------------------------------------------------------------
//! Achtung: Änderungen vermeiden, um Kompatibilität zu wahren.
//! Als Union definiert, um im selben Format unterschiedliche Informationen zu tauschen.

typedef union
	{
	char Buf[50]; //!< zum Direktzugriff beim Block-Schreiben und -Lesen
	struct
		{
		uint8_t Code; //!< Kennung des Datenblocks (Bedeutung). Eine der Konstanten TLNSERV_*
		uint8_t DataLen; //!< Länge des Datenblocks.
		union
			{
			char PureData[1]; 
				//!< Für direkten Zugriff auf den Nutzdatenblock. Wird auch verwendet 
				//!< als Speicher für Textmeldungen bei #Code == #TLNSERV_FEHLER
				//!< und bei #Code == #TLNSERV_SYNC_ENDE.
			struct 
				{
				uint32_t RufNr; //!< Eingene globale Rufnummer.
				uint16_t Pin; //!< Geheimzahl für DynIP-Aktualisierung.
				uint16_t Port; //!< gewünschter Port im WWW.
				} SelbstAkt; //!< Gültig bei #Code == #TLNSERV_SELBSTAKT
			struct
				{
				long EmpfIP; //!< Vom Netz gemeldete globale IP des Absenders der Selbstaktualisierung.
				} IpRueckm; //!< Gültig bei #Code == #TLNSERV_IPRUECKMELD
			struct 
				{
				uint32_t RufNr; //!< globale Telefonnummer.
				uint8_t Version; //!< Welches Datenformat soll verwendet werden?
					// wichtig: diese Reihenfolge RufNr / Version beibehalten, da alte i-Telex Versionen
					// die Versionsnummer nicht mitsenden. Dies wird beim Empfang eines entsprechenden
					// Datenpaketes berücksichtigt.
				} TlnAbfr; //!< Gültig bei #Code == #TLNSERV_ABFRAGE
			struct 
				{
				uint8_t Version; //!< Welches Datenformat soll verwendet werden?
				char SuchMuster[TlnNameMax]; //!< Suchtext. Wird mit den Namen der Einträge abgeglichen.
				} TlnSuche; //!< Gültig bei #Code == #TLNSERV_SUCHE
			TTlnDaten TlnAuskunft; //!< Gültig bei #Code == #TLNSERV_AUSKUNFT_VERSION1
			struct
				{
				uint8_t Version; //!< Welches Datenformat soll verwendet werden?
				uint32_t Geheimzahl; //!< PIN, für alle Teilnehmer-Server gültig und gleich.
				} SyncAnmeldung; //!< Gültig bei #Code == #TLNSERV_SYNC_VOLLABFRAGE
					//!< und bei #Code == #TLNSERV_SYNC_ANMELDUNG 
				
			// für Code == TLNSERV_SYNC_QUITTUNG keine Daten.
			// für Code == TLNSERV_AUSKUNFT_NICHTVERG keine Daten.
			} ;
		} ;
	} TTlnServBuf; 

	
//! Langzeit-Zeitgeber geeignet von 0,5 Minuten bis 5 Tage.
typedef struct { uint16_t x; } TLangTimer;

//! Kurzzeit-Zeitgeber geeignet von x/100 Sekunden bis 10 Minuten.
typedef struct { uint16_t x; } TKurzTimer;
	
//! Statistische Messung von kurz dauernden Vorgängen.
typedef struct	
	{
	TKurzTimer Messung;
	bool Gestartet;
	uint16_t Grenzwert; 
	uint32_t Summe;
	uint16_t Anzahl;
	uint16_t AnzUeberGrenze;
	uint16_t Maximum;
	} TZeitUeberwachung;
	
	
// Variablen
// ================================================================
	
extern TModus Modus;

extern TTastendruck Tastendruck;

//! die tatsächlich laufende Variable für #TLangTimer
extern volatile uint16_t LangTimerCnt;

//! die tatsächlich laufende Variable für #TKurzTimer
extern volatile uint16_t KurzTimerCnt;

extern volatile TPuffer SendePuffer; 
	
extern volatile TPuffer EmpfPuffer; 

extern char AsciiDruckPuffer[AsciiDruckPufferMax+4];
		
extern uint8_t AsciiDruckZiel;

extern int iTelexSocketHandle;

extern TiTelexSocketMode iTelexSocketMode;

extern bool iTelexSocketAbbauGeplant;

extern TKurzTimer iTelexThreadCheckTimer;

extern TKurzTimer iTelexSocketAbbruchTimer;
	
extern TiTelexSocketProtokoll iTelexSocketProtokoll;

extern uint16_t SocketInBufUsed; //!< Benutzter Teil des TCP-Empfangspuffers

extern uint16_t SocketOutBufUsed; //!< Benutzter Teil des TCP-Sendepuffers

extern uint8_t ProtokollPhase;

extern bool TlnBuchOffen;

extern char SocketInBuf[SocketInBufMax+4]; //!< TCP-Empfangspuffer

extern char SocketOutBuf[SocketOutBufMax+4]; //!< TCP-Sendepuffer
	
extern char DiagnosePuffer[DiagnosePufferMax];

extern char TeilnehmerServerAdresse[ANZ_TEILNEHMER_SERVER][TlnAdresseMax];

extern long TeilnehmerServerIP[ANZ_TEILNEHMER_SERVER];

extern TSprache LokaleSprache;
	

// globale Funktionen
// ================================================================

extern void ModusWechsel(TModus neu);

extern void itelex_init1( void );

extern void itelex_init2( void );

extern bool WarteTaste();

extern void AdresseZuWahlStr(uint8_t Adr, char* Buf);

extern void InterneVerbindungBeenden(bool Force);
	
extern void SocketBufInit();

extern bool KonfigFreigabe(void *pStruct, TSprache Sprache, bool Abfragen);

extern bool PruefeSprache(void *pStruct, TSprache *Sprache);

extern bool PruefeSpracheUndKonfigFreigabe(void *pStruct);

extern void SpeichereSpracheAlsLokal(TSprache Sprache);

extern void ZeitUeberwachungInit(TZeitUeberwachung *zue, uint16_t aGrenzwert);

extern void ZeitUeberwachungStart(TZeitUeberwachung *zue);

extern bool ZeitUeberwachungEnde(TZeitUeberwachung *zue);

extern void ZeitUeberwachungAbbruch(TZeitUeberwachung *zue);

extern char *ZeitUeberwachungAusgabe(TZeitUeberwachung *zue);

extern bool Diagnoseausgabe_P(const char *msg, uint8_t Level);

extern bool SonstigeAnwahl(uint8_t aDurchwahl, bool OhneMeldung);

extern void AsciiDruckPufferVerarbeiten();

extern bool TeilnehmerServerVerfuegbar(int ServerI, PGM_P Grund);

extern int TeilnehmerServerSocketOeffnen1(int ServerI, PGM_P Grund);

extern void TeilnehmerServerFehlerSpeichern(int ServerI);

extern void TeilnehmerServerErfolgSpeichern(int ServerI);

extern uint16_t Zufallswert(uint16_t Maske);

//! Startet Langzeit-Messung.
static inline void StartLangTimer(TLangTimer *t)
	{
	uint8_t sreg_tmp = SREG;
	cli();
	t->x = LangTimerCnt;
	SREG = sreg_tmp;
	}
	

//! Aktueller Wert einer Langzeit-Messung entsprechend #LangTimerTakt.
static inline uint16_t LangTimerVal(TLangTimer *t)
	{
	uint16_t res;
	
	uint8_t sreg_tmp = SREG;
	cli();
	res = LangTimerCnt - t->x; // Überlauf wird absichtlich erwartet!
	SREG = sreg_tmp;
	return res;
	}


//! Startet Kurzzeit-Messung.
static inline void StartKurzTimer(TKurzTimer *t)
	{
	uint8_t sreg_tmp = SREG;
	cli();
	t->x = KurzTimerCnt;
	SREG = sreg_tmp;
	}
	

//! Aktueller Wert einer Kurzzeit-Messung in Takten entsprechend #KurzTimerFreq.
static inline uint16_t KurzTimerVal(TKurzTimer *t)
	{
	uint16_t res;
	
	uint8_t sreg_tmp = SREG;
	cli();
	res = KurzTimerCnt - t->x; // Überlauf wird absichtlich erwartet!
	SREG = sreg_tmp;
	return res;
	}
		
	
#endif //def iTelex
	
#endif // def __ITELEX_H__

//@}
