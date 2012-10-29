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


#ifdef TXP_ANSCHLUSS

// Aktueller Modus

typedef enum
	{
	ModRuhe = 0, 
		//!< nichts läuft

	// Gehend = vom internen Anschluss zum Netz, Reservierung ist eingegangen
	ModGehendReserv = 1, 
		//!< Schnittstelle ist angesprochen worden, aber noch ein Einschaltkommando erhalten.
	ModGehendWaehlen = 2,
		//!< Einschaltkommando erhalten, Wahlaufforderung gesendet, 
		//!< ggf. auch schon Wahlziffern empfangen.
	ModGehendVerbunden = 4,
		//!< Wahl abgeschlossen, Socket geöffnet, Endgerät eingeschaltet.
	
	// Kommend = vom Netz zum internen Anschluss
	ModKommendVerbVorstufe = 11, 
		//!< es wird erst mal abgewartet, was aus der ankommenden Verbindung wird.
	ModKommendEinschalten = 12, 
		//!< Es wurden Daten oder ein Einschaltkommando (Durchwahl) empfangen.
	ModKommendWarteEinQuitt = 13, 
		//!< Warte auf Einschalt-Quittung des Endgeräts
	ModKommendVerbunden = 14, 
	
	ModPufferDruckUndSchluss = 18,
	ModWarteSchlussQuitt = 19,
	
	// z.B. über HTML-Seite verursachte direkte Druckausgabe
	ModDirektdruckWarteEinQuitt = 21, //!< Warte auf Einschalt-Quittung des Endgeräts
	ModDirektdruckVerbunden = 22, 
	
	ModDeaktiviert = 31, //!< Durch Tastendruck ausgeschaltet.
	
	} TModus;
	
	
//! Aktueller Modus. Sollte nur durch ModusWechsel geändert werden.	
static TModus Modus;

	
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


volatile uint16_t MsTimerCnt;
//!< Die Timer-Basisvariable


volatile uint8_t MsTimerVorteilerCnt;


volatile static uint16_t TwiLebenszeichenZaehler; //! \todo Ersetzen
	//!< Zählt rückwärts die Takte bis zum nächsten Lebenszeichen auf dem TWI-Bus.
	//!< wird während der Verbindung missbraucht zum Zählen der Takte bis zur Pegelwiederholung.
	
static TMsTimer RuheTimer;  
	//!< Misst die Zeit in der nix passiert. Wird bei Datenempfang und Sendung und 
	//!< Verbindungsaufbau auf Null gesetzt. Wird auch für Timeout beim Warten auf 
	//!< die Ausschalt-Quittung benutzt. In Grundstellung wird die Dauer der Grundstellung
	//!< gemessen für das Protokollschreiben.
	
volatile static uint16_t SocketLebenszeichenZaehler; //! \todo Ersetzen
	//!< Zählt rückwärts die Takte bis zum nächsten Lebenszeichen auf der TCP-Verbindung.

volatile static uint16_t TxpThreadCheckCount;
	//!< Prüft, ob die Funktion void txp_thread() ausreichend häufig aufgerufen wird.

	
volatile TPuffer SendePuffer; 
	//!< Puffer (mit Baudot-Codes gefüllt) für die Richtung Netz -> Endgerät
	
volatile TPuffer EmpfPuffer; 
	//!< Puffer (mit Baudot-Codes gefüllt) für die Richtung Endgerät -> Netz
	
enum { AsciiDruckPufferMax = 100, HtmlSendeTextMax = 400 } ;
	//!< Puffergrößen für Textpuffer bei HTML-Kommunikation

static char AsciiDruckPuffer[AsciiDruckPufferMax];
	//!< Puffer für zu druckenden Text (Netz -> Endgerät), mit Null abgeschlossen

static char HtmlSendeText[HtmlSendeTextMax];
	//!< Puffer für zu Anzuzeigenden Text (Endgerät -> Netz), mit Null abgeschlossen


//OBSOLET static int TxpServerSocket;
	//!< Handle für eingehende Txp-Verbindungen ("Server")
	
//OBSOLET static int TxpClientSocket;
	//!< Handle für ausgehende Txp-Verbindungen ("Client")

static int TxpSocketHandle;
	//!< Verweis auf Socket für Txp-Kommunikation. Istzustand. Wenn ungültig, aber TxpSocketMode
	//!< ungleich Idle, ist ein kurzzeitiger Verbindungsverlust eingetreten.
	
	
static enum { 
	SocketIdle, //!< Unbenutzt
	SocketOriginate, //!< Ausgehende Verbindung
	SocketAnswer //!< Kommende Verbindung
	} TxpSocketMode; 
	//!< Speichert Sollzustand der Txp-Verbindung
	
	
static long TxpSocketIP;
	//!< Aktueller Verbindungspartner. Bei TxpSocketMode = SocketAnswer wird
	//!< nach Verbindungsverlust geprüft, ob neu aufgenommene Verbindung wieder
	//!< vom gleichen Anschluss kommt.

static uint16_t TxpSocketPort;
	//!< Bei ausgehenden Verbindungen der gewünschte Port des Empfängers.

static bool TxpSocketAbbauGeplant;
	//!< Wird auf true gesetzt, wenn ein Verbindungsabbau bevorsteht.
	//!< Abbau erfolgt immer durch Anrufer. 
	//!< \p Wenn true und TxpSocketMode = SocketOriginate wird Abbau nach letzem Datenblock ausgelöst
	//!< \p Wenn true und TxpSocketMode = SocketAnswer wird nach gemeldetem Verbindungsabbau
	//!< TxpSocketMode auf SocketIdle gesetzt und TxpSocketIP gelöscht.
	
static uint8_t TxpSocketProtVersion;
	//!< Vereinbarte Protokollversion der Kommunikation

static uint8_t TxpSocketProtVersionVorschlag;
	//!< Selbst Vorgeschlagene Protokollversion der Kommunikation

static bool TxpSocketModeAscii; 
	//!< true, wenn die Daten als ASCII und nicht als Baudot-Daten übertragen werden.

static TMsTimer TxpSocketAbbruchTimer;
	//!< Nach 30 Sekunden unplanmäßigem Verbindungsverlust wird entgültig abgebaut.
	
enum { SocketInBufMax = 2500 } ; //!< Größe des TCP-Empfangspuffers

static uint16_t SocketInBufUsed; //!< Benutzter Teil des TCP-Empfangspuffers

static char SocketInBuf[SocketInBufMax]; //!< TCP-Empfangspuffer

	
enum { SocketOutBufMax = 2500 } ; //!< Größe des TCP-Sendepuffers

static uint16_t SocketOutBufUsed; //!< Benutzter Teil des TCP-Sendepuffers

