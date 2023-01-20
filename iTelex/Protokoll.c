#include <avr/pgmspace.h>
#include <avr/version.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "config.h"
#include "system/clock/clock.h"
#include "system/net/ip.h"
#include "system/net/arp.h"
#include "hardware/uart/uart_core.h"


#if defined(MMC)
	#include "system/filesystem/fat.h"
	#include "system/filesystem/filesystem.h"
#endif
	
#include "system/stdout/stdout.h"

#include "Protokoll.h"
#include "iTelex.h"

#include "StringTab.h" 
	//!\todo Prio 4 Umstellung noch nicht erfolgt, da Umstellung nur selten eintretende 
	//! Diagnosedrucke betrifft, die nur bei Benutzung der SD-Karte erscheinen.


enum { MaxProtPuffer = 1024 }; //!< Länge des Protokoll-Puffers

char Puffer[MaxProtPuffer]; //!< Puffert Meldungen bis es Zeit ist, diese auf SD-Karte zu speichern.

char Dateiname[40]; //!< Aktueller Dateiname für die Protokolldatei.

enum { MaxDateigroesse = 100000UL } ; //!< Maximale Dateigröße. Bei überschreitung wird die nächste Datei angefangen.

bool Idle; //!< Speichert, ob es zu protokollierende Ereignisse gab.

bool SdGestoert;
	//!< Wenn die SD Karte zwar aktiv ist aber schreiben nicht möglich ist.
	
TProtokollLevel ProtokollLevel;
	//!< "Tiefe" der Protokollierung für "normale" Abläufe

uint8_t ProtokollLevelTlnServ;
	//!< "Tiefe" der Protokollierung für Teilnehmer-Server: 0 = Aus, 1 = Normal, 2 = Intensiv, 3 = im Detail
	//!< Auch Protokollierung der Teilnehmer-Server-Abfragen


TBaudotMode ProtokollBaudotMode;


static bool DruckeUhrzeit;
	//!< speichert, ob die letzte Zeile ein CR LF enthielt, wenn ja wird die 
	//!< nächste Zeile mit Datum / Uhrzeit begonnen
	
static unsigned long LetzteDruckZeit;
	//!< Speichert die Zeit des letzten Protokolliervorgangs.
	//!< Nach 5 Minuten wird eine neue "Kopfzeile" gedruckt.
	
static bool RegelblockAktiv;
	//!< Speichert, ob das Löschen von regelmäßig vorkommenden Meldungen bearbeitet 
	//!< werden soll.
	
static bool InRegelblock;
	//!< Merker ob die folgenden Meldungen regelmäßig vorkommende Meldungen sind.
	

//! Prüft, ob die Protokollierung für den "Level" p stattfinden soll
//! \retval true, wenn ja	
bool ProtokollAktivFuer(TProtokollLevel p)
{
	if (ProtokollLevel > Keine && ProtokollLevel <= AblaeufeAlle)
		return (p > Keine && p <= ProtokollLevel);
	
	if (ProtokollAktivFuerTCP())
		return (p > Keine && p <= ProtokollLevel - TcpVerbindungen);
		
	if (ProtokollLevel >= TelexKommunikationText && ProtokollLevel <= TelexKommunikationAlles)
		return (p >= TelexKommunikationText && p <= ProtokollLevel);
		// Beispiel: Bei #ProtokollLevel 22 wird p = 21 und p = 22 gedruckt, aber p = 23 (noch detaillierter) nicht.

	return false; // dies wirkt auch bei #UhrzeitImpulse
}


bool ProtokollAktivFuerTCP()
	{
	return (ProtokollLevel >= Keine + TcpVerbindungen) && (ProtokollLevel <= AblaeufeAlle + TcpVerbindungen);
	}


bool UhrzeitImpulseAufSeriellerSchnittstelle()
	{
	return ProtokollLevel == UhrzeitImpulse;
	}


