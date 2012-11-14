/*! \file TxP.c \brief Anwendung zur Einbettung in das TxP2-System */
//***************************************************************************
//*            TxP.c
//*
//****************************************************************************/
///	\ingroup software
///	\defgroup TxP Hauptfunktion dieser Applikation: Schnittstelle vom Internet
/// zum Fernschreiber
///	\code #include "TxP.h" \endcode
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

// #include "defports.h"
// #include "bits.h"

#include "hardware/led/led_core.h"

#include "system/net/ip.h"
#include "system/net/tcp.h"
#include "system/net/ethernet.h"
#include "system/net/dns.h"
#include "system/thread/thread.h"
#include "system/config/eeconfig.h"
#include "system/clock/clock.h"
#include "system/softreset/softreset.h"

#include "apps/httpd/cgibin/cgi-bin.h"
#include "apps/httpd/httpd2_pharse.h"

#include "hardware/timer0/timer0.h"

#include "CgiFormTools.h"
#include "TxP.h"
#include "TlnBuch.h"
#include "BusKomm.h"
#include "TxP2-Defs.h"
#include "FifoPuffer.h"
#include "BaudotCode.h"
#include "Protokoll.h"
#include "TlnServer.h"
#include "eMail.h"


#ifdef TXP_ANSCHLUSS


//! Aktueller Modus. Sollte nur durch ModusWechsel geändert werden.	
TModus Modus;

	
// Die Datem auf dem TXP-Port haben folgende Struktur:
// - ASCII-Zeichen einschl. WR (CR) und ZL (LF) werden "pur" übertragen.
// - Ansonsten werden Datenblöcke übertragen, die stets aus folgenden Teilen bestehen:
//   * ein Byte Kommandocode (siehe die folgenden Konstanten mit TXPC_*)
//   * ein Byte Länge folgender Daten (kann 0 sein).
//   * zugehörige Daten

#define TXPC_NULL '\000' //!< Füllzeichen
#define TXPC_DURCHWAHL '\001' //!< Startzeichen, Datenblock enthält ein Byte Durchwahl 
#define TXPC_BAUDOT_DATA '\002' //!< Datenblock mit puren Baudot-Codes
#define TXPC_ENDE '\003' //!< Beabsichtigter Verbindungsabbau.
#define TXPC_STOP '\004' //!< Es können noch Daten angehängt werden. Ursache: Besetzt oder Störung
// \005 freigehalten für ^E = WerDa.
#define TXPC_QUITT '\006' //!< Meldet Empfangsbereitschaft und Anzahl bereits verarbeiteter Zeichen.
#define TXPC_VERSION '\007' 
	//!< Version der Kommunikation. Originate schlägt vor, Answer bestätigt.
	//!< Erst wenn andere Seite mit gleicher Nummer antwortet, ist Protokollversion abgestimmt.

/* Mustertelegramme zur Übernahme in FsTelnet (MFC-Programm)

	Text1 = _T("07 01 02 01 01 00");                       // Protokoll und Durchwahl
	Text2 = _T("02 0b 1f 02 08 16 0a 10 12 04 18 13 04");  // Text
	Text3 = _T("02 02 1b 12");                             // Kennungsabfrage
	Text4 = _T("00 00");                                   // Füllzeichen
	Text5 = _T("03 00");                                   // Ende

*/
	

#define PROTVERSION_AKTUELL 1
	//!< Aktuelle = beste Protokollversion

	
// BusVerbPartner ist in BusKomm.h enthalten

// lokale Variablen für die Umsetzung Seriell-Parallel und umgekehrt (läuft im Timer)
// ----------------------------------------------------------------------------------

//! Aktuell von Seriell nach Parallel umgesetztes Bit.
//----------------------------------------------------
//! 0 = Grundzustand, 1 = Startbit-Prüfung, 2-6 = Datenbits 1-5, 7 = Stopbit-Prüfung, 
//! 8 = Empfang beendet, Daten zur Verarbeitung bereit.
volatile uint8_t SerUmEmpfBitNr; 

//! Hier wird das von seriell zu parallel umgesetzte Byte gespeichert.
volatile uint8_t SerUmEmpfDaten; 

//! Wird auf true gesetzt, wenn Stop-Bit  nicht 1 war.
volatile bool SerUmEmpfFehler; 

//! Zähler zum Ausfiltern von kurzen Störimpulsen.
static volatile int16_t SerUmEmpfMarkZaehl; 

// Senden: Umsetzung Parallel (Daten) --> Seriell (Baudot)
//--------------------------------------------------------

//! Aktuell von parallel nach seriell umgesetztes Bit.
// ---------------------------------------------------
//! 0 = Grundzustand, 1 = Sendedaten bereit, 2 = Startbit, 3-7 = Datenbits 1-5, 8 = Stopbit.
volatile uint8_t SerUmSendBitNr; 
	
//! Aktuell von parallel nach seriell umzusetzendes Byte.
volatile uint8_t SerUmSendDaten;

//! Flag, ob aktuell Mark an die interne Gegenstelle gesendet wurde oder nicht.
volatile bool SendeMark;

// Konstanten für SerUmSendBitNr und SerUmEmpfBitNr
enum { SerUmEmpfWarte = 0, SerUmEmpfFertig = 8, SerUmSendWarte = 0, SerUmSendStart = 1 } ;

// Allgemein: 

volatile static uint8_t SerUmTickZaehlerEmpf; //!< Zähler der Einzel-Ticks beim Empfang

volatile static uint8_t SerUmTickZaehlerSend; //!< Zähler der Einzel-Ticks beim Senden


volatile uint16_t KurzTimerCnt;
//!< Die Timer-Basisvariable


volatile uint8_t KurzTimerVorteilerCnt;


volatile static uint16_t TwiLebenszeichenZaehler; 
	//!< Zählt rückwärts die Takte bis zum nächsten Lebenszeichen auf dem TWI-Bus.
	//!< wird während der Verbindung missbraucht zum Zählen der Takte bis zur Pegelwiederholung.
	//!< Kein Timer, da nur lokal in txp_timerEvent() verwendet und unterschiedliche 
	//!< Ablaufzeiten realisiert werden müssen.
	
static TKurzTimer SchreibPauseTimer;
	//!< Misst die Zeit zwischen zwei vom Endgerät empfangenen Zeichen.
	//!< Sendung wird nach 0,8 Sekunden Pause ausgelöst
	
static TKurzTimer WahlPauseTimer;
	//!< Misst die Zeit zwischen zwei vom Endgerät empfangenen Wahlziffern.
	//!< Abfrage des Rufnummern-Servers wird nach 2 Sekunden ausgelöst.
	
static TKurzTimer BusQuittTimer;
	//!< Misst die Zeit zwischen nach Einschalt-Aufforderung oder Schluss-Aufforderung.
	//!< Auf Empfang der Quittung wird nur 3 Sekunden gewartet.

	
static TKurzTimer TxpSocketLebenszeichenTimer;
	//!< Alle 3,5 bis 4 Sekunden ein Lebenszeichen senden...

static TKurzTimer TxpThreadCheckTimer;
	//!< Prüft, ob die Funktion void txp_thread() ausreichend häufig aufgerufen wird.

	
volatile TPuffer SendePuffer; 
	//!< Puffer (mit Baudot-Codes gefüllt) für die Richtung Netz -> Endgerät
	
volatile TPuffer EmpfPuffer; 
	//!< Puffer (mit Baudot-Codes gefüllt) für die Richtung Endgerät -> Netz
	
char AsciiDruckPuffer[AsciiDruckPufferMax+4];
	//!< Puffer für zu druckenden Text (Netz -> Endgerät), mit Null abgeschlossen

static char HtmlSendeText[HtmlSendeTextMax+4];
	//!< Puffer für zu Anzuzeigenden Text (Endgerät -> Netz), mit Null abgeschlossen

enum { AsciiHilfPufferMax = 100 } ;
	//!< Größe von AsciiDruckPuffer.
	
static char AsciiHilfPuffer[AsciiHilfPufferMax+4];
	//!< Hilfspuffer für Ascii-Druck: Enthält eine Zeile des AsciiPuffers, Umlaute etc. 
	//!< sind übersetzt. Zeilenumbruch wird in diesen Puffer eingebaut.
	
static uint8_t AsciiHilfZeilenanfang;
	//!< Speichert, an welcher Stelle in einer Zeile der Hilfspuffer beginnt, d.h. wieviele
	//!< Zeichen bereits vorher gedruckt worden sind. Erforderlich für automatischen Zeilenumbruch.
	
enum { Druckzeilenlaenge = 64 } ; 
	//!< Zeichen pro Zeile auf den Fernschreibern.
	

static TKurzTimer HtmlDruckspiegelAnzeigeTimer;
	//!< Zeit seit der letzten Anzeige des Druckspiegels. Druckspiegel wird alle 10 Sekunden 
	//!< abgerufen.
	
static TKurzTimer HtmlTexteingabeTimer;
	//!< Zeit seit der letzten Eingabe eines Textes auf der CGI-Seite für Direktdruck oder
	//!< seit dem letzten lokal eingegebenen Zeichen im Modus ModDirektdruckVerbunden.


int TxpSocketHandle;
	//!< Verweis auf Socket für Txp-Kommunikation. Istzustand. Wenn ungültig, aber TxpSocketMode
	//!< ungleich Idle, ist ein kurzzeitiger Verbindungsverlust eingetreten.
	
	
TTxpSocketMode TxpSocketMode;
	//!< Speichert Sollzustand der Txp-Verbindung
	
	
static long TxpSocketIP;
	//!< Aktueller Verbindungspartner. Bei TxpSocketMode = SocketAnswer wird
	//!< nach Verbindungsverlust geprüft, ob neu aufgenommene Verbindung wieder
	//!< vom gleichen Anschluss kommt.

static uint16_t TxpSocketPort;
	//!< Bei ausgehenden Verbindungen der gewünschte Port des Empfängers.

TKurzTimer TxpSocketAbbruchTimer;
	//!< Nach 30 Sekunden unplanmäßigem Verbindungsverlust wird entgültig abgebaut.

static TKurzTimer TxpSocketWiederholungVerzoegerung;
	//!< Bei spontanem Verbindungsabbau oder Sendestörung wird 2 Sekunden auf den nächsten 
	//!< Versuch gewartet.
	
	
bool TxpSocketAbbauGeplant;
	//!< Wird auf true gesetzt, wenn ein Verbindungsabbau bevorsteht.
	//!< Abbau erfolgt immer durch Anrufer. 
	//!< \p Wenn true und TxpSocketMode = SocketOriginate wird Abbau nach letzem Datenblock ausgelöst
	//!< \p Wenn true und TxpSocketMode = SocketAnswer wird nach gemeldetem Verbindungsabbau
	//!< TxpSocketMode auf SocketIdle gesetzt und TxpSocketIP gelöscht.

static TKurzTimer TxpSocketAbbauVerzoegerung;
	//!< Geht der Verbindungsabbau vom Anrufer aus, ist eine kurze Verzögerung zwischen 
	//!< letzter Sendung und Verbindungsabbau sinnvoll.
	
	
static uint8_t TxpSocketProtVersion;
	//!< Vereinbarte Protokollversion der Kommunikation

static uint8_t TxpSocketProtVersionVorschlag;
	//!< Selbst Vorgeschlagene Protokollversion der Kommunikation

TTxpSocketProtokoll TxpSocketProtokoll;
	//!< Was geht über den Socket 'rüber.
	

uint16_t SocketInBufUsed; //!< Benutzter Teil des TCP-Empfangspuffers

char SocketInBuf[SocketInBufMax+4]; //!< TCP-Empfangspuffer

	
uint16_t SocketOutBufUsed; //!< Benutzter Teil des TCP-Sendepuffers

char SocketOutBuf[SocketOutBufMax+4]; //!< TCP-Sendepuffer


uint8_t ProtokollPhase;
	//!< für POP3 und SMTP ein Speicher für den aktuellen Kommunikationsschritt


static uint16_t SocketAnzahlZeichenGesendet;
	//!< Anzahl Baudot- oder Ascii-Codes, die bisher an die Gegenstelle gesendet worden sind.
	
static uint16_t SocketAnzahlZeichenEmpfangen;
	//!< Anzahl Baudot- oder Ascii-Codes, die bisher von der Gegenstelle empfangen worden sind.
	
static uint8_t SocketAnzahlZeichenQuittiert;
	//!< Anzahl Baudot- oder Ascii-Codes, die bisher von der Gegenstelle verarbeitet worden sind.
	
volatile static bool SocketSendeQuittung;
	//!< Wenn true, werden die Anzahl der bisher gedruckten Codes zurückgemeldet.

static uint8_t SocketSendeFehlerZaehler;
	//!< Zählt bis 10 bei nicht erfolgreichen Sendeversuchen auf dem Socket.

	
static bool SendenBeschleunigen;
	//!< wird auf true gesetzt, wenn der Puffer überzulaufen droht.
	//!< bewirkt, dass das generierte Stop-Bit von 1,5 auf 1,3 verkürzt wird. 
	//!< Das ist eine Beschleunigung um 7,5/7,3 ca. 3%.
	
static uint8_t Durchwahl;
	//!< wenn != 0 wurde eine konkrete Nebenstelle gewählt.

	
static uint8_t Hauptstelle; 
	//!< Bus-Adresse für den nächsten kommenden Ruf, wird bei FesteHauptstelle = false auf die
	//!< Adresse des letzten Anrufers gesetzt. 

static bool FesteHauptstelle;
	//!< Wenn true, werden kommende Verbindungen immer auf die gleiche Endstelle gesendet
	//!< werden sollen.

static bool AlternativSucheBeiBesetzt;
	//!< Wenn true, werden bei besetzter Hauptstelle andere Endgeräte probiert.

static uint8_t DurchwahlTabelle[9];
	//!< Liste der Nebenstellen-Nummern bei kommenden Rufen mit Durchwahl

	
static uint32_t Wahlnummer; 
	//!< Momentan gewählte Nummer
	
static uint8_t Wahlziffern; 
	//!< Anzahl gewählter Ziffern
	
static TTlnDaten GewaehlterTln;
	//!< Datensatz zum aktuell gewählten Teilnehmer. Wird global gespeichert, um 
	//!< Aktualisierungen vom Teilnehmer-Server "einpflegen" zu können.
	
static bool TlnServerAbfrageWiederholungssperre;
	//!< Bewirkt, dass der Teilnehmer-Server nur ein mal je gewählte Ziffer abgefragt wird.
	
	
static uint32_t NetzRufnummer;
	//!< Rufnummer des eigenen Anschlusses im ip-telex-Netz
	
static uint16_t Geheimzahl;
	//!< Um unberechtigte Fremd-Aktualisierungen zu vermeiden.

static uint16_t NetzPort;
	//!< Gewünschte Port-Nummer im globalen Netz. Kann aus bestimmten Gründen von TXP_PORT (134) abweichen.

static long NetzEigeneIP;
	//!< Zurückgemeldete IP-Adresse im globalen Netz (nur für Diagnose)

#endif // TXP_ANSCHLUSS
	

#define ANZ_TEILNEHMER_SERVER 3
	
static char TeilnehmerServerAdresse[ANZ_TEILNEHMER_SERVER][TlnAdresseMax];
	//!< URL's oder IP's der Teilnehmer-Server.

enum { KonfigPasswortLen = 10 } ;
	//!< maximale Länge des Passworts für den Zugang zu Konfigurationsdaten.

static char KonfigPasswort[KonfigPasswortLen+1];
	//!< Passwort für den Zugang zu Konfigurationsdaten.
	
static TKurzTimer KonfigFreigabeTimer;
	//!< Timer zur Messung der Zeit seit letzter Freigabe bzw. Benutzung von freizugebenden Seiten
	
static bool KonfigFreigabeErteilt;
	//!< Damit Überlauf des #KonfigFreigabeTimer nicht zur wieder-Freigabe führt.
	
	
static int TeilnehmerServerSocket;
	//!< Handle für ausgehende Verbindungen zum Teilnehmer-Server
	//!< Wird in ZWEI Situationen benutzt: 
	//!< a) Dynamische IP-Aktualisierung
	//!< b) Abfrage einer Teilnehmer-Adresse
	
	
#ifdef TXP_ANSCHLUSS

static bool DynIPAktiv;
	//!< Soll die eigene IP-Adresse auf den Teilnehmer-Server aktualisiert werden?
	
static TKurzTimer DynIPAktualisierungTimer;
	//!< Macht alle 15 Minuten eine Aktualsisierungsmeldung an einen der Teilnehmer-Server (sofern aktiviert).

static uint16_t DynIPAktualisierungEndzeit;
	//!< Wann soll die nächste Aktualisierung sein?

//! Sollfrequenz des Aufrufs von txp_timerEvent()
enum { TxpTimerFreq = 50 * 10 } ; // 50 Baud mit 10 Takten je Bit	


#endif // TXP_ANSCHLUSS


enum { DebugMsgMax = 100 } ;

char DebugMsg[DebugMsgMax];
	//!< String für außergewöhnliche Fälle

	
TTastendruck Tastendruck;

	
// LEDs
// ----	
#define ROT 0
#define GELB 1
#define GRUEN 2
#define BLAU 3


static inline uint8_t low(uint16_t x)
	{
	return x & 0xFF;
	}
	
	
#ifdef TXP_ANSCHLUSS
	
//! Initialisiert die serielle Umsetzung 
static void SeriellUmsetzInit(void)
	{
	SerUmEmpfBitNr = SerUmEmpfWarte;
	SerUmSendBitNr = SerUmSendWarte;
	SerUmTickZaehlerEmpf = 10;
	SerUmTickZaehlerSend = 0;
	SendeMark = true;
	}

	
volatile static uint8_t Timer0Cnt_Min;
volatile static uint8_t Timer0Cnt_Max;
volatile static uint8_t Timer0Callback_Max;
volatile static uint32_t Timer0CallbackCount; 

//! Timer-Callback-Funktion. Macht seriell-parallel-Umsetzung und umgekehrt.
//--------------------------------------------------------------------------
//! Sendet auf TWI auch die Mark- / Space-Wechsel und die Lebenszeichen.
//! Wird mit Frequenz TxpTimerFreq aufgerufen.

