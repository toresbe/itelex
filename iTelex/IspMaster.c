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
	
	
static bool ChipErase()
	{
	SPI_ReadWrite(IspSpiPort, 0xAC);
	SPI_ReadWrite(IspSpiPort, 0x80);
	SPI_ReadWrite(IspSpiPort, 0x00);
	SPI_ReadWrite(IspSpiPort, 0x00);
	return true;
	}
	
	
static bool FlashWriteBlock64(uint16_t Addr, uint8_t *Data)
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
	
	
//! Testfunktion in der Debugging-Phase
//-------------------------------------
//! Wird im finalen Code nicht mehr gebraucht.
	
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
	
	
//! Testfunktion in der Debugging-Phase
//-------------------------------------
//! Wird im finalen Code nicht mehr gebraucht.
	
static void DebugTestReadFlashIdentity()
	{
	char Ident[MaxIdentLen];
	if (ReadFlashIdentity(Ident))
		printf_P(PSTR("Identity found: &lt;%s&gt;<br>"), Ident);
	else
		printf_P(PSTR("Identity not found, error %d<br>"), IspErrorID);
	}
	


// Funktionen zum Empfang von Daten per HTTP
// =========================================
// bei Gelegenheit mal in andere Bibliothek auslagern

//! Liest den HTTP Header komplett ein
//------------------------------------
//! \retval Code der Statusmeldung, meist 200 = OK, oder -1 bei Kommunikationsfehler
//!

int16_t HttpReadHeader(int SocketID)
	{
	char LineBuf[64];
	uint8_t i;
	bool Flag;
	uint8_t Pos;
	uint16_t Res;
	
	Res = 0;
	while (true)
		{
		// erstmal eine Zeile in den Puffer lesen
		i = 0;
		while (GetBytesInSocketData(SocketID) > 0)
			{
			if (GetSocketData(SocketID, 1, InBuf + i) != 1)
				return -1;
			if (InBuf[i] == '\n') 
				break;
			else if (InBuf[i] != '\r' && i < sizeof(InBuf) - 1)
				i++;
			}
		InBuf[i] = '\0';
		
		// Zeile gelesen und jetzt auswerten. Zuerst das zweite Wort finden.
		Flag = false;
		for (Pos = 0 ; InBuf[Pos] != '\0' ; Pos++)
			{
			if (InBuf[Pos] == ' ')
				Flag = true;
			else if (Flag) 
				break;
			}
		// jetzt zeigt Pos auf das zweite Wort
		
		if (Pos == 0)
			break; // das war eine Leerzeile, die beendet den Header
		
		// abhängig vom Zeilenanfang eine Auswertung durchführen
		if (strncmp_P(InBuf, PSTR("HTTP/"), 5) == 0)
			Res = atoi(InBuf + Pos); // der Ergebniscode
			
		// else... jetzt könnte man noch andere Rückmeldungen auswerten
		} // while (true) ... Schleife über alle Zeilen des Header
		
	return Res;
	} // HttpReadHeader()
	
	