//! Schreibt die zwischengespeicherten Daten auf die SD-Karte oder sendet diese an 
//! die serielle Schnittstelle.
//! \param flush Falls True, alle zwischengespeicherten Daten senden
//! \retval true, wenn Puffer gespeichert wurde.
bool ProtokollSpeichern(bool flush)
	{
	if (Puffer[0] == '\0')
		return true; // nichts zu speichern 
		
#if defined(MMC)

	struct fat_dir_struct* dd;
	struct fat_file_struct* fd;
	struct fat_dir_entry_struct directory;
	struct fat_dir_entry_struct dir_entry;
	uint8_t Res;

	if  (fs == NULL || SdGestoert) 

#endif //defined(MMC)

		{ // Filesystem nicht bereit --> auf RS232 senden.
		if (!flush && RegelblockAktiv)
			return true; // nicht ausgeben, da Puffer vielleicht noch gelöscht wird.
			
		while (Puffer[0] != '\0')
			{
			if (UART_GetBytesinTxBuffer(0) > 3)
				{
				if (flush)
					continue; // warten, bis Platz frei ist...
				else
					return false; // Puffer nicht gespeichert
				}
			UART_SendByte(0, Puffer[0]);
			DruckeUhrzeit = (Puffer[0] == '\n');
			memmove(Puffer, Puffer + 1, strlen(Puffer));
			}
		return true;
		}

#if defined(MMC)

	if (!flush)
		return false; //! \todo Prio 3 bei genügendem Inhalt doch speichern...

#if defined(LEDROT_SDKARTE)
	LED_on(ROT); 
#endif //defined(LEDROT_SDKARTE)

	struct TIME Time;
	CLOCK_GetTime(&Time);

	if (Dateiname[0] == '\0')
		fd = NULL;
	else
		{ // prüfen, ob die vorhandene Datei erweitert werden sollte.
		Res = fat_get_dir_entry_of_path(fs, Dateiname, &dir_entry);
		if (!Res)
			fd = NULL; // als Zeichen, eine neue Datei anzulegen.
		else
			{ // vorhandene Datei öffnen
			fd = fat_open_file(fs, &dir_entry); 
			if (fd != NULL)
				{
				int32_t Pos = 0;
				if (fat_seek_file(fd, &Pos, FAT_SEEK_END) > 0)
					{ // Seek war erfolgreich.
					if (Pos + strlen(Puffer) > MaxDateigroesse)
						{ // Datei wird zu groß --> schließen und dann neue anfangen.
						fat_close_file(fd);
						fd = NULL;
						}
					else
						; // nichts. Datei ist geöffnet und kann beschrieben werden.
					}
				else 
					{ // Seek hat versagt --> schließen und dann neue anfangen.
					Diagnoseausgabe_P(PSTR("fat_seek_file versagt"), 1);
						// hier wird SdGestoert nicht gesetzt, vielleicht funktioniert 
						// es ja in einer neuen Datei.
					fat_close_file(fd);
					fd = NULL;
					}
				} // fat_open_file war erfolgreich
			}
		}
		
	if (fd == NULL)
		{ // neue Datei anlegen
		// Dateiname festlegen
		sprintf_P(Dateiname, PSTR("prot_%04u-%02u-%02u_%02d-%02d.txt"), Time.YY, Time.MM, Time.DD, Time.hh, Time.mm);
		
		// Basisverzeichnis öffnen
		Res = fat_get_dir_entry_of_path(fs, "/", &directory);
		if (!Res)
			{
			Diagnoseausgabe_P(PSTR("fat_get_dir_entry_of_path des Hauptverzeichnis versagt"), 1);
			SdGestoert = true;
#if defined(LEDROT_SDKARTE)
			LED_off(ROT); 
#endif //defined(LEDROT_SDKARTE)
			return false;
			}
			
		dd = fat_open_dir(fs, &directory);
		if (dd == NULL)
			{
			Diagnoseausgabe_P(PSTR("fat_open_dir des Hauptverzeichnis versagt"), 1);
			SdGestoert = true;
#if defined(LEDROT_SDKARTE)
			LED_off(ROT); 
#endif //defined(LEDROT_SDKARTE)
			return false;
			}

		Res = fat_create_file(dd, Dateiname, &dir_entry);
		fat_close_dir(dd);
		
		if (Res == 0)
			{
			if (Diagnoseausgabe_P(PSTR("fat_create_file versagt fuer "), 1))
				strcat(DiagnosePuffer, Dateiname);
			SdGestoert = true;
#if defined(LEDROT_SDKARTE)
			LED_off(ROT); 
#endif //defined(LEDROT_SDKARTE)
			return false;
			}
			
		fd = fat_open_file(fs, &dir_entry); 
		if (fd == NULL)
			{
			if (Diagnoseausgabe_P(PSTR("fat_open_file der neuen Datei versagt fuer "), 1))
				strcat(DiagnosePuffer, Dateiname);
			SdGestoert = true;
#if defined(LEDROT_SDKARTE)
			LED_off(ROT); 
#endif //defined(LEDROT_SDKARTE)
			return false;
			}
			
		LetzteDruckZeit = 0; 
			// in neuer Datei immer das Datum vorne einfügen.
			// Damit wird in der neuen Datei auch immer ein Schreibtest gemacht.
		} // neue Datei anlegen.
	
	// jetzt muss fd geöffnet sein.
	
	// ggf. Titelzeile mit Datum und Stunde/Minute
	if (Time.time >= LetzteDruckZeit + 3 * 60)
		{
		char Kopfzeile[40];
		sprintf_P(Kopfzeile, PSTR("\r\n++++++ %02u.%02u.%04u ++++++\r\n"),
			  Time.DD, Time.MM, Time.YY);
		if (fat_write_file(fd, (uint8_t*) Kopfzeile, strlen(Kopfzeile)) <= 0)
			{
			Diagnoseausgabe_P(PSTR("fat_write_file versagt"), 1);
			SdGestoert = true;
			fat_close_file(fd);
			return false;
#if defined(LEDROT_SDKARTE)
			LED_off(ROT); 
#endif //defined(LEDROT_SDKARTE)
			}
		}
	
	// Inhalt des Puffers ausgeben
	if (fat_write_file(fd, (uint8_t*) Puffer, strlen(Puffer)) <= 0)
		{
		Diagnoseausgabe_P(PSTR("fat_write_file versagt"), 1);
			// Kein SdGestoert = true, da es vielleicht in der nächsten Datei funktioniert.
		fat_close_file(fd);
		Dateiname[0] = '\0';
#if defined(LEDROT_SDKARTE)
		LED_off(ROT); 
#endif //defined(LEDROT_SDKARTE)
		return false;
		}

	fat_close_file(fd);

	LetzteDruckZeit = Time.time;
	
	DruckeUhrzeit = (Puffer[strlen(Puffer)-1] == '\n');
	
	Puffer[0] = '\0';

#if defined(LEDROT_SDKARTE)
	LED_off(ROT); 
#endif //defined(LEDROT_SDKARTE)

	return true;

#endif //defined(MMC)

	}
	

