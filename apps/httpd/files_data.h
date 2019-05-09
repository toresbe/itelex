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
	
	#include "SvnVersion.h"

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

const char files1[] PROGMEM = "index.html";
const char data1[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<TITLE>i-Telex</TITLE>"
	"</HEAD>"
	"<frameset rows=\"64,40,*"
	#ifdef HTTPSERVER_STATS
		",40"
	#endif
	"\" scrolling=\"no\" frameborder=\"2\" border=\"2\" framespacing=\"2\" bordercolor=\"#000000\">"
  	"<frame src=\"headline.html\" name=\"Navigation1\" scrolling=\"auto\">"
  	"<frame src=\"mainmenu.html\" name=\"Navigation2\" scrolling=\"auto\">"
  	"<frame src=\"info.html\" name=\"main\">"
	#ifdef HTTPSERVER_STATS
  	"<frame src=\"stats.cgi\" name=\"update\" scrolling=\"auto\">"
	#endif
  	"<noframes>"
    "<body>"
    "<p>Ihr Browser unterst&uuml;tzt keine Frames!</p>"
    "</body>"
  	"</noframes>"
	"</frameset>"
	"</HTML>"
	"\r\n\r\n"	};

const char files2[] PROGMEM = "headline.html";
const char data2[] PROGMEM = {
	"<HTML>"
	"<BODY text=\"#0000C0\" style=\"background-image:url(lochstreifen-hg.png)\">"
	"<span style=\"font: bold 40px 'Courier New','Lucida Console',monospace; \"><b>"
	"i-Telex - ToIP - ethernet interface"
	"</b></span>"
	"</BODY>"
	"</HTML>"
	"\r\n\r\n"	};

	
const char PROGMEM lochstr_hg_filename[] = "lochstreifen-hg.png";

#define byte char // nur für den Lochstreifen jetzt...
#define unsigned PROGMEM // nur für den Lochstreifen jetzt... 	
#include "lochstreifen-hg.h"
#undef unsigned
#undef byte
	
	
#ifdef HTTPSERVER_IO
	const char files3[] PROGMEM = "io.html";
	const char data3[] PROGMEM = {
		"<HTML>"
		"<HEAD>"
		"<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">"
		"</HEAD>"
		"<BODY bgcolor=\"#C0FFC0\">" 
		"<a href=\"mainmenu.html\">zurueck</a>"
		#if defined(HTTPSERVER_DIGITAL_IO)
			" / <a href=\"dio_out.cgi\" target=\"main\">Digital Out</a> / <a href=\"dio_in.cgi\" target=\"main\">Digital In</a>"
		#endif
		#if defined(HTTPSERVER_ANALOG)
			" / <a href=\"aio.cgi\" target=\"main\">Analog In</a>"
		#endif
		#if defined(HTTPSERVER_ONEWIRE)
			" / <a href=\"onewire.cgi\" target=\"main\">1-Wire</a>"
		#endif
		#if defined(HTTPSERVER_TWI)
			" / <a href=\"twi.cgi\" target=\"main\">TWI</a>"
		#endif
		#if defined(LEDTAFEL)
			" / <a href=\"tafel.cgi\" target=\"main\">LED-Tafel</a>"
		#endif
		#if defined(IMPULSCOUNTER)
			" / <a href=\"impuls.cgi\" target=\"main\">Impulsz&auml;hler</a>"
		#endif
		#if defined(TEMP_LOGGER)
			" / <a href=\"templogger.html\">Templogger</a>"
		#endif
		"</BODY>"
		"</HTML>"
		"\r\n\r\n"	};
#endif

#if defined(HTTPSERVER_STREAM)
const char files9[] PROGMEM = "stream.html";
const char data9[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">"
	"</HEAD>"
	"<BODY bgcolor=\"#C0FFC0\">" 
	"<a href=\"mainmenu.html\">zurueck</a> / <a href=\"stream.cgi\" target=\"main\">Stream</a> / <a href=\"stream.cgi?info\" target=\"main\">Infos</a> / <a href=\"stream.cgi?config\" target=\"main\">Konfiguration</a>"
	"</BODY>"
	"</HTML>"
	"\r\n\r\n"	};
#endif

