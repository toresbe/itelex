/*! \file iTelex.c \brief Basisfunktionen des iTelex-Systems */
//***************************************************************************
//*            iTelex.c
//*
//****************************************************************************/
///	\ingroup software
///	\defgroup iTelex Hauptfunktion dieser Applikation: Schnittstelle vom Internet
/// zum Fernschreiber
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
#include <avr/pgmspace.h>
#include <avr/version.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <bool.h>

#include "config.h"

#ifdef iTelex

// #include "defports.h"
// #include "bits.h"

#include "hardware/led/led_core.h"
#include "hardware/spi/spi_core.h"
#include "hardware/spi/spi_2.h"

#include "system/net/ip.h"
#include "system/net/tcp.h"
#include "system/net/ethernet.h"
#include "system/net/dns.h"
#include "system/thread/thread.h"
#include "system/config/eeconfig.h"
#include "system/clock/clock.h"
#include "system/clock/delay_x.h"
#include "system/softreset/softreset.h"

#include "apps/httpd/cgibin/cgi-bin.h"
#include "apps/httpd/httpd2_pharse.h"

#include "hardware/timer0/timer0.h"

#include "CgiFormTools.h"
#include "iTelex.h"
#include "TlnBuch.h"
#include "BusKomm.h"
#include "TxP2-Defs.h"
#include "FifoPuffer.h"
#include "BaudotCode.h"
#include "Protokoll.h"
#include "TlnServer.h"
#include "eMail.h"
#include "SvnVersion.h"
#include "StringTab.h"
#include "ConfigNtp.h"
#include "IspMaster.h"


const PROGMEM char SvnVersion_P[] = SVNVERSION;


#ifdef ITELEX_ANSCHLUSS


//! Aktueller Modus. Sollte nur durch ModusWechsel geändert werden.	
TModus Modus;

	
// Die Datem auf dem iTelex-Port haben folgende Struktur:
// - ASCII-Zeichen einschl. WR (CR) und ZL (LF) werden "pur" übertragen.
// - Ansonsten werden Datenblöcke übertragen, die stets aus folgenden Teilen bestehen:
//   * ein Byte Kommandocode (siehe die folgenden Konstanten mit ITELEXC_*)
//   * ein Byte Länge _folgender_ Daten (kann 0 sein).
//   * zugehörige Daten

//! Nur Konstanten-Definitionen.
enum { 
	ITELEXC_NULL = 0x00, //!< Füllzeichen
	ITELEXC_DURCHWAHL = 0x01, //!< Startzeichen, Datenblock enthält ein Byte Durchwahl 
	ITELEXC_BAUDOT_DATA = 0x02, //!< Datenblock mit puren Baudot-Codes
	ITELEXC_ENDE = 0x03, //!< Beabsichtigter Verbindungsabbau.
	ITELEXC_STOP = 0x04, //!< Es können noch Daten angehängt werden. Ursache: Besetzt oder Störung
	// \005 freigehalten für ^E = WerDa.
	ITELEXC_QUITT = 0x06, //!< Meldet Empfangsbereitschaft und Anzahl bereits verarbeiteter Zeichen.
	ITELEXC_VERSION = 0x07, 
		//!< Version der Kommunikation. Originate schlägt vor, Answer bestätigt.
		//!< Erst wenn andere Seite mit gleicher Nummer antwortet, ist Protokollversion abgestimmt.
	ITELEXC_SELBSTANRUF = 0x08, //!< Kennung für einen testweisen Selbst-Anruf.
	ITELEXC_FERNKONFIG = 0x09, 
		//!< Telegramm für Änderungen an Teilnehmer-Einstellungen aus der Ferne.
		//!< Inhalt: 1 Byte Länge (PIN, Kennung, Daten), 2 Byte PIN der Gegenstelle, 1 Byte Kennung ITELEXC_FKK_xxx, x Byte Daten.
		//!< Wenn Daten ein String ist, wird dieser mit abschließendem \\0 übertragen.
	} ;
	
	