void txp_timerEvent(void)
	{
	uint8_t t0c = TCNT0;
	Timer0CallbackCount++;

	KurzTimerVorteilerCnt++;
	if (KurzTimerVorteilerCnt >= TxpTimerFreq / 10)
		{
		KurzTimerCnt++;
		KurzTimerVorteilerCnt = 0;
		}
		
	wdt_reset();
	
	if (TimerVal(&TxpThreadCheckTimer) > 300) // nach 30 Sekunden Reset
		{ 
		Protokollieren("TxP: Reset wegen nicht-Aufruf von txp_thread()\r\n");
		ProtokollSpeichern(true);
		softreset();
		}
		
#if defined(LEDROT_TXPTHREADBLOCK)
	if (TimerVal(&TxpThreadCheckTimer) > 5) // nach halber Sekunde geht rot an
		LED_on(ROT);
#endif //defined(LEDROT_TXPTHREADBLOCK)
		
	TwiWatchdogCount++; 
		
	if (Modus == ModKommendVerbunden 
		|| Modus == ModGehendVerbunden 
		|| Modus == ModDirektdruckVerbunden 
		|| Modus == ModPufferDruckUndSchluss)
		{ // ist Verbunden, also Pegel senden und empfangen
		bool NeuMark = true; // wird beim Senden vielleicht noch geändert

		if (t0c < Timer0Cnt_Min)
			Timer0Cnt_Min = t0c;
		if (t0c > Timer0Cnt_Max)
			Timer0Cnt_Max = t0c;
		// Statistik über den Zeitverzug...
		
		if (SerUmEmpfBitNr != SerUmEmpfWarte && SerUmEmpfBitNr != SerUmEmpfFertig)
			{ // Empfang läuft
			if (--SerUmTickZaehlerEmpf <= 2)
				{ // 3 Abtast-Zeitpunkte (Zaehler = 2,1,0) im Bit
				if (BusEmpfMark)
					SerUmEmpfMarkZaehl++;
				}
				
			if (SerUmTickZaehlerEmpf == 0)
				{ // erst bei 0 auswerten 
				if (SerUmEmpfBitNr == 1) // im Start-Bit
					{
					if (SerUmEmpfMarkZaehl > 1) // zu viele 1-Impulse im Startbit --> von vorn
						SerUmEmpfBitNr = SerUmEmpfWarte;
					else
						SerUmEmpfBitNr = 2;
					}
				else if (SerUmEmpfBitNr == 7) // im Stop-Bit
					{
					SerUmEmpfFehler = SerUmEmpfMarkZaehl < 2; 
					SerUmEmpfBitNr = SerUmEmpfFertig; //! \todo nur dann Empfang abschließen, wenn auch ein Stop-Bit da war

					// und gleich in den Puffer...
					if (!SerUmEmpfFehler)
						{
						PufferSpeich(&EmpfPuffer, SerUmEmpfDaten);
						SerUmEmpfBitNr = SerUmEmpfWarte;
						}
					}
				else // im Datenbit
					{
					SerUmEmpfDaten <<= 1;
					if (SerUmEmpfMarkZaehl >= 2)
						SerUmEmpfDaten |= 1;
					SerUmEmpfBitNr++;
					SerUmEmpfMarkZaehl = 0;
					}

				SerUmEmpfMarkZaehl = 0;
				SerUmTickZaehlerEmpf = 10;
				} // Abtastung eines Bits abgeschlossen
			StartTimer(&SchreibPauseTimer);
			} // if Empfang läuft
		else // SerUmEmpfBitNr == 0 || SerUmEmpfBitNr == SerUmEmpfFertig
			{ // Empfang ruht 
			if (!BusEmpfMark) // Pausenschritt
				{
				SerUmEmpfBitNr = 1;
				SerUmEmpfDaten = 0;
				SerUmEmpfFehler = false;
				SerUmEmpfMarkZaehl = 0;
				SerUmTickZaehlerEmpf = 6; // nicht 10, da in der Mitte der Bits abgetastet wird
				}
			else
				{
				// Ausgabe starten?
				if (SerUmSendBitNr == SerUmSendStart)
					{
					SerUmSendDaten = ((SerUmSendDaten & 0x1F) << 2) | 0x03; 
						// neue Anordnung in SerUmSendDaten: Bit 7 = Start-Bit = 0, Bit 6..2 = Datenbits, Bit 1/0 = Stop-Bit = 1
					SerUmTickZaehlerSend = 0;
					SerUmSendBitNr = 2;
					} // SerUmSendBitNr == SerUmSendStart
				else if (SerUmSendBitNr == SerUmSendWarte && !PufferLeer(&SendePuffer))
					{
					SerUmSendDaten = PufferAusg(&SendePuffer);
					SerUmSendBitNr = SerUmSendStart;
					if (PufferLeer(&SendePuffer))
						SocketSendeQuittung = true;
					}
				}
			} // else Empfang ruht

		if (SerUmSendBitNr >= 2)
			{ // Sendung läuft
			if (SerUmSendBitNr == 8) // Stop-Bit läuft
				{
				NeuMark = true;
				if (++SerUmTickZaehlerSend >= (SendenBeschleunigen ? 12 : 14)) // 12 und 14 empirisch ermittelt...
				//if (++SerUmTickZaehlerSend >= (SendenBeschleunigen ? 12 : 16)) // HACK Wert 16: Simulation zu schneller Sender
				//if (++SerUmTickZaehlerSend >= ((!get_Taste() || SendenBeschleunigen) ? 12 : 14)) // HACK Test wegen Auswirkung des schnellen Sendens....
					SerUmSendBitNr = SerUmSendWarte; // fertig für die nächsten Daten
				}
			else
				{ // Start oder Datenbit läuft
				NeuMark = BIT_IS_SET(SerUmSendDaten, 7);
				if (++SerUmTickZaehlerSend >= 10)
					{
					SerUmSendDaten <<= 1;
					SerUmSendBitNr++;
					SerUmTickZaehlerSend = 0;
					}
				}
			} // SerUmSendBitNr zwischen 2 und 8
			
		if (NeuMark && !SendeMark)
			{
			BusSenden(BusKdoMark);
			SendeMark = true;
			TwiLebenszeichenZaehler = 2; // sofort Wiederholungszeichen senden
			LED_off(BLAU);
			SET_BIT_Status(StatBit_FsBefEin);
			}
		else if (!NeuMark && SendeMark)
			{
			BusSenden(BusKdoSpace);
			SendeMark = false;
			TwiLebenszeichenZaehler = 2; // sofort Wiederholungszeichen senden
			LED_on(BLAU);
			CLR_BIT_Status(StatBit_FsBefEin);
			}
		else
			{ // kein Sendepegel-Wechsel
			// Lebenszeichen = Aktuellen Pegel regelmäßig senden
			if (TwiLebenszeichenZaehler > 0)
				TwiLebenszeichenZaehler--;
			else
				{ // Lebenszeichen wenn möglich senden
				if ((BusAuftrag == Nichts || BusAuftrag == Fertig) && BusFrei)
					{
					BusSenden(SendeMark ? BusKdoMarkWdh : BusKdoSpaceWdh);
					TwiLebenszeichenZaehler = TxpTimerFreq * 5/10; // alle 0,5 Sekunden
					}
				}
			} // kein Sendepegel-Wechsel

		// Status-Anzeige
		if (BusEmpfMark)
			{
			SET_BIT_Status(StatBit_FsMeldEin);
			if (Modus == ModGehendVerbunden)
				LED_off(GRUEN);
			else
				LED_off(GELB);
			}
		else
			{
			CLR_BIT_Status(StatBit_FsMeldEin);
			if (Modus == ModGehendVerbunden)
				LED_on(GRUEN);
			else
				LED_on(GELB);
			}
		} // if "Verbunden"

	else if (Modus != ModRuhe && Modus != ModWarteSchlussQuitt && Modus != ModWarteGrundstellung)
		{ // Lebenszeichen regelmäßig senden
		if (TwiLebenszeichenZaehler > 0)
			TwiLebenszeichenZaehler--;
		else
			{ // Lebenszeichen wenn möglich senden
			if ((BusAuftrag == Nichts || BusAuftrag == Fertig) && BusFrei)
				{
				BusSenden(BusLebenszeichen);
				TwiLebenszeichenZaehler = TxpTimerFreq * 5/10; // alle 0,5 Sekunden
				}
			}
		} // if Modus != Ruhe

	// Taste prüfen und auswerten
	// -------------------------------
	static enum { TasteAus, TasteEin, TasteSperr } TasteZustandIntern;
		// Speichert den letzten Zustand der Taste.
	static TKurzTimer TasteTimer;

	switch (TasteZustandIntern)
		{
		case TasteAus:
			if (!get_Taste()) // Gedrückt = LOW!
				{
				if (TimerVal(&TasteTimer) >= 2) // 2 Zehntel
					{ // ausreichend lang gedrückt
					TasteZustandIntern = TasteEin;
					StartTimer(&TasteTimer);
					
					// Zugang zur Konfiguration erlauben.
					KonfigFreigabeErteilt = true;
					StartTimer(&KonfigFreigabeTimer);
					}
				}
			else
				{
				StartTimer(&TasteTimer);
				}
			break;

		case TasteEin: 
			if (!get_Taste()) // Gedrückt = LOW!
				{
				if (TimerVal(&TasteTimer) >= 8) // 0,8 Sekunden
					{ // lang gedrückt
					TasteZustandIntern = TasteSperr;
					Tastendruck = Lang;
					}
				}
			else
				{ // Taste wieder früh losgelassen
				TasteZustandIntern = TasteAus;
				Tastendruck = Kurz;
				}
			break;

		case TasteSperr:
			if (!get_Taste()) // Gedrückt = LOW!
				{
				// immer noch gedrückt...
				}
			else
				{ 
				TasteZustandIntern = TasteAus;
				}
			break;

		default:
			TasteZustandIntern = TasteSperr;
			Tastendruck = NichtGedr;
			StartTimer(&TasteTimer);
			break;
			
		} // switch (TasteZustandIntern)
	
	t0c = TCNT0 - t0c;
	if (t0c > Timer0Callback_Max)
		Timer0Callback_Max = t0c;
	}

	
//! Speichert ungültige Befehle vom TWI-Bus.
uint8_t FalscherCode = 0;


//! Speichert ungültige Befehle vom TWI-Bus.
static void FalschCodeEmpfangen(uint8_t Code)
	{
	if (FalscherCode == 0)
		FalscherCode = Code;
	}


/*	
//! Druckt am verbundenen Fernschreiber Datum und Uhrzeit des Anrufs
//-------------------------------------------------------------------
static void DatumDruckenUndAusschalten()
	{
	if (!EndgeraetEinschalten)
		return;

	struct TIME Time;
	// Zeit holen
	CLOCK_GetTime(&Time);
	
	char *p = AsciiDruckPuffer;
	while (*p != '\0' && p < AsciiDruckPuffer + AsciiDruckPufferMax - 50) // 50 ist die Länge des Datum-Strings
		p++;

	sprintf_P(p, PSTR("\r\n\ndatum: %02u.%02u.%04u  uhrzeit: %02d:%02d:%02d\r\n\n"),
			  Time.DD, Time.MM, Time.YY, Time.hh, Time.mm, Time.ss);
	EndgeraetEinschalten = false;
	}
*/	
	

//! Bewirkt Moduswechsel.
//-----------------------
//! Erledigt auch folgende Aufgaben:
//! \par - LED-Anzeigen aktualisieren
//! \par - Status (für TWI-Abfrage) aktualisieren
//! \par - Puffer-Initialisierung
void ModusWechsel(TModus neu)
	{
	if (neu == Modus)
		return;

	TwiWatchdogCount = 0; // nicht in allen Modi erforderlich, schadet aber auch nicht.
		
	switch (neu)
		{
		case ModRuhe: // nichts läuft
			CLR_BIT_Status(StatBit_Verbunden);
			CLR_BIT_Status(StatBit_AngerufenBelegt);
			CLR_BIT_Status(StatBit_FsBefBetrieb);
			CLR_BIT_Status(StatBit_FsMeldBetrieb);
			CLR_BIT_Status(StatBit_FsBefEin);
			CLR_BIT_Status(StatBit_FsMeldEin);
			SET_BIT_Status(StatBit_Frei);
			SET_BIT_Status(StatBit_LeitungKennung);
			LED_off(GELB);
			LED_off(GRUEN);
			LED_off(BLAU);
			AsciiDruckPuffer[0] = '\0';
			AsciiHilfPuffer[0] = '\0';
			AsciiHilfZeilenanfang = 0;
			break;
	
		// Gehend = vom internen Anschluss zum Netz, Reservierung ist eingegangen
		case ModGehendReserv:
			CLR_BIT_Status(StatBit_Frei);
			CLR_BIT_Status(StatBit_LeitungKennung);
			CLR_BIT_Status(StatBit_Verbunden);
			SET_BIT_Status(StatBit_FsMeldBetrieb);
			SET_BIT_Status(StatBit_FsMeldEin);
			CLR_BIT_Status(StatBit_FsBefBetrieb);
			CLR_BIT_Status(StatBit_FsBefEin);
			CLR_BIT_Status(StatBit_AngerufenBelegt);
			LED_on(GELB);
			LED_off(GRUEN);
			LED_off(BLAU);
			BusEmpfMark = true;
			SendeMark = true;
			PufferInit(&SendePuffer);
			PufferInit(&EmpfPuffer); EmpfPuffer.BuZiMode = BuMode;
			SeriellUmsetzInit();
			SendenBeschleunigen = false;
			TxpSocketProtokoll = TelexPhone;
			AsciiDruckPuffer[0] = '\0';
			AsciiHilfPuffer[0] = '\0';
			AsciiHilfZeilenanfang = 0;
			SocketAnzahlZeichenEmpfangen = 0;
			SocketAnzahlZeichenGesendet = 0;
			SocketAnzahlZeichenQuittiert = 0;
			SocketSendeFehlerZaehler = 0;
			break;
	
		case ModGehendWaehlen:
			Wahlnummer = 0;
			Wahlziffern = 0;
			TlnDatenInit(&GewaehlterTln);
			TlnServerAbfrageWiederholungssperre = true; // wird nach erster Ziffer auf false gesetzt
			StartTimer(&WahlPauseTimer);
			break;
	
		case ModGehendVerbunden:
			SET_BIT_Status(StatBit_Verbunden);
			SET_BIT_Status(StatBit_FsMeldBetrieb);
			SET_BIT_Status(StatBit_FsMeldEin);
			SET_BIT_Status(StatBit_FsBefBetrieb);
			SET_BIT_Status(StatBit_FsBefEin);
			BusEmpfMark = true;
			SendeMark = true;
			LED_on(GELB);
			LED_off(GRUEN);
			LED_off(BLAU);
			StartTimer(&SchreibPauseTimer);
			break;
	
		// Kommend = vom Netz zum internen Anschluss
		case ModKommendVerbVorstufe: // es wird erst mal abgewartet, was aus der ankommenden Verbindung wird.
			// trotzdem sofort abgehende Verbindungen sperren.
			CLR_BIT_Status(StatBit_Frei);
			CLR_BIT_Status(StatBit_LeitungKennung);
			CLR_BIT_Status(StatBit_Verbunden);
			CLR_BIT_Status(StatBit_FsMeldBetrieb);
			CLR_BIT_Status(StatBit_FsMeldEin);
			CLR_BIT_Status(StatBit_FsBefBetrieb);
			CLR_BIT_Status(StatBit_FsBefEin);
			SET_BIT_Status(StatBit_AngerufenBelegt);
			LED_off(GELB);
			LED_on(GRUEN);
			LED_off(BLAU);
			BusEmpfMark = true;
			SendeMark = true;
			PufferInit(&SendePuffer);
			PufferInit(&EmpfPuffer); EmpfPuffer.BuZiMode = BuMode;
			SeriellUmsetzInit();
			AsciiDruckPuffer[0] = '\0';
			AsciiHilfPuffer[0] = '\0';
			AsciiHilfZeilenanfang = 0;
			SendenBeschleunigen	= false;
			Durchwahl = 0;
			SocketAnzahlZeichenEmpfangen = 0;
			SocketAnzahlZeichenGesendet = 0;
			SocketAnzahlZeichenQuittiert = 0;
			SocketSendeFehlerZaehler = 0;
			break;

		case ModKommendEinschalten:
			SET_BIT_Status(StatBit_FsBefBetrieb);
			SET_BIT_Status(StatBit_FsBefEin);
			break;
		
		case ModKommendWarteEinQuitt: // Warte auf Einschalt-Quittung des Endgeräts
			SET_BIT_Status(StatBit_FsBefBetrieb);
			SET_BIT_Status(StatBit_FsBefEin);
			StartTimer(&BusQuittTimer);
			break;
	
		case ModKommendVerbunden: 
			SET_BIT_Status(StatBit_FsMeldBetrieb);
			SET_BIT_Status(StatBit_FsMeldEin);
			SET_BIT_Status(StatBit_Verbunden);
			BusEmpfMark = true;
			SendeMark = true;
			StartTimer(&SchreibPauseTimer);
			break;
	
		case ModPufferDruckUndSchluss: 
			CLR_BIT_Status(StatBit_Verbunden);
			break;
		
		case ModWarteSchlussQuitt:
			CLR_BIT_Status(StatBit_FsBefBetrieb);
			CLR_BIT_Status(StatBit_FsBefEin);
			CLR_BIT_Status(StatBit_Verbunden);
			StartTimer(&BusQuittTimer);
			break;

		case ModDirektdruckWarteEinQuitt: //!< Warte auf Einschalt-Quittung des Endgeräts
			CLR_BIT_Status(StatBit_Frei);
			CLR_BIT_Status(StatBit_LeitungKennung);
			CLR_BIT_Status(StatBit_Verbunden);
			CLR_BIT_Status(StatBit_FsMeldBetrieb);
			CLR_BIT_Status(StatBit_FsMeldEin);
			SET_BIT_Status(StatBit_FsBefBetrieb);
			SET_BIT_Status(StatBit_FsBefEin);
			SET_BIT_Status(StatBit_AngerufenBelegt);
			LED_off(GELB);
			LED_on(GRUEN);
			LED_off(BLAU);
			PufferInit(&SendePuffer);
			PufferInit(&EmpfPuffer); EmpfPuffer.BuZiMode = BuMode;
			SeriellUmsetzInit();
			SendenBeschleunigen = false;
			SocketAnzahlZeichenEmpfangen = 0;
			SocketAnzahlZeichenGesendet = 0;
			SocketAnzahlZeichenQuittiert = 0;
			SocketSendeFehlerZaehler = 0;
			break;
	
		case ModDirektdruckVerbunden: 
			SET_BIT_Status(StatBit_FsMeldBetrieb);
			SET_BIT_Status(StatBit_FsMeldEin);
			SET_BIT_Status(StatBit_Verbunden);
			BusEmpfMark = true;
			SendeMark = true;
			StartTimer(&HtmlTexteingabeTimer);
			StartTimer(&HtmlDruckspiegelAnzeigeTimer);
			break;

		case ModDeaktiviert:
			CLR_BIT_Status(StatBit_Frei);
			CLR_BIT_Status(StatBit_LeitungKennung);
			CLR_BIT_Status(StatBit_Verbunden);
			CLR_BIT_Status(StatBit_FsMeldBetrieb);
			CLR_BIT_Status(StatBit_FsMeldEin);
			CLR_BIT_Status(StatBit_FsBefBetrieb);
			CLR_BIT_Status(StatBit_FsBefEin);
			CLR_BIT_Status(StatBit_AngerufenBelegt);
			LED_off(GELB);
			LED_off(GRUEN);
			LED_on(BLAU);
			break;

		case ModWarteGrundstellung:
			CLR_BIT_Status(StatBit_Verbunden);
			CLR_BIT_Status(StatBit_AngerufenBelegt);
			CLR_BIT_Status(StatBit_FsBefBetrieb);
			CLR_BIT_Status(StatBit_FsMeldBetrieb);
			CLR_BIT_Status(StatBit_FsBefEin);
			CLR_BIT_Status(StatBit_FsMeldEin);
			CLR_BIT_Status(StatBit_Frei);
			CLR_BIT_Status(StatBit_LeitungKennung);
			LED_off(GELB);
			LED_off(GRUEN);
			LED_on(BLAU);
			AsciiDruckPuffer[0] = '\0';
			AsciiHilfPuffer[0] = '\0';
			AsciiHilfZeilenanfang = 0;
			break;
			
		default:
			return; // nix wird geändert
		} // switch neu

	Modus = neu; // jetzt wird der neue Modus wirklich aktiv.
	}
	
	