const char files4[] PROGMEM = "mainmenu.html";
const char data4[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">"
	"</HEAD>"
	"<BODY bgcolor=\"#C0FFC0\">" 
	"<a href=\"info.html\" target=\"main\">Info</a>"
	#if defined(iTelex)
	" / <a href=\"itelex-menu-de.html\">i-Telex (Deutsch)</a>"
	" / <a href=\"itelex-menu-en.html\">i-Telex (English)</a>"
	" / <a href=\"itelex-credits.cgi\" target=\"main\">Impressum / credits</a>"
	#endif
	#if defined(HTTPSERVER_STREAM)
		" / <a href=\"stream.html\">Stream</a>"
	#endif
	#ifdef HTTPSERVER_IO
		" / <a href=\"io.html\">IO-Ports</a>"
	#endif
	/* nach iTelex-Konfiguration verlegt
	#ifdef HTTPSERVER_NETCONFIG
		" / <a href=\"network.html\">Netzwerk</a>"
	#endif
	#ifdef HTTPSERVER_SYSTEM
		" / <a href=\"system.html\">System</a>"
	#endif
	*/
	"</BODY>"
	"</HTML>"
	"\r\n\r\n"	};


#ifdef HTTPSERVER_NETCONFIG

const char NetzwerkMenuDeName[] PROGMEM = "network-de.html";
const char NetzwerkMenuDeText[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">"
	"</HEAD>"
	"<BODY bgcolor=\"#C0FFC0\">" 
	"<a href=\"itelexcfg-menu-de.html\">zur&uuml;ck</a> / <a href=\"network.cgi?spr=de\" target=\"main\">Infos</a> / <a href=\"network.cgi?spr=de&config\" target=\"main\">Konfiguration</a>"
	"</BODY>"
	"</HTML>"
	"\r\n\r\n"	};

const char NetzwerkMenuEnName[] PROGMEM = "network-en.html";
const char NetzwerkMenuEnText[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">"
	"</HEAD>"
	"<BODY bgcolor=\"#C0FFC0\">" 
	"<a href=\"itelexcfg-menu-en.html\">back</a> / <a href=\"network.cgi?spr=en\" target=\"main\">Infos</a> / <a href=\"network.cgi?spr=en&config\" target=\"main\">Configuration</a>"
	"</BODY>"
	"</HTML>"
	"\r\n\r\n"	};
	
#endif

#ifdef HTTPSERVER_SYSTEM

const char SystemMenuDeName[] PROGMEM = "system-de.html";
const char SystemMenuDeText[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">"
	"</HEAD>"
	"<BODY bgcolor=\"#C0FFC0\">" 
	"<a href=\"itelexcfg-menu-de.html\">zur&uuml;ck</a> "
#if defined(HTTPSERVER_RESET)
	"/ <a href=\"reset.cgi?spr=de\" target=\"main\">Reset</a>"
#endif
#if defined(HTTPSERVER_NTP) || defined(iTelex)
	" / <a href=\"ntp.cgi?spr=de\" target=\"main\">NTP</a>"
#endif
#if defined(HTTPSERVER_DYNDNS)
	" / <a href=\"dyndns.cgi?spr=de\" target=\"main\">DynDNS</a>"
#endif
#if defined(HTTPSERVER_TWITTER)
	" / <a href=\"twitter.cgi\" target=\"main\">Twitter</a>"
#endif
#if defined(HTTPSERVER_EEMEM)
	" / <a href=\"eemem.cgi?spr=de\" target=\"main\">EEmem</a>"
#endif
#if defined(HTTPSERVER_CRON)
	" / <a href=\"cron.cgi?spr=de\" target=\"main\">cron</a>"
#endif
	"</BODY>"
	"</HTML>"
	"\r\n\r\n"	};

const char SystemMenuEnName[] PROGMEM = "system-en.html";
const char SystemMenuEnText[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">"
	"</HEAD>"
	"<BODY bgcolor=\"#C0FFC0\">" 
	"<a href=\"itelexcfg-menu-en.html\">back</a> "
#if defined(HTTPSERVER_RESET)
	"/ <a href=\"reset.cgi?spr=en\" target=\"main\">Reset</a>"
#endif
#if defined(HTTPSERVER_NTP) || defined(iTelex)
	" / <a href=\"ntp.cgi?spr=en\" target=\"main\">NTP</a>"
#endif
#if defined(HTTPSERVER_DYNDNS)
	" / <a href=\"dyndns.cgi?spr=en\" target=\"main\">DynDNS</a>"
#endif
#if defined(HTTPSERVER_TWITTER)
	" / <a href=\"twitter.cgi\" target=\"main\">Twitter</a>"
#endif
#if defined(HTTPSERVER_EEMEM)
	" / <a href=\"eemem.cgi?spr=en\" target=\"main\">EEmem</a>"
#endif
#if defined(HTTPSERVER_CRON)
	" / <a href=\"cron.cgi?spr=en\" target=\"main\">cron</a>"
#endif
	"</BODY>"
	"</HTML>"
	"\r\n\r\n"	};
