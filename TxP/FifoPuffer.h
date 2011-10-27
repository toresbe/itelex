#ifndef __FIFOPUFFER_H__

#define __FIFOPUFFER_H__

#include <inttypes.h>
#include <bool.h>

enum { MaxPuffer = 50 } ; 

typedef volatile struct {
	uint8_t Puffer[MaxPuffer]; // enthält Baudot-Codes oder im Wahlzustand die Wahlziffern
	char BuZiMode; // Umschaltung Bu/Zi beim Füllen bzw. Auslesen
	uint8_t SpeichP, AusgP;
	} TPuffer;


extern void PufferInit(TPuffer *p);
	
extern bool PufferSpeich(TPuffer *p, uint8_t code);
	
extern bool PufferLeer(TPuffer *p);

extern bool PufferVoll(TPuffer *p);
	
extern uint8_t PufferAusg(TPuffer *p);	
 
extern uint8_t PufferAnzahl(TPuffer *p);

#endif //ndef __FIFOPUFFER_H__