//! TCP-Puffer initialisieren
void SocketBufInit()
	{
	SocketInBufUsed = 0;
	SocketOutBufUsed = 0;
	StartTimer(&TxpSocketLebenszeichenTimer);
	}
	
	
//! Empfangene Daten vom Socket in den SendePuffer schreiben.
//! \retval true, wenn Zeichen gedruckt wird (ausgegeben wird).
static bool SchreibeZeichenInSendePuffer(char c)
	{
	uint8_t Code1, Code2;
	
	if (c == '@') 
		{ // Kennungsgeber besonders behandeln...
		return PufferSpeich(&SendePuffer, TtyCodeZiUm) && PufferSpeich(&SendePuffer, TtyCodeZiWerDa);
		}
	else 
		{
		if (ZeichenZuCode2(c, (char*) &SendePuffer.BuZiMode, &Code1, &Code2))
			{ // Zeichen erfolgreich in Baudot-Code umgesetzt
			return PufferSpeich(&SendePuffer, Code1) && (Code2 == 255 || PufferSpeich(&SendePuffer, Code2));
			}
		else
			// Zeichen ist nicht darstellbar, also löschen
			{
			if (DebugMsg[0] == '\0') // noch leer
				strcpy_P(DebugMsg, PSTR("?nicht druckbare Zeichen: "));
			uint8_t l = strlen(DebugMsg);
			if (DebugMsg[0] == '?' && l + 2 < DebugMsgMax)
				{
				DebugMsg[l] = c;
				DebugMsg[l+1] = '\0';
				}
			return false;
			}
		} // kein Werda
	}
			
			
//! Bei kommenden Verbindungen aller Art (TelexPhone, HTML) passenden internen 
//! Empfänger ermitteln und anwählen.
//-----------------------------------------------------------------------------			
//! Setzt als Ergebnis BusVerbPartner. 
//! \param aDurchwahl Bevorzugstes Endgerät lokal. 0 bei keiner Bevorzugung.
//! \retval true Ein Endgerät gefunden und erfolgreich Reserviert.
//! \retval false Intern alle in Frage kommenden Endgeräte besetzt.
static bool KommendInternAnwaehlen(uint8_t aDurchwahl)
	{
	int16_t Stat;
	uint8_t TestVerbParter = 0;
	
	// zuerst Durchwahl prüfen...
	if (aDurchwahl * 2 >= BusAdrMin && aDurchwahl * 2 <= BusAdrEndgeraetMax)
		{
		TestVerbParter = aDurchwahl * 2;
		Stat = GetStatus(TestVerbParter);
		if (Stat >= 0 && !BIT_IS_SET(Stat, StatBit_LeitungKennung))
			// Gerät ist auf jeden Fall vorhanden und geeignet
			if (BIT_IS_SET(Stat, StatBit_Frei))
				; // alles gut
			else
				{ // besetzt
				BusVerbPartner = 0;
				return false;
				}
		else 
			TestVerbParter = 0; // Hauptstelle suchen
		}

	if (TestVerbParter == 0)
		{ // keine Durchwahl oder Durchwahl ungeeignet...
		// Hauptstelle prüfen:
		if (Hauptstelle == 0)
			Hauptstelle = BusAdrMin;

		TestVerbParter = Hauptstelle;
		
		while (true) // Abbruch in der Schleife
			{
			Stat = GetStatus(TestVerbParter);
			if (Stat >= 0 
				&& BIT_IS_SET(Stat, StatBit_Frei) 
				&& !BIT_IS_SET(Stat, StatBit_LeitungKennung)
				&& (!BIT_IS_SET(Stat, StatBit_SpezialGeraetKennung) || (TestVerbParter == Hauptstelle)))
				break; // gefunden, Hurra!
				
			if (!AlternativSucheBeiBesetzt)
				{ // es soll kein anderer angerufen werden
				BusVerbPartner = 0;
				return false;
				}
				
			// nächsten probieren
			TestVerbParter += 2;
			
			if (TestVerbParter > BusAdrEndgeraetMax)
				// Ende der Liste --> also von vorn.
				TestVerbParter = BusAdrMin;
				
			if (TestVerbParter == Hauptstelle)
				// da wurde mal angefangen, also alle ein mal probiert...
				{
				BusVerbPartner = 0;
				return false;
				}
				
			// wdt_reset();
			
			} // while true
		} // Keine Durchwahl oder Durchwahl ungeeignet
		
	BusVerbPartner = TestVerbParter;

	BusSenden(BusEigenAdresse >> 1);
	BusWarteFertig();
	
	bool Res = (BusErgebnis == Ok);
	
	BusErgebnis = Ok; // um spätere Probleme zu vermeiden
	BusAuftrag = Nichts;

	return Res;
	
	} // KommendInternAnwaehlen()
	

//! Socket bearbeiten.
//---------------------------------------------------------------------------
//! Aufgaben:
//! \par - Empfangene Daten in den Socket-Empfangspuffer schreiben
//! \par - Daten vom Socket-Empfangspuffer übersetzen in den SendePuffer (zum Endgerät).
//! \par - Daten des Empfangspuffers (Endgerät) in den Socket-Sendepuffer übersetzen
//! \par - Daten des Socket-Sendepuffers ggf. senden
//! \par - Schlusszeichen bearbeiten
//! \par - Öffnungs- und Schließanforderung bearbeiten

static void SocketBearbeiten()
	{
	extern struct TCP_SOCKET TCP_sockettable[];

	// Neue Verbindungswünsche bearbeiten
	// ----------------------------------
	int NewServerSocket = CheckPortRequest(TXP_PORT);
	if (NewServerSocket != NO_SOCKET_USED)
		{
		bool Abweisen = true; // Bei berechtigter kommender Verbindung auf false setzen.

		if (ProtokollLevel >= 1)
			{
			Protokollieren_P(PSTR("TxP: Server-Socket geoeffnet von IP "));
			ProtokollierenIPAdr(TCP_sockettable[NewServerSocket].SourceIP);
			Protokollieren_P(PSTR(" / MAC "));
			ProtokollierenMAC(TCP_sockettable[NewServerSocket].MACadress);
			}
		
		if (TxpSocketMode == SocketIdle)
			{ // neue Verbindung
			if (Modus == ModRuhe)
				{ // ID#102 *************************************************
				// Wenn ja, Startmeldung ausgeben und startzustand herstellen für i2c
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR(" ...neu ok\r\n"));
				TxpSocketHandle = NewServerSocket;
				BusVerbPartner = Hauptstelle; // vorbereitet...
				TxpSocketIP = TCP_sockettable[TxpSocketHandle].SourceIP;
				TxpSocketMode = SocketAnswer;
				TxpSocketAbbauGeplant = false;
				TxpSocketProtVersion = 0;
				TxpSocketProtVersionVorschlag = 0; // auf Gegenvorschlag warten
				TxpSocketProtokoll = TelexPhone; // versuch...
				StartTimer(&TxpSocketAbbruchTimer);
				StartTimer(&TxpSocketAbbauVerzoegerung);
				SocketBufInit();
				ModusWechsel(ModKommendVerbVorstufe);
				Abweisen = false;
				}
			else
				{
				Abweisen = true; // anderweitig belegt
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR(", anderweitig belegt"));
				}
			#ifdef LEDROT_SOCKETERROR
				LED_off(ROT);
			#endif //def LEDROT_SOCKETERROR
			} // TxpSocketMode == SocketIdle
			
		else if (TxpSocketMode == SocketAnswer && TxpSocketHandle == NO_SOCKET_USED)
			{
			if (TxpSocketIP == TCP_sockettable[NewServerSocket].SourceIP)
				{
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR(" ...Wiederverbindung ok\r\n"));
				TxpSocketHandle = NewServerSocket;
				StartTimer(&TxpSocketAbbauVerzoegerung);
				Abweisen = false;
				#ifdef LEDROT_SOCKETERROR
					LED_off(ROT);
				#endif //def LEDROT_SOCKETERROR
				}
			else
				{
				Abweisen = true; 
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR(", andere kommende Verbindung besteht"));
				}
			} // (TxpSocketMode == SocketAnswer && TxpSocketIP == NO_SOCKET_USED)
			
		else 
			{ // TxpSocketMode == SocketOriginate || TxpSocketHandle bereits belegt
			Abweisen = true; // anderweitig belegt
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR(", Verbindung besteht"));
			}
		
		if (Abweisen)
			{ // ID#213 ID#225 ***************************************************
			PutSocketData_RPE(NewServerSocket, 7, PSTR("\004\005occ\r\n"), FLASH); // 004 = TXPC_STOP
			CloseTCPSocket(NewServerSocket);
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR(" ...ABGEWIESEN\r\n" ));
			}
			
		} // CheckPortRequest(TXP_PORT) != NO_SOCKET_USED

	// Verbindungsabbruch durch Gegenseite?
	// --------------------------------------------------
	if (TxpSocketHandle != NO_SOCKET_USED 
		&& CheckSocketState(TxpSocketHandle) == SOCKET_NOT_USE
		&& SocketInBufUsed == 0) // Verbindungsabbau verzögern bis Puffer verarbeiet.
		{ // ID#242 ID#342 ID#314 ************************************************
		switch (TxpSocketProtokoll)
			{
			case TelexPhone:
				if (!TxpSocketAbbauGeplant)
					{
					if (ProtokollLevel >= 1)
						Protokollieren_P(PSTR("TxP: Socket wurde von Gegenstelle UNERWARTET geschlossen\r\n" ));
					#ifdef LEDROT_SOCKETERROR
						LED_on(ROT);
					#endif //def LEDROT_SOCKETERROR
					break; // des switch
					}
				// sonst weiter mit Ascii, kein break;
				
			case Ascii:
				// oder TelexPhone und AbbauGeplant
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: Socket wurde von Gegenstelle erwartet geschlossen\r\n" ));
				TxpSocketMode = SocketIdle;
				TxpSocketIP = 0;
				TxpSocketAbbauGeplant = false;
				SocketOutBufUsed = 0;
				SocketInBufUsed = 0;
				#ifdef LEDROT_SOCKETERROR
					LED_off(ROT);
				#endif //def LEDROT_SOCKETERROR
				break;
				
			default:
				if (!TxpSocketAbbauGeplant)
					Protokollieren_P(PSTR("TxP: Socket wurde von Gegenstelle GETRENNT\r\n" ));
				else if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: Socket wurde von Gegenstelle erwartet geschlossen\r\n" ));
				TxpSocketMode = SocketIdle;
				TxpSocketIP = 0;
				TxpSocketAbbauGeplant = false;
				SocketOutBufUsed = 0;
				SocketInBufUsed = 0;
				break;
				
			}
			
		CloseTCPSocket(TxpSocketHandle);
		StartTimer(&TxpSocketAbbruchTimer);
		StartTimer(&TxpSocketWiederholungVerzoegerung);
		TxpSocketHandle = NO_SOCKET_USED;
		return; // GGf wieder aufnahme der Verbindung beim nächsten Aufruf dieser funktion...
		}
		
	// soll offene Verbindung geschlossen werden?
	// --------------------------------------------------
	if (TxpSocketHandle != NO_SOCKET_USED 
		&& TxpSocketAbbauGeplant 
		&& TxpSocketMode == SocketOriginate 
		&& TimerVal(&TxpSocketAbbauVerzoegerung) > 15 // 1,5 Sekunden nach letzter Sendung...
		&& SocketOutBufUsed == 0
		&& SocketInBufUsed == 0)
		{ 
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Socket wird aktiv geschlossen\r\n" ));
		CloseTCPSocket(TxpSocketHandle);
		TxpSocketHandle = NO_SOCKET_USED;
		TxpSocketMode = SocketIdle;
		TxpSocketIP = 0;
		TxpSocketAbbauGeplant = false;
		SocketOutBufUsed = 0;
		SocketInBufUsed = 0;
		#ifdef LEDROT_SOCKETERROR
			LED_off(ROT);
		#endif //def LEDROT_SOCKETERROR
		}
		
	// Auf neue Daten testen
	// ---------------------------------
	if (TxpSocketHandle != NO_SOCKET_USED && SocketInBufUsed < SocketInBufMax)
		{ // Socket offen und Puffer aufnahmefähig
		StartTimer(&TxpSocketAbbruchTimer);
			// so lange Verbindung aufrecht bleibt Timer auf 0

		int InCount = GetBytesInSocketData(TxpSocketHandle);
		
		if (SocketInBufUsed + InCount > SocketInBufMax)
			{
			if (ProtokollLevel >= 1) 
				{
				ProtokollierenInt_P(PSTR("TxP: Socket Empfang drohender Ueberlauf: Empfang von %d " ), InCount);
				ProtokollierenInt_P(PSTR("limitiert auf %d\r\n" ), SocketInBufMax - SocketInBufUsed);
				}
			InCount = SocketInBufMax - SocketInBufUsed;
			}
			
		if (InCount > 0) 
			{
			int Res = GetSocketData(TxpSocketHandle, InCount, SocketInBuf + SocketInBufUsed);
			
			if (ProtokollLevel == 3) // Daten explizit
				{
				ProtokollierenInt_P(PSTR("TxP: Socket Empfang: (%d/" ), InCount);
				ProtokollierenInt_P(PSTR("%d)"), Res);
				ProtokollierenPuffer(SocketInBuf + SocketInBufUsed, Res);
				ProtokollierenInt_P(PSTR(" --> BufUsed %u\r\n"), SocketInBufUsed + Res);
				}		
				
			if (Res > 0)
				SocketInBufUsed += Res;
				
			}

		} // if TxpSocketHandle != NO_SOCKET_USED 
		
	// ggf Lebenszeichen erzeugen
	// --------------------------
	if (TxpSocketMode != SocketIdle
		&& TxpSocketProtokoll == TelexPhone
		&& TimerVal(&TxpSocketLebenszeichenTimer) >= 40
	    && SocketOutBufUsed == 0
		&& SocketSendeFehlerZaehler == 0
		&& !TxpSocketAbbauGeplant
		&& TxpSocketHandle != NO_SOCKET_USED)
		{ // alle 4 Sekunden ein Lebenszeichen
		SocketOutBuf[0] = TXPC_NULL;
		SocketOutBuf[1] = 0;
		SocketOutBufUsed = 2;
		StartTimer(&TxpSocketLebenszeichenTimer);
		}

	// Timeout bei Ascii-Verbindungen verhindern
	// -----------------------------------------
	if (!TxpSocketAbbauGeplant
		&& TxpSocketProtokoll != TelexPhone
		&& TxpSocketHandle != NO_SOCKET_USED)
		{ 
		TCP_sockettable[TxpSocketHandle].Timeoutcounter = 10; // Sekunden
		}
		
	// Ist ein Neuaufbau der Verbindung erforderlich?
	// ----------------------------------------------
	if (TxpSocketMode == SocketOriginate 
		&& TxpSocketHandle == NO_SOCKET_USED 
		&& SocketOutBufUsed != 0
		&& !TxpSocketAbbauGeplant
		&& TimerVal(&TxpSocketWiederholungVerzoegerung) > 20) // 2 Sekunden verzögerung
		{
		TxpSocketHandle = Connect2IP(TxpSocketIP, TxpSocketPort); 
	 
		if (TxpSocketHandle == -1)
			{ 
			// Verbindung konnte nicht aufgebaut werden
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR("TxP: Wieder-Oeffnung des Socket VERSAGT.\r\n"));
			TxpSocketHandle = NO_SOCKET_USED;
			StartTimer(&TxpSocketWiederholungVerzoegerung);
			return;
			}

		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Wieder-Oeffnung des Socket erfolgreich.\r\n"));

		StartTimer(&TxpSocketAbbauVerzoegerung);
			
		#ifdef LEDROT_SOCKETERROR
			LED_off(ROT);
		#endif //def LEDROT_SOCKETERROR
			
		}
		
	// Daten ggf. ins Netz senden
	// --------------------------------------------------
	if (TxpSocketHandle != NO_SOCKET_USED 
		&& SocketOutBufUsed > 0 
		&& (SocketSendeFehlerZaehler == 0 || TimerVal(&TxpSocketWiederholungVerzoegerung) > 15)) 
			// Nach Sendefehlern höchstens alle 1,5 Sekunden senden.
		{
		uint16_t SendSize;
		
		SendSize = SocketOutBufUsed;
		if (SendSize > MAX_TCP_Datalenght)
			SendSize = MAX_TCP_Datalenght;
			
		int Res = PutSocketData_RPE(TxpSocketHandle, SendSize, SocketOutBuf, RAM);

		if (ProtokollLevel == 3)
			{
			ProtokollierenInt_P(PSTR("TxP: Socket Sendung: (%u)" ), SendSize);
			ProtokollierenPuffer(SocketOutBuf, SendSize);
			ProtokollierenInt_P(PSTR(" --> Res %d" ), Res);
			if (Res > 0 && Res < SocketOutBufUsed)
				ProtokollierenInt_P(PSTR(", Rest %u" ), SocketOutBufUsed - Res);
			ProtokollierenInt_P(PSTR(" SumAnz %u" ), SocketAnzahlZeichenGesendet);
			ProtokollierenInt_P(PSTR("/%02X\r\n" ), low(SocketAnzahlZeichenGesendet));
			}

		if (Res <= 0)
			{ // gar nichts gesendet.
			SocketSendeFehlerZaehler++; 
			if (SocketSendeFehlerZaehler >= 10)
				{
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: Mehrfache FEHLER beim Senden ins Netz, Socket wird voruebergehend geschlossen\r\n" ));
				CloseTCPSocket(TxpSocketHandle);
				TxpSocketHandle = NO_SOCKET_USED;
				}
			StartTimer(&TxpSocketWiederholungVerzoegerung);
			#ifdef LEDROT_SOCKETERROR
				LED_on(ROT);
			#endif //def LEDROT_SOCKETERROR
			}
			
		else if (Res < SocketOutBufUsed)
			{ // nicht alles konnte gesendet werden...
			memmove(SocketOutBuf, SocketOutBuf + Res, SocketOutBufUsed - Res);
			SocketOutBufUsed -= Res;
			SocketSendeFehlerZaehler = 0;
			#ifdef LEDROT_SOCKETERROR
				LED_on(ROT);
			#endif //def LEDROT_SOCKETERROR
			StartTimer(&TxpSocketAbbauVerzoegerung);
			}
			
		else // Puffer erfolgreich vollständig gesendet.
			{
			SocketOutBufUsed = 0;
			SocketSendeFehlerZaehler = 0;
			#ifdef LEDROT_SOCKETERROR
				LED_off(ROT);
			#endif //def LEDROT_SOCKETERROR
			StartTimer(&TxpSocketAbbauVerzoegerung);
			}
			
		} // if es gibt was zu senden

	// Abbruch wenn zu lange keine Verbindung besteht...
	// -------------------------------------------------
	if (TxpSocketMode != SocketIdle
		&& TxpSocketHandle == NO_SOCKET_USED
		&& TimerVal(&TxpSocketAbbruchTimer) >= 300) // 30 Sekunden.
		{
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: ZEITUEBERSCHREITUNG bei Wiederaufnahme der Verbindung\r\n" ));
		TxpSocketMode = SocketIdle;
		TxpSocketAbbauGeplant = false;
		TxpSocketIP = 0;
		SocketOutBufUsed = 0;
		SocketInBufUsed = 0;
		#ifdef LEDROT_SOCKETERROR
			LED_off(ROT);
		#endif //def LEDROT_SOCKETERROR
		}

	} // SocketBearbeiten()