/* Mustertelegramme zur Übernahme in FsTelnet (MFC-Programm)

	Text1 = _T("07 01 02 01 01 00");                       // Protokoll und Durchwahl (zwei Datensätze)
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
//!< Die KurzTimer-Basisvariable

volatile uint16_t LangTimerCnt;
//!< Die LangTimer-Basisvariable

#endif //def ITELEX_ANSCHLUSS


volatile uint8_t KurzTimerVorteilerCnt;

volatile uint16_t LangTimerVorteilerCnt;

static TKurzTimer ITelexThreadCheckTimer;
	//!< Prüft, ob die Funktion void itelex_thread() ausreichend häufig aufgerufen wird.

//! Für den Test des Watchdogs.
static TKurzTimer WatchdogTestTimer;

//! Für einen Test des Watchdogs.
static uint16_t WatchdogTestTimerEnde;


volatile static uint16_t TwiLebenszeichenZaehler; 
	//!< Zählt rückwärts die Takte bis zum nächsten Lebenszeichen auf dem TWI-Bus.
	//!< wird während der Verbindung missbraucht zum Zählen der Takte bis zur Pegelwiederholung.
	//!< Kein Timer, da nur lokal in itelex_timerEvent() verwendet und unterschiedliche 
	//!< Ablaufzeiten realisiert werden müssen.

#ifdef ITELEX_ANSCHLUSS
	
static TKurzTimer SchreibPauseTimer;
	//!< Misst die Zeit zwischen zwei vom Endgerät empfangenen Zeichen.
	//!< Sendung wird nach 0,8 Sekunden Pause ausgelöst
	
static TKurzTimer BusQuittTimer;
	//!< Misst die Zeit zwischen nach Einschalt-Aufforderung oder Schluss-Aufforderung.
	//!< Auf Empfang der Quittung wird nur 3 Sekunden gewartet.

	
static TKurzTimer iTelexSocketLebenszeichenTimer;
	//!< Alle 3,5 bis 4 Sekunden ein Lebenszeichen senden...

	
volatile TPuffer SendePuffer; 
	//!< Puffer (mit Baudot-Codes gefüllt) für die Richtung Netz -> Endgerät.
	
volatile TPuffer EmpfPuffer; 
	//!< Puffer (mit Baudot-Codes gefüllt) für die Richtung Endgerät -> Netz.
	
char AsciiDruckPuffer[AsciiDruckPufferMax+4];
	//!< Puffer für zu druckenden Text (Netz -> Endgerät), mit Null abgeschlossen

static char HtmlSendeText[HtmlSendeTextMax+4];
	//!< Puffer für zu Anzuzeigenden Text (Endgerät -> Netz), mit Null abgeschlossen

uint8_t AsciiDruckZiel;
	//!< Endgeräteadresse für spezielle Druckausgaben
	
	
enum { AsciiHilfPufferMax = 100 } ;
	//!< Größe von AsciiDruckPuffer.
	
static char AsciiHilfPuffer[AsciiHilfPufferMax+4];
	//!< Hilfspuffer für Ascii-Druck: Enthält eine Zeile des AsciiPuffers, Umlaute etc. 
	//!< sind übersetzt. Zeilenumbruch wird in diesen Puffer eingebaut.
	
static uint8_t AsciiHilfZeilenanfang;
	//!< Speichert, an welcher Stelle in einer Zeile der Hilfspuffer beginnt, d.h. wieviele
	//!< Zeichen bereits vorher gedruckt worden sind. Erforderlich für automatischen Zeilenumbruch.
	
enum { Druckzeilenlaenge = 68 } ; 
	//!< Zeichen pro Zeile auf den Fernschreibern.
	

static TKurzTimer HtmlDruckspiegelAnzeigeTimer;
	//!< Zeit seit der letzten Anzeige des Druckspiegels. Druckspiegel wird alle 10 Sekunden 
	//!< abgerufen.
	
static TLangTimer BeideRuhigTimer;
	//!< Zeit seit letztem Druck zum oder Schreibempfang vom Endgerät.
	//!< Abschaltung nach 10 Minuten Ruhe.
	

int iTelexSocketHandle;
	//!< Verweis auf Socket für iTelex-Kommunikation. Istzustand. Wenn ungültig, aber #iTelexSocketMode
	//!< ungleich Idle, ist ein kurzzeitiger Verbindungsverlust eingetreten.
	
static int iTelexBlindSocketHandle;
	//!< Verweis auf zweiten Socket für iTelex-Kommunikation, dieser behandelt 
	//!< das Besetztzeichen an den zweiten Anrufer.
	
	
TiTelexSocketMode iTelexSocketMode;
	//!< Speichert Sollzustand der iTelex-Verbindung
	
	
static long iTelexSocketIP;
	//!< Aktueller Verbindungspartner. Bei iTelexSocketMode = SocketAnswer wird
	//!< nach Verbindungsverlust geprüft, ob neu aufgenommene Verbindung wieder
	//!< vom gleichen Anschluss kommt.

static uint16_t iTelexSocketPort;
	//!< Bei ausgehenden Verbindungen der gewünschte Port des Empfängers.

TKurzTimer iTelexSocketAbbruchTimer;
	//!< Nach 30 Sekunden unplanmäßigem Verbindungsverlust wird entgültig abgebaut.

static TKurzTimer iTelexSocketWiederholungVerzoegerung;
	//!< Bei spontanem Verbindungsabbau oder Sendestörung wird 2 Sekunden auf den nächsten 
	//!< Versuch gewartet.
	
	
bool iTelexSocketAbbauGeplant;
	//!< Wird auf true gesetzt, wenn ein Verbindungsabbau bevorsteht.
	//!< Abbau erfolgt immer durch Anrufer. 
	//!< - Wenn true und iTelexSocketMode = SocketOriginate wird Abbau nach letzem Datenblock ausgelöst
	//!< - Wenn true und iTelexSocketMode = SocketAnswer wird nach gemeldetem Verbindungsabbau
	//!<   iTelexSocketMode auf SocketIdle gesetzt und iTelexSocketIP gelöscht.

static TKurzTimer iTelexSocketAbbauVerzoegerung;
	//!< Geht der Verbindungsabbau vom Anrufer aus, ist eine kurze Verzögerung zwischen 
	//!< letzter Sendung und Verbindungsabbau sinnvoll.
	
static TKurzTimer iTelexBlindSocketAbbauVerzoegerung;
	//!< Nach 30 Sekunden unplanmäßigem Verbindungsverlust wird entgültig abgebaut.

	
static uint8_t iTelexSocketProtVersion;
	//!< Vereinbarte Protokollversion der Kommunikation

static uint8_t iTelexSocketProtVersionVorschlag;
	//!< Selbst Vorgeschlagene Protokollversion der Kommunikation

TiTelexSocketProtokoll iTelexSocketProtokoll;
	//!< Was geht über den Socket 'rüber.
	

uint16_t SocketInBufUsed; //!< Benutzter Teil des TCP-Empfangspuffers

char SocketInBuf[SocketInBufMax+4]; //!< TCP-Empfangspuffer

	
uint16_t SocketOutBufUsed; //!< Benutzter Teil des TCP-Sendepuffers

char SocketOutBuf[SocketOutBufMax+4]; //!< TCP-Sendepuffer


uint8_t ProtokollPhase;
	//!< für POP3 und SMTP ein Speicher für den aktuellen Kommunikationsschritt

	
bool TlnBuchOffen;	
	//!< Soll das Teilnehmer-Verzeichnis offen sein oder nicht?
	

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

static bool LangeDienstmeldungen;
	//!< Bei False wird "occ" oder "na" direkt nach dem Wählen ausgegeben. 
	//!< Bei True wird der rufende Fernschreiber ausgeschaltet und eine Diagnosemeldung generiert.
	
static uint8_t DurchwahlTabelle[9];
	//!< Liste der Nebenstellen-Nummern bei kommenden Rufen mit Durchwahl

static TDatumDruckModus DatumDruckModus;
	//!< Wird bei kommenden Verbindungen etwas automatisch gedruckt?
	
static uint32_t Wahlnummer; 
	//!< Momentan gewählte Nummer.
	
static uint8_t Wahlziffern; 
	//!< Anzahl gewählter Ziffern
	
static TTlnDaten GewaehlterTln;
	//!< Datensatz zum aktuell gewählten Teilnehmer. Wird global gespeichert, um 
	//!< Aktualisierungen vom Teilnehmer-Server "einpflegen" zu können.
	
static bool TlnServerAbfrageWiederholungssperre;
	//!< Bewirkt, dass der Teilnehmer-Server nur ein mal je gewählte Ziffer abgefragt wird.

static TKurzTimer WahlPauseTimer;
	//!< Misst die Zeit zwischen zwei vom Endgerät empfangenen Wahlziffern.
	//!< Abfrage des Rufnummern-Servers wird nach 2 Sekunden ausgelöst.
	//!< 5 Sekunden nach Wahl der letzten Ziffer wird auch bei nicht erfolgreicher
	//!< Teilnehmer-Server-Abfrage die Versuchsweise Anwahl des alten Teilnehmers 
	//!< ausgeführt.
	
static bool WahlVerbAufbauNach5SekundenVersuchen;
	//!< Wirkt nur, wenn ohne Rückmeldung eines Teilnehmer-Servers eine nicht
	//!< als lokal im eigenen Teilnehmer-Verzeichnis gespeicherte Nummer gewählt wird.
	
static char NamensucheSuchtext[TlnNameMax];
	//!< Hier wird der zu suchende Namensteil abgelegt. Nur gültig wenn #Modus == #ModNamensucheEingabe

static TTlnListerDat NamenssucheLister;
	//!< Datensatz für das Absuchen des Teilnehmer-Verzeichnisses nach Namensteil.
	//!< Nur gültig bei #Modus == #ModNamensucheAusgabe.
	
static uint32_t NetzRufnummer;
	//!< Rufnummer des eigenen Anschlusses im ip-telex-Netz
	
static uint16_t Geheimzahl;
	//!< Um unberechtigte Fremd-Aktualisierungen zu vermeiden.

static uint16_t NetzPort;
	//!< Gewünschte Port-Nummer im globalen Netz. Kann aus bestimmten Gründen von ITELEX_PORT (134) abweichen.

static long NetzEigeneIP;
	//!< Zurückgemeldete IP-Adresse im globalen Netz.

static TKurzTimer GrundstellungPruefTimer;
	//!< Prüft, ob mit Modus == ModRuhe auch iTelexSocketHandle == NO_SOCKET_USED ist.
	//!< Wenn nicht, wird nach 10 Sekunden eine Meldung generiert.
	
#endif // ITELEX_ANSCHLUSS
	

char TeilnehmerServerAdresse[ANZ_TEILNEHMER_SERVER][TlnAdresseMax];
	//!< URLs oder IPs der Teilnehmer-Server.

long TeilnehmerServerIP[ANZ_TEILNEHMER_SERVER];
	//!< letzte IPs des jeweiligen Teilnehmer-Servers.

static uint8_t TeilnehmerServerFehlerZaehler[ANZ_TEILNEHMER_SERVER];
	//!< Zählt die Probleme bei Verbindungen mit einem Teilnehmer-Server.
	//!< Nach 5 Problemen wird der Server drei Stunden lang nicht benutzt.
	
static bool TeilnehmerServerAlleNichtErreichbar;
	//!< Speichert ob kein Teilnehmer-Server erreichbar ist. Damit die entsprechende Meldung
	//!< nur einmal ausgegeben wird.
	
static TLangTimer TeilnehmerServerSperrTimer[ANZ_TEILNEHMER_SERVER];
	//!< Wird nach dem 5. Problem mit einem Server gestartet. Nächster Verbindungsversuch
	//!< wird erst nach einer Stunde zugelassen.
	
	
enum { KonfigPasswortLen = 10 } ;
	//!< maximale Länge des Passworts für den Zugang zu Konfigurationsdaten.

static char KonfigPasswort[KonfigPasswortLen+1];
	//!< Passwort für den Zugang zu Konfigurationsdaten.
	
static TLangTimer KonfigFreigabeTimer;
	//!< Timer zur Messung der Zeit seit letzter Freigabe bzw. Benutzung von freizugebenden Seiten
	
static bool KonfigFreigabeErteilt;
	//!< Damit Überlauf des #KonfigFreigabeTimer nicht zur Wieder-Freigabe führt.
	
static long KonfigFreigabeFuerIP;
	//!< Die Konfig-Freigabe gilt nur für die IP-Adresse, mit der das Passwort eingegeben wurde.
	
	
TSprache LokaleSprache;
	//!< Sprache für nicht CGI-Seiten
	
static int TeilnehmerServerSocket;
	//!< Handle für ausgehende Verbindungen zum Teilnehmer-Server
	//!< Wird in ZWEI Situationen benutzt: 
	//!< a) Dynamische IP-Aktualisierung
	//!< b) Abfrage einer Teilnehmer-Adresse

static uint8_t AktTlnServerTabI;
	//!< Tabellenindex des aktuell geöffneten Teilnehmer-Servers (#TeilnehmerServerSocket)
	
	
#ifdef ITELEX_ANSCHLUSS

static TLangTimer DynIPAktualisierungTimer;
	//!< Verschiedene Aufgaben bei der Aktualisierung der eigenen IP auf dem Rufnummern-Server.

static uint16_t DynIPAktualisierungEndzeit;
	//!< Wann soll die nächste Aktualisierung sein?
	
static enum {
	DynIP_Inaktiv, //!< Dynamische meldung der eigenen IP-Adresse an Teilnehmer-Server ist nicht eingeschaltet.
	DynIP_Erneuern, //!< Es steht eine Erneuerung der IP-Adresse am Teilnehmer-Server an.
	DynIP_LaeuftGerade, //!< Meldung der IP-Adresse an Teilnehmer-Server läuft gerade.
	DynIP_Bestaetigt, //!< IP wurde von Teilnehmer-Server zurückgemeldet und durch Selbstanruf bestätigt.
	DynIP_Unbestaetigt, //!< IP wurde von Teilnehmer-Server zurückgemeldet und noch nicht durch Selbstanruf bestätigt.
	DynIP_Fehler, //!< Meldung der eigenen IP an Teilnehmer-Server versagt. Erneuerung wird nach Zeitablauf angestoßen.
	} DynIP_Phase;
	

static TKurzTimer SelbstAnrufTimer;
	//!< Verschiedene Aufgaben bei der Aktualisierung der eigenen IP auf dem Rufnummern-Server.

static uint16_t SelbstAnrufPeriode;
	//!< Abstand der Selbstanrufe in Sekunden.
	
static uint16_t SelbstAnrufEndzeit;
	//!< Wann soll der nächste Selbstanruf sein.
	
static int SelbstAnrufSocketHandle;
	//!< Handle für ausgehende Verbindungen zum Selbst-Anruf
	
static uint16_t SelbstAnrufSendePruefwert;
	//!< Wert, der testweise an sich selbst gesendet wurde.
	//!< darf nicht Null sein.
	
static uint16_t SelbstAnrufEmpfangPruefwert;
	//!< Empfangener Wert des Selbstanrufs. Null = noch nichts empfangen.
	
static uint8_t SelbstAnrufFehlerZaehler;
	//!< Zählt die Anzahl der Fehlversuche beim Selbstanruf. Der Dritte führt zu einer Serverabfrage.


static enum {
	SelbstAnrufRuhe,
	SelbstAnrufWarteEmpfang,
	SelbstAnrufSchliessen,
	SelbstAnrufSperre
	} SelbstAnrufPhase;

	
static TZeitUeberwachung SelbstAnrufZeitUeberwachung;
	//!< Überwachung der Dauer des Selbstanrufs.
	
	
#endif // ITELEX_ANSCHLUSS
	
	
//! Sollfrequenz des Aufrufs von itelex_timerEvent()
enum { iTelexTimerFreq = 50 * 10 } ; // 50 Baud mit 10 Takten je Bit	


char DiagnosePuffer[DiagnosePufferMax];
	//!< String für außergewöhnliche Fälle

static uint8_t DiagnosePufferLevel;
	//!< Schweregrad der aktuellen Meldung.

uint8_t	MeldungsdruckLevel;
	//!< Welche Meldungen sollen auf dem angeschlossenen Fernschreiber ausgegeben werden:
	//!< - 0 = keine
	//!< - 1 = interne Fehler die die Funktion beeinträchtigen
	//!< - 2 = wie 1 und externe Fehler
	//!< - 3 = wie 2 und Bedienungsfehler
	//!< - 4 = wie 3 und Statusmeldungen

uint8_t DiagnoseAusgabeZiel;
	//!< Bei Diagnoseausgabe vorzugsweise zu nutzendes Endgerät, das die 
	//!< Ursache des Diagnosetextes durch Endgerät verursacht wurde.
	
	
static struct TIME SystemStartZeit;
	//!< Speichert Uhrzeit des Systemstarts, nur für Diagnose
	
static uint8_t ResetFlags;
	//!< Speichert Ursache des letzten Reset.
	
TTastendruck Tastendruck;
	//!< Speichert, ob und wie lange letztens die Taste an der Platine gedrückt wurde.
	
bool SocketProtokollEin;
	//!< true, wenn alle Änderungen des Socket-Status gespeichert werden sollen.
	//!< \todo Konfigurierbar nicht nur über EEPROM-Variable.


// wird so oft gebraucht...
extern struct TCP_SOCKET TCP_sockettable[];

	
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
	
static inline uint8_t high(uint16_t x)
	{
	return x >> 8;
	}
	
	
void ZeitUeberwachungInit(TZeitUeberwachung *zue, uint16_t aGrenzwert)
	{
	zue->Grenzwert = aGrenzwert;
	zue->Gestartet = false;
	zue->Summe = 0;
	zue->Anzahl = 0;
	zue->AnzUeberGrenze = 0;
	zue->Maximum = 0;
	}
	

void ZeitUeberwachungStart(TZeitUeberwachung *zue)
	{
	StartKurzTimer(&zue->Messung);
	zue->Gestartet = true;
	}
	

bool ZeitUeberwachungEnde(TZeitUeberwachung *zue)
	{
	if (!zue->Gestartet)
		return false;
	uint16_t Mess = KurzTimerVal(&zue->Messung);
	zue->Gestartet = false;
	zue->Summe += Mess;
	zue->Anzahl++;
	if (Mess > zue->Maximum)
		zue->Maximum = Mess;
	if (Mess > zue->Grenzwert)
		{
		zue->AnzUeberGrenze++;
		return true;
		}
	else
		return false;
	}


void ZeitUeberwachungAbbruch(TZeitUeberwachung *zue)
	{
	zue->Gestartet = false;
	}
	

//! Gibt des aktuellen Stand der Zeitueberwachung aus.
//----------------------------------------------------	
//! Ausgabe erfolgt in den #ZeitUeberwachungAusgabePuffer.

char * ZeitUeberwachungAusgabe(TZeitUeberwachung *zue)
	{
	static char ZeitUeberwachungAusgabePuffer[75];
	sprintf_P(ZeitUeberwachungAusgabePuffer, 
			  PSTR("Summe/Anz = %lu/%u  Max = %u  AnzUeberGrenze = %u"), 
			  zue->Summe, zue->Anzahl, zue->Maximum, zue->AnzUeberGrenze);
	return ZeitUeberwachungAusgabePuffer;
	}


static uint16_t ITelexThreadCount; 
	//!< Für Debugging und Zufallsfaktoren
	
	
//! Ermittelt einen Pseudo-Zufallswert aus verschiedenen Systemvariablen
//----------------------------------------------------------------------
//! \param Maske (sollte 2^n-1 sein) begrenzt den Wertebereich.
//! \return Den (zufällig) errechneten Wert.	

uint16_t Zufallswert(uint16_t Maske)
	{
	uint16_t x;
	x = (TwiLebenszeichenZaehler * 23)
		^ (TwiWatchdogCount * 31)
		^ (TwiIsrCount * 47)
		^ (ITelexThreadCount * 83)
		^ (ByteCounter * 101);
	return x & Maske;
	}
	
	
	
//! Speichert einen Diagnosetext.
//------------------------------------------------------
//! \param msg Text aus dem Programmspeicher oder NULL zum Löschen des vorhandenen Textes.
//! \param Level Schweregrad der neuen Meldung. Meldung wird nur gespeichert wenn neuer 
//! Grad schwerwiegender als bestehender Text. Niedrige Nummer ist wichtiger.
//! \retval true wenn die neue Meldung gespeichert wurde.
bool Diagnoseausgabe_P(const char *msg, uint8_t Level)
	{
	if (DiagnosePuffer[0] != '\0' && Level > DiagnosePufferLevel)
		return false; // Neue Meldung ist weniger wichtig als aktuelle.
	if (msg == NULL)
		{
		DiagnosePuffer[0] = '\0';
		DiagnosePufferLevel = 0;
		DiagnoseAusgabeZiel = 0;
		}
	else
		{
		struct TIME Time;
		CLOCK_GetTime(&Time);
		sprintf_P(DiagnosePuffer, 
				  PSTR("%02u.%02u.%04u %02u:%02u:%02u "), 
				  Time.DD, Time.MM, Time.YY, Time.hh, Time.mm, Time.ss);
		strncat_P(DiagnosePuffer, msg, DiagnosePufferMax - strlen(DiagnosePuffer) - 1);
		DiagnosePuffer[DiagnosePufferMax - 1] = '\0';
		DiagnosePufferLevel = Level;
		if (Modus >= ModGehendReserv && Modus < ModKommendVerbVorstufe)
			// also eine gehende Verbindung
			DiagnoseAusgabeZiel = (BusVerbPartner >> 1);
		else
			DiagnoseAusgabeZiel = 0;
		}
	return true;
	}

	
// folgende Funktionen und Variablen sind nur für Debugging der TCP-Ports
// ======================================================================

typedef struct
	{
	uint8_t SocketHandle;
	uint32_t ChangeTime;	
	uint8_t OldState, NewState;
	long IP;
	} TSocketLogEntry;
	
enum { SocketLogMaxEntries = 20 };

TSocketLogEntry SocketLog[SocketLogMaxEntries];

volatile uint8_t SocketLogUsed;

volatile uint8_t LastSocketConnectionState[MAX_TCP_CONNECTIONS];


static void CheckSocketConnectionStateChanges()
	{
	if (!SocketProtokollEin)
		return;
		
	for (uint8_t i = 0 ; i < MAX_TCP_CONNECTIONS ; i++)
		if (TCP_sockettable[i].ConnectionState != LastSocketConnectionState[i])
			{
			struct TIME Time;
			CLOCK_GetTime(&Time); // Todo testen ob man vollte Funktionalität braucht.
			uint8_t SregTemp = SREG;
			cli();
			TSocketLogEntry *se = &SocketLog[SocketLogUsed];
			se->SocketHandle = i;
			se->ChangeTime = Time.time;
			se->OldState = LastSocketConnectionState[i];
			se->NewState = TCP_sockettable[i].ConnectionState;
			se->IP = TCP_sockettable[i].SourceIP;
			if (SocketLogUsed < SocketLogMaxEntries - 1)
				SocketLogUsed++;
			LastSocketConnectionState[i] = se->NewState;
			SREG = SregTemp;
			}
	} // CheckSocketConnectionStateChanges()
	
	
static void PrintSocketConnectionStateChanges()
	{
	if (!SocketProtokollEin)
		return;
		
	struct TIME Time;
	CLOCK_GetTime(&Time); // Todo testen ob man volle Funktionalität braucht.

	for (uint8_t i = 0 ; i < SocketLogUsed ; i++)
		{
		TSocketLogEntry *se = &SocketLog[i];
		ProtokollierenInt_P(PSTR("SocketChange: Handle:%d"), se->SocketHandle);
		for (uint8_t j = 0 ; j < se->SocketHandle ; j++)
			Protokollieren_P(PSTR("    "));
		ProtokollierenInt_P(PSTR(" State:%3u"), se->OldState);
		ProtokollierenInt_P(PSTR("->%3u   IP:"), se->NewState);
		ProtokollierenIPAdr(se->IP);
		if (se->ChangeTime < Time.time)
			ProtokollierenInt_P(PSTR(" (-%u Sekunden)\r\n"), Time.time - se->ChangeTime);
		else
			Protokollieren_P(PSTR("\r\n"));
		uint8_t SregTemp = SREG;
		cli();
		if (i >= SocketLogUsed - 1)
			SocketLogUsed = 0; // wenn letzter gedruckt wurde, wieder Liste leeren.
		SREG = SregTemp;
		}
	}
	

#ifdef ITELEX_ANSCHLUSS
	
//! Initialisiert die serielle Umsetzung 
static void SeriellUmsetzInit(void)
	{
	SerUmEmpfBitNr = SerUmEmpfWarte;
	SerUmSendBitNr = SerUmSendWarte;
	SerUmTickZaehlerEmpf = 10;
	SerUmTickZaehlerSend = 0;
	SendeMark = true;
	}


//! Protokollzeile einleiten.
// ---------------------------
//! Schreibt "iTelex (xxx):" in den Puffer mit xxx = Zykluszaehler von itelex_thread.

static void ProtokollierenITelex()
	{
	ProtokollierenInt_P(PSTR("iTelex(%5u): "), ITelexThreadCount);
	}
	

//! Protokollzeile einfach.
// ---------------------------
//! Schreibt "iTelex (xxx): ttt" in den Puffer mit xxx = Zykluszaehler von itelex_thread und
//! ttt Text aus Programmspeicher.

static void ProtokollierenITelex_P(const char *s)
	{
	ProtokollierenITelex();
	Protokollieren_P(s);
	}
	

//! Prüft, ob im aktuellen Modus ein TWI-Partner verbunden sein müsste.
static bool ModusTwiVerbunden()
	{
	return (Modus == ModGehendReserv 
			|| Modus == ModGehendWaehlen 
			|| Modus == ModGehendVerbunden 
			|| Modus == ModKommendWarteEinQuitt 
			|| Modus == ModKommendVerbunden 
			|| Modus == ModPufferDruckUndSchluss
			|| Modus == ModHtmlChatWarteEinQuitt
			|| Modus == ModHtmlChatVerbunden
			|| Modus == ModMeldungsdruckWarteEinQuitt
			|| Modus == ModNamensucheEingabe
			|| Modus == ModNamensucheServerAbfrage
			|| Modus == ModNamensucheAusgabe);
	}

#endif //def ITELEX_ANSCHLUSS
	
	
volatile static uint8_t Timer0Cnt_Min;
volatile static uint8_t Timer0Cnt_Max;
volatile static uint8_t Timer0Callback_Max;
volatile static uint32_t Timer0CallbackCount; 

//! Timer-Callback-Funktion. Macht seriell-parallel-Umsetzung und umgekehrt.
//--------------------------------------------------------------------------
//! Sendet auf TWI auch die Mark- / Space-Wechsel und die Lebenszeichen.
//! Wird mit Frequenz iTelexTimerFreq aufgerufen.

void itelex_timerEvent(void)
	{
	uint8_t t0c = TCNT0;
	
	if (t0c < Timer0Cnt_Min)
		Timer0Cnt_Min = t0c;
	if (t0c > Timer0Cnt_Max)
		Timer0Cnt_Max = t0c;
	// Statistik über den Zeitverzug...
	
	Timer0CallbackCount++;

	KurzTimerVorteilerCnt++;
	if (KurzTimerVorteilerCnt >= iTelexTimerFreq / KurzTimerFreq)
		{
		KurzTimerCnt++;
		KurzTimerVorteilerCnt = 0;
		LangTimerVorteilerCnt++;
		if (LangTimerVorteilerCnt >= KurzTimerFreq * LangTimerTakt)
			{
			LangTimerVorteilerCnt = 0;
			LangTimerCnt++;
			}
		}
		
	if (WatchdogTestTimerEnde == 0 || KurzTimerVal(&WatchdogTestTimer) < WatchdogTestTimerEnde)
		wdt_reset();
	
	if (KurzTimerVal(&ITelexThreadCheckTimer) > 90 * KurzTimerFreq) // nach 90 Sekunden Reset
		{ 
		ProtokollierenITelex_P(PSTR("! Reset wegen nicht-Aufruf von itelex_thread()\r\n"));
		ProtokollSpeichern(true);
		softreset();
		}
		
#if defined(LEDROT_ITELEXTHREADBLOCK)
	if (KurzTimerVal(&ITelexThreadCheckTimer) > KurzTimerFreq * 5/10) // nach halber Sekunde geht rot an
		LED_on(ROT);
#endif //defined(LEDROT_ITELEXTHREADBLOCK)
		
	TwiWatchdogCount++; 
		
#ifdef ITELEX_ANSCHLUSS
		
	if (Modus == ModKommendVerbunden 
		|| Modus == ModGehendVerbunden 
		|| Modus == ModHtmlChatVerbunden
		|| Modus == ModPufferDruckUndSchluss
		|| Modus == ModNamensucheEingabe
		|| Modus == ModNamensucheServerAbfrage
		|| Modus == ModNamensucheAusgabe)
		{ // ist Verbunden, also Pegel senden und empfangen
		bool NeuMark = true; // wird beim Senden vielleicht noch geändert

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
					SerUmEmpfBitNr = SerUmEmpfFertig; 
						//! \todo Prio 4 nur dann Empfang abschließen, wenn auch ein 
						//! Stop-Bit da war... Entspricht aber nicht den mechanischen Maschinen...

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
			StartKurzTimer(&SchreibPauseTimer);
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
				if (++SerUmTickZaehlerSend >= (SendenBeschleunigen ? 12 : 14)) // 12 und 14 weil ein weiterer Zyklus in SerUmSendWarte verbracht wird.
				//if (++SerUmTickZaehlerSend >= (SendenBeschleunigen ? 12 : 16)) // HACK Wert 16: Simulation zu schneller Sender
				//if (++SerUmTickZaehlerSend >= ((!get_Taste() || SendenBeschleunigen) ? 12 : 14)) // HACK Test des schnellen Sendens....
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
					TwiLebenszeichenZaehler = iTelexTimerFreq * 5/10; // alle 0,5 Sekunden
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

	else if (ModusTwiVerbunden())
		{ // Lebenszeichen regelmäßig senden
		if (TwiLebenszeichenZaehler > 0)
			TwiLebenszeichenZaehler--;
		else
			{ // Lebenszeichen wenn möglich senden
			if ((BusAuftrag == Nichts || BusAuftrag == Fertig) && BusFrei)
				{
				BusSenden(BusLebenszeichen);
				TwiLebenszeichenZaehler = iTelexTimerFreq * 5/10; // alle 0,5 Sekunden
				}
			}
		} // if Modus != Ruhe

#endif //def ITELEX_ANSCHLUSS
		
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
				if (KurzTimerVal(&TasteTimer) >= KurzTimerFreq * 1/20) // 0,5 Zehntel
					{ // ausreichend lang gedrückt
					TasteZustandIntern = TasteEin;
					StartKurzTimer(&TasteTimer);
					
					// Zugang zur Konfiguration erlauben.
					KonfigFreigabeErteilt = true;
					KonfigFreigabeFuerIP = 0; // Zeichen für allgemeine Freigabe.
					StartLangTimer(&KonfigFreigabeTimer);
					}
				}
			else
				{
				StartKurzTimer(&TasteTimer);
				}
			break;

		case TasteEin: 
			if (!get_Taste()) // Gedrückt = LOW!
				{
				if (KurzTimerVal(&TasteTimer) >= KurzTimerFreq * 8/10) // 0,8 Sekunden
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
			StartKurzTimer(&TasteTimer);
			break;
			
		} // switch (TasteZustandIntern)
	
	t0c = TCNT0 - t0c;
	if (t0c > Timer0Callback_Max)
		Timer0Callback_Max = t0c; // Dauer der Funktion itelex_timerEvent()
		
	CheckSocketConnectionStateChanges();
		// hier werden Änderungen der TCP_sockettable nur aufgezeichnet.
		
	} // itelex_timerEvent()


	
#ifdef ITELEX_ANSCHLUSS
	
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
//! - LED-Anzeigen aktualisieren
//! - Status (für TWI-Abfrage) aktualisieren
//! - Puffer-Initialisierung
void ModusWechsel(TModus neu)
	{
	if (neu == Modus)
		return;

	TwiWatchdogCount = 0; // nicht in allen Modi erforderlich, schadet aber auch nicht.
	
	if (ProtokollLevel >= AblaufInfo)
		{
		ProtokollRegelblockStart();
		ProtokollierenITelex();
		ProtokollierenInt_P(PSTR("ModusWechsel von %d "), Modus);
		ProtokollierenInt_P(PSTR("nach %d.\r\n"), neu);
		ProtokollRegelblockEnde();
		}

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
			AsciiDruckZiel = 0;
			AsciiHilfPuffer[0] = '\0';
			AsciiHilfZeilenanfang = 0;
			StartKurzTimer(&SelbstAnrufTimer);
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
			iTelexSocketProtokoll = iTelexProt;
			AsciiDruckPuffer[0] = '\0';
			AsciiHilfPuffer[0] = '\0';
			AsciiHilfZeilenanfang = 0;
			SocketAnzahlZeichenEmpfangen = 0;
			SocketAnzahlZeichenGesendet = 0;
			SocketAnzahlZeichenQuittiert = 0;
			SocketSendeFehlerZaehler = 0;
			StartLangTimer(&BeideRuhigTimer);
			break;
	
		case ModGehendWaehlen:
			Wahlnummer = 0;
			Wahlziffern = 0;
			TlnDatenInit(&GewaehlterTln);
			TlnServerAbfrageWiederholungssperre = true; // wird nach erster Ziffer auf false gesetzt
			StartKurzTimer(&WahlPauseTimer);
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
			StartKurzTimer(&SchreibPauseTimer);
			StartLangTimer(&BeideRuhigTimer);
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
			StartKurzTimer(&BusQuittTimer);
			StartLangTimer(&BeideRuhigTimer);
			break;
	
		case ModKommendVerbunden: 
			SET_BIT_Status(StatBit_FsMeldBetrieb);
			SET_BIT_Status(StatBit_FsMeldEin);
			SET_BIT_Status(StatBit_Verbunden);
			BusEmpfMark = true;
			SendeMark = true;
			StartKurzTimer(&SchreibPauseTimer);
			StartLangTimer(&BeideRuhigTimer);
			break;
	
		case ModPufferDruckUndSchluss: 
			CLR_BIT_Status(StatBit_Verbunden);
			StartLangTimer(&BeideRuhigTimer);
			break;
		
		case ModWarteSchlussQuitt:
			CLR_BIT_Status(StatBit_FsBefBetrieb);
			CLR_BIT_Status(StatBit_FsBefEin);
			CLR_BIT_Status(StatBit_Verbunden);
			StartKurzTimer(&BusQuittTimer);
			break;

		case ModHtmlChatWarteEinQuitt: 
		case ModMeldungsdruckWarteEinQuitt:
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
			BusEmpfMark = true;
			SendeMark = true;
			SeriellUmsetzInit();
			SendenBeschleunigen = false;
			SocketAnzahlZeichenEmpfangen = 0;
			SocketAnzahlZeichenGesendet = 0;
			SocketAnzahlZeichenQuittiert = 0;
			SocketSendeFehlerZaehler = 0;
			StartLangTimer(&BeideRuhigTimer);
			break;
	
		case ModHtmlChatVerbunden: 
			SET_BIT_Status(StatBit_FsMeldBetrieb);
			SET_BIT_Status(StatBit_FsMeldEin);
			SET_BIT_Status(StatBit_Verbunden);
			BusEmpfMark = true;
			SendeMark = true;
			StartKurzTimer(&HtmlDruckspiegelAnzeigeTimer);
			StartLangTimer(&BeideRuhigTimer);
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
			
		case ModNamensucheEingabe:
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
			StartKurzTimer(&SchreibPauseTimer);
			AsciiDruckPuffer[0] = '\0';
			NamensucheSuchtext[0] = '\0';
			break;
			
		case ModNamensucheServerAbfrage:
		    // überhaupt was tun????
			break;
			
		case ModNamensucheAusgabe:
			strcat_P(AsciiDruckPuffer, ISTR(NamensucheErgebnisse, LokaleSprache));
			TlnListerStart(&NamenssucheLister);
			break;

		case ModEmailPOPVerbunden:
			CLR_BIT_Status(StatBit_Frei);
			CLR_BIT_Status(StatBit_LeitungKennung);
			CLR_BIT_Status(StatBit_Verbunden);
			CLR_BIT_Status(StatBit_FsMeldBetrieb);
			CLR_BIT_Status(StatBit_FsMeldEin);
			CLR_BIT_Status(StatBit_FsBefBetrieb);
			CLR_BIT_Status(StatBit_FsBefEin);
			CLR_BIT_Status(StatBit_AngerufenBelegt);
			LED_on(GELB);
			LED_on(GRUEN);
			LED_off(BLAU);
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
	StartKurzTimer(&iTelexSocketLebenszeichenTimer);
	}
	
	
//! Empfangene Daten vom Socket in den SendePuffer schreiben.
//! \retval true, wenn Zeichen gedruckt wird (ausgegeben wird).
static bool SchreibeZeichenInSendePuffer(char c)
	{
	uint8_t Code1, Code2;
	
	if (c == CodeChrWerDa) 
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
			return false;
			}
		} // kein Werda
	}
			

//! Prüft ob von außen kommende Durchwahl erlaubt ist und wendet ggf. auch 
//! DurchwahlTabelle an.
//-----------------------------------------------------------------------			
//! \param aDurchwahl Zeiger auf Variable mit gewählter Durchwahl.
//! Werte von 101 bis 109 sind einstellige Ziffern, siehe WahlZuAdresse()
//! \retval true Durchwahl war zugelassen.
static bool ExternDurchwahlPruefen(uint8_t * aDurchwahl)
	{
	if (ProtokollLevel >= AblaufInfo)
		{
		ProtokollierenITelex();
		ProtokollierenInt_P(PSTR("Durchwahl-Anfrage %u\r\n"), *aDurchwahl);
		}
	
	if (*aDurchwahl == 0)
		return true;
		
	// Prüfen, ob Durchwahl freigegeben ist. Freigegeben ist diese, wenn 
	// zwischen 1 und 9 oder wenn direkte Durchwahl in DurchwahlTabelle enthalten.
	if (*aDurchwahl >= 101 && *aDurchwahl <= 109 && DurchwahlTabelle[*aDurchwahl - 101] > 0)
		{
		*aDurchwahl = DurchwahlTabelle[*aDurchwahl - 101] >> 1;
		if (ProtokollLevel >= AblaufInfo)
			{
			ProtokollierenITelex();
			ProtokollierenInt_P(PSTR("Durchwahl aus Tabelle umgesetzt %u\r\n"), *aDurchwahl);
			}
		return true;
		}
		
	for (uint8_t i = 0 ; i < 9 ; i++)
		if (*aDurchwahl == DurchwahlTabelle[i] >> 1)
			{
			if (ProtokollLevel >= AblaufInfo)
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("Durchwahl in Tabelle gefunden %u\r\n"), *aDurchwahl);
				}
			return true;
			}
	
	return false;
	}
	

//! Bei kommenden Verbindungen aller Art (iTelex, HTML) passenden internen 
//! Empfänger ermitteln und anwählen.
//-----------------------------------------------------------------------------			
//! Setzt als Ergebnis BusVerbPartner und sendet Reservierung an Endgerät.
//! \param aDurchwahl Bevorzugstes Endgerät lokal. 0 bei keiner Bevorzugung.
//! \retval true Ein Endgerät gefunden und erfolgreich Reserviert.
//! \retval false Intern alle in Frage kommenden Endgeräte besetzt.

static bool KommendInternAnwaehlen(uint8_t aDurchwahl)
	{
	int16_t Stat;
	uint8_t TestVerbParter = 0;
	
	// Durchwahl prüfen...
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
//!  - Empfangene Daten in den Socket-Empfangspuffer schreiben
//!  - Daten vom Socket-Empfangspuffer übersetzen in den SendePuffer (zum Endgerät).
//!  - Daten des Empfangspuffers (Endgerät) in den Socket-Sendepuffer übersetzen
//!  - Daten des Socket-Sendepuffers ggf. senden
//!  - Schlusszeichen bearbeiten
//!  - Öffnungs- und Schließanforderung bearbeiten

static void SocketBearbeiten()
	{
	// Neue Verbindungswünsche bearbeiten
	// ----------------------------------
	int NewServerSocket = CheckPortRequest(ITELEX_PORT);
	if (NewServerSocket != NO_SOCKET_USED)
		{
		bool Abweisen = true; // Bei berechtigter kommender Verbindung auf false setzen.

		if (ProtokollLevel >= AblaufInfo)
			{
			ProtokollRegelblockStart();
			ProtokollierenITelex();
			ProtokollierenInt_P(PSTR("Server-Socket #%d geoeffnet von IP "), NewServerSocket);
			ProtokollierenIPAdr(TCP_sockettable[NewServerSocket].SourceIP);
			Protokollieren_P(PSTR(" / MAC "));
			ProtokollierenMAC(TCP_sockettable[NewServerSocket].MACadress);
			ProtokollRegelblockEnde();
			}
		
		if (iTelexSocketMode == SocketIdle)
			{ // neue Verbindung
			if (Modus == ModRuhe)
				{ // ID#102 *************************************************
				// Wenn ja, Startmeldung ausgeben und startzustand herstellen für i2c
				if (ProtokollLevel >= AblaufInfo)
					{
					ProtokollRegelblockStart();
					Protokollieren_P(PSTR(" ...neu ok\r\n"));
					ProtokollRegelblockEnde();
					}
				iTelexSocketHandle = NewServerSocket;
				BusVerbPartner = Hauptstelle; // vorbereitet...
				iTelexSocketIP = TCP_sockettable[iTelexSocketHandle].SourceIP;
				iTelexSocketMode = SocketAnswer;
				iTelexSocketAbbauGeplant = false;
				iTelexSocketProtVersion = 0;
				iTelexSocketProtVersionVorschlag = 0; // auf Gegenvorschlag warten
				iTelexSocketProtokoll = Ascii;
				StartKurzTimer(&iTelexSocketAbbruchTimer);
				StartKurzTimer(&iTelexSocketAbbauVerzoegerung);
				SocketBufInit();
				ModusWechsel(ModKommendVerbVorstufe);
				Abweisen = false;
				}
			else
				{
				Abweisen = true; // anderweitig belegt
				if (ProtokollLevel >= NurFehler)
					Protokollieren_P(PSTR(", anderweitig belegt"));
				}
			#ifdef LEDROT_SOCKETERROR
				LED_off(ROT);
			#endif //def LEDROT_SOCKETERROR
			} // iTelexSocketMode == SocketIdle
			
		else if (iTelexSocketMode == SocketAnswer && iTelexSocketHandle == NO_SOCKET_USED)
			{
			if (iTelexSocketIP == TCP_sockettable[NewServerSocket].SourceIP)
				{
				if (ProtokollLevel >= NurFehler)
					Protokollieren_P(PSTR(" ...Wiederverbindung ok\r\n"));
				iTelexSocketHandle = NewServerSocket;
				StartKurzTimer(&iTelexSocketAbbauVerzoegerung);
				Abweisen = false;
				#ifdef LEDROT_SOCKETERROR
					LED_off(ROT);
				#endif //def LEDROT_SOCKETERROR
				}
			else
				{
				Abweisen = true; 
				if (ProtokollLevel >= NurFehler)
					Protokollieren_P(PSTR(", andere kommende Verbindung besteht!"));
				}
			} // (iTelexSocketMode == SocketAnswer && iTelexSocketIP == NO_SOCKET_USED)
			
		else 
			{ // iTelexSocketMode == SocketOriginate || iTelexSocketHandle bereits belegt
			Abweisen = true; // anderweitig belegt
			if (ProtokollLevel >= NurFehler)
				Protokollieren_P(PSTR(", Verbindung besteht"));
			}
		
		if (Abweisen)
			{ // ID#213 ID#225 ***************************************************
			if (ProtokollLevel >= AblaufInfo)
				Protokollieren_P(PSTR(" * ...ABGEWIESEN, auf Blind-Socket gelegt\r\n" ));
			if (iTelexBlindSocketHandle != NO_SOCKET_USED)
				{
				CloseTCPSocket(iTelexBlindSocketHandle);
				if (ProtokollLevel >= NurFehler)
					Protokollieren_P(PSTR(" ! ...Abweisung auf bereits bestehendem Blind-Socket.\r\n" ));
				}
			iTelexBlindSocketHandle = NewServerSocket;
			StartKurzTimer(&iTelexBlindSocketAbbauVerzoegerung);
			if (Modus == ModDeaktiviert)
				PutSocketData_RPE(iTelexBlindSocketHandle, 5, PSTR("\004\003abs"), FLASH); // 004 = ITELEXC_STOP
				// SendeStopkommando kann nicht benutzt werden, da der Code in den BlindSocket gesendet wird.
			else
				{
				PutSocketData_RPE(iTelexBlindSocketHandle, 5, PSTR("\004\003occ"), FLASH); // 004 = ITELEXC_STOP
				// SendeStopkommando kann nicht benutzt werden, da der Code in den BlindSocket gesendet wird.
				
				Diagnoseausgabe_P(ISTR(AnrufAbgewiesenWegenBesetzt, LokaleSprache), 4);
				}
			}
			
		} // CheckPortRequest(ITELEX_PORT) != NO_SOCKET_USED

	// Verbindungsabbruch durch Gegenseite?
	// --------------------------------------------------
	if (iTelexSocketHandle != NO_SOCKET_USED 
		&& CheckSocketState(iTelexSocketHandle) == SOCKET_NOT_USE
		&& SocketInBufUsed == 0) // Verbindungsabbau verzögern bis Puffer verarbeiet.
		{ // ID#242 ID#342 ID#314 ************************************************
		switch (iTelexSocketProtokoll)
			{
			case iTelexProt:
				if (!iTelexSocketAbbauGeplant)
					{
					if (ProtokollLevel >= NurFehler)
						ProtokollierenITelex_P(PSTR("! Socket wurde von Gegenstelle UNERWARTET geschlossen\r\n" ));
			
					#ifdef LEDROT_SOCKETERROR
						LED_on(ROT);
					#endif //def LEDROT_SOCKETERROR
					break; // des switch
					}
				// sonst weiter mit Ascii, kein break;
				
			case Ascii:
				// oder iTelexProt und AbbauGeplant
				if (ProtokollLevel >= AblaufInfo)
					{
					ProtokollRegelblockStart();
					ProtokollierenITelex_P(PSTR("Socket wurde von Gegenstelle erwartet geschlossen\r\n" ));
					ProtokollRegelblockEnde();
					}
				
				iTelexSocketMode = SocketIdle;
				iTelexSocketIP = 0;
				iTelexSocketAbbauGeplant = false;
				SocketOutBufUsed = 0;
				SocketInBufUsed = 0;
				#ifdef LEDROT_SOCKETERROR
					LED_off(ROT);
				#endif //def LEDROT_SOCKETERROR
				break;
				
			default:
				if (!iTelexSocketAbbauGeplant)
					ProtokollierenITelex_P(PSTR("! Socket wurde von Gegenstelle GETRENNT\r\n" ));
					
				else if (ProtokollLevel >= AblaufInfo)
					ProtokollierenITelex_P(PSTR("Socket wurde von Gegenstelle erwartet geschlossen\r\n" ));
					
				if (Modus == ModEmailPOPVerbunden)
					ModusWechsel(ModWarteGrundstellung);
					
				iTelexSocketMode = SocketIdle;
				iTelexSocketIP = 0;
				iTelexSocketAbbauGeplant = false;
				SocketOutBufUsed = 0;
				SocketInBufUsed = 0;
				break;
				
			}
			
		CloseTCPSocket(iTelexSocketHandle);
		StartKurzTimer(&iTelexSocketAbbruchTimer);
		StartKurzTimer(&iTelexSocketWiederholungVerzoegerung);
		iTelexSocketHandle = NO_SOCKET_USED;
		return; // GGf wieder aufnahme der Verbindung beim nächsten Aufruf dieser funktion...
		}
		
	// soll offene Verbindung geschlossen werden?
	// --------------------------------------------------
	if (iTelexSocketHandle != NO_SOCKET_USED 
		&& iTelexSocketAbbauGeplant 
		&& iTelexSocketMode == SocketOriginate 
		&& KurzTimerVal(&iTelexSocketAbbauVerzoegerung) > KurzTimerFreq * 15/10 // 1,5 Sekunden nach letzter Sendung...
		&& SocketOutBufUsed == 0
		&& SocketInBufUsed == 0)
		{ 
		if (ProtokollLevel >= AblaufInfo)
			ProtokollierenITelex_P(PSTR("Socket wird aktiv geschlossen\r\n" ));
			
		CloseTCPSocket(iTelexSocketHandle);
		iTelexSocketHandle = NO_SOCKET_USED;
		iTelexSocketMode = SocketIdle;
		iTelexSocketIP = 0;
		iTelexSocketAbbauGeplant = false;
		SocketOutBufUsed = 0;
		SocketInBufUsed = 0;
		#ifdef LEDROT_SOCKETERROR
			LED_off(ROT);
		#endif //def LEDROT_SOCKETERROR
		}
		
	// Auf neue Daten testen
	// ---------------------------------
	if (iTelexSocketHandle != NO_SOCKET_USED && SocketInBufUsed < SocketInBufMax)
		{ // Socket offen und Puffer aufnahmefähig
		StartKurzTimer(&iTelexSocketAbbruchTimer);
			// so lange Verbindung aufrecht bleibt Timer auf 0

		int InCount = GetBytesInSocketData(iTelexSocketHandle);
		
		if (SocketInBufUsed + InCount > SocketInBufMax)
			{
			if (ProtokollLevel >= NurFehler) 
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("* Socket Empfang drohender Ueberlauf: Empfang von %d " ), InCount);
				ProtokollierenInt_P(PSTR("limitiert auf %d\r\n" ), SocketInBufMax - SocketInBufUsed);
				}
			InCount = SocketInBufMax - SocketInBufUsed;
			}
			
		if (InCount > 0) 
			{
			int Res = GetSocketData(iTelexSocketHandle, InCount, SocketInBuf + SocketInBufUsed);
			
			if (SocketInBuf[SocketInBufUsed] != ITELEXC_SELBSTANRUF)
				ProtokollRegelblockAbbruch();
				
			if (ProtokollLevel >= DatenDetailliert) // Daten explizit
				{
				ProtokollRegelblockStart();
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("Socket Empfang: (%d/" ), InCount);
				ProtokollierenInt_P(PSTR("%d)"), Res);
				if (Res > 0)
					ProtokollierenPuffer(SocketInBuf + SocketInBufUsed, Res);
				ProtokollierenInt_P(PSTR(" --> BufUsed %u\r\n"), SocketInBufUsed + Res);
				ProtokollRegelblockEnde();
				}		
				
			if (Res > 0)
				{
				SocketInBufUsed += Res;
				}
				
			}

		} // if iTelexSocketHandle != NO_SOCKET_USED 
		
	// ggf Lebenszeichen erzeugen
	// --------------------------
	if (iTelexSocketMode != SocketIdle
		&& iTelexSocketProtokoll == iTelexProt
		&& KurzTimerVal(&iTelexSocketLebenszeichenTimer) >= 4 * KurzTimerFreq
	    && SocketOutBufUsed == 0
		&& SocketSendeFehlerZaehler == 0
		&& !iTelexSocketAbbauGeplant
		&& iTelexSocketHandle != NO_SOCKET_USED)
		{ // alle 4 Sekunden ein Lebenszeichen
		SocketOutBuf[0] = ITELEXC_NULL;
		SocketOutBuf[1] = 0;
		SocketOutBufUsed = 2;
		StartKurzTimer(&iTelexSocketLebenszeichenTimer);
		}

	// Ist ein Neuaufbau der Verbindung erforderlich?
	// ----------------------------------------------
	if (iTelexSocketMode == SocketOriginate 
		&& iTelexSocketHandle == NO_SOCKET_USED 
		&& SocketOutBufUsed != 0
		&& !iTelexSocketAbbauGeplant
		&& KurzTimerVal(&iTelexSocketWiederholungVerzoegerung) > 2 * KurzTimerFreq) // 2 Sekunden verzögerung
		{
		iTelexSocketHandle = Connect2IP(iTelexSocketIP, iTelexSocketPort); 
	 
		if (iTelexSocketHandle == -1)
			{ 
			// Verbindung konnte nicht aufgebaut werden
			if (ProtokollLevel >= NurFehler)
				ProtokollierenITelex_P(PSTR("! Wieder-Oeffnung des Socket VERSAGT.\r\n"));

			iTelexSocketHandle = NO_SOCKET_USED;
			StartKurzTimer(&iTelexSocketWiederholungVerzoegerung);
			return;
			}

		if (ProtokollLevel >= NurFehler)
			{
			ProtokollierenITelex();
			ProtokollierenInt_P(PSTR("Wieder-Oeffnung des Socket #%d erfolgreich.\r\n"), iTelexSocketHandle);
			}

		StartKurzTimer(&iTelexSocketAbbauVerzoegerung);
			
		#ifdef LEDROT_SOCKETERROR
			LED_off(ROT);
		#endif //def LEDROT_SOCKETERROR
			
		}
		
	// Daten ggf. ins Netz senden
	// --------------------------------------------------
	if (iTelexSocketHandle != NO_SOCKET_USED 
		&& SocketOutBufUsed > 0 
		&& (SocketSendeFehlerZaehler == 0 
			|| KurzTimerVal(&iTelexSocketWiederholungVerzoegerung) > KurzTimerFreq * 15/10)) 
			// Nach Sendefehlern höchstens alle 1,5 Sekunden senden.
		{
		uint16_t SendSize;
		
		SendSize = SocketOutBufUsed;
		if (SendSize > MAX_TCP_Datalenght)
			SendSize = MAX_TCP_Datalenght;
			
		int Res = PutSocketData_RPE(iTelexSocketHandle, SendSize, SocketOutBuf, RAM);

		if (ProtokollLevel >= DatenDetailliert)
			{
			ProtokollierenITelex();
			ProtokollierenInt_P(PSTR("Socket Sendung: (%u)" ), SendSize);
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
				if (iTelexSocketAbbauGeplant)
					{
					if (ProtokollLevel >= NurFehler)
						ProtokollierenITelex_P(PSTR("! Mehrfache FEHLER beim Senden ins Netz aber Verbindungsabbau geplant\r\n"));
					CloseTCPSocket(iTelexSocketHandle);
					iTelexSocketHandle = NO_SOCKET_USED;
					iTelexSocketMode = SocketIdle;
					iTelexSocketIP = 0;
					iTelexSocketAbbauGeplant = false;
					SocketOutBufUsed = 0;
					SocketInBufUsed = 0;
					}
				else
					{
					if (ProtokollLevel >= NurFehler)
						ProtokollierenITelex_P(PSTR("! Mehrfache FEHLER beim Senden ins Netz, Socket wird voruebergehend geschlossen\r\n"));
					Diagnoseausgabe_P(ISTR(MehrfacheSendeFehler, LokaleSprache), 2);
		
					CloseTCPSocket(iTelexSocketHandle);
					iTelexSocketHandle = NO_SOCKET_USED;
					}
				} // if (SocketSendeFehlerZaehler >= 10)
			#ifdef LEDROT_SOCKETERROR
				LED_on(ROT);
			#endif //def LEDROT_SOCKETERROR
			StartKurzTimer(&iTelexSocketWiederholungVerzoegerung);
			}
			
		else if (Res < SocketOutBufUsed)
			{ // nicht alles konnte gesendet werden...
			memmove(SocketOutBuf, SocketOutBuf + Res, SocketOutBufUsed - Res);
			SocketOutBufUsed -= Res;
			SocketSendeFehlerZaehler = 0;
			#ifdef LEDROT_SOCKETERROR
				LED_on(ROT);
			#endif //def LEDROT_SOCKETERROR
			StartKurzTimer(&iTelexSocketAbbauVerzoegerung);
			}
			
		else // Puffer erfolgreich vollständig gesendet.
			{
			SocketOutBufUsed = 0;
			SocketSendeFehlerZaehler = 0;
			#ifdef LEDROT_SOCKETERROR
				LED_off(ROT);
			#endif //def LEDROT_SOCKETERROR
			StartKurzTimer(&iTelexSocketAbbauVerzoegerung);
			}
			
		} // if es gibt was zu senden

	// Bei Ascii oder Mail den Timeout auf 'deaktivieren'
	// --------------------------------------------------
	if (ModusTwiVerbunden() 
#ifdef ITELEX_EMAIL
		&& (iTelexSocketProtokoll == Ascii || iTelexSocketProtokoll == POP3 || iTelexSocketProtokoll == SMTP)
#else
		&& (iTelexSocketProtokoll == Ascii)
#endif	
		&& TCP_sockettable[iTelexSocketHandle].ConnectionState == SOCKET_READY)
		TCP_sockettable[iTelexSocketHandle].Timeoutcounter = 30; 
		
	// Abbruch wenn zu lange keine Verbindung besteht...
	// -------------------------------------------------
	if (iTelexSocketMode != SocketIdle
		&& iTelexSocketHandle == NO_SOCKET_USED
		&& KurzTimerVal(&iTelexSocketAbbruchTimer) >= 30 * KurzTimerFreq) // 30 Sekunden.
		{
		if (ProtokollLevel >= NurFehler)
			ProtokollierenITelex_P(PSTR("! ZEITUEBERSCHREITUNG bei Wiederaufnahme der Verbindung\r\n" ));
		Diagnoseausgabe_P(ISTR(ZeitueberschreitungWiederaufnahme, LokaleSprache), 2);
		iTelexSocketMode = SocketIdle;
		iTelexSocketAbbauGeplant = false;
		iTelexSocketIP = 0;
		SocketOutBufUsed = 0;
		SocketInBufUsed = 0;
		#ifdef LEDROT_SOCKETERROR
			LED_off(ROT);
		#endif //def LEDROT_SOCKETERROR
		}

	// Blind-Socket auch bearbeiten
	// ----------------------------
	// 	  Verbindungsabbruch durch Gegenseite?
	if (iTelexBlindSocketHandle != NO_SOCKET_USED 
		&& CheckSocketState(iTelexBlindSocketHandle) == SOCKET_NOT_USE)
		{
		if (ProtokollLevel >= AblaufInfo)
			ProtokollierenITelex_P(PSTR("* iTelex-Blindsocket wurde von Gegenstelle getrennt\r\n" ));
		CloseTCPSocket(iTelexBlindSocketHandle);
		iTelexBlindSocketHandle = NO_SOCKET_USED;
		}
		
	// Blind-Socket Abbau Timeout?
	if (iTelexBlindSocketHandle != NO_SOCKET_USED
		&& KurzTimerVal(&iTelexBlindSocketAbbauVerzoegerung) >= 3 * KurzTimerFreq)
		{
		if (ProtokollLevel >= AblaufInfo)
			ProtokollierenITelex_P(PSTR("* i-Telex-Blindsocket selbst getrennt\r\n" ));
		CloseTCPSocket(iTelexBlindSocketHandle);
		iTelexBlindSocketHandle = NO_SOCKET_USED;
		}
	
	} // SocketBearbeiten()


//! Sende Schluss-Kommando, wenn Gerät noch "online".
//---------------------------------------------------
static void SendeBusKdoSchluss()
	{
	if (BusVerbPartner == 0)
		return;
	int16_t Stat = GetStatus(BusVerbPartner);
	if (Stat < 0)
		{
		ProtokollierenITelex();
		ProtokollierenInt_P(PSTR("! interner Verbindungspartner %u NICHT MEHR ERREICHBAR.\r\n"), BusVerbPartner);
		}
	else if (BIT_IS_SET(Stat, StatBit_Frei))
		{
		ProtokollierenITelex();
		ProtokollierenInt_P(PSTR("! interner Verbindungspartner %u ist SCHON FREI.\r\n"), BusVerbPartner);
		}
	else
		BusSenden(BusKdoSchluss); // dies ist der Gut-Fall.
	}


//! Interner Statuswechsel bei Ende-befehl
//----------------------------------------
//! Aufgerufen bei Socket geschlossen oder anderes Ende-Kommando.
//! \param Force alle schwebenden Zustände (z.B. Wahlzustand) auch zum Abschluss bringen.
void InterneVerbindungBeenden(bool Force)
	{
	if (ProtokollLevel >= AblaufInfo)
		{
		ProtokollierenITelex();
		ProtokollierenInt_P(PSTR("InterneVerbindungBeenden ausgehend von Modus %d\r\n"), Modus);
		}
	
	switch (Modus)
		{
		case ModGehendReserv:
		case ModGehendWaehlen:
		case ModHtmlChatWarteEinQuitt:
		case ModHtmlChatVerbunden:
		case ModMeldungsdruckWarteEinQuitt:
		case ModNamensucheEingabe:
		case ModNamensucheServerAbfrage:
		case ModNamensucheAusgabe:
			if (Force)
				{
				SendeBusKdoSchluss();
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
		
		case ModEmailPOPVerbunden:
			ModusWechsel(ModWarteGrundstellung); 
			break;
			
		case ModKommendVerbVorstufe:
		case ModKommendEinschalten:
		case ModKommendWarteEinQuitt:
			if (ProtokollLevel >= AblaufInfo)
				{
				ProtokollRegelblockStart();
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("Wechsel nach Modus Ruhe (von %d)\r\n"), Modus);
				ProtokollRegelblockEnde();
				}
				
			if (Modus == ModKommendWarteEinQuitt)
				SendeBusKdoSchluss();
				
			ModusWechsel(ModWarteGrundstellung); 
			break;
		
		case ModKommendVerbunden:
		case ModGehendVerbunden:
			if (ProtokollLevel >= AblaufInfo)
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("Wechsel nach Modus PufferDruckUndSchluss (von %d)\r\n"), Modus);
				}
			ModusWechsel(ModPufferDruckUndSchluss);
			break;
		}
	} // InterneVerbindungBeenden()
	

//! Schließt Verbindung nach draußen.
//-----------------------------------
static void ExterneVerbindungBeenden()
	{
	if (iTelexSocketMode != SocketIdle)
		{
		switch (iTelexSocketProtokoll)
			{
			case Ascii:
				iTelexSocketAbbauGeplant = true;
				break;
				
			case iTelexProt:
				iTelexSocketAbbauGeplant = true;
				if (SocketOutBufUsed < SocketOutBufMax - 2)
					{
					SocketOutBuf[SocketOutBufUsed++] = ITELEXC_ENDE;
					SocketOutBuf[SocketOutBufUsed++] = 0;
					}
				break;
				
#ifdef ITELEX_EMAIL
			case SMTP:
				SMTPSchliessen();
				break;
				
			case POP3:
				POP3Abbrechen();
				break;
#endif //def ITELEX_EMAIL
				
			}
		}
						
	// Html-Puffer löschen
	AsciiDruckPuffer[0] = '\0'; // damit es keine neue Einschaltung gibt.
	AsciiHilfPuffer[0] = '\0';
	AsciiHilfZeilenanfang = 0;

	}
	
	
//! Prüft, ob im AsciiPuffer eine Anwahl-Sequenz enthalten ist.
// ------------------------------------------------------------
//! Die Anwahl-Sequenz besteht aus *n* oder *nn*.
//! \param InPufferLoeschen wenn true und Anwahl-Sequenz gültig, wird diese aus dem AsciiPuffer gelöscht.
//! \retval 0 keine Anwahl-Sequenz
//! \retval -1 unvollständige Anwahl-Sequenz
//! \retval >0 Anwahl-Sequenz, dekodierte Anwahl-Nummer als Durchwahl.
static int16_t AnwahlNummerInAsciiPuffer(bool InPufferLoeschen)
	{
	if (AsciiDruckPuffer[0] == '*')
		{
		if (AsciiDruckPuffer[1] >= '0' && AsciiDruckPuffer[1] <= '9')
			{
			uint8_t Ziffer1 = AsciiDruckPuffer[1] - '0';
			if (AsciiDruckPuffer[2] == '*')
				{
				if (InPufferLoeschen)
					memmove(AsciiDruckPuffer, AsciiDruckPuffer + 3, strlen(AsciiDruckPuffer) + 1 - 3);
				return WahlZuAdresse(Ziffer1, 1) >> 1;
				}
			else if (AsciiDruckPuffer[2] >= '0' && AsciiDruckPuffer[2] <= '9')
				{
				uint8_t Ziffer2 = AsciiDruckPuffer[2] - '0';
				if (AsciiDruckPuffer[3] == '*')
					{
					if (InPufferLoeschen)
						memmove(AsciiDruckPuffer, AsciiDruckPuffer + 4, strlen(AsciiDruckPuffer) + 1 - 4);
					return WahlZuAdresse(10 * Ziffer1 + Ziffer2, 2) >> 1;
					}
				else if (AsciiDruckPuffer[3] == '\0')
					return -1;
				else
					return 0;
				}
			else if (AsciiDruckPuffer[2] == '\0')
				return -1;
			else
				return 0;
			} // zweites Zeichen ist Ziffer
		else if (AsciiDruckPuffer[1] == '\0')
			return -1;
		else
			return 0;
		} // erstes Zeichen ist Stern
	else if (AsciiDruckPuffer[0] == '\0')
		return -1;
	else
		return 0;
	} // AnwahlNummerInAsciiPuffer()

	
//! Schreibt ein Ende-Kommando mit Zusatztext in den Socket-Sendepuffer
//---------------------------------------------------------------------
static void SendeStopkommando(PGM_P s)
	{
	uint8_t len = strlen_P(s);
	if (SocketOutBufUsed + 2 + len < SocketOutBufMax - 10) // - 10 = Reserve für wichtige Daten
		{
		SocketOutBuf[SocketOutBufUsed++] = ITELEXC_STOP;
		SocketOutBuf[SocketOutBufUsed++] = len;
		strcpy_P(SocketOutBuf + SocketOutBufUsed, s);
		SocketOutBufUsed += len;
		}
	iTelexSocketAbbauGeplant = true;
	}
	

const char* RufnrServerAdr_P[]; // Vorwärts-Deklaration


/* Liste der Dienstkürzel
=========================
abs	Teilnehmer abwesend, Anlage abgeschaltet
bk	ich trenne
cfm	bitte bestätigen Sie oder ich bestätige
col	bitte vergleichen Sie oder ich vergleich
crv	wie empfangen Sie?
der	gestört
der a	Apparat gestört
der bk	Störung, ich trenne
der cct	Übertragungsweg gestört
der mom	Störung, schalten Sie nicht ab, wir prüfen die Verbindung
df	Sie sind mit dem verlangten Teilnehmer verbunden
dif	verschieden (Differenz)
ya	Sie können übermitteln oder kann ich übermitteln?
inf	Teilnehmer ist vorübergehend nicht zu erreichen, wenden
	Sie sich an die Auskunft.
ltr	Buchstabe(n)
min	Minute(n)
mom	bitte warten
mut	entstellt
na	Verkehr mit diesem Teilnehmer nicht zulässig
nadbo	werden nachforschen und berichten
nc	keine Leitung frei
nch	Telex-Nummer des Teilnehmers hat sich geändert
ndr	keine Störung festgestellt
np	der Verlangte ist nicht oder nicht mehr im Telex-Teilnehmer
nr	geben Sie Ihre Telex-Rufnummer an oder meine Telex-Ruf-
	nummer ist ...
occ	Teilnehmer besetzt
oftug	Kabelverbindung unterbrochen
ofvat	Kabelverbindung wieder hergestellt
ohfop	Verbindung ist wieder hergestellt
ok	einverstanden
p	(mehrmals) stellen Sie bitte Ihre Übermittlung ein. bei Telex-Verbindun-
oder Ziffer	0 gen über Funkwege im Telex-Verzeichnis mit (*) Stern ge-
(mehrmals) 	kennzeichnet, nicht anwendbar
ppr	Papier
r	erhalten
rap	ich werde Sie wieder anwählen
rpt	bitte wiederholen Sie oder ich wiederhole
rpt aa	alles nach ...
rpt ab	alles vor
rpt all	die vollständige Nachricht
rpt wa	Wort nach ...
rpt wb	wort vor ...
svp	bitte
tax	wie hoch sit die Gebühr oder die Gebühr beträgt ...
test msg	bitte senden Sie einen Prüftext
thru	Sie sind mit einem Telex-Platz verbunden
tpr	Fernschreiber
vejar	werden Erforderliches veranlassen
wd	Wort (Wörter) oder Gruppe(n)
wru	wer ist da?
xxxxx	Irrung
yabom	Teilnehmer hat Störung, bitte später anrufen
yabvu	Teilnehmer war mehrmals besetzt
yagym	Teilnehmer ist besetzt, bitte später anrufen
yahet	Teilnehmer ist nicht gestört, bitte rufen Sie wieder
yalim	Telex-Teilnehmer hat neue Rufnummer; neue Rufnummer
	ist ...
yapog	können Teilnehmer nicht erreichen, bitte prüfen Sie nach
	Kennzeichen für das Ende einer Fernschreibnachricht, wenn
	weitere Fernschreibnachrichten noch folgen, bzw. Kennzei-
	chen für das Ende eines Telegramms
+?	Ende der Übermittlung, wollen Sie übermitteln?
++	Kennzeichen für das Ende einer Fernschreib- bzw. Tele-
	gramm-Übermittlung

*/


