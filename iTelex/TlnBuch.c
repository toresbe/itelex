#include <avr/pgmspace.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "system/net/ip.h"
#include "system/clock/clock.h"

#include "apps/httpd/cgibin/cgi-bin.h"
#include "apps/httpd/httpd2.h"
#include "apps/httpd/httpd2_pharse.h"
#include "system/config/eeconfig.h"

#include "iTelex.h"
#include "TlnBuch.h"
#include "TlnServer.h"
#include "SwTwi.h"
#include "Protokoll.h"
#include "BusKomm.h" // für WahlZuAdresse()

#include "CgiFormTools.h"
#include "StringTab.h"

#ifdef ITELEX_BASIS

#ifdef EXTMEM
enum { TlnBuchMemMax = 22000UL } ; //!< Größe des Teilnehmerverzeichnisses in Bytes
#else
enum { TlnBuchMemMax = 2048UL } ; //!< Größe des Teilnehmerverzeichnisses in Bytes
#endif

//! Speicher des Teilnehmerverzeichnisses.
// ---------------------------------------
//! Einträge sind nicht sortiert. Struktur pro Eintrag:
//! \par 4 Byte Teilnehmernummer (muss eindeutig sein). 0, wenn Eintrag komplett gelöscht.
//! \par 1 Byte Datensatzgröße insgesamt (einschließlich Teilnehmernummer und Größenangabe).
//! \par 2 Byte Flags.
//! \par 1 Byte Art. (siehe enum #TTlnAdresseArt)
//! \par x Byte Name as String (mit \0 abgeschlossen)
//! \par 4 Byte Datum.
//! \par Folgende Daten abhängig von Art
//! \par x Byte Adresse als String (mit \0 abgeschlossen) ODER 4 Byte IP-Adresse
//! \par 2 Byte Port
//! \par 1 Byte Durchwahl (0 bei keine Durchwahl).
//! \par 2 Byte DynPin (nur bei Typ = iTelexDynIP)


__attribute__ ((section (".noinit"))) static char TlnBuch[TlnBuchMemMax]; //!< Das Teilnehmer-Verzeichnis.

__attribute__ ((section (".noinit"))) static uint16_t TlnBuchMemUsed; //!< Ende des genutzten Bereichs in TlnBuch.


// Häufig benutzte Offsets:
enum { TBOffsGroesse = 4 } ; //!< Position der Eintragsgröße im Teilnehmer-Verzeichnis-Eintrag
enum { TBOffsFlags = 5 } ; //!< Position der Flags im Teilnehmer-Verzeichnis-Eintrag
enum { TBOffsArt = 7 } ; //!< Position der Art (s. #TTlnAdresseArt) im Teilnehmer-Verzeichnis-Eintrag
enum { TBOffsName = 8 } ; //!< Position der Art (s. #TTlnAdresseArt) im Teilnehmer-Verzeichnis-Eintrag


const PROGMEM char ExternEepromInit_P[] = "EEPROMOK";


//! Ermittelt die Größe eines bestehenden Teilnehmereintrags.
//-----------------------------------------------------------
static inline uint8_t TlnEintragGroesseB(char *tp)
	{
	return *((uint8_t *) (tp + TBOffsGroesse));
	}
	
	
//! Ermittelt die Größe eines neuen / geänderten Teilnehmereintrags.
//------------------------------------------------------------------
static uint8_t TlnEintragGroesse(TTlnDaten *Tln)
	{
	uint8_t Basis = 4 + 1 + 2 + 1 + strlen(Tln->Name)+1 + 4;
	
	switch (Tln->AdrArt)
		{
		case Geloescht: 
			return Basis;
			
		case iTelexHostname:
			return Basis + strlen(Tln->Adresse)+1 + 2 + 1;
			
		case iTelexIP:
			return Basis + 4 + 2 + 1;
		
		case iTelexDynIP:
			return Basis + 4 + 2 + 1 + 2;
		
		case AsciiHostname:
			return Basis + strlen(Tln->Adresse)+1 + 2;
			
		case AsciiIP:
			return Basis + 4 + 2;
		
		case eMail:
			return Basis + strlen(Tln->Adresse)+1;
			
		default:
			return 255;
		}
	}
	
	
//! Füllt die Daten in TlnBuch.
//! \retval true bei Änderung
static bool TlnEintragen(TTlnDaten *Tln, char *BuchP, bool DatumAktualisieren)
	{
	void *p;
	uint32_t *DatumP;
	bool Res = false;
	
	p = BuchP;
#define EINTRAG(typ, wert, size)			\
	{										\
	if (!Res && *((typ *) p) != (wert))		\
		Res = true;							\
	*((typ *) p) = (wert);					\
	p += size;								\
	}
	
#define EINTRAGSTR(wert)					\
	{										\
	if (!Res && strcmp(p, (wert)) != 0)		\
		Res = true;							\
	strcpy(p, (wert));						\
	p += strlen(wert)+1;					\
	}
	
	
	EINTRAG(uint32_t, Tln->Nummer, 4)
	EINTRAG(uint8_t, TlnEintragGroesse(Tln), 1)
	EINTRAG(uint16_t, Tln->Flags, 2)
	EINTRAG(uint8_t, (uint8_t) Tln->AdrArt, 1)
	EINTRAGSTR(Tln->Name)
	DatumP = p;
	EINTRAG(uint32_t, Tln->Datum, 4)
	
	switch (Tln->AdrArt)
		{
		case Geloescht: 
			break;
			
		case iTelexHostname:
			EINTRAGSTR(Tln->Adresse)
			EINTRAG(uint16_t, Tln->Port, 2)
			EINTRAG(uint8_t, Tln->Durchwahl, 1)
			break;
			
		case iTelexIP:
		case iTelexDynIP:
			EINTRAG(long, Tln->IPAdr, 4)
			EINTRAG(uint16_t, Tln->Port, 2)
			EINTRAG(uint8_t, Tln->Durchwahl, 1)
			if (Tln->AdrArt == iTelexDynIP)
				{
				EINTRAG(uint16_t, Tln->DynPin, 2)
				}
			break;
		
		case AsciiHostname:
			EINTRAGSTR(Tln->Adresse)
			EINTRAG(uint16_t, Tln->Port, 2)
			break;
			
		case AsciiIP:
			EINTRAG(long, Tln->IPAdr, 4)
			EINTRAG(uint16_t, Tln->Port, 2)
			break;
		
		case eMail:
			EINTRAGSTR(Tln->Adresse)
			break;
			
		default:
			*((uint8_t *) (p-5)) = (uint8_t) Geloescht; // nachträglich auf gelöscht ändern
			Res = true;
			Tln->AdrArt = Geloescht;
			break;
		}

	while (((char*)p - BuchP) != TlnEintragGroesse(Tln))
		; // Endlosschleife zur Fehlererkennung.
		
	if (Res && DatumAktualisieren)
		{
		struct TIME CurTime;
		CLOCK_GetTime(&CurTime);
		Tln->Datum = CurTime.time;
		*DatumP = CurTime.time;
		}
		
	return Res;
	} // TlnEintragen
	
	
