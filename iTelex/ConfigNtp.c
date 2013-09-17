/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA
 */
 
// Modifiziert für iTelex-Projekt von cmd-ntp.c.
// Ersetzt NUR cgi_ntp()


#include <avr/pgmspace.h>
#include <avr/version.h>
#include <avr/eeprom.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <bool.h>
#include <math.h>

#include "config.h"

#if defined(NTP)

#include "system/clock/clock.h"
#include "system/net/ip.h"
#include "system/net/dns.h"
#include "system/net/ntp.h"
#include "system/stdout/stdout.h"
#include "system/config/eeconfig.h"

#include "system/shell/shell.h"
#include "apps/httpd/cgibin/cgi-bin.h"
#include "apps/httpd/httpd2.h"
#include "apps/httpd/httpd2_pharse.h"

#include "CgiFormTools.h"
#include "StringTab.h"

#include "ConfigNtp.h"
#include "iTelex.h"


const char NtpOn_P[] PROGMEM = "NTP";
const char NtpServerStr_P[] PROGMEM = "NTPSERVER";
const char UtcZoneStr_P[] PROGMEM = "UTCZONE";
const char AutoDst_P[] PROGMEM = "AUTODST";


/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface für zum Einstellen und Anzeigen für den NTP-Service.
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/
void ConfigNtpCgi( void * pStruct )
	{
	static TSprache Sprache;

	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	
	if (!PruefeSpracheUndKonfigFreigabe(pStruct))
		return;
	
	char NtpServerStr[32];
	char UtcZoneStr[4];
	bool NtpOn;
	bool AutoDstOn;
	
	readConfig_P( NtpOn_P, NtpServerStr ); // Missbrauch, aber ok...
	NtpOn = strcmp_P(NtpServerStr, PSTR("on")) == 0;
	
	readConfig_P(AutoDst_P, NtpServerStr); // Missbrauch
	AutoDstOn = strcmp(NtpServerStr, PSTR("on")) == 0;
	
	if( checkConfigName_P( NtpServerStr_P ) != -1 )
		readConfig_P ( NtpServerStr_P, NtpServerStr );
	else
		NtpServerStr[0] = '\0';
		
	if( checkConfigName_P( UtcZoneStr_P ) != -1 )
		readConfig_P ( UtcZoneStr_P, UtcZoneStr );
	else
		strcpy_P ( UtcZoneStr, PSTR("0"));

	cgi_PrintHttpheaderStart();

	// Wenn keine Variablen übergeben wurden, dann Config ausgeben,
	// wenn Variablen übergeben wurden Config ändern
	if ( http_request->argc == 0 )
		{
		CgiFormStartTabbed_P(PSTR("ntp.cgi"));

		CgiFormCheckbox_P(ISTR(NtpOn, Sprache), NtpOn_P, NtpOn);
		CgiFormInputFieldText_P(ISTR(NtpServerHostname, Sprache), NtpServerStr_P, sizeof(NtpServerStr)-1, NtpServerStr);
		CgiFormInputFieldText_P(ISTR(Zeitzone, Sprache), UtcZoneStr_P, sizeof(UtcZoneStr)-1, UtcZoneStr);
			// Text, weil der Zahlenwert eh nicht gebraucht wird.
		CgiFormCheckbox_P(ISTR(AutoSommerzeit, Sprache), AutoDst_P, AutoDstOn);
		
		CgiFormFinish_P(ISTR(EinstellungenUebernehmen, Sprache));
		}
	else // argc > 0
		{
		printf_P(ISTR(NeueEinstellungen, Sprache));
		printf_P(PSTR("<a href=\"ntp.cgi\">"));
		printf_P(ISTR(Weiter, Sprache));
		printf_P(PSTR("</a>"));

		NtpOn = CgiCheckBool_P(http_request, ISTR(NtpOn, Sprache), NtpOn_P, NtpOn, Sprache);
		CgiCheckText_P(http_request, ISTR(NtpServerHostname, Sprache), NtpServerStr_P, sizeof(NtpServerStr), NtpServerStr, Sprache);
		CgiCheckText_P(http_request, ISTR(Zeitzone, Sprache), UtcZoneStr_P, sizeof(UtcZoneStr), UtcZoneStr, Sprache);
		AutoDstOn = CgiCheckBool_P(http_request, ISTR(AutoSommerzeit, Sprache), AutoDst_P, AutoDstOn, Sprache);
		
		SpeichereSpracheAlsLokal(Sprache);

		struct TIME Time;
		// Zeit holen
		CLOCK_GetTime(&Time);
		Time.timezone = atoi(UtcZoneStr);
		Time.use_summertime = AutoDstOn;
		CLOCK_SetTime(&Time);
		
		if (NtpOn)
			NTP_GetTime(0, NtpServerStr, Time.timezone);
		
		}

	cgi_PrintHttpheaderEnd();
	}

	
void UpdateTimezone()
	{
	char Buf[40];
	struct TIME Time;
		
	CLOCK_GetTime(&Time);
	if (readConfig_P(UtcZoneStr_P, Buf) == 1)
		Time.timezone = atoi(Buf);
	if (readConfig_P(AutoDst_P, Buf) == 1)
		Time.use_summertime = atoi(Buf) != 0;
	CLOCK_SetTime(&Time);
	}
	
	
#endif //defined(NTP)

