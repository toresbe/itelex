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
#include "system/string/string.h"
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


#ifdef iTelex // hier ist die Hardware-Plattform gemeint
#define IspSpiPort 2
#endif //def iTelex

#if defined( AVRNETIO ) || defined( iTelex_Light )
#define IspSpiPort 1
#endif //def AVRNETIO || iTelex_Light



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
		
		#if defined( _SPI_2_H )
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


enum { MaxIdentLen = 40 };


//! Liest eine im Flash des Target abgelegte Identifikation aus.
//--------------------------------------------------------------
//! Die Identifikation besteht aus einem maximal 40 Zeichen langen 
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
	

//! Liest die Signatur des angeschlossenen Chips aus
// -------------------------------------------------
//! \param SigBytes zeigt auf einen 3-Byte-Array

static bool ReadSignatureBytes(uint8_t *SigBytes)
	{
	if (!IspEnable())
		return false;

	uint8_t i;
	
	for (i = 0 ; i < 3 ; i++)
		{
		if (!SigRead(i, SigBytes + i))
			{
			IspClose();
			return false;
			}
		}
	
	IspClose();
	return true;
	}


#if 0 // kein Testen mehr erforderlich
	
//! Testfunktion in der Debugging-Phase
//-------------------------------------
//! Wird im finalen Code nicht mehr gebraucht.
	
