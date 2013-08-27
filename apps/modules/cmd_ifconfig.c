/***************************************************************************
 *            cmd_ifconfig.c
 *
 *  Thu Nov  5 17:02:56 2009
 *  Copyright  2009  Dirk Broßwick
 *  <sharandac@snafu.de>
 ****************************************************************************/
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

/**
 * \ingroup modules
 * \addtogroup cmd_ifconfig Shell- und CGI-Interface zu ifconfig (cmd_ifconfig.c)
 *
 * @{
 */

/**
 * \file
 *
 * \author Dirk Broßwick
 */

#include <avr/pgmspace.h>
#include <avr/version.h>
#include <avr/eeprom.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "system/net/arp.h"
#include "system/net/ip.h"
#include "system/net/dns.h"
#include "system/net/ethernet.h"
#include "system/stdout/stdout.h"
#include "system/config/eeconfig.h"

#include "config.h"
#include "system/shell/shell.h"
#include "apps/httpd/cgibin/cgi-bin.h"
#include "apps/httpd/httpd2.h"
#include "apps/httpd/httpd2_pharse.h"

#include "cmd_ifconfig.h"

const char DHCP_NP[] PROGMEM = "DHCP";
const char IP_NP[] PROGMEM = "IP";
const char MASK_NP[] PROGMEM = "MASK";
const char GATE_NP[] PROGMEM = "GATE";
const char DNS_NP[] PROGMEM = "DNS";
const char MAC_NP[] PROGMEM = "MAC";

/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Registriert das Shell- und CGI-Interface zu ifconfig.
 * \param 	NONE
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/
void init_cmd_ifconfig( void )
{
#if defined(SHELL)
	SHELL_RegisterCMD( cmd_ifconfig, PSTR("ifconfig"));
#endif
#if defined(HTTPSERVER_NETCONFIG)
	cgi_RegisterCGI( cgi_network, PSTR("network.cgi"));
#endif
}

/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das Shell-Interface für zum Anzeigen der IP-config.
 * \param 	argc		Anzahl der Argumente
 * \param 	**argc		Pointer auf die Argumente
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/
int cmd_ifconfig( int argc, char ** argv )
{
	char IP[16];
		
	printf_P( PSTR(	"IP:%s\r\n") , iptostr ( myIP, IP ) );		
	printf_P( PSTR(	"Netmask:%s\r\n") , 		iptostr ( Netmask, IP ) );
	printf_P( PSTR(	"Gateway:%s\r\n") , iptostr ( Gateway, IP ) );
	#ifdef DNS
		printf_P( PSTR(	"DNS:%s\r\n") , iptostr ( DNSserver, IP ) );
	#endif
	printf_P( PSTR( "\r\nHW-Adr: %02x:%02x:%02x:%02x:%02x:%02x\r\n\r\n"),mymac[0],mymac[1],mymac[2],mymac[3],mymac[4],mymac[5]);	

	return(0);
}