//! Bereitet Protokollierung vor.
//------------------------------
//! Ggf. Puffer schreiben, zu lange Texte zurückweisen.
//! \param len Textlänge des zu speichernden Textes.
//! \retval true wenn Puffer beschrieben werden darf.
static bool ProtPraeparieren(int len)
	{
	struct TIME Time;

	if (!InRegelblock)
		RegelblockAktiv = false;
		
	Idle = false;
	if (len == 0 || len > MaxProtPuffer / 2)
		return false;
	
	bool ZeilenEnde = (Puffer[0] != '\0' && Puffer[strlen(Puffer) - 1] == '\n');
	
	if (strlen(Puffer) + len + 2 + 15 >= MaxProtPuffer // 15 = Länge des Datums (sicherheitshalber)
		// Text passt nicht mehr in den Puffer
		|| (ZeilenEnde && strlen(Puffer) + len + 2 + 15 + 60 >= MaxProtPuffer))
		// nächste Zeile passt wahrscheinlich nicht mehr in den Puffer...
		{
		if (!ProtokollSpeichern(true))
			return false; // kein Platz mehr.
		}

	if ((DruckeUhrzeit || ZeilenEnde) && (ProtokollAktivFuer(NurFehler) || ProtokollAktivFuer(TelexKommunikationMitDatum)))
		{ // Hinweis: NurFehler ist die niedrigste Stufe der "Ablauf"-Protokollierung, somit werden alle relevanten Ablauf-Infos mit Datum versehen.
		CLOCK_GetTime(&Time);

		uint8_t SregTemp = SREG;
		cli();

		sprintf_P(Puffer + strlen(Puffer), PSTR("%02d:%02d:%02d,%02d: "), Time.hh, Time.mm, Time.ss, Time.ms);
		DruckeUhrzeit = false;
		ZeilenEnde = false;
		
		SREG = SregTemp;
		}

	return true;
	}
		
	