//! Holt die Daten aus dem TlnBuch.
static void TlnLesen(TTlnDaten *Tln, char *BuchP)
	{
	void *p;
	p = BuchP;
	TlnDatenInit(Tln);
	Tln->Nummer = *((uint32_t *) p); 					p += 4;
														p += 1;
    Tln->Flags = *((uint16_t *) p);						p += 2;															
	Tln->AdrArt = (TTlnAdresseArt) *((uint8_t *) p);	p += 1;
	strcpy(Tln->Name, p);								p += strlen(Tln->Name)+1;
	Tln->Datum = *((uint32_t *) p); 					p += 4;
	Tln->DynPin = 0; // wird vielleicht wieder überschrieben.
	switch (Tln->AdrArt)
		{
		case Geloescht: 
			break;
			
		case iTelexHostname:
			strcpy(Tln->Adresse, p);					p += strlen(Tln->Adresse)+1;
			Tln->Port = *((uint16_t *) p);				p += 2;
			Tln->Durchwahl = *((uint8_t *) p);			p += 1;
			break;
			
		case iTelexIP:
		case iTelexDynIP:
			Tln->IPAdr = *((long *) p);					p += 4;
			Tln->Port = *((uint16_t *) p);				p += 2;
			Tln->Durchwahl = *((uint8_t *) p);			p += 1;
			if (Tln->AdrArt == iTelexDynIP)
				{
				Tln->DynPin = *((uint16_t *) p);		p += 2;
				}
			break;
		
		case AsciiHostname:
			strcpy(Tln->Adresse, p);					p += strlen(Tln->Adresse)+1;
			Tln->Port = *((uint16_t *) p);				p += 2;
			Tln->Durchwahl = 0;
			break;
			
		case AsciiIP:
			Tln->IPAdr = *((long *) p);					p += 4;
			Tln->Port = *((uint16_t *) p);				p += 2;
			Tln->Durchwahl = 0;
			break;
		
		case eMail:
			strcpy(Tln->Adresse, p);					p += strlen(Tln->Adresse)+1;
			Tln->Port = 0;
			Tln->Durchwahl = 0;
			break;
			
		default:
			Tln->AdrArt = Geloescht; // nachträglich auf gelöscht ändern
			break;
		}
		
	} // TlnEintragen
	
	
//! Initialisieren eines Adressbuch-Datensatzes
// --------------------------------------------
//! \param[out] Tln Zeiger auf den Datensatz-Puffer

void TlnDatenInit(TTlnDaten *Tln)
	{
	memset(Tln, 0, sizeof(TTlnDaten));
	}
	
	
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
		p += TlnEintragGroesseB(p);
		}
	return NULL;
	}


static bool TlnPlatzschaffenHatGeaendert;
//!< wird au ftrue gesetzt, wenn TlnPlatzschaffen() etwas verändert hat

//! Löscht einen oder mehrere Einträge für einen neuen bzw. aktualisierten Eintrag
static bool TlnPlatzschaffen(uint16_t NoetigerPlatz)
//! \param[in] NoetigerPlatz Anzahl zusätzlich benoetigter Bytes
	{
	char *SuchBuchP;
	char *AeltestBuchP;
	uint32_t AeltestDatum;
	TTlnDaten TD;

	TlnPlatzschaffenHatGeaendert = false;
	while (TlnBuchMemUsed >= TlnBuchMemMax - NoetigerPlatz)
		{
		SuchBuchP = TlnBuch;
		AeltestBuchP = NULL;
		while (SuchBuchP < TlnBuch + TlnBuchMemUsed)
			{
			TlnLesen(&TD, SuchBuchP);
			if ((TD.Flags & TlnFlag_Lokal) == 0 && (AeltestBuchP == NULL || TD.Datum < AeltestDatum))
				{
				AeltestBuchP = SuchBuchP;
				AeltestDatum = TD.Datum;
				}
			SuchBuchP += TlnEintragGroesseB(SuchBuchP);
			}

		if (AeltestBuchP == NULL)
			return false; // es gibt nichts zu loeschen

		if (ProtokollLevel >= AblaufInfo)
			ProtokollierenInt_P(PSTR("iTelex: Teilnehmer-Liste Platzmangel Eintrag %ul geloescht.\r\n"), *((uint32_t *)(AeltestBuchP)));

		uint8_t AeltestLen = TlnEintragGroesseB(AeltestBuchP);
		TlnBuchMemUsed -= AeltestLen;
		memmove(AeltestBuchP, AeltestBuchP + AeltestLen, TlnBuchMemUsed - (AeltestBuchP - TlnBuch));
		TlnPlatzschaffenHatGeaendert = true;
		}

	return true;	
	}


//! Suche eines Adressbuch-Eintrags.
// ---------------------------------
//! \param[in] SucheNummer Die Teilnehmernummer des gesuchten Anschlusses.
//! \param[in] AuchGeloescht Auch gelöschte Einträge suchen.
//! \param[out] Tln der ggf. gefundene Eintrag. Wenn NULL wird nur gefunden oder nicht ermittelt.
//! \retval true falls ein passender Eintrag gefunden wurde.

bool TlnSuche(uint32_t SucheNummer, bool AuchGeloescht, TTlnDaten *Tln)
	{
	char *p;
	
	p = TlnMemSuche(SucheNummer);
	if (p != NULL)
		{
		if (Tln != NULL)
			TlnLesen(Tln, p);
		return AuchGeloescht 
				|| (((TTlnAdresseArt) *((uint8_t *) (p + TBOffsArt))) != Geloescht);
		}
	else
		return false;
	}
	

//! Einfügen eines Adressbuch-Eintrags.
// ------------------------------------
//! ggf. wird ein alter Eintrag gelöscht
//! \param [in] Tln Neuer / zu ändernder Teilnehmereintrag.
//! \param [in] HinzModus siehe #TTlnHinzufuegenModus.
//! \retval 1 Eintrag hinzugefügt oder aktualisiert.
//! \retval 2 Vorhandener Eintrag ist neuer als der zu speichernde!
//! \retval 0 Eintrag unverändert.
//! \retval -1 Speicher voll.

int8_t TlnHinzufuegen(TTlnDaten *Tln, TTlnHinzufuegenModus HinzModus)
	{
	char *p;
	
NochmalVonVorn:
	p = TlnMemSuche(Tln->Nummer);
	uint8_t NeuGr = TlnEintragGroesse(Tln);
	if (p == NULL)
		{
		if (!TlnPlatzschaffen(NeuGr))
			return -1;
		TlnEintragen(Tln, TlnBuch + TlnBuchMemUsed, HinzModus == TlnHinzDatumAktualisieren); 
		TlnBuchMemUsed += NeuGr;
		return 1;
		}
		
	if (HinzModus == TlnHinzNurNeuereUebernehmen)
		{
		TTlnDaten BisherEintrag;
		TlnLesen(&BisherEintrag, p);
		if (BisherEintrag.Datum > Tln->Datum)
			return 2;
			
		if ((BisherEintrag.Flags & TlnFlag_Lokal) != 0)
			return 2;
			
		if (BisherEintrag.Datum == Tln->Datum)
			{
			//! \todo Prio 3 auch andere Daten vergleichen ???
			return 0;
			}
		}
		
	uint8_t AltGr = TlnEintragGroesseB(p);
	if (NeuGr != AltGr)
		{
		if (!TlnPlatzschaffen(NeuGr - AltGr))
			return -1;
		if (TlnPlatzschaffenHatGeaendert)
			goto NochmalVonVorn;
		memmove(p + NeuGr, p + AltGr, TlnBuchMemUsed - (p - TlnBuch) - AltGr);
		TlnBuchMemUsed += NeuGr - AltGr;
		TlnEintragen(Tln, p, HinzModus == TlnHinzDatumAktualisieren); // muss klappen ;-)
		return 1;
		}
	else
		return TlnEintragen(Tln, p, HinzModus == TlnHinzDatumAktualisieren) ? 1 : 0;
	}


//! Startet die sequentielle Abfrage aller Teilnehmereinträge.
//------------------------------------------------------------
//! \retval true wenn es mindestens einen Eintrag gibt.

bool TlnListerStart(TTlnListerDat *ldp)
	{
	//! ldp->Pos zeigt auf nächsten Eintrag, der durch TlnListerNaechster geliefert wird.
	ldp->Pos = TlnBuch;
	if (ldp->Pos >= TlnBuch + TlnBuchMemUsed)
		return false;
	memcpy(ldp->Ref, ldp->Pos, sizeof(ldp->Ref));
	return true;
	}
	

//! Sequentielle Abfrage aller Teilnehmereinträge.
//------------------------------------------------------------
//! \param[out] Tln gefundener Eintrag.
//! \retval true wenn ein weiterer Eintrag gefunden wurde.

