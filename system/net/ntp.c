/*! \file "ntp.c" \brief Die ntp-Funktionlitaet */
/***************************************************************************
 *            ntp.c
 *
 *  Mon Aug 28 11:36:49 2006
 *  Copyright  2006  Dirk Broßwick
 *  Email: sharandac@snafu.de
 *
 *  Changed to 'real' NTP protocol, Fred Sonnenrein 29.11.2018
 ****************************************************************************/
///	\ingroup network
///	\defgroup NTP NTP-Funktionen (ntp.c)
///	\code #include "ip.h" \endcode
///	\code #include "dns.h" \endcode
///	\code #include "tcp.h" \endcode
///	\code #include "ntp.h" \endcode
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
#include <avr/pgmspace.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/interrupt.h>

#include "system/clock/clock.h"

#include "config.h"

#include "ip.h"
#include "udp.h"
#include "ntp.h"
#include "dns.h"


enum { NtpPacketSize = 48 } ;



// #define NTP_DEBUG

/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Holt die Zeit von einen NTP-Server.
 * \param 	IP				Die IP-Adresse von einen NTP-Server.
 * \param 	dnsbuffer		Der DNS-Name von einen NTP-Server.
 * \param 	timedif			Der Zeitunterschied, wichtig bei Sommer/Winterzeit.
 * \return	int
 */
/*------------------------------------------------------------------------------------------------------------*/
unsigned int NTP_GetTime( unsigned long IP, char * dnsbuffer, int timedif )
	{
		char buffer[ NtpPacketSize ];
		int socket, timer, retval = NTP_ERROR;

#if defined(NTP_DEBUG)
		char String[30];
#endif
		
		struct TIME time;

#if defined(NTP_DEBUG)
		printf_P( PSTR("Oeffne neues Socket.\r\n") );
#endif		
		if ( IP == 0 )
		{
#ifdef UDP
	#ifdef DNS
			if ( dnsbuffer != 0 )
			{
				// Host nach IP auflösen
				IP = DNS_ResolveName( dnsbuffer );
				// könnte er aufgelöst werden ?
				if ( IP == DNS_NO_ANSWER ) return( NTP_ERROR );
			}
			else
	#endif
#endif
			return( NTP_ERROR );
		}
		// UDP-socket aufmachen für Bootp
		socket = UDP_RegisterSocket( IP , 123 , NtpPacketSize , buffer);
		// Wenn Fehler aufgetretten, return
		if ( socket == UDP_SOCKET_ERROR ) 
		{
			return ( NTP_ERROR );
		}

#if defined(NTP_DEBUG)
			printf_P( PSTR("UDP-Socket aufgemacht zur %s.\r\n"), iptostr( IP, String ) );
#endif
		// UDP-Packet an Time-server senden
		memset(buffer, 0, NtpPacketSize);
		  // Initialize values needed to form NTP request
		  // (see URL above for details on the packets)
		buffer[0] = 0x1b;  //  ALT: 0b11100011;   // LI, Version, Mode
/* TODO nach bewährung löschen
		buffer[1] = 0;     // Stratum, or type of clock
		buffer[2] = 6;     // Polling Interval
		buffer[3] = 0xEC;  // Peer Clock Precision
						   // 8 bytes of zero for Root Delay & Root Dispersion
		buffer[12] = 49;
		buffer[13] = 0x4E;
		buffer[14] = 49;
		buffer[15] = 52;
*/

		UDP_SendPacket(socket, NtpPacketSize , buffer);

#if defined(NTP_DEBUG)
		printf_P( PSTR("UDP-Packet gesendet.\r\n"));
#endif
		// Timeout-counter reservieren und starten
		timer = CLOCK_RegisterCountdowntimer();
		if ( timer == CLOCK_FAILED ) return ( NTP_ERROR );

		CLOCK_SetCountdownTimer( timer , 1500, MSECOUND );

#if defined(NTP_DEBUG)
		printf_P( PSTR("Warte auf Antwort."));
#endif
		// Auf Antwort des Timer-Servers warten
		while( 1 )
		{
			// Wenn Time-Server geantwortet hat inerhalb des Timeouts, hier weiter
			if ( UDP_GetSocketState( socket ) == UDP_SOCKET_BUSY && ( CLOCK_GetCountdownTimer( timer ) != 0 ) )
			{
				// Sind genug Bytes empfangen worden, wenn ja okay, sonst fehler
				if ( UDP_GetByteInBuffer( socket ) >= NtpPacketSize )
				{				
					// Daten kopieren und Zeit ausrechnen
					unsigned long secsSince1900;
					// convert four bytes starting at location 40 to a long integer
					secsSince1900 = ((unsigned long)buffer[40] << 24)
								  | ((unsigned long)buffer[41] << 16)
								  | ((unsigned long)buffer[42] << 8)
								  | ((unsigned long)buffer[43]);

					CLOCK_GetTime( &time );

					time.time = secsSince1900;

					// check for "bad" values:
					if (timedif <= 12 && timedif >= -12)
						time.timezone = timedif;
					else
						time.timezone = 0;
					
					CLOCK_decode_time( &time );

					CLOCK_SetTime( &time );
					
					retval = NTP_OK;
#if defined(NTP_DEBUG)
					printf_P( PSTR("Antwort erhalten.\r\n"));
#endif
					// fertisch
					break;
				}
				else
				{
#if defined(NTP_DEBUG)
					printf_P( PSTR("Falsches Format der Antwort.\r\n"));
#endif
					retval = NTP_ERROR;
					break;
				}
			}
			// Timeout erreicht ? Wenn ja Fehler.
			if ( CLOCK_GetCountdownTimer( timer ) == 0 )
			{
#if defined(NTP_DEBUG)
				printf_P( PSTR("Timeout beim warten auf Antwort.\r\n"));
#endif
				retval = NTP_ERROR;
				break;
			}
		}
		// timer freigeben und UDP-Socket schliessen
		CLOCK_ReleaseCountdownTimer( timer );
		UDP_CloseSocket( socket );
#if defined(NTP_DEBUG)
		printf_P( PSTR("UDP-Socket geschlossen.\r\n"));
#endif
		return( retval );
	}
//@}
