/*! \file eMail.c \brief Anwendung zur Einbettung in das iTelex-System */
//***************************************************************************
//*            eMail.c
//*
//****************************************************************************/
///	\ingroup software
///	\defgroup iTelex Funktionen für eMail-Empfang und Sendung
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

#ifdef ITELEX_EMAIL

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
#include "system/string/string.h"

#include "apps/httpd/cgibin/cgi-bin.h"
#include "apps/httpd/httpd2_pharse.h"

#include "CgiFormTools.h"
#include "Protokoll.h"
#include "FifoPuffer.h"
#include "BaudotCode.h"

#include "eMail.h"

#include "StringTab.h" 


static char EmailPOPServerAdresse[TlnAdresseMax];
	//!< URL des Email-Servers für Abruf.

static char EmailSMTPServerAdresse[TlnAdresseMax];
	//!< URL des Email-Servers für Abruf.

static char EmailEigeneAdresse[TlnAdresseMax];
	//!< eigene Email-Adresse.

static char EmailEigenesPasswort[TlnAdresseMax];
	//!< eigenes Passwort des Email-Servers.
	
static uint8_t EmailAbfrageTakt;
	//!< Abstand der eMail-Abfragen in Minuten. 0 = Ausgeschaltet.

static bool EmailAusgabeFilternKennung;
	//!< Nur die emails ausgeben, die eine +TX+ Kennung in der Subject-Zeile haben.
	
	
static char EmailEmpfaenger[TlnAdresseMax];
	//!< Zwischenspeicher für Empfänger.
	
	
static TKurzTimer VervollstaendigungTimer;
	//!< Misst, ob der POP bzw SMTP-Server fertig ist mit Meldungen ausgeben.
	
	
static TLangTimer POPWartezeitTimer;
	//!< Zeitmesser für die Email-Abfrage-Takte
	

static uint16_t POPWartezeitEnde;
	//!< Ablaufzeit für die Email-Abfrage-Takte. 3 Minuten nach jeder anderen Kommunikation,
	//!< #EmailAbfrageTakt Minuten nach jeder erfolglosen Email-Abfrage.


static bool POPOkEmpfangen;
	//!< Wird auf true gesetzt, wenn eine Zeile mit + am Anfang empfangen wurde.
	//!< Nach Kommandoausgaben auf false.

static uint8_t POPOeffnenFehlerZaehler;
	//!< Wird inkrement, wenn das Öffnen des eMail-Abfrage Servers versagte. 
	//!< Beim dritten Mal gibt es eine Fehlermeldung, aber beim 4. Mal nicht wieder (also auch eine "Wiederholungssperre")
	//!< Wird wieder auf 0 gesetzt, wenn die Adresse geändert wird oder der POP-Server erfolgreich 
	//!< Verbunden wurde.
	
	
static bool InMailHeader;
	//!< so lange true, so lange Zeilen des Mail-Headers an MailZeileVerarbeiten()
	//!< übergeben werden.
	
static bool MailUnterdruecken; 
	//!< Wird auf true gesetzt, wenn bestimmte Kriterien erfüllt sind:
	//!< Subject ohne Kennung, sofern Filter eingeschaltet
	//!< HTML- oder Multipart-Bodies.
	
	
enum {
	HalloSagen = 1,
	AnmeldungStarten = 2,
	AnmeldungName = 3,
	AnmeldungKennwort = 4,
	EingabeMailTo = 5,
	MailFrom = 6,
	MailTo = 7,
	StartData = 8,
	WarteStart = 9,
	MailSubject = 10,
	MailData = 11,
	Abmelden = 12,
	WarteEnde = 13
	} ; // Konstanten für ProtokollPhase

	
static bool Zeilenanfang;
	//!< Speichert in manchen Phasen, ob in der aktuellen Zeile bereits etwas eingegeben wurde.
	
	
//! Bearbeitet das Drucken von empfangenen Mails.
// ------------------------------------------------
//! Filtert aus dem Header die interessanten Zeilen heraus und Druckt nur diese.
//! Prüft auch auf korrekten Inhalt. Ggf Zeilenumbrüche selber einfügen.
//! \return false, wenn Pufferüberlauf.

