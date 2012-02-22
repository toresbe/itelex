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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "config.h"

#include <bool.h>

// #include "defports.h"
// #include "bits.h"

#include "hardware/led/led_core.h"

#include "system/net/ip.h"
#include "system/net/tcp.h"
#include "system/net/ethernet.h"
#include "system/thread/thread.h"
#include "system/config/eeconfig.h"
#include "system/clock/clock.h"

#include "apps/httpd/cgibin/cgi-bin.h"
#include "apps/httpd/httpd2_pharse.h"

#include "hardware/timer0/timer0.h"

#include "TxP/txp.h"
#include "BusKomm.h"
#include "TxP2-Defs.h"
#include "FifoPuffer.h"
#include "BaudotCode.h"


// Aktueller Modus

static enum
	{
	ModRuhe = 0, // nichts läuft
	ModKommendWarteReservOK = 1, // Kommend = vom Netz zum internen Anschluss
	ModKommendWarteEinQuitt = 2,
	ModKommendVerbunden = 7, 
	ModGehendReserv = 8, // Gehend = vom internen Anschluss zum Netz, Reservierung ist eingegangen
	ModGehendWaehlen = 9,
	ModGehendEinKdoEmpf = 10, // Einschaltkommando ist angekommen, Warte auf Quittung vom Anrufer
	ModGehendVerbunden = 11,
	ModWarteAusQuitt = 15
	} Modus;

	
static bool EndgeraetEinschalten;
	//!< Anforderung zum Einschalten des Fernschreibers \todo in Modus verpacken.

#define TXP_ASCII_PORT 134
#define TXP_PACKET_PORT 133
	
	
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


enum { SerUmEmpfWarte = 0, SerUmEmpfFertig = 8, SerUmSendWarte = 0, SerUmSendStart = 1 } ;

// Allgemein: 

volatile static uint8_t SerUmTickZaehlerEmpf; //!< Zähler der Einzel-Ticks beim Empfang

volatile static uint8_t SerUmTickZaehlerSend; //!< Zähler der Einzel-Ticks beim Senden

volatile static uint16_t LebenszeichenZaehler; 
	//!< Zählt rückwärts die Takte bis zum nächsten Lebenszeichen.
	//!< wird während der Verbindung missbraucht zum Zählen der Takte bis zur Pegelwiederholung.
	
volatile static uint16_t RuheZaehler;
	//!< Zählt Ticks in denen nix passiert. Wird bei Datenempfang und Sendung und 
	//!< Verbindungsaufbau auf Null gesetzt
	
volatile TPuffer SendePuffer; 
	//!< Puffer (mit Baudot-Codes gefüllt) für die Richtung Netz -> Endgerät
	
volatile TPuffer EmpfPuffer; 
	//!< Puffer (mit Baudot-Codes gefüllt) für die Richtung Endgerät -> Netz
	
enum { EmpfTextMax = 400, SendeTextMax = 70 } ;
	//!< Puffergrößen für Textpuffer.

static char EmpfText[EmpfTextMax];
	//!< Puffer für dekodierten Text (Endgerät -> Netz), mit Null abgeschlossen

static char SendeText[SendeTextMax];
	//!< Puffer für zu sendenden Text (Netz -> Endgerät), mit Null abgeschlossen


static int AsciiSocket;
	//!< Handle für ASCII-Verbindungen eingehend.

static int TxpSocket;
	//!< Handle für Txp-Packet-Verbindungen eingehend.


static uint8_t Hauptstelle; 
	//!< Bus-Adresse für den nächsten kommenden Ruf, wird bei FesteHauptstelle = false auf die
	//!< Adresse des letzten Anrufers gesetzt

static bool FesteHauptstelle;
	//!< Wenn true, werden kommende Verbindungen immer auf die gleiche Endstelle gesendet

static uint8_t DurchwahlTabelle[9];
	//!< Liste der Nebenstellen-Nummern bei kommenden Rufen mit Durchwahl

enum { DebugMsgMax = 100 } ;

