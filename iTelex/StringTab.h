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

// für iTelex.c:
_STRTABENTRY(ZweiterAnruf, "Zweiter kommender Anruf auf belegtem i-Telex-Socket", "Second incoming call on busy i-telex socket")
_STRTABENTRY(MehrfacheSendeFehler, "Mehrfache Fehler beim Senden ins Netz", "Multiple errors while sending on network")
_STRTABENTRY(ZeitueberschreitungWiederaufnahme, "Zeitueberschreitung bei Wiederaufnahme der Verbindung", "Timeout while waiting for a reconnection")
_STRTABENTRY(KeinTeilnehmerServerErreichbar, "Kein Teilnehmer-Server erreichbar", "No subscriber directory server available")
_STRTABENTRY(TeilnehmerServerWiederErreichbar, "Teilnehmer-Server wieder erreicht", "Subscriber directory server available again")
_STRTABENTRY(KeineVerbindungZumMailServerAusgang, "Keine Verbindung zum Mail-Server fuer Ausgang", "No connection to server for outgoing mails")
_STRTABENTRY(MailNichtInDieserVersion, "Mail in dieser Version nicht unterstuetzt", "Mail not supported in this version")
_STRTABENTRY(TeilnehmerNichtErreichbar, "Teilnehmer nicht erreichbar", "Subscriber not available")
_STRTABENTRY(AnschlussInternBesetzt, "Anschluss intern besetzt", "Local connection busy")
_STRTABENTRY(TWITimeout, "Interne Verbindung unterbrochen", "Lost local connection")
_STRTABENTRY(NamensucheTexteingabe, "suche nach:     ", "search for:     ")
_STRTABENTRY(NamensucheErgebnisse, "nummer / name:\r\n", "number / name:\r\n")
_STRTABENTRY(DiagInterneIP, "interne IP: ", "local IP: ")
_STRTABENTRY(Datum, "Datum", "Date")
_STRTABENTRY(DiagnoseEinleitung, "\r\n///interne meldung: ", "\r\n///internal message: ")
_STRTABENTRY(SelbstAnrufMehrfachVersagt, 
			 "Selbst-Anruf mehrfach versagt, Router-Konfiguration pruefen.\r\nNach Fehlerbehebung Dynamische IP-Aktualisierung wieder einschalten.", 
			 "Loopback call failed constantly. Check router configuration.\r\nAfter check, enable the dynamic update of IP adress again.")