static char SocketOutBuf[SocketOutBufMax]; //!< TCP-Sendepuffer


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
	//!< \todo obsolet?
	
static uint16_t SocketSendeSperrZaehler;
	//!< Zählt nach Sendefehlern herunter und verhindert solange neue Sendeversuche.
	//!< \todo obsolet?
	
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
	
static unsigned long KonfigFreigabeZeit;
	//!< Uhrzeit der letzen Freigabe bzw. Benutzung von freizugebenden Seiten
	//!< \todo durhc Timer ersetzen
	
	
static int TeilnehmerServerSocket;
	//!< Handle für ausgehende Verbindungen zum Teilnehmer-Server
	//!< Wird in ZWEI Situationen benutzt: 
	//!< a) Dynamische IP-Aktualisierung
	//!< b) Abfrage einer Teilnehmer-Adresse
	
	
#ifdef TXP_ANSCHLUSS

static bool DynIPAktiv;
	//!< Soll die eigene IP-Adresse auf den Teilnehmer-Server aktualisiert werden?
	
static uint32_t DynIPAktZeitZaehler;
	//!< Macht alle 15 Minuten eine Aktualsisierungsmeldung an einen der Teilnehmer-Server (sofern aktiviert).
	

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
void txp_timerEvent(void)
	{
	uint8_t t0c = TCNT0;
	Timer0CallbackCount++;

	MsTimerVorteilerCnt++;
	if (MsTimerVorteilerCnt >= TxpTimerFreq / 100)
		{
		MsTimerCnt++;
		MsTimerVorteilerCnt = 0;
		}
		
	wdt_reset();
	
	SocketLebenszeichenZaehler++; //! TODO Ersetzen
	
	if (DynIPAktiv)
		DynIPAktZeitZaehler++; //! TODO Ersetzen
	
	if (TxpThreadCheckCount++ > 30 * TxpTimerFreq) // nach 30 Sekunden Reset
		{ //! TODO Ersetzen
		Protokollieren("TxP: Reset wegen nicht-Aufruf von txp_thread()\r\n");
		ProtokollSpeichern(true);
		softreset();
		}
		
#if defined(LEDROT_TXPTHREADBLOCK)
	if (TxpThreadCheckCount > TxpTimerFreq / 2) // nach halber Sekunde geht rot an
		LED_on(ROT);
#endif //defined(LEDROT_TXPTHREADBLOCK)
		
	TwiWatchdogCount++; //! TODO Ersetzen
		
	if (SocketSendeSperrZaehler > 0)
		SocketSendeSperrZaehler--; //! TODO Ersetzen
	
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
			StartTimer(&RuheTimer);
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
				StartTimer(&RuheTimer);
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
			StartTimer(&RuheTimer);
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

	else if (Modus != ModRuhe && Modus != ModWarteSchlussQuitt)
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
	static uint16_t TasteZaehler;

	switch (TasteZustandIntern)
		{
		case TasteAus:
			if (!get_Taste()) // Gedrückt = LOW!
				{
				if (++TasteZaehler > TxpTimerFreq * 5/100) // 50 Millisekunden
					//! TODO Ersetzen
					{ // ausreichend lang gedrückt
					TasteZustandIntern = TasteEin;
					TasteZaehler = 0;
					
					// Zugang zur Konfiguration erlauben.
					struct TIME CurTime;
					CLOCK_GetTime(&CurTime);
					KonfigFreigabeZeit = CurTime.time;
					}
				}
			else
				{
				TasteZaehler = 0;
				}
			break;

		case TasteEin: 
			if (!get_Taste()) // Gedrückt = LOW!
				{
				if (++TasteZaehler > TxpTimerFreq * 8/10) // 0,8 Sekunden
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
			TasteZaehler = 0;
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
static void ModusWechsel(TModus neu)
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
			StartTimer(&RuheTimer);
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
			TxpSocketModeAscii = false;
			AsciiDruckPuffer[0] = '\0';
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
			TxpSocketModeAscii = false;
			AsciiDruckPuffer[0] = '\0';
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
			StartTimer(&RuheTimer);
			break;
	
		case ModKommendVerbunden: 
			SET_BIT_Status(StatBit_FsMeldBetrieb);
			SET_BIT_Status(StatBit_FsMeldEin);
			SET_BIT_Status(StatBit_Verbunden);
			BusEmpfMark = true;
			SendeMark = true;
			break;
	
		case ModPufferDruckUndSchluss: 
			CLR_BIT_Status(StatBit_Verbunden);
			StartTimer(&RuheTimer);
			break;
		
		case ModWarteSchlussQuitt:
			CLR_BIT_Status(StatBit_FsBefBetrieb);
			CLR_BIT_Status(StatBit_FsBefEin);
			CLR_BIT_Status(StatBit_Verbunden);
			StartTimer(&RuheTimer);
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
		
		default:
			return; // nix wird geändert
		} // switch neu

	Modus = neu; // jetzt wird der neue Modus wirklich aktiv.
	}
	
	
//! TCP-Puffer initialisieren
static void SocketBufInit()
	{
	SocketInBufUsed = 0;
	SocketOutBufUsed = 0;
	SocketLebenszeichenZaehler = 0;
	SocketSendeSperrZaehler = 0;
	}
	
	
//! Empfangene Daten vom Socket in den SendePuffer schreiben.
//! \retval true, wenn Zeichen gedruckt wird (ausgegeben wird).
static bool SchreibeZeichenInSendePuffer(char c)
	{
	uint8_t Code1, Code2;
	
	//! \todo Umlaute übersetzen.
	
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
	// Neue Verbindungswünsche bearbeiten
	// ----------------------------------
	int NewServerSocket = CheckPortRequest(TXP_PORT);
	if (NewServerSocket != NO_SOCKET_USED)
		{
		extern struct TCP_SOCKET TCP_sockettable[];
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
				StartTimer(&TxpSocketAbbruchTimer);
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
			} // TxpSocketMode == SocketIdle
			
		else if (TxpSocketMode == SocketAnswer && TxpSocketHandle == NO_SOCKET_USED)
			{
			if (TxpSocketIP == TCP_sockettable[TxpSocketHandle].SourceIP)
				{
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR(" ...wiederverbindung ok\r\n"));
				TxpSocketHandle = NewServerSocket;
				Abweisen = false;
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
	if (TxpSocketHandle != NO_SOCKET_USED && CheckSocketState(TxpSocketHandle) == SOCKET_NOT_USE)
		{ // ID#242 ID#342 ID#314 ************************************************
		if (TxpSocketAbbauGeplant)
			{
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR("TxP: Socket wurde von Gegenstelle erwartet geschlossen\r\n" ));
			TxpSocketMode = SocketIdle;
			TxpSocketIP = 0;
			TxpSocketAbbauGeplant = false;
			SocketOutBufUsed = 0;
			SocketInBufUsed = 0;
			}
		else
			{
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR("TxP: Socket wurde von Gegenstelle UNERWARTET geschlossen\r\n" ));
			}
			
		CloseTCPSocket(TxpSocketHandle);
		TxpSocketHandle = NO_SOCKET_USED;
		return; // GGf wieder aufnahme der Verbindung beim nächsten Aufruf dieser funktion...
		}
		
	// soll offene Verbindung geschlossen werden?
	// --------------------------------------------------
	if (TxpSocketHandle != NO_SOCKET_USED 
		&& TxpSocketAbbauGeplant 
		&& TxpSocketMode == SocketOriginate 
		&& SocketOutBufUsed == 0)
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
		}
		
	// Auf neue Daten testen
	// ---------------------------------
	if (TxpSocketHandle != NO_SOCKET_USED)
		{
		StartTimer(&TxpSocketAbbruchTimer);
			// so lange Verbindung aufrecht bleibt Timer auf 0

		int InCount = GetBytesInSocketData(TxpSocketHandle);
		
		if (SocketInBufUsed + InCount > SocketInBufMax)
			InCount = SocketInBufMax - SocketInBufUsed;
			
		if (InCount > 0) 
			{
			int Res = GetSocketData(TxpSocketHandle, InCount, SocketInBuf + SocketInBufUsed);
			
			if (ProtokollLevel == 3) // Daten explizit
				{
				// printf_P(PSTR("TxP: Socket Empfang: (%d/" ), InCount);
				// printf_P(PSTR("%d)"), Res);
				// for (uint16_t i = 0 ; i < Res ; i++)
					// printf_P(PSTR(" %02X"), SocketInBuf[SocketInBufUsed + i]);
				// printf_P(PSTR(" --> neu Ges %d\r\n"), SocketInBufUsed + Res);
				ProtokollierenInt_P(PSTR("TxP: Socket Empfang: (%d/" ), InCount);
				ProtokollierenInt_P(PSTR("%d)"), Res);
				for (uint16_t i = 0 ; i < Res ; i++)
					ProtokollierenInt_P(PSTR(" %02X"), SocketInBuf[SocketInBufUsed + i]);
				ProtokollierenInt_P(PSTR(" --> neu Ges %d\r\n"), SocketInBufUsed + Res);
				}		
				
			if (Res > 0)
				SocketInBufUsed += Res;
			}
		} // if TxpSocketHandle != NO_SOCKET_USED 
		
	// ggf Lebenszeichen erzeugen
	// --------------------------
	if (TxpSocketMode != SocketIdle
		&& SocketLebenszeichenZaehler > 4 * TxpTimerFreq 
	    && SocketOutBufUsed == 0
		&& SocketSendeSperrZaehler == 0)
		{ // alle 4 Sekunden ein Lebenszeichen
		SocketOutBuf[0] = TXPC_NULL;
		SocketOutBuf[1] = 0;
		SocketOutBufUsed = 2;
		}

	// Ist ein Neuaufbau der Verbindung erforderlich?
	// ----------------------------------------------
	if (TxpSocketMode == SocketOriginate 
		&& TxpSocketHandle == NO_SOCKET_USED 
		&& SocketOutBufUsed != 0
		&& !TxpSocketAbbauGeplant)
		//! \todo Verzögerung?
		{
		TxpSocketHandle = Connect2IP(TxpSocketIP, TxpSocketPort); 
	 
		if (TxpSocketHandle == -1)
			{ 
			// Verbindung konnte nicht aufgebaut werden
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR("TxP: Wieder-Oeffnung des Socket versagt.\r\n"));
			TxpSocketHandle = NO_SOCKET_USED;
			return;
			}

		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Wieder-Oeffnung des Socket erfolgreich.\r\n"));
			
		//! \todo Initialsierungsdaten?
		
		}
		
	// Daten ggf. ins Netz senden
	// --------------------------------------------------
	if (TxpSocketHandle != NO_SOCKET_USED 
		&& SocketOutBufUsed > 0 
		&& SocketSendeSperrZaehler == 0) //! \todo && !HaltSocketOut
		{
		int Res = PutSocketData_RPE(TxpSocketHandle, SocketOutBufUsed, SocketOutBuf, RAM);
		SocketLebenszeichenZaehler = 0; 

		if (ProtokollLevel == 3)
			{
			ProtokollierenInt_P(PSTR("TxP: Socket Sendung: (%d)" ), SocketOutBufUsed);
			for (uint16_t i = 0 ; i < SocketOutBufUsed ; i++)
				ProtokollierenInt_P(PSTR(" %02X"), SocketOutBuf[i]);
			ProtokollierenInt_P(PSTR(" --> Res %d" ), Res);
			ProtokollierenInt_P(PSTR(" Sum %d\r\n" ), SocketAnzahlZeichenGesendet);
			// printf_P(PSTR("TxP: Socket Sendung: (%d)" ), SocketOutBufUsed);
			// for (uint16_t i = 0 ; i < SocketOutBufUsed ; i++)
				// printf_P(PSTR(" %02X"), SocketOutBuf[i]);
			// printf_P(PSTR(" --> Res %d" ), Res);
			// printf_P(PSTR(" Sum %d\r\n" ), SocketAnzahlZeichenGesendet);
			}

		if (Res <= 0)
			{ // gar nichts gesendet.
			SocketSendeFehlerZaehler++; //! \todo Prüfen ob wiederholtes Datensenden sinnvoll oder Verbindungsabbau und Wieder-Oeffnung besser...
			if (SocketSendeFehlerZaehler >= 10)
				{
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: Mehrfache Fehler beim Senden ins Netz, Socket wird voruebergehend geschlossen\r\n" ));
				CloseTCPSocket(TxpSocketHandle);
				TxpSocketHandle = NO_SOCKET_USED;
				}
			else
				SocketSendeSperrZaehler = 1 * TxpTimerFreq; // 1 Sekunde für nächsten Versuch warten.
			}
			
		else if (Res < SocketOutBufUsed)
			{ // nicht alles konnte gesendet werden...
			memmove(SocketOutBuf, SocketOutBuf + Res, SocketOutBufUsed - Res);
			SocketOutBufUsed -= Res;
			SocketSendeFehlerZaehler = 0;
			}
			
		else // Puffer erfolgreich vollständig gesendet.
			{
			SocketOutBufUsed = 0;
			SocketSendeFehlerZaehler = 0;
			}
			
		} // if es gibt was zu senden

	// Abbruch wenn zu lange keine Verbindung besteht...
	// -------------------------------------------------
	if (TxpSocketMode != SocketIdle
		&& TxpSocketHandle == NO_SOCKET_USED
		&& TimerVal(&TxpSocketAbbruchTimer) >= 3000) // 30 Sekunden.
		{
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Zeitueberschreibung bei Wiederaufnahme der Verbindung\r\n" ));
		TxpSocketMode = SocketIdle;
		TxpSocketAbbauGeplant = false;
		TxpSocketIP = 0;
		SocketOutBufUsed = 0;
		SocketInBufUsed = 0;
		}

	} // SocketBearbeiten()