//! Interner Statuswechsel bei Ende-befehl (Socket geschlossen oder anderes Ende-Kommando)
//! \param Force alle schwebenden Zustände (z.B. Wahlzustand) auch zum Abschluss bringen.
void InterneVerbindungBeenden(bool Force)
	{
	switch (Modus)
		{
		case ModGehendReserv:
		case ModGehendWaehlen:
		case ModDirektdruckWarteEinQuitt:
		case ModDirektdruckVerbunden: 
			if (Force)
				{
				BusSenden(BusKdoSchluss);
				ModusWechsel(ModWarteSchlussQuitt);
				}
			// sonst in diesen Zuständen ist normalerweise kein Socket offen.
			// daher auch keine Reaktion auf geschlossenen Socket.
			break; 
		
		case ModRuhe:
		case ModPufferDruckUndSchluss:
		case ModWarteSchlussQuitt:
		case ModDeaktiviert:
		case ModWarteGrundstellung:
			// in diesen Zuständen ist nicht zu tun, sondern nur abzuwarten.
			break; 
			
		case ModKommendVerbVorstufe:
		case ModKommendEinschalten:
		case ModKommendWarteEinQuitt:
			if (ProtokollLevel >= 1)
				ProtokollierenInt_P(PSTR("TxP: Wechsel nach Modus Ruhe (von %d)\r\n"), Modus);
				
			if (Modus == ModKommendWarteEinQuitt)
				BusSenden(BusKdoSchluss);
				
			ModusWechsel(ModWarteGrundstellung); 
			break;
		
		case ModKommendVerbunden:
		case ModGehendVerbunden:
			if (ProtokollLevel >= 1)
				ProtokollierenInt_P(PSTR("TxP: Wechsel nach Modus PufferDruckUndSchluss (von %d)\r\n"), Modus);
			BusSenden(BusKdoSchluss);
			ModusWechsel(ModPufferDruckUndSchluss);
			break;
		}
	} // InterneVerbindungBeenden()
	

//! Interpretiert empfangene Daten vom Socket und schiebt diese in den 
//! EmpfPuffer.
static void TxpOderAsciiEmpfangVerarbeiten()
	{
	// Daten des Socket-Empfangspuffer interpretieren
	// ----------------------------------------------
	if (SocketInBufUsed > 0)
		{ 
		uint16_t i = 0;
		uint16_t AnzAsciiEmpf = 0;
		
		while (i < SocketInBufUsed)
			{
			char c = SocketInBuf[i];
			// Achtung: In dieser Schleife entweder i weiterbringen oder break!
			// ****************************************************************
			
			// im Folgenden KEIN switch verwenden wegen break!
			if (c == '\r' || c == '\n' || (c >= ' ' && c <= '~'))
				{ // ein ASCII-Zeichen
				// ID#246 ID#344 *****************************************************
				TxpSocketProtokoll = Ascii;
				int alen = strlen(AsciiDruckPuffer);
				if (alen < AsciiDruckPufferMax-2)
					{
					AsciiDruckPuffer[alen] = c;
					AsciiDruckPuffer[alen+1] = '\0';
					i++;
					SocketAnzahlZeichenEmpfangen++;
					AnzAsciiEmpf++;
					}
				else
					break; // kann nicht mehr verarbeitet werden, also Schleife beenden.
					
				if (Modus == ModKommendVerbVorstufe)
					// ID#311 *******************************************************
					ModusWechsel(ModKommendEinschalten);

				TxpSocketAbbauGeplant = false;

				} // ASCII-Zeichen oder WR oder ZL
				
			else if (c == TXPC_NULL)
				{ // ignorieren
				i++;
				}
				
			else if (c == TXPC_DURCHWAHL)
				{ // ID#312 **************************************************************
				if (Modus == ModKommendVerbVorstufe)
					{
					Durchwahl = SocketInBuf[i+2];
					ModusWechsel(ModKommendEinschalten);
					}
				i += 2 + (uint8_t) SocketInBuf[i+1];
				TxpSocketAbbauGeplant = false;
				}
				
			else if (c == TXPC_BAUDOT_DATA)
				{ // ID#243 ID#343 ***********************************************************
				TxpSocketProtokoll = TelexPhone;
				uint8_t len = SocketInBuf[i+1];
				
				if (i + 2 + len <= SocketInBufUsed && PufferAnzahl(&SendePuffer) + len < MaxPuffer)
					{ // Baudot-Code-Block ist vollständig UND noch entsprechend Platz im Sendepuffer
					if (ProtokollLevel == 2) // Datenmengen protokollieren
						{
						ProtokollierenInt_P(PSTR("TxP: EmpfB %16d" ), PufferAnzahl(&SendePuffer));
						ProtokollierenInt_P(PSTR("%4d"), len);
						ProtokollierenInt_P(PSTR("%4d\r\n"), low(SocketAnzahlZeichenEmpfangen) + len);
						// Zahlen: unverarbeitete Daten / neue Daten / Daten insgesamt
						}
					
					i += 2; // Code und Länge überspringen
					SocketAnzahlZeichenEmpfangen += len;
					while (len > 0)
						{
						PufferSpeich(&SendePuffer, SocketInBuf[i]);
						i++;
						len--;
						} // umkopieren
						
					if (SendenBeschleunigen && PufferAnzahl(&SendePuffer) < MaxPuffer / 2)
						{
						if (ProtokollLevel >= 2)
							Protokollieren_P(PSTR("TxP: SendenBeschleunigen AUS\r\n"));
						SendenBeschleunigen = false;
						}
						
					if (Modus == ModKommendVerbVorstufe)
						// war noch gar nicht eingeschaltet, dann wird es aber Zeit...
						ModusWechsel(ModKommendEinschalten);
					}
				else // Baudot-Code-Block ist noch nicht vollständig UND noch entsprechend Platz im Sendepuffer
					{
					if (!SendenBeschleunigen && (ProtokollLevel >= 2))
						Protokollieren_P(PSTR("TxP: SendenBeschleunigen EIN\r\n"));
					SendenBeschleunigen = true;
					break; // Daten können momentan nicht verarbeitet werden.
					}
				} // else if (c == TXPC_BAUDOT_DATA)

			else if (c == TXPC_STOP || c == TXPC_ENDE)
				{
				uint8_t len = SocketInBuf[i+1];
				int alen = strlen(AsciiDruckPuffer);
				if (len + alen < AsciiDruckPufferMax-1)
					{
					strncpy(AsciiDruckPuffer + alen, SocketInBuf + i + 2, len);
					AsciiDruckPuffer[len + alen] = '\0';
					}
				i += 2 + len;

				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: Abbaubefehl von Gegenstelle\r\n"));

				InterneVerbindungBeenden(true);
				
				TxpSocketAbbauGeplant = true;
				
				} // c == TXPC_STOP oder TXPC_ENDE
				
			else if (c == TXPC_QUITT)
				{ 
				uint8_t len = SocketInBuf[i+1];
				if (Modus == ModKommendVerbVorstufe)
					{ // ID#312 **************************************************
					BusSenden(BusKdoEin);
					ModusWechsel(ModKommendEinschalten);
					}
				else if (Modus == ModGehendWaehlen)
					{ // ID#227 **************************************************
					BusSenden(BusQuittEin);
					ModusWechsel(ModGehendVerbunden);
					}
				if (len >= 1)
					SocketAnzahlZeichenQuittiert = (uint8_t) SocketInBuf[i+2];
				i += 2 + len;
				} // c == TXPC_QUITT
				
			else if (c == TXPC_VERSION)
				{ 
				uint8_t len = SocketInBuf[i+1];
				if (len >= 1)
					{
					uint8_t ProtVorschlag = SocketInBuf[i+2]; 

					if (ProtokollLevel >= 2)
						ProtokollierenInt_P(PSTR("TxP: Protokollversion-Vorschlag %u empfangen\r\n"), ProtVorschlag);

					if (ProtVorschlag == TxpSocketProtVersionVorschlag)
						{ // Vorschlag ist bestätigt...
						TxpSocketProtVersion = ProtVorschlag;
						}
					else if (ProtVorschlag > PROTVERSION_AKTUELL)
						{
						TxpSocketProtVersionVorschlag = PROTVERSION_AKTUELL;
						}
					// hier ggf. weitere Inkompatibilitäten bearbeiten...
					else
						{
						TxpSocketProtVersionVorschlag = ProtVorschlag;
						}
						
					if (TxpSocketProtVersion == 0 && SocketOutBufUsed < SocketOutBufMax - 3) // noch nichts festgelegt, also Gegenvorschlag senden.
						{
						SocketOutBuf[SocketOutBufUsed++] = TXPC_VERSION;
						SocketOutBuf[SocketOutBufUsed++] = 1;
						SocketOutBuf[SocketOutBufUsed++] = TxpSocketProtVersionVorschlag;
						
						if (ProtokollLevel >= 2)
							ProtokollierenInt_P(PSTR("TxP: Sende Protokollversion-Vorschlag %u\r\n"), TxpSocketProtVersionVorschlag);
						}
					} // len >= 1
					
				i += 2 + len;
				} // c == TXPC_VERSION
				
			else 
				{ // unbekannter Code --> ignorieren EINSCHLIEßLICH Daten
				// ID#245 ID#313 ID#346 ********************************************************
				i += 2 + (uint8_t) SocketInBuf[i+1];
				}
				
			} // while (i < SocketInBufUsed)
			
		// verarbeiteten Teil des Empfangspuffers löschen
		if (i < SocketInBufUsed)
			{
			memmove(SocketInBuf, SocketInBuf + i, SocketInBufUsed - i);
			SocketInBufUsed -= i;
			}
		else
			SocketInBufUsed = 0;
			
		if (ProtokollLevel == 2 && AnzAsciiEmpf > 0) // Datenmengen protokollieren
			{
			ProtokollierenInt_P(PSTR("TxP: EmpfA %16d" ), 
				PufferAnzahl(&SendePuffer) + strlen(AsciiDruckPuffer) + strlen(AsciiHilfPuffer) );
			ProtokollierenInt_P(PSTR("%4d"), AnzAsciiEmpf);
			ProtokollierenInt_P(PSTR("%4d\r\n"), low(SocketAnzahlZeichenEmpfangen));
			// Zahlen: unverarbeitete Daten / neue Daten / Daten insgesamt
			}
			
		} // if GetBytesInSocketData > 0
	} // TxpOderAsciiEmpfangVerarbeiten()

	
//! Wandelt Daten aus dem SendePuffer um.
//! Bearbeitet auch Statusänderungen.
static void TxpDatenVerarbeiten()
	{
	TxpOderAsciiEmpfangVerarbeiten();
		
	// vom Endgerät empfangene Daten übersetzen
	// --------------------------------------------------
	// es wird gesendet, wenn es was zu senden gibt 
	// UND      a) es viel zu senden gibt 
	//     ODER b) 0,5 sekunden nicht getippt wurde
	//     ODER c) Alles was bisher gesendet wurde schon verarbeitet ist.
	int InCount = PufferAnzahl(&EmpfPuffer);
	if (InCount > 20
	    || (InCount > 0 && ((TimerVal(&SchreibPauseTimer) >= 8) // 0,8 Sekunden Tipp-Pause
		                    || (SocketAnzahlZeichenQuittiert == low(SocketAnzahlZeichenGesendet)) // alles was gesendet wurde, ist schon verarbeitet
						    )
			)
		)
		{ // ID#244 ID#344 ***************************************************************
		// Baudot-Datenblock senden
		if (SocketOutBufUsed < SocketOutBufMax - 4 - 10) // - 10 = Reserve für wichtige Daten
			{ // es ist überhaupt Platz zum Senden
			uint16_t len = PufferAnzahl(&EmpfPuffer);
			if (len > SocketOutBufMax - 10 - 3 - SocketOutBufUsed)
				len = SocketOutBufMax - 10 - 3 - SocketOutBufUsed;
				
			if (ProtokollLevel == 2) // Datenmengen
				{
				ProtokollierenInt_P(PSTR("TxP: SendB %4d" ), (uint8_t)(low(SocketAnzahlZeichenGesendet) - SocketAnzahlZeichenQuittiert));
				ProtokollierenInt_P(PSTR("%4d"), len);
				ProtokollierenInt_P(PSTR("%4d\r\n"), low(SocketAnzahlZeichenGesendet) + len);
				}
				
			SocketOutBuf[SocketOutBufUsed++] = TXPC_BAUDOT_DATA;
			SocketOutBuf[SocketOutBufUsed++] = len;
			SocketAnzahlZeichenGesendet += len;
			while (len > 0)
				{
				SocketOutBuf[SocketOutBufUsed++] = PufferAusg(&EmpfPuffer);
				len--;
				}
			SocketSendeQuittung = true;
			} // if SocketOutBufUsed < SocketOutBufMax - 4
		} // if (...) = Im Baudot-Puffer liegende Daten senden...
		
	// ggf. Anzahl verarbeiteter Zeichen zurückmelden
	// --------------------------------------------------
	if ((Modus == ModKommendVerbunden || Modus == ModGehendVerbunden)
		&& (SocketSendeQuittung || TimerVal(&TxpSocketLebenszeichenTimer) > 35)
		&& !TxpSocketAbbauGeplant
		&& SocketSendeFehlerZaehler == 0
		&& SocketOutBufUsed < SocketOutBufMax - 4 - 10 // - 10 = Reserve für wichtige Daten
		&& TxpSocketHandle != NO_SOCKET_USED)
		{
		SocketOutBuf[SocketOutBufUsed++] = TXPC_QUITT;
		SocketOutBuf[SocketOutBufUsed++] = 1;
		SocketOutBuf[SocketOutBufUsed++] = 
			(uint8_t) (low(SocketAnzahlZeichenEmpfangen) - PufferAnzahl(&SendePuffer));
		SocketSendeQuittung = false;
		StartTimer(&TxpSocketLebenszeichenTimer);
		}

	} // TxpDatenVerarbeiten()
	

//! Wandelt Daten aus dem SendePuffer um.
//! Bearbeitet auch Statusänderungen.
static void AsciiDatenVerarbeiten()
	{
	TxpOderAsciiEmpfangVerarbeiten();
		
	// vom Endgerät empfangene Daten übersetzen
	// --------------------------------------------------
	// es wird gesendet, wenn es was zu senden gibt 
	// UND      a) es viel zu senden gibt 
	//     ODER b) 0,5 sekunden nicht getippt wurde.
	int InCount = PufferAnzahl(&EmpfPuffer);
	if (InCount > 20
	    || (InCount > 0 && TimerVal(&SchreibPauseTimer) >= 8) // 0,8 Sekunden Tipp-Pause
		)
		{ // ID#244 ID#344 ***************************************************************
		uint8_t ProtAnz = 0;
		while (!PufferLeer(&EmpfPuffer) && SocketOutBufUsed < SocketOutBufMax - 3 - 10) // - 10 = Reserve für wichtige Daten
			{
			SocketOutBuf[SocketOutBufUsed] = CodeZuZeichen(PufferAusg(&EmpfPuffer), (char*) &EmpfPuffer.BuZiMode);
			if (SocketOutBuf[SocketOutBufUsed] != '\0')
				{
				SocketOutBufUsed++;
				SocketAnzahlZeichenGesendet++;
				ProtAnz++;
				}
			}

		if (ProtokollLevel == 2 && ProtAnz > 0) // Datenmengen
			{
			ProtokollierenInt_P(PSTR("TxP: SendA %4d" ), (uint8_t)(low(SocketAnzahlZeichenGesendet) - SocketAnzahlZeichenQuittiert));
			ProtokollierenInt_P(PSTR("%4d"), ProtAnz);
			ProtokollierenInt_P(PSTR("%4d\r\n"), low(SocketAnzahlZeichenGesendet)); // wurde schon erhöht
			}
			
		StartTimer(&TxpSocketLebenszeichenTimer);
		}

	} // AsciiDatenVerarbeiten()
	

//! Schreibt ein Ende-Kommando mit Zusatztext in den Socket-Sendepuffer
static void SendeStopkommando(PGM_P s)
	{
	uint8_t len = strlen_P(s);
	if (SocketOutBufUsed + 2 + len < SocketOutBufMax - 10) // - 10 = Reserve für wichtige Daten
		{
		SocketOutBuf[SocketOutBufUsed++] = TXPC_STOP;
		SocketOutBuf[SocketOutBufUsed++] = len;
		strcpy_P(SocketOutBuf + SocketOutBufUsed, s);
		SocketOutBufUsed += len;
		}
	TxpSocketAbbauGeplant = true;
	}
	
	
//! Schiebt ein Zeichen in den Anzeigepuffer für HTML-Betrieb.
static void ZeichenInHtmlSendeText(char c)
	{
	int i = strlen(HtmlSendeText);
	if (i >= HtmlSendeTextMax - 60)
		{
		i -= 60;
		memmove(HtmlSendeText, HtmlSendeText + 60, i);
		}
	HtmlSendeText[i] = c;
	HtmlSendeText[i+1] = '\0';
	}
	
#endif // TXP_ANSCHLUSS


//! Verbindung zu einem Teilnehmer-Server herstellen.
// --------------------------------------------------
//! \param NONE
//! \return Erfolgreich