_STRTABENTRY(NummerNichtBekannt, "gewaehlte Nummer nicht bekannt", "subscriber number not known")
_STRTABENTRY(InternesVerzeichnisVoll, "internes Rufnummern-Verzeichnis voll", "internal directory out of space")
_STRTABENTRY(KennwortAbfrage, "Seite gesperrt! Bitte Kennwort eingeben", "Page locked! Enter password")
_STRTABENTRY(KennwortFreigeben, "Freigeben", "Unlock")
_STRTABENTRY(KennwortFalsch, "Falsches Kennwort eingegeben!", "Wrong password entered!")
_STRTABENTRY(FalschesKonfigKennwortEingegeben, "unautorisierter Zugriff auf Konfigurationsseite", "unauthorizes access to locked configuratiuon page")
_STRTABENTRY(Druckspiegel, "Druckspiegel", "printout mirror")
_STRTABENTRY(TexteingabeStartetFernschreiber, "Texteingabe startet Fernschreiber", "Enter text to start printer")
_STRTABENTRY(AndereVerbindungBesteht, "Es besteht bereits eine andere Verbindung, bitte warten.", "Connection busy, please wait.")
_STRTABENTRY(HtmlTextEingabe, "Eingabe: ", "Enter text: ")
_STRTABENTRY(HtmlTextEingabeAbsenden, " Absenden ", " Send ")
_STRTABENTRY(HtmlTextEingabeAktualisieren, "Aktualisieren", "Refresh")
_STRTABENTRY(EigeneAmtsnummer, "Netz-Vorwahl f&uuml;r gehende Verbindungen", "Outside line number")
_STRTABENTRY(FesteHauptstelle, "feste Hauptstelle f&uuml;r kommende Verbindungen", "Primary teleprinter for incoming calls")
_STRTABENTRY(FesteHauptstelleNummer, "interne Durchwahl der Hauptstelle f&uuml;r kommende Verbindungen", "Internal call number of primary teleprinter for incoming calls")
_STRTABENTRY(AlternativSucheBeiBesetzt, "Alternativ-Suche bei besetzt", "Use alternative secondary teleprinter if primary is busy")
_STRTABENTRY(DurchwahlenListe, "Durchwahlen (mit Komma trennen)", "Secondary call numbers list (seperate by comma)")
_STRTABENTRY(ProtokollLevel, "Tiefe der Protokollierung an serieller Schnittstelle", "Logging level at serial port")
_STRTABENTRY(ProtokollLevelTlnServer, "Tiefe der Protokollierung f&uuml;r Teiln-Server", "Logging level for directory server")
_STRTABENTRY(DiagnoseLevel, "Level f&uuml;r Druckausgabe von Meldungen", "Message filtering level")
_STRTABENTRY(KonfigPasswort, "Passwort f&uuml;r Konfigurationsseiten", "Password for configuration pages")
_STRTABENTRY(TlnVerzeichnisOffen, "Teilnehmer-Verzeichnis f&uuml;r alle sichtbar", "Local subscriber directory visible for everybody")
_STRTABENTRY(DatumDruckModus, "Datum bei Anrufen automatisch drucken", "Print date on incoming calls")
_STRTABENTRY(DatumDruckKein, "aus", "off")
_STRTABENTRY(DatumDruckLokal, "nur lokal", "only local")
_STRTABENTRY(DatumDruckAnrufer, "nur beim Anrufer", "only at caller")
_STRTABENTRY(DatumDruckBeide, "lokal und Anrufer", "local and caller")
_STRTABENTRY(KonnteNichtGeaendertWerden, " konnte nicht ge&auml;ndert werden", " could not be changed")
_STRTABENTRY(KennwortGgfGeaendert, "<br>Kennwort ggf. ge&auml;ndert.", "<br>Passwort if necessary changed")
_STRTABENTRY(InternesKennwortFehlt, 
			 "zun&auml;chst Passwort in <a href=\"itelexcfg-intern.cgi?spr=de\" target=\"main\">Einstellungen im lokalen TxP-System</a> eingeben!", 
			 "First select password in <a href=\"itelexcfg-intern.cgi?spr=en\" target=\"main\">Settings for the local TxP-system</a>!")
_STRTABENTRY(GesperrtBestaetigung, 
			 "Konfigurationsseiten sind nun gesperrt. Zur Freigabe wieder das Passwort eingeben oder Taste der Baugruppe 2 x dr&uuml;cken.", 
			 "Configuration pages are locked again. For unlocking enter password oder push button at i-telex hardware module twice.")
_STRTABENTRY(ITelexRufnummer, "eigene Rufnummer im i-telex-Netz", "Subscriber's number")
_STRTABENTRY(ITelexRufnummerZuKurz, "<big><b>&lt;=== muss fünf bis neun Ziffern haben!</b></big>", "<big><b>&lt;=== must have five to nine digits!</b></big>")
_STRTABENTRY(RufnrServerAnmeldGeheimzahl, "Geheimzahl zur Anmeldung beim Rufnummern-Server", "PIN for sign in at subscriber directory server")
_STRTABENTRY(DynIPAktiv, "Dynamische IP-Aktualisierung aktiv", "Dynamic update of IP adress active")
_STRTABENTRY(VerbindungstestPeriode, "Verbindungstest-Periode (0 f&uuml;r kein Test)", "Loopback test period (0 to swich off)")
_STRTABENTRY(OeffentlichePortNr, "&ouml;ffentliche Port-Nummer", "Public internet port number")
_STRTABENTRY(RufnrServerAdr, "Adresse des Teilnehmer-Server", "Subscriber directory server address")
_STRTABENTRY(TlnServSyncGeheimzahl, "Geheimzahl f&uuml;r Server-Synchronisierung", "PIN for directory server synchronisation")
_STRTABENTRY(TwiTlnListeAnfang, "Status der angeschlossenen Module:<p>", "Status of all connected hardware modules:<p>")
_STRTABENTRY(TwiTlnListeEintrag, "Nummer %s Status %02X<br>", "Number %s status %02X<br>")
_STRTABENTRY(TwiTlnListeEnde, "+++fertig", "+++end")

