/*! \file ethernet.c \brief Stellt Funktionen zum senden und empfangen bereit. */
/***************************************************************************
 *            ethernet.c
 *
 *  Sat Jun  3 17:25:42 2006
 *  Copyright  2006  Dirk Broßwick
 *  Email: sharandac@snafu.de
 ****************************************************************************/
///	\ingroup network
///	\defgroup ETHERNET Stellt Funktionen zum senden und empfangen bereit. (ethernet.c)
///	\code #include "ethernet.h" \endcode
///	\par Uebersicht
///		Stellt Funktionen zum senden und empfangen bereit.
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

#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/io.h>
#include <stdio.h>

#include "config.h"

#include "hardware/network/enc28j60.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "tcp.h"
#include "endian.h"

#if defined(EXTINT)
	#include "hardware/ext_int/ext_int.h"
#endif

#if defined(myAVR)

	#if defined(LED)
		#include "hardware/led/led_core.h"
	#else
		#error "Bitte LED aktivieren!"
	#endif

	#if defined(PC_INT)
		#include "hardware/pcint/pcint.h"
	#else
		#error "Bitte PC_INT aktivieren!"
	#endif
#endif


#ifndef DEFAULT_MAC
#define DEFAULT_MAC 0x02, 0x03, 0x6f, 0x55, 0x1c, 0xc8
#endif

char mymac[6] = { DEFAULT_MAC };


unsigned long PacketCounter;
unsigned long ByteCounter;
static char eth_state = ETH_NOTINIT;
unsigned long eth_state_error = 0;
/*
 -----------------------------------------------------------------------------------------------------------
   Die Routine die die Packete nacheinander abarbeitet
------------------------------------------------------------------------------------------------------------*/

void ethernet(void)
{
	int packet_lenght;

	char ethernetbuffer[ MAX_FRAMELEN ];

#if defined(myAVR) && defined(LED)
	LED_on(1);
#endif

	// hole ein Frame
	packet_lenght = getEthernetframe( MAX_FRAMELEN, ethernetbuffer);
	// wenn Frame vorhanden packet_lenght != 0
	// arbeite so lange die Frames ab bis keine mehr da sind
		
	 while ( packet_lenght != 0 )
	{
		PacketCounter++;
		ByteCounter = ByteCounter + packet_lenght;
		struct ETH_header *ETH_packet; 		//ETH_struc anlegen
		ETH_packet = (struct ETH_header *) ethernetbuffer; 
		switch ( ntohs( ETH_packet->ETH_typefield ) ) // welcher type ist gesetzt 
		{
			case 0x0806:
							#ifdef _DEBUG_
								printf_P( PSTR("-->> ARP\r\n") );
							#endif
							arp( packet_lenght , ethernetbuffer );
							break;
			case 0x0800:		
							#ifdef _DEBUG_
								printf_P( PSTR("-->> IP\r\n") );										
							#endif
							ip( packet_lenght , ethernetbuffer );
							break;
		}
#if defined(OpenMCP) || defined( AVRNETIO ) || defined(UPP) || defined(XPLAIN) || defined(ATXM2) || defined( EtherSense ) || defined(iTelex) || defined( iTelex_Light )
		packet_lenght = 0;
	}	
#endif
		
#if defined(myAVR)
		packet_lenght = getEthernetframe( MAX_FRAMELEN, ethernetbuffer);
	}	
#if defined(LED)
	LED_off(1);
#endif
#endif
	return;
}

/* -----------------------------------------------------------------------------------------------------------
Holt ein Ethernetframe
------------------------------------------------------------------------------------------------------------*/
unsigned int getEthernetframe( int maxlen, char *ethernetbuffer)
{
	return( enc28j60PacketReceive( maxlen , ethernetbuffer ) );
}
	
/* -----------------------------------------------------------------------------------------------------------
Sendet ein Ethernetframe
------------------------------------------------------------------------------------------------------------*/
void sendEthernetframe( int packet_lenght, char *ethernetbuffer)
{
#if defined(myAVR) && defined(LED)
	LED_on(1);
#endif
	
	PacketCounter++;
	ByteCounter = ByteCounter + packet_lenght;
	enc28j60PacketSend( packet_lenght, ethernetbuffer );
	
#if defined(myAVR) && defined(LED)
	LED_off(1);
#endif
}
	