static void DebugTestReadFunctions()
	{
	if (IspEnable())
		{
		uint8_t x;	
/*
		for (uint8_t i = 0 ; i < 20 ; i++)
			if (FlashRead(i, &x))
				printf_P(PSTR("Flash byte %d = 0x%02X<br>"), i, x);
*/
				
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
	
#endif // 0
	

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
//! \param[Out] Size Dateigröße laut Header.
int16_t HttpReadHeader(int SocketID, uint16_t *Size)
	{
	char LineBuf[64];
	uint8_t i;
	bool Flag;
	uint8_t Pos;
	uint16_t Res;
	TKurzTimer AbbruchTimer;
	
	Res = 0;
	if (Size != NULL)
		*Size = 0;
	
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
		// jetzt zeigt Pos auf das zweite Wort oder das Zeilenende
		
		if (Pos == 0)
			break; // das war eine Leerzeile, die beendet den Header
		
		// abhängig vom Zeilenanfang eine Auswertung durchführen
		if (strncmp_P(LineBuf, PSTR("HTTP/"), 5) == 0)
			Res = atoi(LineBuf + Pos); // der Ergebniscode
			
		else if (strncmp_P(LineBuf, PSTR("Content-Length:"), 15) == 0)
			{
			if (Size != NULL)
				*Size = atoi(LineBuf + Pos);
			}
		
		// else... jetzt könnte man noch andere Rückmeldungen auswerten
		
		} // while (true) ... Schleife über alle Zeilen des Header

	if (ProtokollLevel >= AblaufInfo) // Daten explizit
		ProtokollierenInt_P(PSTR("HttpReadHeader: Res = %d\r\n"), Res);

	return Res;
	} // HttpReadHeader()
	
	
#if 0 // nur in der Testphase gebraucht

static int DebugTestGetFilePerHttp(char *Filename)
	{
	int SocketID;
	char InBuf[64]; // ex 100
	TKurzTimer AbbruchTimer;
	int16_t HttpHeadCode;
	uint16_t GesamtBytes;
	
	SocketID = HttpGet(Filename);
	if (SocketID < 0)
		return -1;
		
	HttpHeadCode = HttpReadHeader(SocketID, NULL);
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
	
#endif // 0

	

// Hier folgenden die Hauptfunktionen
// ===========================================

char IspDiagnoseText[300];


const PROGMEM char BinServerPath_P[] = "servpath";
const PROGMEM char ProgID_P[] = "progid";
const PROGMEM char ClickToContinue_P[] = "<p>Click <a href=\"isp.cgi\">here</a> to continue.";


//! Programmiert und prüft eine Fuse
//----------------------------------

static bool FuseProgAndVerify(uint8_t id, uint8_t val)
	{
	uint8_t readback;
	
	if (!FuseWrite(id, val))
		{
		sprintf_P(IspDiagnoseText + strlen(IspDiagnoseText), PSTR("ISP fuse #%d program failed.<br>"), id);
		return false;
		}
		
	if (!FuseRead(id, &readback))
		{
		sprintf_P(IspDiagnoseText + strlen(IspDiagnoseText), PSTR("ISP fuse #%d read failed.<br>"), id);
		return false;
		}
		
	if (readback != val)
		{
		sprintf_P(IspDiagnoseText + strlen(IspDiagnoseText), PSTR("ISP fuse #%d verify failed.<br>"), id);
		return false;
		}
		
	return true;
	}
	
	
//! Assembles URL from Path template, actual filename (Ident) and Extension (like .bin)

static void BuildFullPath(char *Path, char *Ident, PGM_P Extension, char *FullPath)
	{
	strcpy(FullPath, Path);
	if (strchr(Path, '$') == NULL)
		{
		strcat_P(FullPath, PSTR("/"));
		strcat(FullPath, Ident);
		strcat_P(FullPath, Extension);
		}
	else
		{
		strchr(FullPath, '$')[0] = '\0'; // ab dem Doller wegschneiden
		strcat(FullPath, Ident);
		strcat_P(FullPath, Extension);
		strcat(FullPath, strchr(Path, '$') + 1); 
			// ein Zeichen hinter dem $ wieder Kopieren
		}
	}
	
	
//! Startet den Programmiervorgang entsprechend der Daten aus #ProgDatenTab.
//--------------------------------------------------------------------------
	
static void ProgrammiereVomNetz(char *Ident, uint8_t *SignaturIst, struct HTTP_REQUEST * http_request)
	{
	char FullPath[200];
	int SocketID;
	int Res;
	char Buf[64]; // TODO erhöhen
	char Rett;
	uint8_t BufUsed;
	uint8_t SignaturSoll[3];
	uint8_t Fuses[3];
	TKurzTimer AbbruchTimer;
	int16_t HttpHeadCode;
	uint16_t FileSize;
	uint16_t FehlerBytes;
	uint16_t BlockStart;
	uint16_t BlockFill;
	uint8_t i, Start;
	uint8_t readback;
	bool beenden;
	bool DownloadPfadGeaendert;

	StartKurzTimer(&iTelexThreadCheckTimer);
	LED_off(ROT);
	
	cgi_PrintHttpheaderStart();
	
	IspDiagnoseText[0] = '\0';
	
	if (!PharseCheckName_P(http_request, BinServerPath_P))
		{
		printf_P(PSTR("Error: no server path specified. "));
		printf_P(ClickToContinue_P);
		cgi_PrintHttpheaderEnd();
		return;
		}

	if (readConfig_P(BinServerPath_P, FullPath) != 1)
		DownloadPfadGeaendert = true;
	else
		DownloadPfadGeaendert = (strcmp(FullPath, http_request->argvalue[PharseGetValue_P(http_request, BinServerPath_P)]) != 0);

	printf_P(PSTR("Programm identity %s"), Ident);
	
	BuildFullPath(http_request->argvalue[PharseGetValue_P(http_request, BinServerPath_P)], Ident, PSTR(".txt"), FullPath);

	SocketID = HttpGet(FullPath);
	if (SocketID < 0)
		{
		printf_P(PSTR("<p>Download %s socket open failed code %d."), FullPath, SocketID);
		printf_P(ClickToContinue_P);
		cgi_PrintHttpheaderEnd();
		return;
		}
		
	HttpHeadCode = HttpReadHeader(SocketID, NULL); 
	if (HttpHeadCode != 200)
		{
		CloseTCPSocket(SocketID);
		printf_P(PSTR("<p>Download %s fileserver error code %d."), FullPath, HttpHeadCode);
		printf_P(ClickToContinue_P);
		cgi_PrintHttpheaderEnd();
		return;
		}
	
	// Download-Pfad erfolgreich geöffnet, also ggf. abspeichern
	if (DownloadPfadGeaendert) 
		{
		changeConfig_P(BinServerPath_P, http_request->argvalue[PharseGetValue_P(http_request, BinServerPath_P)]);
		}

	BufUsed = 0;
	
	// Datei enthält paarweise eine Chip-Signatur und die zugehörigen Fuse-Bytes.
	// In der Schleife wird die Signatur des angeschlossenen Chips mit der "gewünschten" Signatur verglichen.
	i = 0;
	do
		{
		// Puffer füllen
		Res = GetSocketData(SocketID, sizeof(Buf) - 1 - BufUsed, Buf + BufUsed);

		ProtokollierenInt_P(PSTR("Read start at pos %d"), BufUsed);
		ProtokollierenInt_P(PSTR(" reading %d bytes\r\n"), Res);

		if (Res < 0)
			{
			CloseTCPSocket(SocketID);
			printf_P(PSTR("<p>Error reading %s (code %d)."), FullPath, HttpHeadCode);
			printf_P(ClickToContinue_P);
			cgi_PrintHttpheaderEnd();
			return;
			}

		BufUsed += Res;
		
		Buf[BufUsed] = '\0';

		i = 0;

		Protokollieren_P(PSTR("Buffer content: >>>"));
		Protokollieren(Buf);
		Protokollieren_P(PSTR("<<<\r\n"));
		
		// Leerzeichen am Anfang überspringen
		while (Buf[i] != '\0' && Buf[i] <= ' ')
			i++; //Steuerzeichen und Spaces überspringen
		Start = i; // erstes Wort ist erwartete Signatur
		while (Buf[i] > ' ')
			i++; // erstes Trennzeichen finden
		
		Rett = Buf[i];
		Buf[i] = '\0';
		Res = strtobin(Buf + Start, (char *) SignaturSoll, 3);
		Buf[i] = Rett;
			
		ProtokollierenInt_P(PSTR("signature code from pos %d"), Start);
		ProtokollierenInt_P(PSTR(" to pos %d\r\n"), i);

		if (Res == 0)
			{ // Umwandlung der Signatur erfolgreich. Jetzt zweites Wort suchen
			i = i + 1;
			while (Buf[i] != '\0' && Buf[i] <= ' ')
				i++; //Steuerzeichen und Spaces überspringen
			Start = i;
			while (Buf[i] > ' ')
				i++; // erstes druckbares Zeichen finden

			Rett = Buf[i];
			Buf[i] = '\0';
			Res = strtobin(Buf + Start, (char *) Fuses, 3);
			Buf[i] = Rett;

			ProtokollierenInt_P(PSTR("fuse code from pos %d"), Start);
			ProtokollierenInt_P(PSTR(" to pos %d\r\n"), i);
			
			// Res sollte jetzt immer noch 0 sein. 0 = strtobin war erfolgreich
			}

		if (Res != 0)
			{
			ProtokollierenInt_P(PSTR("Decode error %d\r\n"), Res);
			
			CloseTCPSocket(SocketID);
			if (Buf[Start] == '+')
				printf_P(PSTR("<p>Actual chip signature fits not to allowed signaltures in %s."), FullPath); // Listenende erreicht
			else
				printf_P(PSTR("<p>Invalid signature / fuses format in file %s: %s position %d."), FullPath, Buf, Start); // Format-Problem
				
			printf_P(ClickToContinue_P);
			cgi_PrintHttpheaderEnd();
			return;
			}
		
		memmove(Buf, Buf + i, BufUsed - i);
		BufUsed -= i;
		
		} while (SignaturIst[0] != SignaturSoll[0] || SignaturIst[1] != SignaturSoll[1] || SignaturIst[2] != SignaturSoll[2]);

	CloseTCPSocket(SocketID);
	
	printf_P(PSTR("<p>Read from %s: Signature %02X %02X %02X, Fuses %02X %02X %02X"), 
		FullPath, SignaturSoll[0], SignaturSoll[1], SignaturSoll[2], Fuses[0], Fuses[1], Fuses[2]);
	
	// jetzt die eigentlichen Programmdaten
	
	BuildFullPath(http_request->argvalue[PharseGetValue_P(http_request, BinServerPath_P)], Ident, PSTR(".bin"), FullPath);
	
	printf_P(PSTR("<p>Loading binary data from %s."), FullPath);
	
	// da der Rest Zeitkritisch ist wird der Bildschirmaufbau erstmal zuende gebracht.
	LED_on(BLAU); 
	printf_P(PSTR("<p>Click <a href=\"isp.cgi\">here</a> after all LED on ethernet interface board went off again."));
	cgi_PrintHttpheaderEnd();
	STDOUT_Flush();
	CloseTCPSocket(http_request->HTTP_SOCKET); // ist erfoderlich, damit erstmal die Meldung erscheint.

	SocketID = HttpGet(FullPath);
	if (SocketID < 0)
		{
		sprintf_P(IspDiagnoseText, PSTR("Socket open failed code %d."), SocketID);
		LED_off(BLAU); 
		return;
		}
		
	HttpHeadCode = HttpReadHeader(SocketID, &FileSize); 
	ProtokollierenInt_P(PSTR("Header Code: %d\r\n"), HttpHeadCode);
	ProtokollierenInt_P(PSTR("File size: %d\r\n"), FileSize);
	if (HttpHeadCode != 200)
		{
		CloseTCPSocket(SocketID);
		sprintf_P(IspDiagnoseText, PSTR("Fileserver error code %d."), HttpHeadCode);
		LED_off(BLAU); 
		return;
		}

	StartKurzTimer(&iTelexThreadCheckTimer);
	LED_off(ROT); 
		
	LED_on(GELB); 
	
	if (!IspEnable())
		{
		strcpy_P(IspDiagnoseText, PSTR("ISP program enable failed. Check connection to target board."));
		CloseTCPSocket(SocketID);
		LED_off(GELB); 
		LED_off(BLAU); 
		return;
		}

	FehlerBytes = 0;
	
	// start with programming of fuses
	for (i = 0 ; i < 3 ; i++)
		{
		if (Fuses[i] != 0)
			{
			if (!FuseProgAndVerify(0, Fuses[0]))
				FehlerBytes++; // failed, Message already stored
			}
		}

	if (FehlerBytes == 0)
		{ // no errors so far -> erase old content
		if (!ChipErase())
			{
			strcat_P(IspDiagnoseText, PSTR("ISP chip erase failed.<br>"));
			FehlerBytes++; // failed
			}
		}
		
	if (FehlerBytes > 0) // any error so far?
		{
		strcat_P(IspDiagnoseText, PSTR("Failed: nothing programmed."));
		IspClose();
		CloseTCPSocket(SocketID);
		LED_off(GELB);
		LED_off(BLAU);
		return;
		}

	LED_off(GELB);
		
	StartKurzTimer(&AbbruchTimer);
	
	beenden = false;
	FehlerBytes = 0;
	BlockStart = 0;
	BlockFill = 0;
	
	while (!beenden)
		{
		StartKurzTimer(&iTelexThreadCheckTimer);
		LED_off(ROT);
		
		int InCount = GetBytesInSocketData(SocketID);

		if ((BlockStart & 64) == 0)
			LED_on(GRUEN);
		else
			LED_off(GRUEN); // gruen blinkt mit jedem Block
			
		if (KurzTimerVal(&AbbruchTimer) > 5 * KurzTimerFreq)
			{
			strcpy_P(IspDiagnoseText, PSTR("Server timeout? "));
			Protokollieren_P(PSTR("FileGet: Timeout beim Empfang.\r\n"));
			beenden = true;
			}
		
		else if (InCount > 0)
			{
			//ProtokollierenInt_P(PSTR("FileGet Empfang: (%d/" ), InCount);

			if (InCount > 64 - BlockFill)
				InCount = 64 - BlockFill;
			Res = GetSocketData(SocketID, InCount, Buf + BlockFill);
			
			//ProtokollierenInt_P(PSTR("%d)"), Res);
			//if (Res > 0)
			//	ProtokollierenPuffer(Buf, Res);
			//Protokollieren_P(PSTR("\r\n"));
			//if (CheckSocketState(SocketID) != SOCKET_READY)
			//	ProtokollierenInt_P(PSTR("CheckSocketState() = %d\r\n" ), CheckSocketState(SocketID));

			if (Res > 0)	
				BlockFill += Res;
				
			StartKurzTimer(&AbbruchTimer);
			}	
				
		else if (CheckSocketState(SocketID) == SOCKET_NOT_USE && KurzTimerVal(&AbbruchTimer) > 3 * KurzTimerFreq)
			{ // dies ist das normale Ende.
			Protokollieren_P(PSTR("FileGet: Beendigung durch Gegenstelle.\r\n"));
			beenden = true;
			}
			
		if (BlockFill == 64   // ein Block zum Programmieren ist bereit
			|| (beenden && BlockFill > 0) 
						// oder Programmierdaten sind teilweise vorhanden aber Dateiende (Timeout) ist erreicht
			|| ((BlockStart + BlockFill) >= FileSize && FileSize > 0))
						// oder Programmierdaten wurden vollständig empfangen (nominelle Dateigröße erreicht) 
			{
			LED_on(GELB);
			if (!FlashWriteBlock64(BlockStart, (uint8_t *) Buf))
				{
				strcpy_P(IspDiagnoseText, PSTR("FlashWriteBlock64 failed. "));
				beenden = true;
				}

			for (i = 0 ; i < BlockFill ; i++)
				{
				if (!FlashRead(BlockStart + i, &readback))
					{
					strcpy_P(IspDiagnoseText, PSTR("FlashRead failed."));
					beenden = true;
					break;
					}
				if (readback != ((uint8_t *) Buf)[i])
					{
					if (FehlerBytes == 0)
						sprintf_P(IspDiagnoseText, PSTR("Verify failed: %04x is %02x should be %02x. "), BlockStart + i, readback, Buf[i]);
					FehlerBytes++;
					}
				}
			LED_off(GELB);
				
			BlockStart += BlockFill; // BlockFill ist meistens 64, außer beim Beenden.
			BlockFill = 0;

			if (BlockStart == FileSize)
				beenden = true;
			
			} // if (BlockFill == 64 || (beenden && BlockFill > 0))
			
		} // while (!beenden)

	IspClose();
	CloseTCPSocket(SocketID);
	
	ProtokollierenInt_P(PSTR("GesamtBytes = %d\r\n" ), BlockStart);
	ProtokollierenInt_P(PSTR("FehlerBytes = %d\r\n" ), FehlerBytes);

	if (FehlerBytes == 0 && (FileSize == 0 || BlockStart == FileSize))
		sprintf_P(IspDiagnoseText + strlen(IspDiagnoseText), PSTR("Success: %d bytes written to flash, no failures."), BlockStart);
	else
		sprintf_P(IspDiagnoseText + strlen(IspDiagnoseText), PSTR("Failed: %d nominal size, %d bytes written to flash, %d failed."), FileSize, BlockStart, FehlerBytes);
		
	LED_off(GRUEN);
	LED_off(BLAU);
	
	} // ProgrammiereVomNetz()
	

	
void ProgrammiereDupliziertenBootloader()
	{
	// Kennung des angeschlossenen Atmel prüfen
	
	// Fuses brennen
	
	// Porgramm aus eigenem Flash duplizieren
	
	// dazu pgm_read_byte_far(uint32_t) benutzen
	}
	
	
//! Benutzerschnittstelle für das Brennen
// ===========================================

void cgi_Isp(void *pStruct)
	{
	//static TSprache Sprache;
	char BinServerPath[120]; 
	char Ident[50];
	uint8_t SignaturIst[3];

	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;

	if (!PruefeSpracheUndKonfigFreigabe(pStruct))
		return;

	Ident[0] = '\0';
	
	if (http_request->argc == 0)
		{ // Standard-Aufruf
		if (readConfig_P(BinServerPath_P, BinServerPath) != 1)
			BinServerPath[0] = '\0';
		
		cgi_PrintHttpheaderStart();

		CgiFormStartTabbed_P(PSTR("isp.cgi"));

		CgiFormInputFieldText_P(PSTR("Path to server for binaries"), BinServerPath_P, sizeof(BinServerPath)-1, BinServerPath);
		// z.B. sourceforge.net/p/telexphone2/code/HEAD/tree/trunk/AlleBins/$?format=raw
		
		CgiFormInputFieldText_P(PSTR("What to program (leave empty for auto-detection)"), ProgID_P, sizeof(Ident)-1, Ident);


		CgiFormFinish_P(PSTR("Start programming"));
		
		if (IspDiagnoseText[0] != '\0')
			printf_P(PSTR("<p>Result of last operation:<p><big>%s</big>"), IspDiagnoseText);
		else
			printf_P(PSTR("<h3>Before start programming action connect target board by special cable</h3><p>"));
		
		cgi_PrintHttpheaderEnd();
		} // if (http_request->argc == 0)
		
	else if (PharseCheckName_P(http_request, ProgID_P))
		{ // kann nur durch Drücken der Taste "Start" erreicht werden
		IspDiagnoseText[0] = '\0';
		LED_on(BLAU);
		strncpy(Ident, http_request->argvalue[PharseGetValue_P(http_request, ProgID_P)], sizeof(Ident)-1);
		Ident[sizeof(Ident)-1] = '\0';
		if (Ident[0] == '\0')
			{ // Automatische Erkennung wurde gewählt
			if (!ReadFlashIdentity(Ident))
				{ // Identifikation konnte nicht geladen werden.
				cgi_PrintHttpheaderStart();
				printf_P(PSTR("<big>ISP program enable failed. Check connection to target board.</big>"));
				printf_P(ClickToContinue_P);
				cgi_PrintHttpheaderEnd();
				Ident[0] = '\0';
				}
			else if (Ident[0] == '\0')
				{ // Identifikation ist leer
				cgi_PrintHttpheaderStart();
				printf_P(PSTR("No identification on connected target found. Select program manually. "));
				printf_P(ClickToContinue_P);
				cgi_PrintHttpheaderEnd();
				} 
			}

		if (strncmp_P(Ident, PSTR("TxP2"), 4) == 0)
			memcpy_P(Ident, PSTR("itlx"), 4);
		
		if (!ReadSignatureBytes(SignaturIst))
			{ // Chip-Signatur konnte nicht geladen werden.
			cgi_PrintHttpheaderStart();
			printf_P(PSTR("<big>Signature readout failed. Check connection to target board.</big>"));
			printf_P(ClickToContinue_P);
			cgi_PrintHttpheaderEnd();
			Ident[0] = '\0';
			}
			
		LED_off(BLAU);

		if (Ident[0] != '\0')
			{ // Identifikation scheint gültig (entweder automatisch ermittelt oder von Hand eingegeben)
			ProgrammiereVomNetz(Ident, SignaturIst, http_request); 
			}
				
		} // if (PharseCheckName_P(http_request, ProgID_P))

	/*/ ab hier Test-Programmteile
	// ========================== HACK
	else if (PharseCheckName_P(http_request, Fuses_P))
		{ // muss von Hand eigegeben werden isp.cgi?fuses
		uint8_t i;
	
		strcpy(Ident, http_request->argvalue[PharseGetValue_P(http_request, Fuses_P)]); // Ident wird misbraucht

		cgi_PrintHttpheaderStart();
		
		CgiFormStartTabbed_P(PSTR("isp.cgi"));
		CgiFormInputFieldText_P(PSTR("Fuses (hex values)"), Fuses_P, sizeof(Ident)-1, Ident);
		CgiFormFinish_P(PSTR("Start fuse check"));

		if (strlen(Ident) >= 2)
			{ // check ob alles Hex-Werte sind
			for (i = 0 ; i < strlen(Ident) ; i++)
				if (atoh(Ident[i]) < 0)
					break;
			
			if (i == strlen(Ident)) // nur Hex werte und gerade anzahl
				{
				if (!IspEnable())
					printf_P(PSTR("<p><big>ISP program enable failed. Check connection to target board.</big><p>"));
				else
					{
					if (i >= 2) // mindestens die Low-Fuse angegeben
						{
						uint8_t Wert = (atoh(Ident[i-2]) << 4) + atoh(Ident[i-1]);
						if (!FuseWrite(0, Wert))
							printf_P(PSTR("<p><big>ISP fuse low program failed.</big><p>"));
						}

					if (i >= 4) // mindestens die Low-Fuse angegeben
						{
						uint8_t Wert = (atoh(Ident[i-4]) << 4) + atoh(Ident[i-3]);
						if (!FuseWrite(1, Wert))
							printf_P(PSTR("<p><big>ISP fuse high program failed.</big><p>"));
						}

					if (i >= 6) // mindestens die Low-Fuse angegeben
						{
						uint8_t Wert = (atoh(Ident[i-6]) << 4) + atoh(Ident[i-5]);
						if (!FuseWrite(2, Wert))
							printf_P(PSTR("<p><big>ISP fuse extended program failed.</big><p>"));
						}

					for (i = 0 ; i < 3 ; i++)
						if (FuseRead(i, &x))
							printf_P(PSTR("Fuse byte %d = 0x%02X<br>"), i, x);
						
					IspClose();
					}
				}
			}
		
		DebugTestReadFunctions();

		cgi_PrintHttpheaderEnd();
		}
	
	// --------------------------
	// bis hier Test-Programmteile */
	
	
	else // ungültiger cgi-Aufruf
		{
		cgi_PrintHttpheaderStart();
		printf_P(PSTR("internal Error: invalid parameter. Click <a href=\"isp.cgi\">here</a> to continue."));
		cgi_PrintHttpheaderEnd();
		}
	} // cgi_Isp()


//! Initialisiert und registriert die CGI-Funktion für das Brennen
//----------------------------------------------------------------
void InitIspMaster()
	{
	init_IspResetOut();
	SPI_init(IspSpiPort);
	
	IspDiagnoseText[0] = '\0';
	cgi_RegisterCGI( cgi_Isp, PSTR("isp.cgi"));
	} // InitIspMaster()
	
	
#endif //def ISP_MASTER