//! Protokolliert einen beliebigen Text.
void Protokollieren(char *s)
	{
	if (s != NULL && ProtPraeparieren(strlen(s)))
		{
		uint8_t SregTemp = SREG;
		cli();
		
		strcat(Puffer, s);
		
		SREG = SregTemp;
		}
	}
	

//! Protokolliert einen beliebigen Text.
void ProtokollierenC(char c)
	{
	if (ProtPraeparieren(1))
		{
		uint8_t SregTemp = SREG;
		cli();

		uint16_t len = strlen(Puffer);
		Puffer[len] = c;
		Puffer[len+1] = '\0';
		
		SREG = SregTemp;
		}
	}
	

//! Protokolliert einen beliebigen Text.
void Protokollieren_P(const prog_char *s)
	{
	if (s != NULL && ProtPraeparieren(strlen_P(s)))
		{
		uint8_t SregTemp = SREG;
		cli();

		strcat_P(Puffer, s);
		
		SREG = SregTemp;
		}
	}
	

//! Protokolliert einen Text mit einer Zahl (Printf-Format verwenden).
void ProtokollierenInt_P(const prog_char *s, long i)
	{
	if (s != NULL && ProtPraeparieren(strlen_P(s) + 10))
		{
		uint8_t SregTemp = SREG;
		cli();

		sprintf_P(Puffer + strlen(Puffer), s, i);
		
		SREG = SregTemp;
		}
	}

	
static char Buf[20];

	
//! Protokolliert eine IP-Adresse.
void ProtokollierenIPAdr(long aip)
	{
	if (ProtPraeparieren(15)) // 15 = 4 * 3 (Ziffern) + 3 (Punkte)
		{
		uint8_t SregTemp = SREG;
		cli();

		strcat(Puffer, iptostr(aip, Buf));
		
		SREG = SregTemp;
		}
	}
	

//! Protokolliert eine MAC-Adresse.
void ProtokollierenMAC(char mac[6])
	{
	if (ProtPraeparieren(17)) // 17 = 6 * 2 (Hex-Ziffern) + 5 (Doppelpunkte)
		{
		uint8_t SregTemp = SREG;
		cli();

		strcat(Puffer, mactostr(mac, Buf));
		
		SREG = SregTemp;
		}
	}


//! Protokolliert eine Pufferinhalt.
//-----------------------------------
//! Erkennt automatisch, ob Hex oder Ascii...
void ProtokollierenPuffer(char buf[], uint16_t Len)
	{
	uint16_t i;
	uint16_t AnzAscii;
	bool DruckAscii;
	bool InHochkomma;
	
	// Prüfe ob Ascii oder Hex
	AnzAscii = 0;
	for (i = 0 ; i < Len ; i++)
		if (buf[i] >= ' ' && buf[i] <= '~')
			AnzAscii++;
			
	DruckAscii = AnzAscii > (Len / 2);
	InHochkomma = false;

	if (DruckAscii)
		ProtPraeparieren(AnzAscii + AnzAscii / 4 + 3 * (Len - AnzAscii));
			// Schätzung: jedes Ascii-Zeichen ein Buchstabe + 25% Mehraufwand für 
			// Hochkommas und jedes nicht-Ascii-Zeichen drei Buchstaben
	else
		ProtPraeparieren(3 * Len);
		
	// Ausgeben
	for (i = 0 ; i < Len ; i++)
		{
		if (DruckAscii && buf[i] >= ' ' && buf[i] <= '~')
			{
			if (!InHochkomma)
				{
				ProtokollierenC(' ');
				ProtokollierenC('\'');
				InHochkomma = true;
				}
			ProtokollierenC(buf[i]);
			}
		else
			{
			if (InHochkomma)
				{
				ProtokollierenC('\'');
				InHochkomma = false;
				}
			ProtokollierenInt_P(PSTR(" %02X"), buf[i]);
			}
		} // for i
	
	if (InHochkomma)
		ProtokollierenC('\'');
	}