bool TeilnehmerServerSocketOeffnen()
	{
	if (TeilnehmerServerSocket != NO_SOCKET_USED)
		return true; // ist schon offen...
		
	int ServerI;
	
	for (ServerI = 0 ; ServerI < ANZ_TEILNEHMER_SERVER ; ServerI++)
		{
		long tsIP;
		
		if (TeilnehmerServerAdresse[ServerI][0] == '\0')
			continue; // leere Adresse
			
		tsIP = strtoip(TeilnehmerServerAdresse[ServerI]);	// Annahme: eine IP-Adresse angegeben
		
		if (tsIP == 0) // ist es doch eine Url?
			tsIP = DNS_ResolveName(TeilnehmerServerAdresse[ServerI]); 
			
		if (tsIP != -1)
			{
			TeilnehmerServerSocket = Connect2IP(tsIP, TXP_TLNSERV_PORT);
			if (TeilnehmerServerSocket != -1)
				{
				if (ProtokollLevelTlnServ >= 2)
					{
					Protokollieren_P(PSTR("TxP: Verbindung an Teilnehmer-Server "));
					Protokollieren(TeilnehmerServerAdresse[ServerI]); 
					Protokollieren_P(PSTR(" hergestellt\r\n"));
					}
				return true;
				}
			TeilnehmerServerSocket = NO_SOCKET_USED;
			if (ProtokollLevelTlnServ >= 1)
				{
				Protokollieren_P(PSTR("TxP: Verbindungsversuch an Teilnehmer-Server "));
				Protokollieren(TeilnehmerServerAdresse[ServerI]); 
				Protokollieren_P(PSTR(" GESCHEITERT\r\n"));
				}
			}
		else
			{
			Protokollieren_P(PSTR("TxP: Teilnehmer-Server "));
			Protokollieren(TeilnehmerServerAdresse[ServerI]); 
			Protokollieren_P(PSTR(" IP nicht bekannt\r\n"));
			}
		}
	return false;
	} // TeilnehmerServerSocketOeffnen()


#ifdef TXP_ANSCHLUSS

//! Versucht den Verbindungsaufbau zu einem vorhandenen Eintrag im eigenen Teilnehmerverzeichnis
// ---------------------------------------------------------------------------------------------
//! \retval 0 Erfolgreich
//! \retval 1 Verbindung konnte nicht hergestellt werden.
//! \retval 2 Keine gültigen Daten im Datensatz.

uint8_t Verbindungsaufbau(TTlnDaten* td)
	{
	switch (td->AdrArt)
		{
		case TxpIP:
		case TxpDynIP:
		case AsciiIP:
			if (ProtokollLevel >= 1)
				{
				Protokollieren_P(PSTR("TxP: Verbindungsaufbau zu IP "));
				ProtokollierenIPAdr(td->IPAdr);
				ProtokollierenInt_P(PSTR(" Port %u\r\n"), td->Port);
				}
			TxpSocketIP = td->IPAdr;
			TxpSocketPort = td->Port;
			// Mode wird nach erfolgreichem Öffnen gesetzt.
			break;
			
		case TxpUrl:
		case AsciiUrl:
			td->IPAdr = DNS_ResolveName(td->Adresse); 
				// IPAdr wird 'missbraucht' aber nicht gespeichert
			if (td->IPAdr != -1)
				{
				if (ProtokollLevel >= 1)
					{
					Protokollieren_P(PSTR("TxP: Verbindungsaufbau zu Url "));
					Protokollieren(td->Adresse);
					Protokollieren_P(PSTR(" = "));
					ProtokollierenIPAdr(td->IPAdr);
					ProtokollierenInt_P(PSTR(" Port %u\r\n"), td->Port);
					}									
				TxpSocketIP = td->IPAdr;
				TxpSocketPort = td->Port;
				// Mode wird nach erfolgreichem Öffnen gesetzt.
				}
			else
				{
				if (ProtokollLevel >= 1)
					{
					Protokollieren_P(PSTR("TxP: IP zu Url "));
					Protokollieren(td->Adresse);
					Protokollieren_P(PSTR(" nicht gefunden\r\n"));
					}
				return 1;
				}
			break;
			
		case eMail:
#ifdef TXP_EMAIL
			if (SMTPOeffnen(td->Adresse))
				{
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: Client-Socket SMTP erfolgreich geoeffnet -> Einschalt-Quittung an TWI\r\n" ));
				BusSenden(BusQuittEin);
				ModusWechsel(ModGehendVerbunden);
				return 0; // gut
				}
			else
				return 2; // schlecht
#else
			Protokollieren_P(PSTR("TxP: eMail nicht unterstuetzt\r\n" ));
			return 2;
#endif //ndef TXP_EMAIL		
			
		default:
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR("TxP: Teilnehmer ist GELOESCHT\r\n" ));
			return 2;
			
		}

	// und hier wird geöffet...
	TxpSocketHandle = Connect2IP(TxpSocketIP, TxpSocketPort); 
	 
	if (TxpSocketHandle == -1)
		{ // ID#223 ********************************************
		// Verbindung konnte nicht aufgebaut werden
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Socket konnte nicht erstmalig geoeffnet werden\r\n"));
		TxpSocketHandle = NO_SOCKET_USED;
		TxpSocketMode = SocketIdle;
		return 1;
		}

	SocketBufInit();
	
	TxpSocketMode = SocketOriginate;
	TxpSocketAbbauGeplant = false;
	TxpSocketProtVersion = 0;
	TxpSocketProtVersionVorschlag = PROTVERSION_AKTUELL;
	
	StartTimer(&TxpSocketAbbruchTimer);
	StartTimer(&TxpSocketAbbauVerzoegerung);
		
	if (td->AdrArt == AsciiUrl || td->AdrArt == AsciiIP)
		{ // ID#226 *********************************************
		BusSenden(BusQuittEin);
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Client-Socket Ascii erfolgreich geoeffnet -> Einschalt-Quittung an TWI\r\n" ));
		ModusWechsel(ModGehendVerbunden);
		TxpSocketProtokoll = Ascii;
		return 0;
		}
	else // TxpUrl oder TxpIP
		{ // ID#222 ********************************************
		if (ProtokollLevel >= 1)
			ProtokollierenInt_P(PSTR("TxP: Client-Socket Txp erfolgreich geoeffnet -> sende Durchwahl %u\r\n"), td->Durchwahl);

		TxpSocketProtokoll = TelexPhone;
		
		SocketOutBuf[0] = TXPC_VERSION;
		SocketOutBuf[1] = 1;
		SocketOutBuf[2] = TxpSocketProtVersionVorschlag;
		SocketOutBuf[3] = TXPC_DURCHWAHL;
		SocketOutBuf[4] = 1;
		SocketOutBuf[5] = td->Durchwahl;
		SocketOutBufUsed = 6;
		
		return 0;
		}
		
	} // Verbindungsaufbau()


//! Startet die Abfrage einer Rufnummer beim Teilnehmer-Server	
static void RufnummerBeiTlnServerAbfragen()
	{
	TlnServerAbfrageWiederholungssperre = true;
	
	if (ProtokollLevel >= 2)
		Protokollieren_P(PSTR("TxP: Abfrage bei Teilnehmer-Servern\r\n" ));

	if (TeilnehmerServerSocketOeffnen())
		{ // Verbindung hergestellt.
		// Telegramm senden
		TTlnServBuf TSB;
		
		TSB.Code = TLNSERV_ABFRAGE;
		TSB.DataLen = sizeof(TSB.TlnAbfr);
		TSB.TlnAbfr.RufNr = Wahlnummer;
		PutSocketData_RPE(TeilnehmerServerSocket, 2 + TSB.DataLen, TSB.Buf, RAM);
		}
	}
	
	
#include "UebersetzTab.h"
// als Include-Datei, da anderer Zeichensatz	

//! Bearbeitet die Ausgabe von Ascii-Text.
//----------------------------------------
//! Funktionen: Übersetzung von Umlauten, Automatischer Zeilenumbruch, 
//! Übersetzung ASCII - Baudot.

void AsciiDruckPufferVerarbeiten()
	{
	//! \todo Werda richtig verarbeiten
	if (Modus != ModDirektdruckVerbunden 
		&& Modus != ModKommendVerbunden 
		&& Modus != ModGehendVerbunden 
		&& Modus != ModPufferDruckUndSchluss)
		return; // Drucken nicht möglich.
		
	if (!PufferLeer(&SendePuffer))
		return; // Erst mal zu Ende drucken lassen.
		
	if (AsciiDruckPuffer[0] != '\0' && AsciiHilfPuffer[0] == '\0')
		{ // Daten vom puren Puffer in den Hilfspuffer umkopieren, dabei Umlaute übersetzten
		// und Zeilenumbruch durchführen
		
		// zuerst bis zum nächsten ZL oder bis zur vollen Zeile übernehmen
		uint8_t hpi = 0; // Index in HilfPuffer
		uint8_t dpi = 0; // Index in DruckPuffer
		uint8_t ZeilePos = AsciiHilfZeilenanfang; // Rechnet mit, an welcher Stelle der Zeile der Druckwagen steht.
		uint8_t UmbruchPosVorschlag = 0; // speichert, wo sinnvollerweise der Umbruch erfolgt, sofern kein Umbruch im Puffer steht.
		
		for (dpi = 0 ; AsciiDruckPuffer[dpi] != '\0' && hpi < AsciiHilfPufferMax - 2 && ZeilePos <= Druckzeilenlaenge ; dpi++)
			{
			if (ZeichenZuCode(AsciiDruckPuffer[dpi], BuMode) != 255 
				|| ZeichenZuCode(AsciiDruckPuffer[dpi], ZiMode) != 255)
				{ // Zeichen direkt druckbar.
				AsciiHilfPuffer[hpi++] = AsciiDruckPuffer[dpi];
				
				if (AsciiDruckPuffer[dpi] == '\r')
					ZeilePos = 0, UmbruchPosVorschlag = 0;
				else if (AsciiDruckPuffer[dpi] == '\n')
					{
					dpi++;
					break; // for-schleife beenden, ZeilePos nicht ändern...
					}
				else
					ZeilePos++;
					
				if (AsciiDruckPuffer[dpi] == ' ' || AsciiDruckPuffer[dpi] == '-')
					UmbruchPosVorschlag = hpi;
				}
			else
				{ // Ersatztabelle benutzten
				PGM_P p = strchr_P(UebersetzUr, AsciiDruckPuffer[dpi]);
				if (p != NULL)
					{
					AsciiHilfPuffer[hpi++] = pgm_read_byte(UebersetzN1 + (p - UebersetzUr));
					ZeilePos++;
					AsciiHilfPuffer[hpi] = pgm_read_byte(UebersetzN2 + (p - UebersetzUr));
					if (AsciiHilfPuffer[hpi] != ' ')
						hpi++, ZeilePos++;
					}
				}
			} // for dpi
			
		// ZeilePos bewerten
		if (ZeilePos > Druckzeilenlaenge)
			{
			// war schon eine geeignete Stelle für den Umbruch gefunden?
			// wenn nein jetzt eines setzen...
			if (UmbruchPosVorschlag != 0)
				{
				// Zeilenumbruch einbauen...
				if (hpi > UmbruchPosVorschlag)
					memmove(AsciiHilfPuffer + UmbruchPosVorschlag + 2, 
							AsciiHilfPuffer + UmbruchPosVorschlag, 
							hpi - UmbruchPosVorschlag);
				AsciiHilfPuffer[UmbruchPosVorschlag] = '\r';
				AsciiHilfPuffer[UmbruchPosVorschlag+1] = '\n';
				hpi += 2;
				}
			else
				{ // jetzt WR + ZL einbauen
				AsciiHilfPuffer[hpi++] = '\r';
				AsciiHilfPuffer[hpi++] = '\n';
				}
			}
			
		AsciiHilfPuffer[hpi] = '\0';

		if (ProtokollLevel == 3)
			{
			Protokollieren_P(PSTR("TxP: Ascii-Verarbeitung: " ));
			ProtokollierenPuffer(AsciiDruckPuffer, dpi);
			ProtokollierenInt_P(PSTR(" (+%u)\r\n" ), strlen(AsciiDruckPuffer) - dpi);
			Protokollieren_P(PSTR("TxP:       gewandelt in: " ));
			ProtokollierenPuffer(AsciiHilfPuffer, hpi);
			Protokollieren_P(PSTR("\r\n" ));
			}

		memmove(AsciiDruckPuffer, AsciiDruckPuffer + dpi, strlen(AsciiDruckPuffer) + 1 - dpi);
		} // if (AsciiDruckPuffer[0] != '\0' && AsciiHilfPuffer[0] == '\0')
		
	if (AsciiHilfPuffer[0] != '\0')
		{
		// zu druckenden Text umwandeln
		// ---------------------------
		uint16_t ki = 0; // Kopierindex

		while (AsciiHilfPuffer[ki] != '\0' && !PufferVoll(&SendePuffer))
			{
			if (AsciiHilfPuffer[ki] == '\r')
				AsciiHilfZeilenanfang = 0;
			else
				AsciiHilfZeilenanfang++;
				
			if (SchreibeZeichenInSendePuffer(AsciiHilfPuffer[ki]))
				{ // nur im Echo darstellen, wenn es auch gedruckt wurde.
				if (Modus == ModDirektdruckVerbunden)
					ZeichenInHtmlSendeText(AsciiHilfPuffer[ki]);
				}
				
			ki++;
			}

		if (ki > 0)
			memmove(AsciiHilfPuffer, AsciiHilfPuffer + ki, strlen(AsciiHilfPuffer) - ki + 1); 
		
		} // AsciiHilfPuffer nicht leer und SendePuffer leer

	} // AsciiDruckPufferVerarbeiten()

	
	
//! Nur für debugging.	
static uint32_t TxpThreadCount; 


//! Der TelexPhone-client an sich.
//------------------------------------------------------------------------------------------------------------
//! Diese Funktion wird zyklisch aufgerufen und hat folgende Aufgaben:
//! \par - Steuerbefehle vom TWI-Bus annehmen und interpretieren.
//! \par - Nachschauen, ob eine Verbindung auf den registrierten Port eingegangen ist. Wenn ja 
//! holt er sich die Socketnummer der Verbindung und speichert diese.
//! \par - Wenn eine Verbindung zustande gekommen ist wird diese wiederrum zyklisch nach neuen Daten abgefragt und entsprechend
//! reagiert.
//! \par Eine Übersicht der Gesamtfunktion ist in der Datei AblaeufeVerbindung.xls dargestellt.
//! Die dort enthaltenen ID sind hier mit ID#xxx referenziert.
//! \param 	NONE
//! \return	NONE

