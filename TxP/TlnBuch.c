#include <avr/pgmspace.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "system/net/ip.h"
#include "system/clock/clock.h"

#include "apps/httpd/cgibin/cgi-bin.h"
#include "apps/httpd/httpd2.h"
#include "apps/httpd/httpd2_pharse.h"

#include "TxP.h"
#include "TlnBuch.h"
#include "SwTwi.h"
#include "Protokoll.h"

#include "CgiFormTools.h"

#ifdef TELEXPHONE

enum { TlnBuchMemMax = 20000UL } ; //!< Größe des Teilnehmerverzeichnisses in Bytes


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
//! \par 2 Byte DynPin (nur bei Typ = TxpDynIP)


static char TlnBuch[TlnBuchMemMax]; //!< Das Teilnehmer-Verzeichnis.


// Häufig benutzte Offsets:
enum { TBOffsGroesse = 4 } ; //!< Position der Eintragsgröße im Teilnehmer-Verzeichnis-Eintrag
enum { TBOffsFlags = 5 } ; //!< Position der Flags im Teilnehmer-Verzeichnis-Eintrag
enum { TBOffsArt = 7 } ; //!< Position der Art (s. #TTlnAdresseArt) im Teilnehmer-Verzeichnis-Eintrag


static uint16_t TlnBuchMemUsed; //!< Ende des genutzten Bereichs in TlnBuch.