bool MailZeileVerarbeiten(char *Zeile)
	{
	char *p; 
	bool DieseZeileDrucken = true;
	
	if (InMailHeader)
		{
		p = strstr_P(Zeile, PSTR(":"));
		
		if (Zeile[0] == '\r' && Zeile[1] == '\n')
			// Leerzeile leitet Mail-Body ein
			InMailHeader = false;
			
		else if (p == NULL) // Header-Zeile ohne Doppelpunkt: ignorieren
			DieseZeileDrucken = false;
				
		else if (strncasecmp_P(Zeile, PSTR("from"), p - Zeile) == 0
				|| strncasecmp_P(Zeile, PSTR("to"), p - Zeile) == 0
				|| strncasecmp_P(Zeile, PSTR("date"), p - Zeile) == 0)
			; // Zeile Drucken, siehe unten
		else if (strncasecmp_P(Zeile, PSTR("subject"), p - Zeile) == 0)
			{
			if (EmailAusgabeFilternKennung && strstr_P(p, PSTR("+TX+")) == NULL && strstr_P(p, PSTR("+tx+")) == NULL)
				// Nur Emails-mit Kennung im Subject drucken, aber keine Kennung enthalten...
				MailUnterdruecken = true;
				// aber kein return, so wird die Subject-Zeile noch gedruckt.
			}
		else if (strncasecmp_P(Zeile, PSTR("content-type"), p - Zeile) == 0)
			{
			//! \todo Prio 3: Zeichensatz
			
			if (strstr_P(p, PSTR("text/plain")) == NULL)
				// Kein pures Ascii -> weg.
				MailUnterdruecken = true;
				// aber kein return, so wird die Content-Zeile noch gedruckt.
			else
				DieseZeileDrucken = false; // die content-type Zeile nicht drucken.
			}
		else
			// uninteressante Header-Zeile -> ignorieren
			DieseZeileDrucken = false;
		} // if InMailHeader

	// jetzt Zeile drucken, um Umbruch und co kümmern sich andere...
	if (DieseZeileDrucken)
		{
		if (strlen(AsciiDruckPuffer) + strlen(Zeile) >= AsciiDruckPufferMax)
			return false; // AsciiDruckPuffer würde überlaufen, also abbrechen.
		
		// quoted-printable umwandeln:
		p = Zeile;
		while (*p != '\0')
			{
			if (*p == '=')
				{ // eigentlich folgt jetzt ein Hex-Wert
				if (p[1] > '0' && p[2] > '0')
					{ // es ist wahrscheinlich ein Hex-Wert
					*p = (atoh(p[1]) << 4) + atoh(p[2]);
					p++;
					strcpy(p, p + 2);
					}
				else if (p[1] < ' ' || p[2] < ' ')
					{ // es ist wahrscheinlich Zeilenende
					*p = '\0';
					break;
					}
				else // einfach so lassen
					p++;
				}
			else // es war kein =
				p++;
			}

		// und ab in den Puffer
		strcat(AsciiDruckPuffer, Zeile);
		}

	// Erst wenn kein Überlauf droht Protokoll drucken.
	if (ProtokollLevel >= DatenKurz)
		{
		Protokollieren_P(PSTR("iTelex POP: ZeileVerarbeiten: "));
		ProtokollierenPuffer(Zeile, strlen(Zeile));
		if (DieseZeileDrucken)
			ProtokollierenInt_P(PSTR(" ...druck (Ges. %u)\r\n"), strlen(AsciiDruckPuffer));
		else
			Protokollieren_P(PSTR(" ...ignorieren\r\n"));
		}

	return true;
	} // MailZeileVerarbeiten()
		
	
