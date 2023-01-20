#ifndef __PROTOKOLL_H__

#define __PROTOKOLL_H__

#include <stdbool.h>

#include "BaudotCode.h"


//! Meldungs-Level für Ausgabe auf der Seriellen Schnittstelle: (Debug-Ausgaben, Protokollierung der Kommunikation oder Uhrenimpulse)
typedef enum
	{
	Keine						= 0,
	NurFehler					= 1,
	AblaufInfo					= 2,
	DatenKurz					= 3,
	DatenDetailliert			= 4,
	AuchRegelmaessiges			= 5,
	AblaeufeAlle				= 9,
	TcpVerbindungen				= 10, // wirkt als "Offset" auf Keine bis AblaeufeAlle
	TelexKommunikationText		= 21,
	TelexKommunikationAblaeufe  = 22,
	TelexKommunikationMitVerbindungsdaten = 23,
	TelexKommunikationMitDatum  = 24,
	TelexKommunikationAlles		= 29,
	UhrzeitImpulse				= 99 // dies ist eine eigene Funktion, die die serielle Schnittstelle als Uhrenimpuls-Ausgabe missbraucht.
	} TProtokollLevel;
// TelexKommunikation wird aufgezeichnet parallel zur Bearbeitung der Fernschreiber-Schnittstelle (seriell-Umsetzung).
// bei Netz -> Endgerät also beim Auslesen von #SendePuffer
// bei Endgerät -> Netz also beim Speichern in #EmpfPuffer


extern TBaudotMode ProtokollBaudotMode;

extern TProtokollLevel ProtokollLevel;

#ifdef ITELEX_TLNSERVER
extern TProtokollLevel ProtokollLevelTlnServ;
#endif //def ITELEX_TLNSERVER

extern bool ProtokollAktivFuer(TProtokollLevel p);

extern bool ProtokollAktivFuerTCP();

extern bool ProtokollSpeichern(bool flush);

extern void Protokollieren(char *s);

extern void ProtokollierenC(char c);

extern void Protokollieren_P(const prog_char *s);

extern void ProtokollierenInt_P(const prog_char *s, long i);

extern void ProtokollierenIPAdr(long aip);

extern void ProtokollierenMAC(char mac[6]);

extern void ProtokollierenPuffer(char Buf[], uint16_t Len);

extern void ProtokollRegelblockInit();

extern void ProtokollRegelblockStart();

extern void ProtokollRegelblockEnde();

extern void ProtokollRegelblockAbbruch();

extern void ProtokollRegelblockLoeschen();

extern void ProtokollRedirectStdout();

extern bool UhrzeitImpulseAufSeriellerSchnittstelle();

extern void ProtokollInit();

#endif //ndef __PROTOKOLL_H__