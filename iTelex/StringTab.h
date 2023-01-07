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
#define _STRTABENTRY(name, text_de, text_en, text_it, text_nl) stridx_ ## name ,
	// wird aus 
	// _STRTABENTRY(Rufnummer, "Rufnummer", "Number", "Numero", "Nummer")
	// _STRTABENTRY(Name, "Name", "Name", "Nome", "Naam")
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
_STRTABENTRY(AnrufAbgewiesenWegenBesetzt, "kommender Anruf abgewiesen wegen besetzt", "incoming call cancelled because busy", 
	"Chiamata in entrata cencellata perche' occupato", 
	"Inkomende verbinding geannuleerd wegens bezet")
_STRTABENTRY(MehrfacheSendeFehler, "Mehrfache Fehler beim Senden ins Netz", "Multiple errors while sending on network", 
	"Errori multipli trasmettendo sulla rete", "Meervoudige fouten bij zenden op het netwerk")
_STRTABENTRY(ZeitueberschreitungWiederaufnahme, "Zeitueberschreitung bei Wiederaufnahme der Verbindung", "Timeout while waiting for a reconnection",
	"timeout in attesa di re-collegamento", "Timeout in afwachting van heraansluiting")
_STRTABENTRY(KeinTeilnehmerServerErreichbar, 
	"kein teilnehmer- / verbindungs-server erreichbar: verbindungsversuche koennen fehlschlagen.", 
	"no subscriber directory / connection server available: Connections may fail.", 
 	"no server per abbonati / connessioni disponibile: Connessioni non possibile",
	"Geen server voor abonnees / verbindingen bereikbaar: Verbindingen zijn niet mogelijk")
_STRTABENTRY(TeilnehmerServerWiederErreichbar, "server wieder erreicht.", "server available again.",
	"Server nuovamente disponibile.", "Server is weer beschikbaar.")
_STRTABENTRY(KeineVerbindungZumMailServerAusgang, "Keine Verbindung zum Mail-Server fuer Ausgang", "No connection to server for outgoing mails",
	"No Connessione al mail-server in uscita", "Geen verbinding met de uitgaande mail-server")
_STRTABENTRY(MailNichtInDieserVersion, "Mail in dieser Version nicht unterstuetzt", "Mail not supported in this version",
	"Mail non e' supportato in questo versione", "Mail is niet beschikbaar in deze versie")
_STRTABENTRY(ServerAusWegenFehlenderUhrzeit, 
	"Zeit-Server war nicht verfuegbar, teilnehmer-server nicht gestartet. bitte diese schnittstelle neu starten.", 
	"time server not available. subscriber server not started, please restart this interface again.",
	"Server per l'orario non disponibile, riavviare l'interfaccia per favore.",
	"Server voor de tijd is niet beschikbaar, gelieve het interface nieuw op te starten")
_STRTABENTRY(TeilnehmerBesetzt, "Teilnehmer besetzt", "Subscriber busy", "Abbonato occupato", "Abonnee is bezet")
_STRTABENTRY(TeilnehmerNichtErreichbar, "Teilnehmer nicht erreichbar", "Subscriber not available", 
	"Abbonato non raggiungibile", "abonnee niet bereikbaar")
_STRTABENTRY(TeilnehmerNichtErlaubt, "Teilnehmer nicht erlaubt", "Subscriber not allowed", "Abbonato non permesso", "abonnee niet toegestaan" )
_STRTABENTRY(TeilnehmerGestoert, "Teilnehmer gestoert", "Subscriber derailed", "Abbonato guasto", "abonnee gestoord")
_STRTABENTRY(TeilnehmerAbgeschaltet, "Teilnehmer abwesend / abgeschaltet", "Subscriber absent / deactivated",
	"Abbonato assente / deattivato", "Abonnee absent / gedeaktiveerd")
_STRTABENTRY(VerbindungGetrennt, "getrennt", "break", "interrotto", "afgebroken")
_STRTABENTRY(SonstigeMeldung, "sonstige meldung: ", "special message: ","Messaggio speciale: ", "Speciaal bericht: ")
_STRTABENTRY(AnschlussInternBesetzt, "Anschluss intern besetzt", "Local connection busy", "Connessione locale occupato",
	"Locale aansluiting bezet")
_STRTABENTRY(TWITimeout, "Interne Verbindung unterbrochen", "Lost local connection", "Connessione locale interrotto",
	"Locale aaunsluiting onderbroken")
_STRTABENTRY(NamensucheTexteingabe, "\r\nteilnehmersuche nach:      ", "\r\nsearch subscribers for:      ",
	"\r\nricerca abbonati per:     ","\r\nzoeken abbonees voor:      ")
_STRTABENTRY(NamensucheBitteWarten, "\r\nmom\r\n", "\r\nmom\r\n","\r\nmom\r\n", "\r\nmom\r\n")
_STRTABENTRY(NamensucheZuKurz, "mindestens 3 zeichen eingeben.      \r\n\n", "enter at least three characters.      \r\n\n",
	"digita almeno 3 caratteri      \r\n\n", "tenminste 3 letters ingeven      \r\n\n")
_STRTABENTRY(NamensucheServerAbbruch, "fehler bei server-abfrage. ", "error in subscriber server answer. ",
	"errore nella la risposta dal server.", "fout bij het antwoord van de server. ")
_STRTABENTRY(NamensucheNurLokal, "lokal vorhandene eintraege:\r\n", "locally stored entries:\r\n",
	"dati salvati localmente:\r\n","lokaal weggeschreven daten:\r\n")
_STRTABENTRY(NamensucheErgebnisse, "   nummer - name - verbindung:\r\n", "   number - name - connection:\r\n",
	"   numero - nome - connessione: \r\n", "   nummer - naam - verbinding: \r\n")
