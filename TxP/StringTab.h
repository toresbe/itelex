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

enum { 
	// bewirkt die Definition von Konstanten, und zwar

#define _STRTABENTRY(name, text) stridx_ ## name ,
	// nur stridx_... ,

#endif //ndef _STRTABENTRY()


// ==================================
// Hier werden die Strings definiert:
// ==================================

// für TxP.c:
_STRTABENTRY(ZweiterAnruf, "Zweiter kommender Anruf auf belegtem Telexphone-Socket")
_STRTABENTRY(MehrfacheSendeFehler, "Mehrfache FEHLER beim Senden ins Netz")
_STRTABENTRY(ZeitueberschreitungWiederaufnahme, "Zeitueberschreitung bei Wiederaufnahme der Verbindung")
_STRTABENTRY(TeilnehmerServerWiederErreichbar, "Teilnehmer-Server wieder erreicht")
_STRTABENTRY(KeinTeilnehmerServerErreichbar, "Kein Teilnehmer-Server erreichbar")
_STRTABENTRY(KeineVerbindungZumMailServerAusgang, "Keine Verbindung zum Mail-Server fuer Ausgang")
_STRTABENTRY(MailNichtInDieserVersion, "Mail in dieser Version nicht unterstuetzt")
_STRTABENTRY(TeilnehmerNichtErreichbar, "Teilnehmer nicht erreichbar")
_STRTABENTRY(AnschlussInternBesetzt, "Anschluss intern besetzt")
_STRTABENTRY(TWITimeout, "Interne Verbindung unterbrochen")
_STRTABENTRY(DiagInterneIP, "interne IP: ")
_STRTABENTRY(Datum, "Datum")
_STRTABENTRY(DiagnoseEinleitung, "\r\n///meldung: ")
_STRTABENTRY(SelbstAnrufMehrfachVersagt, "Selbst-Anruf mehrfach versagt, falsche Router-Konfiguration?")
_STRTABENTRY(NummerNichtBekannt, "gewaehlte Nummer nicht bekannt")
_STRTABENTRY(InternesVerzeichnisVoll, "internes Rufnummern-Verzeichnis voll")
_STRTABENTRY(KennwortAbfrage, "Seite gesperrt! Kennwort :")
_STRTABENTRY(Freigeben, "Freigeben")
_STRTABENTRY(FalschesKonfigKennwortEingegeben, "falsches Konfigurations-Kennwort eingegeben")
_STRTABENTRY(Druckspiegel, "Druckspiegel")
_STRTABENTRY(TexteingabeStartetFernschreiber, "Texteingabe startet Fernschreiber")
_STRTABENTRY(AndereVerbindungBesteht, "Es besteht bereits eine andere Verbindung, bitte warten.")
_STRTABENTRY(HtmlTextEingabe, "Eingabe: ")
_STRTABENTRY(HtmlTextEingabeAbsenden, " Absenden ")
_STRTABENTRY(HtmlTextEingabeAktualisieren, "Aktualisieren")
_STRTABENTRY(EigeneAmtsnummer, "Netz-Vorwahl f&uuml;r gehende Verbindungen: ")
_STRTABENTRY(FesteHauptstelle, "feste Hauptstelle f&uuml;r kommende Verbindungen: ")
_STRTABENTRY(FesteHauptstelleNummer, "interne Durchwahl der Hauptstelle f&uuml;r kommende Verbindungen: ")
_STRTABENTRY(AlternativSucheBeiBesetzt, "Alternativ-Suche bei besetzt: ")
_STRTABENTRY(DurchwahlenListe, "Durchwahlen:<br>(mit Komma trennen) ")
_STRTABENTRY(ProtokollLevel, "Protokoll-Level: ")
_STRTABENTRY(ProtokollLevelTlnServer, "Protokoll-Level f&uuml;r Teiln-Server: ")
_STRTABENTRY(DiagnoseLevel, "Level f&uuml;r Druckausgabe von Meldungen: ")
_STRTABENTRY(KonfigPasswort, "Passwort f&uuml;r Konfigurationsseiten: ")
_STRTABENTRY(TlnVerzeichnisOffen, "Teilnehmer-Verzeichnis f&uuml;r alle sichtbar: ")
_STRTABENTRY(EinstellungenUebernehmen, "Einstellung &Uuml;bernehmen")
_STRTABENTRY(NeueEinstellungen, "neue Einstellungen: ")
_STRTABENTRY(Weiter, "weiter")
_STRTABENTRY(KonnteNichtGeaendertWerden, "konnte nicht ge&auml;ndert werden")
_STRTABENTRY(KennwortGgfGeaendert, "<br>Kennwort ggf. ge&auml;ndert.")
/*
*/

// für CgiFormTools.c:
_STRTABENTRY(Unveraendert, "unver&auml;ndert")
_STRTABENTRY(GeaendertIn, "ge&auml;ndert in")
/*
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
*/

// für TlnBuch.h:
_STRTABENTRY(UeberschriftTeilnehmerverzeichnis, "<h3>Teilnehmerverzeichnis</h3><br>")
_STRTABENTRY(UeberschriftOeffentlichesTeilnehmerverzeichnis, "<h3>&Ouml;ffentliches Teilnehmerverzeichnis</h3><br>")
_STRTABENTRY(VollstaendigesTeilnehmerverzeichnis, "vollst&auml;ndiges Verzeichniss")
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
	"</tr>")