//! Wird aufgerufen, wenn in der Wählphase ein Fehler auftritt (besetzt oder ähnlich)
//-----------------------------------------------------------------------------------
static void WahlAbbruchMeldung(char *msg)
	{
	if (ProtokollLevel >= AblaufInfo)
		{
		ProtokollierenITelex();
		Protokollieren_P(PSTR("WahlAbbruchMeldung <"));
		ProtokollierenPuffer(msg, strlen(msg));
		Protokollieren_P(PSTR(">\r\n"));
		}
	
	if (LangeDienstmeldungen)
		{
		if (strncmp_P(msg, PSTR("occ"), 3) == 0)
			Diagnoseausgabe_P(ISTR(TeilnehmerBesetzt, LokaleSprache), 3);
		else if (strncmp_P(msg, PSTR("nc"), 2) == 0)
			Diagnoseausgabe_P(ISTR(TeilnehmerNichtErreichbar, LokaleSprache), 2);
		else if (strncmp_P(msg, PSTR("na"), 2) == 0)
			Diagnoseausgabe_P(ISTR(TeilnehmerNichtErlaubt, LokaleSprache), 2);
		else if (strncmp_P(msg, PSTR("der"), 3) == 0)
			Diagnoseausgabe_P(ISTR(TeilnehmerGestoert, LokaleSprache), 2);
		else if (strncmp_P(msg, PSTR("abs"), 3) == 0)
			Diagnoseausgabe_P(ISTR(TeilnehmerAbgeschaltet, LokaleSprache), 2);
		else if (strncmp_P(msg, PSTR("bk"), 2) == 0)
			Diagnoseausgabe_P(ISTR(VerbindungGetrennt, LokaleSprache), 3);
		else			
			{
			if (Diagnoseausgabe_P(ISTR(SonstigeMeldung, LokaleSprache), 2))
				strcat(DiagnosePuffer, msg);
			}
		} // if LangeDienstmeldungen
	else // !LangeDienstmeldungen
		{
		int alen = strlen(AsciiDruckPuffer);
		if (strlen(msg) + alen < AsciiDruckPufferMax - 10) // Daten passen noch in den Puffer...
			{
			strcat_P(AsciiDruckPuffer, PSTR("\r\r\r\n"));
			alen = strlen(AsciiDruckPuffer);
			strcpy(AsciiDruckPuffer + alen, msg);
			strcat_P(AsciiDruckPuffer, PSTR("\r\n"));
			if (Modus == ModGehendWaehlen)
				{ 
				BusSenden(BusQuittEin);
				ModusWechsel(ModPufferDruckUndSchluss);
				}
			}
		} // else !LangeDienstmeldungen
	} // WahlAbbruchMeldung()
	
	
//! Bearbeitet Telegramme mit Daten für Fernkonfiguration
//-------------------------------------------------------
// \retval 0 für ok, sonst verschiedene Fehlernummern.
static uint8_t FernKonfigTelegrammBearbeiten(uint16_t i, uint8_t len)
	{
	enum {
		FKK_TEILNEHMERSERVER1 = 0x11,
		FKK_TEILNEHMERSERVER2 = 0x12,
		FKK_TEILNEHMERSERVER3 = 0x13,
		FKK_DYNAMISCHEIPAKT = 0x15,
		} ;
		
	if (SocketInBufUsed < i + 2 + len)
		return 1; // nicht vollständig.
		
	uint16_t Pin = *((uint16_t *)(SocketInBuf + i + 2));
	
	if (Pin != Geheimzahl)
		return 2; // verboten.
	
	uint8_t FKKennung = SocketInBuf[i+4];
	switch (FKKennung)
		{
		case FKK_TEILNEHMERSERVER1:
		case FKK_TEILNEHMERSERVER2:
		case FKK_TEILNEHMERSERVER3:
			{ // für neue Variable
			uint8_t SvrI = FKKennung - FKK_TEILNEHMERSERVER1;
			strncpy(TeilnehmerServerAdresse[SvrI], SocketInBuf + i + 5, TlnAdresseMax-1);
			TeilnehmerServerIP[SvrI] = 0;
			changeConfig_P(RufnrServerAdr_P[SvrI], TeilnehmerServerAdresse[SvrI]);
			break;
			}
			
		case FKK_DYNAMISCHEIPAKT:
			{
			//! \todo Prio 1 Einschalten und speichern.
			}
			
		default:
			return 3; // falsche ID
		}

	// hier darf man nur bei Erfolg ankommen.
	SocketOutBuf[SocketOutBufUsed++] = ITELEXC_QUITT;
	SocketOutBuf[SocketOutBufUsed++] = 2;
	SocketOutBuf[SocketOutBufUsed++] = 0;
	SocketOutBuf[SocketOutBufUsed++] = FKKennung;
	
	ProtokollierenITelex();
	ProtokollierenInt_P(PSTR("Fernkonfig Kenn=%d ok\r\n"), FKKennung);
	
	return 0;
	}
	

//! Interpretiert empfangene Daten vom Socket und schiebt diese in den 
//! EmpfPuffer.
static void ITelexOderAsciiEmpfangVerarbeiten()
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
				iTelexSocketProtokoll = Ascii;
				int alen = strlen(AsciiDruckPuffer);
				if (alen < AsciiDruckPufferMax-2)
					{
					if (c == '@' && i == SocketInBufUsed - 1) // das Zeichen war ein @ und es war das letzte des Empfangs
						AsciiDruckPuffer[alen] = CodeChrWerDa;
					else
						AsciiDruckPuffer[alen] = c;
					AsciiDruckPuffer[alen+1] = '\0';
					i++;
					SocketAnzahlZeichenEmpfangen++;
					AnzAsciiEmpf++;
					}
				else
					break; // kann nicht mehr verarbeitet werden, also Schleife beenden.
					
				iTelexSocketAbbauGeplant = false;

				if (Modus == ModKommendVerbVorstufe)
					// ID#311 *******************************************************
					{
					if (AnwahlNummerInAsciiPuffer(false) >= 0)
						{
						Durchwahl = AnwahlNummerInAsciiPuffer(true);
						if (ExternDurchwahlPruefen(&Durchwahl))
							ModusWechsel(ModKommendEinschalten); // entweder keine oder gültige Anwahl im Puffer
						else
							{ 
							SendeStopkommando(PSTR("na")); //! \todo Test
							iTelexSocketAbbauGeplant = true;
							ModusWechsel(ModWarteGrundstellung);
							}
							
						}
					// sonst auf weitere Zeichen warten.
					}

				} // ASCII-Zeichen oder WR oder ZL
				
			else if (c == ITELEXC_NULL)
				{ // ignorieren
				i++;
				}
				
			else if (c == ITELEXC_DURCHWAHL)
				{ // ID#312 **************************************************************
				iTelexSocketProtokoll = iTelexProt;
				iTelexSocketAbbauGeplant = false;
				if (Modus == ModKommendVerbVorstufe)
					{
					Durchwahl = SocketInBuf[i+2];
					if (ExternDurchwahlPruefen(&Durchwahl))
						ModusWechsel(ModKommendEinschalten); // entweder keine oder gültige Anwahl im Puffer
					else
						{ 
						SendeStopkommando(PSTR("na")); 
						iTelexSocketAbbauGeplant = true;
						ModusWechsel(ModWarteGrundstellung);
						}
					}
				i += 2 + (uint8_t) SocketInBuf[i+1];
				}
				
			else if (c == ITELEXC_BAUDOT_DATA)
				{ // ID#243 ID#343 ***********************************************************
				iTelexSocketProtokoll = iTelexProt;
				uint8_t len = SocketInBuf[i+1];
				
				if (i + 2 + len <= SocketInBufUsed && PufferAnzahl(&SendePuffer) + len < MaxPuffer)
					{ // Baudot-Code-Block ist vollständig UND noch entsprechend Platz im Sendepuffer
					if (ProtokollLevel == DatenKurz) // Datenmengen protokollieren
						{
						ProtokollierenITelex();
						ProtokollierenInt_P(PSTR("EmpfB %16d" ), PufferAnzahl(&SendePuffer));
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
						if (ProtokollLevel >= AblaufInfo)
							ProtokollierenITelex_P(PSTR("SendenBeschleunigen AUS\r\n"));
							
						SendenBeschleunigen = false;
						}
						
					if (Modus == ModKommendVerbVorstufe)
						// war noch gar nicht eingeschaltet, dann wird es aber Zeit...
						ModusWechsel(ModKommendEinschalten);
					}
				else // Baudot-Code-Block ist noch nicht vollständig UND noch entsprechend Platz im Sendepuffer
					{
					if (!SendenBeschleunigen && (ProtokollLevel >= AblaufInfo))
						ProtokollierenITelex_P(PSTR("SendenBeschleunigen EIN\r\n"));
						
					SendenBeschleunigen = true;
					break; // Daten können momentan nicht verarbeitet werden.
					}
				} // else if (c == ITELEXC_BAUDOT_DATA)

			else if (c == ITELEXC_STOP || c == ITELEXC_ENDE)
				{
				uint8_t len = SocketInBuf[i+1];
				if (i + 2 + len > SocketInBufUsed)
					len = SocketInBufUsed - i - 2;
					// dies ist implementiert, weil alte i-Telex-Versionen einen zu kurzen Datenblock sendeten.

				if (len > 0)
					{
					if (ProtokollLevel >= NurFehler)
						{
						ProtokollierenITelex_P(PSTR("* Abbaubefehl von Gegenstelle:"));
						ProtokollierenPuffer(SocketInBuf + i, 2 + len);
						Protokollieren_P(PSTR("\r\n"));
						}
					}
				else
					{
					if (ProtokollLevel >= AblaufInfo)
						ProtokollierenITelex_P(PSTR("Abbaubefehl von Gegenstelle\r\n"));
					}

				if (len > 0)
					{
					char Buf[11];
					uint8_t msglen = (len < 10) ? len : 10;
					strncpy(Buf, SocketInBuf + i + 2, msglen);
					Buf[msglen] = '\0';
					WahlAbbruchMeldung(Buf);
					}
					
				i += 2 + len;
					
				InterneVerbindungBeenden(true);
				
				iTelexSocketAbbauGeplant = true;
				
				} // c == ITELEXC_STOP oder ITELEXC_ENDE
				
			else if (c == ITELEXC_QUITT)
				{ 
				iTelexSocketProtokoll = iTelexProt;
				uint8_t len = SocketInBuf[i+1];
				if (Modus == ModKommendVerbVorstufe)
					{ // ID#312 **************************************************
					if (KommendInternAnwaehlen(0)) 
						{ 
						BusSenden(BusKdoEin);
						ModusWechsel(ModKommendWarteEinQuitt);
						if (ProtokollLevel >= AblaufInfo)
							{ //! \todo Test
							ProtokollierenITelex();
							ProtokollierenInt_P(PSTR("! spontane ??? Anwahl intern %u "), Durchwahl);
							ProtokollierenInt_P(PSTR("verbunden mit %u\r\n"), BusVerbPartner >> 1);
							}
						}
					else
						{
						SendeStopkommando(PSTR("occ")); //! \todo Prio 1 Testen
						iTelexSocketAbbauGeplant = true;
						ModusWechsel(ModWarteGrundstellung);
						}
					} // if Modus == ModKommendVerbVorstufe
				else if (Modus == ModGehendWaehlen)
					{ // ID#227 **************************************************
					BusSenden(BusQuittEin);
					ModusWechsel(ModGehendVerbunden);
					}
				if (len >= 1)
					SocketAnzahlZeichenQuittiert = (uint8_t) SocketInBuf[i+2];
				i += 2 + len;
				} // c == ITELEXC_QUITT
				
			else if (c == ITELEXC_VERSION)
				{ 
				iTelexSocketProtokoll = iTelexProt;
				uint8_t len = SocketInBuf[i+1];
				if (len >= 1)
					{
					uint8_t ProtVorschlag = SocketInBuf[i+2]; 

					if (ProtokollLevel >= AblaufInfo)
						{
						ProtokollierenITelex();
						ProtokollierenInt_P(PSTR("Protokollversion-Vorschlag %u empfangen\r\n"), ProtVorschlag);
						}

					if (ProtVorschlag == iTelexSocketProtVersionVorschlag)
						{ // Vorschlag ist bestätigt...
						iTelexSocketProtVersion = ProtVorschlag;
						}
					else if (ProtVorschlag > PROTVERSION_AKTUELL)
						{
						iTelexSocketProtVersionVorschlag = PROTVERSION_AKTUELL;
						}
					// hier ggf. weitere Inkompatibilitäten bearbeiten...
					else
						{
						iTelexSocketProtVersionVorschlag = ProtVorschlag;
						}
						
					if (iTelexSocketProtVersion == 0 && SocketOutBufUsed < SocketOutBufMax - 10) // noch nichts festgelegt, also Gegenvorschlag senden.
						{
						SocketOutBuf[SocketOutBufUsed++] = ITELEXC_VERSION;
						SocketOutBuf[SocketOutBufUsed++] = 1 + strlen_P(SvnVersion_P) + 1;
						SocketOutBuf[SocketOutBufUsed++] = iTelexSocketProtVersionVorschlag;
						strcpy_P(SocketOutBuf + SocketOutBufUsed, SvnVersion_P);
						SocketOutBufUsed += strlen_P(SvnVersion_P) + 1;
						
						if (ProtokollLevel >= AblaufInfo)
							{
							ProtokollierenITelex();
							ProtokollierenInt_P(PSTR("Sende Protokollversion-Vorschlag %u\r\n"), iTelexSocketProtVersionVorschlag);
							}
						}
					} // len >= 1
					
				// Hinweis: Die Bytes 2 bis x enthalten noch den SVN-Versionsnummer-String.
					
				i += 2 + len;
				} // c == ITELEXC_VERSION

			else if (c == ITELEXC_SELBSTANRUF)
				{ 
				uint8_t len = SocketInBuf[i+1];
				if (len >= 2 && SelbstAnrufPhase == SelbstAnrufWarteEmpfang)
					{
					SelbstAnrufEmpfangPruefwert = (SocketInBuf[i+2] << 8) + SocketInBuf[i+3]; // erst high, dann low
					// Zur Beschleunigung baut ausnahmsweise der Empfänger die Verbindung ab.
					CloseTCPSocket(iTelexSocketHandle);
					iTelexSocketHandle = NO_SOCKET_USED;
					iTelexSocketMode = SocketIdle;
					iTelexSocketIP = 0;
					iTelexSocketAbbauGeplant = false;
					SocketOutBufUsed = 0;
					SocketInBufUsed = 0;
					ModusWechsel(ModWarteGrundstellung);
					}
				i += 2 + len;
				}
				
			else if (c == ITELEXC_FERNKONFIG)
				{ 
				uint8_t len = SocketInBuf[i+1];
				uint8_t Res = FernKonfigTelegrammBearbeiten(i, len);
					// 0 = ok, anderes = Fehlercode

				if (Res != 0)
					{
					SendeStopkommando(PSTR("fernkonferr"));
					ProtokollierenITelex();
					ProtokollierenInt_P(PSTR("Fernkonfig !Fehler Code=%d"), Res);
					ProtokollierenInt_P(PSTR(" Len=%d"), len);
					ProtokollierenInt_P(PSTR(" Kenn=%02X\r\n"), (len >= 4) ? SocketInBuf[i+4] : 0);
					}
					
				i += 2 + len;
				}
				
			else 
				{ // unbekannter Code --> ignorieren EINSCHLIEßLICH Daten
				// ID#245 ID#313 ID#346 ********************************************************
				if (iTelexSocketProtokoll == iTelexProt)
					i += 2 + (uint8_t) SocketInBuf[i+1];
				else
					i++;
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
			
		if (ProtokollLevel == DatenKurz && AnzAsciiEmpf > 0) // Datenmengen protokollieren
			{
			ProtokollierenITelex();
			ProtokollierenInt_P(PSTR("EmpfA %16d" ), 
				PufferAnzahl(&SendePuffer) + strlen(AsciiDruckPuffer) + strlen(AsciiHilfPuffer) );
			ProtokollierenInt_P(PSTR("%4d"), AnzAsciiEmpf);
			ProtokollierenInt_P(PSTR("%4d\r\n"), low(SocketAnzahlZeichenEmpfangen));
			// Zahlen: unverarbeitete Daten / neue Daten / Daten insgesamt
			}
			
		if (iTelexSocketProtokoll == Ascii && SocketInBufUsed > 0 && SocketInBuf[SocketInBufUsed-1] == '@')
			SocketInBuf[SocketInBufUsed-1] = CodeChrWerDa;
			// am Ende des Empfangs ein @ durch Werda ersetzen.
			
		} // if GetBytesInSocketData > 0
	} // ITelexOderAsciiEmpfangVerarbeiten()

	
