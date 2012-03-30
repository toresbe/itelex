/*! \file xram.c \brief Aktiviert das externe RAM-Interface */
//***************************************************************************
//*            xram.c
//*
//*  Sat Jun  3 23:01:42 2006
//*  Copyright  2006  User
//*  Email
//****************************************************************************/
///	\ingroup hardware
///	\defgroup xram Aktiviert das externe RAM-Interface (xram.c)
///	\code #include "xram.h" \endcode
///	\par Uebersicht
/// Aktiviert das externe RAM-Interface. Wenn die xram.h eingebunden wird, wird
/// automatisch die Aktivierung in .init eingetragen und steht somit sofort zur
/// Verfügung.
/// \date 30-03-2011: Fred Sonnenrein: Speichertest verbessert: Vollständiges 
///       Füllen mit Pseudo-Zufallsmuster und Wieder-Auslesen.
//****************************************************************************/
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
//@{

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>

#include "config.h"

#if defined EXTMEM

#include "xram.h"

#if defined(LED)
	#include "hardware/led/led_core.h"
#endif


/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Generiert aus Adresse ein Sollwert für den Speichertest
 * \param 	Addr Adresse der zu testenden Speicherstelle
 * \return	Wert, der zunächst im Speicher abgelegt wird und danach der Sollwert
 *			fur das Rücklesen ist
 */
/*------------------------------------------------------------------------------------------------------------*/

static uint8_t MemTestValue(uint16_t Addr)
{
	uint8_t Res;
	uint8_t Magic;
	uint8_t i;

	Res = (Addr & 0xFF) + (Addr >> 8);
	Magic = (Addr >> 8) % 13; // ja, Modulo!

	for (i = 0 ; i < Magic ; i++)
	{
		if (Res >= 0x80)
			Res = ((Res << 1) & 0xFF) | 1; // Rotieren nach links
		else
			Res <<= 1;
	}

	if (Magic >= 8)
		Res = ~Res;

	return Res;
}


/*------------------------------------------------------------------------------------------------------------*/
/*!\brief Aktiviert das externe RAM-Interface und testet den externen RAM
 * \param 	NONE
 * \return	NONE
 */
/*------------------------------------------------------------------------------------------------------------*/
void __attribute__ ((naked, section(".init3"))) init_xram (void)
{
#if !defined(__AVR_XMEGA__)
	// externes RAM-Interface freigeben
	XMCRA |= ( 1<<SRE );

	// A16 freigeben, hängt an PD7, damit der RAM funktioniert, wenn dies nicht gemacht wird, ist A16 
	// tristate und der RAM macht komische sachen :-)
	DDRD |= ( 1<<PD7 );
	PORTD &= ~( 1<<PD7 );
#else

	#define   SDRAM_ADDR (0x4000)
	
	PORTH.OUT = 0x0F;                      // EBI PORTs richten
	PORTH.DIR = 0xFF;
	PORTK.DIR = 0xFF;
	PORTJ.DIR = 0xF0;

	EBI.CTRL       = EBI_SDDATAW_4BIT_gc | EBI_IFMODE_3PORT_gc;
	EBI.SDRAMCTRLA = (EBI.SDRAMCTRLA & ~(EBI_SDCAS_bm | EBI_SDROW_bm | EBI_SDCOL_gm)) |
	                  EBI_SDCOL_10BIT_gc | EBI_SDROW_bm;
	EBI.SDRAMCTRLB = EBI_MRDLY_0CLK_gc | EBI_ROWCYCDLY_1CLK_gc | EBI_RPDLY_0CLK_gc;
	EBI.SDRAMCTRLC = EBI_WRDLY_0CLK_gc | EBI_ESRDLY_0CLK_gc    | EBI_ROWCOLDLY_0CLK_gc;
	EBI.REFRESH    = 0xff00;
	EBI.INITDLY    = 0x0100;
	EBI.CS3.CTRLB  = (EBI.CS3.CTRLB & ~(EBI_CS_SDSREN_bm | EBI_CS_SDMODE_gm)) |
	                  EBI_CS_SDMODE_NORMAL_gc;
	EBI.CS3.BASEADDR = (((uint32_t) SDRAM_ADDR)>>8) & (0xFFFF<<(EBI_CS_ASIZE_8MB_gc>>2));
	EBI.CS3.CTRLA  = (EBI.CS3.CTRLA & ~(EBI_CS_ASIZE_gm | EBI_CS_MODE_gm)) |
	                  EBI_CS_ASIZE_8MB_gc | EBI_CS_MODE_SDRAM_gc;

	while(!(EBI.CS3.CTRLB & EBI_CS_SDINITDONE_bm)); // warten bis fertig
	
#endif
	
#if defined(LED)
	LED_init();

	LED_on( 0 );
#endif
	
	uint8_t* p;
	uint16_t address;

	wdt_disable(); // der Timeout ist vielleicht noch 10 ms, daher erstmal deaktivieren.
	
	// Speicher vollschreiben
	for (address = 0x2200 ; address < 0xffff ; address++)
	{
		p = (uint8_t*) address;
		*p = MemTestValue(address);
	}

	// Speicher testen
	uint16_t fehler = 0;
	for (address = 0x2200 ; address < 0xffff ; address++)
	{
		p = (uint8_t*) address;
		if (*p != MemTestValue(address))
			fehler++;
		*p = 0;
	}

	// Speicher ok?
	if ( fehler != 0 )
	{
#if defined(LED)
		LED_on( 1 );
#endif
		cli();
		while (1) 
			; // Endlosschleife
	}

#if defined(LED)
	else
	{
		LED_on( 1 );
		LED_off( 0 );
	}
#endif
	
	return;
}
#endif

//@}
