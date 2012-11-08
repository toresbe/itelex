/*! \file eMail.c \brief Anwendung zur Einbettung in das TxP2-System */
//***************************************************************************
//*            eMail.c
//*
//****************************************************************************/
///	\ingroup software
///	\defgroup Txp Funktionen für eMail-Empfang und Sendung
///	\code #include "eMail.h" \endcode
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

#include "config.h"

#ifdef TXP_EMAIL

#include <avr/pgmspace.h>
#include <avr/version.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <bool.h>

#include "system/config/eeconfig.h"

#include "system/net/ip.h"
#include "system/net/tcp.h"
#include "system/net/ethernet.h"
#include "system/net/dns.h"
#include "system/base64/base64.h"

#include "apps/httpd/cgibin/cgi-bin.h"
#include "apps/httpd/httpd2_pharse.h"

#include "CgiFormTools.h"
#include "Protokoll.h"
#include "FifoPuffer.h"
#include "BaudotCode.h"

#include "eMail.h"


static char EmailPOPServerAdresse[TlnAdresseMax];
	//!< URL des Email-Servers für Abruf.

static char EmailSMTPServerAdresse[TlnAdresseMax];
	//!< URL des Email-Servers für Abruf.

static char EmailEigeneAdresse[TlnAdresseMax];
	//!< eigene Email-Adresse.

static char EmailEigenesPasswort[TlnAdresseMax];
	//!< eigenes Passwort des Email-Servers.
	
static uint8_t EmailAbfrageTakt;
	//!< Abstand der eMail-Abfragen in Minuten.
	
	
static char EmailEmpfaenger[TlnAdresseMax];
	//!< Zwischenspeicher für Empfänger.
	

enum {
	HalloSagen = 1,
	AnmeldungStarten = 2,
	AnmeldungName = 3,
	AnmeldungKennwort = 4,
	MailFrom = 5,
	MailTo = 6,
	StartData = 7,
	WarteStart = 8,
	MailData = 9,
	Abmelden = 10,
	WarteEnde = 11
	} ; // Konstanten für ProtokollPhase

	
	
//! Bearbeitet die Socket-Daten für die Kommunikation mit einem POP3-Server.
// =========================================================================
//! 
void Pop3DatenVerarbeiten()
	{
	}
	
			
//! Offnet den Socket-Daten für die Kommunikation mit einem SMTP-Server.
// =========================================================================
//! 
/* Typischer Ablauf einer SMTP-Sitzung:
220 winmail-qwmail.de (IMail 8.15 78648-1) NT-ESMTP Server X1
ehlo
250-teleprinter.net says hello
250-SIZE 0
250-8BITMIME
250-DSN
250-ETRN
250-AUTH LOGIN CRAM-MD5
250-AUTH=LOGIN
250 EXPN
auth login
334 VXNlcm5hbWU6
znjlzeb0zwxlchjpbnrlci5uzxq=
334 UGFzc3dvcmQ6
C29UBMLICW==
235 authenticated
mail from:<fred@teleprinter.net>
250 ok
rcpt to:<fred.sonnenrein@gmx.de>
250 ok its for <fred.sonnenrein@gmx.de>
data
354 ok, send it; end with <CRLF>.<CRLF>
From:<fred@teleprinter.net>
To:<fred.sonnenrein@gmx.de>
Subject: Blubb

Test 123
zweite Zeile
letzte Zeile
.
250 Message queued
quit
221 Goodbye
*/