//! Wandelt Daten aus dem SendePuffer um.
//---------------------------------------
//! Bearbeitet auch Statusänderungen.
static void ITelexDatenVerarbeiten()
	{
	ITelexOderAsciiEmpfangVerarbeiten();
	
	// vom Endgerät empfangene Daten übersetzen
	// --------------------------------------------------
	// es wird gesendet, wenn es was zu senden gibt 
	// UND      a) es viel zu senden gibt 
	//     ODER b) 0,5 sekunden nicht getippt wurde
	//     ODER c) Alles was bisher gesendet wurde schon verarbeitet ist.
	int InCount = PufferAnzahl(&EmpfPuffer);
	
	if (InCount > 0 || !PufferLeer(&SendePuffer))
		StartLangTimer(&BeideRuhigTimer);
	
	if (InCount > 20
	    || (InCount > 0 && ((KurzTimerVal(&SchreibPauseTimer) >= KurzTimerFreq * 8/10) // 0,8 Sekunden Tipp-Pause
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
			if (len > 255)
				len = 245;
				
			if (ProtokollLevel == DatenKurz) // Datenmengen
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("SendB %4d" ), (uint8_t)(low(SocketAnzahlZeichenGesendet) - SocketAnzahlZeichenQuittiert));
				ProtokollierenInt_P(PSTR("%4d"), len);
				ProtokollierenInt_P(PSTR("%4d\r\n"), low(SocketAnzahlZeichenGesendet) + len);
				}
				
			SocketOutBuf[SocketOutBufUsed++] = ITELEXC_BAUDOT_DATA;
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
		&& (SocketSendeQuittung 
			|| KurzTimerVal(&iTelexSocketLebenszeichenTimer) > KurzTimerFreq * 35/10)
		&& !iTelexSocketAbbauGeplant
		&& SocketSendeFehlerZaehler == 0
		&& SocketOutBufUsed < SocketOutBufMax - 4 - 10 // - 10 = Reserve für wichtige Daten
		&& iTelexSocketHandle != NO_SOCKET_USED)
		{
		SocketOutBuf[SocketOutBufUsed++] = ITELEXC_QUITT;
		SocketOutBuf[SocketOutBufUsed++] = 1;
		SocketOutBuf[SocketOutBufUsed++] = 
			(uint8_t) (low(SocketAnzahlZeichenEmpfangen) - PufferAnzahl(&SendePuffer));
		SocketSendeQuittung = false;
		StartKurzTimer(&iTelexSocketLebenszeichenTimer);
		}

	} // ITelexDatenVerarbeiten()
	

//! Wandelt Daten aus dem SendePuffer um.
//---------------------------------------
//! Bearbeitet auch Statusänderungen.
static void AsciiDatenVerarbeiten()
	{
	ITelexOderAsciiEmpfangVerarbeiten();

	// vom Endgerät empfangene Daten übersetzen
	// --------------------------------------------------
	// es wird gesendet, wenn es was zu senden gibt 
	// UND      a) es viel zu senden gibt 
	//     ODER b) 0,5 sekunden nicht getippt wurde.
	int InCount = PufferAnzahl(&EmpfPuffer);
	
	if (InCount > 0 || !PufferLeer(&SendePuffer))
		StartLangTimer(&BeideRuhigTimer);
	
	if (InCount > 20
	    || (InCount > 0 && KurzTimerVal(&SchreibPauseTimer) >= KurzTimerFreq * 8/10) // 0,8 Sekunden Tipp-Pause
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

		if (ProtokollLevel == DatenKurz && ProtAnz > 0) // Datenmengen
			{
			ProtokollierenITelex();
			ProtokollierenInt_P(PSTR("SendA %4d" ), (uint8_t)(low(SocketAnzahlZeichenGesendet) - SocketAnzahlZeichenQuittiert));
			ProtokollierenInt_P(PSTR("%4d"), ProtAnz);
			ProtokollierenInt_P(PSTR("%4d\r\n"), low(SocketAnzahlZeichenGesendet)); // wurde schon erhöht
			}
			
		StartKurzTimer(&iTelexSocketLebenszeichenTimer);
		}

	} // AsciiDatenVerarbeiten()
	

//! Schiebt ein Zeichen in den Anzeigepuffer für HTML-Betrieb.
//------------------------------------------------------------
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
	
#endif // ITELEX_ANSCHLUSS


// Obergrenze für den Fehlerzähler.
enum { TeilnehmerServerFehlerZaehlerGrenze = 6 * 2 } ;
	// * 2 wegen "doppelter" Zählung in TeilnehmerServerFehlerSpeichern().

//! Prüft, ob ein Socket benutzbar ist und nicht wegen Fehlern gesperrt ist
//-------------------------------------------------------------------------
//! \param ServerI Index-Nummer des Teilnehmer-Servers (0 bis ANZ_TEILNEHMER_SERVER-1)
//! \param Grund Grund des Öffnens des Teilnehmer-Servers, nur für Protokollierung. NULL verhindert Protokollierung.
//! \retval true, wenn Verbindung vorraussichtlich erfolgreich sein wird.
bool TeilnehmerServerVerfuegbar(int ServerI, PGM_P Grund)
	{
	if (TeilnehmerServerAdresse[ServerI][0] == '\0')
		return false;
		
	if (TeilnehmerServerFehlerZaehler[ServerI] >= TeilnehmerServerFehlerZaehlerGrenze)
		{
		if (LangTimerVal(&TeilnehmerServerSperrTimer[ServerI]) <= (TeilnehmerServerAlleNichtErreichbar ? 20 * LangTimerMinuteFaktor : 180 * LangTimerMinuteFaktor))
			// Wenn Server offensichtlich dauerhaft nicht erreicht, alle 3 Stunden probieren, 
			// außer wenn alle Server nicht erreichbar, dann alle 20 Minuten probieren
			{
			/* Müllt total den Speicher zu...
			if (ProtokollLevelTlnServ >= AblaufInfo)
				{
				ProtokollierenITelex_P(PSTR("* Teilnehmer-Server "));
				Protokollieren(TeilnehmerServerAdresse[ServerI]); 
				Protokollieren_P(PSTR(" wegen Fehlern noch gesperrt (Oeffnung fuer "));
				Protokollieren_P(Grund);
				Protokollieren_P(PSTR(")\r\n"));
				}
			*/
			return false;
			}
		}

	else if (TeilnehmerServerFehlerZaehler[ServerI] >= TeilnehmerServerFehlerZaehlerGrenze / 2)
		{
		if (LangTimerVal(&TeilnehmerServerSperrTimer[ServerI]) <= 5 * LangTimerMinuteFaktor)
			// Wenn dich Fehlerzähler des Servers kritischer Grenze nähert, nur noch seltener probieren
			{
			/* Müllt total den Speicher zu...
			if (ProtokollLevelTlnServ >= AblaufInfo)
				{
				ProtokollierenITelex_P(PSTR("* Teilnehmer-Server "));
				Protokollieren(TeilnehmerServerAdresse[ServerI]); 
				Protokollieren_P(PSTR(" wegen Fehlern noch gesperrt (Oeffnung fuer "));
				Protokollieren_P(Grund);
				Protokollieren_P(PSTR(")\r\n"));
				}
			*/
			return false;
			}
		}
		
	return true;
	}
	

//! Verbindung zu einem konkreten Teilnehmer-Server herstellen.
// ------------------------------------------------------------
//! \param ServerI Index-Nummer des Teilnehmer-Servers (0 bis ANZ_TEILNEHMER_SERVER-1)
//! \param Grund Grund des Öffnens des Teilnehmer-Servers, nur für Protokollierung
//! \return Socket-Handle bei Erfolg, -1 bei Fehler oder bei "weigerung".
int TeilnehmerServerSocketOeffnen1(int ServerI, PGM_P Grund)
	{
	int Res;

	if (!TeilnehmerServerVerfuegbar(ServerI, Grund))
		return -1;
		
	if (TeilnehmerServerAdresse[ServerI][0] == '\0')
		return -1;
		
	StartLangTimer(&TeilnehmerServerSperrTimer[ServerI]);
		
	TeilnehmerServerIP[ServerI] = strtoip(TeilnehmerServerAdresse[ServerI]);	// Annahme: eine IP-Adresse angegeben
	
	if (TeilnehmerServerIP[ServerI] == 0) // ist es doch eine Hostname?
		TeilnehmerServerIP[ServerI] = DNS_ResolveName(TeilnehmerServerAdresse[ServerI]); 
		
	if (TeilnehmerServerIP[ServerI] != -1)
		{
		Res = Connect2IP(TeilnehmerServerIP[ServerI], ITELEX_TLNSERV_PORT);
		if (Res != -1)
			{
			if (ProtokollLevelTlnServ >= AblaufInfo)
				{
				ProtokollierenITelex_P(PSTR("Verbindung an Teilnehmer-Server "));
				Protokollieren(TeilnehmerServerAdresse[ServerI]); 
				ProtokollierenInt_P(PSTR(" Socket #%d hergestellt fuer "), Res);
				Protokollieren_P(Grund);
				Protokollieren_P(PSTR(".\r\n"));
				}

			TeilnehmerServerErfolgSpeichern(ServerI);
			
			if (TeilnehmerServerAlleNichtErreichbar)
				Diagnoseausgabe_P(ISTR(TeilnehmerServerWiederErreichbar, LokaleSprache), 1);	
				
			TeilnehmerServerAlleNichtErreichbar = false;
				
			return Res;
			}
			
		if (ProtokollLevelTlnServ >= NurFehler)
			{
			ProtokollierenITelex_P(PSTR("! Verbindungsversuch an Teilnehmer-Server "));
			Protokollieren(TeilnehmerServerAdresse[ServerI]); 
			Protokollieren_P(PSTR(" GESCHEITERT fuer "));
			Protokollieren_P(Grund);
			Protokollieren_P(PSTR(".\r\n"));
			}

		TeilnehmerServerFehlerSpeichern(ServerI);
			
		return -1;
		}
	else
		{
		ProtokollierenITelex_P(PSTR("! Teilnehmer-Server "));
		Protokollieren(TeilnehmerServerAdresse[ServerI]); 
		Protokollieren_P(PSTR(" IP nicht bekannt (Oeffnung fuer "));
		Protokollieren_P(Grund);
		Protokollieren_P(PSTR(").\r\n"));
		
		TeilnehmerServerFehlerSpeichern(ServerI);
		return -1;
		}
	}
	

//! Speichern von Fehlern an Teilnehmer-Servern.
// -------------------------------------------------------------------
//! Nur Aufrufen, wenn Öffnen erfolgreich war, dann aber kritische Fehler
//! aufgetreten sind, die sich voraussichtlich wiederholen.
//! \param ServerI Tabellenindex des Servers.

void TeilnehmerServerFehlerSpeichern(int ServerI)
	{
	if (ServerI >= 0 && ServerI < ANZ_TEILNEHMER_SERVER)
		{
		StartLangTimer(&TeilnehmerServerSperrTimer[ServerI]);
		if (TeilnehmerServerFehlerZaehler[ServerI] < TeilnehmerServerFehlerZaehlerGrenze)
			TeilnehmerServerFehlerZaehler[ServerI] += 2; 
			// Plus 2, da bei jedem erfolgreichen öffnen der Zähler wieder um 1 
			// dekrementiert wird. Damit Fehler, die wiederholbar erst bei der Datenübertragung
			// auftreten registriert werden, muss diese dekrementierung "aufgeholt"
			// werden.
		ProtokollierenITelex_P(PSTR("Teilnehmer-Server "));
		Protokollieren(TeilnehmerServerAdresse[ServerI]); 
		ProtokollierenInt_P(PSTR(" Fehlerzaehler erhoeht auf %d\r\n"), TeilnehmerServerFehlerZaehler[ServerI]);
		}
	}
	

//! Speichern von Erfolgsmeldungen zu Teilnehmer-Servern.
// -------------------------------------------------------------------
//! \param ServerI Tabellenindex des Servers.

void TeilnehmerServerErfolgSpeichern(int ServerI)
	{
	if (ServerI >= 0 && ServerI < ANZ_TEILNEHMER_SERVER && TeilnehmerServerFehlerZaehler[ServerI] > 0)
		{
		TeilnehmerServerFehlerZaehler[ServerI]--; 
		ProtokollierenITelex_P(PSTR("Teilnehmer-Server "));
		Protokollieren(TeilnehmerServerAdresse[ServerI]); 
		ProtokollierenInt_P(PSTR(" Fehlerzaehler verringert auf %d\r\n"), TeilnehmerServerFehlerZaehler[ServerI]);
		}
	}
	
	
//! Verbindung zu einem der gespeicherten Teilnehmer-Server herstellen.
// -------------------------------------------------------------------
//! \param Grund Grund des Öffnens des Teilnehmer-Servers, nur für Protokollierung
//! \return Erfolgreich

bool TeilnehmerServerSocketOeffnen(PGM_P Grund)
	{
	if (TeilnehmerServerSocket != NO_SOCKET_USED)
		return true; // ist schon offen...
		
	int ServerI;
	
	for (ServerI = 0 ; ServerI < ANZ_TEILNEHMER_SERVER ; ServerI++)
		{
		TeilnehmerServerSocket = TeilnehmerServerSocketOeffnen1(ServerI, Grund);
		if (TeilnehmerServerSocket != -1)
			{
			AktTlnServerTabI = ServerI;
			return true; // Erfolg.
			}
		} // for ServerI
		
	TeilnehmerServerSocket = NO_SOCKET_USED;
	if (!TeilnehmerServerAlleNichtErreichbar)
		Diagnoseausgabe_P(ISTR(KeinTeilnehmerServerErreichbar, LokaleSprache), 1);	
	TeilnehmerServerAlleNichtErreichbar = true;
	return false;
	} // TeilnehmerServerSocketOeffnen()


#ifdef ITELEX_ANSCHLUSS

//! Versucht den Verbindungsaufbau zu einem vorhandenen Eintrag im eigenen Teilnehmerverzeichnis
// ---------------------------------------------------------------------------------------------
//! \retval 0 Erfolgreich
//! \retval 1 Verbindung konnte nicht hergestellt werden.
//! \retval 2 Keine gültigen Daten im Datensatz oder keine Verbindung zum e-Mail-Server.

uint8_t Verbindungsaufbau(TTlnDaten* td)
	{
	switch (td->AdrArt)
		{
		case iTelexIP:
		case iTelexDynIP:
		case AsciiIP:
			if (ProtokollLevel >= AblaufInfo)
				{
				ProtokollierenITelex_P(PSTR("Verbindungsaufbau zu IP "));
				ProtokollierenIPAdr(td->IPAdr);
				ProtokollierenInt_P(PSTR(" Port %u\r\n"), td->Port);
				}
			iTelexSocketIP = td->IPAdr;
			iTelexSocketPort = td->Port;
			// Mode wird nach erfolgreichem Öffnen gesetzt.
			break;
			
		case iTelexHostname:
		case AsciiHostname:
			td->IPAdr = DNS_ResolveName(td->Adresse); 
				// IPAdr wird 'missbraucht' aber nicht gespeichert
			if (td->IPAdr != -1)
				{
				if (ProtokollLevel >= AblaufInfo)
					{
					ProtokollierenITelex_P(PSTR("Verbindungsaufbau zu Hostname "));
					Protokollieren(td->Adresse);
					Protokollieren_P(PSTR(" = "));
					ProtokollierenIPAdr(td->IPAdr);
					ProtokollierenInt_P(PSTR(" Port %u\r\n"), td->Port);
					}									
				iTelexSocketIP = td->IPAdr;
				iTelexSocketPort = td->Port;
				// Mode wird nach erfolgreichem Öffnen gesetzt.
				}
			else
				{
				if (ProtokollLevel >= NurFehler)
					{
					ProtokollierenITelex_P(PSTR("! IP zu Hostname "));
					Protokollieren(td->Adresse);
					Protokollieren_P(PSTR(" nicht gefunden\r\n"));
					}
				return 1;
				}
			break;
			
		case eMail:
#ifdef ITELEX_EMAIL
			if (SMTPOeffnen(td->Adresse))
				{
				if (ProtokollLevel >= AblaufInfo)
					{
					ProtokollierenITelex();
					ProtokollierenInt_P(PSTR("Client-Socket #%d SMTP erfolgreich geoeffnet -> Einschalt-Quittung an TWI\r\n"), iTelexSocketHandle);
					}
					
				BusSenden(BusQuittEin); 
					//! \todo prüfen, ob der Start des FS nicht auch an das Ende der Authentifizierung am Email Server verschoben werden kann.
					// Dann aber auch Testen, was bei voerzeitigem Abbruch der Verbindung passiert.
				ModusWechsel(ModGehendVerbunden);
				return 0; // gut
				}
			else
				{
				Diagnoseausgabe_P(ISTR(KeineVerbindungZumMailServerAusgang, LokaleSprache), 2);
				return 2; // schlecht
				}
#else
			ProtokollierenITelex_P(PSTR("! eMail nicht unterstuetzt\r\n" ));
			Diagnoseausgabe_P(ISTR(MailNichtInDieserVersion, LokaleSprache), 3);
			return 2;
#endif //ndef ITELEX_EMAIL		
			
		default:
			if (ProtokollLevel >= NurFehler)
				ProtokollierenITelex_P(PSTR("* Teilnehmer ist GELOESCHT\r\n" ));
				
			return 2;
			
		}

	// und hier wird geöffet...
	iTelexSocketHandle = Connect2IP(iTelexSocketIP, iTelexSocketPort); 
	 
	if (iTelexSocketHandle == -1)
		{ // ID#223 ********************************************
		// Verbindung konnte nicht aufgebaut werden
		if (ProtokollLevel >= NurFehler)
			ProtokollierenITelex_P(PSTR("! Client-Socket konnte nicht erstmalig geoeffnet werden\r\n"));
			
		iTelexSocketHandle = NO_SOCKET_USED;
		iTelexSocketMode = SocketIdle;
		return 1;
		}

	SocketBufInit();
	
	iTelexSocketMode = SocketOriginate;
	iTelexSocketAbbauGeplant = false;
	iTelexSocketProtVersion = 0;
	iTelexSocketProtVersionVorschlag = PROTVERSION_AKTUELL;
	
	StartKurzTimer(&iTelexSocketAbbruchTimer);
	StartKurzTimer(&iTelexSocketAbbauVerzoegerung);
		
	Diagnoseausgabe_P(NULL, 2); 
		// ggf Meldung "nicht erreichbar" wieder löschen.
		// dies kann eintreten, wenn zuerst verbindung zu einer alten IP-Adresse 
		// vergeblich versucht wird, dann die aktualisierung vom Teilnehmer-Server
		// kommt und dann die Verbindung erfolgreich hergestellt wird.
	
	if (td->AdrArt == AsciiHostname || td->AdrArt == AsciiIP)
		{ // ID#226 *********************************************
		BusSenden(BusQuittEin);
		if (ProtokollLevel >= AblaufInfo)
			{
			ProtokollierenITelex();
			ProtokollierenInt_P(PSTR("Client-Socket #%d Ascii erfolgreich geoeffnet -> Einschalt-Quittung an TWI\r\n"), iTelexSocketHandle);
			}
			
		ModusWechsel(ModGehendVerbunden);
		iTelexSocketProtokoll = Ascii;
		return 0;
		}
	else // iTelexHostname oder iTelexIP
		{ // ID#222 ********************************************
		if (ProtokollLevel >= AblaufInfo)
			{
			ProtokollierenITelex();
			ProtokollierenInt_P(PSTR("Client-Socket #%d iTelex erfolgreich geoeffnet "), iTelexSocketHandle);
			ProtokollierenInt_P(PSTR("-> sende Durchwahl %u"), td->Durchwahl);
			ProtokollierenInt_P(PSTR(" und Version %u\r\n"), iTelexSocketProtVersionVorschlag);
			}

		iTelexSocketProtokoll = iTelexProt;
		
		SocketOutBuf[SocketOutBufUsed++] = ITELEXC_VERSION;
		SocketOutBuf[SocketOutBufUsed++] = 1 + strlen_P(SvnVersion_P) + 1;
		SocketOutBuf[SocketOutBufUsed++] = iTelexSocketProtVersionVorschlag;
		strcpy_P(SocketOutBuf + SocketOutBufUsed, SvnVersion_P);
		SocketOutBufUsed += strlen_P(SvnVersion_P) + 1;
		SocketOutBuf[SocketOutBufUsed++] = ITELEXC_DURCHWAHL;
		SocketOutBuf[SocketOutBufUsed++] = 1;
		SocketOutBuf[SocketOutBufUsed++] = td->Durchwahl;
		
		return 0;
		}
		
	} // Verbindungsaufbau()