//! Ermittelt die Größe eines Teilnehmereintrags.
static uint8_t TlnEintragGroesse(TTlnDaten *Tln)
	{
	uint8_t Basis = 4 + 1 + 2 + 1 + strlen(Tln->Name)+1 + 4;
	
	switch (Tln->AdrArt)
		{
		case Geloescht: 
			return Basis;
			
		case TxpUrl:
			return Basis + strlen(Tln->Adresse)+1 + 2 + 1;
			
		case TxpIP:
			return Basis + 4 + 2 + 1;
		
		case TxpDynIP:
			return Basis + 4 + 2 + 1 + 2;
		
		case AsciiUrl:
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
static void TlnEintragen(TTlnDaten *Tln, char *BuchP)
	{
	void *p;
	
	p = BuchP;
	*((uint32_t *) p) = Tln->Nummer; 					p += 4;
	*((uint8_t *) p) = TlnEintragGroesse(Tln);			p += 1;
	*((uint16_t *) p) = Tln->Flags;						p += 2;
	*((uint8_t *) p) = (uint8_t) Tln->AdrArt;			p += 1;
	strcpy(p, Tln->Name);								p += strlen(Tln->Name)+1;
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
		case TxpDynIP:
			*((long *) p) = Tln->IPAdr;					p += 4;
			*((uint16_t *) p) = Tln->Port;				p += 2;
			*((uint8_t *) p) = Tln->Durchwahl;			p += 1;
			if (Tln->AdrArt == TxpDynIP)
				{
				*((uint16_t *) p) = Tln->DynPin;		p += 2;
				}
			break;
		
		case AsciiUrl:
			strcpy(p, Tln->Adresse);					p += strlen(Tln->Adresse)+1;
			*((uint16_t *) p) = Tln->Port;				p += 2;
			break;
			
		case AsciiIP:
			*((long *) p) = Tln->IPAdr;					p += 4;
			*((uint16_t *) p) = Tln->Port;				p += 2;
			break;
		
		case eMail:
			strcpy(p, Tln->Adresse);					p += strlen(Tln->Adresse)+1;
			break;
			
		default:
			*((uint8_t *) (p-5)) = (uint8_t) Geloescht; // nachträglich auf gelöscht ändern
			Tln->AdrArt = Geloescht;
			break;
		}

	while (((char*)p - BuchP) != TlnEintragGroesse(Tln))
		; // Endlosschleife zur Fehlererkennung.
	
	} // TlnEintragen
	
	
//! Holt die Daten aus dem TlnBuch.
static void TlnLesen(TTlnDaten *Tln, char *BuchP)
	{
	void *p;
	p = BuchP;
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
			
		case TxpUrl:
			strcpy(Tln->Adresse, p);					p += strlen(Tln->Adresse)+1;
			Tln->Port = *((uint16_t *) p);				p += 2;
			Tln->Durchwahl = *((uint8_t *) p);			p += 1;
			break;
			
		case TxpIP:
		case TxpDynIP:
			Tln->IPAdr = *((long *) p);					p += 4;
			Tln->Port = *((uint16_t *) p);				p += 2;
			Tln->Durchwahl = *((uint8_t *) p);			p += 1;
			if (Tln->AdrArt == TxpDynIP)
				{
				Tln->DynPin = *((uint16_t *) p);		p += 2;
				}
			break;
		
		case AsciiUrl:
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
//! \param[out] Tln Zeiger auf den Datensatz-

void TlnDatenInit(TTlnDaten *Tln)
	{
	Tln->Nummer = 0;
	Tln->Name[0] = '\0';
	Tln->Flags = 0;
	Tln->AdrArt = 0; 
	Tln->Adresse[0] = '\0';
	Tln->IPAdr = 0;
	Tln->Port = 0; 
	Tln->Durchwahl = 0; 
	Tln->DynPin = 0;
	Tln->Datum = 0;
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
		p += *((uint8_t *) (p + TBOffsGroesse));
		}
	return NULL;
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
		
	uint8_t AltGr = *((uint8_t *) (p + TBOffsGroesse));
	if (NeuGr != AltGr)
		{
		if (TlnBuchMemUsed + NeuGr - AltGr >= TlnBuchMemMax)
			return false;
		memmove(p + NeuGr, p + AltGr, TlnBuchMemUsed - (p - TlnBuch) - AltGr);
		TlnBuchMemUsed += NeuGr - AltGr;
		}
	TlnEintragen(Tln, p);
	return true;
	}


//! Zeiger für TlnListerStart und TlnListerNaechster.
//---------------------------------------------------
//! Zeigt auf nächsten Eintrag, der durch TlnListerNaechster geliefert wird.
static char *ListerP = TlnBuch;


//! Startet die sequentielle Abfrage aller Teilnehmereinträge.
//------------------------------------------------------------
//! \retval true wenn es mindestens einen Eintrag gibt.
bool TlnListerStart()
	{
	ListerP = TlnBuch;
	return ListerP < TlnBuch + TlnBuchMemUsed;
	}
	

//! Sequentielle Abfrage aller Teilnehmereinträge.
//------------------------------------------------------------
//! \param[out] Tln gefundener Eintrag.
//! \retval true wenn ein weiterer Eintrag gefunden wurde.
bool TlnListerNaechster(TTlnDaten *Tln)
	{
	if (ListerP >= TlnBuch + TlnBuchMemUsed)
		return false;
	TlnLesen(Tln, ListerP);
	ListerP += *((uint8_t *) (ListerP+4));
	return true;
	}

	
//! Errechnet aus dem aktuellen Wert von TlnBuchMemUsed einen Prüfwert,
//! um die Integrität des externen EEPROM zu testen.
//---------------------------------------------------------------------
//! Im externen EEPROM sind beide Werte ganz am Anfang abgelegt.
uint16_t MemUsedPruefwert(uint16_t groesse)
	{
	return (groesse ^ 0x4587) << 1; //### 8765
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
//------------------------------------------------------------
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

	
//! CGI-Funktion für die Anzeige des Teilnehmerverzeichnisses.
void TlnBuch_Anzeige_CGI(void *pStruct)
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	TTlnDaten TD;
	bool Zurueck = false; // wird auf true gesetzt, wenn ein "zurück"-Text gedruckt werden soll.

	static PROGMEM const char Edit_P[] = "edit";
	static PROGMEM const char Nummer_P[] = "nummer";
	static PROGMEM const char AltNummer_P[] = "altnummer";
	static PROGMEM const char Name_P[] = "name";
	static PROGMEM const char Adresse_P[] = "adresse";
	static PROGMEM const char Port_P[] = "port";
	static PROGMEM const char Durchwahl_P[] = "durchwahl";
	static PROGMEM const char Typ_P[] = "type";
	static PROGMEM const char TypGeloescht_P[] = "geloescht";
	static PROGMEM const char TypAscii_P[] = "Ascii";
	static PROGMEM const char TypTxp_P[] = "TelexPhone";
	static PROGMEM const char TypEMail_P[] = "eMail";
	static PROGMEM const char Lokal_P[] = "local";
	static PROGMEM const char Gesperrt_P[] = "lock";
	static PROGMEM const char Save_P[] = "save";
	static PROGMEM const char Clear_P[] = "clear";
	static PROGMEM const char Load_P[] = "load";
	
	cgi_PrintHttpheaderStart();

	if (http_request->argc != 0 && !KonfigFreigabe(pStruct))
		return;

	if ( http_request->argc == 0 )
		{ 
		// Startseite = Liste
		// ==================================================
		printf_P(PSTR(
			"<form action=\"txp-tlnverz.cgi\">"
			"<h3>Teilnehmerverzeichnis</h3>"
			"<table border=\"1\" cellpadding=\"2\" cellspacing=\"0\">"
			"<tr>"
   			"<th align=\"right\">Rufnummer</th>" // Nummer
   			"<th align=\"left\">Name</th>" // Name
   			"<th align=\"center\">Besond.</th>" // Flags
   			"<th align=\"left\">Typ</th>" // Typ
			"<th align=\"left\">Adresse</th>" // Adresse
			"<th align=\"center\">Port</th>" // Port
			"<th align=\"center\">Durchwahl</th>" // Durchwahl
			"<th align=\"center\">letzte<br>Aktualisierung</th>" // Datum / Uhrzeit
			"<th align=\"left\">Aktion</th>" // in dieser Spalte sind die Buttons
			"</tr>"			
			));
			
		TlnDatenInit(&TD);
		
		struct TIME Time;
		CLOCK_GetTime(&Time); // holt auch die aktuelle Zeitzone
		
		if (TlnListerStart())
			{
			while (TlnListerNaechster(&TD))
				{
				printf_P(PSTR("<tr><td align=\"right\">%ld</td>"), TD.Nummer); // Nummer
				printf_P(PSTR("<td align=\"left\">%s</td><td>&#160;"), TD.Name); // name
				if ((TD.Flags & TlnFlag_Lokal) != 0)
					printf_P(PSTR("Lokal "));
				if ((TD.Flags & TlnFlag_Gesperrt) != 0)
					printf_P(PSTR("gesperrt "));
				if (TD.AdrArt == TxpDynIP)
					printf_P(PSTR("DynIP "));
				printf_P(PSTR("</td>")); // Ende Besonderheiten
				
				switch (TD.AdrArt)
					{
					case TxpIP:
					case TxpDynIP:
						iptostr(TD.IPAdr, TD.Adresse);
						// weiter mit TxpUrl!
					case TxpUrl:
						printf_P(PSTR(
							"<td align=\"left\">TelexPhone</td>"
							"<td align=\"left\">%s</td>" // Adresse
							"<td align=\"center\">%u</td>" // Port
					   		"<td align=\"center\">%u</td>" // Durchwahl
							), TD.Adresse, TD.Port, TD.Durchwahl);
						break;

					case AsciiIP:
						iptostr(TD.IPAdr, TD.Adresse);
						// weiter mit AsciiUrl!
					case AsciiUrl:
						printf_P(PSTR(
							"<td align=\"left\">Ascii</td>"
							"<td align=\"left\">%s</td>" // Adresse
							"<td align=\"center\">%u</td>" // Port
					   		"<td>&#160;</td>" // Durchwahl
							), TD.Adresse, TD.Port);
						break;

					case eMail:
						printf_P(PSTR(
							"<td align=\"left\">eMail</td>"
							"<td align=\"left\">%s</td>" // Adresse
							"<td align=\"center\">&#160;</td>" // Port
					   		"<td>&#160;</td>" // Durchwahl
							), TD.Adresse);
						break;

					default:
						printf_P(PSTR("<td align=\"left\">gel&ouml;scht</td><td>&#160;</td><td>&#160;</td><td>&#160;</td>"));
						break;
					}
					
				// Datum / Uhrzeit...
				Time.time = TD.Datum;
				CLOCK_decode_time(&Time);
				
				printf_P(PSTR("<td align=\"center\">%02u.%02u.%04u %02d:%02d:%02d</td>"), Time.DD, Time.MM, Time.YY, Time.hh, Time.mm, Time.ss);
				
				printf_P(PSTR("<td><a href=\"txp-tlnverz.cgi?edit=%ld\">&Auml;ndern</a></td></tr>"), TD.Nummer);
				}
			printf_P(PSTR( "<tr><td>&#160;</td><td>&#160;</td><td>&#160;</td><td>&#160;</td><td>&#160;</td><td>&#160;</td><td>&#160;</td><td>&#160;</td>"
						   "<td><a href=\"txp-tlnverz.cgi?edit=0\">Hinzuf&uuml;gen</a></td>"
						   "</table>"
						   "<a href=\"txp-tlnverz.cgi?save\">nichtfl&uuml;chtig speichern</a><br>"
						   "<a href=\"txp-tlnverz.cgi?load\">alle &Auml;nderungen verwerfen</a><br>"
						   "<a href=\"txp-tlnverz.cgi?clear\">komplett l&ouml;schen</a><br>"
						   "</form>") );
			} // Teilnehmerverzeichnis nicht leer
		else
			{
			printf_P(PSTR( "</table>Noch keine Eintr&auml;ge vorhanden<p>"
						   "<a href=\"txp-tlnverz.cgi?edit=0\">Hinzuf&uuml;gen</a></form>" ));
			}

		} // argc == 0 --> gesamte Liste ausgeben
		
	else if (PharseCheckName_P(http_request, Edit_P))
		{ 
		// Ändern ODER Neu --> Eingabeformular anzeigen und füllen.
		// =========================================================
		TD.Nummer = atol(http_request->argvalue[PharseGetValue_P(http_request, Edit_P)]);
		if (TD.Nummer == 0 || !TlnSuche(TD.Nummer, true, &TD))
			{ // neuen oder nicht gefundenen Eintrag initialisieren.
			TD.Name[0] = '\0';
			TD.Adresse[0] = '\0';
			TD.AdrArt = TxpUrl;
			TD.Port = TXP_PORT;
			TD.Durchwahl = 0;
			TD.Flags = 0;
			#ifdef TXP_ANSCHLUSS
			TD.Flags |= (TlnFlag_Lokal);
			#endif //def TXP_ANSCHLUSS
			}

		CgiFormStartTabbed_P(PSTR("txp-tlnverz.cgi"));

		CgiFormInputFieldULong_P(PSTR("Rufnummer:"), Nummer_P, 10, TD.Nummer);

		printf_P(PSTR("<input name=\"altnummer\" type=\"hidden\" value=\"%ld\">"), TD.Nummer);

		CgiFormInputFieldText_P(PSTR("Name:"), Name_P, TlnNameMax-1, TD.Name);
	
		CgiFormCheckbox_P(PSTR("nur Lokal:"), Lokal_P, (TD.Flags & TlnFlag_Lokal) != 0);

		CgiFormCheckbox_P(PSTR("gesperrt:"), Gesperrt_P, (TD.Flags & TlnFlag_Gesperrt) != 0);

		const char *TypSelList[] = { TypGeloescht_P, TypTxp_P, TypAscii_P, TypEMail_P } ;
		uint8_t TypSelNr;
		switch (TD.AdrArt)
			{
			case TxpIP:
			case TxpDynIP:
			case TxpUrl: 	TypSelNr = 1; break;
			case AsciiIP:
			case AsciiUrl: 	TypSelNr = 2; break;
			case eMail:		TypSelNr = 3; break;
			default: 		TypSelNr = 0; break;
			}
		CgiFormDropdown_P(PSTR("Typ:"), Typ_P, 4, TypSelList, TypSelNr);
		
		if (TD.AdrArt == TxpIP || TD.AdrArt == TxpDynIP || TD.AdrArt == AsciiIP)
			iptostr(TD.IPAdr, TD.Adresse);
		CgiFormInputFieldText_P(PSTR("Adresse:"), Adresse_P, TlnAdresseMax-1, TD.Adresse);
		CgiFormInputFieldULong_P(PSTR("Port:"), Port_P, 5, TD.Port);
		CgiFormInputFieldULong_P(PSTR("Durchwahl:"), Durchwahl_P, 3, TD.Durchwahl);

		if (TD.Nummer == 0)
			CgiFormFinish_P(PSTR("Hinzuf&uuml;gen"));
		else
			CgiFormFinish_P(PSTR("&Auml;ndern"));
		Zurueck = true;
		} // Ändern oder Neu

	else if (PharseCheckName_P(http_request, Nummer_P))
		{ 
		// neuen Einfügen oder geänderten Aktualisieren
		// ==================================================
		bool Ok = true; // nur wenn gesetzt, wird auch gespeichert
		uint32_t AltNummer = atol(http_request->argvalue[PharseGetValue_P(http_request, AltNummer_P)]);
		uint32_t NeuNummer = atol(http_request->argvalue[PharseGetValue_P(http_request, Nummer_P)]);
			//! \todo Umstellen auf CgiCheckULong...
		
		// brauche alte Geheimzahl und alten Typ
		if (NeuNummer == 0 || !TlnSuche(NeuNummer, true, &TD))
			{ // hat doch nicht geklappt
			TD.AdrArt = 0;
			TD.DynPin = 0;
			}

		TD.Nummer = NeuNummer;
		strncpy(TD.Name, http_request->argvalue[PharseGetValue_P(http_request, Name_P)], TlnNameMax-1);
		TD.Flags = 0;
		char TypStr[20];
		strncpy(TypStr, http_request->argvalue[PharseGetValue_P(http_request, Typ_P)], sizeof(TypStr));
		strncpy(TD.Adresse, http_request->argvalue[PharseGetValue_P(http_request, Adresse_P)], TlnAdresseMax-1);
		TD.Adresse[TlnAdresseMax-1] = '\0'; // sicherheitshalber abhacken.

		if (TD.Nummer == 0)
			{
			printf_P(PSTR("<b>Rufnummer 0 nicht erlaubt!</b><br>"));
			Ok = false;
			}
		else
			{
			printf_P(PSTR("Teilnehmereintrag:<br>Rufnummer: %ld "), TD.Nummer);
			if (AltNummer == 0)
				printf_P(PSTR("hinzuf&uuml;gen"));
			else if (AltNummer != TD.Nummer)
				printf_P(PSTR("ehem. %ld"), AltNummer);
			printf_P(PSTR("<br>"));
			printf_P(PSTR("Name: %s<br>"), TD.Name);
			}
		
		if (PharseCheckName_P(http_request, Lokal_P))
			{
			if (atoi(http_request->argvalue[PharseGetValue_P(http_request, Lokal_P)]) != 0)
				TD.Flags |= TlnFlag_Lokal;
			}
		if ((TD.Flags & TlnFlag_Lokal) != 0)
			printf_P(PSTR("nur Lokal<br>"));

		if (PharseCheckName_P(http_request, Gesperrt_P))
			{
			if (atoi(http_request->argvalue[PharseGetValue_P(http_request, Gesperrt_P)]) != 0)
				TD.Flags |= TlnFlag_Gesperrt;
			}
		if ((TD.Flags & TlnFlag_Gesperrt) != 0)
			printf_P(PSTR("gesperrt<br>"));

		if (TD.Adresse[0] == '\0' || strcmp_P(TypStr, TypGeloescht_P) == 0)
			// Leere Adresse --> löschen
			{
			TD.AdrArt = Geloescht;
			printf_P(PSTR("gel&ouml;scht<br>"));
			}
		else
			{
			TD.IPAdr = strtoip(TD.Adresse);
			
			if (strcmp_P(TypStr, TypTxp_P) == 0)
				{
				if (TD.IPAdr == 0)
					{
					TD.AdrArt = TxpUrl;
					printf_P(PSTR("TelexPhone: Url %s "), TD.Adresse);
					}
				else
					{
					if (TD.AdrArt == TxpDynIP && AltNummer == TD.Nummer)
						{ // der alte (!) Eintrag war ein Eintrag zu dynamischer IP-Aktualisierung
						// es ist nix zu ändern, auch die Pin bleibt unverändert.
						}
					else
						TD.AdrArt = TxpIP;
					iptostr(TD.IPAdr, TD.Adresse); // und wieder zurück wandeln
					printf_P(PSTR("TelexPhone: IP %s "), TD.Adresse);
					}
				TD.Port = atoi(http_request->argvalue[PharseGetValue_P(http_request, Port_P)]);
				TD.Durchwahl = atoi(http_request->argvalue[PharseGetValue_P(http_request, Durchwahl_P)]);
				printf_P(PSTR("Port %u Durchwahl %u<br>"), TD.Port, TD.Durchwahl);
				}
				
			else if (strcmp_P(TypStr, TypAscii_P) == 0)
				{
				if (TD.IPAdr == 0)
					{
					TD.AdrArt = AsciiUrl;
					printf_P(PSTR("Ascii: Url %s "), TD.Adresse);
					}
				else
					{
					TD.AdrArt = AsciiIP;
					iptostr(TD.IPAdr, TD.Adresse); // und wieder zurück wandeln
					printf_P(PSTR("Ascii: IP %s "), TD.Adresse);
					}
				TD.Port = atoi(http_request->argvalue[PharseGetValue_P(http_request, Port_P)]);
				TD.Durchwahl = 0;
				printf_P(PSTR("Port %u<br>"), TD.Port);
				}

			else if (strcmp_P(TypStr, TypEMail_P) == 0)
				{
				printf_P(PSTR("eMail: Adresse %s<br>"), TD.Adresse);
				TD.AdrArt = eMail;
				TD.Port = 0;
				TD.Durchwahl = 0;
				}
				
			else
				{
				printf_P(PSTR("<b>Unbekannter Typ!</b><br>"));
				Ok = false;
				}
			}

		if (TD.AdrArt != TxpDynIP)
			TD.DynPin = 0; // Datenschutz.
			
		if (Ok && TD.AdrArt == Geloescht && AltNummer == 0)
			{ // einen neuen Lösch-Eintrag anzulegen ist doof
			printf_P(PSTR("<b>keine &Auml;nderung</b><br>"));
			Ok = false;
			}
			
		if (Ok && TD.Nummer != AltNummer && TlnSuche(TD.Nummer, false, NULL))
			{
			printf_P(PSTR("<b>Rufnummer ist bereits vergeben, &Auml;nderung nicht gespeichert</b><br>"));
			Ok = false;
			}
			
		if (Ok)
			{ // speichern oder löschen
			struct TIME CurTime;
			CLOCK_GetTime(&CurTime);
			TD.Datum = CurTime.time;
			
			if (TlnHinzufuegen(&TD))
				{
				printf_P(PSTR("Eintrag gespeichert<br>"));
				if (TD.Nummer != AltNummer && AltNummer != 0)
					{
					TD.Nummer = AltNummer;
					TD.AdrArt = Geloescht;
					if (!TlnHinzufuegen(&TD))
						{
						printf_P(PSTR("<b>Alte Nummer %ld konnte nicht gel&ouml;scht werden!</b><br>"), AltNummer);	
						}
					}
				}
			else
				{ // TlnHinzufuegen() == false
				printf_P(PSTR("<b>Teilnehmerliste voll, Eintrag nicht gespeichert</b><br>"));
				}
			} // if Ok
		Zurueck = true;
		} // if (PharseCheckName_P(http_request, Nummer_P)) ; also neuen Einfügen oder geänderten Aktualisieren
		
	else if (PharseCheckName_P(http_request, Save_P))
		{ 
		// auf externem Eeprom speichern
		// ==================================================
		int Res = TlnBuchSpeichereAufExternEeprom();
		if (Res < 0)
			printf_P(PSTR("<b>Fehler beim Speichern (Codes %d / %02X)</b>"), Res, SwTwiLetzterFehler);
		else
			printf_P(PSTR("Erfolgreich gespeichert (%d Bytes)"), Res);
		Zurueck = true;
		}
		
	else if (PharseCheckName_P(http_request, Load_P))
		{ 
		// von externem Eeprom laden
		// ==================================================
		int Res = TlnBuchLadeVonExternEeprom();
		if (Res < 0)
			printf_P(PSTR("<b>Fehler beim Laden (Codes %d / %02X)</b>"), Res, SwTwiLetzterFehler);
		else
			printf_P(PSTR("Erfolgreich geladen (%d Bytes)"), Res);
		Zurueck = true;
		}
		
	else if (PharseCheckName_P(http_request, Clear_P))
		{ 
		// komplett löschen
		// ==================================================
		printf_P(PSTR("komplett gel&ouml;scht"));
		TlnBuchMemUsed = 0;
		Zurueck = true;
		}
		
	else
		{ 
		// nicht erkannt
		// ==================================================
		printf_P(PSTR("Fehler: ungueltiger CGI-Aufruf: %s"), http_request->HTTP_LINEBUFFER);
		Zurueck = true;
		}
		
	if (Zurueck)
		printf_P(PSTR("<br>Zur&uuml;ck zum <a href=\"txp-tlnverz.cgi\">Teilnehmer-Verzeichnis</a>"));
	
	cgi_PrintHttpheaderEnd();

	}
	

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
	
	TlnBuchMemUsed = 0;
	
	if (get_Taste()) // high vom Pullup -> Taste nicht gedrückt
		{
		int Res = TlnBuchLadeVonExternEeprom();
		extern char DebugMsg[];
		if (Res < 0)
			{
			sprintf_P(DebugMsg, PSTR("TxP: Eeprom Ladefehler %d / %02X"), Res, SwTwiLetzterFehler);
			Protokollieren(DebugMsg);
			}
		} // if get_Taste()
		
	cgi_RegisterCGI( TlnBuch_Anzeige_CGI, PSTR("txp-tlnverz.cgi"));
	
	}
	

	
#endif //def TELEXPHONE
