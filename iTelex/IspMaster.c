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
	_delay_ms(20);
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


//! Liest eine im Flash des Target abgelegte Identifikation aus.
//--------------------------------------------------------------
//! Die Identifikation besteht aus einem maximal 20 Zeichen langen 
//! Text eingebettet zwischen je mindestens drei aufeinanderfolgenden 
//! Unterstrichen ( _ ) .
//! \return Lesevorgang erfolgreich. Auch bei nicht gefundener 
//! Identifikation wird true zurückgegeben.
//! \param Ident: Rückgabe der gefundenen Identifikation.

static bool ReadFlashIdentity(char *Ident)
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
	return true;
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


//! Startet eine HTTP-Get-Anfrage
//-------------------------------
//! \return Nummer des Socket oder -1 für IP nicht gefunden oder -2 für Verbindungsfehler oder -3 für fehlenden / im Pfad.
int HttpGet(char *Pfad)
	{
	long ServerIP;
	int SocketID;
	char *FileStart;
	
	FileStart = strchr(Pfad, '/');
	if (FileStart == NULL)
		{
		Protokollieren_P(PSTR("HttpGet: invalid path.\r\n"));
		return -3;
		}
			
	*FileStart = '\0'; 
		
	ServerIP = strtoip(Pfad);	// Annahme: eine IP-Adresse angegeben
	
	if (ServerIP == 0) // ist es doch eine Hostname?
		ServerIP = DNS_ResolveName(Pfad); 

	if (ServerIP == -1)
		{
		Protokollieren_P(PSTR("HttpGet: IP not found.\r\n"));
		*FileStart = '/';
		return -1;
		}
		
	SocketID = Connect2IP(ServerIP, 80); // HTTP_PORT
	if (SocketID == SOCKET_ERROR)
		{
		Protokollieren_P(PSTR("HttpGet: Connect failed.\r\n"));
		*FileStart = '/';
		return -2;
		}
	
	struct STDOUT oldstream;
	// STDOUT umbiegen auf die neue Verbingung und alt STDOUT sichern

	STDOUT_save(&oldstream);
	STDOUT_set(_TCP, SocketID);
	
	printf_P(PSTR("GET /%s HTTP/1.0\r\nUser-Agent: Wget/1.11.4\r\nAccept: */*\r\nHost: "), FileStart + 1);
	printf(Pfad);
	printf_P(PSTR("\r\n\r\n")); 

	STDOUT_restore(&oldstream);

	*FileStart = '/';
	
	return SocketID;
	
	// als nächstes sollte HttpReadHeader aufgerufen werden.
	
	} // HttpGet()
	
	
//! Liest den HTTP Header komplett ein
//------------------------------------
//! \retval Code der Statusmeldung, meist 200 = OK, oder -1 bei Timeout oder -2 bei Kommunikationsfehler

int16_t HttpReadHeader(int SocketID)
	{
	char LineBuf[64];
	uint8_t i;
	bool Flag;
	uint8_t Pos;
	uint16_t Res;
	TKurzTimer AbbruchTimer;
	
	Res = 0;
	
	while (true)
		{
		// erstmal eine Zeile in den Puffer lesen
		i = 0;
		StartKurzTimer(&AbbruchTimer);
		while (true)
			{
			if (GetBytesInSocketData(SocketID) > 0)
				{
				StartKurzTimer(&AbbruchTimer);
				if (GetSocketData(SocketID, 1, LineBuf + i) != 1)
					{
					if (ProtokollLevel >= NurFehler) // Daten explizit
						Protokollieren_P(PSTR("HttpReadHeader: GetSocketData() Fehler\r\n"));
					return -2;
					}
				else if (LineBuf[i] == '\n') 
					break;
				else if (LineBuf[i] != '\r' && i < sizeof(LineBuf) - 1)
					i++;
				}
			else if (KurzTimerVal(&AbbruchTimer) > 10 * KurzTimerFreq)
				{
				if (ProtokollLevel >= NurFehler) // Daten explizit
					Protokollieren_P(PSTR("HttpReadHeader: Timeout\r\n"));
				return -1;
				}
			}
		LineBuf[i] = '\0';

		// Für Debugging:
		if (ProtokollLevel >= DatenDetailliert) // Daten explizit
			{
			Protokollieren_P(PSTR("HttpReadHeader: Zeile >>"));
			ProtokollierenPuffer(LineBuf, i); // i enthält immer noch Zeilenlänge
			Protokollieren_P(PSTR("<< verarbeitet.\r\n"));
			}
		
		// Zeile gelesen und jetzt auswerten. Zuerst das zweite Wort finden.
		Flag = false;
		for (Pos = 0 ; LineBuf[Pos] != '\0' ; Pos++)
			{
			if (LineBuf[Pos] == ' ')
				Flag = true;
			else if (Flag) 
				break;
			}
		// jetzt zeigt Pos auf das zweite Wort
		
		if (Pos == 0)
			break; // das war eine Leerzeile, die beendet den Header
		
		// abhängig vom Zeilenanfang eine Auswertung durchführen
		if (strncmp_P(LineBuf, PSTR("HTTP/"), 5) == 0)
			Res = atoi(LineBuf + Pos); // der Ergebniscode
			
		// else... jetzt könnte man noch andere Rückmeldungen auswerten
		} // while (true) ... Schleife über alle Zeilen des Header

	if (ProtokollLevel >= AblaufInfo) // Daten explizit
		ProtokollierenInt_P(PSTR("HttpReadHeader: Res = %d\r\n"), Res);

	return Res;
	} // HttpReadHeader()
	
	

