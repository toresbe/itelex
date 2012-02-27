#ifndef __TLNBUCH_H__

#define __TLNBUCH_H__

#include <stdbool.h>
#include <inttypes.h>

enum { TlnAdresseMax = 40 } ; //!< maximale Länge der Verbindungsadresse

typedef enum 
	{
	Geloescht = 0,
	TxpUrl = 1,
	TxpIP = 2
	} TTlnAdresseArt;
	
typedef struct
	{
	uint32_t Nummer; //!< Die Rufnummer, darf keine führenden Nullen enthalten
	TTlnAdresseArt AdrArt; //!< Was bedeutet die folgende Adresse
	char Adresse[TlnAdresseMax]; //!< URL, IP, eMail, ...
	uint32_t IPAdr; //!< bei eindeutiger IP-Adresse
	uint16_t Port; //!< bei abweichendem Port
	uint8_t Durchwahl; //!< interne Durchwahl bei "Nebenstellenanlagen"
	uint32_t Datum; //!< letzte Änderung der Adresse
	} TTlnDaten;
	
	
extern bool TlnSuche(uint32_t SucheNummer, bool AuchGeloescht, TTlnDaten *Tln);

extern bool TlnHinzufuegen(TTlnDaten *Tln);	


#endif //ndef __TLNBUCH_H__