//! Startet in gewissen Zeiträumen die Abfrage des POP-Servers.
// =========================================================================
//! 
void POP3Einleiten()
	{
	if (LangTimerVal(&POPWartezeitTimer) < POPWartezeitEnde)
		return;
		
	if (EmailAbfrageTakt == 0)
		return;
		
	if (Modus != ModRuhe || iTelexSocketHandle != NO_SOCKET_USED)
		{
		StartLangTimer(&POPWartezeitTimer);
		POPWartezeitEnde = 3 * LangTimerMinuteFaktor;
		return;
		}

	StartLangTimer(&POPWartezeitTimer);
	POPWartezeitEnde = 3 * LangTimerMinuteFaktor;
		// hier schon, da nach Öffnen immer ein Zeitfenster gestartet wird.
		
	// jetzt geht's los...
	long ServerIP;

	ServerIP = strtoip(EmailPOPServerAdresse);	// Annahme: eine IP-Adresse angegeben
	
	if (ServerIP == 0) // ist es doch eine Hostname?
		ServerIP = DNS_ResolveName(EmailPOPServerAdresse); 

	if (ServerIP == -1)
		{
		Protokollieren_P(PSTR("iTelex POP: ! IP zu Hostname "));
		Protokollieren(EmailPOPServerAdresse);
		Protokollieren_P(PSTR(" nicht gefunden\r\n"));

		if (POPOeffnenFehlerZaehler < 255)
			POPOeffnenFehlerZaehler++;
			
		if (POPOeffnenFehlerZaehler == 3)
			{
			if (Diagnoseausgabe_P(ISTR(POPFehlerAnfang, LokaleSprache), 1))
				{
				strncat(DiagnosePuffer, EmailPOPServerAdresse, strlen(DiagnosePuffer) - 30);
				strcat_P(DiagnosePuffer, ISTR(MailFehlerIPNichtErmittelbar, LokaleSprache));
				}
			}
		
		POPWartezeitEnde = EmailAbfrageTakt * LangTimerMinuteFaktor;
		
		return;
		}
	
	// und hier wird geöffet...
	iTelexSocketHandle = Connect2IP(ServerIP, 110); 
	 
	if (iTelexSocketHandle == -1)
		{ // ID#223 ********************************************
		// Verbindung konnte nicht aufgebaut werden
		Protokollieren_P(PSTR("iTelex POP: ! Client-Socket zum Server konnte nicht geoeffnet werden\r\n"));
		iTelexSocketHandle = NO_SOCKET_USED;
		iTelexSocketMode = SocketIdle;

		if (POPOeffnenFehlerZaehler < 255)
			POPOeffnenFehlerZaehler++;
			
		if (POPOeffnenFehlerZaehler == 3)
			{
			if (Diagnoseausgabe_P(ISTR(POPFehlerAnfang, LokaleSprache), 1))
				{
				strncat(DiagnosePuffer, EmailPOPServerAdresse, strlen(DiagnosePuffer) - 30);
				strcat_P(DiagnosePuffer, ISTR(MailFehlerNotConnected, LokaleSprache));
				}
			}
		
		POPWartezeitEnde = EmailAbfrageTakt * LangTimerMinuteFaktor;
		
		return;
		}

	if (ProtokollLevel >= AblaufInfo)
		ProtokollierenInt_P(PSTR("iTelex POP: Client-Socket #%d zum Server erfolgreich geoeffnet\r\n"), iTelexSocketHandle);
		
	POPOeffnenFehlerZaehler = 0;
	
	SocketBufInit();
	
	iTelexSocketMode = SocketOriginate;
	iTelexSocketAbbauGeplant = false;
	iTelexSocketProtokoll = POP3;
	
	ProtokollPhase = AnmeldungName;
	POPOkEmpfangen = false;
	
	StartKurzTimer(&VervollstaendigungTimer);
	
	ModusWechsel(ModEmailPOPVerbunden);
	
	} // POP3Einleiten
	

//! Bearbeitet die Socket-Daten für die Kommunikation mit einem POP3-Server.
// =========================================================================
//! 
/* Typischer Ablauf einer POP3-Sitzung:
+OK example.com POP3-Server
USER wiki@example.com 	
+OK Please enter password
PASS passwort_im_klartext 	
+OK mailbox locked and ready
STAT 	
+OK 1 236
LIST 	
+OK mailbox has 1 messages (236 octets)
1 236
.
RETR 1 	
+OK message follows
Date: Mon, 18 Oct 2004 04:11:45 +0200
From: Someone <someone@example.com>
To: wiki@example.com
Subject: Test-E-Mail
Content-Type: text/plain; charset=us-ascii; format=flowed
Content-Transfer-Encoding: 7bit

Dies ist eine Test-E-Mail

.
DELE 1 	
+OK message marked for delete
QUIT 	
+OK bye
*/


#define MAXLINELEN 63


