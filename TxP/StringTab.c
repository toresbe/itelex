#include <inttypes.h>
#include <avr\pgmspace.h>
#include "string.h"


// Zu folgenden Konstrukten siehe auch http://www.nongnu.org/avr-libc/user-manual/FAQ.html#faq_rom_array 

// nun werden die Grunddefinitionen angelegt 

#include "StringTab.h" 


// nun werden die Strings selbst in den Programmspeicher definiert
// ---------------------------------------------------------------
	// mit dem folgenden Makro _STRTABENTRY ...
#define _STRTABENTRY(name, text_de, text_en) const PROGMEM char str_ ## name [] = text_de ;
	// wird aus 
	// _STRTABENTRY(Rufnummer, "Rufnummer", "Number")
	// _STRTABENTRY(Name, "Name", "Name")
	// dann
	// const PROGMEM char str_Rufnummer [] = "Rufnummer" ;
	// const PROGMEM char str_Name [] = "Name" ;

#include "StringTab.h"
	// hier stehen die "echten" _STRTABENTRY(xxx, "yyy") drin.

#undef _STRTABENTRY


// hier wird die Tabelle aller Zeiger auf die Strings angelegt
// ---------------------------------------------------------------
	// mit dem folgenden Makro _STRTABENTRY ...
#define _STRTABENTRY(name, text_de, text_en) str_ ## name ,
	// wird aus 
	// _STRTABENTRY(Rufnummer, "Rufnummer", "Number")
	// _STRTABENTRY(Name, "Name", "Name")
	// dann nur
	// str_Rufnummer,
	// str_Name,

const PROGMEM PGM_P IStrList[] = { 

#include "StringTab.h"
	// hier stehen die "echten" _STRTABENTRY(xxx, "yyy") drin.
	
	// da bereits Enum-Konstanten der Form stridx_xxx bereits in der gleichen Reihenfolge 
	// definiert sind, ist nun sichergestellt, dass IStrList[stridx_xxx) nun ein Zeiger auf "yyy" enthält.

} ; // Ende von IStrList[]


#undef _STRTABENTRY


// Definition der Funktion GetIStr(uint16_t stri)

extern PGM_P GetIStr(uint16_t stri)
	{
	return (PGM_P) pgm_read_word(&IStrList[stri]);
	}



