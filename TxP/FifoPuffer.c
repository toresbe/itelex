#include "FifoPuffer.h"

#include <avr/interrupt.h>

void PufferInit(TPuffer *p)
	{
	p->BuZiMode = '\0'; // undefiniert
	p->SpeichP = 0;
	p->AusgP = 0;
	}

	
static uint8_t PufferNP(uint8_t p)
	{
	if (p >= MaxPuffer - 1)
		return 0;
	else
		return p+1;
	}


extern bool PufferLeer(TPuffer *p) 
	{
	return p->AusgP == p->SpeichP;
	}
	
	
extern bool PufferVoll(TPuffer *p) 
	{
	return PufferNP(p->SpeichP) == p->AusgP;
	}
	
	
bool PufferSpeich(TPuffer *p, uint8_t code)
	{
	if (!PufferVoll(p))
		{
		uint8_t SregTemp = SREG; // sichert Interrupt-Enable
		cli();
		p->Puffer[p->SpeichP] = code;
		p->SpeichP = PufferNP(p->SpeichP);
		SREG = SregTemp;
		return true;
		}
	else
		return false;
	}
	

uint8_t PufferAusg(TPuffer *p)
	{
	uint8_t SregTemp = SREG; // sichert Interrupt-Enable
	cli();
	uint8_t res = p->Puffer[p->AusgP];
	p->AusgP = PufferNP(p->AusgP);
	SREG = SregTemp;
	return res;
	}
	
 
 //! liefert Anzahl Zeichen im Puffer.
 uint8_t PufferAnzahl(TPuffer *p)
	{
	if (p->SpeichP >= p->AusgP)
		// Normalfall
		return p->SpeichP - p->AusgP;
	else
		// genutzer Speicher "geht ein mal rum"
		return (p->SpeichP +  MaxPuffer) - p->AusgP;
	}