void POP3DatenVerarbeiten()
	{
	uint16_t i;
	uint16_t ZeileAnfang;
	char *p;
	bool UeberlaufDroht = false; 
		// falls der Puffer droht überzulaufen, muss durch Weglassen von
		// Text Platz gemacht werden.
	
	if (SocketOutBufUsed != 0)
		return;
		
	if (SocketInBufUsed == 0)
		return;
		
	SocketInBuf[SocketInBufUsed] = '\0';
	
	if (strchr(SocketInBuf, 0x0a) == NULL && SocketInBufUsed < SocketInBufMax / 2)
		return; // da müssen noch Daten kommen.
		
	if (SocketInBuf[0] == '+')
		POPOkEmpfangen = true;
		
	if (!POPOkEmpfangen)
		{
		if (ProtokollLevel >= NurFehler)
			{
			Protokollieren_P(PSTR("iTelex POP: ! Fehlermeldung: "));
			Protokollieren(SocketInBuf);
			}

		// InterneVerbindungBeenden(true); // war mal hier drin, wird aber vermutlich nicht gebraucht.
		if (ProtokollPhase < Abmelden)
			ProtokollPhase = Abmelden; // leitet Abbruch der Verbindung ein
		}

	SocketOutBuf[0] = '\0';
		// damit falls nichts in den Puffer geschrieben wird, SocketOutBufUsed auf 0 bleibt.
	
	switch (ProtokollPhase)
		{
		case HalloSagen:
		case AnmeldungStarten:
		case MailFrom:
		case MailTo:
			// gibt es nicht...
			iTelexSocketAbbauGeplant = true;
			return;
			
		case AnmeldungName:
			strcpy_P(SocketOutBuf, PSTR("USER "));
			strcat(SocketOutBuf, EmailEigeneAdresse);
			strcat_P(SocketOutBuf, PSTR("\r\n"));
			ProtokollPhase = AnmeldungKennwort;
			SocketInBufUsed = 0;
			break;
			
		case AnmeldungKennwort:
			strcpy_P(SocketOutBuf, PSTR("PASS "));
			strcat(SocketOutBuf, EmailEigenesPasswort);
			strcat_P(SocketOutBuf, PSTR("\r\n"));
			ProtokollPhase = StartData;
			SocketInBufUsed = 0;
			break;

		case StartData:
			strcpy_P(SocketOutBuf, PSTR("STAT\r\n"));
			ProtokollPhase = WarteStart;
			SocketInBufUsed = 0;
			break;
			
		case WarteStart:
			// Anzahl der Meldungen prüfen
			for (i = 0 ; i < SocketInBufUsed ; i++)
				if (SocketInBuf[i] == ' ')
					break;
					
			if (i < SocketInBufUsed && atoi(SocketInBuf + i) == 0)
				{ // nichts im Puffer ODER plötzlich doch belegt...
				POPWartezeitEnde = EmailAbfrageTakt * LangTimerMinuteFaktor;
				StartLangTimer(&POPWartezeitTimer);
				strcpy_P(SocketOutBuf, PSTR("QUIT\r\n"));
				iTelexSocketAbbauGeplant = true;
				ProtokollPhase = WarteEnde;
				}
			else
				{ // mindestens eine Meldung im Puffer...
				strcpy_P(AsciiDruckPuffer, ISTR(EmailEmpfangStartzeile, LokaleSprache)); 
					// startet sofort den Fernschreiber
				
				strcpy_P(SocketOutBuf, PSTR("RETR 1\r\n"));
				ProtokollPhase = MailData;
				InMailHeader = true; // für MailZeileVerarbeiten()
				MailUnterdruecken = false;
				}
				
			SocketInBufUsed = 0;
			break;
			
		case MailData:
			//HACK if (AsciiDruckPuffer[0] != '\0')
			//HACK	return; // es wird noch gedruckt, also nichts neues Drucken...
				
			// Empfang in einzelne Zeilen zerlegen und verarbeiten...
			ZeileAnfang = 0;
			while (ZeileAnfang < SocketInBufUsed)
				{
				if (strcmp_P(SocketInBuf + ZeileAnfang, PSTR(".\r\n")) == 0)
					// == 0 nur dann, wenn . CR LF auch am Ende des Empfangspuffers steht.
					{ // Kennung des Endes des Mail-Bodys
					strcpy_P(SocketOutBuf, PSTR("DELE 1\r\n"));
					ProtokollPhase = Abmelden;
					strcat_P(AsciiDruckPuffer, PSTR("\r\n\n\n\n"));
					ZeileAnfang = SocketInBufUsed; // SocketInBuf ist komplett bearbeitet.
					break;
					}
				
				// nächstes Zeilenende finden
				p = strstr_P(SocketInBuf + ZeileAnfang, PSTR("\r\n"));
				if (p == NULL)
					{ // es folgt kein CRLF mehr
					if (SocketInBufUsed < ZeileAnfang + MAXLINELEN)
						break; // auf vervollständigung der Zeile warten
					else
						{ // Zeile ist auch so lang genug zum Verarbeiten
						if (!MailUnterdruecken)
							{
							// Ende ist bereits mit \0 markiert.
							if (!MailZeileVerarbeiten(SocketInBuf + ZeileAnfang))
								{
								UeberlaufDroht = true;
								break; // Verarbeitung des Rests auf später verschieben.
								}
							}
						ZeileAnfang = SocketInBufUsed; // SocketInBuf ist komplett bearbeitet.
						break; // der while-Schleife
						}
					} // p == NULL -> kein CRLF in der Zeile
				else
					{ // nach ZeileAnfang wurde ein CRLF gefunden
					p += 2; // Auf das Zeichen HINTER dem CRLF gehen
					if (!MailUnterdruecken)
						{
						char h = *p; // speichert Zeilen nach ZeilenEnde
						*p = '\0';
						bool Erfolg = MailZeileVerarbeiten(SocketInBuf + ZeileAnfang);
						*p = h; // Zeichen der nächsten Zeile wiederherstellen
						if (!Erfolg)
							{
							UeberlaufDroht = true;
							break;
							}
						}
					ZeileAnfang = p - SocketInBuf;
					}
				} // while (ZeileAnfang < SocketInBufUsed)
			
			if (ZeileAnfang > 0 && ZeileAnfang < SocketInBufUsed)
				{ // Abbruch warum auch immer...
				memmove(SocketInBuf, SocketInBuf + ZeileAnfang, SocketInBufUsed - ZeileAnfang);
				SocketInBufUsed -= ZeileAnfang;
				}
				
			else if (ZeileAnfang >= SocketInBufUsed)
				SocketInBufUsed = 0;
				
			else if (UeberlaufDroht && SocketInBufUsed > SocketInBufMax - 20)
				{ // mindestens 20 Zeichen Platz lassen. Aber die letzen 60 Zeichen beibehalten.
				int AnzahlZuLoschen = SocketInBufUsed - (SocketInBufMax - 20);
				int LoeschPosition = SocketInBufMax - 60; // Konstante, macht der Compiler weg...
				int Rest = SocketInBufUsed - LoeschPosition - AnzahlZuLoschen;
				memmove(SocketInBuf + LoeschPosition, 
						SocketInBuf + LoeschPosition + AnzahlZuLoschen, 
					    Rest);
				SocketInBufUsed -= AnzahlZuLoschen;
				}
				
			break;

		case Abmelden:
			strcpy_P(SocketOutBuf, PSTR("QUIT\r\n"));
			iTelexSocketAbbauGeplant = true;
			ProtokollPhase = WarteEnde;
			SocketInBufUsed = 0;
			break;
			
		case WarteEnde:
			SocketInBufUsed = 0;
			break;

		}
			
	SocketOutBufUsed = strlen(SocketOutBuf);
	if (SocketOutBufUsed != 0)
		POPOkEmpfangen = false;
		
	StartKurzTimer(&VervollstaendigungTimer);
	
	}
	
			