static char DebugMsg[DebugMsgMax];
	//!< String für außergewöhnliche Fälle
	

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
	if (t0c < Timer0Cnt_Min)
		Timer0Cnt_Min = t0c;
	if (t0c > Timer0Cnt_Max)
		Timer0Cnt_Max = t0c;

	Timer0CallbackCount++;

	if (Modus == ModKommendVerbunden || Modus == ModGehendVerbunden)
		{ // ist Verbunden, also Pegel senden und empfangen
		bool NeuMark = true; // wird beim Senden vielleicht noch geändert
		RuheZaehler++; // wird aber vielleicht gleich wieder auf Null gestellt

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
					SerUmEmpfBitNr = SerUmEmpfFertig; //! \TODO nur dann Empfang abschließen, wenn auch ein Stop-Bit da war

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
					} // SerUmSendBitNr == 1
				else if (SerUmSendBitNr == 0 && !PufferLeer(&SendePuffer))
					{
					SerUmSendDaten = PufferAusg(&SendePuffer);
					SerUmSendBitNr = SerUmSendStart;
					}
				}
			} // else Empfang ruht

		if (SerUmSendBitNr >= 2)
			{ // Sendung läuft
			if (SerUmSendBitNr == 8) // Stop-Bit läuft
				{
				NeuMark = true;
				if (++SerUmTickZaehlerSend >= 15)
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
			LebenszeichenZaehler = 2; // sofort Wiederholungszeichen senden
			LED_off(BLAU);
			}
		else if (!NeuMark && SendeMark)
			{
			BusSenden(BusKdoSpace);
			SendeMark = false;
			LebenszeichenZaehler = 2; // sofort Wiederholungszeichen senden
			LED_on(BLAU);
			}
		else
			{ // kein Sendepegel-Wechsel
			// Lebenszeichen = Aktuellen Pegel regelmäßig senden
			if (LebenszeichenZaehler > 0)
				LebenszeichenZaehler--;
			else
				{ // Lebenszeichen wenn möglich senden
				if ((BusAuftrag == Nichts || BusAuftrag == Fertig) && BusFrei)
					{
					BusSenden(SendeMark ? BusKdoMarkWdh : BusKdoSpaceWdh);
					LebenszeichenZaehler = 255;
					}
				}
			} // kein Sendepegel-Wechsel

		// Status-Anzeige
		if (BusEmpfMark)
			if (Modus == ModKommendVerbunden)
				LED_off(GELB);
			else
				LED_off(GRUEN);
		else
			if (Modus == ModKommendVerbunden)
				LED_on(GELB);
			else
				LED_on(GRUEN);
		} // if IstVerbunden

	else if (Modus != ModRuhe && Modus != ModWarteAusQuitt)
		{ // Lebenszeichen regelmäßig senden
		if (LebenszeichenZaehler > 0)
			LebenszeichenZaehler--;
		else
			{ // Lebenszeichen wenn möglich senden
			if ((BusAuftrag == Nichts || BusAuftrag == Fertig) && BusFrei)
				{
				BusSenden(BusLebenszeichen);
				LebenszeichenZaehler = 255;
				}
			}
		} // if Modus != Ruhe

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


//! Druckt am verbundenen Fernschreiber Datum und Uhrzeit des Anrufs
//-------------------------------------------------------------------
//! \todo bewirkt manchmal Endlosschleifen -> Ausschaltzwang einbauen.	
static void DatumDruckenUndAusschalten()
	{
	if (!EndgeraetEinschalten)
		return;

	struct TIME Time;
	// Zeit holen
	CLOCK_GetTime(&Time);
	
	char *p = SendeText;
	while (*p != '\0' && p < SendeText + SendeTextMax - 50) // 50 ist die Länge des Datum-Strings
		p++;

	sprintf_P(p, PSTR("\r\n\ndatum: %02u.%02u.%04u  uhrzeit: %02d:%02d:%02d\r\n\n"),
			  Time.DD, Time.MM, Time.YY, Time.hh, Time.mm, Time.ss);
	EndgeraetEinschalten = false;
	}
	
	
//! Nur für debugging.	
static uint32_t TxpThreadCount; 

