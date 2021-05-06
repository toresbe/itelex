#include <inttypes.h>
#include <avr\pgmspace.h>
#include "string.h"


// Zu folgenden Konstrukten siehe auch http://www.nongnu.org/avr-libc/user-manual/FAQ.html#faq_rom_array 

// nun werden die Grunddefinitionen angelegt 

#include "StringTab.h" 


// nun werden die Strings selbst in den Programmspeicher definiert
// ---------------------------------------------------------------
	// mit dem folgenden Makro _STRTABENTRY ...
#define _STRTABENTRY(name, text_de, text_en, text_it, text_nl) \
	const PROGMEM char strde_ ## name [] = text_de ; \
	const PROGMEM char stren_ ## name [] = text_en ; \
	const PROGMEM char strit_ ## name [] = text_it ; \
	const PROGMEM char strnl_ ## name [] = text_nl ;
	
	// wird aus 
	// _STRTABENTRY(Rufnummer, "Rufnummer", "Number", "Numero", "Num")
	// _STRTABENTRY(Name, "Name", "Name")
	// dann
	// const PROGMEM char strde_Rufnummer [] = "Rufnummer" ; const PROGMEM char stren_Rufnummer [] = "Number" ; ... strit_Rufnummer [] ... strnl_Rufnummer
	// const PROGMEM char strde_Name [] = "Name" ; const PROGMEM char stren_Name [] = "Name" ; 

#include "StringTab.h"
	// hier stehen die "echten" _STRTABENTRY(xxx, "yyy", ...) drin.

#undef _STRTABENTRY


// hier wird die Tabelle aller Zeiger auf die Strings angelegt
// ---------------------------------------------------------------
	// mit dem folgenden Makro _STRTABENTRY ...
#define _STRTABENTRY(name, text_de, text_en, text_it, text_nl) strde_ ## name , stren_ ## name , strit_ ## name , strnl_ ## name ,
	// wird aus 
	// _STRTABENTRY(Rufnummer, "Rufnummer", "Number", "Numero", "..")
	// _STRTABENTRY(Name, "Name", "Name")
	// dann nur
	// strde_Rufnummer, stren_Rufnummer, strit_Rufnummer, strnl_Rufnummer,
	// strde_Name, stren_Name, strit_Name, strnl_Name, 

PROGMEM PGM_P const IStrList[] = { 

#include "StringTab.h"
	// hier stehen die "echten" _STRTABENTRY(xxx, "yyy", ...) drin.
	
	// da bereits Enum-Konstanten der Form stridx_xxx bereits in der gleichen Reihenfolge 
	// definiert sind, ist nun sichergestellt, dass IStrList[4*stridx_xxx] nun ein Zeiger auf "yyy" enthält
	// und IStrList[4*stridx_xxx+1] nun ein Zeiger auf "zzz" enthält

} ; // Ende von IStrList[]


#undef _STRTABENTRY


// Definition der Funktion GetIStr(uint16_t stri)

extern PGM_P GetIStr(uint16_t stri, TSprache Sprache)
	{
	return (PGM_P) pgm_read_word(&IStrList[4 * stri + Sprache]); // 4 = Anzahl der Sprachen
	}