//! Abbruch der Abfrage des POP-Servers.
// =========================================================================
//! z.B. durch Drücken der Schluss-taste während des Ausdrucks.
void POP3Abbrechen()
	{
	if (iTelexSocketMode != SocketOriginate || iTelexSocketProtokoll != POP3)
		return; // da gibt es nix abzubrechen...
	if (ProtokollPhase == Abmelden)
		return; // ist eh gleich vorbei...
	
	SocketInBufUsed = 0;
	AsciiDruckPuffer[0] = '\0'; // AsciiHilfpuffer zwar noch nicht leer, aber Endgerät ist eh aus.

	strcpy_P(SocketOutBuf, PSTR("DELE 1\r\n"));
	
	if (ProtokollLevel >= NurFehler)
		Protokollieren_P(PSTR("iTelex POP: ! Abbruch\r\n"));
	
	ProtokollPhase = Abmelden;
	}
	
			
//! Offnet den Socket-Daten für die Kommunikation mit einem SMTP-Server.
// =========================================================================
//! 
bool SMTPOeffnen(char *EmfaengerName)
	{
	long ServerIP;
	
	strncpy(EmailEmpfaenger, EmfaengerName, sizeof(EmailEmpfaenger)-1);
	EmailEmpfaenger[sizeof(EmailEmpfaenger)-1] = '\0';
	
	ServerIP = strtoip(EmailSMTPServerAdresse);	// Annahme: eine IP-Adresse angegeben
	
	if (ServerIP == 0) // ist es doch eine Hostname?
		ServerIP = DNS_ResolveName(EmailSMTPServerAdresse); 

	if (ServerIP == -1)
		{
		Protokollieren_P(PSTR("iTelex SMTP: ! IP zu Hostname "));
		Protokollieren(EmailSMTPServerAdresse);
		Protokollieren_P(PSTR(" nicht gefunden\r\n"));
		if (Diagnoseausgabe_P(ISTR(SMTPFehlerAnfang, LokaleSprache), 1))
			{
			strncat(DiagnosePuffer, EmailSMTPServerAdresse, strlen(DiagnosePuffer) - 30);
			strcat_P(DiagnosePuffer, ISTR(MailFehlerIPNichtErmittelbar, LokaleSprache));
			}
		return false;
		}
	
	// und hier wird geöffet...
	iTelexSocketHandle = Connect2IP(ServerIP, 25); 
	 
	if (iTelexSocketHandle == -1)
		{ // ID#223 ********************************************
		// Verbindung konnte nicht aufgebaut werden
		Protokollieren_P(PSTR("iTelex SMTP: ! Client-Socket zum SMTP-Server konnte nicht geoeffnet werden\r\n"));
		iTelexSocketHandle = NO_SOCKET_USED;
		iTelexSocketMode = SocketIdle;
		if (Diagnoseausgabe_P(ISTR(SMTPFehlerAnfang, LokaleSprache), 1))
			{
			strncat(DiagnosePuffer, EmailSMTPServerAdresse, strlen(DiagnosePuffer) - 30);
			strcat_P(DiagnosePuffer, ISTR(MailFehlerNotConnected, LokaleSprache));
			}
		return false;
		}

	SocketBufInit();
	
	iTelexSocketMode = SocketOriginate;
	iTelexSocketAbbauGeplant = false;
	iTelexSocketProtokoll = SMTP;
	
	ProtokollPhase = HalloSagen;
	
	StartKurzTimer(&VervollstaendigungTimer);
	
	return true;
	} // SMTPOeffnen()
	

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

