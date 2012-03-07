#ifndef __TLNBUCH_H__

#define __TLNBUCH_H__

#include "config.h"

#ifdef TELEXPHONE

#include <stdbool.h>
#include <inttypes.h>

enum { TlnAdresseMax = 40 } ; //!< maximale Länge der Verbindungsadresse

typedef enum 
	{
	Geloescht = 0,
	TxpUrl = 1,
	TxpIP = 2,
	AsciiUrl = 3, //!< Telnet-ähnlich
	AsciiIP = 4
	} TTlnAdresseArt;
	
typedef struct
	{
	uint32_t Nummer; //!< Die Rufnummer, darf keine führenden Nullen enthalten
	uint16_t Flags; //!< Boolsche werte. Siehe TlnFlag_*
	TTlnAdresseArt AdrArt; //!< Was bedeutet die folgende Adresse
	char Adresse[TlnAdresseMax]; //!< URL, IP, eMail, ...
	long IPAdr; //!< bei eindeutiger IP-Adresse
	uint16_t Port; //!< bei abweichendem Port
	uint8_t Durchwahl; //!< interne Durchwahl bei "Nebenstellenanlagen"
	uint32_t Datum; //!< letzte Änderung der Adresse
	} TTlnDaten;
	
	
enum { TlnFlag_Lokal = 1 } ; //!< Diese Nummer wird nicht mit anderen Teilnehmern synchronisiert (TODO).
	
	
extern bool TlnSuche(uint32_t SucheNummer, bool AuchGeloescht, TTlnDaten *Tln);

extern bool TlnHinzufuegen(TTlnDaten *Tln);	

extern bool TlnListerStart();

extern bool TlnListerNaechster(TTlnDaten *Tln);	

extern void TlnBuchInit();

#endif //def TELEXPHONE

#endif //ndef __TLNBUCH_H__