/* -----------------------------------------------------------------------------------------------------------
Erstellt den richtigen Ethernetheader zur passenden Verbindung die gerade mit TCP_socket gewählt ist
------------------------------------------------------------------------------------------------------------*/
void MakeETHheader( char * MACadress , char * ethernetbuffer )
{
	struct ETH_header *ETH_packet; 		// ETH_struct anlegen
	ETH_packet = (struct ETH_header *) ethernetbuffer;

	int i;			

	ETH_packet->ETH_typefield = 0x0008;
	
	for ( i = 0 ; i < 6 ; i++ ) 
	{
		ETH_packet->ETH_sourceMac[i] = mymac[i];			
		ETH_packet->ETH_destMac[i] = MACadress[i];
	}
	return;		
}

/* -----------------------------------------------------------------------------------------------------------
Erstellt den richtigen Ethernetheader zur passenden Verbindung die gerade mit TCP_socket gewählt ist
------------------------------------------------------------------------------------------------------------*/
void LockEthernet( void )
{
	if( eth_state == ETH_FREE )
	{
		char sreg_tmp;
		sreg_tmp = SREG;    /* Sichern */
		cli();

		eth_state = ETH_LOCK;
		LockTCP();

#if defined(OpenMCP) || defined(UPP) || defined(iTelex) || defined( iTelex_Light )
		EXTINT_block( ENC28J60_INT );
#endif

#if defined(XPLAIN) || defined(ATXM2)
		EXTINT_block ( &ENC28J60_INT, ENC28J60_INT_PIN );
#endif

#if defined(AVRNETIO) || defined( EtherSense )
		EXTINT_block( ENC28J60_INT );
#endif

#if defined(myAVR)
		PCINT_disablePCINT( ENC28J60_INT );
#endif
		SREG = sreg_tmp;
	}
	else
		eth_state_error++;
}

/* -----------------------------------------------------------------------------------------------------------
Erstellt den richtigen Ethernetheader zur passenden Verbindung die gerade mit TCP_socket gewählt ist
------------------------------------------------------------------------------------------------------------*/
void FreeEthernet( void )
{
	if( eth_state == ETH_LOCK )
	{
		char sreg_tmp;
		sreg_tmp = SREG;    /* Sichern */
		cli();

		eth_state = ETH_FREE;

#if defined(OpenMCP) || defined(UPP) || defined(iTelex) || defined( iTelex_Light )
		EXTINT_free ( ENC28J60_INT );
#endif

#if defined(XPLAIN) || defined(ATXM2)
		EXTINT_free ( &ENC28J60_INT, ENC28J60_INT_PIN );
#endif

#if defined(AVRNETIO) || defined( EtherSense )
		EXTINT_free ( ENC28J60_INT );
#endif

#if defined(myAVR)
		PCINT_enablePIN( ENC28J60_INT_PIN, ENC28J60_INT );
		PCINT_enablePCINT( ENC28J60_INT );
#endif

		FreeTCP();
		SREG = sreg_tmp;
	}
	else
		eth_state_error++;
}

/* -----------------------------------------------------------------------------------------------------------
führt den Init durch
------------------------------------------------------------------------------------------------------------*/
void EthernetInit( void )
{
	// ENC Initialisieren //
#if defined(myAVR)
		PCINT_init();
#endif
		enc28j60Init();
		nicSetMacAddress( mymac );

		// Alle Packet lesen und ins leere laufen lassen damit ein definierter zustand herrscht
		char ethernetbuffer[ MAX_FRAMELEN ];
		while ( getEthernetframe( MAX_FRAMELEN, ethernetbuffer) != 0 ) { };
		
#if defined(OpenMCP) || defined(AVRNETIO) || defined(UPP) || defined( EtherSense ) || defined(iTelex) || defined(iTelex_Light)
		EXTINT_set ( ENC28J60_INT , SENSE_LOW , ethernet );
#endif

#if defined(XPLAIN) || defined(ATXM2)
		EXTINT_set ( &ENC28J60_INT, ENC28J60_INT_PIN, SENSE_LOW , ethernet );
#endif

#if defined(myAVR)
		PCINT_set( ENC28J60_INT, ethernet );
#endif

		eth_state = ETH_LOCK;
		eth_state_error = 0;
		
		// gibt Ethernet frei
		FreeEthernet();
}

//@}