_STRTABENTRY(NamensucheListenende, "ende ++++\r\n\n", "end ++++\r\n\n", "fine ++++\r\n\n", "einde ++++\r\n\n")
_STRTABENTRY(NamensucheKeineGefunden, "kein passender eintrag\r\n", "no matching entry\r\n", "data non trovato", "geen passende data gevonden")
_STRTABENTRY(DiagInterneIP, "interne IP: ", "local IP: ", "IP locale: ", "interne IP: ")
_STRTABENTRY(Datum, "Datum", "Date", "Data", "Datum")
_STRTABENTRY(DiagnoseEinleitung, "\r\n///interne meldung ", "\r\n///internal message ", "\r\n///messaggio interna", "\r\n///intern bericht")
_STRTABENTRY(SelbstAnrufMehrfachVersagt, 
	"Selbst-Anruf mehrfach versagt, Router-Konfiguration pruefen.\r\nSelbst-Anruf wurde abgeschaltet.", 
	"Loopback call failed constantly. Check router configuration.\r\nLoopback-Test was switched off.",
	"Errore Auto-chiamato, controlla configurazione del router.\r\nAuto-chiamata e' stata dispabilitata.",
	"Fout bij Auto-bellen, kontroleer de konfiguratie van de router. \r\nAuto-bellen is uitgeschakeld.")
_STRTABENTRY(FalscheGeheimzahlBewirktAbschaltung, 
	"geheimzahl fuer anmeldung beim rufnummern-server falsch.\r\nbitte bei server-administrator melden.\r\ndynamische ip aktualisierung und remote-server anbindung wurden abgeschaltet.", 
	"PIN for sign in at subscriber directory server wrong.\r\npleace contact a server-administrator.\r\ndynamic ip update and remote connection server switched off now.",
	"PIN per l'accesso al server abbonati e' sbagliato.\r\ncontatto l'amministratore del server.\r\nl'aggiornamento automatico dell' ip e' server remoto e' disabilitato.", 
	"PIN voor het aanmelden bij de abonnee server is fout. r\nneem kontakt op met de sever administrator.\r\ndynamische ip aktualisering en remote server verbinding is uitgeschaakeld.")
_STRTABENTRY(NummerNichtBekannt, "gewaehlte Nummer nicht bekannt", "subscriber number not known", "Numero abbonato non conosciuto",
	"Abonnee nummer niet bekend")
_STRTABENTRY(InternesVerzeichnisVoll, "internes Rufnummern-Verzeichnis voll", "internal directory out of space",
	"elenco interno degli abbonato pieno", "Interne abonnee lijst is vol")
_STRTABENTRY(KennwortAbfrage, "Seite gesperrt! Bitte Kennwort eingeben", "Page locked! Enter password",
	"Pagina bloccata, Prego digitare Password", "Pagina geblokkeerd, Passwoord ingeven a.u.b.")
_STRTABENTRY(KennwortFreigeben, "Freigeben", "Unlock", "Accessibile", "Vrijgeschakeld")
_STRTABENTRY(KennwortFalsch, "Falsches Kennwort eingegeben!", "Wrong password entered!",
	"Password errata!" , "Passwoord is fout!.")
_STRTABENTRY(SeiteGesperrt, "Seite gesperrt von anderem Anwender!", "Page locked by other user",
	"Pagina bloccata da altra utente!", "Pagina geblokkeerd door andere gebruiker")
_STRTABENTRY(FalschesKonfigKennwortEingegeben, "unautorisierter Zugriff auf Konfigurationsseite", "unauthorised access to locked configuration page",
	"accesso non autorizzato alla configurazione", "niet geoorloofde toegang tot de konfiguratie")
_STRTABENTRY(Druckspiegel, "Druckspiegel", "printed text", "Specchio di stampa", "Gedrukte text")
_STRTABENTRY(TexteingabeStartetFernschreiber, "Texteingabe startet Fernschreiber", "Enter text to start printer",
	"Digitare il testo per avviare la telescrivente", "Text ingeven om de telex te starten")
_STRTABENTRY(AndereVerbindungBesteht, "Es besteht bereits eine andere Verbindung, bitte warten.", "Connection busy, please wait.",
	"Esiste un'alta connessione, aspettare prego", "Er bestaat reeds een andere verbinding, wacht even a.u.b.")
_STRTABENTRY(ModulDeaktiviert, "Schnittstelle ist deaktiviert. Bitte sp&auml;ter wieder versuchen.", "Interface is de-activated. Try again later.",
	"L'interfaccia e' disattivata, prova piu' tardi prego", "Het interface is gedeaktiveerd, probeer het later nog eens")
_STRTABENTRY(HtmlTextEingabe, "Eingabe: ", "Enter text: ", "Digita testo: ","Text ingeven: ")
_STRTABENTRY(HtmlTextEingabeAbsenden, " Absenden ", " Send ", " Trasmettere ", " Zenden ")
_STRTABENTRY(HtmlTextEingabeAktualisieren, "Aktualisieren", "Refresh", "Aggiornare", "Aktualiseren")
_STRTABENTRY(EigeneAmtsnummer, "Netz-Vorwahl f&uuml;r gehende Verbindungen", "Outside line number",
	"Prefisso per collegamenti esterni", "Nummer voor de buitenlijn")
_STRTABENTRY(FesteHauptstelle, "feste Hauptstelle f&uuml;r kommende Verbindungen", "Primary teleprinter for incoming calls",
	"Telescrivente preferito per chiamate in entrata", "Voorkeur telex voor komende verbindingen")
