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


// Aktueller Modus

typedef enum
	{
	ModRuhe = 0, 
		//!< nichts läuft
	// Gehend = vom internen Anschluss zum Netz, Reservierung ist eingegangen
	ModGehendReserv = 1, 
	ModGehendWaehlen = 2,
		// nicht benötigt ModGehendVerbindungHerstellen = 3, 
			// Einschaltkommando ist angekommen, Warte auf Quittung vom Anrufer
	ModGehendVerbunden = 4,
	
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
	
	// Über HTML-Seite eingegebener Verbindungswunsch
	ModHtmlWarteEinQuitt = 21, //!< Warte auf Einschalt-Quittung des Endgeräts
	ModHtmlVerbunden = 22, 
	
	ModDeaktiviert = 31 //!< Durch Tastendruck ausgeschaltet.
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
#define TXPC_DURCHWAHL '\001' //!< Datenblock enthält ein Byte Durchwahl 
#define TXPC_BAUDOT_DATA '\002' //!< Datenblock mit puren Baudot-Codes
#define TXPC_STOP '\004' //!< Es können noch Daten angehängt werden.
// \005 freigehalten für ^E = WerDa.
#define TXPC_QUITT '\006' //!< Meldet Empfangsbereitschaft und Anzahl bereits verarbeiteter Zeichen.
	
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

volatile static uint16_t TwiLebenszeichenZaehler; 
	//!< Zählt rückwärts die Takte bis zum nächsten Lebenszeichen auf dem TWI-Bus.
	//!< wird während der Verbindung missbraucht zum Zählen der Takte bis zur Pegelwiederholung.
	
volatile static uint16_t RuheZaehler;
	//!< Zählt Ticks in denen nix passiert. Wird bei Datenempfang und Sendung und 
	//!< Verbindungsaufbau auf Null gesetzt. Wird auch für Timeout beim Warten auf 
	//!< die Ausschalt-Quittung benutzt. In Grundstellung wird die Datuer der Grundstellung
	//!< gemessen für das Protokollschreiben.
	
volatile static uint16_t SocketLebenszeichenZaehler;
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


static int TxpServerSocket;
	//!< Handle für eingehende Txp-Verbindungen ("Server")
	
static int TxpClientSocket;
	//!< Handle für ausgehende Txp-Verbindungen ("Client")

	
enum { SocketInBufMax = 2500 } ; //!< Größe des TCP-Empfangspuffers

static uint16_t SocketInBufUsed; //!< Benutzter Teil des TCP-Empfangspuffers

static char SocketInBuf[SocketInBufMax]; //!< TCP-Empfangspuffer

	
enum { SocketOutBufMax = 2500 } ; //!< Größe des TCP-Sendepuffers

static uint16_t SocketOutBufUsed; //!< Benutzter Teil des TCP-Sendepuffers

static char SocketOutBuf[SocketOutBufMax]; //!< TCP-Sendepuffer

static uint8_t SocketAnzahlZeichenGesendet;
	//!< Anzahl Baudot- oder Ascii-Codes, die bisher an die Gegenstelle gesendet worden sind.
	
static uint8_t SocketAnzahlZeichenEmpfangen;
	//!< Anzahl Baudot- oder Ascii-Codes, die bisher von der Gegenstelle empfangen worden sind.
	
static uint8_t SocketAnzahlZeichenQuittiert;
	//!< Anzahl Baudot- oder Ascii-Codes, die bisher von der Gegenstelle verarbeitet worden sind.
	
volatile static bool SocketSendeQuittung;
	//!< Wenn true, werden die Anzahl der bisher gedruckten Codes zurückgemeldet.

static uint8_t SocketSendeFehlerZaehler;
	//!< Zählt bis 10 bei nicht erfolgreichen Sendeversuchen auf dem Socket.
	
static bool SocketModeAscii; 
	//!< true, wenn die Daten als ASCII und nicht als Baudot-Daten übertragen werden.

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

static uint8_t ProtokollLevel;
	//!< "Tiefe" der Protokollierung: 0 = Aus, 1 = Normal, 2 = Intensiv

//! Sollfrequenz des Aufrufs von txp_timerEvent()
enum { TxpTimerFreq = 50 * 10 } ; // 50 Baud mit 10 Takten je Bit	


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
	
	wdt_reset();
	
	SocketLebenszeichenZaehler++;
		
	if (TxpThreadCheckCount++ > 30 * TxpTimerFreq) // nach 30 Sekunden Reset
		{
		Protokollieren("TxP: Reset wegen nicht-Aufruf von txp_thread()\r\n");
		ProtokollSpeichern(true);
		softreset();
		}
		
#if defined(LEDROT_TXPTHREADBLOCK)
	if (TxpThreadCheckCount > TxpTimerFreq / 2) // nach halber Sekunde geht rot an
		LED_on(ROT);
#endif //defined(LEDROT_TXPTHREADBLOCK)
		
	TwiWatchdogCount++;
		
	RuheZaehler++; // wird aber vielleicht gleich wieder auf Null gestellt
	
	if (Modus == ModKommendVerbunden || Modus == ModGehendVerbunden || Modus == ModHtmlVerbunden || Modus == ModPufferDruckUndSchluss)
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
			RuheZaehler = 0;
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
				RuheZaehler = 0;
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
			RuheZaehler = 0;
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
		} // if IstVerbunden

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
	static uint8_t TasteZaehler;

	switch (TasteZustandIntern)
		{
		case TasteAus:
			if (!get_Taste()) // Gedrückt = LOW!
				{
				if (++TasteZaehler > TxpTimerFreq * 5/100) // 50 Millisekunden
					{ // ausreichend lang gedrückt
					TasteZustandIntern = TasteEin;
					TasteZaehler = 0;
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
			RuheZaehler = 0;
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
			SocketModeAscii = false;
			AsciiDruckPuffer[0] = '\0';
			SocketAnzahlZeichenEmpfangen = 0;
			SocketAnzahlZeichenGesendet = 0;
			SocketAnzahlZeichenQuittiert = 0;
			SocketSendeFehlerZaehler = 0;
			break;
	
		case ModGehendWaehlen:
			Wahlnummer = 0;
			Wahlziffern = 0;
			// derzeit keine Änderung erforderlich
			break;
	
/* nicht benötigt
		case ModGehendVerbindungHerstellen: // Einschaltkommando ist angekommen, Warte auf Quittung vom Anrufer
			BusEmpfMark = true;
			SendeMark = true;
			break;
*/
	
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
			SocketModeAscii = false;
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
		
		case ModKommendWarteEinQuitt: //!< Warte auf Einschalt-Quittung des Endgeräts
			SET_BIT_Status(StatBit_FsBefBetrieb);
			SET_BIT_Status(StatBit_FsBefEin);
			RuheZaehler = 0;
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
			RuheZaehler = 0;
			break;
		
		case ModWarteSchlussQuitt:
			CLR_BIT_Status(StatBit_FsBefBetrieb);
			CLR_BIT_Status(StatBit_FsBefEin);
			CLR_BIT_Status(StatBit_Verbunden);
			RuheZaehler = 0;
			break;

		// Html = Auf HTML-Seite eingegebener Text
		case ModHtmlWarteEinQuitt: //!< Warte auf Einschalt-Quittung des Endgeräts
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
	
		case ModHtmlVerbunden: 
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
	}
	
	
//! Kommende TCP-Verbindung schließen
static void CloseTxpServerSocket()
	{
	if (TxpServerSocket != NO_SOCKET_USED)
		{ 
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Server-Socket (eingehend) wird geschlossen\r\n" ));
		CloseTCPSocket(TxpServerSocket);
		TxpServerSocket = NO_SOCKET_USED;
		}
	}


//! Gehende TCP-Verbindung schließen
static void CloseTxpClientSocket()
	{
	if (TxpClientSocket != NO_SOCKET_USED)
		{ 
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Client-Socket (ausgehend) wird geschlossen\r\n" ));
		CloseTCPSocket(TxpClientSocket);
		TxpClientSocket = NO_SOCKET_USED;
		}
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
//! \par - Empfangene Daten in den Socket-Empfangspuffer schreiben
//! \par - Daten vom Socket-Empfangspuffer übersetzen in den SendePuffer (zum Endgerät).
//! \par - Daten des Empfangspuffers (Endgerät) in den Socket-Sendepuffer übersetzen
//! \par - Daten des Socket-Sendepuffers ggf. senden
//! \par - Schlusszeichen bearbeiten

static void SocketBearbeiten(int *Socket, bool IstVerbunden)
	{
	if (*Socket == NO_SOCKET_USED)
		return;
		
	// Auf neue Daten testen
	// ---------------------------------
	int InCount = GetBytesInSocketData(*Socket);
	
	if (SocketInBufUsed + InCount > SocketInBufMax)
		InCount = SocketInBufMax - SocketInBufUsed;
		
	if (InCount > 0) 
		{
		int Res = GetSocketData(*Socket, InCount, SocketInBuf + SocketInBufUsed);
		
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
				SocketModeAscii = true;
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
				}
				
			else if (c == TXPC_BAUDOT_DATA)
				{ // ID#243 ID#343 ***********************************************************
				SocketModeAscii = false;
				uint8_t len = SocketInBuf[i+1];
				
				if (i + 2 + len <= SocketInBufUsed && PufferAnzahl(&SendePuffer) + len < MaxPuffer)
					{ // Baudot-Code-Block ist vollständig UND noch entsprechend Platz im Sendepuffer
					if (ProtokollLevel == 2) // Datenmengen protokollieren
						{
						ProtokollierenInt_P(PSTR("TxP: EmpfB %16d" ), PufferAnzahl(&SendePuffer));
						ProtokollierenInt_P(PSTR("%4d"), len);
						ProtokollierenInt_P(PSTR("%4d\r\n"), SocketAnzahlZeichenEmpfangen + len);
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
					}
				else
					{
					if (!SendenBeschleunigen && (ProtokollLevel >= 2))
						Protokollieren_P(PSTR("TxP: SendenBeschleunigen EIN\r\n"));
					SendenBeschleunigen = true;
					break; // Daten können momentan nicht verarbeitet werden.
					}
				}

			else if (c == TXPC_STOP)
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

				if (IstVerbunden)
					ModusWechsel(ModPufferDruckUndSchluss);
				else
					ModusWechsel(ModRuhe);
				
				} // c == TXPC_STOP
				
			else if (c == TXPC_QUITT)
				{ 
				uint8_t len = SocketInBuf[i+1];
				if (Modus == ModKommendVerbVorstufe)
					{ // ID#312 **************************************************
					BusSenden(BusKdoEin);
					ModusWechsel(ModGehendVerbunden);
					}
				else if (Modus == ModGehendWaehlen)
					{ // ID#227 **************************************************
					BusSenden(BusQuittEin);
					ModusWechsel(ModGehendVerbunden);
					}
				if (len >= 1)
					SocketAnzahlZeichenQuittiert = (uint8_t) SocketInBuf[i+2];
				i += 2 + len;
				}
				
			else 
				{ // unbekannter Code --> ignorieren EINSCHLIEßLICH Daten
				// ID#245 ID#313 ID#346 ********************************************************
				i += 2 + (uint8_t) SocketInBuf[i+1];
				}
				
			}
			
		// verarbeiteten Teil des Empfangspuffers löschen
		if (i < SocketInBufUsed)
			{
			memmove(SocketInBuf, SocketInBuf + i, SocketInBufUsed - i);
			SocketInBufUsed -= i;
			}
		else
			SocketInBufUsed = 0;
			
		} // if GetBytesInSocketData > 0

	// soll offene Verbindung geschlossen werden?
	if (CheckSocketState(*Socket) == SOCKET_NOT_USE)
		{ // ID#242 ID#342 ID#314 ************************************************
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Socket wurde von Gegenstelle geschlossen\r\n" ));
		CloseTCPSocket(*Socket);
		*Socket = NO_SOCKET_USED;
		if (IstVerbunden)
			ModusWechsel(ModPufferDruckUndSchluss);
		else
			ModusWechsel(ModRuhe); // ID#314
		return;
		}
		
	// vom Endgerät empfangene Daten übersetzen
	// --------------------------------------------------
	// es wird gesendet, wenn es was zu senden gibt 
	// UND      a) es viel zu senden gibt 
	//     ODER b) 0,5 sekunden nicht getippt wurde
	//     ODER c) Alles was bisher gesendet wurde schon verarbeitet ist.
	InCount = PufferAnzahl(&EmpfPuffer);
	if (InCount > 20
	    || (InCount > 0 && ((RuheZaehler >= TxpTimerFreq * 8/10) // 0,8 Sekunden Tipp-Pause
		                    || (SocketAnzahlZeichenQuittiert == SocketAnzahlZeichenGesendet) // alles was gesendet wurde, ist schon verarbeitet
						    )
			)
		)
		{ // ID#244 ID#344 ***************************************************************
		if (SocketModeAscii)
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
				ProtokollierenInt_P(PSTR("TxP: SendA %4d" ), (uint8_t)(SocketAnzahlZeichenGesendet - SocketAnzahlZeichenQuittiert));
				ProtokollierenInt_P(PSTR("%4d"), ProtAnz);
				ProtokollierenInt_P(PSTR("%4d\r\n"), SocketAnzahlZeichenGesendet); // wurde schon erhöht
				}
				
			} // if SocketModeAscii
		else
			{ // Baudot-Datenblock senden
			uint8_t len = PufferAnzahl(&EmpfPuffer);
			if (len > SocketOutBufMax - 3 - SocketOutBufUsed)
				len = SocketOutBufMax - 3 - SocketOutBufUsed;
				
			if (ProtokollLevel == 2) // Datenmengen
				{
				ProtokollierenInt_P(PSTR("TxP: SendB %4d" ), (uint8_t)(SocketAnzahlZeichenGesendet - SocketAnzahlZeichenQuittiert));
				ProtokollierenInt_P(PSTR("%4d"), len);
				ProtokollierenInt_P(PSTR("%4d\r\n"), SocketAnzahlZeichenGesendet + len);
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
			} // else !SocketModeAscii
		}
		
	// ggf. Anzahl verarbeiteter Zeichen zurückmelden
	if (!SocketModeAscii 
		&& (Modus == ModKommendVerbunden || Modus == ModGehendVerbunden)
		&& (SocketSendeQuittung || SocketLebenszeichenZaehler > 4 * TxpTimerFreq))
		{
		SocketOutBuf[SocketOutBufUsed] = TXPC_QUITT;
		SocketOutBufUsed++;
		SocketOutBuf[SocketOutBufUsed] = 1;
		SocketOutBufUsed++;
		SocketOutBuf[SocketOutBufUsed] = 
			(uint8_t) (SocketAnzahlZeichenEmpfangen - PufferAnzahl(&SendePuffer));
		SocketOutBufUsed++;
		SocketSendeQuittung = false;
		}
		
	// ggf Lebenszeichen erzeugen
	// --------------------------
	if (SocketLebenszeichenZaehler > 4 * TxpTimerFreq && SocketOutBufUsed == 0)
		{ // alle 4 Sekunden ein Lebenszeichen
		SocketOutBuf[0] = TXPC_NULL;
		SocketOutBuf[1] = 0;
		SocketOutBufUsed = 2;
		}
		
	// Daten ggf. ins Netz senden
	// --------------------------------------------------
	if (SocketOutBufUsed > 0) //! \todo && !HaltSocketOut
		{
		int Res = PutSocketData_RPE(*Socket, SocketOutBufUsed, SocketOutBuf, RAM);
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
			{
			SocketSendeFehlerZaehler++;
			if (SocketSendeFehlerZaehler >= 10)
				{
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: Mehrfache Fehler beim Senden ins Netz, Socket wird geschlossen\r\n" ));
				CloseTCPSocket(*Socket);
				*Socket = NO_SOCKET_USED;
				if (IstVerbunden)
					ModusWechsel(ModPufferDruckUndSchluss);
				else
					ModusWechsel(ModRuhe); 
				}
			}
		else if (Res < SocketOutBufUsed)
			{
			memmove(SocketOutBuf, SocketOutBuf + Res, SocketOutBufUsed - Res);
			SocketOutBufUsed -= Res;
			SocketSendeFehlerZaehler = 0;
			}
		else
			{
			SocketOutBufUsed = 0;
			SocketSendeFehlerZaehler = 0;
			}
		} // if es gibt was zu senden
	
	} // SocketBearbeiten()

	
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

				else if (Modus == ModHtmlWarteEinQuitt)
					{ 
					if (ProtokollLevel >= 1)
						Protokollieren_P(PSTR("TxP: TWI Einschaltquittung durch HTML-Fenster\r\n" ));					
					ModusWechsel(ModHtmlVerbunden);
					}
				
				else
					FalschCodeEmpfangen(BusQuittEin);
					
				break;

			case BusKdoWahlFreigabe:
				// \todo Bei Relaisbetrieb... dies ist eine Leitungsschnittstelle, die kann nicht wählen.
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: TWI Wahlaufforderung intern / kommend\r\n" ));
				FalschCodeEmpfangen(BusQuittEin);
				break;
				
			case BusKdoWahlziffer0 ... BusKdoWahlziffer9:
				if (ProtokollLevel >= 2)
					ProtokollierenInt_P(PSTR("TxP: TWI Wahlziffer %d intern / gehend\r\n" ), Code - BusKdoWahlziffer0);
				if (Modus == ModGehendWaehlen && TxpClientSocket == NO_SOCKET_USED)
					{
					TTlnDaten TD;
					
					// ID#221 ********************************************
					Wahlnummer = 10 * Wahlnummer + (Code - BusKdoWahlziffer0);
					Wahlziffern++;
					
					if (TlnSuche(Wahlnummer, false, &TD))
						{ // ID#222 ********************************************
						switch (TD.AdrArt)
							{
							case TxpIP:
							case AsciiIP:
								if (ProtokollLevel >= 1)
									{
									ProtokollierenInt_P(PSTR("TxP: Teilnehmer %ld gefunden: "), TD.Nummer);
									ProtokollierenIPAdr(TD.IPAdr);
									Protokollieren_P(PSTR("\r\n"));
									}
								TxpClientSocket = Connect2IP(TD.IPAdr, TD.Port);
								break;
								
							case TxpUrl:
							case AsciiUrl:
								TD.IPAdr = DNS_ResolveName(TD.Adresse); 
									// TP.IPAdr wird 'missbraucht' aber nicht gespeichert
								if ( TD.IPAdr != -1 )
									{
									if (ProtokollLevel >= 1)
										{
										ProtokollierenInt_P(PSTR("TxP: Teilnehmer %ld gefunden: "), TD.Nummer);
										Protokollieren(TD.Adresse);
										Protokollieren_P(PSTR(" = "));
										ProtokollierenIPAdr(TD.IPAdr);
										Protokollieren_P(PSTR("\r\n"));
										}									TxpClientSocket = Connect2IP(TD.IPAdr, TD.Port);
									}
								else
									{
									if (ProtokollLevel >= 1)
										{
										ProtokollierenInt_P(PSTR("TxP: Teilnehmer %ld gefunden, keine IP zu "), TD.Nummer);
										Protokollieren(TD.Adresse);
										Protokollieren_P(PSTR(" gefunden\r\n"));
										}
									TxpClientSocket = -1;
									}
								break;
								
							default:
								if (ProtokollLevel >= 1)
									ProtokollierenInt_P(PSTR("TxP: Teilnehmer %ld gefunden: GELOESCHT\r\n" ), TD.Nummer);
								TxpClientSocket = -1;
							}
						
						if (TxpClientSocket == -1 )
							{ // ID#223 ********************************************
							// Verbindung konnte nicht aufgebaut werden
							BusSenden(BusKdoSchluss);
							if (ProtokollLevel >= 1)
								Protokollieren_P(PSTR("TxP: Client-Socket konnte nicht geoeffnet werden\r\n"));
							TxpClientSocket = NO_SOCKET_USED;
							ModusWechsel(ModWarteSchlussQuitt);
							}
						else if (TD.AdrArt == AsciiUrl || TD.AdrArt == AsciiIP)
							{ // ID#226 *********************************************
							BusSenden(BusQuittEin);
							if (ProtokollLevel >= 1)
								Protokollieren_P(PSTR("TxP: Client-Socket Ascii erfolgreich geoeffnet -> Einschalt-Quittung an TWI\r\n" ));
							ModusWechsel(ModGehendVerbunden);
							SocketBufInit();
							SocketModeAscii = true;
							}
						else // TxpUrl oder TxpIP
							{ // ID#222 ********************************************
							if (ProtokollLevel >= 1)
								ProtokollierenInt_P(PSTR("TxP: Client-Socket Txp erfolgreich geoeffnet -> sende Durchwahl %d\r\n"), TD.Durchwahl);

							SocketBufInit();
							SocketModeAscii = false;
							
							SocketOutBuf[0] = TXPC_DURCHWAHL;
							SocketOutBuf[1] = 1;
							SocketOutBuf[2] = TD.Durchwahl;
							SocketOutBufUsed = 3;
							}
						} // gewählte Nummer war vollständig
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
					
				// ausgehende Ports schließen...
				// ID#241 ********************************************			
				CloseTxpClientSocket();
				
				// eingehende Ports schließen...
				// ID#341 ********************************************
				CloseTxpServerSocket();
				
				// Html-Puffer löschen
				AsciiDruckPuffer[0] = '\0'; // damit es keine neue Einschaltung gibt.
					
				// ID#411 ID#104 *************************************
				ModusWechsel(ModRuhe);
				break;

			default:
				FalschCodeEmpfangen(Code);
				break;
				
			} // switch Code
		} // if GetEmpfByte

	// ======================================================================
	// Prüfen, ob TWI-Kommunikation überhaupt noch läuft
	// ======================================================================

	if (Modus == ModKommendWarteEinQuitt || Modus == ModKommendVerbunden 
	    || Modus == ModGehendReserv || Modus == ModGehendWaehlen || Modus == ModGehendVerbunden 
		|| Modus == ModHtmlVerbunden || Modus == ModPufferDruckUndSchluss)
		{
		if (TwiWatchdogCount > 4 * TxpTimerFreq) // nach 4 Sekunden ohne TWI-Kommunikation
			{
			if (ProtokollLevel >= 1)
				Protokollieren("TxP: TWI-Timeout -> Abschaltung\r\n");
			BusSenden(BusKdoSchluss);
			ModusWechsel(ModWarteSchlussQuitt);
			}
		}
	
	// ======================================================================
	// Socket Empfang und Sendung
	// ======================================================================

	if (Modus == ModKommendVerbunden)
		SocketBearbeiten(&TxpServerSocket, true);
		
	if (Modus == ModGehendVerbunden || Modus == ModGehendWaehlen)
		{
		SocketBearbeiten(&TxpClientSocket, true);
		if (Modus == ModGehendWaehlen && !PufferLeer(&SendePuffer))
			{ // es wurden Daten empfangen, also schnellstens Endgerät anschmeißen
			// ID#227 Teil 2 *******************************************************
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR("TxP: Angerufener hat geantwortet -> Einschaltung intern\r\n" ));
			BusSenden(BusQuittEin);
			ModusWechsel(ModGehendVerbunden);
			}
		}

	if (Modus == ModKommendVerbVorstufe || Modus == ModKommendEinschalten)
		{
		SocketBearbeiten(&TxpServerSocket, false);
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
				CloseTxpServerSocket(); // Client kann nicht geöffnet sein.
				ModusWechsel(ModRuhe);
				}
			} // if Modus == ModKommendEinschalten
		}
			
	// ==========================================================================
	// Neuer Anruf vom Ethernet?
	// ==========================================================================

	// auf neue Verbindungsanfrage testen
	int NewServerSocket = CheckPortRequest(TXP_PORT);
	if (NewServerSocket != NO_SOCKET_USED)
		{
		extern struct TCP_SOCKET TCP_sockettable[];

		if (ProtokollLevel >= 1)
			{
			Protokollieren_P(PSTR("TxP: Server-Socket geoeffnet von IP "));
			ProtokollierenIPAdr(TCP_sockettable[NewServerSocket].SourceIP);
			Protokollieren_P(PSTR(" / MAC "));
			ProtokollierenMAC(TCP_sockettable[NewServerSocket].MACadress);
			}
		
		if (Modus == ModRuhe && TxpServerSocket == NO_SOCKET_USED)
			{ // ID#102 *************************************************
			// Wenn ja, Startmeldung ausgeben und startzustand herstellen für i2c
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR(" ...ok\r\n"));
			TxpServerSocket = NewServerSocket;
			BusVerbPartner = Hauptstelle;
			ModusWechsel(ModKommendVerbVorstufe);
			SocketBufInit();
			}
		else
			{ // ID#213 ID#225 ***************************************************
			PutSocketData_RPE(NewServerSocket, 7, PSTR("\004\005occ\r\n"), FLASH); // 004 = TXPC_STOP
			CloseTCPSocket(NewServerSocket);
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR(" ...ABGEWIESEN\r\n" ));
			}
		}

	// ==========================================================================
	// Timeouts?
	// ==========================================================================

	if (Modus == ModWarteSchlussQuitt && RuheZaehler > 3 * TxpTimerFreq)
		{ // 3 Sekunden keine Schlussquittung empfangen
		// ID#412 ****************************************************************
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Timeout beim Warten auf die Schlussquittung\r\n" ));
		ModusWechsel(ModRuhe);
		}
		
	if (Modus == ModKommendWarteEinQuitt && RuheZaehler > 3 * TxpTimerFreq)
		{ // 3 Sekunden keine Einschalt-Quittung empfangen
		// ID#332 ***************************************************************
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Timeout beim Warten auf die Einschaltquittung\r\n" ));
		BusSenden(BusKdoSchluss);
		CloseTxpServerSocket();
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
				// ID#103 ********************************************************
				ModusWechsel(ModDeaktiviert);
				break;
				
			case ModDeaktiviert:
				// ID#511 ********************************************************
				ModusWechsel(ModRuhe);
				break;

			case ModPufferDruckUndSchluss:
				// ID#422 *************************************************************
				if (ProtokollLevel >= 1)
					Protokollieren_P(PSTR("TxP: Taste gedruckt --> Ausschaltung intern\r\n" ));
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
				Protokollieren_P(PSTR("TxP: HTML-Eingabe -> Einschaltung intern\r\n" ));
			BusSenden(BusKdoEin);
			ModusWechsel(ModHtmlWarteEinQuitt);
			}
		else
			{ 
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR("TxP: HTML-Eingabe -> Einschaltung intern VERSAGT\r\n" ));
			strcpy_P(DebugMsg, PSTR("Reservierung für Einschaltung konnte nicht versand werden"));
			AsciiDruckPuffer[0] = '\0'; // damit es keine neue Einschaltung gibt.
			ModusWechsel(ModRuhe);
			}
		} // if ModRuhe && Text per HTML empfangen
		
	if (Modus == ModHtmlVerbunden || Modus == ModKommendVerbunden || Modus == ModGehendVerbunden || Modus == ModPufferDruckUndSchluss)
		{
		// zu druckenden Text umwandeln
		// ---------------------------
		//! \todo mehr als ein Zeichen auf ein mal umkopieren
		if (AsciiDruckPuffer[0] != '\0' && PufferLeer(&SendePuffer))
			{
			if (ProtokollLevel == 2) // Datenmengen protokollieren
				{
				ProtokollierenInt_P(PSTR("TxP: EmpfA %16d" ), PufferAnzahl(&SendePuffer));
				ProtokollierenInt_P(PSTR("%4d"), 1);
				ProtokollierenInt_P(PSTR("%4d\r\n"), SocketAnzahlZeichenEmpfangen + 1);
				}
				
			if (SchreibeZeichenInSendePuffer(AsciiDruckPuffer[0]))
				{ // nur im Echo darstellen, wenn es auch gedruckt wurde.
				if (Modus == ModHtmlVerbunden)
					ZeichenInHtmlSendeText(AsciiDruckPuffer[0]);
				}
				
			SocketAnzahlZeichenEmpfangen++;
				
			memmove(AsciiDruckPuffer, AsciiDruckPuffer + 1, strlen(AsciiDruckPuffer)); 
				// erstes Zeichen aus AsciiDruckPuffer-Puffer löschen
				// Länge: +1 für das NUL-Zeichen am Ende, -1 weil das erste Zeichen 'rausfliegt
				
			} // AsciiDruckPuffer nicht leer und SendePuffer leer
			
		} // Modus aktiv, bei dem gedruckt werden kann.
		
	if (Modus == ModHtmlVerbunden)
		{ // eigegebene Zeichen nach Ascii umwandeln
		while (!PufferLeer(&EmpfPuffer))
			ZeichenInHtmlSendeText(CodeZuZeichen(PufferAusg(&EmpfPuffer), (char*) &EmpfPuffer.BuZiMode));

		if (RuheZaehler > 30 * TxpTimerFreq // 30 Sekunden
			&& AsciiDruckPuffer[0] == '\0'
			&& PufferLeer(&SendePuffer)
			&& PufferLeer(&EmpfPuffer) )
			{
			if (ProtokollLevel >= 1)
				Protokollieren_P(PSTR("TxP: HTML-Ruhe --> Ausschaltung intern\r\n" ));
			BusSenden(BusKdoSchluss);
			ModusWechsel(ModWarteSchlussQuitt);
			RuheZaehler = 0;
			} // Abschaltung nach 30 Sekunden

		} // if Modus == ModHtmlVerbunden

	// ==========================================================================
	// Abschaltung nach Reste-Druck?
	// ==========================================================================

	if (Modus == ModPufferDruckUndSchluss && AsciiDruckPuffer[0] == '\0' && PufferLeer(&SendePuffer))
		{ // ID#421 *************************************************************
		if (ProtokollLevel >= 1)
			Protokollieren_P(PSTR("TxP: Reste gedruckt --> Ausschaltung intern\r\n" ));
		BusSenden(BusKdoSchluss);
		ModusWechsel(ModWarteSchlussQuitt);
		RuheZaehler = 0;
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

	uint8_t i;

	cgi_PrintHttpheaderStart();

	printf_P(PSTR("DebugMsg: %s<br>"), DebugMsg);
	DebugMsg[0] = '\0';

	extern char Dateiname[]; // aus Protokoll.c
	printf_P(PSTR("Protokolldatei: %s<br>"), Dateiname);
	extern char Puffer[]; // aus Protokoll.c
	printf_P(PSTR("Protokollpuffer: %s<br>"), Puffer);

#define PRINTVAL(Var) printf_P(PSTR("<br>" #Var " = %d"), Var)

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
	for (i = EmpfPuffer.AusgP ; i != EmpfPuffer.SpeichP ; i++)
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
	for (i = SendePuffer.AusgP ; i != SendePuffer.SpeichP ; i++)
		{
		if (i >= MaxPuffer) 
			i = 0;
		printf_P(PSTR(" %02X"), SendePuffer.Puffer[i]);
		}
	
	PRINTVAL(SocketInBufUsed);
	PRINTVAL(SocketOutBufUsed);
	PRINTVAL(SocketModeAscii);
	
	PRINTVAL(SocketAnzahlZeichenGesendet);
	PRINTVAL(SocketAnzahlZeichenQuittiert);
	PRINTVAL(SocketAnzahlZeichenEmpfangen);
	
	PRINTVAL(RuheZaehler);
	PRINTVAL(TwiLebenszeichenZaehler); 
	PRINTVAL(SocketLebenszeichenZaehler);
	PRINTVAL(TxpThreadCheckCount);

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
	printf_P(PSTR("]<br>Ethernet: %ld Bytes in %ld Packeten LockErrors %ld\r\n") , ByteCounter, PacketCounter, eth_state_error );

	cgi_PrintHttpheaderEnd();

	ProtokollSpeichern(true);

	}
	

	
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
					
	if (Modus == ModHtmlVerbunden)
		{
		printf_P(PSTR("Druckspiegel:<br><pre>%s&lt;&lt;&lt;%s</pre>"), HtmlSendeText, AsciiDruckPuffer);
		}
	else if (Modus == ModRuhe)
		{
		printf_P(PSTR("Texteingabe startet Fernschreiber"));
		HtmlSendeText[0] = '\0';
		}
	else
		{
		printf_P(PSTR("Interface ist belegt, bitte warten."));
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
		&& (Modus == ModRuhe || Modus == ModHtmlWarteEinQuitt || Modus == ModHtmlVerbunden))
		{
		strncat(AsciiDruckPuffer, http_request->argvalue[PharseGetValue_P(http_request, Eingabe_P)], AsciiDruckPufferMax - strlen(AsciiDruckPuffer) - 3);
		AsciiDruckPuffer[AsciiDruckPufferMax-3] = '\0';
		strcat_P(AsciiDruckPuffer, PSTR("\r\n"));
		if (ProtokollLevel >= 2)
			{
			Protokollieren_P(PSTR("TxP: CGI-Druck "));
			Protokollieren(AsciiDruckPuffer); // CRLF steht im Druckpuffer
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
	
	
const PROGMEM char Hauptstelle_P[] = "HAUPTSTELLE";
const PROGMEM char EigeneNummer_P[] = "EIGENENUMMER";
const PROGMEM char FesteHst_P[] = "FESTEHPST";
const PROGMEM char AlternBeiBes_P[] = "ALTERNBEIBES";
const PROGMEM char DurchwahlTabelle_P[] = "DURCHWAHLTAB";
const PROGMEM char ProtokollLevel_P[] = "PROTLEVEL";


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

	
/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface zum Ändern der Einstellungen des TelexPhone-Interface 
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/
 
void txp_cgi_config(void *pStruct)
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	char Buf[35];
	
	cgi_PrintHttpheaderStart();

	if ( http_request->argc == 0 )
		{
		CgiFormStartTabbed_P(PSTR("txp-config.cgi"));

		AdresseZuWahlStr(BusEigenAdresse, Buf);
		CgiFormInputFieldText_P(PSTR("Eigene Nummer:"), EigeneNummer_P, 2, Buf);

		AdresseZuWahlStr(Hauptstelle, Buf);
		CgiFormInputFieldText_P(PSTR("Hauptstelle:"), Hauptstelle_P, 2, Buf);

		CgiFormCheckbox_P(PSTR("feste Hauptstelle:"), FesteHst_P, FesteHauptstelle);

		CgiFormCheckbox_P(PSTR("Alternativ-Suche bei besetzt:"), AlternBeiBes_P, AlternativSucheBeiBesetzt);
						
		readConfig_P(DurchwahlTabelle_P, Buf);

		CgiFormInputFieldText_P(PSTR("Durchwahlen:<br>(mit Komma trennen)"), DurchwahlTabelle_P, 30, Buf);

		CgiFormInputFieldLong_P(PSTR("Protokoll-Level:"), ProtokollLevel_P, 2, ProtokollLevel);

		CgiFormFinish_P(PSTR("Einstellung &Uuml;bernehmen"));
		}
	else // argc > 0
		{
		uint8_t Neu;

		printf_P(PSTR("neue Einstellungen: <a href=\"txp-config.cgi\">weiter</a>"));

		// Eigene Nummer
		// -------------
		if (PharseCheckName_P(http_request, EigeneNummer_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, EigeneNummer_P)], 2);
			Buf[2] = '\0';
			Neu = WahlZuAdresse(atoi(Buf), strlen(Buf));
			if (Neu == BusEigenAdresse)
				printf_P(PSTR("<br>Eigene Nummer unver&auml;ndert: %s"), Buf);
			else if (Modus == ModRuhe && BusEigenAdressePruefenUndSetzen(Neu))
				{
				AdresseZuWahlStr(Neu, Buf);
				changeConfig_P(EigeneNummer_P, Buf);
				printf_P(PSTR("<br>Eigene Nummer: %s"), Buf);
				}
			else
				printf_P(PSTR("<br>Eigene Nummer konnte nicht ge&auml;ndert werden"));
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
			
		} // else argc > 0
		
	cgi_PrintHttpheaderEnd();

	} // txp_cgi_config()
	

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
	
	SeriellUmsetzInit();

	PufferInit(&SendePuffer);
	PufferInit(&EmpfPuffer);
	SocketBufInit();

	AsciiDruckPuffer[0] = '\0';
	HtmlSendeText[0] = '\0';
	DebugMsg[0] = '\0';
	
	// EEPROM auslesen
	char Buf[30];

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

	for (uint8_t i = 0 ; i < 9 ; i++)
		DurchwahlTabelle[i] = 0;
	if (readConfig_P(DurchwahlTabelle_P, Buf) == 1)
		DurchwahlTabelleDekodieren(Buf); // Ergebnis wird ignoriert

	if (readConfig_P(ProtokollLevel_P, Buf) == 1)
		ProtokollLevel = atoi(Buf);
	else
		ProtokollLevel = 1;
		
	BusEigenAdrMehrfach = 1; // muss Potenz von 2 sein (also 1, 2, 4, 8, 16, ... , Standard = 1

	TwiInit();

	timer0_init(TxpTimerFreq); 
	if (!timer0_RegisterCallbackFunction(txp_timerEvent))
		return;
	
	cgi_RegisterCGI( txp_cgi_msg_In, PSTR("txp-msg-in.cgi"));
	cgi_RegisterCGI( txp_cgi_msg_Out, PSTR("txp-msg-out.cgi"));
	cgi_RegisterCGI( txp_cgi_config, PSTR("txp-config.cgi"));
	cgi_RegisterCGI( txp_cgi_debug, PSTR("txp-debug.cgi"));
	cgi_RegisterCGI( txp_cgi_TwiTlnListe, PSTR("txp-twitlnliste.cgi"));
#if defined(MMC)
	cgi_RegisterCGI( cgi_SdDirectory, PSTR("sddir.cgi"));
#endif //defined(MMC)

	TxpClientSocket = NO_SOCKET_USED;
	TxpServerSocket = NO_SOCKET_USED;
	
	RegisterTCPPort(TXP_PORT);
	
	Timer0Cnt_Min = 255;
	Timer0Cnt_Max = 0;
	Timer0Callback_Max = 0;

	TWCR = (1<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);

	Status = (1 << StatBit_Frei) | (1 << StatBit_LeitungKennung);

	printf_P( PSTR("TelexPhone Port %d.\r\n") , TXP_PORT );

	THREAD_RegisterThread( txp_thread, PSTR("TxP"));

	TlnBuchInit();
	
	wdt_enable(WDTO_8S);
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

