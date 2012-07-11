#ifndef __TLNBUCH_H__

#define __TLNBUCH_H__

#include "config.h"

#ifdef TELEXPHONE

#include <stdbool.h>
#include <inttypes.h>

#include "TxP.h"

extern void TlnDatenInit(TTlnDaten *Tln);

extern bool TlnSuche(uint32_t SucheNummer, bool AuchGeloescht, TTlnDaten *Tln);

extern bool TlnHinzufuegen(TTlnDaten *Tln);	

extern bool TlnListerStart();

extern bool TlnListerNaechster(TTlnDaten *Tln);	

extern void TlnBuchInit();

#endif //def TELEXPHONE

#endif //ndef __TLNBUCH_H__