//! Der TelexPhone-client an sich.
//------------------------------------------------------------------------------------------------------------
/*! Er wird zyklisch aufgerufen und hat folgende Aufgaben:
 * \par Steuerbefehle vom TWI-Bus annehmen und interpretieren.
 * \par Nachschauen, ob eine Verbindung auf den registrierten Port eingegangen ist. Wenn ja 
 * holt er sich die Socketnummer der Verbindung und speichert diese.
 * \par Wenn eine Verbindung zustande gekommen ist wird diese wiederrum zyklisch nach neuen Daten abgefragt und entsprechend
 * reagiert.
 * \param 	NONE
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/	

void txp_thread()
	{
	uint8_t Code;
	char z[2];

	TxpThreadCount++;

	LED_toggle(ROT); // HACK Test der Aufrufpausen von txp_thread
	
	// ======================================================================
	// Auf TWI-Bus empfangene Codes auswerten
	// ======================================================================
	if (GetEmpfByte(&Code))
		{
		switch (Code)
			{
			case 1 ... BusKdoVerbAufnahme:
#if (TXP_DEBUG >= 1)
				printf_P(PSTR("TxP: Reservierung intern / gehend von %d\r\n" ), Code);
#endif
				if (Modus == ModRuhe)
					{
					Modus = ModGehendReserv;
					BusVerbPartner = Code << 1; 
					if (!FesteHauptstelle)
						Hauptstelle = BusVerbPartner;
					CLR_BIT_Status(StatBit_Frei);
					CLR_BIT_Status(StatBit_LeitungKennung);
					SET_BIT_Status(StatBit_AngerufenBelegt);
					LED_on(GELB);
					}
				else
					FalschCodeEmpfangen(Code);
				break;
				
			case BusKdoEin:
#if (TXP_DEBUG >= 1)
				printf_P(PSTR("TxP: Einschaltkommando intern / gehend\r\n" ));
#endif
				if (Modus == ModGehendReserv)
					{
					BusSenden(BusKdoWahlFreigabe);
					Modus = ModGehendWaehlen;
					}
				else
					FalschCodeEmpfangen(Code);
				break;
				
			case BusQuittEin:
#if (TXP_DEBUG >= 1)
				printf_P(PSTR("TxP: Einschaltquittung intern / kommend\r\n" ));
#endif
				if (Modus == ModKommendWarteEinQuitt)
					{
					Modus = ModKommendVerbunden;
					BusEmpfMark = true;
					SendeMark = true;
					PufferInit(&SendePuffer);
					PufferInit(&EmpfPuffer); EmpfPuffer.BuZiMode = BuMode;
					EndgeraetEinschalten = true;
					LED_on(GRUEN);
					}
				else
					FalschCodeEmpfangen(BusQuittEin);
				break;

			case BusKdoWahlFreigabe:
				// dies ist eine Leitungsschnittstelle, die kann nicht wählen.
#if (TXP_DEBUG >= 1)
				printf_P(PSTR("TxP: Wahlaufforderung intern / kommend???\r\n" ));
#endif
				FalschCodeEmpfangen(BusQuittEin);
				break;
				
			case BusKdoWahlziffer0 ... BusKdoWahlziffer9:
#if (TXP_DEBUG >= 1)
				printf_P(PSTR("TxP: Wahlziffer %d intern / gehend\r\n" ), Code - BusKdoWahlziffer0);
#endif
				if (Modus == ModGehendWaehlen)
					{
					// Todo Ziffern auswerten
					BusSenden(BusQuittEin);
					BusEmpfMark = true;
					SendeMark = true;
					PufferInit(&SendePuffer);
					PufferInit(&EmpfPuffer); EmpfPuffer.BuZiMode = BuMode;
					EndgeraetEinschalten = true;

					// Todo sinnvoll verbinden.
					long IP = IPDOT(192l,168l,178l,32l);
					AsciiSocket = Connect2IP( IP, 23 ); // << HACK, richtig wäre TXP_ASCII_PORT );
					if (AsciiSocket == -1 )
						{
#if (TXP_DEBUG >= 1)
						printf_P(PSTR("TxP: Verbindung Ascii ausgehend versagt\r\n" ));
#endif
						AsciiSocket = NO_SOCKET_USED;
						}
					else
						{
#if (TXP_DEBUG >= 1)
						printf_P(PSTR("TxP: Verbindung Ascii ausgehend hergestellt\r\n" ));
#endif
						}
					Modus = ModGehendVerbunden;
					}
				else
					FalschCodeEmpfangen(BusQuittEin);
				break;
				
			case BusKdoSchluss:
#if (TXP_DEBUG >= 1)
				printf_P(PSTR("TxP: Ausschaltung intern\r\n" ));
#endif
				EndgeraetEinschalten = false;
				if (Modus != ModRuhe)
					{
					BusSenden(BusQuittSchluss);
					CLR_BIT_Status(StatBit_AngerufenBelegt);
					CLR_BIT_Status(StatBit_FsBefBetrieb);
					CLR_BIT_Status(StatBit_FsMeldBetrieb);
					SET_BIT_Status(StatBit_Frei);
					SET_BIT_Status(StatBit_LeitungKennung);
					}
					
				// alle Ports schließen...
				if (AsciiSocket != NO_SOCKET_USED)
					{
#if (TXP_DEBUG >= 1)
					printf_P(PSTR("TxP: Verbindung Ascii wird geschlossen\r\n" ));
#endif
					CloseTCPSocket(AsciiSocket);
					AsciiSocket = NO_SOCKET_USED;
					}
					
				Modus = ModRuhe;
				LED_off(GELB);
				LED_off(GRUEN);
				LED_off(BLAU);
				break;
					
			case BusQuittSchluss:
#if (TXP_DEBUG >= 1)
				printf_P(PSTR("TxP: Ausschaltung quittiert\r\n" ));
#endif
				if (Modus != ModWarteAusQuitt)
					{
					strcpy_P(DebugMsg, PSTR("Schlussquittung ohne Aufforderung"));
					//! TODO Ausschalten etc., Fehler melden
					FalschCodeEmpfangen(Code);
					}
				Modus = ModRuhe;
				CLR_BIT_Status(StatBit_AngerufenBelegt);
				CLR_BIT_Status(StatBit_FsBefBetrieb);
				CLR_BIT_Status(StatBit_FsMeldBetrieb);
				SET_BIT_Status(StatBit_Frei);
				SET_BIT_Status(StatBit_LeitungKennung);
				LED_off(GELB);
				LED_off(GRUEN);
				LED_off(BLAU);
				break;
			
			default:
				FalschCodeEmpfangen(Code);
				break;
				
			} // switch Code
		} // if GetEmpfByte

	// ======================================================================
	// Umwandlung ASCII - Baudot und zurück
	// ======================================================================
		
	if (Modus == ModKommendVerbunden || Modus == ModGehendVerbunden)
		{
		// zu druckenden Text umwandeln
		// ---------------------------
		if (SendeText[0] != '\0' && PufferLeer(&SendePuffer))
			{
			uint8_t Code1, Code2;
			
			if (SendeText[0] == '@') 
				{ // Kennungsgeber besonders behandeln...
				SendeText[0] = CodeChrWerDa; 
				SendeText[1] = '\0'; // keine weiteren Zeichen bearbeiten
				}
			
			if (ZeichenZuCode2(SendeText[0], (char*) &SendePuffer.BuZiMode, &Code1, &Code2))
				{ // Zeichen erfolgreich in Baudot-Code umgesetzt
				if (PufferSpeich(&SendePuffer, Code1) && (Code2 == 255 || PufferSpeich(&SendePuffer, Code2)))
					{
					z[0] = SendeText[0];
					z[1] = '\0';
					strcat(EmpfText, z); // Eigenecho
					strcpy(SendeText, SendeText+1); // erstes Zeichen aus SendeText-Puffer löschen
					}
				}
			else
				// Zeichen ist nicht darstellbar, also löschen
				{
				if (DebugMsg[0] == '\0') // noch leer
					strcpy_P(DebugMsg, PSTR("?nicht druckbare Zeichen: "));
				uint8_t l = strlen(DebugMsg);
				if (DebugMsg[0] == '?' && l + 2 < DebugMsgMax)
					{
					DebugMsg[l] = SendeText[0];
					DebugMsg[l+1] = '\0';
					}

				strcpy(SendeText, SendeText+1); // erstes Zeichen aus SendeText-Puffer löschen
				}
			}
			
		// empfangene Codes in Text wandeln
		// --------------------------------
		if (!PufferLeer(&EmpfPuffer))
			{
			if (strlen(EmpfText) > EmpfTextMax - 2)
				strcpy(EmpfText, EmpfText+1); // erstes Zeichen aus EmpfangsText-Puffer löschen
			z[0] = CodeZuZeichen(PufferAusg(&EmpfPuffer), (char*) &EmpfPuffer.BuZiMode);
			if (z[0] != '\0')
				{
				z[1] = '\0';
				strcat(EmpfText, z);
				}
			}
		} // if (Modus == ModKommendVerbunden || Modus == ModGehendVerbunden)
		
	// ======================================================================
	// Temporär: Bei eintreffendem Text Endgerät anschalten (eigentlich nur für HTML.
	// ======================================================================
	if (EndgeraetEinschalten && Modus == ModRuhe)
		{
#if (TXP_DEBUG >= 1)
		printf_P(PSTR("TxP: Einschaltung extern -> Reservierung intern\r\n" ));
#endif
		BusVerbPartner = Hauptstelle; // TODO auch andere suchen
		CLR_BIT_Status(StatBit_Frei);
		CLR_BIT_Status(StatBit_LeitungKennung);
		CLR_BIT_Status(StatBit_AngerufenBelegt);
		BusSenden(BusEigenAdresse >> 1);
		Modus = ModKommendWarteReservOK;
		}

	if (Modus == ModKommendWarteReservOK && BusAuftrag == Fertig)
		{
		if (BusErgebnis == Ok)
			{
#if (TXP_DEBUG >= 1)
			printf_P(PSTR("TxP: Einschaltung extern -> Einschaltung intern\r\n" ));
#endif
			BusSenden(BusKdoEin);
			Modus = ModKommendWarteEinQuitt;
			}
		else
			{
#if (TXP_DEBUG >= 1)
			printf_P(PSTR("TxP: Einschaltung extern -> intern VERSAGT\r\n" ));
#endif
			SendeText[0] = '\0'; 
			strcpy_P(DebugMsg, PSTR("Reservierung für Einschaltung konnte nicht versand werden"));
			SET_BIT_Status(StatBit_Frei);
			SET_BIT_Status(StatBit_LeitungKennung);
			Modus = ModRuhe;
			EndgeraetEinschalten = false;
			LED_off(GELB);
			LED_off(GRUEN);
			}
		}

	if (Modus == ModKommendVerbunden && RuheZaehler > 30 * 500)
		DatumDruckenUndAusschalten();
		
	if (!EndgeraetEinschalten 
		&& RuheZaehler > 500 / 2 
		&& (Modus == ModKommendVerbunden || Modus == ModGehendVerbunden)
		&& SendeText[0] == '\0'
		&& PufferLeer(&SendePuffer))
		{
#if (TXP_DEBUG >= 1)
		printf_P(PSTR("TxP: Ausschaltung intern\r\n" ));
#endif
		BusSenden(BusKdoSchluss);
		Modus = ModWarteAusQuitt;
		RuheZaehler = 0;
		}
		
	/* TODO ModWarteAusQuitt zeitlich begrenzen, danach 
					CLR_BIT_Status(StatBit_AngerufenBelegt);
					CLR_BIT_Status(StatBit_FsBefBetrieb);
					CLR_BIT_Status(StatBit_FsMeldBetrieb);
					SET_BIT_Status(StatBit_Frei);
					SET_BIT_Status(StatBit_LeitungKennung);
					LED_off(GELB);
					LED_off(GRUEN);
					LED_off(BLAU);
*/	
		
	// ==========================================================================
	// Ereignisse und Daten der Netzwerk-Ports bearbeiten.
	// ==========================================================================

	// keine alte Verbindung offen?
	if (AsciiSocket == NO_SOCKET_USED)
		{ 	
		// auf neue Verbindungsanfrage testen
		AsciiSocket = CheckPortRequest(TXP_ASCII_PORT);
		
		//! TODO prüfen, was bei bestehender ausgehender ASCII-Verbindung und gleichzeitigem Versuch einer ankommenden Verbindung passiert.
		
		if (AsciiSocket != NO_SOCKET_USED)
			{
			if (Modus == ModRuhe)
				{	
				// Wenn ja, Startmeldung ausgeben und startzustand herstellen für i2c
				printf_P(PSTR("Telnet-Ascii-Verbindung hergestellt\r\n" ));
				SendeText[0] = '\0';
				EmpfText[0] = '\0';
				EndgeraetEinschalten = true;
				}
			else
				{	
				// Wenn ja, Startmeldung ausgeben und startzustand herstellen für i2c
				printf_P(PSTR("Telnet-Ascii-Verbindung abgewiesen\r\n" ));
				CloseTCPSocket(AsciiSocket);
				AsciiSocket = NO_SOCKET_USED;
				}
			}
		}
	
	// soll offene Verbindung geschlossen werden?
	if (Modus == ModKommendVerbunden && AsciiSocket != NO_SOCKET_USED && CheckSocketState(AsciiSocket) == SOCKET_NOT_USE)
		{
		printf_P(PSTR( "Telnet-Ascii-Verbindung getrennt\r\n" ));
		CloseTCPSocket(AsciiSocket);
		AsciiSocket = NO_SOCKET_USED;
		DatumDruckenUndAusschalten();
		}

	// Auf neue Daten zum drucken testen
	// ---------------------------------
	if (AsciiSocket != NO_SOCKET_USED)
		{
		int InCount = GetBytesInSocketData(AsciiSocket);
		bool Read = true;
		if (InCount > 0)
			{
			int AltLen = strlen(SendeText);
			
			if (InCount > SendeTextMax - AltLen - 2)
				InCount = SendeTextMax - AltLen - 2;
				
			if (Read)
				{
				int Res = GetSocketData(AsciiSocket, InCount, SendeText + AltLen);
				if (Res > 0)
					{
					SendeText[AltLen + Res] = '\0';
					EndgeraetEinschalten = true;
					if (strncmp_P(SendeText + AltLen, PSTR("+++"), 3) == 0) 
						{
						// Socket schließen
						printf_P(PSTR("Telnet-Ascii-Verbindung Beenden\r\n") );
						CloseTCPSocket(AsciiSocket);
						AsciiSocket = NO_SOCKET_USED;
						DatumDruckenUndAusschalten();
						}
					}
				else
					{
					// TODO Fehler beim IP-Empfang
					}
				} // if Read (Platz ist im Puffer vorhanden
			} // if (InCount > 0)
		
		// vom Endgerät empfangene Daten ggf. ins Netz senden
		if (strlen(EmpfText) > 10 || (EmpfText[0] != '\0' && RuheZaehler >= 2 * 50))
			{
#if (TXP_DEBUG >= 1)
			printf_P(PSTR("TxP: sende ASCII an Telnet-Verbindung: %s (%d" ), EmpfText, strlen(EmpfText));
#endif
			int Res = PutSocketData_RPE(AsciiSocket, strlen(EmpfText), EmpfText, RAM);
			if (Res > 0)
				{
				strcpy(EmpfText, EmpfText + Res);
				}
#if (TXP_DEBUG >= 1)
			printf_P(PSTR("/%d)\r\n" ), Res);
#endif
			} // if es gibt was zu senden
		} // if AsciiSocket != NO_SOCKET_USED
		
	} // txp_thread


