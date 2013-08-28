#if !defined(__STRINGTAB_H__) || defined(_STRTABENTRY)
	// diesen Inhalt nur ein Mal aufnehmen, es sei denn _STRTABENTRY ist bereits definiert,
	// dann wird dieser Inhalt (in StringTab.c) genutzt, um die Strings tatsächlich im 
	// Programmspeicher anzulegen

#ifndef _STRTABENTRY
	// wenn _STRTABENTRY nicht definiert ist, wird StringTab.h "normal" genutzt und es sind
	// nur die Indizes der Strings als "stridx_..." anzulegen.

#define __STRINGTAB_H__
	// gegen doppelte Definitionen

#define STRINGTAB_H_PUR
	// als Merker, dass StringTab.h normal "included" ist.

	// bewirkt die Definition von Konstanten
	// mit dem folgenden Makro _STRTABENTRY ...
#define _STRTABENTRY(name, text_de, text_en) stridx_ ## name ,
	// wird aus 
	// _STRTABENTRY(Rufnummer, "Rufnummer", "Number")
	// _STRTABENTRY(Name, "Name", "Name")
	// nur 
	// stridx_Rufnummer,
	// stridx_Name,

enum { 
	// Definition der Konstanten stridx_xxx in der gleichen Reihenfolge wie die 
	// Zeiger auf entsprechende Strings in StringTab.c in IStrList[]
	
#endif //ndef _STRTABENTRY()


// ==================================
// Hier werden die Strings definiert:
// ==================================

// für TxP.c:
_STRTABENTRY(ZweiterAnruf, "Zweiter kommender Anruf auf belegtem Telexphone-Socket", "")
_STRTABENTRY(MehrfacheSendeFehler, "Mehrfache Fehler beim Senden ins Netz", "")
_STRTABENTRY(ZeitueberschreitungWiederaufnahme, "Zeitueberschreitung bei Wiederaufnahme der Verbindung", "")
_STRTABENTRY(KeinTeilnehmerServerErreichbar, "Kein Teilnehmer-Server erreichbar", "")
_STRTABENTRY(TeilnehmerServerWiederErreichbar, "Teilnehmer-Server wieder erreicht", "")
_STRTABENTRY(KeineVerbindungZumMailServerAusgang, "Keine Verbindung zum Mail-Server fuer Ausgang", "")
_STRTABENTRY(MailNichtInDieserVersion, "Mail in dieser Version nicht unterstuetzt", "")
_STRTABENTRY(TeilnehmerNichtErreichbar, "Teilnehmer nicht erreichbar", "")
_STRTABENTRY(AnschlussInternBesetzt, "Anschluss intern besetzt", "")
_STRTABENTRY(TWITimeout, "Interne Verbindung unterbrochen", "")
_STRTABENTRY(DiagInterneIP, "interne IP: ", "")
_STRTABENTRY(Datum, "Datum", "Date")
_STRTABENTRY(DiagnoseEinleitung, "\r\n///interne meldung: ", "\r\n///internal message:")
_STRTABENTRY(SelbstAnrufMehrfachVersagt, "Selbst-Anruf mehrfach versagt, falsche Router-Konfiguration?", "")
_STRTABENTRY(NummerNichtBekannt, "gewaehlte Nummer nicht bekannt", "")
_STRTABENTRY(InternesVerzeichnisVoll, "internes Rufnummern-Verzeichnis voll", "")
_STRTABENTRY(KennwortAbfrage, "Seite gesperrt! Bitte Kennwort eingeben", "")
_STRTABENTRY(KennwortFreigeben, "Freigeben", "")
_STRTABENTRY(KennwortFalsch, "Falsches Kennwort eingegeben!", "")
_STRTABENTRY(FalschesKonfigKennwortEingegeben, "falsches Konfigurations-Kennwort eingegeben", "")
_STRTABENTRY(Druckspiegel, "Druckspiegel", "")
_STRTABENTRY(TexteingabeStartetFernschreiber, "Texteingabe startet Fernschreiber", "")
_STRTABENTRY(AndereVerbindungBesteht, "Es besteht bereits eine andere Verbindung, bitte warten.", "")
_STRTABENTRY(HtmlTextEingabe, "Eingabe: ", "Enter text: ")
_STRTABENTRY(HtmlTextEingabeAbsenden, " Absenden ", " Submit ")
_STRTABENTRY(HtmlTextEingabeAktualisieren, "Aktualisieren", "Refresh")
_STRTABENTRY(EigeneAmtsnummer, "Netz-Vorwahl f&uuml;r gehende Verbindungen", "")
_STRTABENTRY(FesteHauptstelle, "feste Hauptstelle f&uuml;r kommende Verbindungen", "")
_STRTABENTRY(FesteHauptstelleNummer, "interne Durchwahl der Hauptstelle f&uuml;r kommende Verbindungen", "")
_STRTABENTRY(AlternativSucheBeiBesetzt, "Alternativ-Suche bei besetzt", "")
_STRTABENTRY(DurchwahlenListe, "Durchwahlen (mit Komma trennen)", "")
_STRTABENTRY(ProtokollLevel, "Tiefe der Protokollierung an serieller Schnittstelle", "")
_STRTABENTRY(ProtokollLevelTlnServer, "Tiefe der Protokollierung f&uuml;r Teiln-Server", "")
_STRTABENTRY(DiagnoseLevel, "Level f&uuml;r Druckausgabe von Meldungen", "")
_STRTABENTRY(KonfigPasswort, "Passwort f&uuml;r Konfigurationsseiten", "")
_STRTABENTRY(TlnVerzeichnisOffen, "Teilnehmer-Verzeichnis f&uuml;r alle sichtbar", "")
_STRTABENTRY(KonnteNichtGeaendertWerden, " konnte nicht ge&auml;ndert werden", "")
_STRTABENTRY(KennwortGgfGeaendert, "<br>Kennwort ggf. ge&auml;ndert.", "")
_STRTABENTRY(InternesKennwortFehlt, "zun&auml;chst Passwort in <a href=\"txpcfg-intern.cgi\" target=\"main\">Einstellungen im lokalen TxP-System</a> eingeben!", "")
_STRTABENTRY(GesperrtBestaetigung, "Konfigurationsseiten sind nun gesperrt. Zur Freigabe wieder das Passwort eingeben oder Taste der Baugruppe 2 x dr&uuml;cken.", "")
_STRTABENTRY(ITelexRufnummer, "eigene Rufnummer im i-telex-Netz", "")
_STRTABENTRY(ITelexRufnummerZuKurz, "<big><b>&lt;=== zu wenig Ziffern!</b></big>", "")
_STRTABENTRY(RufnrServerAnmeldGeheimzahl, "Geheimzahl zur Anmeldung beim Rufnummern-Server", "")
_STRTABENTRY(DynIPAktiv, "Dynamische IP-Aktualisierung aktiv", "")
_STRTABENTRY(VerbindungstestPeriode, "Verbindungstest-Periode", "")
_STRTABENTRY(OeffentlichePortNr, "&ouml;ffentliche Port-Nummer", "Public port number")
_STRTABENTRY(RufnrServerAdr, "Adresse des Teilnehmer-Server", "")
_STRTABENTRY(TlnServSyncGeheimzahl, "Geheimzahl f&uuml;r Server-Synchronisierung", "")
// ungeprüft:
_STRTABENTRY(TwiTlnListeAnfang, "Status der angeschlossenen TWI-Module:<p>", "")
_STRTABENTRY(TwiTlnListeEintrag, "Nummer %s Status %02X<br>", "Number %s status %02X<br>")
_STRTABENTRY(TwiTlnListeEnde, "+++fertig", "+++finish")

// für CgiFormTools.c und sonst häufig verwendet:
_STRTABENTRY(NeueEinstellungen, "Die neuen Einstellungen sind: ", "The new settings are: ")
_STRTABENTRY(Weiter, "weiter", "next")
_STRTABENTRY(Unveraendert, " unver&auml;ndert", " unchanged")
_STRTABENTRY(GeaendertIn, " ge&auml;ndert in", " changed to")
_STRTABENTRY(EinstellungenUebernehmen, "Einstellung &Uuml;bernehmen", "Submit changes")

// für TlnBuch.c:
_STRTABENTRY(UeberschriftTeilnehmerverzeichnis, "<h3>Teilnehmerverzeichnis</h3><br>", "<h3>Directory</h3><br>")
_STRTABENTRY(UeberschriftOeffentlichesTeilnehmerverzeichnis, 
				"<h3>&Ouml;ffentliches Teilnehmerverzeichnis</h3><br>", 
				"<h3>Public directory</h3><br>")
_STRTABENTRY(VollstaendigesTeilnehmerverzeichnis, "zeige vollst&auml;ndiges Verzeichnis", "")
_STRTABENTRY(TeilnehmerverzeichnisHtmlKopf,
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
	"</tr>", 
	"")
_STRTABENTRY(TeilnehmerverzeichnisAktionenOffen,
	"<a href=\"txp-tlnverz.cgi?save\">nichtfl&uuml;chtig speichern</a><br>" 
	"<a href=\"txp-tlnverz.cgi?load\">alle &Auml;nderungen verwerfen</a><br>"
	"<a href=\"txp-tlnverz.cgi?clear\">komplett l&ouml;schen</a><br>", 
	"")
_STRTABENTRY(TeilnehmerverzeichnisAktionenLeerOffen, 
	"Noch keine Eintr&auml;ge vorhanden<p>"
	"<a href=\"txp-tlnverz.cgi?load\">gespeicherte Daten wiederherstellen</a><br>"
	"<a href=\"txp-tlnverz.cgi?edit=0\">Hinzuf&uuml;gen</a></form>",
	"")
	
_STRTABENTRY(Rufnummer, "Rufnummer", "")
_STRTABENTRY(Name, "Name", "")
_STRTABENTRY(Adresse, "Adresse", "")
_STRTABENTRY(Port, "Port", "")
_STRTABENTRY(Durchwahl, "Durchwahl", "")
_STRTABENTRY(TlnverzAttrLokal, "Lokal", "")
_STRTABENTRY(TlnverzAttrGesperrt, "gesperrt", "")
_STRTABENTRY(Typ, "Typ", "")
_STRTABENTRY(TlnverzAttrDyn, "DynIP", "")
_STRTABENTRY(TypGeloescht, "geloescht", "")
_STRTABENTRY(TypAscii, "Ascii", "")
_STRTABENTRY(TypTxp, "i-Telex", "")
_STRTABENTRY(TypEMail, "eMail", "")
_STRTABENTRY(AktionAendern, "&Auml;ndern", "")
_STRTABENTRY(AktionHinzufuegen, "Hinzuf&uuml;gen", "")
_STRTABENTRY(Rufnummer0NichtErlaubt, "<b>Rufnummer 0 nicht erlaubt!</b><br>", "")
_STRTABENTRY(MeldungTlneintragRufnummer, "Teilnehmereintrag:<br>Rufnummer: %ld ", "")
_STRTABENTRY(EhemalsLong, "ehem. %ld", "")
_STRTABENTRY(UrlZusatz, ": Url %s ", "")
_STRTABENTRY(IPZusatz, ": IP %s ", "")
_STRTABENTRY(TypUnbekannt, "<b>Unbekannter Typ!</b><br>", "")
_STRTABENTRY(KeineAenderung, "<b>keine &Auml;nderung</b><br>", "")
_STRTABENTRY(RufnummerDoppelt, "<b>Rufnummer ist bereits vergeben, &Auml;nderung nicht gespeichert</b><br>", "")
_STRTABENTRY(EintragGespeichert, "Eintrag gespeichert<br>", "")
_STRTABENTRY(EintragUnveraendert, "Eintrag unver&auml;ndert<br>", "")
_STRTABENTRY(AlteNummerNichtGeloescht, "<b>Alte Nummer %ld konnte nicht gel&ouml;scht werden!</b><br>", "")
_STRTABENTRY(EepromSpeicherFehler, "<b>Fehler beim Speichern (Codes %d / %02X)</b>", "")
_STRTABENTRY(EepromSpeicherErfolg, "Erfolgreich gespeichert (%d Bytes)", "")
_STRTABENTRY(EepromLadenFehler, "<b>Fehler beim Laden (Codes %d / %02X)</b>", "")
_STRTABENTRY(EepromLadenErfolg, "Erfolgreich geladen (%d Bytes)", "")
_STRTABENTRY(KomplettGeloescht, "komplett gel&ouml;scht", "")
_STRTABENTRY(UngueltigerCgiAufruf, "Fehler: ungueltiger CGI-Aufruf: %s", "")
_STRTABENTRY(ZurueckZumTeilnehmerverzeichnis, "<br>Zur&uuml;ck zum <a href=\"txp-tlnverz.cgi\">Teilnehmer-Verzeichnis</a>", "")
_STRTABENTRY(ZusatzEepromFehler, "Fehler im Zusatz-EEPROM", "")


// für TlnServer.c:
// ungeprüft:
_STRTABENTRY(NeuTeilnehmer, "Neuer Teilnehmer angemeldet", "")
_STRTABENTRY(TeilnehmerlisteVoll, "Rufnummern-Verzeichnis voll, nicht gespeichert", "")
_STRTABENTRY(ServerAnmeldungFalscheGeheimzahl, "Teilnehmer-Server Anmeldung mit falscher Geheimzahl", "")

// für eMail.c:
// ungeprüft:
_STRTABENTRY(MailEmpfangStartzeile, "\r\n///email empfangen:\r\n", "")
_STRTABENTRY(MailEingabeBetreff, "\r\nbetreff:\r\n", "")
_STRTABENTRY(MailEingabeText, "\r\ntext:\r\n", "")
_STRTABENTRY(EmailKonfigPopServer, "POP-Server Adresse", "")
_STRTABENTRY(EmailKonfigSmtpServer, "SMTP-Server Adresse", "")
_STRTABENTRY(EmailKonfigEigeneAdresse, "Eigene eMail-Adresse", "")
_STRTABENTRY(EmailKonfigKennwort, "Kennwort f&uuml;r eMail-Server", "")
_STRTABENTRY(EmailKonfigAbfragetakt, "Takt des eMail-Abrufs (in Minuten; 0 = ausgeschaltet)", "")
_STRTABENTRY(EmailKonfigFilterNurTX, "Nur eMails mit +TX+ im Subject drucken", "")
_STRTABENTRY(EmailKonfigFehler, "<br><b>Konfigurationsdaten unvollst&auml;ndig, Abfragetakt auf Null gesetzt, eMail Abfrage ist ausgeschaltet.</b>", "")
_STRTABENTRY(SMTPFehlerAnfang, "eMail-Ausgang-Server ", "")
_STRTABENTRY(SMTPFehlerIPNichtErmittelbar, " IP nicht ermittelbar", "")
_STRTABENTRY(SMTPFehlerNotConnected, " konnte nicht verbunden werden", "")
_STRTABENTRY(SMTPFehlerDirekt, "Meldung vom eMail-Ausgang-Server: ", "")


// ==================================
// Ende der Stringtabelle
// ==================================


#ifdef STRINGTAB_H_PUR
	// in der "echten" StringTab.h:

} ; 
	// Ende von enum { zur Konstantendefinition

#undef STRINGTAB_H_PUR
	// Merker löschen

#undef _STRTABENTRY
	// Hilfsmakro löschen

extern PGM_P GetIStr(uint16_t stri);
	// Funktion für die Ermittlung eines Strings aus dem Index

#define ISTR(name) GetIStr(stridx_ ## name)
	// Vereinfachendes Hilfsmakro

#endif // def STRINGTAB_H_PUR


#endif // !defined(__STRINGTAB_H__) || defined(_STRTABENTRY)