#ifdef HTTPSERVER_NETCONFIG
/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Das CGI-Interface für zum Einstellen und Anzeigen der Netzwerkeinstellungen.
 * \param 	pStruct	Struktur auf den HTTP_Request
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/
void cgi_network( void * pStruct )
{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	
#ifdef TELEXPHONE
	extern uint8_t KonfigFreigabe(void *pStruct); 
	if (!KonfigFreigabe(pStruct))
		return;
#endif	
	
	char IP[32];

	cgi_PrintHttpheaderStart();

	if ( http_request->argc == 0 )
	{
					   
		printf_P( PSTR( "<pre>\r\n") );
#if defined(SHELL)
		SHELL_runcmdextern_P( PSTR("ifconfig") );
		SHELL_runcmdextern_P( PSTR("stats") );
#endif		
		printf_P( PSTR( "</pre>\r\n") );
	}
	else if( PharseCheckName_P( http_request, PSTR("config") ) )
	{	
		
		printf_P( PSTR(	"<form action=\"network.cgi\">"
					   	"<table border=\"0\" cellpadding=\"5\" cellspacing=\"0\">" ));
#ifdef DHCP
		readConfig_P( PSTR("DHCP"), IP );
  		printf_P( PSTR( "<tr>"
					   	"<td align=\"right\">DHCP active</td>"
					    "<td><input type=\"checkbox\" name=\"DHCP\" value=\"on\" " )); 
		if ( !strcmp_P( IP, PSTR("on") ) )
						printf_P( PSTR("checked"));
		printf_P( PSTR(	"></td>"
  						"</tr>") );
#endif
		if ( readConfig_P ( PSTR("IP"), IP ) != 1 )
			iptostr ( myIP, IP );
  		printf_P( PSTR(	"<tr>"
					   	"<td align=\"right\">IP address</td>"
  						"<td><input name=\"IP\" type=\"text\" size=\"15\" value=\"%s\" maxlength=\"15\"></td>"
  						"</tr>") , IP );
		if ( readConfig_P ( PSTR("MASK"), IP ) != 1 )
			iptostr ( Netmask, IP );		
  		printf_P( PSTR(	"<tr>"
					   	"<td align=\"right\">Netmask</td>"
  						"<td><input name=\"MASK\" type=\"text\" size=\"15\" value=\"%s\" maxlength=\"15\"></td>"
  						"</tr>") , IP );
		if ( readConfig_P ( PSTR("GATE"), IP ) != 1 )
			iptostr ( Gateway, IP );		
  		printf_P( PSTR(	"<tr>"
					   	"<td align=\"right\">Gateway</td>"
						"<td><input name=\"GATE\" type=\"text\" size=\"15\" value=\"%s\" maxlength=\"15\"></td>"
  						"</tr>") , IP );
#ifdef DNS
		if ( readConfig_P ( PSTR("DNS"), IP ) != 1 )
			iptostr ( DNSserver, IP );		
		printf_P( PSTR(	"<tr>"
					   	"<td align=\"right\">DNS-Server</td>"
						"<td><input name=\"DNS\" type=\"text\" size=\"15\" value=\"%s\" maxlength=\"15\"></td>"
  						"</tr>" ), IP );
#endif
		if ( readConfig_P ( PSTR("MAC"), IP ) != 1 )
			mactostr( mymac, IP );
  		printf_P( PSTR(	"<tr>"
					   	"<td align=\"right\">Hardware-MAC</td>"
						"<td><input name=\"MAC\" type=\"text\" size=\"17\" value=\"%s\" maxlength=\"17\"></td>"
  						"</tr>"
		  				"<tr>"
   						"<td></td><td><input type=\"submit\" value=\" Save changes \"></td>"
  						"</tr>"
					   	"</table>"
						"</form>") , IP );
	}
	else
	{
#ifdef DHCP
		if ( PharseCheckName_P( http_request, DHCP_NP ) )
			changeConfig_P( DHCP_NP, http_request->argvalue[ PharseGetValue_P ( http_request, DHCP_NP ) ] );
		else
			changeConfig_P( DHCP_NP, "off" );
#endif
		if ( PharseCheckName_P( http_request, IP_NP ) )
			changeConfig_P( IP_NP, http_request->argvalue[ PharseGetValue_P ( http_request, IP_NP ) ] );
		if ( PharseCheckName_P( http_request, MASK_NP ) )
			changeConfig_P( MASK_NP, http_request->argvalue[ PharseGetValue_P ( http_request, MASK_NP ) ] );
		if ( PharseCheckName_P( http_request, GATE_NP ) )
			changeConfig_P( GATE_NP, http_request->argvalue[ PharseGetValue_P ( http_request, GATE_NP ) ] );
		#ifdef DNS
		if ( PharseCheckName_P( http_request, DNS_NP ) )
			changeConfig_P( DNS_NP, http_request->argvalue[ PharseGetValue_P ( http_request, DNS_NP ) ] );
		#endif
		if ( PharseCheckName_P( http_request, MAC_NP ) )
			changeConfig_P( MAC_NP, http_request->argvalue[ PharseGetValue_P ( http_request, MAC_NP ) ] );
		printf_P( PSTR("Settings saved, reboot necessary to take effect!\r\n"));
	}
	
	cgi_PrintHttpheaderEnd();
}
#endif

/**
 * @}
 */