static int DebugTestGetFilePerHttp(char *Filename)
	{
	int SocketID;
	char InBuf[100];
	TKurzTimer AbbruchTimer;
	int16_t HttpHeadCode;
	uint16_t GesamtBytes;
	
	SocketID = HttpGet(Filename);
	if (SocketID < 0)
		return -1;
		
	HttpHeadCode = HttpReadHeader(SocketID);
	ProtokollierenInt_P(PSTR("Header Code: %d\r\n"), HttpHeadCode);
	
	StartKurzTimer(&AbbruchTimer);
	
	GesamtBytes = 0;
	
	while (true)
		{
		int InCount = GetBytesInSocketData(SocketID);

		if (KurzTimerVal(&AbbruchTimer) > 5 * KurzTimerFreq)
			{
			Protokollieren_P(PSTR("FileGet: Timeout beim Empfang.\r\n"));
			break;
			}
		
		else if (InCount > 0)
			{
			int Res = GetSocketData(SocketID, (InCount > sizeof(InBuf)) ? sizeof(InBuf) : InCount, InBuf);
			
			ProtokollierenInt_P(PSTR("FileGet Empfang: (%d/" ), InCount);
			ProtokollierenInt_P(PSTR("%d)"), Res);
//			if (Res > 0)
//				ProtokollierenPuffer(InBuf, Res);
			Protokollieren_P(PSTR("\r\n"));
			ProtokollierenInt_P(PSTR("CheckSocketState() = %d\r\n" ), CheckSocketState(SocketID));

			GesamtBytes += Res;
			
			// Todo hier eine künstliche Bremse...
				
			StartKurzTimer(&AbbruchTimer);
			}		
				
		else if (CheckSocketState(SocketID) == SOCKET_NOT_USE && KurzTimerVal(&AbbruchTimer) > 2 * KurzTimerFreq)
			{
			Protokollieren_P(PSTR("FileGet: Beendigung durch Gegenstelle.\r\n"));
			break;
			}
			
		} // while (true)

	ProtokollierenInt_P(PSTR("GesamtBytes = %d\r\n" ), GesamtBytes);
		
	CloseTCPSocket(SocketID);

	return 0;
	} // DebugTestGetFilePerHttp()
	

typedef struct 
	{
	const prog_char *Name;
	const prog_char *Kennung;
	const uint8_t FuseL, FuseH, FuseX;
	const prog_char *BinFilename;
	} TProgDaten;
	
	
