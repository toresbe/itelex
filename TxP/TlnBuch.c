#include <avr/pgmspace.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "system/net/ip.h"

#include "apps/httpd/cgibin/cgi-bin.h"
#include "apps/httpd/httpd2.h"
#include "apps/httpd/httpd2_pharse.h"

#include "TxP.h"
#include "TlnBuch.h"

#ifdef TELEXPHONE

enum { TlnBuchMemMax = 30000UL } ; //!< Größe des Teilnehmerverzeichnisses in Bytes


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
	void *p;
	
	p = BuchP;
	*((uint32_t *) p) = Tln->Nummer; 					p += 4;
	*((uint8_t *) p) = TlnEintragGroesse(Tln);			p += 1;
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
			*((long *) p) = Tln->IPAdr;					p += 4;
			*((uint16_t *) p) = Tln->Port;				p += 2;
			*((uint8_t *) p) = Tln->Durchwahl;			p += 1;
			break;
		
		default:
			*((uint8_t *) (p-5)) = (uint8_t) Geloescht; // nachträglich auf gelöscht ändern
			break;
		}

	//! \todo Check, ob p-BuchP == TlnEintragGroesse
	
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
			Tln->IPAdr = *((long *) p);					p += 4;
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

	
static void TlnBuchTesteintrag(uint32_t nr, char *url, long aip, uint16_t port)
	{
	TTlnDaten TD;

	TD.Nummer = nr;
	if (url == NULL)
		{
		TD.AdrArt = TxpIP; 
		TD.IPAdr = aip;
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
	

//! CGI-Funktion für die Anzeige des Teilnehmerverzeichnisses.
void TlnBuch_Anzeige_CGI(void *pStruct)
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	char Buf[35];
	TTlnDaten TD;

	static PROGMEM const char EditPN[] = "edit";
	static PROGMEM const char EditNewPN[] = "editnew";
	static PROGMEM const char DeletePN[] = "delete";
	static PROGMEM const char UpdatePN[] = "update";
	static PROGMEM const char AddPN[] = "add";
	
	cgi_PrintHttpheaderStart();

	if ( http_request->argc == 0 )
		{ // Startseite = Liste
		printf_P(PSTR(
			"<form action=\"txp-tlnverz.cgi\">"
			"<h3>Teilnehmerverzeichnis</h3>"
			"<table border=\"1\" cellpadding=\"2\" cellspacing=\"0\">"
			"<tr>"
   			"<td align=\"right\">Nummer</td>" // Nummer
			"<td align=\"left\">Adresse</td>" // Adresse
			"<td align=\"center\">Port</td>" // Port
			"<td align=\"center\">Durchwahl</td>" // Durchwahl
			"<td align=\"left\">Aktion</td>" // in dieser Spalte sind die Buttons
			"</tr>"			
			));
			
		if (TlnListerStart())
			{
			while (TlnListerNaechster(&TD))
				{
				switch (TD.AdrArt)
					{
					case TxpUrl:
						strcpy(Buf, TD.Adresse);
						break;
					case TxpIP:
						iptostr(TD.IPAdr, Buf);
						break;
					default:
						strcpy_P(Buf, PSTR("gel&ouml;scht"));
						break;
					}
					
				printf_P( PSTR(	"<tr>"
					   			"<td align=\"right\">%ld</td>" // Nummer
					   			"<td align=\"left\">%s</td>" // Adresse
					   			"<td align=\"center\">%d</td>" // Port
					   			"<td align=\"center\">%d</td>" // Durchwahl
								"<td><a href=\"txp-tlnverz.cgi?edit=%ld\" style=\"text-decoration:none\"><input type=\"button\" value=\"&Auml;ndern\" class=\"actionBtn\"></a></td>"
  								"</tr>"), TD.Nummer, Buf, TD.Port, TD.Durchwahl, TD.Nummer);
				}
			printf_P(PSTR( "<tr><td></td><td></td><td></td><td></td>"
						   "<td><a href=\"txp-tlnverz.cgi?editnew\" style=\"text-decoration:none\"><input type=\"button\" value=\"Hinzuf&uuml;gen\" class=\"actionBtn\"></a></td>"
						   "</table></form>") );
			} // Teilnehmerverzeichnis nicht leer
		else
			{
			printf_P(PSTR( "</table>Noch keine Eintr&auml;ge vorhanden<p>"
						   "<a href=\"txp-tlnverz.cgi?editnew\" style=\"text-decoration:none\"><input type=\"button\" value=\"Hinzuf&uuml;gen\" class=\"actionBtn\"></a>" ));
			}

		} // argc == 0
	else if (PharseCheckName_P(http_request, EditNewPN) || PharseCheckName_P(http_request, EditPN))
		{ // Ändern ODER Neu --> Eingabeformular anzeigen und ggf. füllen.
		TD.Nummer = 0;
		TD.Adresse[0] = '\0';
		TD.AdrArt = TxpUrl;
		TD.Port = TXP_PORT;
		TD.Durchwahl = 0;
		if (PharseCheckName_P(http_request, EditPN))
			{
			TD.Nummer = atol(http_request->argvalue[PharseGetValue_P(http_request, EditPN)]);
			TlnSuche(TD.Nummer, true, &TD);
			}
			
		printf_P(PSTR(
			"<form action=\"txp-tlnverz.cgi\">"
			"<table border=\"0\" cellpadding=\"5\" cellspacing=\"0\">"
			));

		printf_P( PSTR(	"<tr>"
						"<td align=\"right\">Nummer:</td>"
						"<td><input name=\"nummer\" type=\"text\" size=\"10\" value=\"%ld\" maxlength=\"10\"></td>"
						"</tr>"), TD.Nummer);

		if (TD.AdrArt == TxpIP)
			iptostr(TD.IPAdr, TD.Adresse);
		printf_P( PSTR(	"<tr>"
						"<td align=\"right\">Adresse:</td>"
						"<td><input name=\"adresse\" type=\"text\" size=\"25\" value=\"%s\" maxlength=\"%d\"></td>"
						"</tr>"), TD.Adresse, TlnAdresseMax-1);

		printf_P( PSTR(	"<tr>"
						"<td align=\"right\">Port:</td>"
						"<td><input name=\"port\" type=\"text\" size=\"3\" value=\"%d\" maxlength=\"3\"></td>"
						"</tr>"), TD.Port);

		printf_P( PSTR(	"<tr>"
						"<td align=\"right\">Durchwahl:</td>"
						"<td><input name=\"durchwahl\" type=\"text\" size=\"3\" value=\"%d\" maxlength=\"3\"></td>"
						"</tr>"), TD.Durchwahl);
						
/* Reserve
		printf_P( PSTR( "<tr>"
					   	"<td align=\"right\">Port:</td>"
					    "<td><input name=\"port\" type=\"checkbox\" value=\"1\" " )); 
		if (FesteHauptstelle)
			printf_P( PSTR("checked"));
		printf_P( PSTR(	"></td>"
  						"</tr>") );
*/		
		printf_P(PSTR( "<tr>"
						"<td></td><td><input type=\"submit\" value=\" Einstellung &Uuml;bernehmen \"></td>"
  						"</tr>"
					   	"</table>"
						"</form>") );

		}
	else if (PharseCheckName_P(http_request, DeletePN))
		{ // löschen
		printf_P(PSTR("TODO Löschen"));
		}
	else if (PharseCheckName_P(http_request, AddPN) || PharseCheckName_P(http_request, UpdatePN))
		{ // neuen Einfügen oder geänderten Aktualisieren
		static PROGMEM const char NummerPN[] = "nummer";
		static PROGMEM const char AdressePN[] = "adresse";
		
		if (PharseCheckName_P(http_request, NummerPN))
			{
			TD.Nummer = atol(http_request->argvalue[PharseGetValue_P(http_request, NummerPN)]);
			strncpy(TD.Adresse, http_request->argvalue[PharseGetValue_P(http_request, AdressePN)], TlnAdresseMax-1);
			TD.Adresse[TlnAdresseMax-1] = '\0'; // sicherheitshalber abhacken.
			TD.IPAdr = strtoip(TD.Adresse);
			printf_P(PSTR("Nummer: %ld<br>IP: %lx<br>Adresse: %s"), TD.Nummer, TD.IPAdr, TD.Adresse);
			// TODO weiter auswerten.
			}
		else
			{
			printf_P(PSTR("Keine gültige Nummer angegeben"));
			}
		}
	else
		{ // nicht erkannt
		printf_P(PSTR("Fehler: ungueltiger CGI-Aufruf: %s"), http_request->HTTP_LINEBUFFER);
		}
		
	cgi_PrintHttpheaderEnd();

	}
	
	
	
//! Initialisiert die Liste der Teilnehmereinträge.
//------------------------------------------------------------
void TlnBuchInit()
	{
	TlnBuchMemUsed = 0;
	
	// HACK Test
	TlnBuchTesteintrag(123, "sonnibs.no-ip.org", 0, 134);
	TlnBuchTesteintrag(124, "sonnibs.no-ip.org", 0, 135);
	TlnBuchTesteintrag(234, 0, IPDOT(192l,168l,178l,30l), 134);
	TlnBuchTesteintrag(235, 0, IPDOT(192l,168l,178l,38l), 134);
	TlnBuchTesteintrag(3333, 0, IPDOT(192l,168l,178l,32l), 23);

	cgi_RegisterCGI( TlnBuch_Anzeige_CGI, PSTR("txp-tlnverz.cgi"));
	
	}
	

	
#endif //def TELEXPHONE
