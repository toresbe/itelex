#ifndef __SERIELLUMSETZ_H__

#define __SERIELLUMSETZ_H__

#include <inttypes.h>
#include <bool.h>

// Empfang: Umsetzung Seriell (Baudot) --> Parallel (Daten)
	
enum { SerUmEmpfWarte = 0, SerUmEmpfFertig = 8, SerUmSendWarte = 0, SerUmSendStart = 1 } ;
	
extern volatile uint8_t SerUmEmpfBitNr; 
	// 0 = Grundzustand, 1 = Startbit-Prüfung, 2-6 = Datenbits 1-5, 7 = Stopbit-Prüfung, 
	// 8 = Empfang beendet, Daten zur Verarbeitung bereit

extern volatile uint8_t SerUmEmpfDaten; 

extern volatile bool SerUmEmpfFehler; // Stop-Bit war nicht 1

// Senden: Umsetzung Parallel (Daten) --> Seriell (Baudot)

extern volatile uint8_t SerUmSendBitNr; 
	// 0 = Grundzustand, 1 = Sendedaten bereit, 2 = Startbit, 3-7 = Datenbits 1-5, 8 = Stopbit

extern volatile uint8_t SerUmSendDaten;


extern void SeriellUmsetzInit();

extern void SeriellUmsetzung(bool SeriellEing, bool *SeriellAusg);

#endif //ndef __SERIELLUMSETZ_H__
