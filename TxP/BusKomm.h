#ifndef __BUSKOMM_H__

#define __BUSKOMM_H__

#include <inttypes.h>
#include <avr/interrupt.h>
#include <bool.h>

#include "bits.h"

// Status-Variablen
// ----------------

extern volatile uint8_t Status;
//!< Bitcodiert, siehe folgende Definitionen. Manches Bit doppelt belegt, abhängig von Bit 7

enum { StatBit_Frei				= 7	}; //!< keine Verbindung besteht (weder innen noch außen)
enum { StatBit_BusKdoEmpfangen	= 6	}; //!< Empfangenes Kommando noch nicht abschließend bearbeitet
// folgende Bits gelten nur bei Frei = JA:
enum { StatBit_LeitungKennung	= 5 }; //!< Leitungsschnittstelle (nur gültig bei StatBit_Frei = 1)
enum { StatBit_SpezialGeraetKennung	= 4 }; //!< Leitungsschnittstelle (nur gültig bei StatBit_Frei = 1)
// folgende Bits gelten nur bei Frei = NEIN:
enum { StatBit_Verbunden		= 5	}; //!< Verbindung ist komplett hergestellt 
enum { StatBit_AngerufenBelegt	= 4	}; //!< Fernschreiber wurde von anderem Partner (intern oder extern) aktiviert
enum { StatBit_FsBefBetrieb		= 3	}; //!< Fernschreiber ist eingeschaltet (Polarität)
enum { StatBit_FsBefEin			= 2	}; //!< Fernschreiber Stromschleife ist aktivert (Ausgabe)
enum { StatBit_FsMeldBetrieb	= 1	}; //!< Fernschreiber angeschaltet (Stromschleife aktiv oder nur kurz unterbrochen
enum { StatBit_FsMeldEin		= 0 }; //!< Fernschreiber Stromschleife mit Stromfluß (Einlesung)

typedef volatile enum { Ok, KeineAntwort, Abbruch, Besetzt } TBusErgebnis;
typedef volatile enum { Nichts, Senden, BedSenden, Lesen, Fertig } TBusAuftrag;

extern volatile TBusAuftrag BusAuftrag; 
extern volatile TBusErgebnis BusErgebnis; // Besetzt nur nach BedSenden 
extern volatile uint8_t BusVerbPartner; // als I²C-Adresse, also * 2
extern uint8_t BusEigenAdresse; // als I²C-Adresse, also * 2. Bei Mehrfach-Adressen nur Basisadresse
extern uint8_t BusEigenAdrMehrfach; // muss Potenz von 2 sein (also 1, 2, 4, 8, 16, ... , Standard = 1
extern volatile uint8_t BusAnrufSubAdresse; // tatsächlich als Adresse verwendete Nummer 
extern volatile uint8_t BusSendeDaten; //!< zu sendendes Byte
extern volatile bool BusFrei;
extern volatile uint8_t BusKollisionZaehler; // nur zum Testen / Statistik
extern volatile bool BusEmpfMark;
extern volatile bool BusEmpfMarkwechsel;
extern volatile uint16_t TwiIsrCount;

// extern void StatusKdoEmpfReset(); --> macht jetzt GetEmpfDaten

extern void CLR_BIT_Status(uint8_t BitNr);
	
extern void SET_BIT_Status(uint8_t BitNr);

extern void TwiInit(void);
// vorher muss BusEigenAdresse und BusEigenAdrMehrfach gesetzt sein!

extern void BusWarteFertig(void);

extern void BusSenden(uint8_t Kdo);

extern void WarteSchlussQuittung(uint16_t MaxTimer);

extern int16_t GetStatus(uint8_t Adr);

extern bool BusEigenAdressePruefenUndSetzen(uint8_t neu);

extern uint8_t WahlZuAdresse(uint8_t Wahl, uint8_t AnzZiffern);

extern uint8_t AdresseZuWahl(uint8_t Adresse, uint8_t *AnzZiffern);

extern void SendeLebenszeichen(void);

extern bool GetEmpfByte(uint8_t *Code);
	//!< holt aus dem Empfangspuffer den nächsten Code
	//!< \retval false, wenn Empfangspuffer leer ist.
	
extern bool EmpfPufferLeer(void);


#endif //ndef __BUSKOMM_H__