_STRTABENTRY(FesteHauptstelleNummer, 
	"interne Durchwahl der Hauptstelle f&uuml;r kommende Verbindungen", 
	"Internal call number of primary teleprinter for incoming calls",
	"numero interno del telescrivente primario per chiamate in arrivo.", 
	"Interne nummer van de primaire telex voor komende verbindingen")
_STRTABENTRY(AlternativSucheBeiBesetzt, "Weiterleitung bei besetzt", 
	"Forward calls to alternative teleprinter if called is busy",
	"Inoltro quando occupato", 
	"Doorverbinden wanneer bezet")
_STRTABENTRY(ASBB_Niemals, "nie", "never", "Mai", "Nooit")
_STRTABENTRY(ASBB_NurHauptstelle, "nur bei Hauptstelle", "only calls to primary printer","solo telescrivente primario","alleen bij de primaire telex")
_STRTABENTRY(ASBB_AuchDurchwahl, "auch bei Durchwahl", "also for direct calls to secondary printer",
	"anche per chiamate alle estensioni","ook bij verbindingen aan extensies")
_STRTABENTRY(DurchwahlenListe, "erlaubte Durchwahlen (mit Komma trennen)", "Secondary call numbers list (separate by comma)",
	"Estensioni disponibili (separare con virgola)", "Beschikbare extensies (met komma's scheiden)")
_STRTABENTRY(BaudrateListe, "Baudraten der Module (Beispiel: 70-79:75,*:50)", "Baudrates of modules (example: 70-79:75,*:50)",
	"Baudrates dei moduli (esempio: 70-79:75,*:50)", "Baudrates van modulen (voorbeeld: 70-79:75,*:50)")
_STRTABENTRY(NeuesTWIProtokoll, 
	"Neues internes Busprotokoll verwenden (ab Version 450)", 
	"Use new internal bus protocol (from version 450)",
	"Usare nuovo interno bus protocol (dal versione 450)",
	"Gebruik nieuw intern bus protocol (van af versie 450)")
_STRTABENTRY(Druckzeilenlaenge, "Automatischer Zeilenumbruch an Position", "Automatic line break at position",
	"Da capo automatico al posizione", "Automatische regeleinde aan positie")
_STRTABENTRY(UhrzeitVerteilen, "Uhrzeit an andere Module verteilen", "Broadcast time to other modules",
	"trasmettere l'orario ad altri moduli", "tijd aan andere modulen zenden")

_STRTABENTRY(AsciiEmpfModus, "Anrufe im ASCII-Modus annehmen", "accept ASCII mode calls",
	"Accetta chiamate in modo ASCII", "accepteer oproep in ASCII-modus")

_STRTABENTRY(AsciiModus_Immer, "immer", "always", "sempre", "altijd")
_STRTABENTRY(AsciiModus_NurBeiDurchwahl, "nur mit Durchwahl", "only with direct calls", 
	"solo con chiamate dirette", "alleen met direkte oproep")
_STRTABENTRY(AsciiModus_Nie, "nie", "never", "mai", "nooit")
	
_STRTABENTRY(ProtokollLevel, "Ausgabe-Modus an serieller Schnittstelle", "Output mode of serial port",
	"Profondita' di protocollo alla porta seriale", "Protocol diepte aan de seriele poort.") // TODO übersetzen
_STRTABENTRY(ProtokollLevelTlnServer, "Tiefe der Protokollierung f&uuml;r Teiln-Server", "Logging level for directory server",
	"Profondita' di protocollo del server abbonati", "Protocol diepte voor de abonnee server")
_STRTABENTRY(DiagnoseLevel, "Level f&uuml;r Druckausgabe von Meldungen", "Message filtering level",
	"livello filtraggio messaggi","filter niveau voor berichten")
_STRTABENTRY(KonfigPasswort, "Passwort f&uuml;r Konfigurationsseiten", "Password for configuration pages",
	"Password per configurazione", "Passwoord voor konfiguratie")
_STRTABENTRY(TlnVerzeichnisOffen, "Teilnehmer-Verzeichnis f&uuml;r alle sichtbar", "Local subscriber directory visible for everybody",
	"Elenco interno abbonati visibile a tutti", "Interne abonnee lijst zichbaar voor iedereen")
_STRTABENTRY(LangeDienstmeldungen, "Dienstmeldungen in Langform", "long service messages",
	"Messaggi di sistema lunghi","Systeemberichten in lange vorm")
_STRTABENTRY(DatumDruckModus, "Datum bei Anrufen automatisch drucken", "Print date on incoming calls",
	"Stampa data automatica alla chiamata", "Datum print automatisch bij komende verbinding" )
_STRTABENTRY(DatumDruckKein, "aus", "off", "spento", "uit")
_STRTABENTRY(DatumDruckLokal, "nur lokal", "only local", "solo locale","alleen lokaal")
_STRTABENTRY(DatumDruckAnrufer, "nur beim Anrufer", "only at caller", "solo dalla chiamante", "alleen bij de beller")
_STRTABENTRY(DatumDruckBeide, "lokal und Anrufer", "local and caller","localmente e chiamante","Lokaal en bij beller")
_STRTABENTRY(ImpressAenderZeile, "zu &auml;ndernde Zeile", "line to change", "Riga da cambiare", "Regel te veranderen")
_STRTABENTRY(ImpressAenderTest, "neuer Text", "new text", "Test nuovo", "Nieuwe text")
_STRTABENTRY(KonnteNichtGeaendertWerden, " konnte nicht ge&auml;ndert werden", " could not be changed",
	"Cambiamento non possibile", "Verandering niet mogelijk")