//! Startet die Abfrage einer Rufnummer beim Teilnehmer-Server.
//-------------------------------------------------------------
static void RufnummerBeiTlnServerAbfragen()
	{
	TlnServerAbfrageWiederholungssperre = true;
	
	if (ProtokollLevel >= AblaufInfo)
		ProtokollierenITelex_P(PSTR("Abfrage bei Teilnehmer-Servern\r\n" ));

	if (TeilnehmerServerSocketOeffnen(PSTR("Rufnummer-Abfrage")))
		{ // Verbindung hergestellt.
		// Telegramm senden
		TTlnServBuf TSB;
		
		TSB.Code = TLNSERV_ABFRAGE;
		TSB.DataLen = sizeof(TSB.TlnAbfr);
		TSB.TlnAbfr.RufNr = Wahlnummer;
		TSB.TlnAbfr.Version = 1;
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
	if (Modus != ModHtmlChatVerbunden
		&& Modus != ModKommendVerbunden 
		&& Modus != ModGehendVerbunden
		&& Modus != ModPufferDruckUndSchluss
		&& Modus != ModNamensucheEingabe
		&& Modus != ModNamensucheServerAbfrage
		&& Modus != ModNamensucheAusgabe)
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
				
			else if (AsciiDruckPuffer[dpi] == CodeChrWerDa)
				{
				AsciiHilfPuffer[hpi++] = CodeChrWerDa;
				AsciiDruckPuffer[dpi+1] = '\0'; // Wandlung terminieren.
				ZeilePos = 0; // damit kein Umbruch eingebaut wird.
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

		if (ProtokollLevel >= DatenDetailliert)
			{
			ProtokollierenITelex_P(PSTR("Ascii-Verarbeitung: " ));
			ProtokollierenPuffer(AsciiDruckPuffer, dpi);
			ProtokollierenInt_P(PSTR(" (+%u)\r\n" ), strlen(AsciiDruckPuffer) - dpi);
			ProtokollierenITelex_P(PSTR("      gewandelt in: " ));
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
				
			if (AsciiHilfPuffer[ki] == CodeChrWerDa)
				{ // nur am Ende einer Zeile und des gesamten Puffers auch weitergeben
				if (AsciiDruckPuffer[0] != '\0')
					; // noch weitere Daten im Puffer --> KEIN Werda
				else if (AsciiHilfPuffer[ki+1] == '\r' && AsciiHilfPuffer[ki+2] == '\n' && AsciiHilfPuffer[ki+3] == '\0')
					{ // Werda am Zeilenende, aber mit WR + ZL --> leztere löschen
					SchreibeZeichenInSendePuffer(CodeChrWerDa);
					AsciiHilfPuffer[ki+1] = '\0';
					} 
				else if (AsciiHilfPuffer[ki+1] == '\0')
					SchreibeZeichenInSendePuffer(CodeChrWerDa);
				else
					; // Werda nicht am Zeilenende, ignorieren
				} 
			else // nicht WerDa
				{
				if (SchreibeZeichenInSendePuffer(AsciiHilfPuffer[ki]))
					{ // nur im Echo darstellen, wenn es auch gedruckt wurde.
					if (Modus == ModHtmlChatVerbunden)
						ZeichenInHtmlSendeText(AsciiHilfPuffer[ki]);
					}
				} // nicht Werda
				
			ki++;
			}

		if (ki > 0)
			memmove(AsciiHilfPuffer, AsciiHilfPuffer + ki, strlen(AsciiHilfPuffer) - ki + 1); 
		
		} // AsciiHilfPuffer nicht leer und SendePuffer leer

	} // AsciiDruckPufferVerarbeiten()


//! Einschaltung für HTML-Chat oder Meldungsdruck.
//------------------------------------------------
bool SonstigeAnwahl(uint8_t aDurchwahl)
	{
	if (KommendInternAnwaehlen(aDurchwahl))
		{ 
		if (ProtokollLevel >= AblaufInfo)
			Protokollieren_P(PSTR("Einschaltung intern\r\n" ));
		BusSenden(BusKdoEin);
		return true;
		}
	else
		{ 
		if (ProtokollLevel >= NurFehler)
			Protokollieren_P(PSTR("! Einschaltung intern VERSAGT\r\n" ));
			
		Diagnoseausgabe_P(ISTR(AnschlussInternBesetzt, LokaleSprache), 1);
			
		AsciiDruckPuffer[0] = '\0'; // damit es keine neue Einschaltung gibt.
		AsciiHilfPuffer[0] = '\0';
		AsciiDruckZiel = 0;
		AsciiHilfZeilenanfang = 0;
		ModusWechsel(ModWarteGrundstellung);
		return false;
		}
	} // SonstigeAnwahl()

		
//! Speichert Datum und Uhrzeit im Puffer, so dass diese beim Sender und Empfänger gedruckt werden.
//-------------------------------------------------------------------------------------------------
static void DatumUhrzeitDrucken()
	{
	char Text[20];
	struct TIME Time;

	// Zeit holen
	CLOCK_GetTime(&Time);
	sprintf_P(Text, PSTR("%02u.%02u.%04u  %02d:%02d\r\n"),
			  Time.DD, Time.MM, Time.YY, Time.hh, Time.mm);

	if (DatumDruckModus == DatumDruckLokal || DatumDruckModus == DatumDruckBeide)
		{
		PufferSpeich(&SendePuffer, TtyCodeWR);
		PufferSpeich(&SendePuffer, TtyCodeZL);
		PufferSpeich(&SendePuffer, TtyCodeZiUm);
		for (uint8_t i = 0 ; i < strlen(Text) ; i++)
			{
			uint8_t Code = ZeichenZuCode(Text[i], ZiMode);
			if (Code != 255)
				PufferSpeich(&SendePuffer, Code);
			}
		}
		
	if (DatumDruckModus == DatumDruckAnrufer || DatumDruckModus == DatumDruckBeide)
		{
		for (uint8_t i = 0 ; i < 7 ; i++)
			PufferSpeich(&EmpfPuffer, TtyCodeBuUm);
				// EmpfPuffer wird an Gegenstelle gesendet, die muss erst anlaufen, daher als "Überbrückung" ein paar ZL
		PufferSpeich(&EmpfPuffer, TtyCodeWR);
		PufferSpeich(&EmpfPuffer, TtyCodeZL);
		PufferSpeich(&EmpfPuffer, TtyCodeZiUm);
		for (uint8_t i = 0 ; i < strlen(Text) ; i++)
			{
			uint8_t Code = ZeichenZuCode(Text[i], ZiMode);
			if (Code != 255)
				PufferSpeich(&EmpfPuffer, Code);
			}
		}
		
	} // DatumUhrzeitDrucken()
	
	
//! Der iTelex-client an sich.
//------------------------------------------------------------------------------------------------------------
//! Diese Funktion wird zyklisch aufgerufen und hat folgende Aufgaben:
//! - Steuerbefehle vom TWI-Bus annehmen und interpretieren.
//! - Nachschauen, ob eine Verbindung auf den registrierten Port eingegangen ist. Wenn ja 
//!   holt er sich die Socketnummer der Verbindung und speichert diese.
//! - Wenn eine Verbindung zustande gekommen ist wird diese wiederrum zyklisch nach neuen Daten abgefragt und entsprechend
//!   reagiert.
//! .
//! Eine Übersicht der Gesamtfunktion ist in der Datei AblaeufeVerbindung.xls dargestellt.
//! Die dort enthaltenen ID sind hier mit ID#xxx referenziert.
//! \param 	NONE
//! \return	NONE

void itelex_thread()
	{
	uint8_t Code;

	ITelexThreadCount++;

	StartKurzTimer(&ITelexThreadCheckTimer);
	
#if defined(LEDROT_ITELEXTHREADBLOCK)
	LED_off(ROT); 
#endif //defined(LEDROT_ITELEXTHREADBLOCK)
	
	// ======================================================================
	// Auf TWI-Bus empfangene Codes auswerten
	// ======================================================================

	if (GetEmpfByte(&Code))
		{
		switch (Code)
			{
			case 1 ... BusKdoVerbAufnahme:
				if (ProtokollLevel >= AblaufInfo)
					{
					ProtokollierenITelex();
					ProtokollierenInt_P(PSTR("TWI Reservierung intern / gehend von %u\r\n" ), Code);
					}
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
				if (ProtokollLevel >= AblaufInfo)
					ProtokollierenITelex_P(PSTR("TWI Einschaltkommando intern / gehend\r\n" ));
					
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
					if (ProtokollLevel >= AblaufInfo)
						ProtokollierenITelex_P(PSTR("TWI Einschaltquittung intern / kommend\r\n" ));
					ModusWechsel(ModKommendVerbunden);
					SocketSendeQuittung = true;
					DatumUhrzeitDrucken();
					}

				else if (Modus == ModHtmlChatWarteEinQuitt)
					{ 
					if (ProtokollLevel >= AblaufInfo)
						ProtokollierenITelex_P(PSTR("TWI Einschaltquittung nach Beginn HTML-Chat\r\n" ));
					ModusWechsel(ModHtmlChatVerbunden);
					}

				else if (Modus == ModMeldungsdruckWarteEinQuitt)
					{ 
					if (ProtokollLevel >= AblaufInfo)
						ProtokollierenITelex_P(PSTR("TWI Einschaltquittung fuer Meldungsdruck\r\n" ));
					ModusWechsel(ModPufferDruckUndSchluss);
					}
				
				else
					FalschCodeEmpfangen(BusQuittEin);
					
				break;

			case BusKdoWahlFreigabe:
				// dies ist eine Leitungsschnittstelle, die kann nicht wählen.
				if (ProtokollLevel >= AblaufInfo)
					ProtokollierenITelex_P(PSTR("TWI Wahlaufforderung intern / kommend\r\n" ));
					
				FalschCodeEmpfangen(BusQuittEin);
				break;
				
			case BusKdoWahlziffer0 ... BusKdoWahlziffer9:
				if (ProtokollLevel >= AblaufInfo)
					{
					ProtokollierenITelex();
					ProtokollierenInt_P(PSTR("TWI Wahlziffer %u intern / gehend\r\n" ), Code - BusKdoWahlziffer0);
					}
					
				if (Modus == ModGehendWaehlen && iTelexSocketMode == SocketIdle)
					{
					// allgemeines Verhalten beim Wählen:
					// 1. nach jeder gewählten Ziffer wird das eigene Teilnehmerverzeichnis durchsucht.
					// 2. Der Teilnehmer-Server wird abgefragt, wenn die gewählte Nummer 
					//    mindestens 5 Stellen hat UND
					//    2a) Ein nicht lokaler Eintrag im eigenen Teilnehmerverzeichnis gefunden wurde.
					//    2b) ODER zwei Sekunden Wahlpause gemacht wurde.
					// 3. Ein Verbindungsaufbau wird versucht, wenn
					//    3a) Ein lokaler Eintrag im eigenen Teilnehmerverzeichnis gefunden wurde.
					//    3b) ODER der Teilnehmer-Server eine positive Rückmeldung bringt
					//    3c) ODER 5 Sekunden Zeit seit der letzen Wahlziffer vergangen sind.
					//			(nur für den Schritt 3c) wirkt #WahlVerbAufbauNach5SekundenVersuchen
					// 4. Die Wahl wird abgebrochen (Abbruch-Meldung an das wählende Gerät, wenn 
					//    4a) Verbindungsaufbau zu 3a) ODER 3b) fehlschlägt
					//    4b) 15 Sekunden seit der letzten Wahlziffer vergangen sind.
					// im folgenden ist auf diese Schritte durch "Wahl-Schritt" verwiesen.

					if (Wahlziffern == 0 && Code == BusKdoWahlziffer0)
						{ // Namenssuche starten.
						if (ProtokollLevel >= AblaufInfo)
							ProtokollierenITelex_P(PSTR("Namenssuche gestartet -> Einschalt-Quittung an TWI\r\n" ));
						ModusWechsel(ModNamensucheEingabe);
						BusSenden(BusQuittEin);
						for (uint8_t i = 0 ; i < 5 ; i++)
							PufferSpeich(&SendePuffer, TtyCodeBuUm); // kurze Verzögerung nach dem Einschalten.
						strcpy_P(AsciiDruckPuffer, ISTR(NamensucheTexteingabe, LokaleSprache));
						}
						
					else // es war keine 0 als erster Stelle
						{
						// ID#221 ********************************************
						Wahlnummer = 10 * Wahlnummer + (Code - BusKdoWahlziffer0);
						Wahlziffern++;
						StartKurzTimer(&WahlPauseTimer);
						TlnServerAbfrageWiederholungssperre = false;
						WahlVerbAufbauNach5SekundenVersuchen = false;
						
						if (TlnSuche(Wahlnummer, false, &GewaehlterTln))
							{  // es wurde ein Teilnehmer im lokalen Telefonbuch gefunden.
							// ID#222 ********************************************
							bool RufnummerServerAbfrage = (Wahlziffern >= GlobRufnrMinZiffern && (GewaehlterTln.Flags & TlnFlag_Lokal) == 0);
								// siehe Wahl-Schritt 1.
							
							if (ProtokollLevel >= AblaufInfo)
								{
								ProtokollierenITelex();
								ProtokollierenInt_P(PSTR("Teilnehmer %lu im eigenen Telefonbuch gefunden.\r\n"), GewaehlterTln.Nummer);
								}
			
							if (RufnummerServerAbfrage)
								RufnummerBeiTlnServerAbfragen(); 
									// siehe Wahl-Schritt 2a)
								
							if ((GewaehlterTln.Flags & TlnFlag_Lokal) != 0)
								{ // es ist ein lokaler Eintrag
								if (Verbindungsaufbau(&GewaehlterTln) != 0)
									{ // Verbindungsaufbau war nicht erfolgreich --> Wahl-Schritt 4a)
									WahlAbbruchMeldung("nc");
									InterneVerbindungBeenden(true);
									TlnServerAbfrageWiederholungssperre = true;
									}
								}
							else // globaler Einrag -> Wahl-Schritt 3c) vorbereiten
								WahlVerbAufbauNach5SekundenVersuchen = true;
								
							} // gewählte Nummer war vollständig
							
						else // !TlnSuche(Wahlnummer...) 
							TlnDatenInit(&GewaehlterTln); 
								// da die aktuell gewählte Nummer ggf. nicht mehr zum zuletzt gefundenen Teilnehmer passt.
						
						StartKurzTimer(&WahlPauseTimer); 
							// nochmal, damit Verzögerungen bei Serverabfrage oder so nicht zu vorzeitigem Abbruch führen.
						} // else es war keine 0 als erster Stelle
					} // if Modus == ModGehendWaehlen
					
				else
					FalschCodeEmpfangen(BusQuittEin);
					
				break; // case BusKdoWahlziffer0 ... BusKdoWahlziffer9:
				
			case BusQuittSchluss:

				if (ProtokollLevel >= AblaufInfo)
					ProtokollierenITelex_P(PSTR("TWI Ausschaltung quittiert\r\n" ));
					
				if (Modus != ModWarteSchlussQuitt)
					{
					if (ProtokollLevel >= NurFehler)
						ProtokollierenITelex_P(PSTR("! Schlussquittung ohne Aufforderung\r\n"));
						
					FalschCodeEmpfangen(Code);
					}

				// Html-Puffer löschen
				AsciiDruckPuffer[0] = '\0'; // damit es keine neue Einschaltung gibt.
				AsciiHilfPuffer[0] = '\0';
				AsciiHilfZeilenanfang = 0;
				
				ModusWechsel(ModWarteGrundstellung);
				
				break;
			
			case BusKdoSchluss:

				if (ProtokollLevel >= AblaufInfo)
					ProtokollierenITelex_P(PSTR("TWI Ausschaltung intern\r\n"));
					
				// ID#212 ********************************************
				// ID#224 ********************************************
				if (Modus != ModRuhe)
					{
					BusSenden(BusQuittSchluss);
					ExterneVerbindungBeenden();
					}
					
				ModusWechsel(ModWarteGrundstellung);
					
				break;

			// ID#411 ID#104 *************************************
			// ???? \todo Prio 1 Ablauftabelle prüfen...
				
			default:
				FalschCodeEmpfangen(Code);
				break;
				
			} // switch Code
		} // if GetEmpfByte

	// ======================================================================
	// Prüfen, ob TWI-Kommunikation überhaupt noch läuft
	// ======================================================================

	if (ModusTwiVerbunden())
		{
		if (TwiWatchdogCount > 4 * iTelexTimerFreq) // nach 4 Sekunden ohne TWI-Kommunikation
			{
			if (ProtokollLevel >= NurFehler)	
				ProtokollierenITelex_P(PSTR("! TWI-Timeout -> Abschaltung\r\n"));
			Diagnoseausgabe_P(ISTR(TWITimeout, LokaleSprache), 1);
			
			InterneVerbindungBeenden(true);
			iTelexSocketAbbauGeplant = true;
			if (SocketOutBufUsed < SocketOutBufMax - 2)
				{
				SocketOutBuf[SocketOutBufUsed++] = ITELEXC_ENDE;
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
	if (iTelexSocketMode == SocketIdle)
		{
		if (Modus == ModGehendVerbunden
			|| (Modus >= ModKommendVerbVorstufe && Modus <= ModKommendVerbunden)
			|| (Modus == ModEmailPOPVerbunden))
			{
			InterneVerbindungBeenden(false);
			}
		} // if (iTelexSocketMode == SocketIdle)
	else
		{
		switch (iTelexSocketProtokoll)
			{
			case Ascii:
				AsciiDatenVerarbeiten();
				break;
				
			case iTelexProt:
				ITelexDatenVerarbeiten();
				break;
				
	#ifdef ITELEX_EMAIL
			case POP3:
				POP3DatenVerarbeiten();
				break;
				
			case SMTP:
				SMTPDatenVerarbeiten();
				break;
				
	#endif //def ITELEX_EMAIL
			
			default:
				ProtokollierenITelex_P(PSTR("! ILLEGALES Protokoll\r\n"));
				iTelexSocketProtokoll = Ascii;
				break;
			}
		} // else iTelexSocketMode != SocketIdle
		
	// besondere Modus Gegebenheiten bearbeiten:
	// -----------------------------------------
	if (Modus == ModGehendWaehlen && !PufferLeer(&SendePuffer))
		{ // es wurden Daten empfangen, also schnellstens Endgerät anschmeißen
		// ID#227 Teil 2 *******************************************************
		if (ProtokollLevel >= AblaufInfo)
			ProtokollierenITelex_P(PSTR("Angerufener hat geantwortet -> Einschaltung intern\r\n" ));
			
		BusSenden(BusQuittEin);
		ModusWechsel(ModGehendVerbunden);
		}
		
	if (Modus == ModKommendEinschalten)
		{
		if (KommendInternAnwaehlen(Durchwahl)) 
			{ // ID#321 ********************************************
			BusSenden(BusKdoEin);
			ModusWechsel(ModKommendWarteEinQuitt);
			if (ProtokollLevel >= AblaufInfo)
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("Anwahl intern %u "), Durchwahl);
				ProtokollierenInt_P(PSTR("verbunden mit %u\r\n"), BusVerbPartner >> 1);
				}
			}
		else
			{ // ID#322 ********************************************
			if (ProtokollLevel >= NurFehler)
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("! Anwahl intern an %u VERSAGT\r\n"), Durchwahl);
				}
				
			Diagnoseausgabe_P(ISTR(AnschlussInternBesetzt, LokaleSprache), 1);

			SendeStopkommando(PSTR("occ"));
			
			ModusWechsel(ModWarteGrundstellung);
			}
		} // if Modus == ModKommendEinschalten
			
	if (Modus == ModNamensucheEingabe)
		{
		while (!PufferLeer(&EmpfPuffer))
			{
			char z = CodeZuZeichen(PufferAusg(&EmpfPuffer), (char*) &EmpfPuffer.BuZiMode);
			uint8_t SuchTextLen = strlen(NamensucheSuchtext);
			
			if (z == '\r' || z == '\n')
				{
				if (SuchTextLen == 0)
					; // WR / ZL am Zeilenanfang ignorieren
				else
					{
					ProtokollierenITelex_P(PSTR("Starte Namenssuche mit <"));
					Protokollieren(NamensucheSuchtext);
					Protokollieren_P(PSTR(">\r\n"));

					strcat_P(AsciiDruckPuffer, PSTR("\r\n"));

					if (strlen(NamensucheSuchtext) < 3) 
						{
						strcat_P(AsciiDruckPuffer, ISTR(NamensucheZuKurz, LokaleSprache));
						ModusWechsel(ModPufferDruckUndSchluss);
						}
#ifdef ITELEX_TLNSERVER
					else if (TlnServSyncGeheimzahl != 0)
						{ // Gerät ist selbst Teilnehmer-Server, Abfrage nicht erforderlich
						ModusWechsel(ModNamensucheAusgabe);
						}

#endif //def ITELEX_TLNSERVER
					else
						{ // Gerät ist nur normaler Teilnehmer, also jetzt Server-Anfrage starten.
						if (TeilnehmerServerSocketOeffnen(PSTR("Namensuche")))
							{ // Verbindung hergestellt.
							// Telegramm senden
							TTlnServBuf TSB;
							
							TSB.Code = TLNSERV_SUCHE;
							TSB.DataLen = sizeof(TSB.TlnSuche);
							strncpy(TSB.TlnSuche.SuchMuster, NamensucheSuchtext, sizeof(TSB.TlnSuche.SuchMuster));
							TSB.TlnSuche.Version = 1;
							PutSocketData_RPE(TeilnehmerServerSocket, 2 + TSB.DataLen, TSB.Buf, RAM);
							ModusWechsel(ModNamensucheServerAbfrage);
							strcat_P(AsciiDruckPuffer, ISTR(NamensucheBitteWarten, LokaleSprache));
							}
						else
							{
							strcat_P(AsciiDruckPuffer, ISTR(KeinTeilnehmerServerErreichbar, LokaleSprache));
							strcat_P(AsciiDruckPuffer, ISTR(NamensucheNurLokal, LokaleSprache));
							ModusWechsel(ModNamensucheAusgabe);
							}
						
						}
					break;
					}
				} // z == WR oder ZL
				
			else if (z != '\0' && z != '#' && SuchTextLen < TlnNameMax - 1)
				{
				if (z != ' ' || SuchTextLen > 0)
					{
					NamensucheSuchtext[SuchTextLen++] = z;
					NamensucheSuchtext[SuchTextLen] = '\0';
					}
				} // z druckbar
				
			} // while !PufferLeer(EmpfPuffer)
		} // if (Modus == ModNamensucheEingabe)
			
	if (Modus == ModNamensucheAusgabe)
		{
		while (AsciiDruckPuffer[0] == '\0')
			{ // Puffer ist leer
			TTlnDaten TD;
			if (!TlnListerNaechster(&NamenssucheLister, &TD))
				{
				strcpy_P(AsciiDruckPuffer, ISTR(NamensucheListenende, LokaleSprache));
				ModusWechsel(ModPufferDruckUndSchluss);
				break;
				}
			if (TlnSuchMusterPasst(NamensucheSuchtext, &TD))
				{
				sprintf_P(AsciiDruckPuffer, PSTR("%9ld - %s - "), TD.Nummer, TD.Name);
				switch (TD.AdrArt)
					{
					case Geloescht:
						break; // kann nicht sein
					case iTelexHostname:
					case iTelexIP:
					case iTelexDynIP:
						if (TD.Durchwahl != 0)
							sprintf_P(AsciiDruckPuffer + strlen(AsciiDruckPuffer), PSTR("(%d) "), TD.Durchwahl);
						strcat_P(AsciiDruckPuffer, ISTR(TypITelex, LokaleSprache));
						break;
					case AsciiHostname:
					case AsciiIP:
						strcat_P(AsciiDruckPuffer, ISTR(TypAscii, LokaleSprache));
						break;
					case eMail:
						strcat_P(AsciiDruckPuffer, ISTR(TypEMail, LokaleSprache));
						break;
					}
				strcat_P(AsciiDruckPuffer, PSTR("\r\n"));
				}
			} // while (AsciiDruckPuffer[0] == '\0')
		} // if (Modus == ModNamensucheAusgabe)
		
	// ==========================================================================
	// Timeouts? (auch 2 Sekunden Wahlpause...)
	// ==========================================================================

	if (Modus == ModGehendWaehlen 
		&& !TlnServerAbfrageWiederholungssperre
		&& Wahlziffern >= GlobRufnrMinZiffern
		&& KurzTimerVal(&WahlPauseTimer) >= 2 * KurzTimerFreq)
		{ // 2 Sekunden Wahlpause und 5 Ziffern gewählt --> Wahl-Schritt 2b)
		// ID#231 **************************************************************
		RufnummerBeiTlnServerAbfragen();
		}

	if (Modus == ModGehendWaehlen
		&& KurzTimerVal(&WahlPauseTimer) >= 5 * KurzTimerFreq
		&& WahlVerbAufbauNach5SekundenVersuchen
		&& iTelexSocketMode == SocketIdle
		&& GewaehlterTln.AdrArt != Geloescht)
		{ // Es ist ein nicht-Lokaler Eintrag im eigenen Teilnehmer-Verzeichbnis gewesen,
		// ggf. läuft eine Server-Abfrage, die wurde aber noch nicht beantwortet.
		// -> Wahl-Schritt 3c)
		WahlVerbAufbauNach5SekundenVersuchen = false; // nur ein Mal...
		if (Verbindungsaufbau(&GewaehlterTln) == 2)
			{ // ungültige Daten oder e-Mail gestört -> Abbruch
			WahlAbbruchMeldung("der"); 
			InterneVerbindungBeenden(true);
			}
		}
		
	if (Modus == ModGehendWaehlen 
		&& KurzTimerVal(&WahlPauseTimer) >= 15 * KurzTimerFreq
		&& iTelexSocketMode == SocketIdle)
		{ // 15 Sekunden Wahlpause --> Wahl-Schritt 4b)
		if (ProtokollLevel >= NurFehler)
			ProtokollierenITelex_P(PSTR("* 15 Sekunden nicht gewaehlt, Abbruch\r\n" ));
		WahlAbbruchMeldung("bk");
		InterneVerbindungBeenden(true);
		}
		
	if (Modus == ModWarteSchlussQuitt && KurzTimerVal(&BusQuittTimer) > 3 * KurzTimerFreq)
		{ // 3 Sekunden keine Schlussquittung empfangen
		// ID#412 ****************************************************************
		if (ProtokollLevel >= NurFehler)
			ProtokollierenITelex_P(PSTR("* Timeout beim Warten auf die Schlussquittung\r\n" ));
			
		ModusWechsel(ModWarteGrundstellung);
		}
		
	if (Modus == ModKommendWarteEinQuitt && KurzTimerVal(&BusQuittTimer) > 3 * KurzTimerFreq)
		{ // 3 Sekunden keine Einschalt-Quittung empfangen
		// ID#332 ***************************************************************
		if (ProtokollLevel >= NurFehler)
			ProtokollierenITelex_P(PSTR("! Timeout beim Warten auf die Einschaltquittung\r\n" ));
			
		InterneVerbindungBeenden(true);
		SendeStopkommando(PSTR("der"));
		}
		
	if (ModusTwiVerbunden() && LangTimerVal(&BeideRuhigTimer) > 10 * LangTimerMinuteFaktor)
		{
		if (ProtokollLevel >= NurFehler)
			ProtokollierenITelex_P(PSTR("* Abbau wegen 10 Minuten Funkstille.\r\n"));

		InterneVerbindungBeenden(true);
		ExterneVerbindungBeenden();
		ModusWechsel(ModWarteGrundstellung);
		}
	
	if (Modus == ModRuhe && (iTelexSocketHandle != NO_SOCKET_USED || iTelexSocketMode != SocketIdle))
		{
		if (KurzTimerVal(&GrundstellungPruefTimer) > 5 * KurzTimerFreq)
			{
			Diagnoseausgabe_P(PSTR("Grundstellung gestoert"), 1); // keine Englische Version, da nur ein Hack.
			iTelexSocketHandle = NO_SOCKET_USED;
			iTelexSocketMode = SocketIdle;
			}
		}
	else
		StartKurzTimer(&GrundstellungPruefTimer);
		
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
					strcpy_P(AsciiDruckPuffer, ISTR(DiagInterneIP, LokaleSprache));
					iptostr(myIP, AsciiDruckPuffer + strlen(AsciiDruckPuffer));
					strcat_P(AsciiDruckPuffer, PSTR("\r\n"));
					struct TIME Time;
					CLOCK_GetTime(&Time);
					sprintf_P(AsciiDruckPuffer + strlen(AsciiDruckPuffer), ISTR(Datum, LokaleSprache));
					sprintf_P(AsciiDruckPuffer + strlen(AsciiDruckPuffer), 
							  PSTR(": %02u.%02u.%04u %02u:%02u:%02u\r\n"), 
							  Time.DD, Time.MM, Time.YY, Time.hh, Time.mm, Time.ss);
					}
				break;
				
			case ModDeaktiviert:
				// ID#511 ********************************************************
				if (Tastendruck == Kurz)
					ModusWechsel(ModWarteGrundstellung);
				else
					{
					LED_on(GELB);
					TlnBuchSpeichereAufExternEeprom();					
					LED_off(GELB);
					softreset();
					}
				break;

			case ModPufferDruckUndSchluss:
				// ID#422 *************************************************************
				if (ProtokollLevel >= AblaufInfo)
					ProtokollierenITelex_P(PSTR("* Taste gedruckt --> Reste-Druck abgebrochen\r\n" ));
					
				SendeBusKdoSchluss();
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
	// Diagnosedaten drucken?
	// ==========================================================================

	if (Modus == ModRuhe 
		&& AsciiDruckPuffer[0] == '\0' 
		&& DiagnosePuffer[0] != '\0'
		&& DiagnosePufferLevel <= MeldungsdruckLevel)
		{ 
		AsciiDruckZiel = DiagnoseAusgabeZiel;
		strcpy_P(AsciiDruckPuffer, ISTR(DiagnoseEinleitung, LokaleSprache));
		strncat(AsciiDruckPuffer, DiagnosePuffer, AsciiDruckPufferMax-30);
		AsciiDruckPuffer[AsciiDruckPufferMax-30] = '\0';
		strcat_P(AsciiDruckPuffer, PSTR("\r\n\n\n"));
		if (ProtokollLevel >= AblaufInfo && ProtokollLevel < DatenDetailliert)
			{ // bei DatenDetailliert wird der Text eh ausgedruckt.
			ProtokollierenITelex_P(PSTR("Diagnosedruck: "));
			ProtokollierenPuffer(AsciiDruckPuffer, strlen(AsciiDruckPuffer));
			Protokollieren_P(PSTR("\r\n"));
			}
		DiagnosePuffer[0] = '\0';
		DiagnosePufferLevel = 0;
		DiagnoseAusgabeZiel = 0;
		}

	// ==========================================================================
	// Ascii-Text im Puffer z.B. durch Html-Eingabe?
	// ==========================================================================

	if ((Modus == ModRuhe || Modus == ModEmailPOPVerbunden) && AsciiDruckPuffer[0] != '\0')
		{
		if (ProtokollLevel >= AblaufInfo)
			{
			ProtokollierenITelex_P(PSTR("Meldungsdruck -> "));
			ProtokollierenPuffer(AsciiDruckPuffer, strlen(AsciiDruckPuffer));
			Protokollieren_P(PSTR("\r\n"));
			}
			
		if (SonstigeAnwahl(AsciiDruckZiel))
			{
			ModusWechsel(ModMeldungsdruckWarteEinQuitt);
			AsciiDruckZiel = 0;
			}
			
		} // if ModRuhe && Text im DruckPuffer
		
	AsciiDruckPufferVerarbeiten();
	
	if (Modus == ModHtmlChatVerbunden)
		{ // am Fernschreiber eigegebene Zeichen nach Ascii umwandeln 
		while (!PufferLeer(&EmpfPuffer))
			{
			ZeichenInHtmlSendeText(CodeZuZeichen(PufferAusg(&EmpfPuffer), (char*) &EmpfPuffer.BuZiMode));
			StartLangTimer(&BeideRuhigTimer);
			}

		if (AsciiDruckPuffer[0] == '\0'
			&& AsciiHilfPuffer[0] == '\0'
			&& SerUmSendBitNr == SerUmSendWarte
			&& PufferLeer(&SendePuffer)
			&& PufferLeer(&EmpfPuffer)
			&& (iTelexSocketHandle == NO_SOCKET_USED || SocketInBufUsed == 0)
			&& (KurzTimerVal(&HtmlDruckspiegelAnzeigeTimer) >= 30 * KurzTimerFreq // 30 Sekunden keine Anzeige-Abfrage
				|| LangTimerVal(&BeideRuhigTimer) >= 5 * LangTimerMinuteFaktor)) // 5 Minuten nichts eingegeben
			{
			if (ProtokollLevel >= AblaufInfo)
				ProtokollierenITelex_P(PSTR("HTML-Chat-Ruhe --> Ausschaltung intern\r\n" ));
				
			InterneVerbindungBeenden(true);
			} // Abschaltung nach 30 Sekunden / 5 Minuten.

		} // if Modus == ModHtmlChatVerbunden

	// ==========================================================================
	// Abschaltung nach Reste-Druck?
	// ==========================================================================

	if (Modus == ModPufferDruckUndSchluss 
		&& AsciiDruckPuffer[0] == '\0' 
		&& AsciiHilfPuffer[0] == '\0'
		&& SerUmSendBitNr == SerUmSendWarte
		&& PufferLeer(&SendePuffer))
		{ // ID#421 *************************************************************
		if (ProtokollLevel >= AblaufInfo)
			ProtokollierenITelex_P(PSTR("Reste gedruckt --> Ausschaltung intern\r\n" ));
			
		SendeBusKdoSchluss();
		ModusWechsel(ModWarteSchlussQuitt);
		}

	// ==========================================================================
	// Grundstellung nach eigenem Verbindungsabbau?
	// ==========================================================================

	if (Modus == ModWarteGrundstellung 
		&& iTelexSocketHandle == NO_SOCKET_USED
		&& iTelexSocketMode == SocketIdle)
		{
		if (ProtokollLevel >= AblaufInfo)
			{
			ProtokollRegelblockStart();
			ProtokollierenITelex_P(PSTR("Grundstellung erreicht (Socket geschlossen, TWI geschlossen)\r\n" ));
			ProtokollRegelblockEnde();
			}

		BusVerbPartner = 0;
		ModusWechsel(ModRuhe);
		ZeitUeberwachungEnde(&SelbstAnrufZeitUeberwachung);
		#ifdef LEDROT_SOCKETERROR
			LED_off(ROT);
		#endif //def LEDROT_SOCKETERROR
		}
		
	// ======================================================================
	// Dynamische IP-Aktualisierung / Selbstanruf starten
	// ======================================================================
	
	if (DynIP_Phase != DynIP_Inaktiv && NetzRufnummer >= GlobRufnrMinWert)
		{ // jetzt ist DynIP überhaupt sinnvoll...
		if (DynIP_Phase == DynIP_Erneuern
			&& (Modus == ModRuhe || Modus == ModDeaktiviert)
			&& TeilnehmerServerSocket == NO_SOCKET_USED)
			{ // Keine Verbindung laufend, Zeit für Aktualsierung 
			bool Fehler;
			
			if (TeilnehmerServerSocketOeffnen(PSTR("Selbstaktualisierung")))
				{ // Verbindung hergestellt.
				// Telegramm senden
				TTlnServBuf TSB;
				
				TSB.Code = TLNSERV_SELBSTAKT;
				TSB.DataLen = sizeof(TSB.SelbstAkt);
				TSB.SelbstAkt.RufNr = NetzRufnummer;
				TSB.SelbstAkt.Pin = Geheimzahl;
				TSB.SelbstAkt.Port = NetzPort;
				Fehler = (PutSocketData_RPE(TeilnehmerServerSocket, 2 + TSB.DataLen, TSB.Buf, RAM) != 2 + TSB.DataLen);
				if (Fehler)
					{
					CloseTCPSocket(TeilnehmerServerSocket);
					TeilnehmerServerSocket = NO_SOCKET_USED;
					TeilnehmerServerFehlerSpeichern(AktTlnServerTabI);
					}
				}
			else 
				Fehler = true;
				
			if (Fehler)
				{ // keine Verbindung hergestellt
				DynIPAktualisierungEndzeit = 15 * LangTimerMinuteFaktor - Zufallswert(0xF);
					// in 15 Minuten minus Zufall wieder. 
				DynIP_Phase = DynIP_Fehler;
				}
			else
				DynIP_Phase = DynIP_LaeuftGerade;
				
			StartLangTimer(&DynIPAktualisierungTimer);
			} // Zeit für Aktualsierung UND keine Verbindung laufend
		
		if ((DynIP_Phase == DynIP_Bestaetigt || DynIP_Phase == DynIP_Unbestaetigt)
			&& (Modus == ModRuhe || Modus == ModDeaktiviert)
			&& SelbstAnrufPhase == SelbstAnrufRuhe
			&& SelbstAnrufSocketHandle == NO_SOCKET_USED
			&& iTelexSocketHandle == NO_SOCKET_USED
			&& TeilnehmerServerSocket == NO_SOCKET_USED
			&& DiagnosePuffer[0] == '\0' // sonst würde der laufende Selbst-Anruf gleich unterbrochen werden
			&& SelbstAnrufPeriode > 0
			&& KurzTimerVal(&SelbstAnrufTimer) > SelbstAnrufEndzeit)
			{ // Selbst-Anruf starten
			if (NetzEigeneIP == 0)
				SelbstAnrufPhase = SelbstAnrufSperre;
			else
				{ // NetzEigeneIP gültig
				SelbstAnrufSendePruefwert = ITelexThreadCount ^ Timer0CallbackCount;
				if (SelbstAnrufSendePruefwert == 0)
					SelbstAnrufSendePruefwert = 1;
				SelbstAnrufEmpfangPruefwert = 0; // als Zeichen, dass noch nichts empfangen wurde.
				
				ZeitUeberwachungStart(&SelbstAnrufZeitUeberwachung);
				
				SelbstAnrufSocketHandle = Connect2IP(NetzEigeneIP, NetzPort); 
				if (SelbstAnrufSocketHandle == -1)
					{ 
					// Verbindung konnte nicht aufgebaut werden
					if (ProtokollLevel >= NurFehler)
						ProtokollierenITelex_P(PSTR("! Selbst-Anruf oeffnen des Socket VERSAGT.\r\n"));

					SelbstAnrufSocketHandle = NO_SOCKET_USED;
					SelbstAnrufFehlerZaehler++;
					ZeitUeberwachungAbbruch(&SelbstAnrufZeitUeberwachung);
					}
				else
					{ // Öffnen erfolgreich.
					char Buf[10];
					Buf[0] = ITELEXC_SELBSTANRUF;
					Buf[1] = 2; // 16 Bit-Wert
					Buf[2] = high(SelbstAnrufSendePruefwert);
					Buf[3] = low(SelbstAnrufSendePruefwert);
					SelbstAnrufPhase = SelbstAnrufWarteEmpfang;
					if (PutSocketData_RPE(SelbstAnrufSocketHandle, 4, Buf, RAM) == 4)
						{
						if (ProtokollLevel < AuchRegelmaessiges)
							ProtokollRegelblockInit();
						ProtokollRegelblockStart();
						ProtokollierenITelex();
						ProtokollierenInt_P(PSTR("Selbst-Anruf Daten ueber Socket #%d gesendet.\r\n"), SelbstAnrufSocketHandle);
						ProtokollRegelblockEnde();
						}
					else
						{
						if (ProtokollLevel >= NurFehler)
							ProtokollierenITelex_P(PSTR("! Selbst-Anruf Daten-Sendung VERSAGT.\r\n"));
						SelbstAnrufFehlerZaehler++;
						SelbstAnrufPhase = SelbstAnrufSchliessen;
						ZeitUeberwachungAbbruch(&SelbstAnrufZeitUeberwachung);
						}
					} // Öffnen von NetzEigeneIP erfolgreich.
					
				StartKurzTimer(&SelbstAnrufTimer);
				SelbstAnrufEndzeit = SelbstAnrufPeriode * KurzTimerFreq - Zufallswert(0x7);
				} // NetzEigeneIP gültig
			} // Selbst-Anruf starten
				
		if (SelbstAnrufPhase == SelbstAnrufWarteEmpfang)
			{
			if (SelbstAnrufEmpfangPruefwert != 0)
				{ // Echo ist angekommen
				if (SelbstAnrufEmpfangPruefwert == SelbstAnrufSendePruefwert)
					{ // Richtiges Echo angekommen
					if (ProtokollLevel >= DatenKurz)
						{
						ProtokollRegelblockStart();
						ProtokollierenITelex_P(PSTR("* Selbst-Anruf erfolgreich abgeschlossen.\r\n"));
						ProtokollRegelblockEnde();
						}
					if (ProtokollLevel < AuchRegelmaessiges)
						ProtokollRegelblockLoeschen();
					
					SelbstAnrufFehlerZaehler = 0;
					SelbstAnrufEndzeit = SelbstAnrufPeriode * KurzTimerFreq - Zufallswert(0x3F);
					if (DynIP_Phase == DynIP_Unbestaetigt)
						{
						ProtokollierenITelex_P(PSTR("Selbst-Anruf bestaetigt IP Adresse.\r\n"));
						DynIP_Phase = DynIP_Bestaetigt;
						}
					} // Richtiges Echo angekommen
				else
					{ // Falsches Echo angekommen
					if (ProtokollLevel >= NurFehler)
						ProtokollierenITelex_P(PSTR("! Selbst-Anruf FALSCHE Daten empfangen.\r\n"));
					SelbstAnrufFehlerZaehler++;
					SelbstAnrufEndzeit = 5 * KurzTimerFreq + Zufallswert(0x37);					
					} // Falsches Echo angekommen
				StartKurzTimer(&SelbstAnrufTimer);
				SelbstAnrufPhase = SelbstAnrufSchliessen;
				} // Echo ist angekommen
				
			else if (Modus != ModRuhe && Modus != ModDeaktiviert && Modus != ModKommendVerbVorstufe)
				{ // irgend ein Modus-Wechsel genau in der Phase des Selbst-Anruf
				if (ProtokollLevel >= NurFehler)
					ProtokollierenITelex_P(PSTR("! Selbst-Anruf ABGEBROCHEN wegen Modus-Wechsel.\r\n"));
				StartKurzTimer(&SelbstAnrufTimer);
				SelbstAnrufEndzeit = SelbstAnrufPeriode * KurzTimerFreq - Zufallswert(0x3F);
				SelbstAnrufPhase = SelbstAnrufSchliessen;
				ZeitUeberwachungAbbruch(&SelbstAnrufZeitUeberwachung);
				} // irgend ein Modus-Wechsel genau in der Phase des Selbst-Anruf
				
			else if (KurzTimerVal(&SelbstAnrufTimer) > 5 * KurzTimerFreq) 
				{ // Timeout nach 5 Sekunden
				if (ProtokollLevel >= NurFehler)
					ProtokollierenITelex_P(PSTR("! Selbst-Anruf KEIN Echo empfangen.\r\n"));
				SelbstAnrufFehlerZaehler++;
				StartKurzTimer(&SelbstAnrufTimer);
				SelbstAnrufEndzeit = 10 * KurzTimerFreq + Zufallswert(0x37);					
				SelbstAnrufPhase = SelbstAnrufSchliessen;
				ZeitUeberwachungAbbruch(&SelbstAnrufZeitUeberwachung);
				} // Timeout nach 5 Sekunden
				
			} // if (SelbstAnrufPhase == SelbstAnrufWarteEmpfang)

		if (SelbstAnrufPhase == SelbstAnrufSchliessen) 
			{
			CloseTCPSocket(SelbstAnrufSocketHandle);
			SelbstAnrufSocketHandle = NO_SOCKET_USED;
			SelbstAnrufPhase = SelbstAnrufRuhe;
			StartKurzTimer(&SelbstAnrufTimer);
			}
			
		if (SelbstAnrufPhase == SelbstAnrufRuhe && SelbstAnrufFehlerZaehler >= 3 && DynIP_Phase == DynIP_Bestaetigt)
			{ // nach drei Fehlversuchen Server-Aktulisierung starten
			DynIP_Phase = DynIP_Erneuern; // sofort erneuern.
			SelbstAnrufPhase = SelbstAnrufSperre;
			}
			
		else if (SelbstAnrufPhase == SelbstAnrufRuhe && SelbstAnrufFehlerZaehler >= 8 && DynIP_Phase == DynIP_Unbestaetigt)
			{ // nach acht Fehlversuchen Selbst-Anruf nicht mehr durchführen.
			SelbstAnrufPeriode = 0;
			Diagnoseausgabe_P(ISTR(SelbstAnrufMehrfachVersagt, LokaleSprache), 1);
			SelbstAnrufPhase = SelbstAnrufSperre;
			}
			
		if (LangTimerVal(&DynIPAktualisierungTimer) >= DynIPAktualisierungEndzeit && DynIP_Phase != DynIP_LaeuftGerade)
			DynIP_Phase = DynIP_Erneuern;

		} // if (DynIP_Phase != DynIP_Inaktiv && NetzRufnummer >= GlobRufnrMinWert)
		
	if (SelbstAnrufSocketHandle != NO_SOCKET_USED && CheckSocketState(SelbstAnrufSocketHandle) == SOCKET_NOT_USE)
		{
		if (ProtokollLevel >= NurFehler)
			ProtokollierenITelex_P(PSTR("* Selbst-Anruf-Socket durch Timeout geschlossen!\r\n" ));
		CloseTCPSocket(SelbstAnrufSocketHandle);
		SelbstAnrufSocketHandle = NO_SOCKET_USED;
		SelbstAnrufPhase = SelbstAnrufRuhe;
		StartKurzTimer(&SelbstAnrufTimer);
		SelbstAnrufEndzeit = SelbstAnrufPeriode * KurzTimerFreq + Zufallswert(0x7);
		}
			
	// ======================================================================
	// Antworten vom Teilnehmer-Server auswerten
	// ======================================================================
	
	if (TeilnehmerServerSocket != NO_SOCKET_USED)
		{
		// Datenempfang vom Teilnehmer-Server
		static TTlnServBuf TSB;
		int InCount = GetBytesInSocketData(TeilnehmerServerSocket);

		if (InCount > sizeof(TSB))
			{
			int Res = GetSocketData(TeilnehmerServerSocket, sizeof(TSB), TSB.Buf);
			if (ProtokollLevel >= NurFehler)
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("! Teilnehmer-Server Empfang UEBERLAUF zu viele Daten (%d byte)"), InCount);
				if (Res > 0)
					ProtokollierenPuffer(TSB.Buf, Res);
				Protokollieren_P(PSTR(" -> verworfen, Socket geschlossen\r\n"));
				}
			CloseTCPSocket(TeilnehmerServerSocket);
			TeilnehmerServerSocket = NO_SOCKET_USED;
			} // InCount zu groß
			
		else if (InCount > 0) 
			{ // Daten verarbeiten
			int Res = GetSocketData(TeilnehmerServerSocket, InCount, TSB.Buf);
			
			if (ProtokollLevel >= DatenDetailliert) // Daten explizit
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("Teilnehmer-Server Empfang: (%d/" ), InCount);
				ProtokollierenInt_P(PSTR("%d)"), Res);
				if (Res > 0)
					ProtokollierenPuffer(TSB.Buf, Res);
				Protokollieren_P(PSTR("\r\n"));
				}		
			
			// Daten des Socket-Empfangspuffer interpretieren
			// ----------------------------------------------
			// es wird immer nur ein Telegramm gesendet und empfangen
			switch (TSB.Code)
				{
				case TLNSERV_AUSKUNFT_NICHTVERG:
					if (ProtokollLevel >= AblaufInfo)
						ProtokollierenITelex_P(PSTR("Teilnehmer-Server meldet 'nicht gefunden'\r\n" ));
						
					if (LangeDienstmeldungen)
						Diagnoseausgabe_P(ISTR(NummerNichtBekannt, LokaleSprache), 4); 

					if (Modus == ModNamensucheServerAbfrage)
						{
						strcat_P(AsciiDruckPuffer, ISTR(NamensucheServerAbbruch, LokaleSprache));
						strcat_P(AsciiDruckPuffer, ISTR(NamensucheNurLokal, LokaleSprache));
						ModusWechsel(ModNamensucheAusgabe);
						}
					
					break;
					
				case TLNSERV_AUSKUNFT_VERSION1:
					Res = 0; // vorsorglich
					if (ProtokollLevel >= AblaufInfo)
						{
						ProtokollierenITelex_P(PSTR("Teilnehmer-Server meldet Eintrag gefunden: " ));
						ProtokollierenIPAdr(TSB.TlnAuskunft.IPAdr);
						Protokollieren_P(PSTR("\r\n"));
						}
						
					if (Modus == ModGehendWaehlen)
						{ // dann ist GewaehlterTln gesetzt
						if (GewaehlterTln.AdrArt == Geloescht)
							; // weitermachen
						
						else if (GewaehlterTln.Nummer != TSB.TlnAuskunft.Nummer)
							{ // vorhandener Eintrag weicht von 'aktuellem' ab --> Abbruch
							ProtokollierenITelex_P(PSTR("! Teilnehmer-Server meldet ANDERE Nummer als angefragt\r\n"));
							break;
							}
						
						else if ((GewaehlterTln.Flags & TlnFlag_Lokal) != 0)
							{ // Privater Eintrag --> nicht ändern
							ProtokollierenITelex_P(PSTR("! im lokalen Telefonbuch als 'Privat' gekennzeichnet\r\n"));
							break;
							}

						// gelieferte Daten _teilweise_ in das eigene Telefonbuch kopieren...
						if (TSB.TlnAuskunft.Datum > GewaehlterTln.Datum || GewaehlterTln.AdrArt == Geloescht)
							{
							GewaehlterTln.Nummer = TSB.TlnAuskunft.Nummer;
							if (GewaehlterTln.Name[0] == '\0') //! \todo Prio 1 Einstellbarkeit, ob nur leere Namen überschreiben werden
								strncpy(GewaehlterTln.Name, TSB.TlnAuskunft.Name, sizeof(GewaehlterTln.Name));
							GewaehlterTln.Flags = TSB.TlnAuskunft.Flags;
							GewaehlterTln.AdrArt = TSB.TlnAuskunft.AdrArt; 
							strncpy(GewaehlterTln.Adresse, TSB.TlnAuskunft.Adresse, sizeof(GewaehlterTln.Adresse));
							GewaehlterTln.IPAdr = TSB.TlnAuskunft.IPAdr;
							GewaehlterTln.Port = TSB.TlnAuskunft.Port;
							GewaehlterTln.Durchwahl = TSB.TlnAuskunft.Durchwahl;
							if (GewaehlterTln.Datum < TSB.TlnAuskunft.Datum)
								GewaehlterTln.Datum = TSB.TlnAuskunft.Datum;

							Res = TlnHinzufuegen(&GewaehlterTln, TlnHinzKopieren);
							} // Aktualisieren ist sinnvoll
						
						if (iTelexSocketMode == SocketIdle && TSB.TlnAuskunft.Nummer == Wahlnummer)
							{ // erhaltenen Datensatz auch zum Verbindungsaufbau nutzen -> Wahl-Schritt 3b)
							if (Verbindungsaufbau(&GewaehlterTln) == 0)
								// erfolgreich
								Diagnoseausgabe_P(NULL, 3);
							
							else // nicht erfolgreich
								{
								WahlAbbruchMeldung("nc");
								InterneVerbindungBeenden(true); // Wahl-Schritt 4a)
								}
								
							}
						} // if (Modus == ModGehendWaehlen)
					
					else if (Modus == ModNamensucheServerAbfrage)
						{ 
						// erhaltene Datensätze einfach speichern.
						Res = TlnHinzufuegen(&TSB.TlnAuskunft, TlnHinzNurNeuereUebernehmen);
						
						// und nächsten anfordern
						TSB.Code = TLNSERV_SYNC_QUITTUNG;
						TSB.DataLen = 0;
						PutSocketData_RPE(TeilnehmerServerSocket, 2 + TSB.DataLen, TSB.Buf, RAM);
						}
						
					// Falls TlnHinzufuegen() aufgerufen wurde, ist Res gesetzt und auszuwerten.
					if (Res < 0)
						{
						ProtokollierenITelex();
						ProtokollierenInt_P(PSTR("! Datensatz vom Teilnehmer-Server mit Nr %ld konnte nicht gespeichert werden\r\n"), GewaehlterTln.Nummer);
						Diagnoseausgabe_P(ISTR(InternesVerzeichnisVoll, LokaleSprache), 2);
						}
#ifdef ITELEX_TLNSERVER							
					else if (Res == 1) 
						{
						TlnServTlnbuchEintragGeaendert(&GewaehlterTln, -1); 
							// -1: Geänderter Eintrag kommt nicht durch einen Sync-Vorgang 
						}
#endif //def ITELEX_TLNSERVER
						
					break; // case TLNSERV_AUSKUNFT_VERSION1
					
				case TLNSERV_IPRUECKMELD:
					if (TSB.IpRueckm.EmpfIP == NetzEigeneIP)
						{ // keine Änderung
						if (ProtokollLevelTlnServ >= AblaufInfo)
							ProtokollierenITelex_P(PSTR("Dynamische IP-Aktualisierung: bestehende IP gilt weiter\r\n" ));
						}
					else
						{
						NetzEigeneIP = TSB.IpRueckm.EmpfIP;
						if (ProtokollLevelTlnServ >= NurFehler) // ausnahmsweise
							{
							ProtokollierenITelex_P(PSTR("Dynamische IP-Aktualisierung: neue IP "));
							ProtokollierenIPAdr(NetzEigeneIP);
							Protokollieren_P(PSTR("\r\n"));
							}
						SelbstAnrufFehlerZaehler = 0;
						}
					DynIP_Phase = DynIP_Unbestaetigt;
					StartLangTimer(&DynIPAktualisierungTimer);
					DynIPAktualisierungEndzeit = 60 * LangTimerMinuteFaktor - Zufallswert(0x3F); 
						// in einer Stunde wieder
					StartKurzTimer(&SelbstAnrufTimer);
					SelbstAnrufPhase = SelbstAnrufRuhe;
					break;

				case TLNSERV_SYNC_ENDE:
					ProtokollierenITelex_P(PSTR("TlnServer meldet Listenende\r\n"));
					if (Modus == ModNamensucheServerAbfrage)
						ModusWechsel(ModNamensucheAusgabe); 
						// bewirkt auch, dass unten die Verbindung zum Teilnehmer-Server abgebaut wird.
					break;
				
				case TLNSERV_FEHLER:
					ProtokollierenITelex_P(PSTR("! Fehlermeldung des Teilnehmer-Servers: "));
					Protokollieren(TSB.PureData);
					Protokollieren_P(PSTR("\r\n"));
					StartLangTimer(&DynIPAktualisierungTimer);
					DynIPAktualisierungEndzeit = 15 * LangTimerMinuteFaktor - Zufallswert(0xF);
						// in 15 Minuten minus Zufall wieder.

					SelbstAnrufPhase = SelbstAnrufSperre;
					
					if (Modus == ModNamensucheServerAbfrage)
						{
						strcat(AsciiDruckPuffer, TSB.PureData);
						strcat_P(AsciiDruckPuffer, ISTR(NamensucheServerAbbruch, LokaleSprache));
						strcat_P(AsciiDruckPuffer, ISTR(NamensucheNurLokal, LokaleSprache));
						ModusWechsel(ModNamensucheAusgabe);
						}
					break;
				
				default:
					ProtokollierenITelex_P(PSTR("! unerwartete Antwort des Teilnehmer-Servers\r\n" ));
					StartLangTimer(&DynIPAktualisierungTimer);
					DynIPAktualisierungEndzeit = 15 * LangTimerMinuteFaktor - Zufallswert(0xF);
						// in 15 Minuten minus Zufall wieder.

					SelbstAnrufPhase = SelbstAnrufSperre;
					
					if (Modus == ModNamensucheServerAbfrage)
						{
						strcat_P(AsciiDruckPuffer, ISTR(NamensucheServerAbbruch, LokaleSprache));
						strcat_P(AsciiDruckPuffer, ISTR(NamensucheNurLokal, LokaleSprache));
						ModusWechsel(ModNamensucheAusgabe);
						}
					
					break;
				
				} // switch (TSB.Code)
				
			// in der Wahlphase genügt eine Antwort...
			if (Modus != ModNamensucheServerAbfrage)
				{
				CloseTCPSocket(TeilnehmerServerSocket);
				TeilnehmerServerSocket = NO_SOCKET_USED;
				}
				
			} // if (InCount = GetBytesInSocketData(TeilnehmerServerSocket)) > 0
		
		// Schließanforderung vom Teilnehmer-Server?
		if (TeilnehmerServerSocket != NO_SOCKET_USED && CheckSocketState(TeilnehmerServerSocket) == SOCKET_NOT_USE)
			{
			if (ProtokollLevelTlnServ >= AblaufInfo)
				ProtokollierenITelex_P(PSTR("Socket zum Teilnehmer-Server wurde von Gegenstelle geschlossen\r\n" ));
			CloseTCPSocket(TeilnehmerServerSocket);
			TeilnehmerServerSocket = NO_SOCKET_USED;
			}
		
		// Timeout? kommt von selbst nach 30 Sekunden...
		
		} // if (TeilnehmerServerSocket != NO_SOCKET_USED)
		