void txp_thread()
	{
	uint8_t Code;

	TxpThreadCount++;

	StartTimer(&TxpThreadCheckTimer);
	
#if defined(LEDROT_TXPTHREADBLOCK)
	LED_off(ROT); 
#endif //defined(LEDROT_TXPTHREADBLOCK)
	
	// ======================================================================
	// Auf TWI-Bus empfangene Codes auswerten
	// ======================================================================

	if (GetEmpfByte(&Code))
		{
		switch (Code)
			{
			case 1 ... BusKdoVerbAufnahme:
				if (ProtokollLevel >= 1)
					ProtokollierenInt_P(PSTR("TxP: TWI Reservierung intern / gehend von %u\r\n" ), Code);
				if (Modus == ModRuhe)
					{ // ID#101 *********************************************
					ModusWechsel(ModGehendReserv);
					BusVerbPartner = Code << 1; 
					if (!FesteHauptstelle)
						Hauptstelle = BusVerbPartner;
					}
				else
					FalschCodeEmpfangen(Code);
				break;
				
			case BusKdoEin:
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: TWI Einschaltkommando intern / gehend\r\n" ));
				if (Modus == ModGehendReserv)
					{ // ID#211 ********************************************
					BusSenden(BusKdoWahlFreigabe);
					ModusWechsel(ModGehendWaehlen);
					}
				else
					FalschCodeEmpfangen(Code);
				break;
				
			case BusQuittEin:
				if (Modus == ModKommendWarteEinQuitt)
					{ // ID#331 ********************************************
					if (ProtokollLevel >= 1)
						Protokollieren_P(PSTR("TxP: TWI Einschaltquittung intern / kommend\r\n" ));
					ModusWechsel(ModKommendVerbunden);
					SocketSendeQuittung = true;
					}

				else if (Modus == ModDirektdruckWarteEinQuitt)
					{ 
					if (ProtokollLevel >= 1)
						Protokollieren_P(PSTR("TxP: TWI Einschaltquittung nach Direktdruck\r\n" ));					
					ModusWechsel(ModDirektdruckVerbunden);
					}
				
				else
					FalschCodeEmpfangen(BusQuittEin);
					
				break;

			case BusKdoWahlFreigabe:
				//! \todo Bei Relaisbetrieb... dies ist eine Leitungsschnittstelle, die kann nicht wählen.
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: TWI Wahlaufforderung intern / kommend\r\n" ));
				FalschCodeEmpfangen(BusQuittEin);
				break;
				
			case BusKdoWahlziffer0 ... BusKdoWahlziffer9:
				if (ProtokollLevel >= 2)
					ProtokollierenInt_P(PSTR("TxP: TWI Wahlziffer %u intern / gehend\r\n" ), Code - BusKdoWahlziffer0);
				if (Modus == ModGehendWaehlen && TxpSocketMode == SocketIdle)
					{
					// ID#221 ********************************************
					Wahlnummer = 10 * Wahlnummer + (Code - BusKdoWahlziffer0);
					Wahlziffern++;
					StartTimer(&WahlPauseTimer);
					TlnServerAbfrageWiederholungssperre = false;
					
					if (TlnSuche(Wahlnummer, false, &GewaehlterTln))
						{ // ID#222 ********************************************
						bool RufnummerServerAbfrage = (Wahlziffern >= 5 && (GewaehlterTln.Flags & TlnFlag_Lokal) == 0);
						
						if (ProtokollLevel >= 1)
							ProtokollierenInt_P(PSTR("TxP: Teilnehmer %lu im eigenen Telefonbuch gefunden.\r\n"), GewaehlterTln.Nummer);
		
						if (RufnummerServerAbfrage)
							RufnummerBeiTlnServerAbfragen(); 
							
						switch (Verbindungsaufbau(&GewaehlterTln))
							{
							case 0: 
								break; // erfolgreich
								
							case 1:
								// ID#223 ********************************************
								if (!RufnummerServerAbfrage)
									// sonst besteht eine Chance auf eine Meldung des Rufnummern-Servers
									InterneVerbindungBeenden(true);
									
								break;
							
							case 2:
								InterneVerbindungBeenden(true);
								TlnServerAbfrageWiederholungssperre = true;
								break;
							}
						
						} // gewählte Nummer war vollständig
					else
						TlnDatenInit(&GewaehlterTln); 
							// da die aktuell gewählte Nummer ggf. nicht mehr zum zuletzt gefundenen Teilnehmer passt.
					} // if Modus == ModGehendWaehlen
				else
					FalschCodeEmpfangen(BusQuittEin);
				break;
				
			case BusQuittSchluss:

				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: TWI Ausschaltung quittiert\r\n" ));
					
				if (Modus != ModWarteSchlussQuitt)
					{
					if (ProtokollLevel >= 1)
						Protokollieren_P(PSTR("TxP: Schlussquittung ohne Aufforderung\r\n"));
					FalschCodeEmpfangen(Code);
					}

				// Html-Puffer löschen
				AsciiDruckPuffer[0] = '\0'; // damit es keine neue Einschaltung gibt.
				AsciiHilfPuffer[0] = '\0';
				AsciiHilfZeilenanfang = 0;
					
				ModusWechsel(ModWarteGrundstellung);
				
				break;
			
			case BusKdoSchluss:

				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: TWI Ausschaltung intern\r\n" ));
					
				// ID#212 ********************************************
				// ID#224 ********************************************
				if (Modus != ModRuhe)
					{
					BusSenden(BusQuittSchluss);

					switch (TxpSocketProtokoll)
						{
						case Ascii:
							TxpSocketAbbauGeplant = true;
							break;
							
						case TelexPhone:
							TxpSocketAbbauGeplant = true;
							if (SocketOutBufUsed < SocketOutBufMax - 2)
								{
								SocketOutBuf[SocketOutBufUsed++] = TXPC_ENDE;
								SocketOutBuf[SocketOutBufUsed++] = 0;
								}
							break;
							
						case SMTP:
							SMTPSchliessen();
							break;
							
						case POP3:
							// todo
							break;
							
						}
					}
						
				// Html-Puffer löschen
				AsciiDruckPuffer[0] = '\0'; // damit es keine neue Einschaltung gibt.
				AsciiHilfPuffer[0] = '\0';
				AsciiHilfZeilenanfang = 0;

				ModusWechsel(ModWarteGrundstellung);
					
				break;

			// ID#411 ID#104 *************************************
			// ???? \todo Ablauftabelle prüfen...
				
			default:
				FalschCodeEmpfangen(Code);
				break;
				
			} // switch Code
		} // if GetEmpfByte

	// ======================================================================
	// Prüfen, ob TWI-Kommunikation überhaupt noch läuft
	// ======================================================================

	if (Modus == ModKommendWarteEinQuitt 
		|| Modus == ModKommendVerbunden 
	    || Modus == ModGehendReserv 
		|| Modus == ModGehendWaehlen 
		|| Modus == ModGehendVerbunden 
		|| Modus == ModDirektdruckVerbunden 
		|| Modus == ModPufferDruckUndSchluss)
		{
		if (TwiWatchdogCount > 4 * TxpTimerFreq) // nach 4 Sekunden ohne TWI-Kommunikation
			{
			if (ProtokollLevel >= 1)
				Protokollieren("TxP: TWI-Timeout -> Abschaltung\r\n");
			InterneVerbindungBeenden(true);
			TxpSocketAbbauGeplant = true;
			if (SocketOutBufUsed < SocketOutBufMax - 2)
				{
				SocketOutBuf[SocketOutBufUsed++] = TXPC_ENDE;
				SocketOutBuf[SocketOutBufUsed++] = 0;
				}
			}
		}
	
	// ======================================================================
	// Socket Empfang und Sendung
	// ======================================================================

	SocketBearbeiten();
	
	// Verbindungsabbau bearbeiten
	// ---------------------------
	if (TxpSocketMode == SocketIdle)
		{
		InterneVerbindungBeenden(false);
		} // if (TxpSocketMode == SocketIdle)
	
	switch (TxpSocketProtokoll)
		{
		case Ascii:
			AsciiDatenVerarbeiten();
			break;
			
		case TelexPhone:
			TxpDatenVerarbeiten();
			break;
			
#ifdef TXP_EMAIL
		case POP3:
			Pop3DatenVerarbeiten();
			break;
			
		case SMTP:
			SMTPDatenVerarbeiten();
			break;
			
#endif //def TXP_EMAIL
		
		default:
			Protokollieren("TxP: ILLEGALES Protokoll\r\n");
			TxpSocketProtokoll = Ascii;
			break;
		}
			
		
	if (Modus == ModGehendWaehlen && !PufferLeer(&SendePuffer))
		{ // es wurden Daten empfangen, also schnellstens Endgerät anschmeißen
		// ID#227 Teil 2 *******************************************************
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Angerufener hat geantwortet -> Einschaltung intern\r\n" ));
		BusSenden(BusQuittEin);
		ModusWechsel(ModGehendVerbunden);
		}

	if (Modus == ModKommendEinschalten)
		{ 
		if (KommendInternAnwaehlen(Durchwahl)) 
			{ // ID#321 ********************************************
			if (ProtokollLevel >= 1)
				ProtokollierenInt_P(PSTR("TxP: Anwahl intern an %u erfolgt\r\n"), Durchwahl);
			BusSenden(BusKdoEin);
			ModusWechsel(ModKommendWarteEinQuitt);
			}
		else
			{ // ID#322 ********************************************
			if (ProtokollLevel >= 1)
				ProtokollierenInt_P(PSTR("TxP: Anwahl intern an %u VERSAGT\r\n"), Durchwahl);
			strcpy_P(DebugMsg, PSTR("Reservierung fuer Einschaltung konnte nicht versand werden"));

			SendeStopkommando(PSTR("occ\r\n"));
			
			ModusWechsel(ModWarteGrundstellung);
			}
		} // if Modus == ModKommendEinschalten
			
	// ==========================================================================
	// Timeouts? (auch 2 Sekunden Wahlpause...)
	// ==========================================================================

	if (Modus == ModGehendWaehlen 
		&& !TlnServerAbfrageWiederholungssperre
		&& Wahlziffern >= 5
		&& TimerVal(&WahlPauseTimer) >= 20)
		{ // 2 Sekunden Wahlpause und 5 Ziffern gewählt
		// ID#231 **************************************************************
		RufnummerBeiTlnServerAbfragen();
		}
		
	if (Modus == ModWarteSchlussQuitt && TimerVal(&BusQuittTimer) > 30)
		{ // 3 Sekunden keine Schlussquittung empfangen
		// ID#412 ****************************************************************
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Timeout beim Warten auf die Schlussquittung\r\n" ));
		ModusWechsel(ModWarteGrundstellung);
		}
		
	if (Modus == ModKommendWarteEinQuitt && TimerVal(&BusQuittTimer) > 30)
		{ // 3 Sekunden keine Einschalt-Quittung empfangen
		// ID#332 ***************************************************************
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Timeout beim Warten auf die Einschaltquittung\r\n" ));
		InterneVerbindungBeenden(true);
		SendeStopkommando(PSTR("err\r\n"));
		}
		

	// ==========================================================================
	// Tastendruck?
	// ==========================================================================

	if (Tastendruck != NichtGedr)
		{
		switch (Modus)
			{
			case ModRuhe:
				if (Tastendruck == Kurz)
					// ID#103 ********************************************************
					ModusWechsel(ModDeaktiviert);
				else
					{
					AlternativSucheBeiBesetzt = true; // damit auf jeden Fall gedruckt wird!
					strcpy_P(AsciiDruckPuffer, PSTR("interne IP: "));
					iptostr(myIP, AsciiDruckPuffer + strlen(AsciiDruckPuffer));
					strcat_P(AsciiDruckPuffer, PSTR("\r\n"));
					}
				break;
				
			case ModDeaktiviert:
				// ID#511 ********************************************************
				ModusWechsel(ModWarteGrundstellung);
				break;

			case ModPufferDruckUndSchluss:
				// ID#422 *************************************************************
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: Taste gedruckt --> Reste-Druck abgebrochen\r\n" ));
				BusSenden(BusKdoSchluss);
				ModusWechsel(ModWarteSchlussQuitt);
				AsciiDruckPuffer[0] = '\0';
				AsciiHilfPuffer[0] = '\0';
				AsciiHilfZeilenanfang = 0;
				PufferInit(&SendePuffer);
				break;
			
			default:
				break;
								
			} // switch Modus
			
		Tastendruck = NichtGedr;
		}
		
	// ==========================================================================
	// Ascii-Text im Puffer z.B. durch Html-Eingabe?
	// ==========================================================================

	if (Modus == ModRuhe && AsciiDruckPuffer[0] != '\0')
		{
		if (KommendInternAnwaehlen(0)) // keine Durchwahl
			{ 
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR("TxP: Direktdruck -> Einschaltung intern\r\n" ));
			BusSenden(BusKdoEin);
			ModusWechsel(ModDirektdruckWarteEinQuitt);
			}
		else
			{ 
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR("TxP: Direktdruck -> Einschaltung intern VERSAGT\r\n" ));
			strcpy_P(DebugMsg, PSTR("Reservierung für Einschaltung konnte nicht versand werden"));
			AsciiDruckPuffer[0] = '\0'; // damit es keine neue Einschaltung gibt.
			AsciiHilfPuffer[0] = '\0';
			AsciiHilfZeilenanfang = 0;
			ModusWechsel(ModWarteGrundstellung);
			}
		} // if ModRuhe && Text per HTML empfangen
		
	AsciiDruckPufferVerarbeiten();
	
	if (Modus == ModDirektdruckVerbunden)
		{ // am Fernschreiber eigegebene Zeichen nach Ascii umwandeln 
		//! \todo eigentlich nur, wenn es tatsächlich über eine HTML-Seite lief...
		while (!PufferLeer(&EmpfPuffer))
			{
			ZeichenInHtmlSendeText(CodeZuZeichen(PufferAusg(&EmpfPuffer), (char*) &EmpfPuffer.BuZiMode));
			StartTimer(&HtmlTexteingabeTimer);
			}

		if (AsciiDruckPuffer[0] == '\0'
			&& AsciiHilfPuffer[0] == '\0'
			&& PufferLeer(&SendePuffer)
			&& PufferLeer(&EmpfPuffer)
			&& (TxpSocketHandle == NO_SOCKET_USED || SocketInBufUsed == 0)
			&& (TimerVal(&HtmlDruckspiegelAnzeigeTimer) > 300 // 30 Sekunden keine Anzeige-Abfrage
				|| TimerVal(&HtmlTexteingabeTimer) > 1800)) // 3 Minuten nichts eingegeben
			{
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR("TxP: Direktdruck-Ruhe --> Ausschaltung intern\r\n" ));
			InterneVerbindungBeenden(true);
			//! \todo ResteDruck wartet auch auf Leerung des Puffers...
			} // Abschaltung nach 30 Sekunden / 180 Sekunden.

		} // if Modus == ModDirektdruckVerbunden

	// ==========================================================================
	// Abschaltung nach Reste-Druck?
	// ==========================================================================

	if (Modus == ModPufferDruckUndSchluss 
		&& AsciiDruckPuffer[0] == '\0' 
		&& AsciiHilfPuffer[0] == '\0'
		&& PufferLeer(&SendePuffer))
		{ // ID#421 *************************************************************
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Reste gedruckt --> Ausschaltung intern\r\n" ));
		BusSenden(BusKdoSchluss);
		ModusWechsel(ModWarteSchlussQuitt);
		}

	// ==========================================================================
	// Grundstellung nach eigenem Verbindungsabbau?
	// ==========================================================================

	if (Modus == ModWarteGrundstellung 
		&& TxpSocketHandle == NO_SOCKET_USED
		&& TxpSocketMode == SocketIdle)
		{
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Grundstellung erreicht (Socket geschlossen, TWI geschlossen)\r\n" ));
		ModusWechsel(ModRuhe);
		#ifdef LEDROT_SOCKETERROR
			LED_off(ROT);
		#endif //def LEDROT_SOCKETERROR
		}
		
	// ======================================================================
	// Dynamische IP-Aktualisierung starten
	// ======================================================================
	
	if (DynIPAktiv)
		{
		// Aktialisierung starten?
		if ((Modus == ModRuhe || Modus == ModDeaktiviert)
			&& TimerVal(&DynIPAktualisierungTimer) >= DynIPAktualisierungEndzeit
			&& TeilnehmerServerSocket == NO_SOCKET_USED)
			{
			if (TeilnehmerServerSocketOeffnen())
				{ // Verbindung hergestellt.
				// Telegramm senden
				TTlnServBuf TSB;
				
				TSB.Code = TLNSERV_SELBSTAKT;
				TSB.DataLen = sizeof(TSB.SelbstAkt);
				TSB.SelbstAkt.RufNr = NetzRufnummer;
				TSB.SelbstAkt.Pin = Geheimzahl;
				TSB.SelbstAkt.Port = NetzPort;
				PutSocketData_RPE(TeilnehmerServerSocket, 2 + TSB.DataLen, TSB.Buf, RAM);
				}
			else
				{ // keine Verbindung hergestellt
				StartTimer(&DynIPAktualisierungTimer);
				DynIPAktualisierungEndzeit = 15 * 600 - (KurzTimerCnt & 0x3FF);
					// in 15 Minuten minus Zufall wieder.
				
				}
			}
		}
		
	// ======================================================================
	// Antworten vom Teilnehmer-Server auswerten
	// ======================================================================
	
	if (TeilnehmerServerSocket != NO_SOCKET_USED)
		{
		// Datenempfang vom Teilnehmer-Server
		int InCount = GetBytesInSocketData(TeilnehmerServerSocket);
		if (InCount > 0) 
			{
			static TTlnServBuf TSB;
			
			int Res = GetSocketData(TeilnehmerServerSocket, InCount, TSB.Buf);
			
			if (ProtokollLevelTlnServ >= 3) // Daten explizit
				{
				ProtokollierenInt_P(PSTR("TxP: Teilnehmer-Server Empfang: (%d/" ), InCount);
				ProtokollierenInt_P(PSTR("%d)"), Res);
				ProtokollierenPuffer(TSB.Buf, Res);
				Protokollieren_P(PSTR("\r\n"));
				}		
			
			// Daten des Socket-Empfangspuffer interpretieren
			// ----------------------------------------------
			// es wird immer nur ein Telegramm gesendet und empfangen
			switch (TSB.Code)
				{
				case TLNSERV_AUSKUNFT_NICHTVERG:
					if (ProtokollLevel >= 2)
						Protokollieren_P(PSTR("TxP: Teilnehmer-Server meldet 'nicht gefunden'\r\n" ));
					break;
					
				case TLNSERV_AUSKUNFT_VERSION1:
					if (ProtokollLevel >= 2)
						{
						Protokollieren_P(PSTR("TxP: Teilnehmer-Server meldet IP gefunden: " ));
						ProtokollierenIPAdr(TSB.TlnAuskunft.IPAdr);
						Protokollieren_P(PSTR("\r\n"));
						}
						
					if (GewaehlterTln.AdrArt == Geloescht)
						; // weitermachen
					else if (GewaehlterTln.Nummer != TSB.TlnAuskunft.Nummer)
						{ // vorhandener Eintrag weicht von 'aktuellem' ab --> Abbruch
						Protokollieren_P(PSTR("TxP: Teilnehmer-Server meldet andere Nummer als angefragt\r\n"));
						break;
						}
					else if ((GewaehlterTln.Flags & TlnFlag_Lokal) != 0)
						{ // Privater Eintrag --> nicht ändern
						Protokollieren_P(PSTR("TxP: im lokalen Telefonbuch als 'Privat' gekennzeichnet\r\n"));
						break;
						}

					// gelieferte Daten _teilweise_ in das eigene Telefonbuch kopieren...
					if (TSB.TlnAuskunft.Datum > GewaehlterTln.Datum || GewaehlterTln.AdrArt == Geloescht)
						{
						GewaehlterTln.Nummer = TSB.TlnAuskunft.Nummer;
						if (GewaehlterTln.Name[0] == '\0') // nur leere Namen überschreiben
							strncpy(GewaehlterTln.Name, TSB.TlnAuskunft.Name, sizeof(GewaehlterTln.Name));
						GewaehlterTln.Flags = TSB.TlnAuskunft.Flags;
						if (GewaehlterTln.AdrArt != TxpDynIP || TSB.TlnAuskunft.AdrArt != TxpIP)
							// Nicht DynIP durch IP überschreiben
							GewaehlterTln.AdrArt = TSB.TlnAuskunft.AdrArt; 
						strncpy(GewaehlterTln.Adresse, TSB.TlnAuskunft.Adresse, sizeof(GewaehlterTln.Adresse));
						GewaehlterTln.IPAdr = TSB.TlnAuskunft.IPAdr;
						GewaehlterTln.Port = TSB.TlnAuskunft.Port;
						GewaehlterTln.Durchwahl = TSB.TlnAuskunft.Durchwahl;
						GewaehlterTln.Datum = TSB.TlnAuskunft.Datum;

						if (!TlnHinzufuegen(&GewaehlterTln))
							ProtokollierenInt_P(PSTR("TxP: Datensatz vom Teilnehmer-Server mit Nr %ld konnte nicht gespeichert werden\r\n"), GewaehlterTln.Nummer);
						} // Aktualisieren ist sinnvoll
						
					if (Modus == ModGehendWaehlen && TxpSocketMode == SocketIdle && TSB.TlnAuskunft.Nummer == Wahlnummer)
						{ // erhaltenen Datensatz auch zum Verbindungsaufbau nutzen.
						switch (Verbindungsaufbau(&GewaehlterTln))
							{
							case 0: 
								break; // erfolgreich
								
							case 1: // Socket öffnen nicht erfolgreich
							case 2: // Ungültige Daten
								InterneVerbindungBeenden(true);
								break;
							}
						}
						
					break; // case TLNSERV_AUSKUNFT_VERSION1
					
				case TLNSERV_IPRUECKMELD:
					if (TSB.IpRueckm.EmpfIP == NetzEigeneIP)
						{ // keine Änderung
						if (ProtokollLevelTlnServ >= 2)
							Protokollieren_P(PSTR("TxP: Dynamische IP-Aktualisierung: bestehende IP gilt weiter\r\n" ));
						}
					else
						{
						NetzEigeneIP = TSB.IpRueckm.EmpfIP;
						if (ProtokollLevelTlnServ >= 1)
							{
							Protokollieren_P(PSTR("TxP: Dynamische IP-Aktualisierung: neue IP "));
							ProtokollierenIPAdr(NetzEigeneIP);
							Protokollieren_P(PSTR("\r\n"));
							}
						}
					StartTimer(&DynIPAktualisierungTimer);
					DynIPAktualisierungEndzeit = 15 * 600; // in 15 Minuten wieder
					break;

				case TLNSERV_FEHLER:
					Protokollieren_P(PSTR("TxP: Fehlermeldung des Teilnehmer-Servers: "));
					Protokollieren(TSB.PureData);
					Protokollieren_P(PSTR("\r\n"));
					StartTimer(&DynIPAktualisierungTimer);
					DynIPAktualisierungEndzeit = 15 * 600 - (KurzTimerCnt & 0x3FF);
						// in 15 Minuten minus Zufall wieder.
					break;
				
				default:
					Protokollieren_P(PSTR("TxP: unerwartete Antwort des Teilnehmer-Servers\r\n" ));
					StartTimer(&DynIPAktualisierungTimer);
					DynIPAktualisierungEndzeit = 15 * 600 - (KurzTimerCnt & 0x3FF);
						// in 15 Minuten minus Zufall wieder.
					break;
				
				} // switch (TSB.Code)
				
			// eine Antwort genügt...
			CloseTCPSocket(TeilnehmerServerSocket);
			TeilnehmerServerSocket = NO_SOCKET_USED;
			}
		
		// Schließanforderung vom Teilnehmer-Server
		if (CheckSocketState(TeilnehmerServerSocket) == SOCKET_NOT_USE)
			{
			if (ProtokollLevelTlnServ >= 1)
				Protokollieren_P(PSTR("TxP: Socket zum Teilnehmer-Server wurde von Gegenstelle geschlossen\r\n" ));
			CloseTCPSocket(TeilnehmerServerSocket);
			TeilnehmerServerSocket = NO_SOCKET_USED;
			}
		
		// Timeout? kommt von selbst nach 30 Sekunden...
		
		}
		
	// ==========================================================================
	// Ab und zu mal den Protokollinhalt speichern
	// ==========================================================================

	ProtokollSpeichern(false);
	