_STRTABENTRY(KennwortGgfGeaendert, "<br>Kennwort ggf. ge&auml;ndert.", "<br>Password changed if necessary",
	"Cambio password se necessario", "Passwoord veranderen als nodig")
_STRTABENTRY(InternesKennwortFehlt, 
	"zun&auml;chst Passwort in <a href=\"itelexcfg-intern.cgi?spr=de\" target=\"main\">Einstellungen im lokalen System</a> eingeben!", 
	"First select password in <a href=\"itelexcfg-intern.cgi?spr=en\" target=\"main\">Settings for the local system</a>!",
	"Selezione password in <a href=\"itelexcfg-intern.cgi?spr=it\" target=\"main\">Parametri sistema locale</a>!",
	"Kies passwoord eerst in <a href=\"itelexcfg-intern.cgi?spr=nl\" target=\"main\">Parameters in lokaal systeem</a>!")
_STRTABENTRY(GesperrtBestaetigung, 
	"Konfigurationsseiten sind nun gesperrt. Zur Freigabe wieder das Passwort eingeben oder Taste der Baugruppe 2 x dr&uuml;cken.", 
	"Configuration pages are locked again. For unlocking enter password or push button at ethernet interface module twice.",
	"Configuratione e' bloccata adesso. Per sbloccare digitare password o premi pulsante sulla scheda ethernet 2 volte.",
	"Konfiguratie is nu geblokkeerd. Opheffing door passwoord ingave of de drukknop op de ethernetkaart 2x drukken")
	
_STRTABENTRY(ITelexRufnummer, "eigene Rufnummer im i-telex-Netz", "Subscriber's number", "Proprio numero nella rete i-telex", "eigen nummer in het i-telex net")

_STRTABENTRY(ITelexRufnummerZuKurz, "<big><b>&lt;=== muss fünf bis neun Ziffern haben!</b></big>", "<big><b>&lt;=== must have five to nine digits!</b></big>",
	"<big><b>&lt;=== deve avere almeno 5 fino a 9 cifre!</b></big>", "<big><b>&lt;=== moet tenminste 5 to 9 cijfers zijn!</b></big>")

_STRTABENTRY(RufnrServerAnmeldGeheimzahl, "Geheimzahl zur Anmeldung beim Rufnummern-Server", "PIN for sign in at subscriber directory server",
	"PIN per accedere al server abbonati", "PIN voor aanmelding bij de abonnee server")

_STRTABENTRY(DynIPAktiv, "Dynamische IP-Aktualisierung aktiv", "Dynamic update of IP address active",
	"Aggiornamento IP dinamico e' attivo","Dynamische IP aktualisering is aktief")
_STRTABENTRY(VerbindungstestPeriode, "Verbindungstest-Periode (0 f&uuml;r kein Test)", "Loopback test period (0 to switch off)",
	"Periodo di test del collegamento (0 = spento)", "Verbindingstest-Periode (0 = uit)")
_STRTABENTRY(OeffentlichePortNr, "&ouml;ffentliche Port-Nummer", "Public internet port number","Porta internet pubblico", "Publieke internet poort")
_STRTABENTRY(RemoteServerNutzen, 
	"Standverbindung zum Server wegen nicht-&ouml;ffentlicher IP verwenden", 
	"Use permanent server connection due to non-public IP address",
	"Usa connessione permanente se non IP fisso",
	"Gebruik permanente server verbinding als er geen vast IP adres is.")
_STRTABENTRY(RemoteServerAusschlussDynIP, 
	"Verbindungsbr&uuml;ckenserver und Dynamische IP nicht gemeinsam verwendbar.", 
	"Remote connection server and dynamic IP are exclusive",
	"IP dinamico e connessione fisso non possono coesistere",
	"Dynamische IP en vaste server verbinding kunnen niet gelijktijdig bestaan")
_STRTABENTRY(RufnrServerAdr, 
	"Adresse des Teilnehmer-Servers", 
	"Subscriber directory server address", 
	"Indirizzo del server-abbonati",
	"Adres van de Abonnee-server")
_STRTABENTRY(TlnServSyncGeheimzahl, 
	"Geheimzahl f&uuml;r Server-Synchronisierung", 
	"PIN for directory server synchronisation",
	"PIN per syncronizzazione server",
	"PIN voor de server synchronisering")
_STRTABENTRY(TwiTlnListeAnfang, "Status der angeschlossenen Module:<p>", "Status of all connected hardware modules:<p>",
	"Stato di tutti moduli collegati: <p>", "Status van alle voorhandene modulen <P>")
_STRTABENTRY(TwiTlnListeEintrag, "Nummer %s Status %02X<br>", "Number %s status %02X<br>", "Numero %s stato %02X<br>", "Nummer %s status %02X<br>")
_STRTABENTRY(TwiTlnListeEnde, "+++fertig","+++end","+++fine","+++einde")

// für CgiFormTools.c und sonst häufig verwendet:
_STRTABENTRY(NeueEinstellungen, "Die neuen Einstellungen sind: ", "The new settings are: ","Parametri nuovi sono: ","De nieuwe parameters zijn: ")
_STRTABENTRY(Weiter, "weiter", "continue","continuare","volgende")
_STRTABENTRY(Unveraendert, " unver&auml;ndert", " unchanged"," invariato"," geen verandering")
_STRTABENTRY(GeaendertIn, " ge&auml;ndert in", " changed to", " cambiato in","veranderd in")
_STRTABENTRY(EinstellungenUebernehmen, "Einstellung &Uuml;bernehmen", "Submit changes","Conferma cambio","Bevestig verandering")
_STRTABENTRY(FehlerMitPos, 
	"fehlerhafte Angabe (siehe hinter &gt;&gt;&gt; ): ",
	"invalid entry (see after &gt;&gt;&gt; ): ",
	"Errore dati (vedi dopo &gt;&gt;&gt; ): ",
	" &gt;&gt;&gt; ): ")


