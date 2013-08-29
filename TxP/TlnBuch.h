#ifndef __TLNBUCH_H__

#define __TLNBUCH_H__

#include "config.h"

#ifdef ITELEX

#include <stdbool.h>
#include <inttypes.h>

#include "TxP.h"

extern void TlnDatenInit(TTlnDaten *Tln);

extern bool TlnSuche(uint32_t SucheNummer, bool AuchGeloescht, TTlnDaten *Tln);

typedef enum { 
	TlnHinzDatumAktualisieren, //!< Datensatz wird in jedem Fall übernommen und mit aktuellem Datum versehen.
	TlnHinzNurNeuereUebernehmen, //!< Datensatz wird nur dann übernommen, wenn Daten neuer sind als alte.
	TlnHinzKopieren, //!< alle Daten werden so übernommen wie sie sind.
	} TTlnHinzufuegenModus;
	
extern int8_t TlnHinzufuegen(TTlnDaten *Tln, TTlnHinzufuegenModus HinzModus);	

//! Datentyp für die Speicherung der aktuellen Lister-Position
typedef struct
	{
	char* Pos; //!< Position in Puffer
	char Ref[10]; //!< Referenzmuster, wenn abweichend muss neu gestartet werden.
	} TTlnListerDat; 
	
extern bool TlnListerStart(TTlnListerDat *ldp);

extern bool TlnListerNaechster(TTlnListerDat *ldp, TTlnDaten *Tln);	

extern void TlnBuchInit();

#endif //def ITELEX

#endif //ndef __TLNBUCH_H__