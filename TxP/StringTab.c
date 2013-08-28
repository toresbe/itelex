#include <inttypes.h>
#include <avr\pgmspace.h>
#include "string.h"


// Zu folgenden Konstrukten siehe auch http://www.nongnu.org/avr-libc/user-manual/FAQ.html#faq_rom_array 

// nun werden die Grunddefinitionen angelegt 

#include "StringTab.h" 


// nun werden die Strings selbst in den Programmspeicher definiert
// ---------------------------------------------------------------
	// mit dem folgenden Makro _STRTABENTRY ...
#define _STRTABENTRY(name, text_de, text_en) const PROGMEM char strde_ ## name [] = text_de ; const PROGMEM char stren_ ## name [] = text_en ;
	// wird aus 
	// _STRTABENTRY(Rufnummer, "Rufnummer", "Number")
	// _STRTABENTRY(Name, "Name", "Name")
	// dann
	// const PROGMEM char strde_Rufnummer [] = "Rufnummer" ; const PROGMEM char stren_Rufnummer [] = "Number" ;
	// const PROGMEM char strde_Name [] = "Name" ; const PROGMEM char stren_Name [] = "Name" ; 

#include "StringTab.h"
	// hier stehen die "echten" _STRTABENTRY(xxx, "yyy") drin.

#undef _STRTABENTRY


// hier wird die Tabelle aller Zeiger auf die Strings angelegt
// ---------------------------------------------------------------
	// mit dem folgenden Makro _STRTABENTRY ...
#define _STRTABENTRY(name, text_de, text_en) strde_ ## name , stren_ ## name ,
	// wird aus 
	// _STRTABENTRY(Rufnummer, "Rufnummer", "Number")
	// _STRTABENTRY(Name, "Name", "Name")
	// dann nur
	// strde_Rufnummer, stren_Rufnummer,
	// strde_Name, stren_Name,

const PROGMEM PGM_P IStrList[] = { 

#include "StringTab.h"
	// hier stehen die "echten" _STRTABENTRY(xxx, "yyy", "zzz") drin.
	
	// da bereits Enum-Konstanten der Form stridx_xxx bereits in der gleichen Reihenfolge 
	// definiert sind, ist nun sichergestellt, dass IStrList[2*stridx_xxx] nun ein Zeiger auf "yyy" enthält
	// und IStrList[2*stridx_xxx+1] nun ein Zeiger auf "zzz" enthält

} ; // Ende von IStrList[]


#undef _STRTABENTRY


// Definition der Funktion GetIStr(uint16_t stri)

extern PGM_P GetIStr(uint16_t stri, TSprache Sprache)
	{
	return (PGM_P) pgm_read_word(&IStrList[2 * stri + Sprache]); // 2 = Anzahl der Sprachen
	}