//! Bearbeitet die Socket-Daten für die Kommunikation mit einem SMTP-Server.
// =========================================================================
//! 
void SMTPDatenVerarbeiten()
	{
	if (SocketOutBufUsed != 0)
		return;
		
	if (ProtokollPhase != MailData 
		&& ProtokollPhase != MailSubject 
		&& ProtokollPhase != EingabeMailTo)
		{ // in diesen Phasen wird jedes Zeichen sofort gesendet.
		if (SocketInBufUsed == 0)
			return;
			
		if (SocketInBuf[SocketInBufUsed-1] != 0x0a) // Linefeed.
			return; 
			
		if (ProtokollPhase == AnmeldungStarten)
			{
			//! \todo bei Änderungen von SocketInBufUsed Timer neu Starten
			// warte auf ende aller Meldungen des SMTP-Servers.
			if (KurzTimerVal(&VervollstaendigungTimer) < 2 * KurzTimerFreq)
				return;
			}
		
		// Abbruch bei Fehlern
		if (SocketInBuf[0] >= '4')
			{
			SocketInBuf[SocketInBufUsed] = '\0';
			
			if (ProtokollLevel >= NurFehler)
				{
				Protokollieren_P(PSTR("iTelex SMTP: ! Fehlermeldung: "));
				Protokollieren(SocketInBuf);
				}

			if (Diagnoseausgabe_P(ISTR(SMTPFehlerDirekt, LokaleSprache), 2))
				strncat(DiagnosePuffer, SocketInBuf, strlen(DiagnosePuffer) - 20);
			
			InterneVerbindungBeenden(true);
			iTelexSocketAbbauGeplant = true;
			SocketInBufUsed = 0; 
			return;
			}
			
		SocketInBufUsed = 0;
		} // if (ProtokollPhase != MailData	&& != MailSubject && != EingabeMailTo)
		
	SocketOutBuf[0] = '\0';
		// damit falls nichts in den Puffer geschrieben wird, SocketOutBufUsed auf 0 bleibt.
	
	switch (ProtokollPhase)
		{
		case HalloSagen:
			strcpy_P(SocketOutBuf, PSTR("EHLO iTelex mail client\r\n"));
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
			if (EmailEmpfaenger[0] == '?')
				{
				strcpy_P(AsciiDruckPuffer, ISTR(EmailEingabeEmpfaenger, LokaleSprache));
				PufferInit(&EmpfPuffer);
				ProtokollPhase = EingabeMailTo;
				EmailEmpfaenger[0] = '\0';
				}
			else
				ProtokollPhase = MailFrom;
			break;
			
		case EingabeMailTo:
			while (!PufferLeer(&EmpfPuffer) && strlen(EmailEmpfaenger) < sizeof(EmailEmpfaenger) - 1)
				{
				char z = CodeZuZeichen(PufferAusg(&EmpfPuffer), (char*) &EmpfPuffer.BuZiMode);
				if (z == '\r' || z == '\n')
					{
					if (strlen(EmailEmpfaenger) == 0)
						; // WR / ZL am Zeilenanfang ignorieren
					else
						{
						ProtokollPhase = MailFrom;
						break;
						}
					}
				else if (z != '\0' && z != '#')
					{
					if (z == '/')
						z = '@';
					EmailEmpfaenger[strlen(EmailEmpfaenger) + 1] = '\0';
					EmailEmpfaenger[strlen(EmailEmpfaenger)] = z;
					}
				}
				
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
			strcat_P(SocketOutBuf, PSTR(">\r\nTo:<"));
			strcat(SocketOutBuf, EmailEmpfaenger);
			strcat_P(SocketOutBuf, PSTR(">\r\nContent-Type: text/plain; charset=us-ascii\r\nSubject: ")); 
			
			// Aufforderung für Subject-Eingabe:
			strcpy_P(AsciiDruckPuffer, ISTR(EmailEingabeBetreff, LokaleSprache));
			ProtokollPhase = MailSubject;
			Zeilenanfang = true;
			break;
			
		case MailSubject:
			while (!PufferLeer(&EmpfPuffer) && SocketOutBufUsed < SocketOutBufMax - 3 - 10) // - 10 = Reserve für wichtige Daten
				{
				char z = CodeZuZeichen(PufferAusg(&EmpfPuffer), (char*) &EmpfPuffer.BuZiMode);
				if (z == '\r' || z == '\n')
					{
					if (Zeilenanfang)
						; // WR / ZL am Zeilenanfang ignorieren
					else
						{
						strcpy_P(SocketOutBuf + SocketOutBufUsed, PSTR(" +TX+\r\n\r\n"));
							// Ende der Subject-Zeile + Einleitung des Body
						SocketOutBufUsed = strlen(SocketOutBuf);
						
						ProtokollPhase = MailData;
						
						// Aufforderung für Body-Eingabe:
						strcpy_P(AsciiDruckPuffer, ISTR(EmailEingabeText, LokaleSprache));
						PufferInit(&EmpfPuffer);
						break;
						}
					}
				else if (z != '\0' && z != '#')
					{
					SocketOutBuf[SocketOutBufUsed++] = z;
					Zeilenanfang = false;
					}
				}
				
			SocketOutBuf[SocketOutBufUsed] = '\0';
			break;
			
		case MailData:
			while (!PufferLeer(&EmpfPuffer) && SocketOutBufUsed < SocketOutBufMax - 3 - 10) // - 10 = Reserve für wichtige Daten
				{
				char z = CodeZuZeichen(PufferAusg(&EmpfPuffer), (char*) &EmpfPuffer.BuZiMode);
				if (z != '\0' && z != '#')
					SocketOutBuf[SocketOutBufUsed++] = z;
				}
				
			SocketOutBuf[SocketOutBufUsed] = '\0';
			//! \todo Zeile mit einzelnem Punkt abfangen
			//! \todo Ende mit +++
			break;
			
		case Abmelden:
			strcpy_P(SocketOutBuf, PSTR("QUIT\r\n"));
			iTelexSocketAbbauGeplant = true;
			ProtokollPhase = WarteEnde;
			break;
			
		case WarteEnde:
			iTelexSocketAbbauGeplant = true;
			break;

		}
			
	SocketOutBufUsed = strlen(SocketOutBuf);
	StartKurzTimer(&VervollstaendigungTimer);
			
	}
	