bool TlnListerNaechster(TTlnListerDat *ldp, TTlnDaten *Tln)
	{
	while (true)
		{
		if (ldp->Pos >= TlnBuch + TlnBuchMemUsed)
			return false;

		if (memcmp(ldp->Ref, ldp->Pos, sizeof(ldp->Ref)) != 0)
			ldp->Pos = TlnBuch; // zur Not halt nochmal von vorn...
			
		TlnLesen(Tln, ldp->Pos);
		ldp->Pos += TlnEintragGroesseB(ldp->Pos);
		if (ldp->Pos < TlnBuch + TlnBuchMemUsed)
			memcpy(ldp->Ref, ldp->Pos, sizeof(ldp->Ref));
			
		if (Tln->Nummer != 0)
			return true;
			// Eintränge mit Nummer = 0 gleich überspringen
		}
	}


//! Vergleichsfunktion für die Suchfunktion.
//------------------------------------------
bool TlnSuchMusterPasst(char *SuchMuster, TTlnDaten *Tln)
	{
	if (SuchMuster[0] == '\0')
		return true; // es wird nichts konkretes gesucht.
	return strcasestr(Tln->Name, SuchMuster) != NULL;
	}
	
	
//! Hilfsfuntion zum Sortieren der Teilnehmer-Verzeichnis-Einträge.
//-----------------------------------------------------------------
//! Vergleicht nach Wahlnummer. 
//! \param p1 Zeiger auf ersten Eintrag
//! \param p2 Zeiger auf zweiten Eintrag
//! \retval -1, wenn erster Eintrag vor zweitem einzureihen ist.
//! \retval 0, wenn erster Eintrag vor zweitem einzureihen ist.
//! \retval 1, wenn erster Eintrag nach zweitem einzureihen ist.

static int8_t EintragVergleichNummer(char *p1, char *p2)
	{
	uint32_t Nr1, Nr2;
	uint8_t Ziffern1, Ziffern2;
	
	Nr1 = *((uint32_t *)(p1));
	Nr2 = *((uint32_t *)(p2));
	
	Ziffern1 = 0;
	while (Nr1 < 100000000)
		Nr1 *= 10, Ziffern1++;
	Ziffern2 = 0;
	while (Nr2 < 100000000)
		Nr2 *= 10, Ziffern2++;
		
	if (Nr1 < Nr2)
		return -1;
	else if (Nr1 == Nr2)
		{
		if (Ziffern1 > Ziffern2) // Es wurden bei Nr1 mehr Ziffern 'ergänzt', also ist die ursprüngliche Nummer kürzer
			return -1;
		else if (Ziffern1 == Ziffern2)
			return 0;
		else
			return 1;
		}
	else
		return 1;
	}


//! Hilfsfuntion zum Sortieren der Teilnehmer-Verzeichnis-Einträge.
//-----------------------------------------------------------------
//! Vergleicht nach Name. 
//! \param p1 Zeiger auf ersten Eintrag
//! \param p2 Zeiger auf zweiten Eintrag
//! \retval -1, wenn erster Eintrag vor zweitem einzureihen ist.
//! \retval 0, wenn erster Eintrag vor zweitem einzureihen ist.
//! \retval 1, wenn erster Eintrag nach zweitem einzureihen ist.

static int8_t EintragVergleichName(char *p1, char *p2)
	{
	char *Name1, *Name2;
	
	Name1 = p1 + TBOffsName;
	Name2 = p2 + TBOffsName;
	
	return strcasecmp(Name1, Name2);
	}


//! Hilfsfuntion zum Sortieren der Teilnehmer-Verzeichnis-Einträge.
//-----------------------------------------------------------------
//! Vergleicht nach letztem Änderungsdatum. 
//! \param p1 Zeiger auf ersten Eintrag
//! \param p2 Zeiger auf zweiten Eintrag
//! \retval -1, wenn erster Eintrag vor zweitem einzureihen ist.
//! \retval 0, wenn erster Eintrag vor zweitem einzureihen ist.
//! \retval 1, wenn erster Eintrag nach zweitem einzureihen ist.

static int8_t EintragVergleichDatum(char *p1, char *p2)
	{
	char *Name1, *Name2;
	
	Name1 = p1 + TBOffsName;
	Name2 = p2 + TBOffsName;

	uint32_t Datum1, Datum2;
	
	Datum1 = *((uint32_t *)(p1 + TBOffsName + strlen(Name1) + 1)); 
	Datum2 = *((uint32_t *)(p2 + TBOffsName + strlen(Name2) + 1));
		// Datum kommt gleich hinter dem Namen plus Null-Zeilen 
	
	if (Datum1 < Datum2)
		return -1;
	else if (Datum1 == Datum2)
		return 0;
	else
		return 1;
	}


typedef int8_t ( * SortierKritFunktion ) (char *, char *);


//! Eigentliche Sortierfunktion für das Teilnehmerverzeichnis.
//------------------------------------------------------------
//! Arbeitet nach dem Bubblesort-Prinzip. Zuerst wird der kleinste Eintrag
//! nach vorne geholt, dann der nächst-kleinste usw.
//! Als Zwischenpuffer für den Eintragstausch dient der freie Bereich 
//! hinter dem Ende der Liste.
//! \param Vergleich Anonyme Vergleichsfunktion für zwei Einträge.
//! \param Rueckwaerts wenn die Liste Absteigend sortiert sein soll.
//! \retval true, wenn Verzeichnis neu sortiert ist.
//! \retval false, wenn der Speicher zum Umsortieren nicht reicht.

static bool TlnBuchSortieren(SortierKritFunktion Vergleich, bool Rueckwaerts)
	{
	char *Kopf; // Aktuell oberstes Element (dort kommt der nächste Kleinste hin).
	char *p1; // Suchzeiger
	char *Kleinster; // Zeiger auf den kleinsten gefundenen.
	
NochmalVonVorne:
	Kopf = TlnBuch;
	while (Kopf < TlnBuch + TlnBuchMemUsed)
		{ // solange noch Einträge kommen...
		// Kleinsten suchen:
		Kleinster = Kopf;
		p1 = Kleinster + TlnEintragGroesseB(Kleinster);
		while (p1 < TlnBuch + TlnBuchMemUsed)
			{
			if (!Rueckwaerts && (*Vergleich)(p1, Kleinster) < 0)
				// neuen Kleinsten gefunden.
				Kleinster = p1;
			if (Rueckwaerts && (*Vergleich)(p1, Kleinster) > 0)
				// eigentlich den neuesten größten gefunden, aber durch Rueckwaerts ist es der kleinste ;-)
				Kleinster = p1;
			p1 += TlnEintragGroesseB(p1);
			}
		
		if (Kleinster != p1)
			{ // Kleinsten ganz nach vorne holen, dazu...
			// Hilfs-Platz prüfen:
			uint8_t KleinsterGroesse = TlnEintragGroesseB(Kleinster);

			if (!TlnPlatzschaffen(KleinsterGroesse))
				return false; // Abbruch wegen Speichermangel.

			if (TlnPlatzschaffenHatGeaendert)
				goto NochmalVonVorne;

			// Kleinsten auf Hilfs-Platz schieben:
			memmove(TlnBuch + TlnBuchMemUsed, Kleinster, KleinsterGroesse);
			// andere nach hinten schieben:
			memmove(Kopf + KleinsterGroesse, Kopf, Kleinster - Kopf);
			// Kleinsten von Hilfs-Platz an den Kopf holen:
			memmove(Kopf, TlnBuch + TlnBuchMemUsed, KleinsterGroesse);
			}
			
		// Kopf auf nächsten Eintrag:
		Kopf += TlnEintragGroesseB(Kopf);
		}
		
	return true; // Fertig!!!
	} // TlnVerzeichnisSortieren()
	
	
//! Errechnet aus dem aktuellen Wert von TlnBuchMemUsed einen Prüfwert,
//! um die Integrität des externen EEPROM zu testen.
//---------------------------------------------------------------------
//! Im externen EEPROM sind beide Werte ganz am Anfang abgelegt.

uint16_t MemUsedPruefwert(uint16_t groesse)
	{
	return (groesse ^ 0x4587) << 1; //### 8765
	}