//! Interner Statuswechsel bei Ende-befehl (Socket geschlossen oder anderes Ende-Kommando)

static void InterneVerbindungBeenden()
	{
	switch (Modus)
		{
		case ModRuhe:
		case ModGehendReserv:
		case ModGehendWaehlen:
		case ModPufferDruckUndSchluss:
		case ModWarteSchlussQuitt:
		case ModDirektdruckWarteEinQuitt:
		case ModDirektdruckVerbunden: 
		case ModDeaktiviert:
			// in diesen Zuständen ist normalerweise kein Socket offen.
			// daher auch keine Reaktion auf geschlossenen Socket.
			break; 
			
		case ModKommendVerbVorstufe:
		case ModKommendEinschalten:
		case ModKommendWarteEinQuitt:
			if (ProtokollLevel >= 1)
				ProtokollierenInt_P(PSTR("TxP: Wechsel nach Modus Ruhe (von %d)\r\n"), Modus);
			ModusWechsel(ModRuhe); 
			break;
		
		case ModKommendVerbunden:
		case ModGehendVerbunden:
			if (ProtokollLevel >= 1)
				ProtokollierenInt_P(PSTR("TxP: Wechsel nach Modus PufferDruckUndSchluss (von %d)\r\n"), Modus);
			ModusWechsel(ModPufferDruckUndSchluss);
			break;
		}
	} // InterneVerbindungBeenden()
	
	