//! Liest den String s aus in die Durchwahl-Tabelle.
//! \return Anzahl der korrekt gelesenen Einträge
static uint8_t DurchwahlTabelleDekodieren(char *s)
	{
	uint8_t i = 0; // Index in der Tabelle
	bool Anf = true; // noch keine Ziffer erkannt
	
	while (*s != '\0' && i < 9)
		{
		switch (*s)
			{
			case '0' ... '9':
				if (Anf)
					{
					DurchwahlTabelle[i] = *s - '0';
					Anf = false;
					}
				else
					DurchwahlTabelle[i] = (10 * DurchwahlTabelle[i]) + (*s - '0');
				break;
			
			case ',':
			case '/':
			case '.':
				i++;
				Anf = true;
				break;
			
			case ' ':
				if (!Anf)
					i++;
				Anf = true;
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

	printf_P(PSTR("EmpfText: ["));
	printf(EmpfText);
	printf_P(PSTR("]<br>SendeText: ["));
	printf(SendeText);
	printf_P(PSTR("]<p>DebugMsg: %s<br>"), DebugMsg);
	DebugMsg[0] = '\0';

#define PRINTVAL(Var) printf_P(PSTR("<br>" #Var " = %d"), Var)

	PRINTVAL(Timer0Cnt_Min);
	PRINTVAL(Timer0Cnt_Max);
	PRINTVAL(Timer0Callback_Max);

	PRINTVAL(Modus);
	PRINTVAL(Status); // bezüglich TxP-Funktionalität (ist auf TWI-Bus sichtbar)

	PRINTVAL(BusEmpfMark);

	PRINTVAL(SerUmEmpfBitNr); 

	PRINTVAL(SerUmEmpfDaten);
	PRINTVAL(SerUmEmpfFehler);
	PRINTVAL(SerUmEmpfMarkZaehl);
	PRINTVAL(SerUmTickZaehlerEmpf);
	PRINTVAL(SerUmTickZaehlerSend);
	PRINTVAL(RuheZaehler);

	PRINTVAL(PufferAnzahl(&EmpfPuffer));
	for (i = EmpfPuffer.AusgP ; i != EmpfPuffer.SpeichP ; i++)
		{
		if (i >= MaxPuffer) 
			i = 0;
		printf_P(PSTR(" %02X"), EmpfPuffer.Puffer[i]);
		}

	PRINTVAL(SerUmSendBitNr);
	PRINTVAL(SerUmSendDaten);
	PRINTVAL(SendeMark);
	PRINTVAL(LebenszeichenZaehler); 

	PRINTVAL(FalscherCode); FalscherCode = 0;
	PRINTVAL(TwiIsrCount); TwiIsrCount = 0;
	PRINTVAL(Timer0CallbackCount / 500); //Timer0CallbackCount = 0;
	PRINTVAL(TxpThreadCount / 500); //TxpThreadCount = 0;

	printf_P(PSTR("<p>Ethernet: %ld Bytes in %ld Packeten LockErrors %ld\r\n") , ByteCounter, PacketCounter, eth_state_error );

	cgi_PrintHttpheaderEnd();

	}
	

/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface für das Hauptfenster der Fernschreiber-Simulation
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void txp_cgi_msg_MainFrame( void * pStruct )
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;

	printf_P(PSTR(
		"<HTML>"
		"<HEAD>"
		"<meta http-equiv=\"expires\" content=\"1\">"
		"<meta http-equiv=\"pragma\" content=\"no-cache\">"
		"</HEAD>"
		"<frameset rows=\"*,60\" scrolling=\"no\" frameborder=\"2\" border=\"2\" framespacing=\"2\" bordercolor=\"#000000\">"
		"<frame src=\"txp-msg-out.cgi\" name=\"MsgOut\" scrolling=\"auto\">"
		"<frame src=\"txp-msg-in.cgi\" name=\"MsgIn\" scrolling=\"no\">"
		"<noframes>"
		"<body>"
		"<p>Ihr Browser unterstützt keine Frames!</p>"
		"</body>"
		"</noframes>"
		"</frameset>"
		"</HTML>"
		"\r\n\r\n"	));
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
					"<meta http-equiv=\"refresh\" content=\"5; URL=txp-msg-out.cgi\">"
					"</HEAD>"
					"<BODY>" ));
					
	if (Modus == ModKommendVerbunden || Modus == ModGehendVerbunden)
		{
		printf_P(PSTR("Druckspiegel:<br><pre>%s</pre>"), EmpfText);
		if (SendeText[0] != '\0')
			printf_P(PSTR("<i><pre>%s</pre></i>"), SendeText);
		}
	else
		{
		printf_P(PSTR("Texteingabe startet Fernschreiber"));
		EmpfText[0] = '\0';
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
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;

	bool DrucktextAufforderung;

	DrucktextAufforderung = (http_request->argc != 0) && PharseCheckName_P(http_request, PSTR("Eingabe"));

	if (DrucktextAufforderung)
		{
		strncat(SendeText, http_request->argvalue[PharseGetValue_P(http_request, PSTR("Eingabe"))], SendeTextMax - strlen(SendeText) - 3);
		SendeText[SendeTextMax-3] = '\0';
		strcat_P(SendeText, PSTR("\r\n"));
		EndgeraetEinschalten = true;
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
	
	
/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface zum Ändern der Einstellungen des TelexPhone-Interface 
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/


const char Hauptstelle_P[] PROGMEM = "HAUPTSTELLE";
const char EigeneNummer_P[] PROGMEM = "EIGENENUMMER";
const char FesteHst_P[] PROGMEM = "FESTEHPST";
const char DurchwahlTabelle_P[] PROGMEM = "DURCHWAHLTAB";

 
void txp_cgi_config(void *pStruct)
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	char Buf[35];
	
	cgi_PrintHttpheaderStart();

	if ( http_request->argc == 0 )
		{
		printf_P(PSTR(
			"<form action=\"txp-config.cgi\">"
			"<table border=\"0\" cellpadding=\"5\" cellspacing=\"0\">"
			));

		printf_P( PSTR(	"<tr>"
						"<td align=\"right\">Eigene Nummer:</td>"
						"<td><input name=\"EIGENENUMMER\" type=\"text\" size=\"2\" value=\"%d\" maxlength=\"2\"></td>"
						"</tr>"), BusEigenAdresse >> 1);

		printf_P( PSTR(	"<tr>"
						"<td align=\"right\">Hauptstelle:</td>"
						"<td><input name=\"HAUPTSTELLE\" type=\"text\" size=\"2\" value=\"%d\" maxlength=\"2\"></td>"
						"</tr>"), Hauptstelle >> 1);
						
		printf_P( PSTR( "<tr>"
					   	"<td align=\"right\">feste Hauptstelle:</td>"
					    "<td><input name=\"FESTEHPST\" type=\"checkbox\" value=\"1\" " )); 
		if (FesteHauptstelle)
			printf_P( PSTR("checked"));
		printf_P( PSTR(	"></td>"
  						"</tr>") );
		
		readConfig_P(DurchwahlTabelle_P, Buf);

		printf_P( PSTR(	"<tr>"
						"<td align=\"right\">Durchwahlen:<br>(mit Komma trennen)</td>"
						"<td><input name=\"DURCHWAHLTAB\" type=\"text\" size=\"30\" value=\"%s\" maxlength=\"30\"></td>"
						"</tr>"), Buf);

		printf_P(PSTR( "<tr>"
						"<td><input type=\"submit\" value=\" Einstellung &Uuml;bernehmen \"></td>"
  						"</tr>"
					   	"</table>"
						"</form>") );
		}
	else // argc > 0
		{
		uint8_t NeuEigenAdresse;

		printf_P(PSTR("neue Einstellungen: <a href=\"txp-config.cgi\">weiter</a>"));

		Buf[2] = '\0';
		if (PharseCheckName_P(http_request, EigeneNummer_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, EigeneNummer_P)], 2);
			NeuEigenAdresse = atoi(Buf) << 1; 
			if (Modus == ModRuhe && BusEigenAdressePruefenUndSetzen(NeuEigenAdresse))
				{
				itoa(NeuEigenAdresse >> 1, Buf, 10);
				changeConfig_P(EigeneNummer_P, Buf);
				printf_P(PSTR("<br>Eigene Nummer: %d"), BusEigenAdresse >> 1);
				}
			else
				printf_P(PSTR("<br>Eigene Nummer konnte nicht ge&auml;ndert werden"));
			}
		
		if (PharseCheckName_P(http_request, Hauptstelle_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, Hauptstelle_P)], 2);
			Hauptstelle = atoi(Buf) << 1;
			itoa(Hauptstelle >> 1, Buf, 10);
			changeConfig_P(Hauptstelle_P, Buf);
			}
		printf_P(PSTR("<br>Hauptstelle: %d"), Hauptstelle >> 1);
		
		if (PharseCheckName_P(http_request, FesteHst_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, FesteHst_P)], 2);
			FesteHauptstelle = atoi(Buf) != 0; 
			}
		else
			{
			FesteHauptstelle = false;
			Buf[0] = '0', Buf[1] = '\0';
			}
		changeConfig_P(FesteHst_P, Buf);
		printf_P(PSTR("<br>Feste Hauptstelle: %d"), FesteHauptstelle);
		
		if (PharseCheckName_P(http_request, DurchwahlTabelle_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, DurchwahlTabelle_P)], 30);
			Buf[30] = '\0';
			DurchwahlTabelleDekodieren(Buf);
			}
		sprintf_P(Buf, PSTR("%d,%d,%d,%d,%d,%d,%d,%d,%d"), 
				  DurchwahlTabelle[0], DurchwahlTabelle[1], DurchwahlTabelle[2],
				  DurchwahlTabelle[3], DurchwahlTabelle[4], DurchwahlTabelle[5],
				  DurchwahlTabelle[6], DurchwahlTabelle[7], DurchwahlTabelle[8]);
		changeConfig_P(DurchwahlTabelle_P, Buf);
		printf_P(PSTR("<br>Durchwahlen: %s"), Buf);
		}
		
	cgi_PrintHttpheaderEnd();

	}
	

