#include <stdlib.h>
#include <string.h>

#include "system/net/ip.h"

#include "TlnBuch.h"

enum { TlnBuchMemMax = 10000 } ; //!< Größe des Teilnehmerverzeichnisses in Bytes


//! Speicher des Teilnehmerverzeichnisses.
// ---------------------------------------
//! Einträge sind nicht sortiert. Struktur pro Eintrag:
//! \par 4 Byte Teilnehmernummer (muss eindeutig sein). 0, wenn Eintrag komplett gelöscht.
//! \par 1 Byte Datensatzgröße insgesamt (einschließlich Teilnehmernummer und Größenangabe.
//! \par 1 Byte Art. (siehe enum \sa TTlnAdresseArt)
//! \par 4 Byte Datum.
//! \par Folgende Daten nur, wenn Art != Gelöscht
//! \par x Byte Adresse als String (mit \0 abgeschlossen) ODER 4 Byte IP-Adresse
//! \par 2 Byte Port
//! \par 1 Byte Durchwahl (0 bei keine Durchwahl).

static char TlnBuch[TlnBuchMemMax];


static uint16_t TlnBuchMemUsed; //!< Ende des genutzten Bereichs in TlnBuch.




//! Ermittelt die Größe eines Teilnehmereintrags.
static uint8_t TlnEintragGroesse(TTlnDaten *Tln)
	{
	switch (Tln->AdrArt)
		{
		case Geloescht: 
			return 4 + 1 + 1 + 4;
			
		case TxpUrl:
			return 4 + 1 + 1 + 4 + strlen(Tln->Adresse)+1 + 2 + 1;
			
		case TxpIP:
			return 4 + 1 + 1 + 4 + 4 + 2 + 1;
		
		default:
			return 255;
		}
	}
	
	
//! Füllt die Daten in TlnBuch.
static void TlnEintragen(TTlnDaten *Tln, char *BuchP)
	{
	uint8_t *SizeP;
	void *p;
	
	p = BuchP;
	*((uint32_t *) p) = Tln->Nummer; 					p += 4;
	SizeP = p; 											p += 1;
	*((uint8_t *) p) = (uint8_t) Tln->AdrArt;			p += 1;
	*((uint32_t *) p) = Tln->Datum; 					p += 4;
	switch (Tln->AdrArt)
		{
		case Geloescht: 
			break;
			
		case TxpUrl:
			strcpy(p, Tln->Adresse);					p += strlen(Tln->Adresse)+1;
			*((uint16_t *) p) = Tln->Port;				p += 2;
			*((uint8_t *) p) = Tln->Durchwahl;			p += 1;
			break;
			
		case TxpIP:
			*((uint32_t *) p) = Tln->IPAdr;				p += 4;
			*((uint16_t *) p) = Tln->Port;				p += 2;
			*((uint8_t *) p) = Tln->Durchwahl;			p += 1;
			break;
		
		default:
			*((uint8_t *) (p-5)) = (uint8_t) Geloescht; // nachträglich auf gelöscht ändern
			break;
		}
		
	*SizeP = ((char *) p) - BuchP;
	} // TlnEintragen
	
	
//! Holt die Daten aus dem TlnBuch.
static void TlnLesen(TTlnDaten *Tln, char *BuchP)
	{
	void *p;
	p = BuchP;
	Tln->Nummer = *((uint32_t *) p); 					p += 4;
														p += 1;
	Tln->AdrArt = (TTlnAdresseArt) *((uint8_t *) p);	p += 1;
	Tln->Datum = *((uint32_t *) p); 					p += 4;
	switch (Tln->AdrArt)
		{
		case Geloescht: 
			break;
			
		case TxpUrl:
			strcpy(Tln->Adresse, p);					p += strlen(Tln->Adresse)+1;
			Tln->Port = *((uint16_t *) p);				p += 2;
			Tln->Durchwahl = *((uint8_t *) p);			p += 1;
			break;
			
		case TxpIP:
			Tln->IPAdr = *((uint32_t *) p);				p += 4;
			Tln->Port = *((uint16_t *) p);				p += 2;
			Tln->Durchwahl = *((uint8_t *) p);			p += 1;
			break;
		
		default:
			Tln->AdrArt = Geloescht; // nachträglich auf gelöscht ändern
			break;
		}
		
	} // TlnEintragen
	
	
//! Suche eines Adressbuch-Eintrags.
// ---------------------------------
//! \param[in] SucheNummer Die Teilnehmernummer des gesuchten Anschlusses.
//! \retval Zeiger auf den Speicherbereich im Adressbuch.

char *TlnMemSuche(uint32_t SucheNummer)
	{
	char *p; 
	
	p = TlnBuch;
	while (p < TlnBuch + TlnBuchMemUsed)
		{
		if (SucheNummer == *((uint32_t *) p))
			return p;
		p += *((uint8_t *) (p+4));
		}
	return NULL;
	}


//! Suche eines Adressbuch-Eintrags.
// ---------------------------------
//! \param[in] SucheNummer Die Teilnehmernummer des gesuchten Anschlusses.
//! \param[in] AuchGeloescht Auch gelöschte Einträge suchen.
//! \param[out] Tln der ggf. gefundene Eintrag.
//! \retval true falls ein passender Eintrag gefunden wurde.

bool TlnSuche(uint32_t SucheNummer, bool AuchGeloescht, TTlnDaten *Tln)
	{
	char *p;
	
	p = TlnMemSuche(SucheNummer);
	if (p != NULL)
		{
		TlnLesen(Tln, p);
		return AuchGeloescht || (Tln->AdrArt != Geloescht);
		}
	else
		return false;
	}
	

//! Einfügen eines Adressbuch-Eintrags.
// ------------------------------------
//! ggf. wird ein alter Eintrag gelöscht
//! \param[in] Neuer / zu ändernder Tln 
//! \retval true falls erfolgreich
//! \retval false Speicher voll.

bool TlnHinzufuegen(TTlnDaten *Tln)
	{
	char *p;
	
	p = TlnMemSuche(Tln->Nummer);
	uint8_t NeuGr = TlnEintragGroesse(Tln);
	if (p == NULL)
		{
		if (TlnBuchMemUsed + NeuGr >= TlnBuchMemMax)
			return false;
		TlnEintragen(Tln, TlnBuch + TlnBuchMemUsed);
		TlnBuchMemUsed += NeuGr;
		return true;
		}
		
	uint8_t AltGr = *((uint8_t *) (p+4));
	if (NeuGr != AltGr)
		{
		if (TlnBuchMemUsed + NeuGr - AltGr >= TlnBuchMemMax)
			return false;
		memmove(p + NeuGr, p + AltGr, TlnBuchMemUsed - (p - TlnBuch) - AltGr);
		}
	TlnEintragen(Tln, p);
	return true;
	}


void TlnBuchTesteintrag(uint32_t nr, char *url, long ip, uint16_t port)
	{
	TTlnDaten TD;

	TD.Nummer = nr;
	if (url == NULL)
		{
		TD.AdrArt = TxpIP; 
		TD.IPAdr = ip;
		}
	else
		{
		TD.AdrArt = TxpUrl;
		strcpy(TD.Adresse, url);
		}
	TD.Port = port;
	TD.Durchwahl = 0;
	TD.Datum = 0;
	TlnHinzufuegen(&TD);
	}
	

void TlnBuchInit()
	{
	TlnBuchMemUsed = 0;
	
	// HACK Test
	TlnBuchTesteintrag(123, "sonnibs.no-ip.org", 0, 23);
	TlnBuchTesteintrag(32, 0, IPDOT(192l,168l,178l,32l), 23);
	}
	
	