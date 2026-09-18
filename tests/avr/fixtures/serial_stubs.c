#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdbool.h>
#include <stdint.h>

#include "BusKomm.h"
#include "BaudotCode.h"
#include "iTelex/Protokoll.h"
#include "TxP2-Defs.h"
#include "system/clock/clock.h"
#include "system/net/tcp.h"

volatile uint8_t Status;
volatile TBusAuftrag BusAuftrag;
volatile TBusErgebnis BusErgebnis;
volatile uint8_t BusVerbPartner;
uint8_t BusEigenAdresse;
volatile uint8_t BusAnrufSubAdresse;
volatile uint8_t BusSendeDaten;
volatile bool BusFrei;
volatile uint8_t BusKollisionZaehler;
volatile bool BusEmpfMark;
volatile bool BusEmpfMarkwechsel;
uint8_t BusEigenAdrMehrfach;
bool RundsendEmpfFreig;
volatile uint8_t RundsendDaten[RundsendMaxDaten];
volatile uint8_t RundsendAnzDaten;
volatile uint16_t TwiIsrCount;
volatile uint16_t TwiWatchdogCount;
TBaudotMode ProtokollBaudotMode;
TProtokollLevel ProtokollLevel;
struct TCP_SOCKET TCP_sockettable[MAX_TCP_CONNECTIONS];

void CLR_BIT_Status(uint8_t BitNr)
{
	Status &= (uint8_t)~_BV(BitNr);
}

void SET_BIT_Status(uint8_t BitNr)
{
	Status |= _BV(BitNr);
}

void BusSenden(uint8_t Kdo)
{
	if (Kdo == BusKdoMark || Kdo == BusKdoMarkWdh)
		PORTB |= _BV(PB1);
	else if (Kdo == BusKdoSpace || Kdo == BusKdoSpaceWdh)
		PORTB &= (uint8_t)~_BV(PB1);
}

void LED_on(char LED_index)
{
	(void)LED_index;
}

void LED_off(char LED_index)
{
	(void)LED_index;
}

bool ProtokollAktivFuerRuntime(TProtokollLevel Level)
{
	(void)Level;
	return false;
}

bool ProtokollAktivFuerTCPRuntime(void)
{
	return false;
}

bool ProtokollSpeichern(bool Flush)
{
	(void)Flush;
	return true;
}

void ProtokollierenC(char Zeichen)
{
	(void)Zeichen;
}

void Protokollieren_P(const prog_char *Text)
{
	(void)Text;
}

void ProtokollierenInt_P(const prog_char *Text, long Wert)
{
	(void)Text;
	(void)Wert;
}

char CodeZuZeichen(uint8_t Code, TBaudotMode *Mode)
{
	(void)Code;
	(void)Mode;
	return 0;
}

int CLOCK_GetTime(struct TIME *Time)
{
	(void)Time;
	return CLOCK_OK;
}

void softreset(void)
{
}
