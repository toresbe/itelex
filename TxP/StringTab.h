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
/*
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
_STRTABENTRY(, )
*/

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
