/*! \file TxP.h \brief TelexPhone Definitionen */
/***************************************************************************
 *            TxP.h
 *
 ****************************************************************************/
///	\ingroup software
///	\defgroup 
///	\code #include "txp.h" \endcode
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
#ifndef _TXP_H_

#define _TXP_H_

#include "config.h"

#ifdef TELEXPHONE

#if !defined(TXP_TLNSERVER) && !defined(TXP_ANSCHLUSS)
	#warning Kein TxP-Modul aktiv!
#endif

#include <avr/pgmspace.h>  
#include <avr/interrupt.h>
#include "system/shell/shell.h"
#include "config.h"

#include "Defports.h"
#include "FifoPuffer.h"


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
	
	// über HTML-Seite verursachte direkte Druckausgabe
	ModHtmlChatWarteEinQuitt = 21, //!< Warte auf Einschalt-Quittung des Endgeräts
	ModHtmlChatVerbunden = 22, 
	
	ModMeldungsdruckWarteEinQuitt = 25, //!< Danach kommt gleich PufferdruckUndSchluss.

	ModDeaktiviert = 31, //!< Durch Tastendruck ausgeschaltet.
	ModWarteGrundstellung = 32, 
		//!< Wartet darauf, dass nach Ausschaltung des lokalen Endgerätes der 
		//!< Socket wieder geschlossen ist und alles andere auch die Grundstellung hat.
	
	} TModus;
	
	
extern TModus Modus;

extern void ModusWechsel(TModus neu);


DEFPORTINPULL(Taste, B, 3);

typedef enum { 
	NichtGedr, //!< nicht gedrückt.
	Kurz, //!< kurz gedrückt ( < 0,8 Sekunden)
	Lang  //!< lang gedrückt ( > 0,8 Sekunden)
	} TTastendruck; //!< Art des Tastendrucks

//! Der TCP-Port für die TelexPhone-Kommunikation
#define TXP_PORT 134

//! Der TCP-Port für die TelexPhone-Rumnummernverwaltung
#define TXP_TLNSERV_PORT 11811

//! Typ eines Teilnehmers
typedef enum 
	{
	Geloescht = 0,
	TxpUrl = 1,
	TxpIP = 2,
	AsciiUrl = 3, //!< Telnet-ähnlich
	AsciiIP = 4,
	TxpDynIP = 5,
		//!< diesen Typ gibt es nur beim Teilnehmer-Server. Bei Abfragen wird der 
		//!< Typ TxpIP gemeldet.
	eMail = 6
	} TTlnAdresseArt;
	

enum { TlnAdresseMax = 40 } ; 
	//!< maximale Länge der Verbindungsadresse.
	//!< Nicht ändern, da auch der Datenaustausch mit dem Teilnehmer-Server
	//!< betroffen wäre (Kompatibilitätsprobleme) (siehe #TTlnDaten)

enum { TlnNameMax = 40 } ; //!< maximale Länge des Teilnehmer-Namens
	//!< Nicht ändern, da auch der Datenaustausch mit dem Teilnehmer-Server
	//!< betroffen wäre (Kompatibilitätsprobleme) (siehe #TTlnDaten)

	
//! Datenstruktur für alle Informationen eines Teilnehmers.
//! Achtung: Bei Änderungen berücksichtigen, dass auch der Datenaustausch 
//! mit dem Teilnehmer-Server über dieses Format läuft.
	
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
	
	
enum { TlnFlag_Lokal = 1 } ; 
	//!< Diese Nummer wird nicht mit anderen Teilnehmern synchronisiert (TODO).
	
enum { TlnFlag_Gesperrt = 2 } ; 
	//!< Diese Nummer darf nicht bei Abfragen der Teilnehmerliste vom 
	//!< Teilnehmer-Server gemeldet werden.


// Daten / Datenstrukturen für Datenaustausch mit Teilnehmer-Server ("Auskunft")
#define TLNSERV_SELBSTAKT 0x01
#define TLNSERV_IPRUECKMELD 0x02
#define TLNSERV_ABFRAGE 0x03
#define TLNSERV_AUSKUNFT_NICHTVERG 0x04
#define TLNSERV_AUSKUNFT_VERSION1 0x05 // definiert das Datenformat
#define TLNSERV_FEHLER 0xFF