// Folgende Variablen sind für die Prüfsummenberechnung.
// Die Prüfsumme wird im laufenden Betrieb ständig berechnet und aktualisiert.
// Nach Neustart wird die Prüfsumme auch berechnet und mit der gespeicherten verglichen.
// Ist das Ergebnis identisch, erfolgt KEINE Initialisierung des Teilnehmer-Verzeichnisses.	
	
static uint32_t PruefsummeIst; //!< Aktuelles Zwischenergebnis der Prüfsummenberechnung

__attribute__ ((section (".noinit"))) static uint32_t PruefsummeSoll; //!< Aktuelles Endergebnis der Prüfsummenberechnung

static uint16_t PruefsummeBerechnungIndex; //!< Aktuelle Position der Prüfsummenberechnung


//! Beginnt die Prüfsummenberechnung.
//------------------------------------

static void PruefsummeBerechnungStart()
	{
	PruefsummeIst = 0xDEADBEEF;
	PruefsummeBerechnungIndex = 0;
	}


//! Führt einen Schritt der Prüfsummenberechnung durch.
//-----------------------------------------------------
//! \retval true, wenn Berechnung beendet.

static bool PruefsummeBerechnungSchritt()
	{
	if (PruefsummeBerechnungIndex >= TlnBuchMemUsed)
		return true;
	uint8_t SregTemp = SREG;
	cli();
	PruefsummeIst ^= 0x04C11DB7;
	PruefsummeIst = (PruefsummeIst << 1) 
					+ ((PruefsummeIst & (1UL << 31)) ? 1 : 0);
	PruefsummeIst ^= TlnBuch[PruefsummeBerechnungIndex] << (PruefsummeIst & 0xf);
	PruefsummeBerechnungIndex++;
	SREG = SregTemp;
	return false;
	}
	
	
//! Führt im laufenden Betrieb einen Schritt der Prüfsummenberechnung durch.
//--------------------------------------------------------------------------

void TlnBuchPruefsummeBerechnenSchritt()
	{
	if (PruefsummeBerechnungSchritt())
		{ // fertig -> abspeichern.
		if (PruefsummeSoll != PruefsummeIst)
			{
			PruefsummeSoll = PruefsummeIst;
			if (ProtokollLevel >= AblaufInfo)
				ProtokollierenInt_P(PSTR("iTelex: Teilnehmer-Verzeichnis Pruefsumme aktualisiert auf %08lX.\r\n"), PruefsummeSoll);
			}
		PruefsummeBerechnungStart();
		}
	}

	
#define XEEPROM_TWI_ADR 0xA0 //!< TWI-Adresse des Externen Eeproms (Typ 24AT256)


//! Öffnet das externe Eeprom.
//----------------------------
//! Solange ein Schreibvorgang läuft, stellt sich das IC 24C256 tot.
//! Daher mehrere öffnungsversuche.
//! \param TwiAddr anzusprechende Adresse inkl. R/W-Bit.
//! \retval 1 ok
//! \retval 0 niemand da
//! \retval -1 Fehler
static int ExternEepromOeffnen(uint8_t TwiAddr)
	{
#if defined(LEDROT_EXTEEPROM)
	LED_on(ROT);
#endif //defined(LEDROT_EXTEEPROM)
	
	for (uint16_t TryCount = 0 ; TryCount < 10000 ; TryCount++)
		{
		bool Ack = true;
		if (!SwTwiStart()
			|| !SwTwiSendByte(TwiAddr, &Ack, false))
			{
			SwTwiForceStop();
			return -1;
			}
			
		if (Ack) 
			return 1;
		else
			SwTwiStop(false); // vorheriger Schreibprozess nicht abgeschlossen, daher stellt sich externes Eeprom "tot".
		}						
		
	return 0;
	}
	

//! Lädt das Teilnehmer-Verzeichnis aus dem externen EEPROM
//------------------------------------------------------------
int TlnBuchLadeVonExternEeprom()
	{
	bool Ack;
	uint16_t NeuGr;
	uint8_t lo, hi;
	int Res;

	// Eeprom Lesevorgang initialisieren --> Adresse schreiben
	Res = ExternEepromOeffnen(XEEPROM_TWI_ADR);
	if (Res < 0)
		{
#if defined(LEDROT_EXTEEPROM)
		LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
		return -__LINE__ + Res;
		}

	if (!SwTwiSendByte(0x00, &Ack, false) || !Ack
		|| !SwTwiSendByte(0x00, &Ack, false) || !Ack
		|| !SwTwiStop(false))
		{
		SwTwiForceStop();
#if defined(LEDROT_EXTEEPROM)
		LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
		return -__LINE__;
		}

	Res = ExternEepromOeffnen(XEEPROM_TWI_ADR+1); // +1 = read
	if (Res < 0)
		{
#if defined(LEDROT_EXTEEPROM)
		LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
		return -__LINE__ + Res;
		}

	// Gespeicherte Anzahl Byte laden
	if (!SwTwiReadByte(&lo, true, false)
		|| !SwTwiReadByte(&hi, true, false))
		{
		SwTwiForceStop();
#if defined(LEDROT_EXTEEPROM)
		LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
		return -__LINE__;
		}
		
	NeuGr = (hi << 8) | lo;
	
	// Prüfwert lesen
	if (!SwTwiReadByte(&lo, true, false)
		|| !SwTwiReadByte(&hi, true, false))
		{
		SwTwiForceStop();
#if defined(LEDROT_EXTEEPROM)
		LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
		return -__LINE__;
		}
		
	if (((hi << 8) | lo) != MemUsedPruefwert(NeuGr))
		{
		SwTwiStop(false);
#if defined(LEDROT_EXTEEPROM)
		LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
		return -__LINE__;
		}

	if (NeuGr == 0)
		{ // nichts weiter zu lesen
		SwTwiReadByte(&lo, false, false); // dummy
		SwTwiStop(false);
#if defined(LEDROT_EXTEEPROM)
		LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
		return 0;
		}
		
	// Speicher scheint ok, also geht's jetzt ans lesen...
	TlnBuchMemUsed = NeuGr;
	uint16_t TbAdr;

	for (TbAdr = 0 ; TbAdr < TlnBuchMemUsed - 1 ; TbAdr++) // -1, da das letzte Byte mit Ack = false zu lesen ist
		{
		if (!SwTwiReadByte((uint8_t*) &TlnBuch[TbAdr], true, false))
			{
			SwTwiForceStop();
			TlnBuchMemUsed = 0;
#if defined(LEDROT_EXTEEPROM)
			LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
			return -__LINE__;
			}
		}
		
	if (!SwTwiReadByte((uint8_t*) &TlnBuch[TbAdr], false, false))
		{
		SwTwiForceStop();
		TlnBuchMemUsed = 0;
#if defined(LEDROT_EXTEEPROM)
		LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
		return -__LINE__;
		}
		
	SwTwiStop(false);
#if defined(LEDROT_EXTEEPROM)
	LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
	return TlnBuchMemUsed;
	}
			
			