#ifdef ITELEX_EMAIL

	// ==========================================================================
	// Ab und zu mal prüfen, ob es neue Mails gibt.
	// ==========================================================================
	
	if ((SelbstAnrufPhase == SelbstAnrufRuhe || SelbstAnrufPhase == SelbstAnrufSperre)
	    && Modus == ModRuhe)
		POP3Einleiten();
	
#endif //def ITELEX_EMAIL
	
	// ==========================================================================
	// Ab und zu mal den Protokollinhalt speichern
	// ==========================================================================

	ProtokollSpeichern(false);
	
	// ==========================================================================
	// Sicherheitslücke durch Überlauf des Konfig-Freigabe-Timers schließen.
	// ==========================================================================
	
	if (KonfigFreigabeErteilt && LangTimerVal(&KonfigFreigabeTimer) > 5 * LangTimerMinuteFaktor)
		// Konfig-Freigabe nur 5 Minuten gültig.
		KonfigFreigabeErteilt = false;
	
	// ==========================================================================
	// Ausgabe der Gespeicherten Änderungen der Socket-Tabelle.
	// ==========================================================================
	
	PrintSocketConnectionStateChanges();
	
	// ==========================================================================
	// laufende Prüfsummenberechnung des Teilnehmer-Verzeichnisses.
	// ==========================================================================
	
	TlnBuchPruefsummeBerechnenSchritt();
	
	// ==========================================================================
	// Uhrzeit verteilen?
	// ==========================================================================
	
	static uint8_t MinuteLetzeRundsendung;

	struct TIME Time;
	CLOCK_GetTime(&Time);
	if (Time.mm != MinuteLetzeRundsendung && BusFrei && (BusAuftrag == Nichts || BusAuftrag == Fertig))
		{
		MinuteLetzeRundsendung = Time.mm;
		
		RundsendDaten[0] = 'c';
		RundsendDaten[1] = 'l';
		RundsendDaten[2] = 'k';
		RundsendDaten[3] = Time.YY - 2000;
		RundsendDaten[4] = Time.MM;
		RundsendDaten[5] = Time.DD;
		RundsendDaten[6] = Time.hh;
		RundsendDaten[7] = Time.mm;
		RundsendAnzDaten = 8;

		BusRundsenden();
		}

	// ==========================================================================
	// HACK Status-Signale Seriell
	// ==========================================================================

	bset_RTS(get_CTS());
	
	} // itelex_thread
	

// ================================================================================	
			

//! Liest den String s aus in die Durchwahl-Tabelle.
//--------------------------------------------------

static void DurchwahlTabelleDekodieren(char *s)
	{
	uint8_t i = 0; // Index in der Tabelle
	uint8_t AnzSt = 0; // Anzahl Stellen
	uint8_t WahlNr = 0; // Bisherige Nummer
	
	while (i < 9)
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

			case '\0':
			default:
				while (i < 9)
					DurchwahlTabelle[i++] = 0;
				return;
			
			} // switch (*s)
		s++;
		} // while *s != 0 && i < 9
		
	} // DurchwahlTabelleDekodieren()
	
#endif // ITELEX_ANSCHLUSS


//! Kann am Anfang jeder cgi-Funktion aufgerufen werden, um Zugang zu der Funktion erst nach Kennwort-Eingabe zu erlauben.
// -----------------------------------------------------------------------------------------------------------------------
//! \param 	pStruct	Struktur auf den HTTP_Request. Bei NULL wird nur die Variable abgefragt, es gibt keine "Ersatzausgabe" 
//! des Passwort-Abfragefensters.
//! \param Sprache Index der Sprache für die ggf. erforderliche Passwort-Abfrage
//! \param Abfragen Abfrag des Passworts erfolgt nur bei true
//! \retval true, wenn Zugriff erfolgen darf.

bool KonfigFreigabe(void *pStruct, TSprache Sprache, bool Abfragen)
	{
	static const PROGMEM char Kennwort_P[] = "kennw";
	
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;

	if (KonfigPasswort[0] == '\0')
		return true; // ohne Kennwort keine Sperre
	
	if (KonfigFreigabeErteilt && LangTimerVal(&KonfigFreigabeTimer) <= 5 * LangTimerMinuteFaktor)
		{ // 5 Minuten lang ist der Zugang erlaubt, aber nur wenn es von der gleichen IP kommt.
		StartLangTimer(&KonfigFreigabeTimer);
		if (http_request == NULL)
			return true;
		else if (KonfigFreigabeFuerIP == 0)
			{ //! \todo Check ob im lokalen Netz.
			KonfigFreigabeFuerIP = TCP_sockettable[http_request->HTTP_SOCKET].SourceIP;
			return true;
			}
		else if (KonfigFreigabeFuerIP == TCP_sockettable[http_request->HTTP_SOCKET].SourceIP)
			return true;
		else
			{
			if (Abfragen)
				{ // auch nur dann eine Ersatzausgabe 
				cgi_PrintHttpheaderStart();
				printf_P(ISTR(SeiteGesperrt, Sprache));
				cgi_PrintHttpheaderEnd();
				}
			return false;
			}
		}
		
	if (http_request == NULL)
		// ohne Bezug auf HTML-Abfrage keine Chance
		return false;
		
	if (!Abfragen)
		return false; // wenn nicht gefragt werden soll, kann die Antwort nur Nein sein.
		
	//! \todo Prio 2 Sperre nach Fehlversuchen
	
	if (http_request->argc == 0 || PharseCheckName_P(http_request, Kennwort_P) == 0)
		{ // Ausgabe der Passwort - Eingabeseite
		KonfigFreigabeErteilt = false;
		cgi_PrintHttpheaderStart();
		CgiFormStartTabbed_P(PSTR("")); //! \todo Prio 2 Formularname mit Sprache
		CgiFormInputFieldText_P(ISTR(KennwortAbfrage, Sprache), Kennwort_P, KonfigPasswortLen, NULL);
		CgiFormFinish_P(ISTR(KennwortFreigeben, Sprache));
		cgi_PrintHttpheaderEnd();
		return false;
		}
	else
		{ // Test des eingegebenen Kennworts
		char *EingabeText = http_request->argvalue[PharseGetValue_P(http_request, Kennwort_P)];
		if (strcmp(EingabeText, KonfigPasswort) == 0)
			{ // korrekt eingegebenen
			KonfigFreigabeErteilt = true;
			KonfigFreigabeFuerIP = TCP_sockettable[http_request->HTTP_SOCKET].SourceIP;
			StartLangTimer(&KonfigFreigabeTimer);
			http_request->argc = 0; // damit die eigentliche Seite nicht durch die Kennwort-Eingabe verwirrt ist!			
			return true;
			}
		else
			{ // falsches Kennwort
			cgi_PrintHttpheaderStart();
			printf_P(ISTR(KennwortFalsch, Sprache));
			cgi_PrintHttpheaderEnd();
			KonfigFreigabeErteilt = false;
			Diagnoseausgabe_P(ISTR(FalschesKonfigKennwortEingegeben, Sprache), 3);
			return false;
			} // else falsches Kennwort
		} // else argc > 0 && Kennwort im Request
	} // KonfigFreigabe()
	
	
//! Kann am Anfang jeder cgi-Funktion aufgerufen werden, um eine Sprachselektion zu ermöglichen. 
// -----------------------------------------------------------------------------------------------------------------------
//! \param 	pStruct	Struktur auf den HTTP_Request. Die Sprachangabe muss als "spr=de" oder "spr=en" erfolgt
//! sein. Ist die Sprachangabe das einzige Attribut des CGI-Requests wird die Anzahl der Parameter
//! des CGI Requests auf Null gesetzt.
//! \retval true wenn eine Angabe gefunden wurde.

