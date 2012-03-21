#include <avr/pgmspace.h>
#include <avr/version.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <bool.h>

#include "config.h"
#include "system/clock/clock.h"
#include "hardware/uart/uart_core.h"


#if defined(MMC)
	#include "system/filesystem/fat.h"
	#include "system/filesystem/filesystem.h"
#endif
	
#include "system/stdout/stdout.h"

#include "Protokoll.h"


enum { MaxPuffer = 512 }; //!< Länge des Protokoll-Puffers

char Puffer[MaxPuffer]; //!< Puffert Meldungen bis es Zeit ist, diese auf SD-Karte zu speichern.

char Dateiname[40]; //!< Aktueller Dateiname für die Protokolldatei.

enum { MaxDateigroesse = 100000UL } ; //!< Maximale Dateigröße. Bei überschreitung wird die nächste Datei angefangen.

bool Idle; //!< Speichert, ob es zu protokollierende Ereignisse gab.


extern char *DebugMsg;


//! Schreibt die zwischengespeicherten Daten auf die SD-Karte.
//! \retval true, wenn Puffer gespeichert wurde.
bool ProtokollSpeichern()
	{
	if (Puffer[0] == '\0')
		return true; // nichts zu speichern 
		
#if defined(MMC)

	struct fat_dir_struct* dd;
	struct fat_file_struct* fd;
	struct fat_dir_entry_struct directory;
	struct fat_dir_entry_struct dir_entry;
	uint8_t Res;

	if  (fs == NULL) 

#endif //defined(MMC)

		{ // Filesystem nicht bereit --> auf RS232 senden.
		for (char *p = Puffer ; *p != '\0' ; p++)
			UART_SendByte(0, *p);
		Puffer[0] = '\0';
		return true;
		}

#if defined(MMC)

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
					sprintf_P(DebugMsg, PSTR("fat_seek_file versagt"));
					fat_close_file(fd);
					fd = NULL;
					}
				} // fat_open_file war erfolgreich
			}
		}
		
	if (fd == NULL)
		{ // neue Datei anlegen
		// Dateiname festlegen
		struct TIME Time;
		CLOCK_GetTime(&Time);
		sprintf_P(Dateiname, PSTR("prot_%04u-%02u-%02u_%02d-%02d.txt"), Time.YY, Time.MM, Time.DD, Time.hh, Time.mm);
		
		// Basisverzeichnis öffnen
		Res = fat_get_dir_entry_of_path(fs, "/", &directory);
		if (!Res)
			{
			sprintf_P(DebugMsg, PSTR("fat_get_dir_entry_of_path des Hauptverzeichnis versagt"));
			return false;
			}
			
		dd = fat_open_dir(fs, &directory);
		if (dd == NULL)
			{
			sprintf_P(DebugMsg, PSTR("fat_open_dir des Hauptverzeichnis versagt"));
			return false;
			}

		Res = fat_create_file(dd, Dateiname, &dir_entry);
		fat_close_dir(dd);
		
		if (Res == 0)
			{
			sprintf_P(DebugMsg, PSTR("fat_create_file(%s) versagt"), Dateiname);
			return false;
			}
			
		fd = fat_open_file(fs, &dir_entry); 
		if (fd == NULL)
			{
			sprintf_P(DebugMsg, PSTR("fat_open_file der neuen Datei %s versagt"), Dateiname);
			return false;
			}
		}
	
	// jetzt muss fd geöffnet sein.
	if (fat_write_file(fd, (uint8_t*) Puffer, strlen(Puffer)) <= 0)
		{
		sprintf_P(DebugMsg, PSTR("fat_write_file versagt"));
		fat_close_file(fd);
		return false;
		}
		
	fat_close_file(fd);
	Puffer[0] = '\0';
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
	Idle = false;
	if (len == 0 || len > MaxPuffer / 2)
		return false;
	
	if (strlen(Puffer) + len + 2 >= MaxPuffer)
		{
		if (!ProtokollSpeichern())
			return false; // kein Platz mehr.
		}

	if (Puffer[0] == '\0')
		{ // Zeit protokollieren
		struct TIME Time;

		CLOCK_GetTime(&Time);
		sprintf_P(Puffer, PSTR("\r\n++++++ %02u.%02u.%04u ++++++ %02d:%02d ++++++\r\n"),
			  Time.DD, Time.MM, Time.YY, Time.hh, Time.mm);
		}
	return true;
	}
		
	
//! Protokolliert einen beliebigen Text.
void Protokollieren(char *s)
	{
	if (s != NULL && ProtPraeparieren(strlen(s)))
		strcat(Puffer, s);
	}
	

//! Protokolliert einen beliebigen Text.
void Protokollieren_P(const char *s)
	{
	if (s != NULL && ProtPraeparieren(strlen_P(s)))
		strcat_P(Puffer, s);
	}
	

//! Protokolliert einen Text mit einer Zahl (Printf-Format verwenden).
void ProtokollierenInt_P(const char *s, long i)
	{
	if (s != NULL && ProtPraeparieren(strlen_P(s) + 10))
		sprintf_P(Puffer + strlen(Puffer), s, i);
	}

	
//! Speichert zwischengespeicherten Protokolltext auf SD-Karte, sobald Ruhe eingekehrt ist.	
static void SpeichernBeiIdle()
	{
	if (Idle)
		ProtokollSpeichern();
	else
		Idle = true;
	}
	

//! Initialisiert die Protokollierung.
void ProtokollInit()
	{
	Puffer[0] = '\0';
	Dateiname[0] = '\0';
	Idle = true;
	CLOCK_RegisterCallbackFunction(SpeichernBeiIdle, MINUTE);
	Protokollieren("Neustart\r\n");
	}
	