// für CgiFormTools.c und sonst häufig verwendet:
_STRTABENTRY(NeueEinstellungen, "Die neuen Einstellungen sind: ", "The new settings are: ")
_STRTABENTRY(Weiter, "weiter", "continue")
_STRTABENTRY(Unveraendert, " unver&auml;ndert", " unchanged")
_STRTABENTRY(GeaendertIn, " ge&auml;ndert in", " changed to")
_STRTABENTRY(EinstellungenUebernehmen, "Einstellung &Uuml;bernehmen", "Submit changes")

// für TlnBuch.c:
_STRTABENTRY(UeberschriftTeilnehmerverzeichnis, "<h3>Teilnehmerverzeichnis</h3><br>", "<h3>Subscriber directory</h3><br>")
_STRTABENTRY(UeberschriftOeffentlichesTeilnehmerverzeichnis, 
				"<h3>&Ouml;ffentliches Teilnehmerverzeichnis</h3><br>", 
				"<h3>Public subscriber directory</h3><br>")
_STRTABENTRY(VollstaendigesTeilnehmerverzeichnis, "zeige vollst&auml;ndiges Verzeichnis", "show complete directory with private entries")
_STRTABENTRY(TeilnehmerverzeichnisHtmlKopf,
	"<table border=\"1\" cellpadding=\"2\" cellspacing=\"0\">"
	"<tr>"
	"<th align=\"left\">Rufnummer<br>sortiere <a href=\"itelex-tlnverz.cgi?sort=nummer\">auf</a> <a href=\"itelex-tlnverz.cgi?sort=nummer&ab\">ab</a></th>" // Nummer
	"<th align=\"left\">Name<br>sortiere <a href=\"itelex-tlnverz.cgi?sort=name\">auf</a> <a href=\"itelex-tlnverz.cgi?sort=name&ab\">ab</a></th>" // Name
	"<th align=\"center\">Besond.</th>" // Flags
	"<th align=\"left\">Typ</th>" // Typ
	"<th align=\"left\">Adresse</th>" // Adresse
	"<th align=\"center\">Port</th>" // Port
	"<th align=\"center\">Durchwahl</th>" // Durchwahl
	"<th align=\"center\">letzte<br>Aktualisierung<br>sortiere <a href=\"itelex-tlnverz.cgi?sort=datum\">auf</a> <a href=\"itelex-tlnverz.cgi?sort=datum&ab\">ab</a></th>" // Datum / Uhrzeit
	"<th align=\"left\">Aktion</th>" // in dieser Spalte sind die Buttons
	"</tr>", 
	"<table border=\"1\" cellpadding=\"2\" cellspacing=\"0\">"
	"<tr>"
	"<th align=\"left\">Number<br>sort <a href=\"itelex-tlnverz.cgi?sort=nummer\">up</a> <a href=\"itelex-tlnverz.cgi?sort=nummer&ab\">down</a></th>" // Nummer
	"<th align=\"left\">Name<br>sort <a href=\"itelex-tlnverz.cgi?sort=name\">up</a> <a href=\"itelex-tlnverz.cgi?sort=name&ab\">down</a></th>" // Name
	"<th align=\"center\">Specials</th>" // Flags
	"<th align=\"left\">Type</th>" // Typ
	"<th align=\"left\">Address</th>" // Adresse
	"<th align=\"center\">Port</th>" // Port
	"<th align=\"center\">Direct dial</th>" // Durchwahl
	"<th align=\"center\">Updated<br>sort <a href=\"itelex-tlnverz.cgi?sort=datum\">up</a> <a href=\"itelex-tlnverz.cgi?sort=datum&ab\">down</a></th>" // Datum / Uhrzeit
	"<th align=\"left\">Action</th>" // in dieser Spalte sind die Buttons
	"</tr>")
	
_STRTABENTRY(TeilnehmerverzeichnisAktionenOffen,
	"<a href=\"itelex-tlnverz.cgi?save\">nichtfl&uuml;chtig speichern</a><br>" 
	"<a href=\"itelex-tlnverz.cgi?load\">alle &Auml;nderungen verwerfen</a><br>"
	"<a href=\"itelex-tlnverz.cgi?clear\">komplett l&ouml;schen</a><br>", 
	"<a href=\"itelex-tlnverz.cgi?save\">save non-volatile</a><br>" 
	"<a href=\"itelex-tlnverz.cgi?load\">discard all changes</a><br>"
	"<a href=\"itelex-tlnverz.cgi?clear\">clear all</a><br>")
