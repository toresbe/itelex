#include <inttypes.h>
#include <avr\pgmspace.h>
#include "string.h"


// Zu folgenden Konstrukten siehe auch http://www.nongnu.org/avr-libc/user-manual/FAQ.html#faq_rom_array 

// nun werden die Grunddefinitionen angelegt 

#include "StringTab.h" 


// nun werden die Strings selbst in den Programmspeicher definiert

#define _STRTABENTRY(name, text) PROGMEM const char str_ ## name [] = text ;

#include "StringTab.h"

#undef _STRTABENTRY


// hier wird die Tabelle aller Zeiger auf die Strings angelegt

#define _STRTABENTRY(name, text) str_ ## name ,

PROGMEM const PGM_P IStrList[] = { 

#include "StringTab.h"

} ;

#undef _STRTABENTRY


// Definition der Funktion GetIStr(uint16_t stri)

extern PGM_P GetIStr(uint16_t stri)
	{
	return (PGM_P) pgm_read_word(&IStrList[stri]);
	}