//! Initialisiert die Bearbeitung von regelmäßig vorkommenden Meldungs-Blöcken.
//-----------------------------------------------------------------------------
//! Diese können nachträglich wieder "gelöscht" werden. Folge dieses Funktionsaufrufs ist
//! Die Speicherung aller bisherigen Meldungen auf der SD-Karte.	
void ProtokollRegelblockInit()
	{
	ProtokollSpeichern(true);
	RegelblockAktiv = true;
	InRegelblock = false;
	}


//! Markiert den Beginn eines regelmäßig vorkommenden Meldungs-Blocks.
//-----------------------------------------------------------------------------
void ProtokollRegelblockStart()
	{
	InRegelblock = RegelblockAktiv;
	}


//! Markiert das Ende eines regelmäßig vorkommenden Meldungs-Blocks.
//-----------------------------------------------------------------------------
void ProtokollRegelblockEnde()
	{
	InRegelblock = false;
	}
	

//! Beendet die Bearbeitung von regelmäßig vorkommenden Meldungs-Blöcken.
//-----------------------------------------------------------------------------
//! d.h. auch die vorherigen Meldungen werden doch gedruckt.
void ProtokollRegelblockAbbruch()
	{
	RegelblockAktiv = false;
	InRegelblock = false;
	}

	
//! Löscht die regelmäßig vorkommenden Meldungs-Blöcke wieder aus dem Puffer.
//-----------------------------------------------------------------------------
//! Prozedere: Vor der ersten Meldung ProtokollRegelblockInit() aufrufen.
//! Vor jeder Meldung ProtokollRegelblockStart() und danach ProtokollRegelblockEnde()
//! aufrufen. Wurde bei Aufruf von ProtokollRegelblockLoeschen() keine andere Meldung
//! ("Außerhalb" von ProtokollRegelblockStart() und ProtokollRegelblockEnde() ) 
//! ausgegeben, werden die Meldungen seit ProtokollRegelblockInit() gelöscht.
void ProtokollRegelblockLoeschen()
	{
	if (RegelblockAktiv)
		{
		Puffer[0] = '\0';
		DruckeUhrzeit = true;
		}
	RegelblockAktiv = true;
	InRegelblock = false;
	}

	
//! Speichert zwischengespeicherten Protokolltext auf SD-Karte, sobald Ruhe eingekehrt ist.	
static void SpeichernBeiIdle()
	{
	if (Idle)
		ProtokollSpeichern(true);
	else
		Idle = true;
	}
	

//! Abhaengig von der Protokoll-Stufe wird ggf. die Weiterleitung von STDOUT an die serielle Schnittstelle abgeschaltet.
void ProtokollRedirectStdout()
	{
	if (ProtokollAktivFuer(TelexKommunikationText) || ProtokollAktivFuer(UhrzeitImpulse))
		STDOUT_set(NONE, 0);
	else
		STDOUT_set(RS232, 0);
	}



//! Initialisiert die Protokollierung.
void ProtokollInit()
	{
	ProtokollLevel = NurFehler; // wird später aus der Konfiguration überschrieben
	ProtokollLevelTlnServ = NurFehler; // wird später aus der Konfiguration überschrieben
	Puffer[0] = '\0';
	Dateiname[0] = '\0';
	Idle = true;
	SdGestoert = false;
	CLOCK_RegisterCallbackFunction(SpeichernBeiIdle, MINUTE);
		// durch den Minutentakt wird alle ein bis zwei Minuten der 
		// Protokollinhalt gespeichert.
	DruckeUhrzeit = true;
	LetzteDruckZeit = 0;
	RegelblockAktiv = false;
	InRegelblock = false;
	}
	