typedef union
	{
	char Buf[50]; // 50 Zeichen für Diagnosetexte...
	struct
		{
		uint8_t Code;
		uint8_t DataLen;
		union
			{
			char PureData[1];
			struct 
				{
				uint32_t RufNr;
				uint16_t Pin; //!< Geheimzahl für DynIP-Aktualisierung
				uint16_t Port;
				} SelbstAkt;
			struct
				{
				long EmpfIP;
				} IpRueckm;
			struct 
				{
				uint32_t RufNr;
				} TlnAbfr;
			// für Code == TLNSERV_AUSKUNFT_NICHTVERG keine Daten.
			TTlnDaten TlnAuskunft; // Version 1 = aktuelle Version
			} ;
		} ;
	} TTlnServBuf; 


extern void txp_init( void );


extern TTastendruck Tastendruck;
extern bool WarteTaste();
extern uint8_t KonfigFreigabe(void *pStruct);

extern volatile uint16_t KurzTimerCnt;

typedef uint16_t TKurzTimer;

//! Startet Kurzzeit-Messung.
static inline void StartTimer(TKurzTimer *t)
	{
	uint8_t sreg_tmp = SREG;
	cli();
	*t = KurzTimerCnt;
	SREG = sreg_tmp;
	}
	

//! Aktueller Wert einer Kurzzeit-Messung in zehntel Sekunden.
static inline uint16_t TimerVal(TKurzTimer *t)
	{
	uint16_t res;
	
	uint8_t sreg_tmp = SREG;
	cli();
	res = KurzTimerCnt - (*t); // Überlauf wird absichtlich erwartet!
	SREG = sreg_tmp;
	return res;
	}
		
		
extern volatile TPuffer SendePuffer; 
	
extern volatile TPuffer EmpfPuffer; 

enum { AsciiDruckPufferMax = 1000, HtmlSendeTextMax = 400 } ;
	//!< Puffergrößen für Textpuffer bei HTML-Kommunikation

extern char AsciiDruckPuffer[AsciiDruckPufferMax+4];
		
		
extern int TxpSocketHandle;
	
typedef enum { 
	SocketIdle, //!< Unbenutzt
	SocketOriginate, //!< Ausgehende Verbindung
	SocketAnswer //!< Kommende Verbindung
	} TTxpSocketMode; 
	//!< Speichert Sollzustand der Txp-Verbindung

extern TTxpSocketMode TxpSocketMode;


extern bool TxpSocketAbbauGeplant;

	
typedef enum {
	TelexPhone,		//!< Das eigene Protokoll
	Ascii,			//!< Ascii, also telnet
	POP3,			//!< Mail-Abfrage
	SMTP			//!< Mail-Sendung
	} TTxpSocketProtokoll; 
	//!< Was geht über den Socket 'rüber.
	

extern TKurzTimer TxpSocketAbbruchTimer;
	
extern TTxpSocketProtokoll TxpSocketProtokoll;


enum { SocketInBufMax = 2500 } ; //!< Größe des TCP-Empfangspuffers

extern uint16_t SocketInBufUsed; //!< Benutzter Teil des TCP-Empfangspuffers

extern char SocketInBuf[SocketInBufMax+4]; //!< TCP-Empfangspuffer

	
enum { SocketOutBufMax = 2500 } ; //!< Größe des TCP-Sendepuffers

extern uint16_t SocketOutBufUsed; //!< Benutzter Teil des TCP-Sendepuffers

extern char SocketOutBuf[SocketOutBufMax+4]; //!< TCP-Sendepuffer

extern uint8_t ProtokollPhase;
	

extern void InterneVerbindungBeenden(bool Force);
	
extern void SocketBufInit();

extern void AsciiDruckPufferVerarbeiten();


DEFPORTOUT(RTS, D, 4)

DEFPORTIN(CTS, D, 5)

	
// Was soll LED rot anzeigen?
//---------------------------
//#define LEDROT_EXTEEPROM
//#define LEDROT_SDKARTE
//#define LEDROT_TXPTHREADBLOCK
#define LEDROT_SOCKETERROR
//#define LEDROT_UNERWARTET  // noch ungenutzt

#endif //def TELEXPHONE
	
#endif /* _TXP_H_ */
//@}
