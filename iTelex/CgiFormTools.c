/*! \file CgiFormTools.c \brief Funktionen zum strukturierten Erstellen von CGI-Eingabeformularen */
//***************************************************************************
//*            CgiFormTools.c
//*
//****************************************************************************/
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
#include "config.h"

#include <avr/pgmspace.h>
#include <avr/version.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <bool.h>

#include "apps/httpd/cgibin/cgi-bin.h"
#include "apps/httpd/httpd2_pharse.h"
#include "system/config/eeconfig.h"

#include "iTelex/StringTab.h"


//! Einleitung eines durch Tabelle strukturieten CGI-Eingabeformulares
//--------------------------------------------------------------------
//! \param FormName Dateiname der CGI-Auswertung (im PROGMEM)
void CgiFormStartTabbed_P(const char *FormName)
	{
	printf_P(PSTR("<form action=\""));
	printf_P(FormName); 
	printf_P(PSTR("\"><table border=\"0\" cellpadding=\"5\" cellspacing=\"0\">"));
	}
	

//! Einleitungsteil für alle Eingabefelder
//--------------------------------------------------------------------
//! \param FieldText Beschriftung des Feldes für den Anwender (im PROGMEM)
//! \param FieldLabel Name des Feldes für die Auswertung (im PROGMEM)
void CgiFormFieldIntro_P(const char *FieldText, const char *FieldLabel)
	{
	printf_P(PSTR("<tr><td align=\"right\">"));
	printf_P(FieldText);
	printf_P(PSTR(":</td><td><input name=\""));
	printf_P(FieldLabel);
	}

//! Eingabefeld für Zahlen in einem mit Tabelle strukturieten CGI-Eingabeformular
//--------------------------------------------------------------------
//! \param FieldText Beschriftung des Feldes für den Anwender (im PROGMEM)
//! \param FieldLabel Name des Feldes für die Auswertung (im PROGMEM)
//! \param Size Eingabegröße des Feldes ( = Stellenzahl)
//! \param Value Initialier Wert des Feldes
void CgiFormInputFieldULong_P(const char *FieldText, const char *FieldLabel, int Size, unsigned long Value)
	{
	CgiFormFieldIntro_P(FieldText, FieldLabel);
	printf_P(PSTR("\" type=\"text\" size=\"%d\" value=\"%lu\" maxlength=\"%d\"></td></tr>"), Size, Value, Size);
	}


//! Eingabefeld für Texte in einem mit Tabelle strukturieten CGI-Eingabeformular
//--------------------------------------------------------------------
//! \param FieldText Beschriftung des Feldes für den Anwender (im PROGMEM)
//! \param FieldLabel Name des Feldes für die Auswertung (im PROGMEM)
//! \param Size Eingabegröße des Feldes ( = Zeichenzahl)
//! \param Value Initialier Wert des Feldes
void CgiFormInputFieldText_P(const char *FieldText, const char *FieldLabel, int Size, char *Value)
	{
	CgiFormFieldIntro_P(FieldText, FieldLabel);
	printf_P(PSTR("\" type=\"text\" size=\"%d\" value=\"%s\" maxlength=\"%d\"></td></tr>"), Size, Value, Size);
	}


//! Eingabe-Schaltfeld in einem mit Tabelle strukturieten CGI-Eingabeformular
//--------------------------------------------------------------------
//! \param FieldText Beschriftung des Feldes für den Anwender (im PROGMEM)
//! \param FieldLabel Name des Feldes für die Auswertung (im PROGMEM)
//! \param Value Initialier Wert des Feldes
void CgiFormCheckbox_P(const char *FieldText, const char *FieldLabel, bool Value)
	{
	CgiFormFieldIntro_P(FieldText, FieldLabel);
	printf_P(PSTR("\" type=\"checkbox\" value=\"1\"")); 
	if (Value)
		printf_P(PSTR(" checked"));
	printf_P(PSTR("></td></tr>"));
	}


//! Auswahllisten-Feld in einem mit Tabelle strukturieten CGI-Eingabeformular
//--------------------------------------------------------------------
//! \param FieldText Beschriftung des Feldes für den Anwender (im PROGMEM)
//! \param FieldLabel Name des Feldes für die Auswertung (im PROGMEM)
//! \param NItems Anzahl der Wahlmöglichkeiten
//! \param ItemList Feld der Texte (im PROGMEM)
//! \param Value Aktuelles Feld
void CgiFormDropdown_P(const char *FieldText, const char *FieldLabel, uint8_t NItems, const char **ItemList, uint8_t Value)
	{
	printf_P(PSTR("<tr><td align=\"right\">"));
	printf_P(FieldText);
	printf_P(PSTR(":</td><td><select name=\""));
	printf_P(FieldLabel);
	printf_P(PSTR("\" size=\"1\">"));
	for (uint8_t i = 0 ; i < NItems ; i++)
		{
		if (Value == i)
			printf_P(PSTR("<option selected>"));
		else
			printf_P(PSTR("<option>"));
		printf_P(*ItemList);
		ItemList++;
		printf_P(PSTR("</option>"));
		}
	printf_P(PSTR("</select></td>"));
	}

						
