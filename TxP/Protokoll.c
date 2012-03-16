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
#include "system/filesystem/fat.h"
#include "system/filesystem/filesystem.h"

#include "Protokoll.h"


enum { MaxPuffer = 512 }; //!< Länge des Protokoll-Puffers

char Puffer[MaxPuffer]; //!< Puffert Meldungen bis es Zeit ist, diese auf SD-Karte zu speichern.

char Dateiname[20]; //!< Aktueller Dateiname für die Protokolldatei.

enum { MaxDateigroesse = 100000UL } ; //!< Maximale Dateigröße. Bei überschreitung wird die nächste Datei angefangen.

bool Idle; //!< Speichert, ob es zu protokollierende Ereignisse gab.


extern char *DebugMsg;


//! Schreibt die zwischengespeicherten Daten auf die SD-Karte.
//! \retval true, wenn Puffer gespeichert wurde.
static bool PufferSpeichern()
	{
	struct fat_dir_struct* dd;
	struct fat_file_struct* fd;
	struct fat_dir_entry_struct directory;
	struct fat_dir_entry_struct dir_entry;
	uint8_t Res;

	if (Puffer[0] == '\0' || fs == NULL)
		return true; // nichts zu speichern ODER filesystem nicht bereit.
		
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
	}
	
	
//! Protokolliert einen beliebigen Text.
void Protokollieren(char *s)
	{
	Idle = false;
	if (s == NULL || s[0] == '\0' || strlen(s) > MaxPuffer / 2)
		return;
	
	if (strlen(Puffer) + strlen(s) + 2 >= MaxPuffer)
		{
		if (!PufferSpeichern())
			return; // kein Platz mehr.
		}

	if (Puffer[0] == '\0')
		{ // Zeit protokollieren
		struct TIME Time;

		CLOCK_GetTime(&Time);
		sprintf_P(Puffer, PSTR("%02u.%02u.%04u %02d:%02d\r\n"),
			  Time.DD, Time.MM, Time.YY, Time.hh, Time.mm);
		}
		
	strcat(Puffer, s);
	strcat_P(Puffer, "\r\n");
	}
	

//! Speichert zwischengespeicherten Protokolltext auf SD-Karte, sobald Ruhe eingekehrt ist.	
static void SpeichernBeiIdle()
	{
	if (Idle)
		PufferSpeichern();
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
	}
	