const PROGMEM char AutomatikBez[] = "Auto detection";
const char AnalogModemBez[] PROGMEM = "AnalogModem";
const char AnalogModemKenn[] PROGMEM = "TxP2_LeitungAnalog2";
const char AnalogModemFile[] PROGMEM = "AnalogModem2.bin";
const char ED1000Bez[] PROGMEM = "ED1000";
const char ED1000Kenn[] PROGMEM = "TxP2_ED1000";
const char ED1000File[] PROGMEM = "ED1000.bin";
const char FernschrTW39Bez[] PROGMEM = "Fs TW39";
const char FernschrTW39Kenn[] PROGMEM = "TxP2_TW39";
const char FernschrTW39File[] PROGMEM = "FernschrTW39.bin";
const char MessgeraetBez[] PROGMEM = "Messgeraet";
const char MessgeraetKenn[] PROGMEM = "TxP2_Messgeraet";
const char MessgeraetFile[] PROGMEM = "Messgeraet.bin";
const char SeriellUndSpeicherBez[] PROGMEM = "SeriellUndSpeicher";
const char SeriellUndSpeicherKenn[] PROGMEM = "TxP2_SeriellUndSpeicher";
const char SeriellUndSpeicherFile[] PROGMEM = "SeriellUndSpeicher.bin";


const char *ProgWahlTab[] = { AutomatikBez, AnalogModemBez, ED1000Bez, FernschrTW39Bez, MessgeraetBez, SeriellUndSpeicherBez } ;

#define ProgWahlTabAnzahl (sizeof(ProgWahlTab) / sizeof(ProgWahlTab[0]))

TProgDaten ProgDatenTab[] =	{
	{ AnalogModemBez, AnalogModemKenn, 0xE0, 0xD5, 0xF9, AnalogModemFile } ,
	{ ED1000Bez, ED1000Kenn, 0xF7, 0xD5, 0xF9, ED1000File } ,
	{ FernschrTW39Bez, FernschrTW39Kenn, 0xBF, 0xD1, 0xFF, FernschrTW39File } ,
	{ MessgeraetBez, MessgeraetKenn, 0xF7, 0xD5, 0xF9, MessgeraetFile } ,
	{ SeriellUndSpeicherBez, SeriellUndSpeicherKenn, 0xF7, 0xD5, 0xF9, SeriellUndSpeicherFile } ,
	} ;


#define ProgDatenTabAnzahl (sizeof(ProgDatenTab) / sizeof(TProgDaten))


// Hier folgenden die Hauptfunktionen
// ===========================================

char IspDiagnoseText[200];


const PROGMEM char BinServerPath_P[] = "servpath";
const PROGMEM char ProgID_P[] = "progid";
const PROGMEM char ClickToContinue_P[] = "Click <a href=\"isp.cgi\">here</a> to continue.";


//! Startet den Programmiervorgang entsprechend der Daten aus #ProgDatenTab.
//--------------------------------------------------------------------------
//! \todo fuses brennen
	