#endif

const char files7[] PROGMEM = "info.html";
const char data7[] PROGMEM = {
	"<HTML>"
	"<BODY>"
	"<pre><p>"
	"  Welcome to\r\n"
	"______________________________________________\r\n"
	"                                              \r\n"
	"  OOO      OOOOO  OOOOO  O      OOOOO  O   O  \r\n"
	"   O         O    O      O      O       O O   \r\n"
	" .............................................\r\n"
	"   O   OOO   O    OOO    O      OOO      O    \r\n"
	"   O         O    O      O      O       O O   \r\n"
	"  OOO        O    OOOOO  OOOOO  OOOOO  O   O  \r\n"
	"______________________________________________\r\n"
	"Build " SVNVERSION 
#ifdef PROG_ID_ZUSATZ
	" " PROG_ID_ZUSATZ
#endif
	" at Date: " __DATE__ " " __TIME__ "\r\n"
	"Modules:"
#ifdef ITELEX_ANSCHLUSS
	" Anschluss"
#endif
#ifdef ITELEX_TLNSERVER
	" Nameserver"
#endif
#ifdef ITELEX_EMAIL
	" Email"
#endif
	"\r\n\r\n"
	"build on AVR-libc version: " __AVR_LIBC_VERSION_STRING__ "/" __AVR_LIBC_DATE_STRING__ 
	" with avr-gcc " __VERSION__ "\r\n"
	"\r\n"
	"(c)2006-2019   Software: Fred Sonnenrein\r\n"
	"based on OpenMCP by Dirk Brosswick\r\n"
	"</pre></p><br>"
	"<p>Our homepage: <a href=\"http://www.i-telex.net/\" target=\"blank\">www.i-telex.net</a>"
	"<p>This project on <a href=\"http://sourceforge.net/projects/itelex/\" target=\"_blank\">SourceForge.net</a>"
	"</BODY>"
	"</HTML>"
	"\r\n"	};

const char files8[] PROGMEM = "style.css";
const char data8[] PROGMEM = {
	"@charset \"ISO-8859-1\"\r\n"
	"   a:link { text-decoration:none; font-weight:bold; color:#0000A0; } \r\n"
	"   a:visited { text-decoration:none; font-weight:bold; color:#A000A0; } \r\n"
	"   a:hover { text-decoration:none; font-weight:bold; background-color:#FFCCCC; } \r\n"
	"   a:active { text-decoration:none; font-weight:bold; background-color:#FFFFCC; } \r\n"
	"   a:focus { text-decoration:none; font-weight:bold; background-color:#CCCCFF; } \r\n"
	"   pre { overflow: scroll; }\r\n" };

#if defined(TEMP_LOGGER)
const char files10[] PROGMEM = "templogger.html";
const char data10[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">"
	"</HEAD>"
	"<BODY bgcolor=\"#C0FFC0\">" 
	"<a href=\"io.html\">zurueck</a> "
	" / <a href=\"templogger.cgi\" target=\"main\">Templogger</a>"
	" / <a href=\"tempconfig.cgi\" target=\"main\">Config</a>"
	"</BODY>"
	"</HTML>"
	"\r\n\r\n"	};
#endif

#if defined(iTelex)

const char iTelexMainMenuDeName[] PROGMEM = "itelex-menu-de.html";
const char iTelexMainMenuDeText[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">"
	"</HEAD>"
	"<BODY bgcolor=\"#C0FFC0\">" 
	"<a href=\"mainmenu.html\">zur&uuml;ck</a>"
	" / <a href=\"itelex-msg-de.html\" target=\"main\">Nachricht senden</a>"
	" / <a href=\"itelex-tlnverz.cgi?spr=de\" target=\"main\">Teilnehmer-Verzeichnis</a>"
	" / <a href=\"itelexcfg-menu-de.html\">i-Telex-Einstellungen</a>"
	" / <a href=\"itelex-debug.cgi\" target=\"main\">Debug-Infos</a>"
	" / <a href=\"itelex-twitlnliste.cgi?spr=de\" target=\"main\">Bus-Tln-Liste</a>"
#if defined(MMC)
	" / <a href=\"sddir.cgi\" target=\"main\">SD-Karte</a>"
#endif //defined(MMC)
	"</BODY>"
	"</HTML>"
	"\r\n\r\n" } ;