// für TlnBuch.c:
_STRTABENTRY(UeberschriftTeilnehmerverzeichnis, 
	"<h3>Teilnehmerverzeichnis</h3><br>", 
	"<h3>Subscriber directory</h3><br>", 
	"<h3>Elenco abbonati</h3><br>", 
	"<h3>Abonnee lijst</h3><br>")
_STRTABENTRY(UeberschriftOeffentlichesTeilnehmerverzeichnis, 
				"<h3>&Ouml;ffentliches Teilnehmerverzeichnis</h3><br>", 
				"<h3>Public subscriber directory</h3><br>",
				"<h3>Public elenco abbonati</h3><br>",
				"<h3>Abonnee lijst</h3><br>")
_STRTABENTRY(VollstaendigesTeilnehmerverzeichnis, "zeige vollst&auml;ndiges Verzeichnis", "show complete directory with private entries",
	"visualizza elenco privato intero", "visualiseer prive' abonnee lijst geheel" )
_STRTABENTRY(TeilnehmerverzeichnisHtmlKopf,
	"<table border=\"1\" cellpadding=\"2\" cellspacing=\"0\">"
	"<tr>"
	"<th align=\"left\">Rufnummer<br>sortiere <a href=\"itelex-tlnverz.cgi?sort=nummer\">1-9</a> <a href=\"itelex-tlnverz.cgi?sort=nummer&ab\">9-1</a></th>" // Nummer
	"<th align=\"left\">Name<br>sortiere <a href=\"itelex-tlnverz.cgi?sort=name\">A-Z</a> <a href=\"itelex-tlnverz.cgi?sort=name&ab\">Z-A</a></th>" // Name
	"<th align=\"center\">Besond.</th>" // Flags
	"<th align=\"left\">Typ</th>" // Typ
	"<th align=\"left\">Adresse</th>" // Adresse
	"<th align=\"center\">Port</th>" // Port
	"<th align=\"center\">Durchwahl</th>" // Durchwahl
#ifdef ITELEX_TLNSERVER
	"<th align=\"center\">letzte<br>Aktualisierung<br>sortiere <a href=\"itelex-tlnverz.cgi?sort=datum\">auf</a> <a href=\"itelex-tlnverz.cgi?sort=datum&ab\">ab</a></th>" // Datum / Uhrzeit
#else
	"<th align=\"center\">letzte<br>Verwendung<br>sortiere <a href=\"itelex-tlnverz.cgi?sort=datum\">auf</a> <a href=\"itelex-tlnverz.cgi?sort=datum&ab\">ab</a></th>" // Datum / Uhrzeit
#endif
	"<th align=\"left\">Aktion</th>" // in dieser Spalte sind die Buttons
	"</tr>",
	
	"<table border=\"1\" cellpadding=\"2\" cellspacing=\"0\">"
	"<tr>"
	"<th align=\"left\">Number<br>sort <a href=\"itelex-tlnverz.cgi?sort=nummer\">1-9</a> <a href=\"itelex-tlnverz.cgi?sort=nummer&ab\">9-1</a></th>" // Nummer
	"<th align=\"left\">Name<br>sort <a href=\"itelex-tlnverz.cgi?sort=name\">A-Z</a> <a href=\"itelex-tlnverz.cgi?sort=name&ab\">Z-A</a></th>" // Name
	"<th align=\"center\">Specials</th>" // Flags
	"<th align=\"left\">Type</th>" // Typ
	"<th align=\"left\">Address</th>" // Adresse
	"<th align=\"center\">Port</th>" // Port
	"<th align=\"center\">Direct dial</th>" // Durchwahl
#ifdef ITELEX_TLNSERVER
	"<th align=\"center\">Updated<br>sort <a href=\"itelex-tlnverz.cgi?sort=datum\">asc.</a> <a href=\"itelex-tlnverz.cgi?sort=datum&ab\">desc.</a></th>" // Datum / Uhrzeit
#else
	"<th align=\"center\">Changed<br>sort <a href=\"itelex-tlnverz.cgi?sort=datum\">asc.</a> <a href=\"itelex-tlnverz.cgi?sort=datum&ab\">desc.</a></th>" // Datum / Uhrzeit
#endif
	"<th align=\"left\">Action</th>" // in dieser Spalte sind die Buttons
	"</tr>",
	
	"<table border=\"1\" cellpadding=\"2\" cellspacing=\"0\">"
	"<tr>"
	"<th align=\"left\">Numero<br>sort <a href=\"itelex-tlnverz.cgi?sort=nummer\">1-9</a> <a href=\"itelex-tlnverz.cgi?sort=nummer&ab\">9-1</a></th>" // Nummer
	"<th align=\"left\">Nome<br>sort <a href=\"itelex-tlnverz.cgi?sort=name\">A-Z</a> <a href=\"itelex-tlnverz.cgi?sort=name&ab\">Z-A</a></th>" // Name
	"<th align=\"center\">Speciale</th>" // Flags
	"<th align=\"left\">Tipo</th>" // Typ
	"<th align=\"left\">Indirizzo</th>" // Adresse
	"<th align=\"center\">Porta</th>" // Port
	"<th align=\"center\">Estensione</th>" // Durchwahl
#ifdef ITELEX_TLNSERVER
	"<th align=\"center\">Aggiornato<br>sort <a href=\"itelex-tlnverz.cgi?sort=datum\">su</a> <a href=\"itelex-tlnverz.cgi?sort=datum&ab\">giu</a></th>" // Datum / Uhrzeit
#else
	"<th align=\"center\">Utilizzo<br>sort <a href=\"itelex-tlnverz.cgi?sort=datum\">su</a> <a href=\"itelex-tlnverz.cgi?sort=datum&ab\">giu</a></th>" // Datum / Uhrzeit
#endif
	"<th align=\"left\">Azione</th>" // in dieser Spalte sind die Buttons
	"</tr>",
	
	"<table border=\"1\" cellpadding=\"2\" cellspacing=\"0\">"
	"<tr>"
	"<th align=\"left\">Nummer<br>sort <a href=\"itelex-tlnverz.cgi?sort=nummer\">1-9</a> <a href=\"itelex-tlnverz.cgi?sort=nummer&ab\">9-1</a></th>" // Nummer
	"<th align=\"left\">Naam<br>sort <a href=\"itelex-tlnverz.cgi?sort=name\">A-Z</a> <a href=\"itelex-tlnverz.cgi?sort=name&ab\">Z-A</a></th>" // Name
	"<th align=\"center\">Speciaal</th>" // Flags
	"<th align=\"left\">Type</th>" // Typ
	"<th align=\"left\">Adres</th>" // Adresse
	"<th align=\"center\">Poort</th>" // Port
	"<th align=\"center\">Extensie</th>" // Durchwahl
#ifdef ITELEX_TLNSERVER
	"<th align=\"center\">Aktueel<br>sort <a href=\"itelex-tlnverz.cgi?sort=datum\">op</a> <a href=\"itelex-tlnverz.cgi?sort=datum&ab\">neer</a></th>" // Datum / Uhrzeit
#else
	"<th align=\"center\">Gebruik<br>sort <a href=\"itelex-tlnverz.cgi?sort=datum\">op</a> <a href=\"itelex-tlnverz.cgi?sort=datum&ab\">neer</a></th>" // Datum / Uhrzeit
#endif
	"<th align=\"left\">Actie</th>" // in dieser Spalte sind die Buttons
	"</tr>")


	