void SMTPDatenVerarbeiten()
	{
	if (SocketOutBufUsed != 0)
		return;
		
	if (ProtokollPhase != MailData)
		{ // in MailData wird jedes Zeichen sofort gesendet.
		if (SocketInBufUsed == 0)
			return;
			
		if (SocketInBuf[SocketInBufUsed-1] != '\n')
			return; 
			
		if (ProtokollPhase == AnmeldungStarten)
			{
			// warte auf ende aller Meldungen des SMTP-Servers.
			if (TimerVal(&TxpSocketAbbruchTimer) < 20)
				// der Timer wird bei jedem Datenpaket-Empfang auf 0 gesetzt
				return;
			}
		
		// Abbruch bei Fehlern
		if (SocketInBuf[0] >= '4')
			{
			if (ProtokollLevel >= 1)
				{
				Protokollieren_P(PSTR("TxP SMTP: Fehlermeldung: "));
				SocketInBuf[SocketInBufUsed] = '\0';
				Protokollieren(SocketInBuf);
				}

			InterneVerbindungBeenden(true);
			TxpSocketAbbauGeplant = true;
			return;
			}
			
		SocketInBufUsed = 0;
		} // if ProtokollPhase != MailData
		
	switch (ProtokollPhase)
		{
		case HalloSagen:
			strcpy_P(SocketOutBuf, PSTR("EHLO TelexPhone mail client\r\n"));
			ProtokollPhase = AnmeldungStarten;
			break;
			
		case AnmeldungStarten:
			strcpy_P(SocketOutBuf, PSTR("AUTH LOGIN\r\n"));
			ProtokollPhase = AnmeldungName;
			break;
			
		case AnmeldungName:
			base64_encode(SocketOutBuf, SocketOutBufMax - 10, EmailEigeneAdresse, strlen(EmailEigeneAdresse)); 			
			strcat_P(SocketOutBuf, PSTR("\r\n"));
			ProtokollPhase = AnmeldungKennwort;
			break;
			
		case AnmeldungKennwort:
			base64_encode(SocketOutBuf, SocketOutBufMax - 10, EmailEigenesPasswort, strlen(EmailEigenesPasswort)); 			
			strcat_P(SocketOutBuf, PSTR("\r\n"));
			ProtokollPhase = MailFrom;
			break;
			
		case MailFrom:
			strcpy_P(SocketOutBuf, PSTR("MAIL FROM:<"));
			strcat(SocketOutBuf, EmailEigeneAdresse);
			strcat_P(SocketOutBuf, PSTR(">\r\n"));
			ProtokollPhase = MailTo;
			break;
			
		case MailTo:
			strcpy_P(SocketOutBuf, PSTR("RCPT TO:<"));
			strcat(SocketOutBuf, EmailEmpfaenger);
			strcat_P(SocketOutBuf, PSTR(">\r\n"));
			ProtokollPhase = StartData;
			break;
			
		case StartData:
			strcpy_P(SocketOutBuf, PSTR("DATA\r\n"));
			ProtokollPhase = WarteStart;
			break;
			
		case WarteStart:
			strcpy_P(SocketOutBuf, PSTR("From:<"));
			strcat(SocketOutBuf, EmailEigeneAdresse);
			strcat_P(SocketOutBuf, PSTR(">\r\nTo:"));
			strcat(SocketOutBuf, EmailEmpfaenger);
			strcat_P(SocketOutBuf, PSTR(">\r\nSubject: *TXP* Mail sent by TelexPhone\r\n\r\n")); 
			//! \todo Erste Zeile als Subject...
			ProtokollPhase = MailData;
			break;
			
		case MailData:
			while (!PufferLeer(&EmpfPuffer) && SocketOutBufUsed < SocketOutBufMax - 3 - 10) // - 10 = Reserve für wichtige Daten
				{
				SocketOutBuf[SocketOutBufUsed] = CodeZuZeichen(PufferAusg(&EmpfPuffer), (char*) &EmpfPuffer.BuZiMode);
				if (SocketOutBuf[SocketOutBufUsed] != '\0')
					SocketOutBufUsed++;
				}
			SocketOutBuf[SocketOutBufUsed] = '\0';
			//! \todo Zeile mit einzelnem Punkt abfangen
			break;
			
		case Abmelden:
			strcpy_P(SocketOutBuf, PSTR("QUIT\r\n"));
			ProtokollPhase = WarteEnde;
			break;
			
		case WarteEnde:
			TxpSocketAbbauGeplant = true;
			break;

		}
			
	SocketOutBufUsed = strlen(SocketOutBuf);
			
	}
	