bool PruefeSprache(void *pStruct, TSprache *Sprache)
	{
	static const PROGMEM char Sprache_P[] = "spr";
	
	if (pStruct == NULL)
		return false;
		
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	
	// Eigentlich hat das folgende gar nix mit dem Sprachprüfen zu tun, hier ist aber eine geeignete Stelle
	// für eine Protokollierung der CGI-Aufrufe.
	if (ProtokollLevel >= AblaufInfo)
		{
		char *Ende;
		
		ProtokollierenITelex_P(PSTR("cgi-Aufruf von "));
		ProtokollierenIPAdr(TCP_sockettable[http_request->HTTP_SOCKET].SourceIP);
		Protokollieren_P(PSTR(":"));
		if (http_request->argc == 0)
			Ende = http_request->HTTP_LINEBUFFER;
		else
			Ende = http_request->argvalue[http_request->argc - 1];
		Ende += strlen(Ende);
		ProtokollierenPuffer(http_request->HTTP_LINEBUFFER, Ende - http_request->HTTP_LINEBUFFER);
		Protokollieren_P(PSTR("\r\n"));
		}
	
	if (http_request->argc == 0)
		return false; // da kann man nix finden.
		
	if (PharseCheckName_P(http_request, Sprache_P) == 0)
		return false; // keine Sprachangabe in der Abfrage enthalten.
	
	char *SprachAngabe = http_request->argvalue[PharseGetValue_P(http_request, Sprache_P)];
	if (strcmp_P(SprachAngabe, PSTR("de")) == 0)
		*Sprache = Deutsch;
	else if (strcmp_P(SprachAngabe, PSTR("en")) == 0)
		*Sprache = Englisch;
	else
		return false;
		
	if (http_request->argc == 1)
		http_request->argc = 0; 
			// damit die nur-sprache-Angabe dazu führt, dass die "Grundseite" der CGI-Funktion dargestellt wird.
	
	return true;
	} // PruefeSprache()
	
	
//! Kann am Anfang jeder cgi-Funktion aufgerufen werden, um das Kennwort abzufragen.
// ---------------------------------------------------------------------------------
//! Diese Funktion ist nur ein Behelfskonstrukt. Die Ermittelte Sprache wird nicht 
//! an den Aufrufer übergeben.
//! \param 	pStruct	Struktur auf den HTTP_Request. Bei NULL wird nur die Variable abgefragt, es gibt keine "Ersatzausgabe" 
//! des Passwort-Abfragefensters. Die Sprachangabe muss als "spr=de" oder "spr=en" erfolgt
//! sein. Ist die Sprachangabe das einzige Attribut des CGI-Requests wird die Anzahl der Parameter
//! des CGI Requests auf Null gesetzt.
//! \retval true, wenn Zugriff erlaubt ist.

bool PruefeSpracheUndKonfigFreigabe(void *pStruct)
	{
	static TSprache Sprache = Deutsch;
	
	PruefeSprache(pStruct, &Sprache);	
	
	return KonfigFreigabe(pStruct, Sprache, true); 
		// wenn dass Kennwort nicht abgefragt werden soll, sind die Funktionen PruefeSprache und KonfigFreigabe 
		// einzeln zu benutzen.
	}
	
	
/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface für Ausgabe von Debug-Infos des iTelex
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void itelex_cgi_debug( void * pStruct )
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	
	if (http_request->argc != 0 && PharseCheckName_P(http_request, PSTR("reset")) != 0)
		{ // Bestimmte Werte zurücksetzen
		DiagnosePuffer[0] = '\0';
		Timer0Cnt_Min = 255;
		Timer0Cnt_Max = 0;
		Timer0Callback_Max = 0;
#ifdef ITELEX_ANSCHLUSS		
		FalscherCode = 0;
		ZeitUeberwachungInit(&SelbstAnrufZeitUeberwachung, 1 * KurzTimerFreq);
#endif //def ITELEX_ANSCHLUSS		
		TwiIsrCount = 0;
		}
	
	if (http_request->argc != 0 && PharseCheckName_P(http_request, PSTR("watchdogtest")) != 0)
		{ // Watchdog-Reset verursachen nach 20 sekunden.
		StartKurzTimer(&WatchdogTestTimer);
		WatchdogTestTimerEnde = 20 * KurzTimerFreq; //! \todo Prio 2 einstellbar...
		}
	
	cgi_PrintHttpheaderStart();

#define PRINTVAL(Var) printf_P(PSTR("<br>" #Var " = %u"), Var)
#define PRINTVALHEX(Var) printf_P(PSTR("<br>" #Var " = %02X"), Var)

	printf_P(PSTR("DiagnosePuffer: %s"), DiagnosePuffer);
	PRINTVAL(DiagnosePufferLevel);

#ifdef ITELEX_TLNSERVER
	if (TlnServSyncGeheimzahl != 0)
		TlnServDebugPrint();
	else
		printf_P(PSTR("<br>Teilnehmer-Server inaktiv"));
#endif //def ITELEX_TLNSERVER
	
#ifdef ITELEX_ANSCHLUSS

	PRINTVAL(TeilnehmerServerAlleNichtErreichbar);
	for (uint8_t i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
		{
		printf_P(PSTR("<br>TeilnehmerServerFehlerZaehler(%s) = %d, Sperre-Timer %d"), 
				 TeilnehmerServerAdresse[i], TeilnehmerServerFehlerZaehler[i], LangTimerVal(&TeilnehmerServerSperrTimer[i]));
		}
	
	PRINTVAL(Modus);
	PRINTVALHEX(Status); // bezüglich interner Telex Funktionalität (ist auf TWI-Bus sichtbar)

	//*
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
	
	PRINTVAL(Wahlnummer);
	PRINTVAL(Wahlziffern);
	PRINTVAL(KurzTimerVal(&WahlPauseTimer));
	PRINTVAL(KurzTimerVal(&SchreibPauseTimer));
	//*/
	
	PRINTVAL(DynIP_Phase);
	PRINTVAL(LangTimerVal(&DynIPAktualisierungTimer));
	PRINTVAL(DynIPAktualisierungEndzeit);

	PRINTVAL(SelbstAnrufPhase);
	PRINTVAL(SelbstAnrufFehlerZaehler);
	PRINTVAL(KurzTimerVal(&SelbstAnrufTimer));
	PRINTVAL(SelbstAnrufEndzeit);
	PRINTVAL(SelbstAnrufSocketHandle);
	PRINTVAL(SelbstAnrufSendePruefwert);
	PRINTVAL(SelbstAnrufEmpfangPruefwert);
	
	printf_P(PSTR("<br>SelbstAnrufZeitUeberwachung: "));
	printf(ZeitUeberwachungAusgabe(&SelbstAnrufZeitUeberwachung));
	
	PRINTVAL(iTelexSocketMode);
	PRINTVAL(iTelexSocketHandle);
	PRINTVALHEX(iTelexSocketIP);
	PRINTVAL(iTelexSocketAbbauGeplant);
	PRINTVAL(KurzTimerVal(&iTelexSocketAbbruchTimer));
	PRINTVAL(SocketInBufUsed);
	PRINTVAL(SocketOutBufUsed);
	PRINTVAL(iTelexSocketProtokoll);
	PRINTVAL(ProtokollPhase);

	PRINTVAL(SocketAnzahlZeichenGesendet);
	PRINTVAL(SocketAnzahlZeichenQuittiert);
	PRINTVAL(SocketAnzahlZeichenEmpfangen);

	PRINTVAL(TeilnehmerServerSocket);
	
	printf_P(PSTR("<br>HtmlSendeText: ["));
	printf(HtmlSendeText);
	printf_P(PSTR("]<br>AsciiDruckPuffer: ["));
	printf(AsciiDruckPuffer);
	printf_P(PSTR("]"));

	PRINTVAL(KurzTimerVal(&BusQuittTimer));
	PRINTVAL(TwiLebenszeichenZaehler);
	PRINTVAL(TwiWatchdogCount);
	PRINTVAL(BusKollisionZaehler);
	PRINTVAL(KurzTimerVal(&iTelexSocketLebenszeichenTimer));
	PRINTVAL(KurzTimerVal(&ITelexThreadCheckTimer));

	PRINTVAL(FalscherCode); 
	PRINTVAL(TwiIsrCount); 
	PRINTVAL(ITelexThreadCount); 

	PRINTVAL(Timer0CallbackCount); 
	PRINTVAL(Timer0Cnt_Min); 
	PRINTVAL(Timer0Cnt_Max); 
	PRINTVAL(Timer0Callback_Max); 

#endif // ITELEX_ANSCHLUSS

	for (uint8_t i = 0 ; i < MAX_TCP_CONNECTIONS ; i++)
		{
		if (TCP_sockettable[i].ConnectionState != 0)
			printf_P(PSTR("<br>TCP_socket[%d]: ConnectionState=%u, SendState=%u, SourcePort=%u, DestinationPort=%u, SourceIP=%lX, Timeoutcounter=%d"),
					 i,     TCP_sockettable[i].ConnectionState, 
												TCP_sockettable[i].SendState, 
															  TCP_sockettable[i].SourcePort,
																			 TCP_sockettable[i].DestinationPort,
																								 TCP_sockettable[i].SourceIP, 
																											   TCP_sockettable[i].Timeoutcounter);
		else
			printf_P(PSTR("<br>TCP_socket[%d]: closed"), i);
																		
		}
	
	CLOCK_decode_time(&SystemStartZeit);
	printf_P(PSTR("<br>SystemStartZeit = %02u.%02u.%04u %02d:%02d:%02d, ResetFlag = %02X"), 
			 SystemStartZeit.DD, SystemStartZeit.MM, SystemStartZeit.YY,
			 SystemStartZeit.hh, SystemStartZeit.mm, SystemStartZeit.ss, ResetFlags);

	// 5 V messen:
	ADCSRA = (1<<ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
	ADMUX = (0 << REFS1) | (1 << REFS0) | (0 << ADLAR) | (1 << MUX4) | (1 << MUX3) | (1 << MUX2) | (1 << MUX1) | (0 << MUX0);
	ADCSRB = 0;
	printf_P(PSTR("<br> VCC [mV] ="));
	for (uint8_t i = 0 ; i < 10 ; i++)
		{
		ADCSRA |= (1 << ADSC); // Start
		while (BIT_IS_SET(ADCSRA, ADSC))
			; // warten bis A/D-Wandlung fertig.
		uint16_t Mess = ADC;
		if (Mess == 0)
			Mess = 1;
		// zur Messung der 5V:
		// ADC = Vin * 1024 / Vref
		// Vin = 1,1 V
		// Vref = 5 V (zu messen)
		// Vref (mV) = Vin * 1024 / ADC = 1100 * 1024 / Vref.
		printf_P(PSTR(" %lu"), 1100UL * 1024UL / Mess);
		}
				  
	printf_P(PSTR("<br><a href=\"itelex-debug.cgi?reset\">Statiktik-Daten zur&uuml;cksetzen</a>"
				  "<br>Ethernet: %ld Bytes in %ld Packeten LockErrors %ld") , 
				  ByteCounter, PacketCounter, eth_state_error );

	cgi_PrintHttpheaderEnd();

	ProtokollSpeichern(true);

	}
	

#ifdef ITELEX_ANSCHLUSS
	
/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface für das Ausgabefenster der Fernschreiber-Simulation
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void itelex_cgi_msg_Out( void * pStruct )
	{
	static TSprache Sprache;
	
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	
	PruefeSprache(pStruct, &Sprache);	

	printf_P( PSTR(	"<HTML>"
					"<HEAD>"
					"<meta http-equiv=\"expires\" content=\"1\">"
					"<meta http-equiv=\"pragma\" content=\"no-cache\">"
					"<meta http-equiv=\"refresh\" content=\"10; URL=itelex-msg-out.cgi\">"
					"</HEAD>"
					"<BODY>" ));
					
	if (Modus == ModHtmlChatVerbunden)
		{
		printf_P(ISTR(Druckspiegel, Sprache));
		printf_P(PSTR("<br><pre>%s&lt;&lt;&lt;%s%s</pre>"), HtmlSendeText, AsciiHilfPuffer, AsciiDruckPuffer);

		if (ProtokollLevel >= DatenKurz)
			{
			ProtokollierenITelex_P(PSTR("Direktdruck Abruf Druckspiegel:"));
			char *p = HtmlSendeText + strlen(HtmlSendeText) - 40;
			if (p < HtmlSendeText) 
				p = HtmlSendeText;
			ProtokollierenPuffer(p, strlen(p));
			ProtokollierenInt_P(PSTR(" (%u)\r\n"), strlen(HtmlSendeText));
			}

		}

	else if (Modus == ModRuhe)
		{
		printf_P(ISTR(TexteingabeStartetFernschreiber, Sprache));
		HtmlSendeText[0] = '\0';
		if (ProtokollLevel >= DatenKurz)
			ProtokollierenITelex_P(PSTR("Direktdruck Abruf Druckspiegel (aus)\r\n"));
		}
		
	else
		{
		if (Modus == ModDeaktiviert)
			printf_P(ISTR(ModulDeaktiviert, Sprache)); 
		else
			printf_P(ISTR(AndereVerbindungBesteht, Sprache)); 
		
		if (ProtokollLevel >= DatenKurz)
			ProtokollierenITelex_P(PSTR("Direktdruck Abruf Druckspiegel (belegt)\r\n"));
		}
	
	cgi_PrintHttpheaderEnd();
	
	StartKurzTimer(&HtmlDruckspiegelAnzeigeTimer);
	
	}


/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface für das Eingabefenster der Fernschreiber-Simulation
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void itelex_cgi_msg_In( void * pStruct )
	{
	static const PROGMEM char Eingabe_P[] = "Eingabe";

	static TSprache Sprache;

	PruefeSprache(pStruct, &Sprache);	
	
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;

	if ((http_request->argc != 0) 
	    && PharseCheckName_P(http_request, Eingabe_P)
		&& (Modus == ModRuhe || Modus == ModHtmlChatWarteEinQuitt || Modus == ModHtmlChatVerbunden))
		{
		// Text holen...
		char *EingabeText = http_request->argvalue[PharseGetValue_P(http_request, Eingabe_P)];

		// In den Druckpuffer schieben
		strncat(AsciiDruckPuffer, EingabeText, AsciiDruckPufferMax - strlen(AsciiDruckPuffer) - 3);
		AsciiDruckPuffer[AsciiDruckPufferMax-3] = '\0'; // sicherheitshalber
		
		// falls letztes Zeichen ein @ war, ändern in Werda, falls nicht WR und ZL anfügen.
		if (AsciiDruckPuffer[strlen(AsciiDruckPuffer)-1] == '@')
			AsciiDruckPuffer[strlen(AsciiDruckPuffer)-1] = CodeChrWerDa;
		else
			strcat_P(AsciiDruckPuffer, PSTR("\r\n"));
			
		// Ergebnis protokollieren
		if (ProtokollLevel >= AblaufInfo)
			{
			ProtokollierenITelex_P(PSTR("HTML-Chat Eingabe: "));
			Protokollieren(EingabeText); 
			Protokollieren_P(PSTR("\r\n"));
			}
			
		// Und Zeit für Verbindungsabbau messen.
		StartLangTimer(&BeideRuhigTimer);
		StartKurzTimer(&HtmlDruckspiegelAnzeigeTimer); 
			// Für den Fall, dass das Anzeigefenster noch nicht aktualisiert wurde.
		
		if (Modus == ModRuhe)
			{
			uint8_t Anwahl;
			
			if (AnwahlNummerInAsciiPuffer(false) > 0)
				Anwahl = AnwahlNummerInAsciiPuffer(true);
			else
				Anwahl = 0;
		
			if (!ExternDurchwahlPruefen(&Anwahl))
				Anwahl = 0;
				
			if (ProtokollLevel >= AblaufInfo)
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("HTML-Chat begonnen (Anwahl %u) -> "), Anwahl);
				}
				
			if (SonstigeAnwahl(Anwahl)) 
				ModusWechsel(ModHtmlChatWarteEinQuitt);
				
			}
		}

	cgi_PrintHttpheaderStart();
	printf_P(PSTR("<form action=\"itelex-msg-in.cgi\">"));
	printf_P(ISTR(HtmlTextEingabe, Sprache));
	printf_P(PSTR("<input name=\"Eingabe\" type=\"text\" size=\"65\" value=\"\" maxlength=\"65\">"
				  "<input type=\"submit\" value=\""));
	printf_P(ISTR(HtmlTextEingabeAbsenden, Sprache));
	printf_P(PSTR(" \"><a href=\"itelex-msg-out.cgi\" target=\"MsgOut\">"));
	printf_P(ISTR(HtmlTextEingabeAktualisieren, Sprache));
	printf_P(PSTR("</a></form>"));
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
		{
		Buf[0] = '-';
		Buf[1] = '\0'; // ungültige Nummer
		}
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

#endif // ITELEX_ANSCHLUSS


const PROGMEM char LokaleSprache_P[] = "SPRACHE";

//! Speichert die (z.B. in einem CGI-Aufruf) benutzte Sprache in #LokaleSprache	
//-----------------------------------------------------------------------------
void SpeichereSpracheAlsLokal(TSprache Sprache)
	{
	if (Sprache == LokaleSprache)
		return; // da gibt es nix zu speichern.

	char Buf[6];
	itoa(Sprache, Buf, 10); // 10 ist die Basis für Dezimal!
	changeConfig_P(LokaleSprache_P, Buf);		
	LokaleSprache = Sprache;
	printf_P(ISTR(LokaleSpracheGespeichert, Sprache));
	} // SpeichereSpracheAlsLokal
	

const PROGMEM char KonfigPasswort_P[] = "CFGPASS";
const PROGMEM char TlnBuchOffen_P[] = "TLNBUCHOFFEN";
const PROGMEM char ProtokollLevel_P[] = "PROTLEVEL";
const PROGMEM char ProtokollLevelTlnServ_P[] = "PROTLEVELTLNSRV";
const PROGMEM char LangeDienstmeldungen_P[] = "LANGDIENSTMELD";
const PROGMEM char SocketLog_P[] = "SOCKETLOG";

#ifdef ITELEX_ANSCHLUSS

const PROGMEM char Hauptstelle_P[] = "HAUPTSTELLE";
const PROGMEM char EigeneNummer_P[] = "EIGENENUMMER";
const PROGMEM char FesteHst_P[] = "FESTEHPST";
const PROGMEM char MeldungsdruckLevel_P[] = "MELDRUCK";
const PROGMEM char AlternBeiBes_P[] = "ALTERNBEIBES";
const PROGMEM char DurchwahlTabelle_P[] = "DURCHWAHLTAB";
const PROGMEM char DatumDruckModus_P[] = "AUTODATUM";

#endif // ITELEX_ANSCHLUSS

	
/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface zum Ändern der Einstellungen des iTelex-Interface bezüglich der Einbindung
 * in das lokale iTelex-System
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/
 
void itelex_cgi_config_intern(void *pStruct)
	{
	static TSprache Sprache;
	
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	char Buf[35];

	PruefeSprache(pStruct, &Sprache);	
	
	if (!KonfigFreigabe(pStruct, Sprache, true))
		return;

	const char *AutoDatumSelList[4];
	AutoDatumSelList[0] = ISTR(DatumDruckKein, Sprache);
	AutoDatumSelList[1] = ISTR(DatumDruckLokal, Sprache);
	AutoDatumSelList[2] = ISTR(DatumDruckAnrufer, Sprache);
	AutoDatumSelList[3] = ISTR(DatumDruckBeide, Sprache);
		
	cgi_PrintHttpheaderStart();

	if ( http_request->argc == 0 )
		{
		CgiFormStartTabbed_P(PSTR("itelexcfg-intern.cgi"));

		#ifdef ITELEX_ANSCHLUSS
		AdresseZuWahlStr(BusEigenAdresse, Buf);
		CgiFormInputFieldText_P(ISTR(EigeneAmtsnummer, Sprache), EigeneNummer_P, 2, Buf);

		CgiFormCheckbox_P(ISTR(FesteHauptstelle, Sprache), FesteHst_P, FesteHauptstelle);

		AdresseZuWahlStr(Hauptstelle, Buf);
		CgiFormInputFieldText_P(ISTR(FesteHauptstelleNummer, Sprache), Hauptstelle_P, 2, Buf);

		CgiFormCheckbox_P(ISTR(AlternativSucheBeiBesetzt, Sprache), AlternBeiBes_P, AlternativSucheBeiBesetzt);
						
		readConfig_P(DurchwahlTabelle_P, Buf);
		CgiFormInputFieldText_P(ISTR(DurchwahlenListe, Sprache), DurchwahlTabelle_P, 30, Buf);

		CgiFormDropdown_P(ISTR(DatumDruckModus, Sprache), DatumDruckModus_P, 4, AutoDatumSelList, DatumDruckModus);
		
		#endif //def ITELEX_ANSCHLUSS

		CgiFormInputFieldULong_P(ISTR(ProtokollLevel, Sprache), ProtokollLevel_P, 2, ProtokollLevel + (SocketProtokollEin ? 10 : 0));
		CgiFormInputFieldULong_P(ISTR(ProtokollLevelTlnServer, Sprache), ProtokollLevelTlnServ_P, 2, ProtokollLevelTlnServ);

		#ifdef ITELEX_ANSCHLUSS
		CgiFormInputFieldULong_P(ISTR(DiagnoseLevel, Sprache), MeldungsdruckLevel_P, 2, MeldungsdruckLevel);
		#endif //def ITELEX_ANSCHLUSS
		
		CgiFormInputFieldText_P(ISTR(KonfigPasswort, Sprache), KonfigPasswort_P, KonfigPasswortLen, KonfigPasswort);
		
		CgiFormCheckbox_P(ISTR(TlnVerzeichnisOffen, Sprache), TlnBuchOffen_P, TlnBuchOffen);

		#ifdef ITELEX_ANSCHLUSS
		CgiFormCheckbox_P(ISTR(LangeDienstmeldungen, Sprache), LangeDienstmeldungen_P, LangeDienstmeldungen);
		#endif //def ITELEX_ANSCHLUSS

		CgiFormFinish_P(ISTR(EinstellungenUebernehmen, Sprache));
		}
	else // argc > 0
		{
		uint8_t Neu;

		printf_P(ISTR(NeueEinstellungen, Sprache));
		printf_P(PSTR("<a href=\"itelexcfg-intern.cgi\">"));
		printf_P(ISTR(Weiter, Sprache));
		printf_P(PSTR("</a>"));

		#ifdef ITELEX_ANSCHLUSS
		
		// Eigene Nummer
		// -------------
		if (PharseCheckName_P(http_request, EigeneNummer_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, EigeneNummer_P)], 2);
			Buf[2] = '\0';
			Neu = WahlZuAdresse(atoi(Buf), strlen(Buf));
			printf_P(PSTR("<br>"));
			printf_P(ISTR(EigeneAmtsnummer, Sprache));
			if (Neu == BusEigenAdresse)
				{
				printf_P(ISTR(Unveraendert, Sprache));
				printf_P(PSTR(": %s"), Buf);
				}
			else if (Modus == ModRuhe && BusEigenAdressePruefenUndSetzen(Neu))
				{
				AdresseZuWahlStr(Neu, Buf);
				changeConfig_P(EigeneNummer_P, Buf);
				printf_P(ISTR(GeaendertIn, Sprache));
				printf_P(PSTR(": %s"), Buf);
				}
			else
				printf_P(ISTR(KonnteNichtGeaendertWerden, Sprache));
			}
		
		// Nummer Hauptstelle
		// ------------------
		if (PharseCheckName_P(http_request, Hauptstelle_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, Hauptstelle_P)], 2);
			Buf[2] = '\0';
			Neu = WahlZuAdresse(atoi(Buf), strlen(Buf));
			printf_P(PSTR("<br>"));
			printf_P(ISTR(FesteHauptstelleNummer, Sprache));
			if (Neu == Hauptstelle)
				{
				printf_P(ISTR(Unveraendert, Sprache));
				printf_P(PSTR(": %s"), Buf);
				}
			else
				{
				AdresseZuWahlStr(Neu, Buf);
				changeConfig_P(Hauptstelle_P, Buf);
				Hauptstelle = Neu;
				printf_P(ISTR(GeaendertIn, Sprache));
				printf_P(PSTR(": %s"), Buf);
				}
			}
		
		// Feste Hauptstelle
		// ------------------
		FesteHauptstelle = CgiCheckBool_P(http_request, ISTR(FesteHauptstelle, Sprache), FesteHst_P, FesteHauptstelle, Sprache);
			
		// AlternativSucheBeiBesetzt
		// -------------------------
		AlternativSucheBeiBesetzt = CgiCheckBool_P(http_request, ISTR(AlternativSucheBeiBesetzt, Sprache), AlternBeiBes_P, AlternativSucheBeiBesetzt, Sprache);
			
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
				printf_P(PSTR("<br>"));
				printf_P(ISTR(DurchwahlenListe, Sprache));
				printf_P(ISTR(GeaendertIn, Sprache));
				printf_P(PSTR(": %s"), Buf);
				}
			else
				{
				printf_P(PSTR("<br>"));
				printf_P(ISTR(DurchwahlenListe, Sprache));
				printf_P(ISTR(Unveraendert, Sprache));
				}
			}

		// DatumDruckModus
		// ----------------
		char DatumDruckModusStr[20];
		strncpy(DatumDruckModusStr, http_request->argvalue[PharseGetValue_P(http_request, DatumDruckModus_P)], sizeof(DatumDruckModusStr));
		for (Neu = 0 ; Neu <= 3 ; Neu++)
			if (strcmp_P(DatumDruckModusStr, AutoDatumSelList[Neu]) == 0)
				break;
		if (Neu <= 3) 	
			{ // übereinstimmung gefunden
			printf_P(PSTR("<br>"));
			printf_P(ISTR(DatumDruckModus, Sprache));
			if (DatumDruckModus != DatumDruckKein + Neu)
				{ // geändert!
				DatumDruckModus = DatumDruckKein + Neu;
				itoa(DatumDruckModus, Buf, 10); // 10 ist die Basis für Dezimal!
				changeConfig_P(DatumDruckModus_P, Buf);
				printf_P(ISTR(GeaendertIn, Sprache));
				printf_P(PSTR(": %s (%d)"), DatumDruckModusStr, DatumDruckModus);
				}
			else
				printf_P(ISTR(Unveraendert, Sprache));
			}
			
		#endif // ITELEX_ANSCHLUSS
		
		ProtokollLevel = CgiCheckULong_P(http_request, ISTR(ProtokollLevel, Sprache), ProtokollLevel_P, 
										 ProtokollLevel + (SocketProtokollEin ? 10 : 0), Sprache);
		SocketProtokollEin = ProtokollLevel >= 10;
		if (SocketProtokollEin)
			ProtokollLevel -= 10;

		ProtokollLevelTlnServ = CgiCheckULong_P(http_request, ISTR(ProtokollLevelTlnServer, Sprache), ProtokollLevelTlnServ_P, ProtokollLevelTlnServ, Sprache);

		#ifdef ITELEX_ANSCHLUSS
		MeldungsdruckLevel = CgiCheckULong_P(http_request, ISTR(DiagnoseLevel, Sprache), MeldungsdruckLevel_P, MeldungsdruckLevel, Sprache);
		#endif //def ITELEX_ANSCHLUSS

		// KonfigPasswort
		// --------------
		if (PharseCheckName_P(http_request, KonfigPasswort_P))
			{
			strncpy(KonfigPasswort, http_request->argvalue[PharseGetValue_P(http_request, KonfigPasswort_P)], KonfigPasswortLen);
			KonfigPasswort[KonfigPasswortLen] = '\0';
			changeConfig_P(KonfigPasswort_P, KonfigPasswort);
			printf_P(ISTR(KennwortGgfGeaendert, Sprache));
			}
			
		TlnBuchOffen = CgiCheckBool_P(http_request, ISTR(TlnVerzeichnisOffen, Sprache), TlnBuchOffen_P, TlnBuchOffen, Sprache);
		
		#ifdef ITELEX_ANSCHLUSS
		LangeDienstmeldungen = CgiCheckBool_P(http_request, ISTR(LangeDienstmeldungen, Sprache), LangeDienstmeldungen_P, LangeDienstmeldungen, Sprache);
		#endif //def ITELEX_ANSCHLUSS

		SpeichereSpracheAlsLokal(Sprache);
		
		} // else argc > 0
		
	cgi_PrintHttpheaderEnd();

	} // itelex_cgi_config_intern()
	

/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface zum Aktivieren der Passwort-Sperre
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/
 
void itelex_cgi_config_sperren(void *pStruct)
	{
	static TSprache Sprache;
	
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;

	PruefeSprache(pStruct, &Sprache);	
	
	cgi_PrintHttpheaderStart();

	if (KonfigPasswort[0] == '\0')
		{
		printf_P(ISTR(InternesKennwortFehlt, Sprache));
		}
	else
		{
		KonfigFreigabeErteilt = false;
		printf_P(ISTR(GesperrtBestaetigung, Sprache));
		}

	cgi_PrintHttpheaderEnd();
	}
	
	
#ifdef ITELEX_ANSCHLUSS

const PROGMEM char NetzRufnummer_P[] = "NETZRUFNR";
const PROGMEM char Geheimzahl_P[] = "PIN";
const PROGMEM char DynIPAktiv_P[] = "DYNIPAKTIV";
const PROGMEM char NetzPort_P[] = "NETZPORT";
const PROGMEM char SelbstAnrufPeriode_P[] = "SELBSTANPER";

