/*! \file IspMaster.c \brief Funktionen für In-System-Programming von anderen Modulen */
//***************************************************************************
//*            IspMaster.c
//*
//****************************************************************************/
///	\ingroup software
///	\defgroup iTelex Hauptfunktion dieser Applikation: Schnittstelle vom Internet
/// zum Fernschreiber
///	\code #include "IspMaster.h" \endcode
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
#include <avr/version.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <bool.h>

#include "config.h"

// #include "defports.h"
// #include "bits.h"

#include "hardware/led/led_core.h"
#include "hardware/spi/spi_core.h"
#include "hardware/spi/spi_2.h"

#include "system/net/ip.h"
#include "system/net/tcp.h"
#include "system/net/ethernet.h"
#include "system/net/dns.h"
#include "system/thread/thread.h"
#include "system/config/eeconfig.h"
#include "system/clock/clock.h"
#include "system/clock/delay_x.h"
//#include "system/softreset/softreset.h"

#include "apps/httpd/cgibin/cgi-bin.h"
#include "apps/httpd/httpd2_pharse.h"

#include "hardware/timer0/timer0.h"

#include "CgiFormTools.h"
//#include "iTelex.h"
//#include "TlnBuch.h"
//#include "BusKomm.h"
//#include "TxP2-Defs.h"
//#include "FifoPuffer.h"
//#include "BaudotCode.h"
#include "Protokoll.h"
//#include "TlnServer.h"
//#include "eMail.h"
//#include "SvnVersion.h"
//#include "StringTab.h"
//#include "ConfigNtp.h"
#include "IspMaster.h"


#ifdef ISP_MASTER

//! Rudimentärer Anfang eines ISP-Programmier-Master
//--------------------------------------------------
//! Erste realisierte Funktion: Ein Block des Flash-Rom auslesen und Protokollieren.
//! Anschluss über SPI 2, dieser ist auf Port F (eigentlich JTAG) geschaltet.
// Atmel - JTAG - ISP - Slave
//  PF5  -   5  -  5  - Reset
//  PF6  -   3  -  7  - SCK
//  PF7  -   9  -  9  - MISO
//  PF4  -   1  -  1  - MOSI
//  GND  -   2  -  4  - GND
//  GND  -  10  - 10  - GND
//  VCC  -   4  -  2  - VCC
//  VCC  -   7  -  3  - nc (LED)
//   nc  -   6  -  6  - GND
//   nc  -   8  -  8  - GND
// --> Das Kabel muss also an einem Ende Adern 2 und 4 drehen, am anderen Ende Adern 3 und 7


#define IspSpiPort 2


typedef enum {
	IspError_NoError = 0,
	IspError_EnableFailed = 1,
	} TIspError;
	
	
static TIspError IspErrorID;


static bool IspEnable()
	{
	uint8_t ProgEnabCheck;
	
	clro_IspResetOut();
	
	_delay_ms(25);

	for (uint8_t i = 0 ; i < 32 ; i++)
		{
		// Programming enable:
		SPI_ReadWrite(IspSpiPort, 0xAC);
		SPI_ReadWrite(IspSpiPort, 0x53);
		ProgEnabCheck = SPI_ReadWrite(IspSpiPort, 0x00);
		SPI_ReadWrite(IspSpiPort, 0x00);

		//DEBUG: printf_P(PSTR("Program Enable Echo %d was 0x%02X (should be 0x53)<p>"), i, ProgEnabCheck);
		
		if (ProgEnabCheck == 0x53)
			{
			IspErrorID = IspError_NoError;
			return true;
			}
			
		// einen Extra Taktimpuls zum Synchronisieren
		_delay_us(100);
		
		#if (IspSpiPort == 2)
			// SCK auf High setzen
			SPI2_PORT |= ( 1<<SCK2 );
			_delay_us(100);
			
			// SCK wieder auf low
			SPI2_PORT &= ~( 1<<SCK2 );
		#else
			#error Nur fuer SPI-Port 2 definiert
		#endif
		
		_delay_us(100);
		
		}
	
	// hierher kommt man nur wenn keine Verbindung besteht
	
	IspErrorID = IspError_EnableFailed;
	
	inp_IspResetOut();
	
	_delay_ms(25);
	
	return false;
	}
	
	
static bool FlashRead(uint16_t Addr, uint8_t *Val)
	{
	SPI_ReadWrite(IspSpiPort, (Addr & 1) ? 0x28 : 0x20);
	SPI_ReadWrite(IspSpiPort, (Addr >> 9));
	SPI_ReadWrite(IspSpiPort, (Addr >> 1) & 0xFF);
	*Val = SPI_ReadWrite(IspSpiPort, 0x00);
	return true;
	}
	
	
static bool SigRead(uint8_t Addr, uint8_t *Val)	
	{
	SPI_ReadWrite(IspSpiPort, 0x30);
	SPI_ReadWrite(IspSpiPort, 0x00);
	SPI_ReadWrite(IspSpiPort, Addr);
	*Val = SPI_ReadWrite(IspSpiPort, 0x00);
	return true;
	}

	
static void IspClose()
	{
	inp_IspResetOut();
	}
	
	
void cgi_FlashReadTest(void *pStruct)
	{
	cgi_PrintHttpheaderStart();
	
	_delay_ms(25);

	if (IspEnable())
		{
		for (uint8_t i = 0 ; i < 32 ; i++)
			{
			uint8_t x;
			if (FlashRead(i, &x))
				printf_P(PSTR("FLASH(0x%02X) = 0x%02X<br>"), i, x);
			else
				break;
			}
		IspClose();
		}
	
	cgi_PrintHttpheaderEnd();

	}


void InitIspMaster()
	{
	init_IspResetOut();
	SPI_init(IspSpiPort);
	
	cgi_RegisterCGI( cgi_FlashReadTest, PSTR("isptest.cgi"));
	}
	
	
#endif //def ISP_MASTER
