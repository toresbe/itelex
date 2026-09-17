/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA
 */
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include "softreset.h"

// Function Implementation
void softreset( void )
{
	do                          
	{            
		// möglicherweise gesetzte Interrupt-Freigaben löschen statt cli() da der WDTIE aktiv bleibt.
		TIMSK0 &= ~((1<<OCIE0A)|(1<<OCIE0B)|(1<<TOIE0));
		TIMSK1 &= ~((1<<OCIE1A)|(1<<OCIE1B));
		UCSR0B &= ~((1<<UDRIE0)|(1<<TXCIE0)|(1<<RXCIE0));
		UCSR1B &= ~((1<<UDRIE1)|(1<<TXCIE1)|(1<<RXCIE1));
		TWCR &= ~((1<<TWIE)|(1<<TWEN));
	
		wdt_disable();
   		wdt_enable(WDTO_250MS);  
   		for(;;)                 
			{                       
			}                       
	} while(1);
}

// Function Implementation
void wdt_init(void)
{
#if defined(__AVR_XMEGA__)
	wdt_reset();
#else
	asm("CLR R1"); 
		// da der Compiler permanent R1 für 0 verwendet, dies aber erst nach .init1 initialisiert, 
		// wird dies hier vorsorglich gemacht, damit MCUSR = 0 funktioniert.

	GPIOR2 = MCUSR;
	GPIOR0 = SPL;
	GPIOR1 = SPH;
		// kann dann später dort ausgelesen werden.

	MCUSR = 0;
    wdt_disable();
		
#endif	
    return;
}

