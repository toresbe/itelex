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

#include "CgiFormTools.h"

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
		
		case AsciiUrl:
			return 4 + 1 + 1 + 4 + strlen(Tln->Adresse)+1 + 2;
			
		case AsciiIP:
			return 4 + 1 + 1 + 4 + 4 + 2;
		
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
		
		case AsciiUrl:
			strcpy(p, Tln->Adresse);					p += strlen(Tln->Adresse)+1;
			*((uint16_t *) p) = Tln->Port;				p += 2;
			break;
			
		case AsciiIP:
			*((long *) p) = Tln->IPAdr;					p += 4;
			*((uint16_t *) p) = Tln->Port;				p += 2;
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
				|| (((TTlnAdresseArt) *((uint8_t *) (p+5))) != Geloescht);
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
	TlnHinzufuegen(&TD);
	}
	

//! CGI-Funktion für die Anzeige des Teilnehmerverzeichnisses.
void TlnBuch_Anzeige_CGI(void *pStruct)
	{
	struct HTTP_REQUEST * http_request;
	http_request = (struct HTTP_REQUEST *) pStruct;
	TTlnDaten TD;

	static PROGMEM const char Edit_P[] = "edit";
	static PROGMEM const char Nummer_P[] = "nummer";
	static PROGMEM const char AltNummer_P[] = "altnummer";
	static PROGMEM const char Adresse_P[] = "adresse";
	static PROGMEM const char Port_P[] = "port";
	static PROGMEM const char Durchwahl_P[] = "durchwahl";
	static PROGMEM const char Typ_P[] = "type";
	static PROGMEM const char TypGeloescht_P[] = "gel&ouml;scht";
	static PROGMEM const char TypAscii_P[] = "Ascii";
	static PROGMEM const char TypTxp_P[] = "TelexPhone";
	
	cgi_PrintHttpheaderStart();

	if ( http_request->argc == 0 )
		{ // Startseite = Liste
		printf_P(PSTR(
			"<form action=\"txp-tlnverz.cgi\">"
			"<h3>Teilnehmerverzeichnis</h3>"
			"<table border=\"1\" cellpadding=\"2\" cellspacing=\"0\">"
			"<tr>"
   			"<th align=\"right\">Nummer</th>" // Nummer
   			"<th align=\"left\">Typ</th>" // Typ
			"<th align=\"left\">Adresse</th>" // Adresse
			"<th align=\"center\">Port</th>" // Port
			"<th align=\"center\">Durchwahl</th>" // Durchwahl
			"<th align=\"left\">Aktion</th>" // in dieser Spalte sind die Buttons
			"</tr>"			
			));
			
		if (TlnListerStart())
			{
			while (TlnListerNaechster(&TD))
				{
				printf_P(PSTR("<tr><td align=\"right\">%ld</td>"), TD.Nummer); // Nummer

				switch (TD.AdrArt)
					{
					case TxpIP:
						iptostr(TD.IPAdr, TD.Adresse);
						// weiter mit TxpUrl!
					case TxpUrl:
						printf_P(PSTR(
							"<td align=\"left\">TelexPhone</td>"
							"<td align=\"left\">%s</td>" // Adresse
							"<td align=\"center\">%d</td>" // Port
					   		"<td align=\"center\">%d</td>" // Durchwahl
							), TD.Adresse, TD.Port, TD.Durchwahl);
						break;

					case AsciiIP:
						iptostr(TD.IPAdr, TD.Adresse);
						// weiter mit AsciiUrl!
					case AsciiUrl:
						printf_P(PSTR(
							"<td align=\"left\">Ascii</td>"
							"<td align=\"left\">%s</td>" // Adresse
							"<td align=\"center\">%d</td>" // Port
					   		"<td>&#160;</td>" // Durchwahl
							), TD.Adresse, TD.Port, TD.Durchwahl);
						break;

					default:
						printf_P(PSTR("<td align=\"left\">gel&ouml;scht</td><td>&#160;</td><td>&#160;</td><td>&#160;</td>"));
						break;
					}
					
				printf_P(PSTR("<td><a href=\"txp-tlnverz.cgi?edit=%ld\">&Auml;ndern</a></td></tr>"), TD.Nummer);
				}
			printf_P(PSTR( "<tr><td>&#160;</td><td>&#160;</td><td>&#160;</td><td>&#160;</td><td>&#160;</td>"
						   "<td><a href=\"txp-tlnverz.cgi?edit=0\">Hinzuf&uuml;gen</a></td>"
						   "</table></form>") );
			} // Teilnehmerverzeichnis nicht leer
		else
			{
			printf_P(PSTR( "</table>Noch keine Eintr&auml;ge vorhanden<p>"
						   "<a href=\"txp-tlnverz.cgi?edit=0\">Hinzuf&uuml;gen</a></form>" ));
			}

		} // argc == 0 --> gesamte Liste ausgeben
		
	else if (PharseCheckName_P(http_request, Edit_P))
		{ // Ändern ODER Neu --> Eingabeformular anzeigen und ggf. füllen.
		TD.Nummer = atol(http_request->argvalue[PharseGetValue_P(http_request, Edit_P)]);
		if (TD.Nummer == 0 || !TlnSuche(TD.Nummer, true, &TD))
			{ // neuen oder nicht gefundenen Eintrag initialisieren.
			TD.Adresse[0] = '\0';
			TD.AdrArt = TxpUrl;
			TD.Port = TXP_PORT;
			TD.Durchwahl = 0;
			}

		CgiFormStartTabbed_P(PSTR("txp-tlnverz.cgi"));

		CgiFormInputFieldLong_P(PSTR("Nummer:"), Nummer_P, 10, TD.Nummer);

		printf_P(PSTR("<input name=\"altnummer\" type=\"hidden\" value=\"%ld\">"), TD.Nummer);
		
		const char *TypSelList[] = { TypGeloescht_P, TypTxp_P, TypAscii_P } ;
		uint8_t TypSelNr;
		switch (TD.AdrArt)
			{
			case TxpIP:
			case TxpUrl: 	TypSelNr = 1; break;
			case AsciiIP:
			case AsciiUrl: 	TypSelNr = 2; break;
			default: 		TypSelNr = 0; break;
			}
		CgiFormDropdown_P(PSTR("Typ:"), Typ_P, 3, TypSelList, TypSelNr);
		
		if (TD.AdrArt == TxpIP || TD.AdrArt == AsciiIP)
			iptostr(TD.IPAdr, TD.Adresse);
		CgiFormInputFieldText_P(PSTR("Adresse:"), Adresse_P, TlnAdresseMax-1, TD.Adresse);
		CgiFormInputFieldLong_P(PSTR("Port:"), Port_P, 5, TD.Port);
		CgiFormInputFieldLong_P(PSTR("Durchwahl:"), Durchwahl_P, 3, TD.Durchwahl);
		if (TD.Nummer == 0)
			CgiFormFinish_P(PSTR("Hinzuf&uuml;gen"));
		else
			CgiFormFinish_P(PSTR("&Auml;ndern"));
		} // Ändern oder Neu

	else if (PharseCheckName_P(http_request, Nummer_P))
		{ // neuen Einfügen oder geänderten Aktualisieren
		bool Ok = true; // nur wenn gesetzt, wird auch gespeichert
		uint32_t AltNummer = atol(http_request->argvalue[PharseGetValue_P(http_request, AltNummer_P)]);
		TD.Nummer = atol(http_request->argvalue[PharseGetValue_P(http_request, Nummer_P)]);
		char TypStr[20];
		strncpy(TypStr, http_request->argvalue[PharseGetValue_P(http_request, Typ_P)], sizeof(TypStr));
		strncpy(TD.Adresse, http_request->argvalue[PharseGetValue_P(http_request, Adresse_P)], TlnAdresseMax-1);
		TD.Adresse[TlnAdresseMax-1] = '\0'; // sicherheitshalber abhacken.

		if (TD.Nummer == 0)
			{
			printf_P(PSTR("<b>Teilnehmernummer 0 nicht erlaubt!</b><br>"));
			Ok = false;
			}
		else
			{
			printf_P(PSTR("Teilnehmereintrag:<br>Nummer: %ld "), TD.Nummer);
			if (AltNummer == 0)
				printf_P(PSTR("hinzuf&uuml;gen"));
			else if (AltNummer != TD.Nummer)
				printf_P(PSTR("ehem. %ld"), AltNummer);
			printf_P(PSTR("<br>"));
			}
		
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
					TD.AdrArt = TxpIP;
					iptostr(TD.IPAdr, TD.Adresse); // und wieder zurück wandeln
					printf_P(PSTR("TelexPhone: IP %s "), TD.Adresse);
					}
				TD.Port = atoi(http_request->argvalue[PharseGetValue_P(http_request, Port_P)]);
				TD.Durchwahl = atoi(http_request->argvalue[PharseGetValue_P(http_request, Durchwahl_P)]);
				printf_P(PSTR("Port %d Durchwahl %d<br>"), TD.Port, TD.Durchwahl);
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
				printf_P(PSTR("Port %d<br>"), TD.Port);
				}
			else
				{
				printf_P(PSTR("<b>Unbekannter Typ!</b><br>"));
				Ok = false;
				}
			}

		if (Ok && TD.AdrArt == Geloescht && AltNummer == 0)
			{ // einen neuen Lösch-Eintrag anzulegen ist doof
			printf_P(PSTR("<b>keine &Auml;nderung</b><br>"));
			Ok = false;
			}
			
		if (Ok && TD.Nummer != AltNummer && TlnSuche(TD.Nummer, false, NULL))
			{
			printf_P(PSTR("<b>Nummer ist bereits vergeben, &Auml;nderung nicht gespeichert</b><br>"));
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
			
		printf_P(PSTR("<br>Zur&uuml;ck zum <a href=\"txp-tlnverz.cgi\">Teilnehmer-Verzeichnis</a>"));
		} // if (PharseCheckName_P(http_request, Nummer_P)) ; also neuen Einfügen oder geänderten Aktualisieren
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
	TlnBuchTesteintrag(123, TxpUrl, "sonnibs.no-ip.org", 0, 134, 0);
	TlnBuchTesteintrag(124, TxpUrl, "sonnibs.no-ip.org", 0, 135, 0);
	TlnBuchTesteintrag(234, TxpIP, 0, IPDOT(192l,168l,178l,30l), 134, 0);
	TlnBuchTesteintrag(235, TxpIP, 0, IPDOT(192l,168l,178l,38l), 134, 0);
	TlnBuchTesteintrag(294, AsciiIP, 0, IPDOT(192l,168l,178l,30l), 134, 0);
	TlnBuchTesteintrag(295, AsciiIP, 0, IPDOT(192l,168l,178l,38l), 134, 0);
	TlnBuchTesteintrag(3333, AsciiIP, 0, IPDOT(192l,168l,178l,32l), 23, 0); //*/

	cgi_RegisterCGI( TlnBuch_Anzeige_CGI, PSTR("txp-tlnverz.cgi"));
	
	}
	

	
#endif //def TELEXPHONE