_STRTABENTRY(TeilnehmerverzeichnisAktionenOffen,
	"<a href=\"txp-tlnverz.cgi?save\">nichtfl&uuml;chtig speichern</a><br>" 
	"<a href=\"txp-tlnverz.cgi?load\">alle &Auml;nderungen verwerfen</a><br>"
	"<a href=\"txp-tlnverz.cgi?clear\">komplett l&ouml;schen</a><br>")
_STRTABENTRY(TeilnehmerverzeichnisAktionenLeerOffen, 
	"Noch keine Eintr&auml;ge vorhanden<p>"
	"<a href=\"txp-tlnverz.cgi?load\">gespeicherte Daten wiederherstellen</a><br>"
	"<a href=\"txp-tlnverz.cgi?edit=0\">Hinzuf&uuml;gen</a></form>" )
	
_STRTABENTRY(Rufnummer, "Rufnummer")
_STRTABENTRY(Name, "Name")
_STRTABENTRY(Adresse, "Adresse")
_STRTABENTRY(Port, "Port")
_STRTABENTRY(Durchwahl, "Durchwahl")
_STRTABENTRY(TlnverzAttrLokal, "Lokal")
_STRTABENTRY(TlnverzAttrGesperrt, "gesperrt")
_STRTABENTRY(TlnverzAttrDyn, "DynIP")
_STRTABENTRY(TypGeloescht, "geloescht")
_STRTABENTRY(TypAscii, "Ascii")
_STRTABENTRY(TypTxp, "i-Telex")
_STRTABENTRY(TypEMail, "eMail")
_STRTABENTRY(AktionAendern, "&Auml;ndern")
_STRTABENTRY(AktionHinzufuegen, "Hinzuf&uuml;gen")
_STRTABENTRY(Rufnummer0NichtErlaubt, "<b>Rufnummer 0 nicht erlaubt!</b><br>")
_STRTABENTRY(MeldungTlneintragRufnummer, "Teilnehmereintrag:<br>Rufnummer: %ld ")
_STRTABENTRY(EhemalsLong, "ehem. %ld")
_STRTABENTRY(UrlZusatz, ": Url %s ")
_STRTABENTRY(IPZusatz, ": IP %s ")
_STRTABENTRY(TypUnbekannt, "<b>Unbekannter Typ!</b><br>")
_STRTABENTRY(KeineAenderung, "<b>keine &Auml;nderung</b><br>")
_STRTABENTRY(RufnummerDoppelt, "<b>Rufnummer ist bereits vergeben, &Auml;nderung nicht gespeichert</b><br>")
_STRTABENTRY(EintragGespeichert, "Eintrag gespeichert<br>")
_STRTABENTRY(EintragUnveraendert, "Eintrag unver&auml;ndert<br>")
_STRTABENTRY(AlteNummerNichtGeloescht, "<b>Alte Nummer %ld konnte nicht gel&ouml;scht werden!</b><br>")
_STRTABENTRY(TeilnehmerlisteVoll, "<b>Teilnehmerliste voll, Eintrag nicht gespeichert</b><br>")
_STRTABENTRY(EepromSpeicherFehler, "<b>Fehler beim Speichern (Codes %d / %02X)</b>")
_STRTABENTRY(EepromSpeicherErfolg, "Erfolgreich gespeichert (%d Bytes)")
_STRTABENTRY(EepromLadenFehler, "<b>Fehler beim Laden (Codes %d / %02X)</b>")
_STRTABENTRY(EepromLadenErfolg, "Erfolgreich geladen (%d Bytes)")
_STRTABENTRY(KomplettGeloescht, "komplett gel&ouml;scht")
_STRTABENTRY(UngueltigerCgiAufruf, "Fehler: ungueltiger CGI-Aufruf: %s")
_STRTABENTRY(ZurueckZumTeilnehmerverzeichnis, "<br>Zur&uuml;ck zum <a href=\"txp-tlnverz.cgi\">Teilnehmer-Verzeichnis</a>")
_STRTABENTRY(ZusatzEepromFehler, "Fehler im Zusatz-EEPROM")

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