//! Speichert das Teilnehmer-Verzeichnis auf dem externen EEPROM
//--------------------------------------------------------------
//! \retval Anzahl Bytes im EEPROM oder negative Fehlernummer
int TlnBuchSpeichereAufExternEeprom()
	{
	bool Ack;
	uint16_t EeAdr;
	uint16_t TbAdr;
	int Res;
	uint8_t i;
	bool EeOpen;
	TTlnDaten TD;
	
	enum { EePageSize = 64 } ;

	Res = ExternEepromOeffnen(XEEPROM_TWI_ADR);
	if (Res < 0)
		{
#if defined(LEDROT_EXTEEPROM)
		LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
		return -__LINE__ + Res;
		}
		
	// Eeprom Speichervorgang initialisieren --> Adresse schreiben, Dummy-Länge 0 schreiben, Dummy-Prüfwert FF schreiben.
	if (!SwTwiSendByte(0x00, &Ack, false) || !Ack // Zugriffs-Adresse EEPROM
		|| !SwTwiSendByte(0x00, &Ack, false) || !Ack 
		|| !SwTwiSendByte(0x00, &Ack, false) || !Ack // Dummy-Länge
		|| !SwTwiSendByte(0x00, &Ack, false) || !Ack
		|| !SwTwiSendByte(0xFF, &Ack, false) || !Ack // Dummy-Prüfwert
		|| !SwTwiSendByte(0xFF, &Ack, false) || !Ack)
		{
		SwTwiForceStop();
#if defined(LEDROT_EXTEEPROM)
		LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
		return -__LINE__;
		}

	EeAdr = 4;
	EeOpen = true;
	TbAdr = 0;
	while (TbAdr < TlnBuchMemUsed)
		{
		TlnLesen(&TD, TlnBuch + TbAdr);
		if (TD.Nummer != 0 && TD.AdrArt != Geloescht)
			{ // Diesen Eintrag speichern
			for (i = TlnEintragGroesse(&TD) ; i > 0 ; i--)
				{ // Ein Byte speichern
				if (!EeOpen)
					{ // vorheriger Schreibvorgang abgeschlossen, neuen beginnen
					Res = ExternEepromOeffnen(XEEPROM_TWI_ADR);
					if (Res < 0)
						{
#if defined(LEDROT_EXTEEPROM)
						LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
						return -__LINE__ + Res;
						}

					// Zugriffsadresse senden
					if (!SwTwiSendByte(EeAdr >> 8, &Ack, false) || !Ack
						|| !SwTwiSendByte(EeAdr & 0xFF, &Ack, false) || !Ack)
						{
						SwTwiForceStop();
#if defined(LEDROT_EXTEEPROM)
						LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
						return -__LINE__;
						}
						
					EeOpen = true;
					} // if !EeOpen

				// Das Datenbyte ins externe Eeprom schreiben
				if (!SwTwiSendByte(TlnBuch[TbAdr], &Ack, false))
					{
					SwTwiForceStop();
#if defined(LEDROT_EXTEEPROM)
					LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
					return -__LINE__;
					}
					
				EeAdr++;
				TbAdr++;
				
				if ((EeAdr & (EePageSize-1)) == 0)
					{ // Block abschließen, da sonst 'Rollover'
					if (!SwTwiStop(false))
						{
						SwTwiForceStop();
#if defined(LEDROT_EXTEEPROM)
						LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
						return -__LINE__;
						}
					EeOpen = false;
					}
				} // for i; Ein Byte speichern
			} // if diesen Eintrag speichern
		else 
			{ // diesen Eintrag überspringen
			TbAdr += TlnEintragGroesse(&TD);
			}
		} // while (TbAdr < TlnBuchMemUsed)

	// letzten Schreibvorgang beenden
	if (EeOpen)
		{
		if (!SwTwiStop(false))
			{
			SwTwiForceStop();
#if defined(LEDROT_EXTEEPROM)
			LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
			return -__LINE__;
			}
		EeOpen = false;
		}
		
	// jetzt die wirkliche Größe schreiben

	EeAdr -= 4; // - 4, da die anfänglichen Einträge (Größe, Prüfwert) mit drin sind.
	uint16_t pruef = MemUsedPruefwert(EeAdr); 

	// Öffnen kann schiefgehen, solange vorheriger Schreibprozess noch läuft
	Res = ExternEepromOeffnen(XEEPROM_TWI_ADR);
	if (Res < 0)
		{
#if defined(LEDROT_EXTEEPROM)
		LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
		return -__LINE__ + Res;
		}

	if (!SwTwiSendByte(0x00, &Ack, false) || !Ack // Zugriffs-Adresse EEPROM
		|| !SwTwiSendByte(0x00, &Ack, false) || !Ack 
		|| !SwTwiSendByte(EeAdr & 0xFF, &Ack, false) || !Ack // wirkliche Länge
		|| !SwTwiSendByte(EeAdr >> 8, &Ack, false) || !Ack
		|| !SwTwiSendByte(pruef & 0xFF, &Ack, false) || !Ack // prüfwert
		|| !SwTwiSendByte(pruef >> 8, &Ack, false) || !Ack)
		{
		SwTwiForceStop();
#if defined(LEDROT_EXTEEPROM)
		LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
		return -__LINE__;
		}

	if (!SwTwiStop(false))
		{
		SwTwiForceStop();		
#if defined(LEDROT_EXTEEPROM)
		LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
		return -__LINE__;
		}
		
#if defined(LEDROT_EXTEEPROM)
	LED_off(ROT);
#endif //defined(LEDROT_EXTEEPROM)
	return EeAdr;
	}

	
	
//! Hilfsfunktion für die Darstellung des Teilnehmer-Verzeichnisses als Tabelle.
//------------------------------------------------------------------------------
//! \param Sprache Sprachindex
//! \param Freigegeben true für vollständige Ausgabe

static void TlnBuchTabelleAusgabe(TSprache Sprache, bool Freigegeben)
	{
	// Anzeige der Teilnehmerliste...
	//   wenn TlnBuchOffen 
	//	 oder wenn Freigegeben immer vollständige Liste
	//	 sonst wenn TlnServer aktiv zumindest die öffentlichen Einträge
	
	// Startseite = Liste
	// ==================================================
	printf_P(PSTR("<form action=\"itelex-tlnverz.cgi\">"));
	
	if (TlnBuchOffen || Freigegeben) // NULL fragt nicht wieder nach einem Kennwort
		printf_P(ISTR(UeberschriftTeilnehmerverzeichnis, Sprache));
	else
		printf_P(ISTR(UeberschriftOeffentlichesTeilnehmerverzeichnis, Sprache));

	if (!Freigegeben)
		{
		printf_P(PSTR("<a href=\"itelex-tlnverz.cgi?allezeigen\">"));
		printf_P(ISTR(VollstaendigesTeilnehmerverzeichnis, Sprache));
		printf_P(PSTR("</a><br>"));
		}
	
	printf_P(ISTR(TeilnehmerverzeichnisHtmlKopf, Sprache));
		
	TTlnDaten TD;
	TlnDatenInit(&TD);
	
	struct TIME Time;
	CLOCK_GetTime(&Time); // holt auch die aktuelle Zeitzone
	uint32_t AktZeit = Time.time;

	char Hilf[10];
	
	TTlnListerDat LD;
	if (TlnListerStart(&LD))
		{
		while (TlnListerNaechster(&LD, &TD))
			{
			if (!TlnBuchOffen && (TD.Flags & TlnFlag_Lokal) != 0 && !Freigegeben)
				continue; // Private Einträge nicht darstellen.
				
			if (TD.AdrArt == Geloescht && TD.Datum < AktZeit - 7L * 24 * 60 * 60) // Mehr als 7 Tage alte Einträge mit "gelöscht" nicht mehr darstellen.
				continue;
			
			printf_P(PSTR("<tr><td align=\"left\">%ld</td>"), TD.Nummer); // Nummer
			printf_P(PSTR("<td align=\"left\">%s</td><td>&#160;"), TD.Name); // name
			if ((TD.Flags & TlnFlag_Lokal) != 0)
				{
				printf_P(ISTR(TlnverzAttrLokal, Sprache));
				printf_P(PSTR(" "));
				}
			if ((TD.Flags & TlnFlag_Gesperrt) != 0)
				{
				printf_P(ISTR(TlnverzAttrGesperrt, Sprache));
				printf_P(PSTR(" "));
				}
			if (TD.AdrArt == iTelexDynIP)
				{
				printf_P(ISTR(TlnverzAttrDyn, Sprache));
				printf_P(PSTR(" "));
				}
				
			printf_P(PSTR("</td>" // Ende Besonderheiten
						  "<td align=\"left\">")); // Beginn Typ
			
			switch (TD.AdrArt)
				{
				case iTelexIP:
				case iTelexDynIP:
					iptostr(TD.IPAdr, TD.Adresse);
					// weiter mit iTelexHostname!
				case iTelexHostname:
					AdresseZuWahlStr(TD.Durchwahl << 1, Hilf);
					printf_P(ISTR(TypITelex, Sprache));
					printf_P(PSTR("</td>"
						"<td align=\"left\"><a href=\"http://%s\" target=\"_blank\">%s</a></td>" // Adresse
						"<td align=\"center\">%u</td>" // Port
						"<td align=\"center\">%s</td>" // Durchwahl
						), TD.Adresse, TD.Adresse, TD.Port, Hilf);
					break;

				case AsciiIP:
					iptostr(TD.IPAdr, TD.Adresse);
					// weiter mit AsciiHostname!
				case AsciiHostname:
					printf_P(ISTR(TypAscii, Sprache));
					printf_P(PSTR("</td>"
						"<td align=\"left\">%s</td>" // Adresse
						"<td align=\"center\">%u</td>" // Port
						"<td>&#160;</td>" // Durchwahl
						), TD.Adresse, TD.Port);
					break;

				case eMail:
					printf_P(ISTR(TypEMail, Sprache));
					printf_P(PSTR("</td>"
						"<td align=\"left\">%s</td>" // Adresse
						"<td align=\"center\">&#160;</td>" // Port
						"<td>&#160;</td>" // Durchwahl
						), TD.Adresse);
					break;

				default:
					printf_P(ISTR(TypGeloescht, Sprache));
					printf_P(PSTR("</td><td>&#160;</td><td>&#160;</td><td>&#160;</td>"));
					break;
				} // switch (TD.AdrArt)
				
			// Datum / Uhrzeit...
			Time.time = TD.Datum;
			CLOCK_decode_time(&Time);
			
			printf_P(PSTR("<td align=\"center\">%02u.%02u.%04u %02d:%02d:%02d</td>"), Time.DD, Time.MM, Time.YY, Time.hh, Time.mm, Time.ss);
			
			if (Freigegeben)
				{
				printf_P(PSTR("<td><a href=\"itelex-tlnverz.cgi?edit=%ld\">"), TD.Nummer);
				printf_P(ISTR(AktionAendern, Sprache));
				printf_P(PSTR("</a></td></tr>"));
				}
			else
				printf_P(PSTR("<td>&#160;</td></tr>"));
			
			} // while (TlnListerNaechster(&LD, &TD))

		if (Freigegeben)
			{
			printf_P(PSTR( "<tr><td>&#160;</td><td>&#160;</td><td>&#160;</td><td>&#160;</td><td>&#160;</td><td>&#160;</td><td>&#160;</td><td>&#160;</td>"
						   "<td><a href=\"itelex-tlnverz.cgi?edit=0\">"));
			printf_P(ISTR(AktionHinzufuegen, Sprache));
			printf_P(PSTR("</a></td></tr></table>"));
			printf_P(ISTR(TeilnehmerverzeichnisAktionenOffen, Sprache));
			printf_P(PSTR("</form>"));
			}
		else
			printf_P(PSTR( "</table></form>") );
		
		} // Teilnehmerverzeichnis nicht leer
	else
		{
		printf_P(PSTR("</table>"));
		printf_P(ISTR(TeilnehmerverzeichnisAktionenLeerOffen, Sprache));
		}

	} // TlnBuchTabelleAusgabe()
	
	