static int DebugTestGetFilePerHttp()
	{
	struct STDOUT oldstream;
	char FileName[] = "lochstreifen-hg.png";
	int SocketID;
	long FileIP;
	char InBuf[100];
	TKurzTimer AbbruchTimer;
	
	// FileIP = DNS_ResolveName_P(PSTR("sonnibs.no-ip.org"));
	
	FileIP = IPDOT(192l,168l,178l,38l); // die andere Karte
	
	if (FileIP == -1)
		{
		Protokollieren_P(PSTR("FileGet: IP not found.\r\n"));
		return 1;
		}
		
	SocketID = Connect2IP(FileIP, 80); // HTTP_PORT
	if (SocketID == SOCKET_ERROR)
		{
		Protokollieren_P(PSTR("FileGet: Connect failed.\r\n"));
		return 2;
		}
	
	// STDOUT umbiegen auf die neue Verbingung und alt STDOUT sichern
	STDOUT_save(&oldstream);
	STDOUT_set(_TCP, SocketID);
	
	printf_P(PSTR("GET /%s HTTP/1.0\r\nUser-Agent: Wget/1.11.4\r\nAccept: */*\r\nHost: "), FileName);
	printf_P(PSTR("sonnibs.no-ip.org"));
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
	
#endif // 0


// Hier kommen die Binärdaten...
// =============================

#define byte uint8_t // nur für die Binärdaten
#define unsigned PROGMEM // nur für die Binärdaten

#include "ProgBinData\AnalogModem.h"
//#include "ProgBinData\ED1000.h"
#include "ProgBinData\FernschrTW39.h"
//#include "ProgBinData\Messgeraet.h"
//#include "ProgBinData\SeriellUndSpeicher.h"

#undef unsigned
#undef byte

//HACK:


typedef struct 
	{
	const prog_char *Name;
	const prog_char *Kennung;
	const uint8_t FuseL, FuseH, FuseX;
	const prog_uint8_t *ProgBin;
	const uint16_t ProgSize;
	} TProgDaten;
	
	
const char AnalogModemBez[] PROGMEM = "AnalogModem";
const char AnalogModemKenn[] PROGMEM = "TxP2_LeitungAnalog2";
const char ED1000Bez[] PROGMEM = "ED1000";
const char ED1000Kenn[] PROGMEM = "TxP2_ED1000";
const char FernschrTW39Bez[] PROGMEM = "Fs TW39";
const char FernschrTW39Kenn[] PROGMEM = "TxP2_";
const char MessgeraetBez[] PROGMEM = "Messgeraet";
const char MessgeraetKenn[] PROGMEM = "TxP2_";
const char SeriellUndSpeicherBez[] PROGMEM = "SeriellUndSpeicher";
const char SeriellUndSpeicherKenn[] PROGMEM = "TxP2_";


TProgDaten ProgDatenTab[] =	{
	{ AnalogModemBez, AnalogModemKenn, 0xE0, 0xD5, 0xF9, AnalogModem, sizeof(AnalogModem) } ,
//	{ ED1000Bez, ED1000Kenn, 0xF7, 0xD5, 0xF9, ED1000, sizeof(ED1000) } ,
	{ FernschrTW39Bez, FernschrTW39Kenn, 0xBF, 0xD1, 0xFF, FernschrTW39, sizeof(FernschrTW39) } ,
//	{ MessgeraetBez, MessgeraetKenn, 0xF7, 0xD5, 0xF9, Messgeraet , sizeof(Messgeraet) } ,
//	{ SeriellUndSpeicherBez, SeriellUndSpeicherKenn, 0xF7, 0xD5, 0xF9, SeriellUndSpeicher, sizeof(SeriellUndSpeicher) } ,
	} ;


#define ProgDatenTabAnzahl (sizeof(ProgDatenTab) / sizeof(TProgDaten))


// Hier folgenden die Hauptfunktionen
// ===========================================

char IspDiagnoseText[200];


void ProgrammiereFlashAusProgSpeicher(const prog_uint8_t *Data, uint16_t Size)
	{
	uint16_t start, pos;
	uint8_t Buf[64];
	uint8_t readback;

	if (!IspEnable())
		{
		strcpy_P(IspDiagnoseText, PSTR("IspEnable failed"));
		return;
		}
	
	if (!ChipErase())
		{
		IspClose();
		strcpy_P(IspDiagnoseText, PSTR("ChipErase failed"));
		return;
		}
	
	for (start = 0 ; start < Size ; start += 64)
		{
		for (pos = 0 ; pos < 64 && start + pos < Size ; pos++)
			Buf[pos] = pgm_read_byte(Data + start + pos);

		if (!FlashWriteBlock64(start, Buf))
			{
			IspClose();
			strcpy_P(IspDiagnoseText, PSTR("FlashWriteBlock64 failed"));
			return;
			}
			
		for (pos = 0 ; pos < 64 && start + pos < Size ; pos++)
			{
			if (!FlashRead(start + pos, &readback))
				{
				IspClose();
				strcpy_P(IspDiagnoseText, PSTR("FlashRead failed"));
				return;
				}
			if (readback != Buf[pos])
				{
				IspClose();
				sprintf_P(IspDiagnoseText, PSTR("Verify failed: %ld is %d should be %d"), start + pos, readback, Buf[pos]);
				return;
				}
			}
		}
		
	IspClose();
	strcpy_P(IspDiagnoseText, PSTR("flash program success"));
	}
		
	
// Benutzerschnittstelle für das Brennen
// ===========================================
	
void cgi_Isp(void *pStruct)
	{
	static TSprache Sprache;
	uint8_t i;

	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;

	if (!PruefeSpracheUndKonfigFreigabe(pStruct))
		return;

	if (http_request->argc == 0)
		{ // Standard-Aufruf
		cgi_PrintHttpheaderStart();
		if (IspDiagnoseText[0] != '\0')
			{
			printf_P(PSTR("Result of last operation: %s <p>"), IspDiagnoseText);
			}
		
		printf_P(PSTR("<h1>Before start of any programming action connect target board by special cable</h1><p>"));
		printf_P(PSTR("<big><a href=\"isp.cgi?autoprog\">Automatic</a> identification and update<p></big>"));
		for (i = 0 ; i < ProgDatenTabAnzahl ; i++)
			{
			printf_P(PSTR("Initial programming of board <a href=\"isp.cgi?progid=%d\">"), i);
			printf_P(ProgDatenTab[i].Name);
			printf_P(PSTR("</a><br>"));
			}
		cgi_PrintHttpheaderEnd();
		} // if (http_request->argc == 0)
		
	else if (PharseCheckName_P(http_request, PSTR("autoprog")))
		{ // identifizieren des angeschlossenen Boards und automatische Auswahl des hochzuladenden Programms
		cgi_PrintHttpheaderStart();

		// Test der Kennungs-Auslesung:
		
		// DebugTestReadFlashIdentity(); funktioniert, daher ausgeblendet...
		
		//! \todo identifizieren und auswählen

		cgi_PrintHttpheaderEnd();
		
		// HACK Testfunktion:
		STDOUT_Flush();
		CloseTCPSocket(http_request->HTTP_SOCKET); // ist erfoderlich, damit erstmal die Meldung erscheint.
		
		LED_on(1); // gelb
		
		DebugTestGetFilePerHttp();
		
		LED_off(1); // gelb
		
		} // if (PharseCheckName_P(http_request, PSTR("autoprog")))
		
	else if (PharseCheckName_P(http_request, PSTR("progid")))
		{ // konkreten Typ ausgewählt
		i = atoi(http_request->argvalue[PharseGetValue_P(http_request, PSTR("progid"))]);
		cgi_PrintHttpheaderStart();
		if (i >= ProgDatenTabAnzahl)
			{
			printf_P(PSTR("Error: invalid progid. Click <a href=\"isp.cgi\">here</a> to continue."));
			cgi_PrintHttpheaderEnd();
			}
		else
			{
			printf_P(PSTR("programming board "));
			printf_P(ProgDatenTab[i].Name);
			printf_P(PSTR("<p>Click <a href=\"isp.cgi\">here</a> after red and yellow LED went off again."));
			cgi_PrintHttpheaderEnd();
			STDOUT_Flush();
			CloseTCPSocket(http_request->HTTP_SOCKET); // ist erfoderlich, damit erstmal die Meldung erscheint.
			
			LED_on(1); // gelb
			
			ProgrammiereFlashAusProgSpeicher(ProgDatenTab[i].ProgBin, ProgDatenTab[i].ProgSize);
			
			LED_off(1); // gelb
			}
		} // if (PharseCheckName_P(http_request, PSTR("progid")))
			
	}


void InitIspMaster()
	{
	init_IspResetOut();
	SPI_init(IspSpiPort);
	
	IspDiagnoseText[0] = '\0';
	cgi_RegisterCGI( cgi_Isp, PSTR("isp.cgi"));
	}
	
	
#endif //def ISP_MASTER