const char iTelexMainMenuEnName[] PROGMEM = "itelex-menu-en.html";
const char iTelexMainMenuEnText[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">"
	"</HEAD>"
	"<BODY bgcolor=\"#C0FFC0\">" 
	"<a href=\"mainmenu.html\">back</a>"
	" / <a href=\"itelex-msg-en.html\" target=\"main\">Send message</a>"
	" / <a href=\"itelex-tlnverz.cgi?spr=en\" target=\"main\">Subscriber directory</a>"
	" / <a href=\"itelexcfg-menu-en.html\">i-Telex settings</a>"
	" / <a href=\"itelex-debug.cgi\" target=\"main\">debug info</a>"
	" / <a href=\"itelex-twitlnliste.cgi?spr=en\" target=\"main\">list of modules</a>"
#if defined(MMC)
	" / <a href=\"sddir.cgi\" target=\"main\">SD card</a>"
#endif //defined(MMC)
	"</BODY>"
	"</HTML>"
	"\r\n\r\n" } ;

const char iTelexCfgMenuDeName[] PROGMEM = "itelexcfg-menu-de.html";
const char iTelexCfgMenuDeText[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">"
	"</HEAD>"
	"<BODY bgcolor=\"#C0FFC0\">" 
	"<a href=\"itelex-menu-de.html\">zur&uuml;ck</a>"
	" / <a href=\"itelexcfg-intern.cgi?spr=de\" target=\"main\">Einstellungen im lokalen System</a>"
	" / <a href=\"itelexcfg-extern.cgi?spr=de\" target=\"main\">Einstellungen im i-Telex-Netz</a>"
	" / <a href=\"itelex-credits.cgi?spr=de\" target=\"main\">Impressum</a>"
#ifdef ITELEX_EMAIL
	" / <a href=\"itelexcfg-email.cgi?spr=de\" target=\"main\">eMail-Einstellungen</a>"
#endif //def ITELEX_EMAIL	
#ifdef HTTPSERVER_NETCONFIG
	" / <a href=\"network-de.html\">Netzwerk</a>"
#endif
#ifdef HTTPSERVER_SYSTEM
	" / <a href=\"system-de.html\">System</a>"
#endif
	" / <a href=\"itelexcfg-sperren.cgi?spr=de\" target=\"main\">Sperren</a>"
	"</BODY>"
	"</HTML>"
	"\r\n\r\n" } ;

	
const char iTelexCfgMenuEnName[] PROGMEM = "itelexcfg-menu-en.html";
const char iTelexCfgMenuEnText[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">"
	"</HEAD>"
	"<BODY bgcolor=\"#C0FFC0\">" 
	"<a href=\"itelex-menu-en.html\">back</a>"
	" / <a href=\"itelexcfg-intern.cgi?spr=en\" target=\"main\">local settings</a>"
	" / <a href=\"itelexcfg-extern.cgi?spr=en\" target=\"main\">i-Telex network settings</a>"
	" / <a href=\"itelex-credits.cgi?spr=en\" target=\"main\">credits</a>"
#ifdef ITELEX_EMAIL
	" / <a href=\"itelexcfg-email.cgi?spr=en\" target=\"main\">eMail settings</a>"
#endif //def ITELEX_EMAIL	
#ifdef HTTPSERVER_NETCONFIG
	" / <a href=\"network-en.html\">Network</a>"
#endif
#ifdef HTTPSERVER_SYSTEM
	" / <a href=\"system-en.html\">System</a>"
#endif
	" / <a href=\"itelexcfg-sperren.cgi?spr=en\" target=\"main\">lock</a>"
	"</BODY>"
	"</HTML>"
	"\r\n\r\n" } ;

	