/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Initialisiert den TelexPhone-clinet und registriert den Port auf welchen dieser lauschen soll.
 * \param 	NONE
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void txp_init()
	{
	EndgeraetEinschalten = false;
	
	SeriellUmsetzInit();

	PufferInit(&SendePuffer);
	PufferInit(&EmpfPuffer);
	
	SendeText[0] = '\0';
	EmpfText[0] = '\0';
	DebugMsg[0] = '\0';
	
	// EEPROM auslesen
	char Buf[30];

	if (readConfig_P(EigeneNummer_P, Buf) == 1)
		BusEigenAdresse = atoi(Buf) << 1;
	else
		BusEigenAdresse = 22 << 1;

	if (readConfig_P(FesteHst_P, Buf) == 1)
		FesteHauptstelle = atoi(Buf) != 0;
	else
		FesteHauptstelle = false;

	if (readConfig_P(Hauptstelle_P, Buf) == 1)
		Hauptstelle = atoi(Buf) << 1;
	else
		Hauptstelle = 0, FesteHauptstelle = false;

	for (uint8_t i = 0 ; i < 9 ; i++)
		DurchwahlTabelle[i] = 0;
	if (readConfig_P(DurchwahlTabelle_P, Buf) == 1)
		DurchwahlTabelleDekodieren(Buf); // Ergebnis wird ignoriert
	
	BusEigenAdrMehrfach = 1; // muss Potenz von 2 sein (also 1, 2, 4, 8, 16, ... , Standard = 1

	TwiInit();

	timer0_init(50 * 10); // 50 Baud mit 10 Takten je Bit
	if (!timer0_RegisterCallbackFunction(txp_timerEvent))
		return;
	
	cgi_RegisterCGI( txp_cgi_msg_MainFrame, PSTR("txp-msg.cgi"));
	cgi_RegisterCGI( txp_cgi_msg_In, PSTR("txp-msg-in.cgi"));
	cgi_RegisterCGI( txp_cgi_msg_Out, PSTR("txp-msg-out.cgi"));
	cgi_RegisterCGI( txp_cgi_config, PSTR("txp-config.cgi"));
	cgi_RegisterCGI( txp_cgi_debug, PSTR("txp-debug.cgi"));

	AsciiSocket = NO_SOCKET_USED;
	TxpSocket = NO_SOCKET_USED;
	
	RegisterTCPPort(TXP_ASCII_PORT);
	
	Timer0Cnt_Min = 255;
	Timer0Cnt_Max = 0;
	Timer0Callback_Max = 0;

	TWCR = (1<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);

	Status = (1 << StatBit_Frei) | (1 << StatBit_LeitungKennung);

	printf_P( PSTR("TelexPhone Port %d.\r\n") , TXP_PORT );
	THREAD_RegisterThread( txp_thread, PSTR("TxP"));

	}


//@}