_STRTABENTRY(TeilnehmerverzeichnisAktionenOffen,
	"<a href=\"itelex-tlnverz.cgi?save\">nichtfl&uuml;chtig speichern</a><br>" 
	"<a href=\"itelex-tlnverz.cgi?load\">alle &Auml;nderungen verwerfen</a><br>"
	"<a href=\"itelex-tlnverz.cgi?clear\">komplett l&ouml;schen</a><br>", 
	"<a href=\"itelex-tlnverz.cgi?save\">save non-volatile</a><br>" 
	"<a href=\"itelex-tlnverz.cgi?load\">discard all changes</a><br>"
	"<a href=\"itelex-tlnverz.cgi?clear\">clear all</a><br>",
	"<a href=\"itelex-tlnverz.cgi?save\">salva non-volatile</a><br>" 
	"<a href=\"itelex-tlnverz.cgi?load\">annulla cambiamenti</a><br>"
	"<a href=\"itelex-tlnverz.cgi?clear\">elimina tutti</a><br>",
	"<a href=\"itelex-tlnverz.cgi?save\">niet vluchtig opslaan</a><br>" 
	"<a href=\"itelex-tlnverz.cgi?load\">annuleer alle veranderingen</a><br>"
	"<a href=\"itelex-tlnverz.cgi?clear\">wis alles</a><br>")

_STRTABENTRY(TeilnehmerverzeichnisAktionenLeerOffen, 
	"Noch keine Eintr&auml;ge vorhanden<p>"
	"<a href=\"itelex-tlnverz.cgi?load\">gespeicherte Daten wiederherstellen</a><br>"
	"<a href=\"itelex-tlnverz.cgi?edit=0\">Hinzuf&uuml;gen</a></form>",
	"No entries yet<p>"
	"<a href=\"itelex-tlnverz.cgi?load\">restore last saved entries</a><br>"
	"<a href=\"itelex-tlnverz.cgi?edit=0\">add entry</a></form>",
	"Lista vuota<p>"
	"<a href=\"itelex-tlnverz.cgi?load\">ripristina dati salvati ultimi</a><br>"
	"<a href=\"itelex-tlnverz.cgi?edit=0\">aggiungi voce</a></form>",
	"Lijst is leeg<p>"
	"<a href=\"itelex-tlnverz.cgi?load\">herstel laatste data</a><br>"
	"<a href=\"itelex-tlnverz.cgi?edit=0\">Nieuwe data</a></form>")

	
_STRTABENTRY(Rufnummer, "Rufnummer", "Number", "Numero", "Nummer")
_STRTABENTRY(Name, "Name", "Name","Nome","Naam")
_STRTABENTRY(Adresse, "Adresse", "Address","Indirizzo","Adres")
_STRTABENTRY(Port, "Port", "Port", "Porta", "Poort")
_STRTABENTRY(Durchwahl, "Durchwahl", "Direct dial", "Estensione", "Extensie")
_STRTABENTRY(TlnverzAttrLokal, "Lokal", "local", "Locale", "Lokaal")
_STRTABENTRY(TlnverzAttrGesperrt, "gesperrt", "locked", "Bloccata", "Geblokkeerd")
_STRTABENTRY(Typ, "Typ", "Type", "Tipo", "Type")
_STRTABENTRY(TlnverzAttrDyn, "DynIP", "DynIP", "DynIP", "DynIP")
_STRTABENTRY(TypGeloescht, "geloescht", "deleted","cancellato","gewist")
_STRTABENTRY(TypAscii, "Ascii", "ASCII","ASCII","ASCII")
_STRTABENTRY(TypITelex, "i-Telex", "i-Telex", "i-Telex", "i-Telex")
_STRTABENTRY(TypEMail, "eMail", "eMail", "eMail", "eMail")
_STRTABENTRY(AktionAendern, "&Auml;ndern", "edit", "editare", "edit")
_STRTABENTRY(AktionHinzufuegen, "Hinzuf&uuml;gen", "add", "aggiungi", "toevoegen")
_STRTABENTRY(Rufnummer0NichtErlaubt, "<b>Rufnummer 0 nicht erlaubt!</b><br>", "<b>Number 0 not allowed!</b><br>",
	"<b>Numero 0 non permesso!</b><br>", "<b>Nummer 0 mag niet!</b><br>")