#endif // ITELEX_ANSCHLUSS

const PROGMEM char RufnrServerAdr1_P[] = "RUFNRSERV1";
const PROGMEM char RufnrServerAdr2_P[] = "RUFNRSERV2";
const PROGMEM char RufnrServerAdr3_P[] = "RUFNRSERV3";
const PROGMEM char TlnServSyncGeheimzahl_P[] = "SYNCPIN";

const char* RufnrServerAdr_P[] = { RufnrServerAdr1_P, RufnrServerAdr2_P, RufnrServerAdr3_P } ; // liegt dann zwar im RAM, ist aber halt so...
	

/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface zum Ändern der Einstellungen des iTelex-Interface bezüglich der Einbindung
 * in das globale ip-netz
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/
 
void itelex_cgi_config_extern(void *pStruct)
	{
	static TSprache Sprache;
	
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	uint8_t i;
	
	PruefeSprache(pStruct, &Sprache);

	if (!KonfigFreigabe(pStruct, Sprache, true))
		return;
	
	cgi_PrintHttpheaderStart();

	if ( http_request->argc == 0 )
		{
		CgiFormStartTabbed_P(PSTR("itelexcfg-extern.cgi"));

		#ifdef ITELEX_ANSCHLUSS
		CgiFormInputFieldULong_P(ISTR(ITelexRufnummer, Sprache), NetzRufnummer_P, 10, NetzRufnummer);
		CgiFormInputFieldULong_P(ISTR(RufnrServerAnmeldGeheimzahl, Sprache), Geheimzahl_P, 6, Geheimzahl);
		CgiFormCheckbox_P(ISTR(DynIPAktiv, Sprache), DynIPAktiv_P, DynIP_Phase != DynIP_Inaktiv);
		CgiFormInputFieldULong_P(ISTR(VerbindungstestPeriode, Sprache), SelbstAnrufPeriode_P, 3, SelbstAnrufPeriode);
		CgiFormInputFieldULong_P(ISTR(OeffentlichePortNr, Sprache), NetzPort_P, 6, NetzPort);
		#endif // ITELEX_ANSCHLUSS
		
		for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
			CgiFormInputFieldText_P(ISTR(RufnrServerAdr, Sprache), RufnrServerAdr_P[i], TlnAdresseMax, TeilnehmerServerAdresse[i]);

		#ifdef ITELEX_TLNSERVER
		CgiFormInputFieldULong_P(ISTR(TlnServSyncGeheimzahl, Sprache), TlnServSyncGeheimzahl_P, 10, TlnServSyncGeheimzahl);
		#endif //def ITELEX_TLNSERVER
		
		CgiFormFinish_P(ISTR(EinstellungenUebernehmen, Sprache));
		}
	else // argc > 0
		{
		printf_P(ISTR(NeueEinstellungen, Sprache));
		printf_P(PSTR("<a href=\"itelexcfg-extern.cgi\">"));
		printf_P(ISTR(Weiter, Sprache));
		printf_P(PSTR("</a>"));

		#ifdef ITELEX_ANSCHLUSS
		NetzRufnummer = CgiCheckULong_P(http_request, ISTR(ITelexRufnummer, Sprache), NetzRufnummer_P, NetzRufnummer, Sprache);
		if (NetzRufnummer < GlobRufnrMinWert)
			printf_P(ISTR(ITelexRufnummerZuKurz, Sprache));
		Geheimzahl = CgiCheckULong_P(http_request, ISTR(RufnrServerAnmeldGeheimzahl, Sprache), Geheimzahl_P, Geheimzahl, Sprache);

		if (CgiCheckBool_P(http_request, ISTR(DynIPAktiv, Sprache), DynIPAktiv_P, DynIP_Phase != DynIP_Inaktiv, Sprache))
			DynIP_Phase = DynIP_Bestaetigt; 
		else
			DynIP_Phase = DynIP_Inaktiv;
			
		SelbstAnrufPeriode = CgiCheckULong_P(http_request, ISTR(VerbindungstestPeriode, Sprache), SelbstAnrufPeriode_P, SelbstAnrufPeriode, Sprache);
		NetzPort = CgiCheckULong_P(http_request, ISTR(OeffentlichePortNr, Sprache), NetzPort_P, NetzPort, Sprache);
		
		#endif //def ITELEX_ANSCHLUSS
		
		#if defined(ITELEX_ANSCHLUSS) || defined(ITELEX_TLNSERVER)
		for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
			{
			CgiCheckText_P(http_request, ISTR(RufnrServerAdr, Sprache), RufnrServerAdr_P[i], TlnAdresseMax, TeilnehmerServerAdresse[i], Sprache);
			TeilnehmerServerIP[i] = 0; // damit diese neu ermittelt wird.
			}
		#endif //defined(ITELEX_ANSCHLUSS) || defined(ITELEX_TLNSERVER)

		#ifdef ITELEX_TLNSERVER
		TlnServSyncGeheimzahl = CgiCheckULong_P(http_request, ISTR(TlnServSyncGeheimzahl, Sprache), TlnServSyncGeheimzahl_P, TlnServSyncGeheimzahl, Sprache);
		#endif //def ITELEX_TLNSERVER
		
		SpeichereSpracheAlsLokal(Sprache);

		#ifdef ITELEX_ANSCHLUSS
		if (SelbstAnrufPhase == SelbstAnrufSperre)
			SelbstAnrufPhase = SelbstAnrufRuhe;
		SelbstAnrufFehlerZaehler = 0;
		#endif //def ITELEX_ANSCHLUSS
			
		} // else argc > 0
		
	cgi_PrintHttpheaderEnd();

	} // itelex_cgi_config_extern()
	

#ifdef ITELEX_ANSCHLUSS

/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface für eine Bus-Status-Liste (TWI-Busteilnehmer)
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void itelex_cgi_TwiTlnListe(void *pStruct)
	{
	static TSprache Sprache;
	
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	char Buf[10];
	
	PruefeSprache(pStruct, &Sprache);
	
	cgi_PrintHttpheaderStart();

	printf_P(ISTR(TwiTlnListeAnfang, Sprache));
	for (uint8_t AnzZif = 1 ; AnzZif <= 2 ; AnzZif++)
		for (uint8_t Wahl = 0 ; Wahl <= ((AnzZif == 1) ? 9 : 99) ; Wahl++)
			{
			uint8_t BusNr = WahlZuAdresse(Wahl, AnzZif);
			AdresseZuWahlStr(BusNr, Buf);
			int16_t Stat = ((BusNr == BusEigenAdresse) ? Status : GetStatus(BusNr));
			if (Stat >= 0)
				{
				printf_P(ISTR(TwiTlnListeEintrag, Sprache), Buf, Stat);
				}
			}
	printf_P(ISTR(TwiTlnListeEnde, Sprache));

	cgi_PrintHttpheaderEnd();
	
	}

#endif // ITELEX_ANSCHLUSS

	
#if defined(MMC)
	
#include "system/filesystem/fat.h"
#include "system/filesystem/filesystem.h"
	
//! Erzeugt Inhaltsverzeichnis der SD-Karte als HTML-Seite.
//---------------------------------------------------------
void cgi_SdDirectory(void *pStruct)
	{
	static TSprache Sprache;

	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	char *BaseDir;
	
	PruefeSprache(pStruct, &Sprache);
	
	if (!KonfigFreigabe(pStruct, Sprache, true))
		return;
	
	cgi_PrintHttpheaderStart();

	struct fat_dir_entry_struct directory;
	struct fat_dir_struct* dd;
	
	// Löschkommando?
	if (http_request->argc != 0 && PharseCheckName_P(http_request, PSTR("del")))
		{
		char *FileToDelete = http_request->argvalue[PharseGetValue_P(http_request, PSTR("del"))];
		struct fat_dir_entry_struct dir_entry;
		
		if (fat_get_dir_entry_of_path(fs, FileToDelete, &dir_entry))
			if (fat_delete_file(fs, &dir_entry))
				printf_P(PSTR("File %s deleted<p>"), FileToDelete);
			else				
				printf_P(PSTR("File %s NOT deleted<p>"), FileToDelete);
		else				
			printf_P(PSTR("File %s NOT found<p>"), FileToDelete);
		}

	// Wenn nur filename dann Stammverzeichniss wählen, wenn nicht Verzeichnis wählen
	if (http_request->argc == 0 || !PharseCheckName_P(http_request, PSTR("dir")))
		{
		fat_get_dir_entry_of_path(fs, "/" , &directory);
		BaseDir = NULL;
		printf_P(PSTR("<b>Content of /:"));
		}
	else
		{
		BaseDir = http_request->argvalue[PharseGetValue_P(http_request, PSTR("dir"))];
		fat_get_dir_entry_of_path(fs, BaseDir, &directory);
		printf_P(PSTR("<b>Content of %s:"), BaseDir);
		}
	printf_P(PSTR("</b> (%lu free)<br>"), fat_get_fs_free(fs));
		
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
					printf_P(PSTR("<a href =\"sddir.cgi?dir=%s\">%s</a> DIR<br>"), 
														    dir_entry.long_name, 
															     dir_entry.long_name);
				else
					printf_P(PSTR("<a href =\"sddir.cgi?dir=%s/%s\">%s</a> DIR<br>"), 
															BaseDir, 
															   dir_entry.long_name, 
															        dir_entry.long_name);
				}
			else
				{ // normale Datei
				if (BaseDir == NULL)
					printf_P(PSTR("<a href =\"%s\">%s</a> %ld <small><a href =\"sddir.cgi?del=%s\">delete</a></small><br>"), 
									          dir_entry.long_name, 
											       dir_entry.long_name, 
												          dir_entry.file_size,                dir_entry.long_name);
				else
					printf_P(PSTR("<a href =\"%s/%s\">%s</a> %ld <small><a href =\"sddir.cgi?dir=%s&del=%s/%s\">delete</a></small><br>"), 
											  BaseDir, 
											     dir_entry.long_name, 
												      dir_entry.long_name, 
													         dir_entry.file_size,               BaseDir,BaseDir, 
															                                            dir_entry.long_name);
				}
			}
		fat_close_dir(dd);
		}
	else
		printf_P(PSTR("Error reading directory!"));
	
	cgi_PrintHttpheaderEnd();
	}

#endif //defined(MMC)
	

// folgende Funktionen könnten auch mal in eine Library...

//! Gibt eine Zeile in eine Intel-HEX-Datei aus.
//----------------------------------------------
static void IntelHexWriteLine(uint8_t Type, uint16_t Address, uint8_t Len, uint8_t* pData)
	{
	uint8_t i;
	uint8_t CheckSum;
	
	printf_P(PSTR(":%02X%04X%02X"), Len, Address, Type);
	CheckSum = 0 - Type - (Address >> 8) - (Address & 0xFF) - Len; // Überlauf ist beabsichtigt.
	for (i = 0 ; i < Len ; i++)
		{
		printf_P(PSTR("%02X"), pData[i]);
		CheckSum -= pData[i]; // Überlauf ist beabsichtigt.
		}
	printf_P(PSTR("%02X\r\n"), CheckSum);
	}
	
	
//! Gibt den Speicherinhalt des XRAM in eine Intel-HEX-Datei aus.
//---------------------------------------------------------------
//! \par LastReset bei true wird die "zweite" Seite ausgegeben: Vor dem 
//! RAM-Test (nach Reset) wird der vorgefundene Speicherinhalt in die 
//! "zweite Seite" des externen RAM gerettet wird.
static void RamHexdump(bool LastReset)
	{
	enum { Blocklen = 16 };
	volatile uint8_t* p;
	uint16_t address;
	uint8_t Buf[Blocklen];
	uint8_t i;

	for (address = 0x2200 ; address < 0xffff ; address += Blocklen)
		{
		for (i = 0 ; i < Blocklen ; i++)
			{
			uint8_t h;
			p = (uint8_t*) (address + i);
		
			uint8_t SregTemp = SREG;
			
			if (LastReset)
				{
				cli();
				PORTD |= ( 1<<PD7 );
				}
				
			h = *p;
			
			PORTD &= ~( 1<<PD7 );
			
			SREG = SregTemp;
			
			Buf[i] = h;
			
			}
			
		IntelHexWriteLine(0, address, Blocklen, Buf);
		
		if (address >= 0xffff - Blocklen)
			break; // da der normale Abbruch der for-Anweisung nie wirkt.
		
		}
	IntelHexWriteLine(1, 0, 0, NULL);
	}
		

//! Wird bei Abruf von "memdump.hex" aufgerufen.
//----------------------------------------------
void cgi_MemDump(void *pStruct)
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	
	if (http_request->argc != 0 && PharseCheckName_P(http_request, PSTR("cur")))
		RamHexdump(false); // den aktuellen RAM Inhalt speichern
	else
		RamHexdump(true); // den RAM Inhalt vor dem letzten Reset speichern.
	}
	
	
//! Hält für Debugging-Zwecke den Stackpointer fest
volatile uint16_t DebugSP;


//! Rettet beim Watchdog-Reset den Stack und Stackpointer...
ISR(WDT_vect)
	{
	uint16_t Size;
	
	cli();
	wdt_reset();
	
	LED_on(ROT);
	LED_on(GELB);
	LED_on(GRUEN);
	
	DebugSP = SP;
	Size = 0x21FF - DebugSP; 
	memcpy((void*) (0xFFFF - Size + 1), (void*) (DebugSP + 1), Size);
		// Der genutzte Stack-Bereich beginnt erst bei SP+1, da ein PUSH erst die Daten nach
		// SP kopiert und danach SP dekrementiert wird.
		// "0xFFFF - Size + 1" statt "0x10000 - Size" damit nur mit 16 Bit gerechnet wird.
		// Ergebnis: Wenn Size == 1 wird das einzige zu kopierende Byte von 21FF nach FFFF kopiert.
	
	while (DebugSP != 0)
		; // hier kommt es dann zum nächsten Watchdog-Timerüberlauf, der dann einen Reset macht.
	
	// der folgende Programmcode hat nur den Zweck, im Simulator das Stack-Abbild zurück zu kopieren.
	// ----------------------------------------------------------------------------------------------
	SP = DebugSP; // Bei dieser Anweisung den Debugger starten, nachdem memdump.hex ins Extended RAM kopiert wurde.
	Size = 0x21FF - DebugSP;
	memcpy((void*) (DebugSP + 1), (void*) (0xFFFF - Size + 1), Size);
	}
	

//! Behandelt "Werkseinstellungen" durch Tastendruck beim Hochfahren.
//------------------------------------------------------------------------
//! Wird aufgerufen, wenn Taste während der Boot-Phase gedrückt wird.

static void iTelexInit_Taste()
	{
	TKurzTimer TasteTimer;
	
	LED_off(ROT);
	LED_on(GELB);
	LED_off(GRUEN);
	LED_off(BLAU);
	
	StartKurzTimer(&TasteTimer);
	while (KurzTimerVal(&TasteTimer) <= KurzTimerFreq * 2/10) // 0,2 Sekunden Loslassen abwarten
		{
		if (!get_Taste())
			StartKurzTimer(&TasteTimer);
		}

	LED_on(ROT);
	LED_off(GELB);
		
	// Taste ist jetzt losgelassen.
	// kein Tastendruck für 3 Sekunden --> Ausstieg ohne Werkseinstellungen
	// Kurzer Tastendruck --> nur "DHCP = on"
	// Langer Tastendruck --> alles Reset
	StartKurzTimer(&TasteTimer);
	bool Gedrueckt = false;
	while (KurzTimerVal(&TasteTimer) <= KurzTimerFreq * 30/10 || Gedrueckt)
		{
		if (!get_Taste()) // Low = gedrückt!
			{
			if (!Gedrueckt)
				{ // gerade erst gedrückt
				StartKurzTimer(&TasteTimer);
				Gedrueckt = true;
				LED_on(GRUEN);
				}
			else if (KurzTimerVal(&TasteTimer) > KurzTimerFreq * 30/10) 
				{ // 3 Sekunden lang gedrückt --> Total-Reset des EEPROM
				LED_off(ROT);
				LED_on(GELB);
				LED_on(GRUEN);
				LED_on(BLAU);
				makeConfig(); // löscht Konfiguration im EEPROM
				StartKurzTimer(&TasteTimer);
				while (KurzTimerVal(&TasteTimer) <= 1 * KurzTimerFreq)
					; // nix, einfach noch die LED etwas leuchten lassen...
				softreset();
				}
			else if (KurzTimerVal(&TasteTimer) > KurzTimerFreq * 20/10) 
				LED_on(BLAU); // nach 2 Sekunden geht zur Warnung blau an.
			} // if Taste momentan gedrückt
		else  
			{ // if Taste momentan losgelassen
			if (Gedrueckt) 
				{ // war aber gerade gedrückt
				if (KurzTimerVal(&TasteTimer) > KurzTimerFreq * 2/10) 
					{ // Taste kurz gedrückt --> DHCP = on durch löschen von DHCP
					LED_off(ROT);
					LED_on(GELB);
					LED_off(GRUEN);
					LED_on(BLAU);
					deleteConfig_P(PSTR("DHCP"));
					StartKurzTimer(&TasteTimer);
					while (KurzTimerVal(&TasteTimer) <= 1 * KurzTimerFreq)
						; // nix, einfach noch die LED etwas leuchten lassen...
					softreset();
					}
				StartKurzTimer(&TasteTimer);
				Gedrueckt = false;
				LED_off(GRUEN);
				} // Taste gerade eben losgelassen
			}
		} // while Timer < 3 Sekunden oder TasteGedrueckt

	LED_off(ROT);
	LED_off(GELB);
	LED_off(GRUEN);
	LED_on(BLAU);
	} // iTelexInit_Taste()
	
	
/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Initialisiert den iTelex-clinet und registriert den Port auf welchen dieser lauschen soll.
 * \param 	NONE
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/

void itelex_init()
	{
	ResetFlags = MCUSR; // was war die Ursache des letzten Reset?
	MCUSR = 0;
	
	WatchdogTestTimerEnde = 0;
	
	init_Taste();
	init_RTS();
	init_CTS();

	ProtokollInit();
	ProtokollierenInt_P(PSTR("Neustart " SVNVERSION " Reset-Flags %02X\r\n"), ResetFlags);

	printf_P(PSTR("itelex_init:\r\n"));
	
	DiagnosePuffer[0] = '\0';
	DiagnosePufferLevel = 0;

	#ifdef ITELEX_ANSCHLUSS
	
	SeriellUmsetzInit();

	PufferInit(&SendePuffer);
	PufferInit(&EmpfPuffer);
	SocketBufInit();

	AsciiDruckPuffer[0] = '\0';
	HtmlSendeText[0] = '\0';

	printf_P(PSTR("...SendePuffer, EmpfPuffer ok\r\n"));
	
	#endif // ITELEX_ANSCHLUSS
	
	// EEPROM auslesen
	char Buf[TlnAdresseMax];
	uint16_t i;

	#ifdef ITELEX_ANSCHLUSS
	
	if (readConfig_P(EigeneNummer_P, Buf) == 1)
		BusEigenAdresse = WahlZuAdresse(atoi(Buf), strlen(Buf));
	else
		BusEigenAdresse = 22 << 1;

	FesteHauptstelle = ReadConfigBool(FesteHst_P, false);

	AlternativSucheBeiBesetzt = ReadConfigBool(AlternBeiBes_P, true);
	
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

	if (ReadConfigBool(DynIPAktiv_P, false))
		DynIP_Phase = DynIP_Fehler; 
			// Damit ist erst mal Selbst-Anruf ausgeschaltet, aber die Aktualisierung wird bald ausgeführt.
	else
		DynIP_Phase = DynIP_Inaktiv;
		
	if (readConfig_P(SelbstAnrufPeriode_P, Buf) == 1)
		SelbstAnrufPeriode = atoi(Buf);
	else
		SelbstAnrufPeriode = 45;
	
	if (readConfig_P(NetzPort_P, Buf) == 1)
		NetzPort = atol(Buf);
	else
		NetzPort = ITELEX_PORT;

	if (readConfig_P(DatumDruckModus_P, Buf) == 1)
		DatumDruckModus = atoi(Buf);
	else
		DatumDruckModus = DatumDruckBeide;

	#endif // ITELEX_ANSCHLUSS

	#ifdef ITELEX_TLNSERVER
	if (readConfig_P(TlnServSyncGeheimzahl_P, Buf) == 1)
		TlnServSyncGeheimzahl = atol(Buf);
	else
		TlnServSyncGeheimzahl = 0;
	#endif //def ITELEX_TLNSERVER
	
	for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
		{
		if (readConfig_P(RufnrServerAdr_P[i], TeilnehmerServerAdresse[i]) == 1)
			; // ok
		else
			TeilnehmerServerAdresse[i][0] = '\0';
		TeilnehmerServerIP[i] = 0;
		TeilnehmerServerFehlerZaehler[i] = 0;
		StartLangTimer(&TeilnehmerServerSperrTimer[i]);
		}
	TeilnehmerServerAlleNichtErreichbar = false;

	if (readConfig_P(KonfigPasswort_P, KonfigPasswort) != 1)
		KonfigPasswort[0] = '\0';
	KonfigFreigabeErteilt = false;

	TlnBuchOffen = ReadConfigBool(TlnBuchOffen_P, true);
	
	LangeDienstmeldungen = ReadConfigBool(LangeDienstmeldungen_P, false);
	
	if (readConfig_P(MeldungsdruckLevel_P, Buf) == 1)
		MeldungsdruckLevel = atoi(Buf);
	else
		MeldungsdruckLevel = 2;

	if (readConfig_P(LokaleSprache_P, Buf) == 1)
		LokaleSprache = atoi(Buf);
	else
		LokaleSprache = Deutsch;

		
	// dies müsste eigentlich in Protokoll.c enthalten sein.
	if (readConfig_P(ProtokollLevel_P, Buf) == 1)
		ProtokollLevel = atoi(Buf);
	else
		ProtokollLevel = NurFehler;
	SocketProtokollEin = ProtokollLevel >= 10;
	if (SocketProtokollEin)
		ProtokollLevel -= 10;
		
	// dies müsste eigentlich in Protokoll.c enthalten sein.
	if (readConfig_P(ProtokollLevelTlnServ_P, Buf) == 1)
		ProtokollLevelTlnServ = atoi(Buf);
	else
		ProtokollLevelTlnServ = NurFehler;
		
	TeilnehmerServerSocket = NO_SOCKET_USED;
	AktTlnServerTabI = 0;

	printf_P(PSTR("...Config ok\r\n"));

	#ifdef ITELEX_ANSCHLUSS
	
	BusEigenAdrMehrfach = 1; // muss Potenz von 2 sein (also 1, 2, 4, 8, 16, ... , Standard = 1

	iTelexSocketHandle = NO_SOCKET_USED;
	iTelexSocketMode = SocketIdle;
	iTelexSocketIP = 0;
	iTelexSocketAbbauGeplant = false;
	StartKurzTimer(&iTelexSocketAbbruchTimer);
	SocketOutBufUsed = 0;
	SocketInBufUsed = 0;

	StartKurzTimer(&GrundstellungPruefTimer);
	
	iTelexBlindSocketHandle = NO_SOCKET_USED;
	StartKurzTimer(&iTelexBlindSocketAbbauVerzoegerung);
	
	NetzEigeneIP = 0;

	SelbstAnrufPhase = SelbstAnrufSperre; // da noch keine eigene IP bekannt.
	SelbstAnrufFehlerZaehler = 0;
	SelbstAnrufSocketHandle = NO_SOCKET_USED;

	ZeitUeberwachungInit(&SelbstAnrufZeitUeberwachung, 1 * KurzTimerFreq);
	
	TwiInit();

	TWCR = (1<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);

	Timer0Cnt_Max = 0;
	Timer0Callback_Max = 0;

	StartLangTimer(&DynIPAktualisierungTimer);
	DynIPAktualisierungEndzeit = LangTimerMinuteFaktor / 2; // 1/2 Minute 
		
	Status = (1 << StatBit_Frei) | (1 << StatBit_LeitungKennung);

	timer0_init(iTelexTimerFreq); 
	if (!timer0_RegisterCallbackFunction(itelex_timerEvent))
		return;

	printf_P(PSTR("...Timer-Callback-Funktion ok\r\n"));
		
	wdt_enable(WDTO_250MS);  
	WDTCSR |= (1 << WDIE); // Interrupt-Mode auch aktivieren, somit Modus Interrupt + Reset aktiv
		// in itelex_timerEvent wird wdt_reset() ausgefährt.

	if (!get_Taste()) // Gedrückt = LOW!
		iTelexInit_Taste();
		
	Tastendruck = NichtGedr;

	StartKurzTimer(&ITelexThreadCheckTimer);
		
	cgi_RegisterCGI( itelex_cgi_msg_In, PSTR("itelex-msg-in.cgi"));
	cgi_RegisterCGI( itelex_cgi_msg_Out, PSTR("itelex-msg-out.cgi"));
	cgi_RegisterCGI( itelex_cgi_TwiTlnListe, PSTR("itelex-twitlnliste.cgi"));
	
	#endif // ITELEX_ANSCHLUSS
	
	cgi_RegisterCGI( itelex_cgi_config_intern, PSTR("itelexcfg-intern.cgi"));
	cgi_RegisterCGI( itelex_cgi_config_extern, PSTR("itelexcfg-extern.cgi"));
	cgi_RegisterCGI( itelex_cgi_config_sperren, PSTR("itelexcfg-sperren.cgi"));
	cgi_RegisterCGI( itelex_cgi_debug, PSTR("itelex-debug.cgi"));
	cgi_RegisterCGI( cgi_MemDump, PSTR("memdump.hex")); 
	
#if defined(MMC)
	cgi_RegisterCGI( cgi_SdDirectory, PSTR("sddir.cgi"));
#endif //defined(MMC)

#ifdef ISP_MASTER
	InitIspMaster();
#endif //def ISP_MASTER

	cgi_RegisterCGI( ConfigNtpCgi, PSTR("ntp.cgi"));
	
	printf_P(PSTR("...Cgi ok\r\n"));

	#ifdef ITELEX_ANSCHLUSS
	
	RegisterTCPPort(ITELEX_PORT);
	
	Timer0Cnt_Min = 255;

	printf_P( PSTR("iTelex Port %u.\r\n") , ITELEX_PORT );

	THREAD_RegisterThread( itelex_thread, PSTR("iTelex"));

	#endif // ITELEX_ANSCHLUSS
	
	TlnBuchInit();

	printf_P(PSTR("...TlnBuch ok\r\n"));
	
	#ifdef ITELEX_TLNSERVER
	
	itelex_tlnserv_init();

	printf_P(PSTR("...Teilnehmer-Server ok\r\n"));
	
	#endif // ITELEX_TLNSERVER

	#ifdef ITELEX_EMAIL
	
	itelex_email_init();

	printf_P(PSTR("...Email ok\r\n"));
	
	#endif //def ITELEX_EMAIL
	
	CLOCK_GetTime(&SystemStartZeit);	

	void UseEEConfig();
	UseEEConfig();
	}


#endif //def iTelex


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


// EEPROM-Vorbelegung:
// -------------------

EEMEM struct Config EE_ConfigHead = { configID, E2END - 40 /* hier gehört eigentlich die wirkliche Länge des genutzten Config-Bereichs hin. */ } ;
EEMEM char EE_Dummy = '\0';
EEMEM char EE_ConfigData[] =
	"MAC=00:22:F9:01:4E:CE\r"
	"DHCP=on\r"
	"IP=192.168.1.101\r"
	"MASK=255.255.255.0\r"
	"GATE=192.168.178.1\r"
	"DNS=192.168.178.1\r"
	"ALTERNBEIBES=1\r"
	"FESTEHPST=1\r"
	"EIGENENUMMER=66\r"
	"HAUPTSTELLE=31\r"
	"POPSERVER=winmail.qwmail.de\r"
	"SMTPSERVER=winmail.qwmail.de\r"
	"EMAILADR=xxx@teleprinter.net\r"
	"EMAILFILTERKENNUNG=off\r"
	"EMAILABFRTAKT=0\r"
	"SELBSTANPER=45\r"
	"TLNBUCHOFFEN=off\r"
	"RUFNRSERV1=sonnibs.dyndns.org\r"
	"RUFNRSERV2=df3oe.no-ip.org\r"
	"RUFNRSERV3=120.146.186.6\r"
	"SPRACHE=0\r"
	"NTP=on\r"
	"NTPSERVER=time.fu-berlin.de\r"
	"UTCZONE=1\r"
	"AUTODST=on\r"
	"PROTLEVEL=2\r"
	"PROTLEVELTLNSRV=2\r"
	"MELDRUCK=4" ;
EEMEM char EE_ConfigDataEnd[] = "\0";


//! Die folgende Funktion hat nur den Zweck, dass die EEPROM Daten überhaupt irgendwo verwendung finden.
//! Sonst würden sie vom Linker wegoptimiert werden.

volatile uint8_t EEUsageDummy;

void UseEEConfig()
	{
	EEUsageDummy = EE_ConfigHead.TAG[0];
	EEUsageDummy = EE_Dummy;
	EEUsageDummy = EE_ConfigData[0];
	EEUsageDummy = EE_ConfigDataEnd[0];
	}
	

//@}