//! Abschicken-Button in einem mit Tabelle strukturieten CGI-Eingabeformular
//--------------------------------------------------------------------
//! \param ButtonText Beschriftung des Feldes für den Anwender (im PROGMEM)
void CgiFormFinish_P(const char *ButtonText)
	{
	printf_P(PSTR("<tr><td></td><td><input type=\"submit\" value=\""));
	printf_P(ButtonText);
	printf_P(PSTR("\"></td></tr></table></form>"));
	}
	

static char Buf[255];

//! Wertet Eingabefeld für Text aus
//--------------------------------------------------------------------
//! \param http_request Zeiger auf die Struktur der CGI-Antwort
//! \param FieldText Bedeutung des Feldes für den Anwender (im PROGMEM)
//! \param FieldLabel Name des Feldes für die Auswertung (im PROGMEM)
//! \param Size Eingabegröße des Feldes ( = Zeichenzahl)
//! \param Value Aktueller Wert des Feldes
void CgiCheckText_P(struct HTTP_REQUEST * http_request, const char *FieldText, const char *FieldLabel, int Size, char *Value, TSprache Sprache)
	{
	if (PharseCheckName_P(http_request, FieldLabel))
		{
		strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, FieldLabel)], Size-1);
		Buf[Size-1] = '\0';
		printf_P(PSTR("<br>"));
		printf_P(FieldText);
		if (strcmp(Buf, Value) == 0)
			{
			printf_P(ISTR(Unveraendert, Sprache));
			printf_P(PSTR(": %s"), Buf);
			}
		else
			{
			printf_P(ISTR(GeaendertIn, Sprache));
			printf_P(PSTR(": %s"), Buf);
			changeConfig_P(FieldLabel, Buf);
			strcpy(Value, Buf);
			}
		} // if PharseCheckName_P()
	} // CgiCheckText_P
	
	
//! Wertet Eingabefeld für kleine Ganzzahl aus
//--------------------------------------------------------------------
//! \param http_request Zeiger auf die Struktur der CGI-Antwort
//! \param FieldText Bedeutung des Feldes für den Anwender (im PROGMEM)
//! \param FieldLabel Name des Feldes für die Auswertung (im PROGMEM)
//! \param Old Aktueller Wert des Feldes
//! \return Neuer Wert der Variable
unsigned long CgiCheckULong_P(struct HTTP_REQUEST * http_request, const char *FieldText, const char *FieldLabel, unsigned long Old, TSprache Sprache)
	{
	unsigned long Neu;
	
	if (PharseCheckName_P(http_request, FieldLabel))
		{
		strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, FieldLabel)], 10);
		Buf[10] = '\0';
		Neu = atol(Buf);
		ltoa(Neu, Buf, 10);
		printf_P(PSTR("<br>"));
		printf_P(FieldText);
		if (Neu == Old)
			{
			printf_P(ISTR(Unveraendert, Sprache));
			printf_P(PSTR(": %lu"), Neu);
			}
		else
			{
			printf_P(ISTR(GeaendertIn, Sprache));
			printf_P(PSTR(": %lu"), Neu);
			changeConfig_P(FieldLabel, Buf);
			}
		return Neu;
		} // if PharseCheckName_P()
	else
		return Old;
	} // CgiCheckULong_P
		
		
//! Wertet Eingabefeld für Boolsche Werte aus
//--------------------------------------------------------------------
//! \param http_request Zeiger auf die Struktur der CGI-Antwort
//! \param FieldText Bedeutung des Feldes für den Anwender (im PROGMEM)
//! \param FieldLabel Name des Feldes für die Auswertung (im PROGMEM)
//! \param Old Aktueller Wert der Variable
//! \return Neuer Wert der Variable
bool CgiCheckBool_P(struct HTTP_REQUEST * http_request, const char *FieldText, const char *FieldLabel, bool Old, TSprache Sprache)
	{
	bool Neu;
	
	if (PharseCheckName_P(http_request, FieldLabel))
		{
		strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, FieldLabel)], 2);
		Buf[2] = '\0';
		Neu = atoi(Buf) != 0; 
		}
	else
		{
		Neu = false;
		Buf[0] = '0', Buf[1] = '\0';
		}
	printf_P(PSTR("<br>"));
	printf_P(FieldText);
	if (Neu == Old)
		{
		printf_P(ISTR(Unveraendert, Sprache));
		printf_P(PSTR(": %u"), Neu);
		}
	else
		{
		printf_P(ISTR(GeaendertIn, Sprache));
		printf_P(PSTR(": %u"), Neu);
		changeConfig_P(FieldLabel, Buf);
		}
	return Neu;
	}