//! Startet den Abbau der Verbindung mit einem SMTP-Server.
// ========================================================
//! Muss in jedem Zustand funktionieren können.
void SMTPSchliessen()
	{
	if (ProtokollPhase == MailData || ProtokollPhase == MailSubject)
		{
		strcpy_P(SocketOutBuf, PSTR("\r\n.\r\n"));
		ProtokollPhase = Abmelden;
		}
	else
		{
		strcpy_P(SocketOutBuf, PSTR("QUIT\r\n"));
		ProtokollPhase = WarteEnde;
		iTelexSocketAbbauGeplant = true;
		}
	SocketOutBufUsed = strlen(SocketOutBuf);
	}
	
		
// Parameternamen

const PROGMEM char EmailPOPServerAdresse_P[] = "POPSERVER";
const PROGMEM char EmailSMTPServerAdresse_P[] = "SMTPSERVER";
const PROGMEM char EmailEigeneAdresse_P[] = "EMAILADR";
const PROGMEM char EmailEigenesPasswort_P[] = "EMAILPASS";
const PROGMEM char EmailAbfrageTakt_P[] = "EMAILABFRTAKT";
const PROGMEM char EmailAusgabeFilternKennung_P[] = "EMAILFILTERKENNUNG";

/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface zum Ändern der Einstellungen des iTelex-Interface bezüglich der Anbindung 
 * an einen eMail-Server
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/
 