//! CGI-Funktion für die Anzeige des Teilnehmerverzeichnisses.
//------------------------------------------------------------
void TlnBuch_Anzeige_CGI(void *pStruct)
	{
	// folgende Namen sind nur Intern und nicht zu übersetzen.
	static PROGMEM const char AlleZeigen_P[] = "allezeigen";
	static PROGMEM const char Edit_P[] = "edit";
	static PROGMEM const char Nummer_P[] = "nummer";
	static PROGMEM const char AltNummer_P[] = "altnummer";
	static PROGMEM const char Name_P[] = "name";
	static PROGMEM const char Adresse_P[] = "adresse";
	static PROGMEM const char Port_P[] = "port";
	static PROGMEM const char Durchwahl_P[] = "durchwahl";
	static PROGMEM const char Typ_P[] = "type";
	static PROGMEM const char Lokal_P[] = "local";
	static PROGMEM const char Gesperrt_P[] = "lock";
	static PROGMEM const char Datum_P[] = "datum";
	static PROGMEM const char Save_P[] = "save";
	static PROGMEM const char Clear_P[] = "clear";
	static PROGMEM const char Load_P[] = "load";
	static PROGMEM const char Sortiere_P[] = "sort";
	static PROGMEM const char SortAb_P[] = "ab";

	static TSprache Sprache;

	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	TTlnDaten TD;
	char Hilf[10];
	bool Zurueck = false; // wird auf true gesetzt, wenn ein "zurück"-Text gedruckt werden soll.

	PruefeSprache(pStruct, &Sprache);	

	bool FreigabePruefung = true; // Merker, ob eine Konfigurationsfreigabe gebraucht wird.
	if (http_request->argc == 0 
		|| PharseCheckName_P(http_request, Sortiere_P))
		// erster Aufruf
		#ifdef ITELEX_TLNSERVER
			FreigabePruefung = !TlnBuchOffen && TlnServSyncGeheimzahl == 0;
		#else //ndef ITELEX_TLNSERVER
			FreigabePruefung = !TlnBuchOffen;
		#endif //def ITELEX_TLNSERVER
	else 
		FreigabePruefung = true; // alle Aktionen (inkl. "AlleZeigen" mit Kennwort-Abfrage
		
	if (FreigabePruefung)
		{
		if (!KonfigFreigabe(pStruct, Sprache, true))
			return; // verboten.
		}
	
	cgi_PrintHttpheaderStart();

	if (http_request->argc == 0 || PharseCheckName_P(http_request, AlleZeigen_P))
		{ 
		TlnBuchTabelleAusgabe(Sprache, KonfigFreigabe(pStruct, Sprache, false));
		} // argc == 0 --> gesamte Liste ausgeben

	else if (PharseCheckName_P(http_request, Sortiere_P))
		{ 
		// sortieren (ohne Kennwort-Abfrage möglich)
		// ==================================================
		bool Rueckwaerts = PharseCheckName_P(http_request, SortAb_P);
		char *Krit = http_request->argvalue[PharseGetValue_P(http_request, Sortiere_P)];
		bool Res;
		
		if (strcmp_P(Krit, Nummer_P) == 0)
			Res = TlnBuchSortieren(EintragVergleichNummer, Rueckwaerts);
		else if (strcmp_P(Krit, Name_P) == 0)
			Res = TlnBuchSortieren(EintragVergleichName, Rueckwaerts);
		else if (strcmp_P(Krit, Datum_P) == 0)
			Res = TlnBuchSortieren(EintragVergleichDatum, Rueckwaerts);
		else
			{
			printf_P(ISTR(UngueltigerCgiAufruf, Sprache), http_request->HTTP_LINEBUFFER);
			Zurueck = true;
			Res = true;
			}
			
		if (!Res)
			{
			printf_P(ISTR(InternesVerzeichnisVoll, Sprache));
			Zurueck = true;
			}

		if (!Zurueck)
			// Bei Fehlern wird 'Zurueck' gesetzt, sonst bleibt es auf False und die Tabelle ist auszugeben.
			TlnBuchTabelleAusgabe(Sprache, KonfigFreigabe(pStruct, Sprache, false));
			
		} // if (PharseCheckName_P(http_request, Sortiere_P))
		
	else if (PharseCheckName_P(http_request, Edit_P))
		{ 
		// Ändern ODER Neu --> Eingabeformular anzeigen und füllen.
		// =========================================================
		TD.Nummer = atol(http_request->argvalue[PharseGetValue_P(http_request, Edit_P)]);
		if (TD.Nummer == 0 || !TlnSuche(TD.Nummer, true, &TD))
			{ // neuen oder nicht gefundenen Eintrag initialisieren.
			TD.Name[0] = '\0';
			TD.Adresse[0] = '\0';
			TD.AdrArt = iTelexHostname;
			TD.Port = ITELEX_PORT;
			TD.Durchwahl = 0;
			TD.Flags = 0;
			#ifdef ITELEX_ANSCHLUSS
			TD.Flags |= (TlnFlag_Lokal);
			#endif //def ITELEX_ANSCHLUSS
			}

		CgiFormStartTabbed_P(PSTR("itelex-tlnverz.cgi"));

		CgiFormInputFieldULong_P(ISTR(Rufnummer, Sprache), Nummer_P, 10, TD.Nummer);

		printf_P(PSTR("<input name=\"altnummer\" type=\"hidden\" value=\"%ld\">"), TD.Nummer);

		CgiFormInputFieldText_P(ISTR(Name, Sprache), Name_P, TlnNameMax-1, TD.Name);
	
		CgiFormCheckbox_P(ISTR(TlnverzAttrLokal, Sprache), Lokal_P, (TD.Flags & TlnFlag_Lokal) != 0);

		CgiFormCheckbox_P(ISTR(TlnverzAttrGesperrt, Sprache), Gesperrt_P, (TD.Flags & TlnFlag_Gesperrt) != 0);

		const char *TypSelList[4];
		TypSelList[0] = ISTR(TypGeloescht, Sprache);
		TypSelList[1] = ISTR(TypITelex, Sprache);
		TypSelList[2] = ISTR(TypAscii, Sprache);
		TypSelList[3] = ISTR(TypEMail, Sprache);
		
		uint8_t TypSelNr;
		switch (TD.AdrArt)
			{
			case iTelexIP:
			case iTelexDynIP:
			case iTelexHostname: 	TypSelNr = 1; break;
			case AsciiIP:
			case AsciiHostname: 	TypSelNr = 2; break;
			case eMail:	            TypSelNr = 3; break;
			default:                TypSelNr = 0; break;
			}
		CgiFormDropdown_P(ISTR(Typ, Sprache), Typ_P, 4, TypSelList, TypSelNr);
		
		if (TD.AdrArt == iTelexIP || TD.AdrArt == iTelexDynIP || TD.AdrArt == AsciiIP)
			iptostr(TD.IPAdr, TD.Adresse);
		CgiFormInputFieldText_P(ISTR(Adresse, Sprache), Adresse_P, TlnAdresseMax-1, TD.Adresse);
		CgiFormInputFieldULong_P(ISTR(Port, Sprache), Port_P, 5, TD.Port);
		AdresseZuWahlStr(TD.Durchwahl << 1, Hilf);
		CgiFormInputFieldText_P(ISTR(Durchwahl, Sprache), Durchwahl_P, 2, Hilf);

		if (TD.Nummer == 0)
			CgiFormFinish_P(ISTR(AktionHinzufuegen, Sprache));
		else
			CgiFormFinish_P(ISTR(AktionAendern, Sprache));
		Zurueck = true;
		} // if (PharseCheckName_P(http_request, Edit_P)): Ändern oder Neu

	else if (PharseCheckName_P(http_request, Nummer_P))
		{ // Eine Nummer ist angegeben, dass kann nur das Ergebnis eines Änderungs- oder Hinzufüge-Wunsches sein.
		// neuen Einfügen oder geänderten Aktualisieren
		// ==================================================
		bool DatenOk = true; // nur wenn gesetzt, wird auch gespeichert
		uint32_t AltNummer = atol(http_request->argvalue[PharseGetValue_P(http_request, AltNummer_P)]);
		uint32_t NeuNummer = atol(http_request->argvalue[PharseGetValue_P(http_request, Nummer_P)]);
		uint8_t TypSelNr = atoi(http_request->argvalue[PharseGetValue_P(http_request, Typ_P)]);
		
		// brauche alte Geheimzahl und alten Typ
		if (NeuNummer == 0 || !TlnSuche(NeuNummer, true, &TD))
			{ // hat doch nicht geklappt
			TD.AdrArt = 0;
			TD.DynPin = 0;
			}

		TD.Nummer = NeuNummer;
		strncpy(TD.Name, http_request->argvalue[PharseGetValue_P(http_request, Name_P)], TlnNameMax-1);
		TD.Flags = 0;
		
		strncpy(TD.Adresse, http_request->argvalue[PharseGetValue_P(http_request, Adresse_P)], TlnAdresseMax-1);
		TD.Adresse[TlnAdresseMax-1] = '\0'; // sicherheitshalber abhacken.

		if (TD.Nummer == 0)
			{
			printf_P(ISTR(Rufnummer0NichtErlaubt, Sprache));
			DatenOk = false;
			}
		else
			{
			printf_P(ISTR(MeldungTlneintragRufnummer, Sprache), TD.Nummer);
			if (AltNummer == 0)
				printf_P(ISTR(AktionHinzufuegen, Sprache));
			else if (AltNummer != TD.Nummer)
				printf_P(ISTR(EhemalsLong, Sprache), AltNummer);
			printf_P(PSTR("<br>"));
			printf_P(ISTR(Name, Sprache));
			printf_P(PSTR(": %s<br>"), TD.Name);
			}
		
		if (PharseCheckName_P(http_request, Lokal_P))
			{
			if (atoi(http_request->argvalue[PharseGetValue_P(http_request, Lokal_P)]) != 0)
				TD.Flags |= TlnFlag_Lokal;
			}
		if ((TD.Flags & TlnFlag_Lokal) != 0)
			{
			printf_P(ISTR(TlnverzAttrLokal, Sprache));
			printf_P(PSTR("<br>"));
			}

		if (PharseCheckName_P(http_request, Gesperrt_P))
			{
			if (atoi(http_request->argvalue[PharseGetValue_P(http_request, Gesperrt_P)]) != 0)
				TD.Flags |= TlnFlag_Gesperrt;
			}
		if ((TD.Flags & TlnFlag_Gesperrt) != 0)
			{
			printf_P(ISTR(TlnverzAttrGesperrt, Sprache));
			printf_P(PSTR("<br>"));
			}

		if (TD.Adresse[0] == '\0' || TypSelNr == 0)
			// Leere Adresse --> löschen
			{
			TD.AdrArt = Geloescht;
			printf_P(ISTR(TypGeloescht, Sprache));
			printf_P(PSTR("<br>"));
			}
		else
			{
			TD.IPAdr = strtoip(TD.Adresse);
			
			if (TypSelNr == 1)
				{
				if (TD.IPAdr == 0)
					{
					TD.AdrArt = iTelexHostname;
					printf_P(ISTR(TypITelex, Sprache));
					printf_P(ISTR(HostnameZusatz, Sprache), TD.Adresse);
					}
				else
					{
					if (TD.AdrArt == iTelexDynIP && AltNummer == TD.Nummer)
						{ // der alte (!) Eintrag war ein Eintrag zu dynamischer IP-Aktualisierung
						// es ist nix zu ändern, auch die Pin bleibt unverändert.
						}
					else
						TD.AdrArt = iTelexIP;
					iptostr(TD.IPAdr, TD.Adresse); // und wieder zurück wandeln
					printf_P(ISTR(TypITelex, Sprache));
					printf_P(ISTR(IPZusatz, Sprache), TD.Adresse);
					}
				TD.Port = atoi(http_request->argvalue[PharseGetValue_P(http_request, Port_P)]);
				strncpy(Hilf, http_request->argvalue[PharseGetValue_P(http_request, Durchwahl_P)], 2);
				Hilf[2] = '\0';
				// Leerzeichen löschen:
				if (Hilf[1] == ' ')
					Hilf[1] = '\0';
				if (Hilf[0] == ' ')
					{
					Hilf[0] = Hilf[1];
					Hilf[1] = '\0';
					}
				TD.Durchwahl = WahlZuAdresse(atoi(Hilf), strlen(Hilf)) >> 1;
				if (TD.Durchwahl == 110) 
					TD.Durchwahl = 0; // eingabe von WahlZuAdresse(0) = 110
				AdresseZuWahlStr(TD.Durchwahl << 1, Hilf);
				printf_P(ISTR(Port, Sprache));
				printf_P(PSTR(" %u "), TD.Port);
				printf_P(ISTR(Durchwahl, Sprache));
				printf_P(PSTR(" %s (%u)<br>"), Hilf, TD.Durchwahl);
				}
				
			else if (TypSelNr == 2)
				{
				if (TD.IPAdr == 0)
					{
					TD.AdrArt = AsciiHostname;
					printf_P(ISTR(TypAscii, Sprache));
					printf_P(ISTR(HostnameZusatz, Sprache), TD.Adresse);
					}
				else
					{
					TD.AdrArt = AsciiIP;
					iptostr(TD.IPAdr, TD.Adresse); // und wieder zurück wandeln
					printf_P(ISTR(TypAscii, Sprache));
					printf_P(ISTR(IPZusatz, Sprache), TD.Adresse);
					}
				TD.Port = atoi(http_request->argvalue[PharseGetValue_P(http_request, Port_P)]);
				TD.Durchwahl = 0;
				printf_P(ISTR(Port, Sprache));
				printf_P(PSTR(" %u<br>"), TD.Port);
				}

			else if (TypSelNr == 3)
				{
				printf_P(ISTR(TypEMail, Sprache));
				printf_P(PSTR(": "));
				printf_P(ISTR(Adresse, Sprache));
				printf_P(PSTR(" %s<br>"), TD.Adresse);
				TD.AdrArt = eMail;
				TD.Port = 0;
				TD.Durchwahl = 0;
				}
				
			else
				{
				printf_P(ISTR(TypUnbekannt, Sprache));
				DatenOk = false;
				}
			}

		if (TD.AdrArt != iTelexDynIP)
			TD.DynPin = 0; 
			
		if (DatenOk && TD.AdrArt == Geloescht && AltNummer == 0)
			{ // einen neuen Lösch-Eintrag anzulegen ist doof
			printf_P(ISTR(KeineAenderung, Sprache));
			DatenOk = false;
			}
			
		if (DatenOk && TD.Nummer != AltNummer && TlnSuche(TD.Nummer, false, NULL))
			{
			printf_P(ISTR(RufnummerDoppelt, Sprache));
			DatenOk = false;
			}
			
		if (DatenOk)
			{ // speichern oder löschen
			int8_t Res = TlnHinzufuegen(&TD, TlnHinzDatumAktualisieren);
			if (Res >= 0)
				{
				if (Res > 0)
					{
					printf_P(ISTR(EintragGespeichert, Sprache));
#ifdef ITELEX_TLNSERVER
					TlnServTlnbuchEintragGeaendert(&TD, -1); // -1: Änderung kommt von keinem Server
#endif //def ITELEX_TLNSERVER
					}
				else
					printf_P(ISTR(EintragUnveraendert, Sprache));
				
				if (TD.Nummer != AltNummer && AltNummer != 0)
					{
					TD.Nummer = AltNummer;
					TD.AdrArt = Geloescht;
					if (TlnHinzufuegen(&TD, TlnHinzDatumAktualisieren) < 0)
						{
						printf_P(ISTR(AlteNummerNichtGeloescht, Sprache), AltNummer);	
						}
					}
				}
			else
				{ // TlnHinzufuegen() < 0
				printf_P(PSTR("<b>"));
				printf_P(ISTR(TeilnehmerlisteVoll, Sprache));
				printf_P(PSTR("</b><br>"));
				}
			} // if DatenOk
		Zurueck = true;
		} // if (PharseCheckName_P(http_request, Nummer_P)) ; also neuen Einfügen oder geänderten Aktualisieren
		
	else if (PharseCheckName_P(http_request, Save_P))
		{ 
		// auf externem Eeprom speichern
		// ==================================================
		int Res = TlnBuchSpeichereAufExternEeprom();
		if (Res < 0)
			printf_P(ISTR(EepromSpeicherFehler, Sprache), Res, SwTwiLetzterFehler);
		else
			{
			printf_P(ISTR(EepromSpeicherErfolg, Sprache), Res);
			
			// Flag, dass das externe EEPROM eigentlich "korrekt" sein sollte, prüfen und ggf. setzen
			if (!ReadConfigBool(ExternEepromInit_P, false)) 
				changeConfig_P(ExternEepromInit_P, "1");
			}
		Zurueck = true;
		} // if (PharseCheckName_P(http_request, Save_P))
		
	else if (PharseCheckName_P(http_request, Load_P))
		{ 
		// von externem Eeprom laden
		// ==================================================
		int Res = TlnBuchLadeVonExternEeprom();
		if (Res < 0)
			printf_P(ISTR(EepromLadenFehler, Sprache), Res, SwTwiLetzterFehler);
		else
			printf_P(ISTR(EepromLadenErfolg, Sprache), Res);
		Zurueck = true;
		} // if (PharseCheckName_P(http_request, Load_P))
		
	else if (PharseCheckName_P(http_request, Clear_P))
		{ 
		// komplett löschen
		// ==================================================
		printf_P(ISTR(KomplettGeloescht, Sprache));
		TlnBuchMemUsed = 0;
		Zurueck = true;
		} // if (PharseCheckName_P(http_request, Clear_P))
		
	else
		{ 
		// nicht erkannt
		// ==================================================
		printf_P(ISTR(UngueltigerCgiAufruf, Sprache), http_request->HTTP_LINEBUFFER);
		// HACK TEST:
			printf_P(PSTR("<br>argc = %d, argv1 = %s"), http_request->argc, http_request->argvalue[0]);
		Zurueck = true;
		}
		
	if (Zurueck)
		printf_P(ISTR(ZurueckZumTeilnehmerverzeichnis, Sprache));
	
	cgi_PrintHttpheaderEnd();

	} // TlnBuch_Anzeige_CGI()
	