//! Bearbeitet die Socket-Daten für die Kommunikation mit einem SMTP-Server.
// =========================================================================
//! 
bool SMTPOeffnen(char *EmfaengerName)
	{
	long ServerIP;
	
	strncpy(EmailEmpfaenger, EmfaengerName, sizeof(EmailEmpfaenger)-1);
	EmailEmpfaenger[sizeof(EmailEmpfaenger)-1] = '\0';
	
	ServerIP = DNS_ResolveName(EmailSMTPServerAdresse); 
	if (ServerIP == -1)
		{
		Protokollieren_P(PSTR("TxP: IP zu Url "));
		Protokollieren(EmailSMTPServerAdresse);
		Protokollieren_P(PSTR(" nicht gefunden\r\n"));
		return false;
		}
	
	// und hier wird geöffet...
	TxpSocketHandle = Connect2IP(ServerIP, 25); 
	 
	if (TxpSocketHandle == -1)
		{ // ID#223 ********************************************
		// Verbindung konnte nicht aufgebaut werden
		Protokollieren_P(PSTR("TxP: Socket zum SMTP-Server konnte nicht geoeffnet werden\r\n"));
		TxpSocketHandle = NO_SOCKET_USED;
		TxpSocketMode = SocketIdle;
		return false;
		}

	SocketBufInit();
	
	TxpSocketMode = SocketOriginate;
	TxpSocketAbbauGeplant = false;
	
	ProtokollPhase = HalloSagen;
	
	StartTimer(&TxpSocketAbbruchTimer);
	
	return true;
	}
	

//! Startet den Abbau der Verbindung mit einem SMTP-Server.
// ========================================================
//! Muss in jedem Zustand funktionieren können.
void SMTPSchliessen()
	{
	if (ProtokollPhase == MailData)
		{
		strcpy_P(SocketOutBuf, PSTR("\r\n.\r\n"));
		ProtokollPhase = Abmelden;
		}
	else
		{
		strcpy_P(SocketOutBuf, PSTR("QUIT\r\n"));
		ProtokollPhase = WarteEnde;
		SocketOutBufUsed = strlen(SocketOutBuf);
		}
	}
	
	
// Parameternamen

const PROGMEM char EmailPOPServerAdresse_P[] = "POPSERVER";
const PROGMEM char EmailSMTPServerAdresse_P[] = "SMTPSERVER";
const PROGMEM char EmailEigeneAdresse_P[] = "EMAILADR";
const PROGMEM char EmailEigenesPasswort_P[] = "EMAILPASS";
const PROGMEM char EmailAbfrageTakt_P[] = "EMAILABFRTAKT";

/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface zum Ändern der Einstellungen des TelexPhone-Interface bezüglich der Anbindung 
 * an einen eMail-Server
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/
 
