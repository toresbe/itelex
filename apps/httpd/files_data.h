/*! \file Files_data.h */
//***************************************************************************
//*            files_data.h
//*
//*  Mon Jun 23 14:19:16 2008
//*  Copyright  2006 Dirk Broßwick
//*  Email: sharandac@snafu.de
//****************************************************************************/
///	\ingroup http_files
///	\addtogroup data_files Dateien in im Flash gespeichert werden (files_data.h)
//****************************************************************************/
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
//@{
 
#ifndef _FILES_DATA_H
	#define FILES_DATA_H

	#include "config.h"
	#include <avr/pgmspace.h>

	#define MAX_FILES_ENTRYS 	32

#if !defined(HTTP_FILES_FROM_MMC)

	typedef struct {
		const prog_char	*filesname;
		const prog_char	*files;
		const prog_char	filestype;
		const int	len;
	} const FILES ;

	#define	TEXT	0
	#define	JPEG	1
	#define PNG		2

	#include "config.h"

const char HtmlPageIndexName[] PROGMEM = "index.html";
const char HtmlPageIndexData[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<TITLE>TxP2-Net - Das Internet-Interface zum TelexPhone</TITLE>"
	"</HEAD>"
    "<BODY>"
	"<h2>TxP2-Net - Das Internet-Interface zum TelexPhone</h2>"
	"<h1>Hauptseite</h1>"
	"<pre>"
	"__________________________________________________________________________\r\n"
	" OOOOO  OOOOO  O      OOOOO  O   O       OOOO   O   O   OOO   O   O  OOOOO \r\n"
	"   O    O      O      O       O O        O   O  O   O  O   O  OO  O  O     \r\n"
	" .........................................................................\r\n"
	"   O    OOO    O      OOO      O    OOO  OOOO   OOOOO  O   O  O O O  OOO   \r\n"
	"   O    O      O      O       O O        O      O   O  O   O  O  OO  O     \r\n"
	"   O    OOOOO  OOOOO  OOOOO  O   O       O      O   O   OOO   O   O  OOOOO\r\n"
	"__________________________________________________________________________</pre><p>"
	"<a href=\"txp-msg.cgi\">Nachricht senden</a><p>"
	"<a href=\"txp-config.cgi\">Einstellungen</a><p>"
	"<a href=\"txp-debug.cgi\">Debug-Infos</a><p>"
	#ifdef HTTPSERVER_NETCONFIG
		"<a href=\"network.html\">Netzwerk</a><p>"
	#endif
	#ifdef HTTPSERVER_SYSTEM
		"<a href=\"system.html\">System</a><p>"
	#endif
	"<h3>Grundlage: OpenMCP</h3>"
	"Microwebserver build on AVR-libc version: " __AVR_LIBC_VERSION_STRING__ "/" __AVR_LIBC_DATE_STRING__ " with avr-gcc "__VERSION__", Date: " __DATE__ " " __TIME__ "</BODY>"
	"</HTML>"
	"\r\n\r\n"	};



#ifdef HTTPSERVER_NETCONFIG
const char HtmlPageNetworkName[] PROGMEM = "network.html";
const char HtmlPageNetworkData[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<TITLE>TxP2-Net - Das Internet-Interface zum TelexPhone</TITLE>"
	"</HEAD>"
	"<BODY>"
	"<h2>TxP2-Net - Das Internet-Interface zum TelexPhone</h2>"
	"<h1>Netzwerk-Menue</h1>"
	"<a href=\"network.cgi\">Netzwerk-Infos</a><p>"
	"<a href=\"network.cgi?config\">Netzwerk-Konfiguration</a><p>"
	"<a href=\"index.html\">Hauptseite</a><p>"
	"</BODY>"
	"</HTML>"
	"\r\n\r\n"	};
#endif

#ifdef HTTPSERVER_SYSTEM
const char HtmlPageSystemName[] PROGMEM = "system.html";
const char HtmlPageSystemData[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<TITLE>TxP2-Net - Das Internet-Interface zum TelexPhone</TITLE>"
	"</HEAD>"
	"<BODY>"
	"<h2>TxP2-Net - Das Internet-Interface zum TelexPhone</h2>"
	"<h1>System-Menue</h1>"
	"<a href=\"index.html\">zurueck</a><p>"
#if defined(HTTPSERVER_RESET)
	"<a href=\"reset.cgi\">Reset</a><p>"
#endif
#if defined(HTTPSERVER_NTP)
	"<a href=\"ntp.cgi\">NTP</a><p>"
#endif
#if defined(HTTPSERVER_EEMEM)
	"<a href=\"eemem.cgi\">EEmem</a><p>"
#endif
#if defined(HTTPSERVER_DYNDNS)
	"<a href=\"dyndns.cgi\">DynDNS</a><p>"
#endif
#if defined(HTTPSERVER_TWITTER)
	"<a href=\"twitter.cgi\">Twitter</a><p>"
#endif
#if defined(HTTPSERVER_CRON)
	"<a href=\"cron.cgi\">cron</a><p>"
#endif
	"</BODY>"
	"</HTML>"
	"\r\n\r\n"	};
#endif


FILES files[] = {
	{ HtmlPageIndexName, HtmlPageIndexData, TEXT, sizeof( HtmlPageIndexData ) - 1 },
#ifdef HTTPSERVER_NETCONFIG
	{ HtmlPageNetworkName, HtmlPageNetworkData, TEXT, sizeof( HtmlPageNetworkData ) - 1 },
#endif
#ifdef HTTPSERVER_SYSTEM
	{ HtmlPageSystemName, HtmlPageSystemData, TEXT, sizeof( HtmlPageSystemData ) - 1 },
#endif
	{ 0,0,0,0 }
};
#endif

#endif /* FILES_DATA_H */

//@}