void itelex_cgi_email_config(void *pStruct)
	{
	static TSprache Sprache;
	
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	char Buf[TlnAdresseMax+5];

	PruefeSprache(pStruct, &Sprache);
	
	if (!KonfigFreigabe(pStruct, Sprache, true))
		return;
	
	cgi_PrintHttpheaderStart();

	if ( http_request->argc == 0 )
		{
		CgiFormStartTabbed_P(PSTR("itelexcfg-email.cgi"));

		CgiFormInputFieldText_P(ISTR(EmailKonfigPopServer, Sprache), EmailPOPServerAdresse_P, TlnAdresseMax, EmailPOPServerAdresse);
		CgiFormInputFieldText_P(ISTR(EmailKonfigSmtpServer, Sprache), EmailSMTPServerAdresse_P, TlnAdresseMax, EmailSMTPServerAdresse);
		CgiFormInputFieldText_P(ISTR(EmailKonfigEigeneAdresse, Sprache), EmailEigeneAdresse_P, TlnAdresseMax, EmailEigeneAdresse);
		CgiFormInputFieldText_P(ISTR(EmailKonfigKennwort, Sprache), EmailEigenesPasswort_P, TlnAdresseMax, EmailEigenesPasswort);
		CgiFormInputFieldULong_P(ISTR(EmailKonfigAbfragetakt, Sprache), EmailAbfrageTakt_P, 2, EmailAbfrageTakt);
		CgiFormCheckbox_P(ISTR(EmailKonfigFilterNurTX, Sprache), EmailAusgabeFilternKennung_P, EmailAusgabeFilternKennung);

		CgiFormFinish_P(ISTR(EinstellungenUebernehmen, Sprache));
		}
	else // argc > 0
		{
		printf_P(ISTR(NeueEinstellungen, Sprache));
		printf_P(PSTR("<a href=\"itelexcfg-email.cgi\">"));
		printf_P(ISTR(Weiter, Sprache));
		printf_P(PSTR("</a>"));

		CgiCheckText_P(http_request, ISTR(EmailKonfigPopServer, Sprache), EmailPOPServerAdresse_P, TlnAdresseMax, EmailPOPServerAdresse, Sprache);
		
		CgiCheckText_P(http_request, ISTR(EmailKonfigSmtpServer, Sprache), EmailSMTPServerAdresse_P, TlnAdresseMax, EmailSMTPServerAdresse, Sprache);
		
		CgiCheckText_P(http_request, ISTR(EmailKonfigEigeneAdresse, Sprache), EmailEigeneAdresse_P, TlnAdresseMax, EmailEigeneAdresse, Sprache);
		
		CgiCheckText_P(http_request, ISTR(EmailKonfigKennwort, Sprache), EmailEigenesPasswort_P, TlnAdresseMax, EmailEigenesPasswort, Sprache);
		
		EmailAbfrageTakt = CgiCheckULong_P(http_request, ISTR(EmailKonfigAbfragetakt, Sprache), EmailAbfrageTakt_P, EmailAbfrageTakt, Sprache);
		
		if (EmailAbfrageTakt != 0 
			&& (EmailPOPServerAdresse[0] == '\0'
				|| EmailEigeneAdresse[0] == '\0'
				|| EmailEigenesPasswort[0] == '\0'))
			{
			printf_P(ISTR(EmailKonfigFehler, Sprache));
			strcpy_P(Buf, PSTR("0"));
			changeConfig_P(EmailAbfrageTakt_P, Buf);
			}
			
		// Filtern nach +TX+ im Subject
		// ----------------------------
		EmailAusgabeFilternKennung = CgiCheckBool_P(http_request, ISTR(EmailKonfigFilterNurTX, Sprache), 
													EmailAusgabeFilternKennung_P, EmailAusgabeFilternKennung, Sprache);
													
		SpeichereSpracheAlsLokal(Sprache);

		POPOeffnenFehlerZaehler = 0;

		
		} // else argc > 0
		
	cgi_PrintHttpheaderEnd();

	} // itelex_cgi_email_config()
	

void itelex_email_init()
	{
	POPOeffnenFehlerZaehler = 0;
	
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
		EmailAbfrageTakt = 0; // ausgeschaltet.
		
	EmailAusgabeFilternKennung = ReadConfigBool(EmailAusgabeFilternKennung_P, false);
		
	POPWartezeitEnde = LangTimerMinuteFaktor; // 1 Minute
	
	StartLangTimer(&POPWartezeitTimer);
	
	// cgi Registrieren
	
	cgi_RegisterCGI(itelex_cgi_email_config, PSTR("itelexcfg-email.cgi"));
	}
	
	
#endif //def ITELEX_EMAIL
