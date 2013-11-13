#ifndef __PROTOKOLL_H__

#define __PROTOKOLL_H__

#include <stdbool.h>

//! Meldungs-Level für "generelle" Meldungen
typedef enum
	{
	Keine,
	NurFehler,
	AblaufInfo,
	DatenKurz,
	DatenDetailliert,
	AuchRegelmaessiges,
	} TProtokollLevel;
	

extern TProtokollLevel ProtokollLevel;

extern TProtokollLevel ProtokollLevelTlnServ;

extern bool ProtokollSpeichern(bool flush);

extern void Protokollieren(char *s);

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

extern void ProtokollInit();

#endif //ndef __PROTOKOLL_H__