const char iTelexChatDeName[] PROGMEM = "itelex-msg-de.html";
const char iTelexChatDeText[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"</HEAD>"
	"<frameset rows=\"*,60\" scrolling=\"no\" frameborder=\"2\" border=\"2\" framespacing=\"2\" bordercolor=\"#000000\">"
	"<frame src=\"itelex-msg-out.cgi?spr=de\" name=\"MsgOut\" scrolling=\"auto\">"
	"<frame src=\"itelex-msg-in.cgi?spr=de\" name=\"MsgIn\" scrolling=\"no\">"
	"<noframes>"
	"<body>"
	"<p>Ihr Browser unterstützt keine Frames!</p>"
	"</body>"
	"</noframes>"
	"</frameset>"
	"</HTML>"
	"\r\n\r\n" } ;

	
const char iTelexChatEnName[] PROGMEM = "itelex-msg-en.html";
const char iTelexChatEnText[] PROGMEM = {
	"<HTML>"
	"<HEAD>"
	"</HEAD>"
	"<frameset rows=\"*,60\" scrolling=\"no\" frameborder=\"2\" border=\"2\" framespacing=\"2\" bordercolor=\"#000000\">"
	"<frame src=\"itelex-msg-out.cgi?spr=en\" name=\"MsgOut\" scrolling=\"auto\">"
	"<frame src=\"itelex-msg-in.cgi?spr=en\" name=\"MsgIn\" scrolling=\"no\">"
	"<noframes>"
	"<body>"
	"<p>Ihr Browser unterstützt keine Frames!</p>"
	"</body>"
	"</noframes>"
	"</frameset>"
	"</HTML>"
	"\r\n\r\n" } ;
		
#endif //def iTelex


const char RobotsTxtName[] PROGMEM = "robots.txt";
const char RobotsTxtData[] PROGMEM = {
	"# robots.txt zum i-telex Web-Interface\r\n"
	"User-agent: *\r\n"
	"Disallow: /\r\n" } ;


FILES files[] = {
	{ files1, data1, TEXT, sizeof( data1 ) - 1 },
	{ files2, data2, TEXT, sizeof( data2 ) - 1 },
#ifdef HTTPSERVER_IO
	{ files3, data3, TEXT, sizeof( data3 ) - 1 },
#endif
	{ files4, data4, TEXT, sizeof( data4 ) - 1 },
#ifdef HTTPSERVER_NETCONFIG
	{ NetzwerkMenuDeName, NetzwerkMenuDeText, TEXT, sizeof( NetzwerkMenuDeText ) - 1 },
	{ NetzwerkMenuEnName, NetzwerkMenuEnText, TEXT, sizeof( NetzwerkMenuEnText ) - 1 },
#endif
#ifdef HTTPSERVER_SYSTEM
	{ SystemMenuDeName, SystemMenuDeText, TEXT, sizeof( SystemMenuDeText ) - 1 },
	{ SystemMenuEnName, SystemMenuEnText, TEXT, sizeof( SystemMenuEnText ) - 1 },
#endif
	{ files7, data7, TEXT, sizeof( data7 ) - 1 },
	{ files8, data8, TEXT, sizeof( data8 ) - 1 },
#if defined(HTTPSERVER_STREAM)
	{ files9, data9, TEXT, sizeof( data9 ) - 1 },
#endif
#if defined(TEMP_LOGGER)
	{ files10, data10, TEXT, sizeof( data10 ) - 1 },
#endif
#if defined(iTelex)
	{ iTelexMainMenuDeName, iTelexMainMenuDeText, TEXT, sizeof( iTelexMainMenuDeText ) - 1 },
	{ iTelexMainMenuEnName, iTelexMainMenuEnText, TEXT, sizeof( iTelexMainMenuEnText ) - 1 },
	{ iTelexCfgMenuDeName, iTelexCfgMenuDeText, TEXT, sizeof( iTelexCfgMenuDeText ) - 1 },
	{ iTelexCfgMenuEnName, iTelexCfgMenuEnText, TEXT, sizeof( iTelexCfgMenuEnText ) - 1 },
	{ iTelexChatDeName, iTelexChatDeText, TEXT, sizeof( iTelexChatDeText ) - 1 },
	{ iTelexChatEnName, iTelexChatEnText, TEXT, sizeof( iTelexChatEnText ) - 1 },
#endif
	{ lochstr_hg_filename, lochstreifen_hg, PNG, sizeof( lochstreifen_hg ) },
	{ RobotsTxtName, RobotsTxtData, TEXT, sizeof(RobotsTxtData) - 1 },
	{ 0,0,0,0 }
};
#endif

#endif /* FILES_DATA_H */

//@}
