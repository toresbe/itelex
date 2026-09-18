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

#ifndef ITELEX_TRACE_LEVEL
#define ITELEX_TRACE_LEVEL 255
#endif


extern TBaudotMode ProtokollBaudotMode;

extern TProtokollLevel ProtokollLevel;

#ifdef ITELEX_TLNSERVER
extern TProtokollLevel ProtokollLevelTlnServ;
#endif //def ITELEX_TLNSERVER

extern bool ProtokollAktivFuerRuntime(TProtokollLevel p);

extern bool ProtokollAktivFuerTCPRuntime(void);

extern bool UhrzeitImpulseAufSeriellerSchnittstelleRuntime(void);

/** Return whether a trace category is enabled at run time and compiled in. */
static inline bool ProtokollAktivFuer(TProtokollLevel p)
	{
	return p <= ITELEX_TRACE_LEVEL && ProtokollAktivFuerRuntime(p);
	}

/** Return whether exactly one trace category is selected and compiled in. */
static inline bool ProtokollAktivGenauFuer(TProtokollLevel p)
	{
	return p <= ITELEX_TRACE_LEVEL && ProtokollLevel == p;
	}

/** Return whether TCP tracing is enabled and compiled in. */
static inline bool ProtokollAktivFuerTCP(void)
	{
	return ITELEX_TRACE_LEVEL >= TcpVerbindungen && ProtokollAktivFuerTCPRuntime();
	}

extern bool ProtokollSpeichern(bool flush);

extern void Protokollieren(char *s);

extern void ProtokollierenC(char c);

extern void Protokollieren_P(const char *s);

extern void ProtokollierenInt_P(const char *s, long i);

extern void ProtokollierenIPAdr(long aip);

extern void ProtokollierenMAC(char mac[6]);

extern void ProtokollierenPuffer(char Buf[], uint16_t Len);

extern void ProtokollRegelblockInit();

extern void ProtokollRegelblockStart();

extern void ProtokollRegelblockEnde();

extern void ProtokollRegelblockAbbruch();

extern void ProtokollRegelblockLoeschen();

extern void ProtokollRedirectStdout();

/** Return whether the serial output is configured for clock pulses. */
static inline bool UhrzeitImpulseAufSeriellerSchnittstelle(void)
	{
	return ITELEX_TRACE_LEVEL >= UhrzeitImpulse && UhrzeitImpulseAufSeriellerSchnittstelleRuntime();
	}

extern void ProtokollInit();

#endif //ndef __PROTOKOLL_H__
