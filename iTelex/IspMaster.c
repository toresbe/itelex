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
#include "system/stdout/stdout.h"
//#include "system/softreset/softreset.h"

#include "apps/httpd/cgibin/cgi-bin.h"
#include "apps/httpd/httpd2_pharse.h"

#include "hardware/timer0/timer0.h"

#include "CgiFormTools.h"
#include "iTelex.h"
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


// Funktionen für In System Programming (ISP) über den SPI-Bus.
// ============================================================

typedef enum {
	IspError_NoError = 0,
	IspError_EnableFailed = 1,
	IspError_NotFound = 2,
	} TIspError;
	
	
static TIspError IspErrorID;


static void IspClose()
	{
	inp_IspResetOut();
	//! \todo: SPI auf Input setzen
	_delay_ms(25);
	}
	

static bool IspEnable()
	{
	uint8_t ProgEnabCheck;

	_delay_ms(25);

	//! \todo SPI auf Output setzen
	
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
	
	IspClose();

	IspErrorID = IspError_EnableFailed;
	
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


static bool FuseRead(uint8_t Addr, uint8_t *Val)
	{
	SPI_ReadWrite(IspSpiPort, (Addr == 1) ? 0x58 : 0x50);
	SPI_ReadWrite(IspSpiPort, (Addr == 0) ? 0x00 : 0x08);
	SPI_ReadWrite(IspSpiPort, 0x00);
	*Val = SPI_ReadWrite(IspSpiPort, 0x00);
	return true;
	}
	
	
static bool FlashWriteBlock(uint16_t Addr, uint8_t *Data)
// writes 64 Bytes into Flash.
// Addr must be a multiple of 64
	{
	for (uint8_t i = 0 ; i < 64 ; i++)
		{
		SPI_ReadWrite(IspSpiPort, (i & 1) ? 0x48 : 0x40);
		SPI_ReadWrite(IspSpiPort, (Addr >> 9));
		SPI_ReadWrite(IspSpiPort, ((Addr + i) >> 1) & 0xFF);
		SPI_ReadWrite(IspSpiPort, Data[i]);
		}
		
	SPI_ReadWrite(IspSpiPort, 0x4C);
	SPI_ReadWrite(IspSpiPort, (Addr >> 9));
	SPI_ReadWrite(IspSpiPort, (Addr >> 1) & 0xFF);
	SPI_ReadWrite(IspSpiPort, 0x00);

	_delay_ms(10);
	// statt pollen einfach warten
	
	return true; // verify???
	}
	
	
static bool FuseWrite(uint8_t Addr, uint8_t Data)
	{
	SPI_ReadWrite(IspSpiPort, 0xAC);
	if (Addr == 0)
		SPI_ReadWrite(IspSpiPort, 0xA0);
	else if (Addr == 1)
		SPI_ReadWrite(IspSpiPort, 0xA8);
	else
		SPI_ReadWrite(IspSpiPort, 0xA4);
	
	SPI_ReadWrite(IspSpiPort, 0x00);
	SPI_ReadWrite(IspSpiPort, Data);

	_delay_ms(10);
	// statt pollen einfach warten
	
	return true; // verify???
	}
	

	
// Auswertungsfunktionen
// =====================


enum { MaxIdentLen = 20 };


static bool ReadFlashIdentity(char *Ident)
// Spezialität von meinen Programmen: Im Flash steht eine Identifikation.
// Sie besteht aus einem maximal 20 Zeichen langen Text eingebettet zwischen
// je mindestens drei aufeinanderfolgenden Unterstrichen ( _ )
	{
	enum { MaxIdentPos = 8192 } ; // in den ersten 8 k des Programms
	uint8_t UnterstrichZaehl;
	uint8_t IdentI; // Zeiger in Ident-String, wenn 255, dann noch nicht begonnen.
	uint16_t FlashAddr; // Zeiger in Flash-Speicher des Bausteins
	
	if (!IspEnable())
		return false;
	
	UnterstrichZaehl = 0;
	IdentI = 255; // noch nicht begonnen
	
	for (FlashAddr = 0 ; FlashAddr < MaxIdentPos ; FlashAddr++)
		{
		uint8_t b;
		if (!FlashRead(FlashAddr, &b))
			{
			IspClose();
			return false;
			}
			
		// Zeichen abspeichern:
		if (IdentI < MaxIdentLen - 1) // impliziert auch != 255
			Ident[IdentI++] = b;
			
		if (b == '_')
			{
			if (IdentI == 1)
				IdentI = 0; 
				// dies entfernt überschüssige _ , falls der Ident-String mit mehr als 
				// drei _ merkiert ist.
			else
				UnterstrichZaehl++; 
				// nur Zählen, wenn nicht weiterhin die Einleitung des Ident-String
				
			if (UnterstrichZaehl >= 3)
				{
				if (IdentI == 255)
					{ // es wurden die einleitenden 3x _ gefunden
					IdentI = 0;
					UnterstrichZaehl = 0;
					}
				else
					{ // jetzt wurde der abschließende 3x _ gefunden
					if (IdentI > UnterstrichZaehl)
						IdentI -= UnterstrichZaehl;
					Ident[IdentI] = '\0'; // Abschluss speichern
					IspClose();
					return true;
					}
				} // if UnterstrichZaehl >= 3
			} // if b == '_'

		else if (b >= ' ' && b <= '~')
			{ // b != '_' aber lesbares Zeichen
			UnterstrichZaehl = 0;
			if (IdentI != 255 && IdentI >= MaxIdentLen - 1)
				IdentI = 255; // es war ein Trugschluss 
			} // else b != '_'

		else
			{ // nicht druckbares Zeichen
			IdentI = 255;
			UnterstrichZaehl = 0;
			}
		} // for (FlashAddr)
	
	Ident[0] = '\0'; // nichts gefunden
	IspClose();
	return false;
	}
	
	
static void DebugTestReadFunctions()
	{
	if (IspEnable())
		{
		uint8_t x;	
		for (uint8_t i = 0 ; i < 20 ; i++)
			if (FlashRead(i, &x))
				printf_P(PSTR("Flash byte %d = 0x%02X<br>"), i, x);
				
		for (uint8_t i = 0 ; i < 3 ; i++)
			if (SigRead(i, &x))
				printf_P(PSTR("Signature byte %d = 0x%02X<br>"), i, x);

		for (uint8_t i = 0 ; i < 3 ; i++)
			if (FuseRead(i, &x))
				printf_P(PSTR("Fuse byte %d = 0x%02X<br>"), i, x);
		IspClose();
		}
	}
	
	
static void DebugTestReadFlashIdentity()
	{
	char Ident[MaxIdentLen];
	if (ReadFlashIdentity(Ident))
		printf_P(PSTR("Identity found: &lt;%s&gt;<br>"), Ident);
	else
		printf_P(PSTR("Identity not found, error %d<br>"), IspErrorID);
	}
	

const PROGMEM char HostUrl[] = "df3oe.no-ip.org";
	

static int DebugTestGetFilePerHttp()
	{
	struct STDOUT oldstream;
	char FileName[] = "mainmenu.html";
	int SocketID;
	long FileIP;
	char InBuf[100];
	TKurzTimer AbbruchTimer;
	
	FileIP = DNS_ResolveName_P(HostUrl);
	if (FileIP == -1)
		return 1;
	SocketID = Connect2IP(FileIP, 80); // HTTP_PORT
	if (SocketID == SOCKET_ERROR)
		return 2;
	
	// STDOUT umbiegen auf die neue Verbingung und alt STDOUT sichern
	STDOUT_save(&oldstream);
	STDOUT_set(_TCP, SocketID);
	
	printf_P(PSTR("GET /%s HTTP/1.0\r\nUser-Agent: Wget/1.11.4\r\nAccept: */*\r\nHost: "), FileName);
	printf_P(HostUrl);
	printf_P(PSTR("\r\n\r\n")); 

	STDOUT_restore(&oldstream);

	StartKurzTimer(&AbbruchTimer);
	
	while (true)
		{
		if (CheckSocketState(SocketID) == SOCKET_NOT_USE)
			{
			Protokollieren_P(PSTR("FileGet: Beendigung durch Gegenstelle.\r\n"));
			break;
			}
			
		if (KurzTimerVal(&AbbruchTimer) > 5 * KurzTimerFreq)
			{
			Protokollieren_P(PSTR("FileGet: Timeout beim Empfang.\r\n"));
			break;
			}
		
		int InCount = GetBytesInSocketData(SocketID);
		
		if (InCount >= sizeof(InBuf))
			{
			InCount = sizeof(InBuf) - 1;
			}
			
		if (InCount > 0) 
			{
			int Res = GetSocketData(SocketID, InCount, InBuf);
			
			ProtokollierenInt_P(PSTR("FileGet Empfang: (%d/" ), InCount);
			ProtokollierenInt_P(PSTR("%d)"), Res);
			if (Res > 0)
				ProtokollierenPuffer(InBuf, Res);
			Protokollieren_P(PSTR("\r\n"));

			// Todo hier eine künstliche Bremse...
			
			StartKurzTimer(&AbbruchTimer);
			}		
				
		} // while KurzTimerVal(&AbbruchTimer) < 5 * KurzTimerFreq
		
	CloseTCPSocket(SocketID);

	return 0;
	}
	
	
void cgi_IspTest(void *pStruct)
	{
	cgi_PrintHttpheaderStart();
	
	// hier nur Tests...
	DebugTestGetFilePerHttp();
	
	cgi_PrintHttpheaderEnd();
	}


void InitIspMaster()
	{
	init_IspResetOut();
	SPI_init(IspSpiPort);
	
	cgi_RegisterCGI( cgi_IspTest, PSTR("isptest.cgi"));
	}
	
	
#endif //def ISP_MASTER
