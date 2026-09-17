//! Übersetzungstabellen für nicht direkt im Zeichenvorrat der Fernschreiber 
//! vorkommende Zeichen
//--------------------------------------------------------------------------
// The escapes in UebersetzUr are the ISO-8859-1 bytes of äöüÄÖÜß.
// Writing them as escapes keeps every entry one byte wide, so the three
// tables stay aligned by index now that this file is stored as UTF-8.

const PROGMEM char UebersetzUr[] = "\xE4\xF6\xFC\xC4\xD6\xDC\xDF[]<>{}@_#\""; //!< Übersetzungstabelle für Umlaute: zu übersetzendes Zeichen.
const PROGMEM char UebersetzN1[] = "aouAOUs(:(.(-( +\'"; //!< Übersetzungstabelle für Umlaute: erstes Ersatzzeichen.
const PROGMEM char UebersetzN2[] = "eeeeees:).)-)) +\'"; //!< Übersetzungstabelle für Umlaute: zweites Ersatzzeichen.