#ifdef TXP_EMAIL

	// ==========================================================================
	// Ab und zu mal prüfen, ob es neue Mails gibt.
	// ==========================================================================
	
	POP3Einleiten();
	
#endif //def TXP_EMAIL
	
	// ==========================================================================
	// Sicherheitslücke durch Überlauf des Konfig-Freigabe-Timers schließen.
	// ==========================================================================
	
	if (KonfigFreigabeErteilt && TimerVal(&KonfigFreigabeTimer) > 5 * 600)
		KonfigFreigabeErteilt = false;
	
	// HACK Status-Signale Seriell
	bset_RTS(get_CTS());
	
	} // txp_thread
	

// ================================================================================	
			

//! Liest den String s aus in die Durchwahl-Tabelle.
//--------------------------------------------------
//! \return Anzahl der korrekt gelesenen Einträge
static uint8_t DurchwahlTabelleDekodieren(char *s)
	{
	uint8_t i = 0; // Index in der Tabelle
	uint8_t AnzSt = 0; // Anzahl Stellen
	uint8_t WahlNr = 0; // Bisherige Nummer
	
	while (*s != '\0' && i < 9)
		{
		switch (*s)
			{
			case '0' ... '9':
				if (AnzSt == 0)
					WahlNr = *s - '0';
				else
					WahlNr = (10 * WahlNr) + (*s - '0');
				if (AnzSt < 2)
					AnzSt++;
				DurchwahlTabelle[i] = WahlZuAdresse(WahlNr, AnzSt);
				break;
			
			case ',':
			case '/':
			case '.':
				i++;
				AnzSt = 0;
				WahlNr = 0;
				break;
			
			case ' ':
				if (AnzSt > 0)
					i++;
				AnzSt = 0;
				WahlNr = 0;
				break;

			default:
				return i;
			
			} // switch (*s)
		s++;
		} // while *s != 0 && i < 9
	return i;
	} // DurchwahlTabelleDekodieren()
	
#endif // TXP_ANSCHLUSS


//! Kann am Anfang jeder cgi-Funktion aufgerufen werden, um Zugang zu der Funktion erst nach Kennwort-Eingabe zu erlauben.
// -----------------------------------------------------------------------------------------------------------------------
//! \param 	pStruct	Struktur auf den HTTP_Request
//! \retval true, wenn Zugriff erfolgen darf.

static const PROGMEM char Kennwort_P[] = "kennw";

uint8_t KonfigFreigabe(void *pStruct)
	{
	if (KonfigPasswort[0] == '\0')
		return true; // ohne Kennwort keine Sperre
	
	if (KonfigFreigabeErteilt && TimerVal(&KonfigFreigabeTimer) < 5 * 600)
		{ // 5 Minuten lang ist der Zugang erlaubt
		StartTimer(&KonfigFreigabeTimer);
		return true;
		}
		
	//! \todo Sperre nach Fehlversuchen
	
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;

	if (http_request->argc == 0 || PharseCheckName_P(http_request, Kennwort_P) == 0)
		{ // Ausgabe der Passwort - Eingabeseite
		KonfigFreigabeErteilt = false;
		cgi_PrintHttpheaderStart();
		CgiFormStartTabbed_P(PSTR("txpcfg-intern.cgi"));
		CgiFormInputFieldText_P(PSTR("Seite gesperrt! Kennwort :"), Kennwort_P, KonfigPasswortLen, NULL);
		CgiFormFinish_P(PSTR("Freigeben"));
		cgi_PrintHttpheaderEnd();
		return false;
		}
	else
		{ // Test des eingegebenen Kennworts
		char *EingabeText = http_request->argvalue[PharseGetValue_P(http_request, Kennwort_P)];
		if (strcmp(EingabeText, KonfigPasswort) == 0)
			{ // korrekt eingegebenen
			KonfigFreigabeErteilt = true;
			StartTimer(&KonfigFreigabeTimer);
			http_request->argc = 0; // damit die eigentliche Seite nicht durch die Kennwort-Eingabe verwirrt ist!			
			return true;
			}
		else
			{
			cgi_PrintHttpheaderStart();
			printf_P(PSTR("Kennwort falsch!"));
			cgi_PrintHttpheaderEnd();
			KonfigFreigabeErteilt = false;
			return false;
			}
		}
	}
	
	
/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface für Ausgabe von Debug-Infos des TelexPhone
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void txp_cgi_debug( void * pStruct )
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;

	cgi_PrintHttpheaderStart();

	printf_P(PSTR("DebugMsg: %s<br>"), DebugMsg);
	DebugMsg[0] = '\0';

	extern char Dateiname[]; // aus Protokoll.c
	printf_P(PSTR("Protokolldatei: %s<br>"), Dateiname);
	extern char Puffer[]; // aus Protokoll.c
	printf_P(PSTR("Protokollpuffer: %s<br>"), Puffer);

#define PRINTVAL(Var) printf_P(PSTR("<br>" #Var " = %u"), Var)
#define PRINTVALHEX(Var) printf_P(PSTR("<br>" #Var " = %X"), Var)

	#ifdef TXP_ANSCHLUSS
	
	PRINTVAL(Modus);
	PRINTVAL(Status); // bezüglich TxP-Funktionalität (ist auf TWI-Bus sichtbar)
	PRINTVAL(Wahlnummer);
	PRINTVAL(Wahlziffern);

	PRINTVAL(BusEmpfMark);
	PRINTVAL(SerUmTickZaehlerEmpf);
	PRINTVAL(SerUmEmpfBitNr); 
	PRINTVAL(SerUmEmpfMarkZaehl);
	PRINTVAL(SerUmEmpfDaten);
	PRINTVAL(SerUmEmpfFehler);
	PRINTVAL(PufferAnzahl(&EmpfPuffer));
	for (uint16_t i = EmpfPuffer.AusgP ; i != EmpfPuffer.SpeichP ; i++)
		{
		if (i >= MaxPuffer) 
			i = 0;
		printf_P(PSTR(" %02X"), EmpfPuffer.Puffer[i]);
		}

	PRINTVAL(SendeMark);
	PRINTVAL(SerUmTickZaehlerSend);
	PRINTVAL(SerUmSendBitNr);
	PRINTVAL(SerUmSendDaten);
	PRINTVAL(PufferAnzahl(&SendePuffer));
	for (uint16_t i = SendePuffer.AusgP ; i != SendePuffer.SpeichP ; i++)
		{
		if (i >= MaxPuffer) 
			i = 0;
		printf_P(PSTR(" %02X"), SendePuffer.Puffer[i]);
		}
	
	PRINTVAL(TxpSocketMode);
	PRINTVAL(TxpSocketHandle);
	PRINTVALHEX(TxpSocketIP);
	PRINTVAL(TxpSocketAbbauGeplant);
	PRINTVAL(TimerVal(&TxpSocketAbbruchTimer));
	PRINTVAL(SocketInBufUsed);
	PRINTVAL(SocketOutBufUsed);
	PRINTVAL(TxpSocketProtokoll);
	PRINTVAL(ProtokollPhase);
	
	PRINTVAL(SocketAnzahlZeichenGesendet);
	PRINTVAL(SocketAnzahlZeichenQuittiert);
	PRINTVAL(SocketAnzahlZeichenEmpfangen);
	
	PRINTVAL(TimerVal(&WahlPauseTimer));
	PRINTVAL(TimerVal(&SchreibPauseTimer));
	PRINTVAL(TimerVal(&BusQuittTimer));
	PRINTVAL(TwiLebenszeichenZaehler); 
	PRINTVAL(TimerVal(&TxpSocketLebenszeichenTimer));
	PRINTVAL(TimerVal(&TxpThreadCheckTimer));

	PRINTVAL(TimerVal(&DynIPAktualisierungTimer));
	PRINTVAL(DynIPAktualisierungEndzeit);

	PRINTVAL(FalscherCode); FalscherCode = 0;
	PRINTVAL(TwiIsrCount); TwiIsrCount = 0;
	PRINTVAL(TxpThreadCount); //TxpThreadCount = 0;

	PRINTVAL(Timer0CallbackCount); //Timer0CallbackCount = 0;
	PRINTVAL(Timer0Cnt_Min); Timer0Cnt_Min = 255;
	PRINTVAL(Timer0Cnt_Max); Timer0Cnt_Max = 0;
	PRINTVAL(Timer0Callback_Max); Timer0Callback_Max = 0;

	printf_P(PSTR("<br>HtmlSendeText: ["));
	printf(HtmlSendeText);
	printf_P(PSTR("]<br>AsciiDruckPuffer: ["));
	printf(AsciiDruckPuffer);
	
	#endif // TXP_ANSCHLUSS
	
	printf_P(PSTR("]<br>Ethernet: %ld Bytes in %ld Packeten LockErrors %ld\r\n") , ByteCounter, PacketCounter, eth_state_error );

	cgi_PrintHttpheaderEnd();

	ProtokollSpeichern(true);

	}
	

#ifdef TXP_ANSCHLUSS
	
/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface für das Ausgabefenster der Fernschreiber-Simulation
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void txp_cgi_msg_Out( void * pStruct )
	{
	//struct HTTP_REQUEST * http_request;
	//http_request = (struct HTTP_REQUEST *) pStruct;

	printf_P( PSTR(	"<HTML>"
					"<HEAD>"
					"<meta http-equiv=\"expires\" content=\"1\">"
					"<meta http-equiv=\"pragma\" content=\"no-cache\">"
					"<meta http-equiv=\"refresh\" content=\"10; URL=txp-msg-out.cgi\">"
					"</HEAD>"
					"<BODY>" ));
					
	if (Modus == ModDirektdruckVerbunden)
		{
		printf_P(PSTR("Druckspiegel:<br><pre>%s&lt;&lt;&lt;%s</pre>"), HtmlSendeText, AsciiDruckPuffer);

		if (ProtokollLevel >= 3)
			{
			Protokollieren_P(PSTR("TxP: Direktdruck Abruf Druckspiegel:"));
			char *p = HtmlSendeText + strlen(HtmlSendeText) - 20;
			if (p < HtmlSendeText) 
				p = HtmlSendeText;
			Protokollieren(p);
			ProtokollierenInt_P(PSTR(" (%u)\r\n"), strlen(HtmlSendeText));
			}

		}

	else if (Modus == ModRuhe)
		{
		printf_P(PSTR("Texteingabe startet Fernschreiber"));
		HtmlSendeText[0] = '\0';
		if (ProtokollLevel >= 3)
			Protokollieren_P(PSTR("TxP: Direktdruck Abruf Druckspiegel (aus)\r\n"));
		}
		
	else
		{
		printf_P(PSTR("Interface ist belegt, bitte warten."));
		if (ProtokollLevel >= 3)
			Protokollieren_P(PSTR("TxP: Direktdruck Abruf Druckspiegel (belegt)\r\n"));
		}
	
	cgi_PrintHttpheaderEnd();
	
	StartTimer(&HtmlDruckspiegelAnzeigeTimer);
	
	}


/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface für das Eingabefenster der Fernschreiber-Simulation
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void txp_cgi_msg_In( void * pStruct )
	{
	static const PROGMEM char Eingabe_P[] = "Eingabe";
	
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;

	if ((http_request->argc != 0) 
	    && PharseCheckName_P(http_request, Eingabe_P)
		&& (Modus == ModRuhe || Modus == ModDirektdruckWarteEinQuitt || Modus == ModDirektdruckVerbunden))
		{
		char *EingabeText = http_request->argvalue[PharseGetValue_P(http_request, Eingabe_P)];
		strncat(AsciiDruckPuffer, EingabeText, AsciiDruckPufferMax - strlen(AsciiDruckPuffer) - 3);
		AsciiDruckPuffer[AsciiDruckPufferMax-3] = '\0';
		strcat_P(AsciiDruckPuffer, PSTR("\r\n"));
		if (ProtokollLevel >= 2)
			{
			Protokollieren_P(PSTR("TxP: Direktdruck Eingabe: "));
			Protokollieren(EingabeText); 
			Protokollieren_P(PSTR("\r\n"));
			}
		StartTimer(&HtmlTexteingabeTimer);
		}

	cgi_PrintHttpheaderStart();
	printf_P(PSTR(
		"<form action=\"txp-msg-in.cgi\">"
		"Eingabe: <input name=\"Eingabe\" type=\"text\" size=\"65\" value=\"\" maxlength=\"65\">"
		"<input type=\"submit\" value=\" Absenden \">"
		"<a href=\"txp-msg-out.cgi\" target=\"MsgOut\">Aktualisieren</a>"
		"</form>"));
	cgi_PrintHttpheaderEnd();
	}
	
	
//! Bildet den zur TWI-Adresse passenden Wähltext.
//------------------------------------------------
//! Beispiele: 45 -> "45", 05 -> "05", 103 -> "3"
//! Siehe auch AdresseZuWahl()
//! \param[in] Adr TWI-Adresse von 2 bis 220 (2 * 1 bis 2 * 110)
//! \param[out] Buf String für Wähltext, mindestens 4 Zeichen Länge.

void AdresseZuWahlStr(uint8_t Adr, char* Buf)
	{
	uint8_t Wahl, AnzZif;
	
	Wahl = AdresseZuWahl(Adr, &AnzZif);
	
	if (AnzZif == 0)
		Buf[0] = '\0'; // ungültige Nummer
	else
		{
		itoa(Wahl, Buf, 10); // 10 ist die Basis für Dezimal!
		if (AnzZif > 1 && Buf[1] == '\0')
			{
			Buf[2] = '\0';
			Buf[1] = Buf[0];
			Buf[0] = '0';
			}
		}
	}

#endif // TXP_ANSCHLUSS

	
const PROGMEM char KonfigPasswort_P[] = "CFGPASS";
const PROGMEM char ProtokollLevel_P[] = "PROTLEVEL";
const PROGMEM char ProtokollLevelTlnServ_P[] = "PROTLEVELTLNSRV";


#ifdef TXP_ANSCHLUSS

const PROGMEM char Hauptstelle_P[] = "HAUPTSTELLE";
const PROGMEM char EigeneNummer_P[] = "EIGENENUMMER";
const PROGMEM char FesteHst_P[] = "FESTEHPST";
const PROGMEM char AlternBeiBes_P[] = "ALTERNBEIBES";
const PROGMEM char DurchwahlTabelle_P[] = "DURCHWAHLTAB";

#endif // TXP_ANSCHLUSS

	
/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface zum Ändern der Einstellungen des TelexPhone-Interface bezüglich der Einbindung
 * in das lokale TxP-System
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/
 
