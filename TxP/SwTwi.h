#ifndef __SWTwi_H__

#define __SWTwi_H__

#include <inttypes.h>

#include <stdbool.h>

extern void SwTwiInit();

extern bool SwTwiStart();

extern bool SwTwiStop(bool ExitWhenWaitstate);

extern void SwTwiForceStop();

extern bool SwTwiSendByte(uint8_t x, bool* Ack, bool ExitWhenWaitstate);

extern bool SwTwiReadByte(uint8_t* x, bool Ack, bool ExitWhenWaitstate);





/*
#define MAXDATEN 16


static enum { Wartend, Startbereit, Laeuft, Beendet } SwTwiModus;


extern uint8_t SwTwiAdresse;
extern uint8_t SwTwiDaten[MAXDATEN];
extern uint8_t SwTwiAnzahlDaten; 
extern uint8_t SwTwiResultat; // = Anzahl erfolgreich übertragener SwTwiDaten

*/

// Fehlercodes:
typedef enum {
	KeinFehler       = 0x00,
	UnbekanntesKdo   = 0x01,
	KeineAntwort     = 0x02,
	SlaveAbbruch     = 0x03,
	ArbitLost        = 0x04,
	StoerungSDA      = 0x11,
	StoerungSCL      = 0x12,
	Blockiert        = 0x13,
	InterfaceDefekt  = 0x14,
	InternerFehler   = 0x3F } TSwTwiFehler;
		

extern TSwTwiFehler SwTwiLetzterFehler;


/*
extern void SwTwiMain();
*/


#endif //__SWTwi_H__