_STRTABENTRY(MeldungTlneintragRufnummer, "Teilnehmereintrag:<br>Rufnummer: %ld ", "Directory entry:<br>Number: %ld ",
	"Voce elenco: <br>Number: %ld ", "Data lijst<br>Number: %ld ")
_STRTABENTRY(EhemalsLong, "ehem. %ld", "former %ld" , "precedente %ld", "voormalig %ld")
_STRTABENTRY(HostnameZusatz, ": Hostname %s ", ": Hostname %s ", ": Nome host %s ",": Host naam %s ")
_STRTABENTRY(IPZusatz, ": IP %s ", ": IP %s ", ": IP %s ", ": IP %s ")
_STRTABENTRY(TypUnbekannt, "<b>Unbekannter Typ!</b><br>", "<b>Type unknown!</b><br>", "<b>Tipo sconosciuto</b><br>","Onbekend Type</b><br>")
_STRTABENTRY(KeineAenderung, "<b>keine &Auml;nderung</b><br>", "<b>unchanged</b><br>","<b>non cambiato</b><br>","<b>geen verandering</b><br>")
_STRTABENTRY(LokaleSpracheGespeichert, "<br>Sprache ge&auml;ndert nach Deutsch", "<br>Language changed to English",
	"<br>Lingua cambiato in Italiano","<br>Taal veranderd in Nederlands")
_STRTABENTRY(RufnummerDoppelt, 
			"<b>Rufnummer ist bereits vergeben, &Auml;nderung nicht gespeichert</b><br>", 
			"<b>Number already used, change not saved</b><br>",
			"<b>Numero gia' in uso, cambiamento non salvato</b><br>",
			"<b>Nummer reeds in gebruik, verandering niet doorgevoerd</b><br>")

_STRTABENTRY(EintragGespeichert, "Eintrag gespeichert<br>", "Entry saved<br>", "Voce salvata<br>","Data weggeschreven<br>")

_STRTABENTRY(EintragUnveraendert, "Eintrag unver&auml;ndert<br>", "Entry unchanged<br>", "Voce non cambiata<br>","Data niet veranderd<br>")

_STRTABENTRY(AlteNummerNichtGeloescht, 
			"<b>Alte Nummer %ld konnte nicht gel&ouml;scht werden!</b><br>", 
			"<b>Former number %ld could not be deleted!</b><br>",
			"<b>Numero precedente %ld non cancellato!</b><br>",
			"<b>Oude nummer %ld is niet gewist!</b><br>")
			
_STRTABENTRY(EepromSpeicherFehler, "<b>Fehler beim Speichern (Codes %d / %02X)</b>", "<b>Error at saving (codes %d / %02X)</b>",
			"<b>Errore al salvataggio (codes %d / %02X)</b>",
			"<b>Fout bij wegschrijven (codes %d / %02X)</b>")

_STRTABENTRY(EepromSpeicherErfolg, "Erfolgreich gespeichert (%d Bytes)", "saved successfully (%d byte)",
			"salvato con successo (%d byte)",
			"wegschrijven gelukt (%d byte)")

_STRTABENTRY(EepromLadenFehler, "<b>Fehler beim Laden (Codes %d / %02X)</b>", "<b>Error at loading (codes %d / %02X)</b>",
			"<b>Errore caricando (codes %d / %02X)</b>",
			"<b>Fout bij laden (codes %d / %02X)</b>")

_STRTABENTRY(EepromLadenErfolg, "Erfolgreich geladen (%d Bytes)", "loaded successfully (%d byte)",
			"Caricamento completato (%d byte)",
			"Laden is gelukt (%d byte)")

_STRTABENTRY(KomplettGeloescht, "komplett gel&ouml;scht", "completely cleared","completamente cancellato", "alles gewist")

_STRTABENTRY(UngueltigerCgiAufruf, "Fehler: ungueltiger CGI-Aufruf: %s", "Error: invalid cgi-call: %s",
			"Errore: invalido cgi-call: %s", "Fout: invalide cgi-call: %s")

_STRTABENTRY(ZurueckZumTeilnehmerverzeichnis, 
			"<br>Zur&uuml;ck zum <a href=\"itelex-tlnverz.cgi?spr=de\">Teilnehmer-Verzeichnis</a>", 
			"<br>back to <a href=\"itelex-tlnverz.cgi?spr=en\">subscriber directory</a>",
			"<br>Indietro a <a href=\"itelex-tlnverz.cgi?spr=it\">elenco abbonati</a>",
			"<br>Terug naar <a href=\"itelex-tlnverz.cgi?spr=nl\">abonnee lijst</a>")

_STRTABENTRY(ZusatzEepromFehler, "Fehler im Zusatz-EEPROM", "Error in directory EEPROM", "Errore EEPROM aggiuntivo","Fout in de toegevoegde EEPROM")

_STRTABENTRY(TeilnehmerlisteVoll, "Rufnummern-Verzeichnis voll, nicht gespeichert", "directory out of space, not saved",
		"Spazio elenco esaurito, non salvato","Abonnee lijst vol, niets is weggeschreven")

