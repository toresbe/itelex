#include "BaudotCode.h"

#include <avr/pgmspace.h>

// Übersetzer-Tabellen
// -------------------         0           1         2         3 
//                             012 345678 90123456789012345678901
char TtyCodeTabBu[] PROGMEM = "#t\ro hnm\nlrgipcvezdbsyfxawj#uqk#";
char TtyCodeTabZi[] PROGMEM = "#5\r9 #,.\n)4#80:=3+#?'6#/-2##71(#";


uint8_t ZeichenZuCode(char c, char Mode)
	{
	volatile prog_char* tp;
	if (Mode == BuMode)
		tp = TtyCodeTabBu;
	else if (Mode == ZiMode)
		tp = TtyCodeTabZi;
	else
		return 255;

	if (c >= 'A' && c <= 'Z')
		c += 'a' - 'A';
	for (uint8_t i = 0 ; i < 32 ; i++)
		if (c == pgm_read_byte(tp))
			return i;
		else
			tp++;
	return 255;
	}


char CodeZuZeichen(uint8_t code, char *Mode)
	{
	if (code == TtyCodeZiUm)
		*Mode = ZiMode;
	else if (code == TtyCodeBuUm)
		*Mode = BuMode;
	else
		{
		if (*Mode == ZiMode)
			return pgm_read_byte(&TtyCodeTabZi[code]);
		else if (*Mode == BuMode)
			return pgm_read_byte(&TtyCodeTabBu[code]);
		}
	return '\0';
	}
	

#ifndef FUER_TW39

bool ZeichenZuCode2(char c, char *Mode, uint8_t* Code1, uint8_t* Code2)
	{
	if (c == CodeChrWerDa)
		{
		*Code1 = TtyCodeZiUm;
		*Code2 = TtyCodeZiWerDa;
		*Mode = '\0'; // undefiniert!
		return true;
		}

	*Code1 = ZeichenZuCode(c, *Mode);
	*Code2 = 255;

	if (*Code1 != 255)
		return true; // fertig

	*Code2 = ZeichenZuCode(c, BuMode); // andere Tabelle probieren
	if (*Code2 != 255)
		{
		*Code1 = TtyCodeBuUm;
		*Mode = BuMode;
		return true;
		}

	*Code2 = ZeichenZuCode(c, ZiMode); // andere Tabelle probieren
	if (*Code2 != 255)
		{
		*Code1 = TtyCodeZiUm;
		*Mode = ZiMode;
		return true;
		}

	return false; // Zeichen gar nicht umsetzbar
	}

		
#endif //def FUER_TW39
