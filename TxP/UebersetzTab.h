//! Übersetzungstabellen für nicht direkt im Zeichenvorrat der Fernschreiber 
//! vorkommende Zeichen
//--------------------------------------------------------------------------

const PROGMEM char UebersetzUr[] = "äöüÄÖÜß[]<>{}@_#\""; //!< Übersetzungstabelle für Umlaute: zu übersetzendes Zeichen.
const PROGMEM char UebersetzN1[] = "aouAOUs(:(.(-( +\'"; //!< Übersetzungstabelle für Umlaute: erstes Ersatzzeichen.
const PROGMEM char UebersetzN2[] = "eeeeees:).)-)) +\'"; //!< Übersetzungstabelle für Umlaute: zweites Ersatzzeichen.
