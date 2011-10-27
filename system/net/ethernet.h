/*! \file ethernet.h \brief Stellt Funktionen zum senden und empfangen bereit. */
/***************************************************************************
 *            ethernet.h
 *
 *  Sat Jun  3 17:25:42 2006
 *  Copyright  2006  Dirk Broßwick
 *  Email: sharandac@snafu.de
 ****************************************************************************/
///	\ingroup network
///	\defgroup ETHERNET Stellt Funktionen zum senden und empfangen bereit. (ethernet.h)
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

#ifndef __ETHERNET_H__
	
	#define __ETHERNET_H__
	
	extern char mymac[6];
	extern unsigned long PacketCounter;
	extern unsigned long ByteCounter;
	extern unsigned long eth_state_error;
	
	void ethernetloop( void );
	unsigned int getEthernetframe( int maxlen, char *buffer);
	void MakeETHheader( char * MACadress , char * buffer );
	void sendEthernetframe( int packet_lenght, char *buffer);
	void EthernetInit( void );
	void LockEthernet( void );
	void FreeEthernet( void );
	void alive( void );

	#define	ETH_LOCK	1
	#define	ETH_FREE	0

	#define ETHERNET_MIN_PACKET_LENGTH	0x3C
	#define ETHERNET_HEADER_LENGTH		14

	struct ETH_header {
		unsigned char ETH_destMac[6];	
		unsigned char ETH_sourceMac[6];
		unsigned int ETH_typefield;
	};

#endif
//@}