//! Interpretiert empfangene Daten vom Socket und schiebt diese in den 
//! EmpfPuffer. Wandelt Daten aus dem SendePuffer um.
//! Bearbeitet auch Statusänderungen.

static void TxpDatenVerarbeiten()
	{
	// Verbindungsabbau bearbeiten
	// ---------------------------
	if (TxpSocketMode == SocketIdle)
		{
		InterneVerbindungBeenden();
		} // if (TxpSocketMode == SocketIdle)
	
	// Daten des Socket-Empfangspuffer interpretieren
	// ----------------------------------------------
	if (SocketInBufUsed > 0)
		{ 
		uint8_t i = 0;
		while (i < SocketInBufUsed)
			{
			char c = SocketInBuf[i];
			// Achtung: In dieser Schleife entweder i weiterbringen oder break!
			// ****************************************************************
			
			// im Folgenden KEIN switch verwenden wegen break!
			if (c == '\r' || c == '\n' || (c >= ' ' && c <= '~'))
				{ // ein ASCII-Zeichen
				// ID#246 ID#344 *****************************************************
				TxpSocketModeAscii = true;
				int alen = strlen(AsciiDruckPuffer);
				if (alen < AsciiDruckPufferMax-2)
					{
					AsciiDruckPuffer[alen] = c;
					AsciiDruckPuffer[alen+1] = '\0';
					i++;
					SocketAnzahlZeichenEmpfangen++;
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
				TxpSocketModeAscii = false;
				uint8_t len = SocketInBuf[i+1];
				
				if (i + 2 + len <= SocketInBufUsed && PufferAnzahl(&SendePuffer) + len < MaxPuffer)
					{ // Baudot-Code-Block ist vollständig UND noch entsprechend Platz im Sendepuffer
					if (ProtokollLevel == 2) // Datenmengen protokollieren
						{
						ProtokollierenInt_P(PSTR("TxP: EmpfB %16d" ), PufferAnzahl(&SendePuffer));
						ProtokollierenInt_P(PSTR("%4d"), len);
						ProtokollierenInt_P(PSTR("%4d\r\n"), low(SocketAnzahlZeichenEmpfangen) + len);
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

				InterneVerbindungBeenden();
				
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
						ProtokollierenInt_P(PSTR("TxP: Protokollversion-Vorschlag %d empfangen\r\n"), ProtVorschlag);

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
						
					if (TxpSocketProtVersion == 0) // noch nichts festgelegt, also Gegenvorschlag senden.
						{
						SocketOutBuf[SocketOutBufUsed++] = TXPC_VERSION;
						SocketOutBuf[SocketOutBufUsed++] = 1;
						SocketOutBuf[SocketOutBufUsed++] = TxpSocketProtVersionVorschlag;
						
						if (ProtokollLevel >= 2)
							ProtokollierenInt_P(PSTR("TxP: Sende Protokollversion-Gegenvorschlag %d\r\n"), TxpSocketProtVersionVorschlag);
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
			
		} // if GetBytesInSocketData > 0

	// vom Endgerät empfangene Daten übersetzen
	// --------------------------------------------------
	// es wird gesendet, wenn es was zu senden gibt 
	// UND      a) es viel zu senden gibt 
	//     ODER b) 0,5 sekunden nicht getippt wurde
	//     ODER c) Alles was bisher gesendet wurde schon verarbeitet ist.
	int InCount = PufferAnzahl(&EmpfPuffer);
	if (InCount > 20
	    || (InCount > 0 && ((TimerVal(&RuheTimer) >= 80) // 0,8 Sekunden Tipp-Pause
		                    || (SocketAnzahlZeichenQuittiert == low(SocketAnzahlZeichenGesendet)) // alles was gesendet wurde, ist schon verarbeitet
						    )
			)
		)
		{ // ID#244 ID#344 ***************************************************************
		if (TxpSocketModeAscii)
			{
			uint8_t ProtAnz = 0;
			while (!PufferLeer(&EmpfPuffer) && SocketOutBufUsed < SocketOutBufMax - 3)
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
				
			} // if TxpSocketModeAscii
		else
			{ // Baudot-Datenblock senden
			uint8_t len = PufferAnzahl(&EmpfPuffer);
			if (len > SocketOutBufMax - 3 - SocketOutBufUsed)
				len = SocketOutBufMax - 3 - SocketOutBufUsed;
				
			if (ProtokollLevel == 2) // Datenmengen
				{
				ProtokollierenInt_P(PSTR("TxP: SendB %4d" ), (uint8_t)(low(SocketAnzahlZeichenGesendet) - SocketAnzahlZeichenQuittiert));
				ProtokollierenInt_P(PSTR("%4d"), len);
				ProtokollierenInt_P(PSTR("%4d\r\n"), low(SocketAnzahlZeichenGesendet) + len);
				}
				
			SocketOutBuf[SocketOutBufUsed] = TXPC_BAUDOT_DATA;
			SocketOutBufUsed++;
			SocketOutBuf[SocketOutBufUsed] = len;
			SocketOutBufUsed++;
			SocketAnzahlZeichenGesendet += len;
			while (len > 0)
				{
				SocketOutBuf[SocketOutBufUsed] = PufferAusg(&EmpfPuffer);
				SocketOutBufUsed++;
				len--;
				}
			SocketSendeQuittung = true;
			} // else !TxpSocketModeAscii
		}
		
	// ggf. Anzahl verarbeiteter Zeichen zurückmelden
	// --------------------------------------------------
	if (!TxpSocketModeAscii 
		&& (Modus == ModKommendVerbunden || Modus == ModGehendVerbunden)
		&& (SocketSendeQuittung || SocketLebenszeichenZaehler > 4 * TxpTimerFreq)
		&& SocketSendeSperrZaehler == 0)
		{
		SocketOutBuf[SocketOutBufUsed] = TXPC_QUITT;
		SocketOutBufUsed++;
		SocketOutBuf[SocketOutBufUsed] = 1;
		SocketOutBufUsed++;
		SocketOutBuf[SocketOutBufUsed] = 
			(uint8_t) (low(SocketAnzahlZeichenEmpfangen) - PufferAnzahl(&SendePuffer));
		SocketOutBufUsed++;
		SocketSendeQuittung = false;
		}

	} // TxpDatenVerarbeiten()
	

//! Schreibt ein Ende-Kommando mit Zusatztext in den Socket-Sendepuffer
static void SendeStopkommando(PGM_P s)
	{
	uint8_t len = strlen_P(s);
	SocketOutBuf[SocketOutBufUsed] = TXPC_STOP;
	SocketOutBuf[SocketOutBufUsed + 1] = len;
	strcpy_P(SocketOutBuf + SocketOutBufUsed + 2, s);
	SocketOutBufUsed += 2 + len;
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
				if (ProtokollLevel >= 2)
					{
					Protokollieren_P(PSTR("TxP: Verbindung an Teilnehmer-Server "));
					Protokollieren(TeilnehmerServerAdresse[ServerI]); 
					Protokollieren_P(PSTR(" hergestellt\r\n"));
					}
				return true;
				}
			TeilnehmerServerSocket = NO_SOCKET_USED;
			if (ProtokollLevel >= 1)
				{
				Protokollieren_P(PSTR("TxP: Verbindungsversuch an Teilnehmer-Server "));
				Protokollieren(TeilnehmerServerAdresse[ServerI]); 
				Protokollieren_P(PSTR(" gescheitert\r\n"));
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
				ProtokollierenInt_P(PSTR(" Port %d\r\n"), td->Port);
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
					ProtokollierenInt_P(PSTR(" Port %d\r\n"), td->Port);
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

	TxpSocketMode = SocketOriginate;
	TxpSocketAbbauGeplant = false;
	TxpSocketProtVersion = 0;
	TxpSocketProtVersionVorschlag = PROTVERSION_AKTUELL;
	
	StartTimer(&TxpSocketAbbruchTimer);
		
	if (td->AdrArt == AsciiUrl || td->AdrArt == AsciiIP)
		{ // ID#226 *********************************************
		BusSenden(BusQuittEin);
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Client-Socket Ascii erfolgreich geoeffnet -> Einschalt-Quittung an TWI\r\n" ));
		ModusWechsel(ModGehendVerbunden);
		SocketBufInit();
		TxpSocketModeAscii = true;
		return 0;
		}
	else // TxpUrl oder TxpIP
		{ // ID#222 ********************************************
		if (ProtokollLevel >= 1)
			ProtokollierenInt_P(PSTR("TxP: Client-Socket Txp erfolgreich geoeffnet -> sende Durchwahl %d\r\n"), td->Durchwahl);

		SocketBufInit();
		TxpSocketModeAscii = false;
		
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

	TxpThreadCheckCount = 0;
	
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
					ProtokollierenInt_P(PSTR("TxP: TWI Reservierung intern / gehend von %d\r\n" ), Code);
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
					ProtokollierenInt_P(PSTR("TxP: TWI Wahlziffer %d intern / gehend\r\n" ), Code - BusKdoWahlziffer0);
				if (Modus == ModGehendWaehlen && TxpSocketMode == SocketIdle)
					{
					// ID#221 ********************************************
					Wahlnummer = 10 * Wahlnummer + (Code - BusKdoWahlziffer0);
					Wahlziffern++;
					StartTimer(&RuheTimer);
					TlnServerAbfrageWiederholungssperre = false;
					
					if (TlnSuche(Wahlnummer, false, &GewaehlterTln))
						{ // ID#222 ********************************************
						if (ProtokollLevel >= 1)
							ProtokollierenInt_P(PSTR("TxP: Teilnehmer %ld im eigenen Telefonbuch gefunden.\r\n"), GewaehlterTln.Nummer);
		
						if (Wahlziffern >= 5 && (GewaehlterTln.Flags & TlnFlag_Lokal) == 0)
							RufnummerBeiTlnServerAbfragen(); // TEST ob das sinnvoll ist...
							
						switch (Verbindungsaufbau(&GewaehlterTln))
							{
							case 0: 
								break; // erfolgreich
								
							case 1:
								// ID#223 ********************************************
								if (GewaehlterTln.Flags & TlnFlag_Lokal)
									{
									BusSenden(BusKdoSchluss);
									ModusWechsel(ModWarteSchlussQuitt);
									}
								else if (Wahlziffern >= 5)
									{ // mal den Rufnummer-Server befragen...
									// herausgenommen, da oben TEST TimerVal(&RuheTimer) > 200; // nicht mehr 2 Sekunden warten.
									}
								break;
							
							case 2:
								BusSenden(BusKdoSchluss);
								ModusWechsel(ModWarteSchlussQuitt);
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
			case BusKdoSchluss:

				if (ProtokollLevel >= 1)
					{
					if (Code == BusQuittSchluss)
						Protokollieren_P(PSTR("TxP: TWI Ausschaltung quittiert\r\n" ));
					else
						Protokollieren_P(PSTR("TxP: TWI Ausschaltung intern\r\n" ));
					}
					
				if (Code == BusQuittSchluss && Modus != ModWarteSchlussQuitt)
					{
					if (ProtokollLevel >= 1)
						Protokollieren_P(PSTR("TxP: Schlussquittung ohne Aufforderung\r\n"));
					FalschCodeEmpfangen(Code);
					}

				// ID#212 ********************************************
				// ID#224 ********************************************
				if (Modus != ModRuhe && Code == BusKdoSchluss)
					BusSenden(BusQuittSchluss);
					
				TxpSocketAbbauGeplant = true;

				SocketOutBuf[SocketOutBufUsed++] = TXPC_ENDE;
				SocketOutBuf[SocketOutBufUsed++] = 0;
				
				// Html-Puffer löschen
				AsciiDruckPuffer[0] = '\0'; // damit es keine neue Einschaltung gibt.
					
				// ID#411 ID#104 *************************************
				// ???? \todo Ablauftabelle prüfen...
				break;

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
			BusSenden(BusKdoSchluss);
			ModusWechsel(ModWarteSchlussQuitt);
			//! \todo Socket ordentlich schließen
			}
		}
	
	// ======================================================================
	// Socket Empfang und Sendung
	// ======================================================================

	SocketBearbeiten();
	TxpDatenVerarbeiten();
	
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
				ProtokollierenInt_P(PSTR("TxP: Anwahl intern an %d erfolgt\r\n"), Durchwahl);
			BusSenden(BusKdoEin);
			ModusWechsel(ModKommendWarteEinQuitt);
			}
		else
			{ // ID#322 ********************************************
			if (ProtokollLevel >= 1)
				ProtokollierenInt_P(PSTR("TxP: Anwahl intern an %d VERSAGT\r\n"), Durchwahl);
			strcpy_P(DebugMsg, PSTR("Reservierung fuer Einschaltung konnte nicht versand werden"));

			SendeStopkommando(PSTR("occ\r\n"));
			
			ModusWechsel(ModRuhe);
			}
		} // if Modus == ModKommendEinschalten
			
	// ==========================================================================
	// Timeouts? (auch 2 Sekunden Wahlpause...)
	// ==========================================================================

	if (Modus == ModGehendWaehlen 
		&& !TlnServerAbfrageWiederholungssperre
		&& Wahlziffern >= 5
		&& TimerVal(&RuheTimer) >= 200)
		{ // 2 Sekunden Wahlpause und 5 Ziffern gewählt
		// ID#231 **************************************************************
		RufnummerBeiTlnServerAbfragen();
		}
		
	if (Modus == ModWarteSchlussQuitt && TimerVal(&RuheTimer) > 300)
		{ // 3 Sekunden keine Schlussquittung empfangen
		// ID#412 ****************************************************************
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Timeout beim Warten auf die Schlussquittung\r\n" ));
		ModusWechsel(ModRuhe);
		}
		
	if (Modus == ModKommendWarteEinQuitt && TimerVal(&RuheTimer) > 300)
		{ // 3 Sekunden keine Einschalt-Quittung empfangen
		// ID#332 ***************************************************************
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Timeout beim Warten auf die Einschaltquittung\r\n" ));
		BusSenden(BusKdoSchluss);
		SendeStopkommando(PSTR("err\r\n"));
		ModusWechsel(ModWarteSchlussQuitt);
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
				ModusWechsel(ModRuhe);
				break;

			case ModPufferDruckUndSchluss:
				// ID#422 *************************************************************
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: Taste gedruckt --> Reste-Druck abgebrochen\r\n" ));
				BusSenden(BusKdoSchluss);
				ModusWechsel(ModWarteSchlussQuitt);
				AsciiDruckPuffer[0] = '\0';
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
			ModusWechsel(ModRuhe);
			}
		} // if ModRuhe && Text per HTML empfangen
		
	if (Modus == ModDirektdruckVerbunden 
		|| Modus == ModKommendVerbunden 
		|| Modus == ModGehendVerbunden 
		|| Modus == ModPufferDruckUndSchluss)
		{
		// zu druckenden Text umwandeln
		// ---------------------------
		if (AsciiDruckPuffer[0] != '\0' && PufferLeer(&SendePuffer))
			{
			uint8_t ki = 0; // Kopierindex

			while (AsciiDruckPuffer[ki] != '\0' && !PufferVoll(&SendePuffer))
				{
				if (SchreibeZeichenInSendePuffer(AsciiDruckPuffer[ki]))
					{ // nur im Echo darstellen, wenn es auch gedruckt wurde.
					if (Modus == ModDirektdruckVerbunden)
						ZeichenInHtmlSendeText(AsciiDruckPuffer[ki]);
					}
				ki++;
				}

			if (ki > 0)
				memmove(AsciiDruckPuffer, AsciiDruckPuffer + ki, strlen(AsciiDruckPuffer) - ki + 1); 
			
			SocketAnzahlZeichenEmpfangen += ki;
			
			if (ProtokollLevel == 2) // Datenmengen protokollieren
				{
				ProtokollierenInt_P(PSTR("TxP: EmpfA %16d" ), PufferAnzahl(&SendePuffer) + ki);
				ProtokollierenInt_P(PSTR("%4d"), ki);
				ProtokollierenInt_P(PSTR("%4d\r\n"), low(SocketAnzahlZeichenEmpfangen));
				}
				
			} // AsciiDruckPuffer nicht leer und SendePuffer leer
			
		} // Modus aktiv, bei dem gedruckt werden kann.
		
	if (Modus == ModDirektdruckVerbunden)
		{ // am Fernschreiber eigegebene Zeichen nach Ascii umwandeln 
		//! \todo eigentlich nur, wenn es tatsächlich über eine HTML-Seite lief...
		while (!PufferLeer(&EmpfPuffer))
			ZeichenInHtmlSendeText(CodeZuZeichen(PufferAusg(&EmpfPuffer), (char*) &EmpfPuffer.BuZiMode));

		if (TimerVal(&RuheTimer) > 3000 // 30 Sekunden
			&& AsciiDruckPuffer[0] == '\0'
			&& PufferLeer(&SendePuffer)
			&& PufferLeer(&EmpfPuffer) )
			{
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR("TxP: Direktdruck-Ruhe --> Ausschaltung intern\r\n" ));
			BusSenden(BusKdoSchluss);
			ModusWechsel(ModWarteSchlussQuitt);
			StartTimer(&RuheTimer);
			} // Abschaltung nach 30 Sekunden

		} // if Modus == ModDirektdruckVerbunden

	// ==========================================================================
	// Abschaltung nach Reste-Druck?
	// ==========================================================================

	if (Modus == ModPufferDruckUndSchluss && AsciiDruckPuffer[0] == '\0' && PufferLeer(&SendePuffer))
		{ // ID#421 *************************************************************
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Reste gedruckt --> Ausschaltung intern\r\n" ));
		BusSenden(BusKdoSchluss);
		ModusWechsel(ModWarteSchlussQuitt);
		StartTimer(&RuheTimer);
		}

	// ======================================================================
	// Dynamische IP-Aktualisierung starten
	// ======================================================================
	
	if (DynIPAktiv)
		{
		// Aktialisierung starten?
		if ((Modus == ModRuhe || Modus == ModDeaktiviert)
			&& DynIPAktZeitZaehler >= 15L * 60 * TxpTimerFreq // alle 15 Minuten
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
				DynIPAktZeitZaehler = 0; // in 15 Minuten nochmal probieren
				//! \todo Zufallsgesteuert einen anderen Abstand versuchen, da sonst absolute synchronität mit zweitem Teilnehmer möglich.
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
			
			if (ProtokollLevel >= 3) // Daten explizit
				{
				ProtokollierenInt_P(PSTR("TxP: Teilnehmer-Server Empfang: (%d/" ), InCount);
				ProtokollierenInt_P(PSTR("%d)"), Res);
				for (uint16_t i = 0 ; i < Res ; i++)
					ProtokollierenInt_P(PSTR(" %02X"), TSB.Buf[i]);
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
								BusSenden(BusKdoSchluss);
								ModusWechsel(ModWarteSchlussQuitt);
								break;
							}
						}
						
					break; // case TLNSERV_AUSKUNFT_VERSION1
					
				case TLNSERV_IPRUECKMELD:
					if (TSB.IpRueckm.EmpfIP == NetzEigeneIP)
						{ // keine Änderung
						if (ProtokollLevel >= 2)
							Protokollieren_P(PSTR("TxP: Dynamische IP-Aktualisierung: bestehende IP gilt weiter\r\n" ));
						}
					else
						{
						NetzEigeneIP = TSB.IpRueckm.EmpfIP;
						if (ProtokollLevel >= 1)
							{
							Protokollieren_P(PSTR("TxP: Dynamische IP-Aktualisierung: neue IP "));
							ProtokollierenIPAdr(NetzEigeneIP);
							Protokollieren_P(PSTR("\r\n"));
							}
						}
					DynIPAktZeitZaehler = 0; // in 15 Minuten wieder 
					break;

				case TLNSERV_FEHLER:
					Protokollieren_P(PSTR("TxP: Fehlermeldung des Teilnehmer-Servers: "));
					Protokollieren(TSB.PureData);
					Protokollieren_P(PSTR("\r\n"));
					DynIPAktZeitZaehler = 0; // in 15 Minuten wieder 
					break;
				
				default:
					Protokollieren_P(PSTR("TxP: unerwartete Antwort des Teilnehmer-Servers\r\n" ));
					DynIPAktZeitZaehler = 0; // in 15 Minuten wieder 
					break;
				
				} // switch (TSB.Code)
				
			// eine Antwort genügt...
			CloseTCPSocket(TeilnehmerServerSocket);
			TeilnehmerServerSocket = NO_SOCKET_USED;
			}
		
		// Schließanforderung vom Teilnehmer-Server
		if (CheckSocketState(TeilnehmerServerSocket) == SOCKET_NOT_USE)
			{
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR("TxP: Socket zum Teilnehmer-Server wurde von Gegenstelle geschlossen\r\n" ));
			CloseTCPSocket(TeilnehmerServerSocket);
			TeilnehmerServerSocket = NO_SOCKET_USED;
			return;
			}
		
		// Timeout? kommt von selbst nach 20 Sekunden...
		
		}
		
	// ==========================================================================
	// Ab und zu mal den Protokollinhalt speichern
	// ==========================================================================

	ProtokollSpeichern(false);
		
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
	
	struct TIME CurTime;
	CLOCK_GetTime(&CurTime);
	if (CurTime.time <= KonfigFreigabeZeit + 5 * 60)
		{ // 5 Minuten lang ist der Zugang erlaubt
		KonfigFreigabeZeit = CurTime.time;
		return true;
		}
		
	//! \todo Sperre nach Fehlversuchen
	
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;

	if (http_request->argc == 0 || PharseCheckName_P(http_request, Kennwort_P) == 0)
		{ // Ausgabe der Passwort - Eingabeseite
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
			KonfigFreigabeZeit = CurTime.time;
			http_request->argc = 0; // damit die eigentliche Seite nicht durch die Kennwort-Eingabe verwirrt ist!
			return true;
			}
		else
			{
			cgi_PrintHttpheaderStart();
			printf_P(PSTR("Kennwort falsch!"));
			cgi_PrintHttpheaderEnd();
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
	for (uint8_t i = EmpfPuffer.AusgP ; i != EmpfPuffer.SpeichP ; i++)
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
	for (uint8_t i = SendePuffer.AusgP ; i != SendePuffer.SpeichP ; i++)
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
	PRINTVAL(TxpSocketModeAscii);
	
	PRINTVAL(SocketAnzahlZeichenGesendet);
	PRINTVAL(SocketAnzahlZeichenQuittiert);
	PRINTVAL(SocketAnzahlZeichenEmpfangen);
	
	PRINTVAL(TimerVal(&RuheTimer));
	PRINTVAL(TwiLebenszeichenZaehler); 
	PRINTVAL(SocketLebenszeichenZaehler);
	PRINTVAL(TxpThreadCheckCount);

	PRINTVAL(DynIPAktZeitZaehler / TxpTimerFreq);

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
		//! \todo Timeout-Zeitgeber für Verbindungsverlust resetten. Timeout ist 20 Sekunden, da normalerweise diese CGI-Seite alle 10 Sekunden abgerufen wird.

		if (ProtokollLevel >= 3)
			{
			Protokollieren_P(PSTR("TxP: Direktdruck Abruf Druckspiegel:"));
			char *p = HtmlSendeText + strlen(HtmlSendeText) - 20;
			if (p < HtmlSendeText) 
				p = HtmlSendeText;
			Protokollieren(p);
			ProtokollierenInt_P(PSTR(" (%d)\r\n"), strlen(HtmlSendeText));
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

		CgiFormInputFieldLong_P(PSTR("Protokoll-Level:"), ProtokollLevel_P, 2, ProtokollLevel);

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
			printf_P(PSTR("<br>FesteHauptstelle unver&auml;ndert: %d"), Neu);
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
			printf_P(PSTR("<br>AlternativSucheBeiBesetzt unver&auml;ndert: %d"), Neu);
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
		
		// ProtokollLevel
		// --------------
		if (PharseCheckName_P(http_request, ProtokollLevel_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, ProtokollLevel_P)], 2);
			Neu = atoi(Buf);
			if (Neu == ProtokollLevel)
				printf_P(PSTR("<br>ProtokollLevel unver&auml;ndert: %d"), Neu);
			else
				{
				itoa(Neu, Buf, 10); // 10 ist die Basis, nicht die Länge!
				changeConfig_P(ProtokollLevel_P, Buf);
				printf_P(PSTR("<br>Protokoll-Level: %s"), Buf);
				ProtokollLevel = Neu;
				}
			}
			
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
		KonfigFreigabeZeit = 0;
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
	char Buf[TlnAdresseMax + 1];
	uint8_t i;
	
	if (!KonfigFreigabe(pStruct))
		return;
	
	cgi_PrintHttpheaderStart();

	if ( http_request->argc == 0 )
		{
		CgiFormStartTabbed_P(PSTR("txpcfg-extern.cgi"));

		#ifdef TXP_ANSCHLUSS
		
		CgiFormInputFieldLong_P(PSTR("eigene Rufnummer im ip-telex-Netz:"), NetzRufnummer_P, 10, NetzRufnummer);
		
		CgiFormInputFieldLong_P(PSTR("Geheimzahl:"), Geheimzahl_P, 6, Geheimzahl);
		
		CgiFormCheckbox_P(PSTR("IP-Aktualisierung aktiv:"), DynIPAktiv_P, DynIPAktiv);

		CgiFormInputFieldLong_P(PSTR("Port-Nummer im Netz:"), NetzPort_P, 6, NetzPort);
		
		#endif // TXP_ANSCHLUSS
		
		for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
			CgiFormInputFieldText_P(PSTR("Adresse des Teilnehmer-Server:"), RufnrServerAdr_P[i], TlnAdresseMax, TeilnehmerServerAdresse[i]);

		CgiFormFinish_P(PSTR("Einstellung &Uuml;bernehmen"));
		}
	else // argc > 0
		{
		printf_P(PSTR("neue Einstellungen: <a href=\"txpcfg-extern.cgi\">weiter</a>"));

		#ifdef TXP_ANSCHLUSS

		uint32_t Neu;
		
		// Eigene Netz-Rufnummer
		// ---------------------
		if (PharseCheckName_P(http_request, NetzRufnummer_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, NetzRufnummer_P)], 10);
			Buf[10] = '\0';
			Neu = atol(Buf);
			if (Neu == NetzRufnummer)
				printf_P(PSTR("<br>Netz-Rufnummer unver&auml;ndert: %s"), Buf);
			else
				{
				printf_P(PSTR("<br>Netz-Rufnummer ge&auml;ndert in: %s"), Buf);
				changeConfig_P(NetzRufnummer_P, Buf);
				NetzRufnummer = Neu;
				}
			}
		
		// Prüfzahl zur eigene Netz-Rufnummer
		// ----------------------------------
		if (PharseCheckName_P(http_request, Geheimzahl_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, Geheimzahl_P)], 10);
			Buf[10] = '\0';
			Neu = atol(Buf);
			if (Neu == Geheimzahl)
				printf_P(PSTR("<br>Geheimzahl unver&auml;ndert: %s"), Buf);
			else
				{
				printf_P(PSTR("<br>Geheimzahl ge&auml;ndert in: %s"), Buf);
				changeConfig_P(Geheimzahl_P, Buf);
				Geheimzahl = Neu;
				}
			}
			
		// Dynamische IP-Aktualisierung
		// ---------------------------
		if (PharseCheckName_P(http_request, DynIPAktiv_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, DynIPAktiv_P)], 2);
			Buf[2] = '\0';
			Neu = atoi(Buf) != 0; 
			}
		else
			{
			Neu = false;
			Buf[0] = '0', Buf[1] = '\0';
			}
		if (Neu == DynIPAktiv)
			printf_P(PSTR("<br>DynIPAktualisierung unver&auml;ndert: %d"), Neu);
		else
			{
			changeConfig_P(DynIPAktiv_P, Buf);
			printf_P(PSTR("<br>DynIPAktualisierung: %s"), Buf);
			DynIPAktiv = Neu;
			}
		
		// Netz-Port
		// ---------------------
		if (PharseCheckName_P(http_request, NetzPort_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, NetzPort_P)], 10);
			Buf[10] = '\0';
			Neu = atol(Buf);
			if (Neu == NetzPort)
				printf_P(PSTR("<br>Netz-Port unver&auml;ndert: %s"), Buf);
			else
				{
				printf_P(PSTR("<br>Netz-Port ge&auml;ndert in: %s"), Buf);
				changeConfig_P(NetzPort_P, Buf);
				NetzPort = Neu;
				}
			}
		
		#endif // TXP_ANSCHLUSS
		
		// URLs der Teilnehmer-Server
		// --------------------------
		for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
			{
			if (PharseCheckName_P(http_request, RufnrServerAdr_P[i]))
				{
				strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, RufnrServerAdr_P[i])], TlnAdresseMax);
				Buf[TlnAdresseMax-1] = '\0';
				if (strcmp(Buf, TeilnehmerServerAdresse[i]) == 0)
					printf_P(PSTR("<br>Teilnehmer-Server #%d unver&auml;ndert: %s"), i+1, Buf);
				else
					{
					printf_P(PSTR("<br>Teilnehmer-Server #%d ge&auml;ndert in: %s"), i+1, Buf);
					changeConfig_P(RufnrServerAdr_P[i], Buf);
					strcpy(TeilnehmerServerAdresse[i], Buf);
					}
				} // if PharseCheckName_P()
			} // for i
			
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
	uint8_t i;

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
	KonfigFreigabeZeit = 0; // ist zwar 1970, sollte aber nicht das Problem sein...
		
	// dies müsste eigentlich in Protokoll.c enthalten sein.
	if (readConfig_P(ProtokollLevel_P, Buf) == 1)
		ProtokollLevel = atoi(Buf);
	else
		ProtokollLevel = 1;
		
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

	DynIPAktZeitZaehler = 14L * 60 * TxpTimerFreq; 
		// 14 Minuten sind schon abgelaufen, daher Aktualisierung in einer Minute

	Status = (1 << StatBit_Frei) | (1 << StatBit_LeitungKennung);

	timer0_init(TxpTimerFreq); 
	if (!timer0_RegisterCallbackFunction(txp_timerEvent))
		return;
	
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

	printf_P( PSTR("TelexPhone Port %d.\r\n") , TXP_PORT );

	THREAD_RegisterThread( txp_thread, PSTR("TxP"));

	#endif // TXP_ANSCHLUSS
	
	TlnBuchInit();
	
	#ifdef TXP_TLNSERVER
	
	txp_tlnserv_init();
	
	#endif // TXP_TLNSERVER

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