void ProgrammiereVordefiniert(uint8_t index, struct HTTP_REQUEST * http_request)
	{
	char FullPath[200];
	int SocketID;
	int Res;

	cgi_PrintHttpheaderStart();
	
	IspDiagnoseText[0] = '\0';
	
	if (index >= ProgDatenTabAnzahl)
		{
		printf_P(PSTR("Error: invalid progid. "));
		printf_P(ClickToContinue_P);
		cgi_PrintHttpheaderEnd();
		return;
		}
	
	if (!PharseCheckName_P(http_request, BinServerPath_P))
		{
		printf_P(PSTR("Error: no server path specified. "));
		printf_P(ClickToContinue_P);
		cgi_PrintHttpheaderEnd();
		return;
		}
		
	strcpy(FullPath, http_request->argvalue[PharseGetValue_P(http_request, BinServerPath_P)]);
	strcat_P(FullPath, PSTR("/"));
	strcat_P(FullPath, ProgDatenTab[index].BinFilename);
	
	printf_P(PSTR("programming board "));
	printf_P(ProgDatenTab[index].Name);
	printf_P(PSTR("<p>Loading binary data from %s."), FullPath);

	SocketID = HttpGet(FullPath);
	if (SocketID < 0)
		{
		printf_P(PSTR("<big> &lt;-- open failed code %d.</big><p>"), SocketID);
		printf_P(ClickToContinue_P);
		cgi_PrintHttpheaderEnd();
		return;
		}

	Res = HttpReadHeader(SocketID);
	if (Res != 200)
		{
		CloseTCPSocket(SocketID);
		printf_P(PSTR("<big> &lt;-- http get failed statuscode %d.</big><p>"), Res);
		printf_P(ClickToContinue_P);
		cgi_PrintHttpheaderEnd();
		return;
		}

	if (!IspEnable())
		{
		CloseTCPSocket(SocketID);
		printf_P(PSTR("<p><big>ISP program enable failed. Check connection to target board.</big><p>"), SocketID);
		printf_P(ClickToContinue_P);
		cgi_PrintHttpheaderEnd();
		return;
		}
		
	if (!ChipErase())
		{
		IspClose();
		CloseTCPSocket(SocketID);
		printf_P(PSTR("<p><big>ISP chip erase failed.</big><p>"), SocketID);
		printf_P(ClickToContinue_P);
		cgi_PrintHttpheaderEnd();
		return;
		}
	
	// da der Rest Zeitkritisch ist wird der Bildschirmaufbau erstmal zuende gebracht.
	printf_P(PSTR("<p>Click <a href=\"isp.cgi\">here</a> after red and yellow LED went off again."));
	cgi_PrintHttpheaderEnd();
	STDOUT_Flush();
	CloseTCPSocket(http_request->HTTP_SOCKET); // ist erfoderlich, damit erstmal die Meldung erscheint.
	
	LED_on(1); // gelb
	
	uint16_t start, pos;
	uint8_t Buf[64];
	uint8_t i;
	uint8_t readback;
	bool beenden;
	TKurzTimer AbbruchTimer;
	
	start = 0;
	pos = 0;
	beenden = false;
	
	StartKurzTimer(&AbbruchTimer);
	while (!beenden)
		{
		int InCount = GetBytesInSocketData(SocketID);

		if ( /* (InCount == 0 && CheckSocketState(SocketID) > SOCKET_READY)
			|| */ (CheckSocketState(SocketID) == SOCKET_NOT_USE && KurzTimerVal(&AbbruchTimer) > 2 * KurzTimerFreq))
			{
			sprintf_P(IspDiagnoseText, PSTR("CheckSocketState() = %d. "), CheckSocketState(SocketID));
//			ProtokollierenInt_P(PSTR("CheckSocketState: %d "), CheckSocketState(SocketID));
			beenden = true;
			}
		
		if (KurzTimerVal(&AbbruchTimer) > 5 * KurzTimerFreq)
			{
			strcpy_P(IspDiagnoseText, PSTR("Socket timeout. "));
			beenden = true;
			}

		if (InCount > 0)
			{
			if (InCount > 64 - pos)
				InCount = 64 - pos;
			Res = GetSocketData(SocketID, InCount, Buf);

//			ProtokollierenInt_P(PSTR("GetSocketData: %d\r\n"), Res);
			
			if (Res != InCount)
				{
				strcpy_P(IspDiagnoseText, PSTR("GetSocketData Error. "));
				beenden = true;
				}
			if (Res > 0)
				pos = pos + Res;
			StartKurzTimer(&AbbruchTimer);
			}

		if (beenden || pos == 64)
			{
			if (!FlashWriteBlock64(start, Buf))
				{
				strcpy_P(IspDiagnoseText, PSTR("FlashWriteBlock64 failed. "));
				beenden = true;
				}

/*				
			for (i = 0 ; i < pos ; i++)
				{
				if (!FlashRead(start + i, &readback))
					{
					strcpy_P(IspDiagnoseText, PSTR("FlashRead failed."));
					beenden = true;
					break;
					}
				if (readback != Buf[i])
					{
					sprintf_P(IspDiagnoseText, PSTR("Verify failed: %04x is %02x should be %02x. "), start + i, readback, Buf[i]);
					beenden = true;
					break;
					}
				}
*/				
				
			start += pos; // pos ist meistens 64, außer beim Beenden.
			pos = 0;
			} // if (beenden || pos == 64)
			
		} // while (!beenden)
		
	IspClose();

	CloseTCPSocket(SocketID);

	sprintf_P(IspDiagnoseText + strlen(IspDiagnoseText), PSTR("%d bytes written to flash."), start);
	
	LED_off(1); // gelb
	
	} // ProgrammiereVordefiniert()
	

// Benutzerschnittstelle für das Brennen
// ===========================================