void txp_cgi_email_config(void *pStruct)
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	char Buf[TlnAdresseMax+5];

	if (!KonfigFreigabe(pStruct))
		return;
	
	cgi_PrintHttpheaderStart();

	if ( http_request->argc == 0 )
		{
		CgiFormStartTabbed_P(PSTR("txpcfg-email.cgi"));

		CgiFormInputFieldText_P(PSTR("POP-Server Adresse:"), EmailPOPServerAdresse_P, TlnAdresseMax, EmailPOPServerAdresse);
		CgiFormInputFieldText_P(PSTR("SMTP-Server Adresse:"), EmailSMTPServerAdresse_P, TlnAdresseMax, EmailSMTPServerAdresse);
		CgiFormInputFieldText_P(PSTR("Eigene eMail-Adresse:"), EmailEigeneAdresse_P, TlnAdresseMax, EmailEigeneAdresse);
		CgiFormInputFieldText_P(PSTR("Kennwort für eMail-Server:"), EmailEigenesPasswort_P, TlnAdresseMax, EmailEigenesPasswort);
		CgiFormInputFieldLong_P(PSTR("Takt des eMail-Abrufs (Minuten):"), EmailAbfrageTakt_P, 2, EmailAbfrageTakt);
		
		CgiFormFinish_P(PSTR("Einstellung &Uuml;bernehmen"));
		}
	else // argc > 0
		{
		uint8_t Neu;

		printf_P(PSTR("neue Einstellungen: <a href=\"txpcfg-email.cgi\">weiter</a>"));

		// EmailPOPServerAdresse
		// ---------------------
		if (PharseCheckName_P(http_request, EmailPOPServerAdresse_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, EmailPOPServerAdresse_P)], TlnAdresseMax);
			Buf[TlnAdresseMax-1] = '\0';
			if (strcmp(Buf, EmailPOPServerAdresse) == 0)
				printf_P(PSTR("<br>POP-Server unver&auml;ndert: %s"), Buf);
			else
				{
				printf_P(PSTR("<br>POP-Server ge&auml;ndert in: %s"), Buf);
				changeConfig_P(EmailPOPServerAdresse_P, Buf);
				strcpy(EmailPOPServerAdresse, Buf);
				}
			} // if PharseCheckName_P()
		
		// EmailSMTPServerAdresse
		// ----------------------
		if (PharseCheckName_P(http_request, EmailSMTPServerAdresse_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, EmailSMTPServerAdresse_P)], TlnAdresseMax);
			Buf[TlnAdresseMax-1] = '\0';
			if (strcmp(Buf, EmailSMTPServerAdresse) == 0)
				printf_P(PSTR("<br>SMTP-Server unver&auml;ndert: %s"), Buf);
			else
				{
				printf_P(PSTR("<br>SMTP-Server ge&auml;ndert in: %s"), Buf);
				changeConfig_P(EmailSMTPServerAdresse_P, Buf);
				strcpy(EmailSMTPServerAdresse, Buf);
				}
			} // if PharseCheckName_P()
		
		// EmailEigeneAdresse
		// ----------------------
		if (PharseCheckName_P(http_request, EmailEigeneAdresse_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, EmailEigeneAdresse_P)], TlnAdresseMax);
			Buf[TlnAdresseMax-1] = '\0';
			if (strcmp(Buf, EmailEigeneAdresse) == 0)
				printf_P(PSTR("<br>eigene Adresse unver&auml;ndert: %s"), Buf);
			else
				{
				printf_P(PSTR("<br>eigene Adresse ge&auml;ndert in: %s"), Buf);
				changeConfig_P(EmailEigeneAdresse_P, Buf);
				strcpy(EmailEigeneAdresse, Buf);
				}
			} // if PharseCheckName_P()
		
		// EmailEigenesPasswort
		// ----------------------
		if (PharseCheckName_P(http_request, EmailEigenesPasswort_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, EmailEigenesPasswort_P)], TlnAdresseMax);
			Buf[TlnAdresseMax-1] = '\0';
			if (strcmp(Buf, EmailEigenesPasswort) == 0)
				printf_P(PSTR("<br>eigenes Kennwort unver&auml;ndert: %s"), Buf);
			else
				{
				printf_P(PSTR("<br>eigenes Kennwort ge&auml;ndert in: %s"), Buf);
				changeConfig_P(EmailEigenesPasswort_P, Buf);
				strcpy(EmailEigenesPasswort, Buf);
				}
			} // if PharseCheckName_P()
		
		// Abfragetakt
		// ---------------------
		if (PharseCheckName_P(http_request, EmailAbfrageTakt_P))
			{
			strncpy(Buf, http_request->argvalue[PharseGetValue_P(http_request, EmailAbfrageTakt_P)], 10);
			Buf[10] = '\0';
			Neu = atol(Buf);
			if (Neu == EmailAbfrageTakt)
				printf_P(PSTR("<br>Abfragetakt unver&auml;ndert: %s"), Buf);
			else
				{
				if (Neu < 5)
					{
					Neu = 5;
					Buf[0] = '5'; Buf[1] = '\0';
					}
				printf_P(PSTR("<br>Abfragetakt ge&auml;ndert in: %s"), Buf);
				changeConfig_P(EmailAbfrageTakt_P, Buf);
				EmailAbfrageTakt = Neu;
				}
			}
		
		} // else argc > 0
		
	cgi_PrintHttpheaderEnd();

	} // txp_cgi_email_config()
	

void txp_email_init()
	{
	// EEPROM auslesen
	char Buf[TlnAdresseMax];

	if (readConfig_P(EmailPOPServerAdresse_P, EmailPOPServerAdresse) != 1)
		EmailPOPServerAdresse[0] = '\0';

	if (readConfig_P(EmailSMTPServerAdresse_P, EmailSMTPServerAdresse) != 1)
		EmailSMTPServerAdresse[0] = '\0';

	if (readConfig_P(EmailEigeneAdresse_P, EmailEigeneAdresse) != 1)
		EmailEigeneAdresse[0] = '\0';

	if (readConfig_P(EmailEigenesPasswort_P, EmailEigenesPasswort) != 1)
		EmailEigenesPasswort[0] = '\0';

	if (readConfig_P(EmailAbfrageTakt_P, Buf) == 1)
		EmailAbfrageTakt = atoi(Buf);
	else
		EmailAbfrageTakt = 15; // Minuten

	// cgi Registrieren
	
	cgi_RegisterCGI(txp_cgi_email_config, PSTR("txpcfg-email.cgi"));
	}
	
	
#endif //def TXP_EMAIL