/*
static void TlnBuchTesteintrag(uint32_t nr, TTlnAdresseArt art, char *url, long aip, uint16_t port, uint8_t dw)
	{
	TTlnDaten TD;

	TD.Nummer = nr;
	TD.AdrArt = art; 
	TD.IPAdr = aip;
	strcpy(TD.Adresse, url);
	TD.Port = port;
	TD.Durchwahl = dw;
	TD.Datum = 0;
	TD.Flags = 0;
	TlnHinzufuegen(&TD);
	}
*/
	

//! Initialisiert die Liste der Teilnehmereinträge.
//------------------------------------------------------------

void TlnBuchInit()
	{
	SwTwiInit();
	
	cgi_RegisterCGI( TlnBuch_Anzeige_CGI, PSTR("itelex-tlnverz.cgi"));
	
	if (get_Taste()) // high vom Pullup -> Taste nicht gedrückt
		{
		// zuerst prüfen, ob vorhandener Inhalt noch korrekt. 
		if (TlnBuchMemUsed <= TlnBuchMemMax)
			{
			PruefsummeBerechnungStart();
			while (!PruefsummeBerechnungSchritt())
				; // warten bis fertig.
			if (PruefsummeIst == PruefsummeSoll) // offensichtlich alles noch ok.
				{
				Protokollieren_P(PSTR("iTelex: * Teilnehmer-Verzeichnis wird unveraendert uebernommen.\r\n"));
				return; // nichts weiter tun.
				}
			}
			
		// sonst aus dem EEPROM-Speicher laden.
		TlnBuchMemUsed = 0;
		int Res = TlnBuchLadeVonExternEeprom();
		if (Res < 0)
			{ 
			ProtokollierenInt_P(PSTR("iTelex: ! Eeprom Ladefehler %d"), Res);
			ProtokollierenInt_P(PSTR(" / %02X\r\n"), SwTwiLetzterFehler);
			if (ReadConfigBool(ExternEepromInit_P, false)) // Flag, dass das externe EEPROM eigentlich "korrekt" sein sollte
				Diagnoseausgabe_P(ISTR(ZusatzEepromFehler, LokaleSprache), 1);
			}
		else
			Protokollieren_P(PSTR("iTelex: Teilnehmer-Verzeichnis aus EEPROM geladen.\r\n"));
		
		} // if get_Taste()
	else
		TlnBuchMemUsed = 0;
	
	}
	
	
#endif //def ITELEX_BASIS