void cgi_Isp(void *pStruct)
	{
	static TSprache Sprache;
	uint8_t i;
	char BinServerPath[50]; 
	char Ident[50];

	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;

	if (!PruefeSpracheUndKonfigFreigabe(pStruct))
		return;

	if (http_request->argc == 0)
		{ // Standard-Aufruf
		strcpy_P(BinServerPath, PSTR("sonnibs.no-ip.org/ProgBinData")); // vorläufig, künftig Variable im EEPROM
		
		cgi_PrintHttpheaderStart();

		printf_P(PSTR("<h1>Before start of any programming action connect target board by special cable</h1><p>"));

		if (IspDiagnoseText[0] != '\0')
			printf_P(PSTR("Result of last operation: <big>%s</big><p>"), IspDiagnoseText);

		CgiFormStartTabbed_P(PSTR("isp.cgi"));

		CgiFormInputFieldText_P(PSTR("Path to server for binaries"), BinServerPath_P, sizeof(BinServerPath)-1, BinServerPath);

		CgiFormDropdown_P(PSTR("What to program"), ProgID_P, ProgWahlTabAnzahl, ProgWahlTab, 0);

		CgiFormFinish_P(PSTR("Start programming"));

		cgi_PrintHttpheaderEnd();
		} // if (http_request->argc == 0)
		
	else if (PharseCheckName_P(http_request, ProgID_P))
		{ // kann nur durch Drücken der Taste "Start" erreicht werden

		if (strcmp_P(http_request->argvalue[PharseGetValue_P(http_request, PSTR("progid"))], AutomatikBez) == 0)
			{
			DebugTestGetFilePerHttp(http_request->argvalue[PharseGetValue_P(http_request, BinServerPath_P)]);

/*			
			if (!ReadFlashIdentity(Ident))
				{ // Identifikation konnte nicht geladen werden.
				cgi_PrintHttpheaderStart();
				printf_P(PSTR("<big>ISP program enable failed. Check connection to target board.</big>"));
				printf_P(ClickToContinue_P);
				cgi_PrintHttpheaderEnd();
				}
			else if (Ident[0] == '\0')
				{ // Identifikation ist leer
				cgi_PrintHttpheaderStart();
				printf_P(PSTR("No identification on connected target found. Select program manually. "));
				printf_P(ClickToContinue_P);
				cgi_PrintHttpheaderEnd();
				} 
			else
				{ // Identifikation nicht leer
				for (i = 0 ; i < ProgDatenTabAnzahl ; i++)
					{
					if (strcmp_P(Ident, ProgDatenTab[i].Kennung) == 0)
						{
						ProgrammiereVordefiniert(i, http_request);
						break;
						}
					}
					
				if (i == ProgDatenTabAnzahl)
					{ // Identifikation nicht in der Liste der bekannten Programme
					cgi_PrintHttpheaderStart();
					printf_P(PSTR("Identification of target ""%s"" unknown. Select program manually. "), Ident);
					printf_P(ClickToContinue_P);
					cgi_PrintHttpheaderEnd();
					}
				} // else Identifikation gefunden
*/
				
			} // Automatische Auswahl des Programms
			
		else
			{
			for (i = 0 ; i < ProgDatenTabAnzahl ; i++)
				{
				if (strcmp_P(http_request->argvalue[PharseGetValue_P(http_request, PSTR("progid"))], ProgDatenTab[i].Name) == 0)
					{
					ProgrammiereVordefiniert(i, http_request);
					break;
					}
				// hier sollte es keinesfalls vorkommen, dass das Programm nicht gefunden wird.
				}
			} // else keine Automatik
				
		} // if (PharseCheckName_P(http_request, PSTR("autoprog")))

	else // ungültiger cgi-Aufruf
		{
		cgi_PrintHttpheaderStart();
		printf_P(PSTR("internal Error: invalid parameter. Click <a href=\"isp.cgi\">here</a> to continue."));
		cgi_PrintHttpheaderEnd();
		}
	}


void InitIspMaster()
	{
	init_IspResetOut();
	SPI_init(IspSpiPort);
	
	IspDiagnoseText[0] = '\0';
	cgi_RegisterCGI( cgi_Isp, PSTR("isp.cgi"));
	}
	
	
#endif //def ISP_MASTER