_STRTABENTRY(TeilnehmerverzeichnisAktionenLeerOffen, 
	"Noch keine Eintr&auml;ge vorhanden<p>"
	"<a href=\"itelex-tlnverz.cgi?load\">gespeicherte Daten wiederherstellen</a><br>"
	"<a href=\"itelex-tlnverz.cgi?edit=0\">Hinzuf&uuml;gen</a></form>",
	"No entries yet<p>"
	"<a href=\"itelex-tlnverz.cgi?load\">restore last saved entries</a><br>"
	"<a href=\"itelex-tlnverz.cgi?edit=0\">add entry</a></form>")
	
_STRTABENTRY(Rufnummer, "Rufnummer", "Number")
_STRTABENTRY(Name, "Name", "Name")
_STRTABENTRY(Adresse, "Adresse", "Address")
_STRTABENTRY(Port, "Port", "Port")
_STRTABENTRY(Durchwahl, "Durchwahl", "Direct dial")
_STRTABENTRY(TlnverzAttrLokal, "Lokal", "local")
_STRTABENTRY(TlnverzAttrGesperrt, "gesperrt", "locked")
_STRTABENTRY(Typ, "Typ", "Type")
_STRTABENTRY(TlnverzAttrDyn, "DynIP", "DynIP")
_STRTABENTRY(TypGeloescht, "geloescht", "deleted")
_STRTABENTRY(TypAscii, "Ascii", "Ascii")
_STRTABENTRY(TypITelex, "i-Telex", "i-Telex")
_STRTABENTRY(TypEMail, "eMail", "eMail")
_STRTABENTRY(AktionAendern, "&Auml;ndern", "edit")
_STRTABENTRY(AktionHinzufuegen, "Hinzuf&uuml;gen", "add")
_STRTABENTRY(Rufnummer0NichtErlaubt, "<b>Rufnummer 0 nicht erlaubt!</b><br>", "<b>Number 0 not allowed!</b><br>")
_STRTABENTRY(MeldungTlneintragRufnummer, "Teilnehmereintrag:<br>Rufnummer: %ld ", "Directory entry:<br>Number: %ld ")
_STRTABENTRY(EhemalsLong, "ehem. %ld", "former %ld")
_STRTABENTRY(HostnameZusatz, ": Hostname %s ", ": Hostname %s ")
_STRTABENTRY(IPZusatz, ": IP %s ", ": IP %s ")
_STRTABENTRY(TypUnbekannt, "<b>Unbekannter Typ!</b><br>", "<b>Type unknown!</b><br>")
_STRTABENTRY(KeineAenderung, "<b>keine &Auml;nderung</b><br>", "<b>unchanged</b><br>")
_STRTABENTRY(LokaleSpracheGespeichert, "<br>Sprache ge&auml;ndert nach Deutsch", "<br>Language changed to English")
_STRTABENTRY(RufnummerDoppelt, 
			 "<b>Rufnummer ist bereits vergeben, &Auml;nderung nicht gespeichert</b><br>", 
			 "<b>Number already used, change not saved</b><br>")
_STRTABENTRY(EintragGespeichert, "Eintrag gespeichert<br>", "Entry saved<br>")
_STRTABENTRY(EintragUnveraendert, "Eintrag unver&auml;ndert<br>", "Entry unchanged<br>")
_STRTABENTRY(AlteNummerNichtGeloescht, 
			 "<b>Alte Nummer %ld konnte nicht gel&ouml;scht werden!</b><br>", 
			 "<b>Former number %ld could not be deleted!</b><br>")
_STRTABENTRY(EepromSpeicherFehler, "<b>Fehler beim Speichern (Codes %d / %02X)</b>", "<b>Error at saving (codes %d / %02X)</b>")
_STRTABENTRY(EepromSpeicherErfolg, "Erfolgreich gespeichert (%d Bytes)", "saved successfully (%d byte)")
_STRTABENTRY(EepromLadenFehler, "<b>Fehler beim Laden (Codes %d / %02X)</b>", "<b>Error at loading (codes %d / %02X)</b>")
_STRTABENTRY(EepromLadenErfolg, "Erfolgreich geladen (%d Bytes)", "loaded successfully (%d byte)")
_STRTABENTRY(KomplettGeloescht, "komplett gel&ouml;scht", "completely cleared")
_STRTABENTRY(UngueltigerCgiAufruf, "Fehler: ungueltiger CGI-Aufruf: %s", "Error: invalid cgi-call: %s")
_STRTABENTRY(ZurueckZumTeilnehmerverzeichnis, 
			 "<br>Zur&uuml;ck zum <a href=\"itelex-tlnverz.cgi?spr=de\">Teilnehmer-Verzeichnis</a>", 
			 "<br>back to <a href=\"itelex-tlnverz.cgi?spr=en\">subscriber directory</a>")