void txp_cgi_config_intern(void *pStruct)
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	char Buf[35];

	if (!KonfigFreigabe(pStruct))
		return;
	
	cgi_PrintHttpheaderStart();

	if ( http_request->argc == 0 )
		{
		CgiFormStartTabbed_P(PSTR("txpcfg-intern.cgi"));

		#ifdef TXP_ANSCHLUSS
		
		AdresseZuWahlStr(BusEigenAdresse, Buf);
		CgiFormInputFieldText_P(PSTR("Netz-Vorwahl f&uuml;r gehende Verbindungen:"), EigeneNummer_P, 2, Buf);

		CgiFormCheckbox_P(PSTR("feste Hauptstelle f&uuml;r kommende Verbindungen:"), FesteHst_P, FesteHauptstelle);

		AdresseZuWahlStr(Hauptstelle, Buf);
		CgiFormInputFieldText_P(PSTR("intere Duchwahl der Hauptstelle f&uuml;r kommende Verbindungen:"), Hauptstelle_P, 2, Buf);

		CgiFormCheckbox_P(PSTR("Alternativ-Suche bei besetzt:"), AlternBeiBes_P, AlternativSucheBeiBesetzt);
						
		readConfig_P(DurchwahlTabelle_P, Buf);

		CgiFormInputFieldText_P(PSTR("Durchwahlen:<br>(mit Komma trennen)"), DurchwahlTabelle_P, 30, Buf);
		
		#endif // TXP_ANSCHLUSS

		CgiFormInputFieldULong_P(PSTR("Protokoll-Level:"), ProtokollLevel_P, 2, ProtokollLevel);

		CgiFormInputFieldULong_P(PSTR("Protokoll-Level f&uuml;r Teiln-Server:"), ProtokollLevelTlnServ_P, 2, ProtokollLevelTlnServ);

		CgiFormInputFieldText_P(PSTR("Passwort f&uuml;r Kofigurationsseiten:"), KonfigPasswort_P, KonfigPasswortLen, KonfigPasswort);
		
		CgiFormFinish_P(PSTR("Einstellung &Uuml;bernehmen"));
		}
	else // argc > 0
		{
		uint8_t Neu;

		printf_P(PSTR("neue Einstellungen: <a href=\"txpcfg-intern.cgi\">weiter</a>"));

		#ifdef TXP_ANSCHLUSS
		
		// Eigene Nummer
		// -------------
		if (PharseCheckName_P(http_request, EigeneNummer_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, EigeneNummer_P)], 2);
			Buf[2] = '\0';
			Neu = WahlZuAdresse(atoi(Buf), strlen(Buf));
			if (Neu == BusEigenAdresse)
				printf_P(PSTR("<br>Netz-Vorwahl unver&auml;ndert: %s"), Buf);
			else if (Modus == ModRuhe && BusEigenAdressePruefenUndSetzen(Neu))
				{
				AdresseZuWahlStr(Neu, Buf);
				changeConfig_P(EigeneNummer_P, Buf);
				printf_P(PSTR("<br>Netz-Vorwahl: %s"), Buf);
				}
			else
				printf_P(PSTR("<br>Netz-Vorwahl konnte nicht ge&auml;ndert werden"));
			}
		
		// Nummer Hauptstelle
		// ------------------
		if (PharseCheckName_P(http_request, Hauptstelle_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, Hauptstelle_P)], 2);
			Buf[2] = '\0';
			Neu = WahlZuAdresse(atoi(Buf), strlen(Buf));
			if (Neu == Hauptstelle)
				printf_P(PSTR("<br>Hauptstelle unver&auml;ndert: %s"), Buf);
			else
				{
				AdresseZuWahlStr(Neu, Buf);
				changeConfig_P(Hauptstelle_P, Buf);
				printf_P(PSTR("<br>Hauptstelle: %s"), Buf);
				Hauptstelle = Neu;
				}
			}
		
		// Feste Hauptstelle
		// ------------------
		if (PharseCheckName_P(http_request, FesteHst_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, FesteHst_P)], 2);
			Buf[2] = '\0';
			Neu = atoi(Buf) != 0; 
			}
		else
			{
			Neu = false;
			Buf[0] = '0', Buf[1] = '\0';
			}
		if (Neu == FesteHauptstelle)
			printf_P(PSTR("<br>FesteHauptstelle unver&auml;ndert: %u"), Neu);
		else
			{
			changeConfig_P(FesteHst_P, Buf);
			printf_P(PSTR("<br>Feste Hauptstelle: %s"), Buf);
			FesteHauptstelle = Neu;
			}
			
		// AlternativSucheBeiBesetzt
		// -------------------------
		if (PharseCheckName_P(http_request, AlternBeiBes_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, AlternBeiBes_P)], 2);
			Buf[2] = '\0';
			Neu = atoi(Buf) != 0; 
			}
		else
			{
			Neu = false;
			Buf[0] = '0', Buf[1] = '\0';
			}
		if (Neu == AlternativSucheBeiBesetzt)
			printf_P(PSTR("<br>AlternativSucheBeiBesetzt unver&auml;ndert: %u"), Neu);
		else
			{
			changeConfig_P(AlternBeiBes_P, Buf);
			printf_P(PSTR("<br>Alternativ-Suche bei Besetzt: %s"), Buf);
			AlternativSucheBeiBesetzt = Neu;
			}
			
		// DurchwahlTabelle
		// ----------------
		// hier ist Neu nur ein Flag
		if (PharseCheckName_P(http_request, DurchwahlTabelle_P))
			{
			if (readConfig_P(DurchwahlTabelle_P, Buf) == 1)
				Neu = strcmp(Buf, http_request->argvalue[PharseGetValue_P(http_request, DurchwahlTabelle_P)]) != 0;
			else
				Neu = true;
				
			if (Neu)
				{
				strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, DurchwahlTabelle_P)], 33);
				Buf[33] = '\0';
				DurchwahlTabelleDekodieren(Buf);
				AdresseZuWahlStr(DurchwahlTabelle[0], Buf);
				for (uint8_t i = 1 ; i < 9 ; i++)
					{
					uint8_t len = strlen(Buf);
					Buf[len] = ','; // Komma angefügt
					AdresseZuWahlStr(DurchwahlTabelle[i], Buf + len + 1);
					}
				changeConfig_P(DurchwahlTabelle_P, Buf);
				printf_P(PSTR("<br>Durchwahlen: %s"), Buf);
				}
			else
				printf_P(PSTR("<br>Durchwahlen unver&auml;ndert: %s"), Buf);
			}
		
		#endif // TXP_ANSCHLUSS
		
		ProtokollLevel = CgiCheckULong_P(http_request, 
			PSTR("Protokoll-Level"), ProtokollLevel_P, ProtokollLevel);

		ProtokollLevelTlnServ = CgiCheckULong_P(http_request, 
			PSTR("Protokoll-Level Rufnr-Server"), ProtokollLevelTlnServ_P, ProtokollLevelTlnServ);

		// KonfigPasswort
		// --------------
		if (PharseCheckName_P(http_request, KonfigPasswort_P))
			{
			strncpy(KonfigPasswort, http_request->argvalue[PharseGetValue_P(http_request, KonfigPasswort_P)], KonfigPasswortLen);
			KonfigPasswort[KonfigPasswortLen] = '\0';
			changeConfig_P(KonfigPasswort_P, KonfigPasswort);
			}
			
		} // else argc > 0
		
	cgi_PrintHttpheaderEnd();

	} // txp_cgi_config_intern()
	

/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface zum Aktivieren der Passwort-Sperre
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/
 
void txp_cgi_config_sperren(void *pStruct)
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;

	cgi_PrintHttpheaderStart();

	if (KonfigPasswort[0] == '\0')
		{
		printf_P(PSTR("zun&auml;chst Passwort in <a href=\"txpcfg-intern.cgi\" target=\"main\">Einstellungen im lokalen TxP-System</a> eingeben!"));
		}
	else
		{
		KonfigFreigabeErteilt = false;
		printf_P(PSTR("Konfigurationsseiten sind nun gesperrt. Zur Freigabe wieder das Passwort eingeben oder Taste der Baugruppe 2 x dr&uuml;cken."));
		}

	cgi_PrintHttpheaderEnd();
	}
	
	
#ifdef TXP_ANSCHLUSS

const PROGMEM char NetzRufnummer_P[] = "NETZRUFNR";
const PROGMEM char Geheimzahl_P[] = "PIN";
const PROGMEM char DynIPAktiv_P[] = "DYNIPAKTIV";
const PROGMEM char NetzPort_P[] = "NETZPORT";

#endif // TXP_ANSCHLUSS


const PROGMEM char RufnrServerAdr1_P[] = "RUFNRSERV1";
const PROGMEM char RufnrServerAdr2_P[] = "RUFNRSERV2";
const PROGMEM char RufnrServerAdr3_P[] = "RUFNRSERV3";
const char* RufnrServerAdr_P[] = { RufnrServerAdr1_P, RufnrServerAdr2_P, RufnrServerAdr3_P } ; // liegt dann zwar im RAM, ist aber halt so...
	
	
/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface zum Ändern der Einstellungen des TelexPhone-Interface bezüglich der Einbindung
 * in das globale ip-netz
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/
 
void txp_cgi_config_extern(void *pStruct)
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	uint8_t i;
	
	if (!KonfigFreigabe(pStruct))
		return;
	
	cgi_PrintHttpheaderStart();

	if ( http_request->argc == 0 )
		{
		CgiFormStartTabbed_P(PSTR("txpcfg-extern.cgi"));

		#ifdef TXP_ANSCHLUSS
		
		CgiFormInputFieldULong_P(PSTR("eigene Rufnummer im ip-telex-Netz:"), NetzRufnummer_P, 10, NetzRufnummer);
		
		CgiFormInputFieldULong_P(PSTR("Geheimzahl:"), Geheimzahl_P, 6, Geheimzahl);
		
		CgiFormCheckbox_P(PSTR("IP-Aktualisierung aktiv:"), DynIPAktiv_P, DynIPAktiv);

		CgiFormInputFieldULong_P(PSTR("Port-Nummer im Netz:"), NetzPort_P, 6, NetzPort);
		
		#endif // TXP_ANSCHLUSS
		
		for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
			CgiFormInputFieldText_P(PSTR("Adresse des Teilnehmer-Server:"), RufnrServerAdr_P[i], TlnAdresseMax, TeilnehmerServerAdresse[i]);

		CgiFormFinish_P(PSTR("Einstellung &Uuml;bernehmen"));
		}
	else // argc > 0
		{
		printf_P(PSTR("neue Einstellungen: <a href=\"txpcfg-extern.cgi\">weiter</a>"));

		#ifdef TXP_ANSCHLUSS

		NetzRufnummer = CgiCheckULong_P(http_request, PSTR("Netz-Rufnummer"), NetzRufnummer_P, NetzRufnummer);

		Geheimzahl = CgiCheckULong_P(http_request, PSTR("Geheimzahl"), Geheimzahl_P, Geheimzahl);

		DynIPAktiv = CgiCheckBool_P(http_request, PSTR("DynIPAktualisierung"), DynIPAktiv_P, DynIPAktiv);

		NetzPort = CgiCheckULong_P(http_request, PSTR("Netz-Port"), NetzPort_P, NetzPort);
		
		#endif //def TXP_ANSCHLUSS
		
		for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
			CgiCheckText_P(http_request, PSTR("Teilnehmer-Server"), RufnrServerAdr_P[i], TlnAdresseMax, TeilnehmerServerAdresse[i]);
			
		} // else argc > 0
		
	cgi_PrintHttpheaderEnd();

	} // txp_cgi_config_extern()
	

#ifdef TXP_ANSCHLUSS

/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface für eine Bus-Status-Liste (TWI-Busteilnehmer)
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void txp_cgi_TwiTlnListe(void *pStruct)
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	char Buf[10];
	
	cgi_PrintHttpheaderStart();

	printf_P(PSTR("Status der angeschlossenen TWI-Module:<p>"));
	for (uint8_t AnzZif = 1 ; AnzZif <= 2 ; AnzZif++)
		for (uint8_t Wahl = 0 ; Wahl <= ((AnzZif == 1) ? 9 : 99) ; Wahl++)
			{
			uint8_t BusNr = WahlZuAdresse(Wahl, AnzZif);
			AdresseZuWahlStr(BusNr, Buf);
			int16_t Stat = ((BusNr == BusEigenAdresse) ? Status : GetStatus(BusNr));
			if (Stat >= 0)
				{
				printf_P(PSTR("Nummer %s Status %02X<br>"), Buf, Stat);
				}
			}
	printf_P(PSTR("+++fertig"));

	cgi_PrintHttpheaderEnd();
	
	}

#endif // TXP_ANSCHLUSS

	
#if defined(MMC)
	
#include "system/filesystem/fat.h"
#include "system/filesystem/filesystem.h"
	
//! Erzeugt Inhaltsverzeichnis der SD-Karte als HTML-Seite.
//---------------------------------------------------------
void cgi_SdDirectory(void *pStruct)
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	char *BaseDir;
	
	if (!KonfigFreigabe(pStruct))
		return;
	
	cgi_PrintHttpheaderStart();

	struct fat_dir_entry_struct directory;
	struct fat_dir_struct* dd;

	// Wenn nur filename dann Stammverzeichniss wählen, wenn nicht Verzeichnis wählen
	if (http_request->argc == 0 || !PharseCheckName_P(http_request, PSTR("dir")))
		{
		fat_get_dir_entry_of_path(fs, "/" , &directory);
		BaseDir = NULL;
		printf_P(PSTR("<b>Content of /:</b><p>"));
		}
	else
		{
		BaseDir = http_request->argvalue[PharseGetValue_P(http_request, PSTR("dir"))];
		fat_get_dir_entry_of_path(fs, BaseDir, &directory);
		printf_P(PSTR("<b>Content of %s:</b><p>"), BaseDir);
		}
		
	// Verzeichbnis öffnen
	dd = fat_open_dir(fs, &directory);
	if (dd)
        {
		struct fat_dir_entry_struct dir_entry;
		
		// Verzeichniss inhalt lesen und Datei suchen
		while (fat_read_dir(dd, &dir_entry) > 0)
		    {
			if ((dir_entry.attributes & FAT_ATTRIB_DIR) != 0)
				{
				if (BaseDir == NULL)
					printf_P(PSTR("<a href =\"sddir.cgi?dir=%s\">%s</a> DIR<br>"), dir_entry.long_name, dir_entry.long_name);
				else
					printf_P(PSTR("<a href =\"sddir.cgi?dir=%s/%s\">%s</a> DIR<br>"), BaseDir, dir_entry.long_name, dir_entry.long_name);
				}
			else
				{ // normale Datei
				if (BaseDir == NULL)
					printf_P(PSTR("<a href =\"%s\">%s</a> %ld<br>"), dir_entry.long_name, dir_entry.long_name, dir_entry.file_size);
				else
					printf_P(PSTR("<a href =\"%s/%s\">%s</a> %ld<br>"), BaseDir, dir_entry.long_name, dir_entry.long_name, dir_entry.file_size);
				}
			}
		fat_close_dir(dd);
		}
	else
		printf_P(PSTR("Error reading directory!"));
	
	cgi_PrintHttpheaderEnd();
	}

#endif //defined(MMC)
	
	
/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Initialisiert den TelexPhone-clinet und registriert den Port auf welchen dieser lauschen soll.
 * \param 	NONE
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void txp_init()
	{
	init_Taste();
	init_RTS();
	init_CTS();
	
	ProtokollInit();

	DebugMsg[0] = '\0';

	#ifdef TXP_ANSCHLUSS
	
	SeriellUmsetzInit();

	PufferInit(&SendePuffer);
	PufferInit(&EmpfPuffer);
	SocketBufInit();

	AsciiDruckPuffer[0] = '\0';
	HtmlSendeText[0] = '\0';
	
	#endif // TXP_ANSCHLUSS
	
	// EEPROM auslesen
	char Buf[TlnAdresseMax];
	uint16_t i;

	#ifdef TXP_ANSCHLUSS
	
	if (readConfig_P(EigeneNummer_P, Buf) == 1)
		BusEigenAdresse = WahlZuAdresse(atoi(Buf), strlen(Buf));
	else
		BusEigenAdresse = 22 << 1;

	if (readConfig_P(FesteHst_P, Buf) == 1)
		FesteHauptstelle = atoi(Buf) != 0;
	else
		FesteHauptstelle = false;

	if (readConfig_P(AlternBeiBes_P, Buf) == 1)
		AlternativSucheBeiBesetzt = atoi(Buf) != 0;
	else
		AlternativSucheBeiBesetzt = true;

	if (readConfig_P(Hauptstelle_P, Buf) == 1)
		Hauptstelle = WahlZuAdresse(atoi(Buf), strlen(Buf));
	else
		Hauptstelle = 0, FesteHauptstelle = false;

	for (i = 0 ; i < 9 ; i++)
		DurchwahlTabelle[i] = 0;
	if (readConfig_P(DurchwahlTabelle_P, Buf) == 1)
		DurchwahlTabelleDekodieren(Buf); // Ergebnis wird ignoriert

	if (readConfig_P(NetzRufnummer_P, Buf) == 1)
		NetzRufnummer = atol(Buf);
	else
		NetzRufnummer = 0;
		
	if (readConfig_P(Geheimzahl_P, Buf) == 1)
		Geheimzahl = atoi(Buf);
	else
		Geheimzahl = 0;

	if (readConfig_P(DynIPAktiv_P, Buf) == 1)
		DynIPAktiv = atoi(Buf) != 0;
	else
		DynIPAktiv = false;
		
	if (readConfig_P(NetzPort_P, Buf) == 1)
		NetzPort = atol(Buf);
	else
		NetzPort = TXP_PORT;
		
	#endif // TXP_ANSCHLUSS
		
	for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
		if (readConfig_P(RufnrServerAdr_P[i], TeilnehmerServerAdresse[i]) == 1)
			; // ok
		else
			TeilnehmerServerAdresse[i][0] = '\0';

	if (readConfig_P(KonfigPasswort_P, KonfigPasswort) != 1)
		KonfigPasswort[0] = '\0';
	KonfigFreigabeErteilt = false;
		
	// dies müsste eigentlich in Protokoll.c enthalten sein.
	if (readConfig_P(ProtokollLevel_P, Buf) == 1)
		ProtokollLevel = atoi(Buf);
	else
		ProtokollLevel = 1;
		
	// dies müsste eigentlich in Protokoll.c enthalten sein.
	if (readConfig_P(ProtokollLevelTlnServ_P, Buf) == 1)
		ProtokollLevelTlnServ = atoi(Buf);
	else
		ProtokollLevelTlnServ = 1;
		
	TeilnehmerServerSocket = NO_SOCKET_USED;

	#ifdef TXP_ANSCHLUSS
	
	BusEigenAdrMehrfach = 1; // muss Potenz von 2 sein (also 1, 2, 4, 8, 16, ... , Standard = 1

	TxpSocketHandle = NO_SOCKET_USED;
	TxpSocketMode = SocketIdle;
	TxpSocketIP = 0;
	TxpSocketAbbauGeplant = false;
	StartTimer(&TxpSocketAbbruchTimer);
	SocketOutBufUsed = 0;
	SocketInBufUsed = 0;
	
	NetzEigeneIP = 0;
	
	TwiInit();

	TWCR = (1<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);

	Timer0Cnt_Max = 0;
	Timer0Callback_Max = 0;

	StartTimer(&DynIPAktualisierungTimer);
	DynIPAktualisierungEndzeit = 300; // 1/2 Minute 
		
	Status = (1 << StatBit_Frei) | (1 << StatBit_LeitungKennung);

	timer0_init(TxpTimerFreq); 
	if (!timer0_RegisterCallbackFunction(txp_timerEvent))
		return;

	StartTimer(&TxpThreadCheckTimer);
		
	cgi_RegisterCGI( txp_cgi_msg_In, PSTR("txp-msg-in.cgi"));
	cgi_RegisterCGI( txp_cgi_msg_Out, PSTR("txp-msg-out.cgi"));
	cgi_RegisterCGI( txp_cgi_TwiTlnListe, PSTR("txp-twitlnliste.cgi"));
	
	#endif // TXP_ANSCHLUSS
	
	cgi_RegisterCGI( txp_cgi_config_intern, PSTR("txpcfg-intern.cgi"));
	cgi_RegisterCGI( txp_cgi_config_extern, PSTR("txpcfg-extern.cgi"));
	cgi_RegisterCGI( txp_cgi_config_sperren, PSTR("txpcfg-sperren.cgi"));
	cgi_RegisterCGI( txp_cgi_debug, PSTR("txp-debug.cgi"));
	
#if defined(MMC)
	cgi_RegisterCGI( cgi_SdDirectory, PSTR("sddir.cgi"));
#endif //defined(MMC)

	#ifdef TXP_ANSCHLUSS
	
	RegisterTCPPort(TXP_PORT);
	
	Timer0Cnt_Min = 255;

	printf_P( PSTR("TelexPhone Port %u.\r\n") , TXP_PORT );

	THREAD_RegisterThread( txp_thread, PSTR("TxP"));

	#endif // TXP_ANSCHLUSS
	
	TlnBuchInit();
	
	#ifdef TXP_TLNSERVER
	
	txp_tlnserv_init();
	
	#endif // TXP_TLNSERVER

	#ifdef TXP_EMAIL
	
	txp_email_init();
	
	#endif //def TXP_EMAIL
	}


#endif //def TELEXPHONE


#if defined(MMC)

//! Ermittelt aktuelles Datum und Uhrzeit. 
//----------------------------------------
//! Wird für FAT-Funktionen erwartet.
void get_datetime(uint16_t* year, uint8_t* month, uint8_t* day, uint8_t* hour, uint8_t* min, uint8_t* sec)
	{
	struct TIME Time;
	CLOCK_GetTime(&Time);
	*day = Time.DD;
	*month = Time.MM;
	*year = Time.YY;
	*hour = Time.hh;
	*min = Time.mm;
	*sec = Time.ss;
	}

#endif //defined(MMC)


//@}

