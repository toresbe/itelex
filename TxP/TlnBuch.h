#ifndef __TLNBUCH_H__

#define __TLNBUCH_H__

#include "config.h"

#ifdef TELEXPHONE

#include <stdbool.h>
#include <inttypes.h>

#include "TxP.h"

extern void TlnDatenInit(TTlnDaten *Tln);

extern bool TlnSuche(uint32_t SucheNummer, bool AuchGeloescht, TTlnDaten *Tln);

extern int8_t TlnHinzufuegen(TTlnDaten *Tln, bool DatumAktualisieren);	

//! Datentyp für die Speicherung der aktuellen Lister-Position
typedef struct
	{
	char* Pos; //!< Position in Puffer
	char Ref[10]; //!< Referenzmuster, wenn abweichend muss neu gestartet werden.
	} TTlnListerDat; 
	
extern bool TlnListerStart(TTlnListerDat *ldp);

extern bool TlnListerNaechster(TTlnListerDat *ldp, TTlnDaten *Tln);	

extern void TlnBuchInit();

#endif //def TELEXPHONE

#endif //ndef __TLNBUCH_H__