#ifdef ITELEX_TLNSERVER

// für TlnServer.c:
_STRTABENTRY(NeuTeilnehmer, "Neuer Teilnehmer angemeldet", "new subscriber signed up","Nuovo abbonato sottoscritto","Nieuwe abonnee ingeschreven")
_STRTABENTRY(ServerAnmeldungFalscheGeheimzahl, "Teilnehmer-Server Anmeldung mit falscher Geheimzahl", "Unauthorised access to directory server port",
		"Accesso al server con PIN errato","Aanmelding bij server met verkeerde PIN")

#endif //def ITELEX_TLNSERVER


#ifdef ITELEX_EMAIL

// für eMail.c:
// ungeprüft:
_STRTABENTRY(EmailEmpfangStartzeile, "\r\n///email empfangen:\r\n", "\r\n///email received:\r\n","\r\n///email ricevuto:\r\n","\r\n///email ontvangen:\r\n")
_STRTABENTRY(EmailEingabeEmpfaenger, "\r\nemail an:      ", "\r\nemail to:      ", "\r\nemail a:      ", "\r\nemail aan:      ")
_STRTABENTRY(EmailEingabeBetreff, "\r\nbetreff:      ", "\r\nsubject:      ","\r\noggetto:      ", "\r\nonderwerp:      ")
_STRTABENTRY(EmailEingabeText, "\r\ntext:   \r\n", "\r\nbody text:   \r\n","\r\ntesto:   \r\n", "\r\ntext:   \r\n")
_STRTABENTRY(EmailKonfigPopServer, "POP-Server Adresse", "POP server address","indirizzo POP-Server", "POP server adres")
_STRTABENTRY(EmailKonfigSmtpServer, "SMTP-Server Adresse", "SMTP server address","Indirizzo SMTP-Server", "SMTP server adres")
_STRTABENTRY(EmailKonfigEigeneAdresse, "Eigene eMail-Adresse", "Own email address","Proprio indirizzo eMail", "Eigen email addes")
_STRTABENTRY(EmailKonfigKennwort, "Kennwort f&uuml;r eMail-Server", "Password for email servers","Password per il mail-server","Passwoord vor de mail-server")
_STRTABENTRY(EmailKonfigAbfragetakt, "Takt des eMail-Abrufs (in Minuten; 0 = ausgeschaltet)", "Polling period for email query (in minutes; 0 = off)",
		"Polling mail-server (in Minuti; 0 = spento)", "Polling periode mail-server (in minuten; 0 = uitgeschakeld")

_STRTABENTRY(EmailKonfigFilterNurTX, "Nur eMails mit +TX+ im Subject drucken", "Print only emails with +tx+ in subject",
		"Stampa solo mail con +tx+ nell'oggetto","Alleen mail met +tx+ in het onderwerp")

_STRTABENTRY(EmailKonfigDruckZiel, "Spezieller Drucker f&uuml;r eMail", "special printer for emails","Stampante speciale per mail","Speciale printer voor mail")
_STRTABENTRY(EmailKonfigFehler, 
			"<br><b>Konfigurationsdaten unvollst&auml;ndig, Abfragetakt auf Null gesetzt, eMail Abfrage ist ausgeschaltet.</b>", 
			"<br><b>Configuration not complete, polling period set to zero, incoming eMail will not be printed.</b>",
			"<br><b>Configurazione non completata, polling e' impostato a zero, eMail non saranno stampate.</b>",
			"<br><b>Configuratie niet compleet, polling is op nul gezet, eMails worden niet uitgeprint.</b>")

_STRTABENTRY(SMTPFehlerAnfang, "eMail-Ausgang-Server ", "Server for outgoing email","Server per uscita mail", "Server voor uitgaande mail")
_STRTABENTRY(POPFehlerAnfang, "Server fuer eMail-Empfang ", "server for incoming email ", "Server per mail in entrata","Server voor ingang mail")
_STRTABENTRY(MailFehlerIPNichtErmittelbar, " IP nicht ermittelbar", " no IP address was found", "Indirizzo IP non trovato", "IP adres niet gevonden")
_STRTABENTRY(MailFehlerNotConnected, " konnte nicht verbunden werden", " connection failed", "connessione impossibile","verbinding niet mogelijk")
_STRTABENTRY(SMTPFehlerDirekt, "Meldung vom eMail-Ausgang-Server: ", "Message from the Server for outgoing email: ",
		"Messaggio server-uscita-eMail: ", "Bericht von de mail-uitgangs-server: ")

#endif //def ITELEX_EMAIL


// für ConfigNtp.c:
_STRTABENTRY(NtpOn, "Uhrzeit vom Server abfragen", "Use time server", "Utilizzare server del tempo", "Gebruik tijd server")
_STRTABENTRY(NtpServerHostname, "Hostname des Zeitservers", "Time server host name", "Nome host server del tempo","Tijd host server naam")
_STRTABENTRY(Zeitzone, "Zeitzone", "Timezone","Zona orario","Tijdzone")
_STRTABENTRY(AutoSommerzeit, "Sommerzeit automatisch umstellen", "Use daylight saving time (european standard)",
		"Usare ora solare automatico", "Zomertijd automatisch instellen")
			 
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
	Italienisch,
	Niederlaendisch,
	} TSprache;
	
extern PGM_P GetIStr(uint16_t stri, TSprache Sprache);
	// Funktion für die Ermittlung eines Strings aus dem Index

#define ISTR(name, Sprache) GetIStr(stridx_ ## name, Sprache)
	// Vereinfachendes Hilfsmakro

#endif // def STRINGTAB_H_PUR


#endif // !defined(__STRINGTAB_H__) || defined(_STRTABENTRY)