_STRTABENTRY(ZusatzEepromFehler, "Fehler im Zusatz-EEPROM", "Error in directory EEPROM")


// für TlnServer.c:
_STRTABENTRY(NeuTeilnehmer, "Neuer Teilnehmer angemeldet", "new subscriber signed up")
_STRTABENTRY(TeilnehmerlisteVoll, "Rufnummern-Verzeichnis voll, nicht gespeichert", "directory out of space, not saved")
_STRTABENTRY(ServerAnmeldungFalscheGeheimzahl, "Teilnehmer-Server Anmeldung mit falscher Geheimzahl", "Unauthorized access to directory server port")

// für eMail.c:
// ungeprüft:
_STRTABENTRY(MailEmpfangStartzeile, "\r\n///email empfangen:\r\n", "\r\n///email received:\r\n")
_STRTABENTRY(MailEingabeBetreff, "\r\nbetreff:\r\n", "\r\nsubject:\r\n")
_STRTABENTRY(MailEingabeText, "\r\ntext:\r\n", "\r\nbody text:\r\n")
_STRTABENTRY(EmailKonfigPopServer, "POP-Server Adresse", "POP server address")
_STRTABENTRY(EmailKonfigSmtpServer, "SMTP-Server Adresse", "SMTP server address")
_STRTABENTRY(EmailKonfigEigeneAdresse, "Eigene eMail-Adresse", "Own email address")
_STRTABENTRY(EmailKonfigKennwort, "Kennwort f&uuml;r eMail-Server", "Password for email servers")
_STRTABENTRY(EmailKonfigAbfragetakt, "Takt des eMail-Abrufs (in Minuten; 0 = ausgeschaltet)", "Polling period for email query")
_STRTABENTRY(EmailKonfigFilterNurTX, "Nur eMails mit +TX+ im Subject drucken", "Print only emails with +tx+ in subject")
_STRTABENTRY(EmailKonfigFehler, 
			 "<br><b>Konfigurationsdaten unvollst&auml;ndig, Abfragetakt auf Null gesetzt, eMail Abfrage ist ausgeschaltet.</b>", 
			 "<br><b>Configuration not complete, polling period set to zero, incoming eMail will not be printed.</b>")
_STRTABENTRY(SMTPFehlerAnfang, "eMail-Ausgang-Server ", "Server for outgoing email")
_STRTABENTRY(SMTPFehlerIPNichtErmittelbar, " IP nicht ermittelbar", " no IP address was found")
_STRTABENTRY(SMTPFehlerNotConnected, " konnte nicht verbunden werden", " connection failed")
_STRTABENTRY(SMTPFehlerDirekt, "Meldung vom eMail-Ausgang-Server: ", "Message from the Server for outgoing email: ")

// für ConfigNtp.c:
_STRTABENTRY(NtpOn, "Uhrzeit vom Server abfragen", "Use time server")
_STRTABENTRY(NtpServerHostname, "Hostname des Zeitservers", "Time server host name")
_STRTABENTRY(Zeitzone, "Zeitzone", "Timezone")
_STRTABENTRY(AutoSommerzeit, "Sommerzeit automatisch umstellen", "Use daylight saving time (european)")
			 
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

typedef enum {
	Deutsch,
	Englisch,
	} TSprache;
	
extern PGM_P GetIStr(uint16_t stri, TSprache Sprache);
	// Funktion für die Ermittlung eines Strings aus dem Index

#define ISTR(name, Sprache) GetIStr(stridx_ ## name, Sprache)
	// Vereinfachendes Hilfsmakro

#endif // def STRINGTAB_H_PUR


#endif // !defined(__STRINGTAB_H__) || defined(_STRTABENTRY)
