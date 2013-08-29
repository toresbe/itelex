/***************************************************************************
 *            hal.c
 *
 *  Sun Dec 13 16:45:12 2009
 *  Copyright  2009  Dirk Broßwick
 *  <sharandac@snafu.de>
 ****************************************************************************/

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
#include <avr/pgmspace.h>
#include <avr/version.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "system/net/tcp.h"
#include "system/stdout/stdout.h"

#include "config.h"

#include "hal.h"

#if defined(__AVR_ATmega644P__) && defined(myAVR)
HAL hal_table[] = {
	// Port A
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__ADC,
	__LCD,
	__LCD,
	__LCD,
	__LCD,

	// Port B
	__LCD,
	__LCD,
	__LCD,
	__LCD,
	__GPIO_IN,
	__GPIO_IN,
	__PCINT,
	__ONEWIRE,
	
	// Port C
	__TWI,
	__TWI,
	__GPIO_OUT,
	__GPIO_OUT,
	__LED,
	__LED,
	__MMC,
	__SYSTEM,

	// Port D
	__UART,
	__UART,
	__SPI,
	__SPI,
	__SPI,
	__SPI,
	__PCINT,
	__PCINT };

#elif ( defined(__AVR_ATmega644P__) || defined(__AVR_ATmega644__) ) && defined(AVRNETIO)
HAL hal_table[] = {
	// Port A
	__GPIO_IN,
	__GPIO_IN,
	__GPIO_IN,
	__GPIO_IN,
	__ADC,
	__ADC,
	__ADC,
	__ADC,

	// Port B
	__ONEWIRE,
	__GPIO_IN,
	__EXTINT,
	__SPI_CS,
	__SPI_CS,
	__SPI,
	__SPI,
	__SPI,
	
	// Port C
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,

	// Port D
	__UART,
	__UART,
	__MMC,
	__MMC,
	__MMC,
	__MMC,
	__MMC,
	__MMC };

#elif defined(__AVR_ATmega2561__) && defined(OpenMCP)
HAL hal_table[] = {
	// Port A
	__GPIO_IN, 
	__GPIO_IN,
	__GPIO_IN,
	__GPIO_IN,
	__ADC,
	__ADC,
	__ADC,
	__ADC,

	// Port B
	__ONEWIRE,
	__GPIO_IN,
	__EXTINT,
	__MMC,
	__SPI,
	__SPI,
	__SPI,
	__SPI,
	
	// Port C
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,

	// Port D
	__UART,
	__UART,
	__MMC,
	__MMC,
	__MMC,
	__MMC,
	__MMC,
	__MMC
};

#elif defined(__AVR_ATmega2561__) && defined(TXPnet)
HAL hal_table[] = {
// TODO das kann doch gar nicht sein, was hier steht...	
	// Port A
	__XRAM, 
	__XRAM,
	__XRAM,
	__XRAM,
	__XRAM,
	__XRAM,
	__XRAM,
	__XRAM,

	// Port B
	__ONEWIRE,
	__GPIO_IN,
	__EXTINT,
	__MMC,
	__SPI,
	__SPI,
	__SPI,
	__SPI,
	
	// Port C
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,
	__GPIO_OUT,

	// Port D
	__UART,
	__UART,
	__MMC,
	__MMC,
	__MMC,
	__MMC,
	__MMC,
	__MMC

};

#else
	#error "HAL wird für diese Plattform nicht unterstützt."
#endif

void HAL_printconfig( void )
{
	
	int i;

	for ( i = 0 ; i < HAL_PINCOUNT ; i ++ )
	{
		printf_P( PSTR( "Pin%c%d: "),65 + (i/8), i%8);
		
		switch( hal_table[i].HAL_Config )
		{
			case __SYSTEM:			printf_P( PSTR("System"));
											break;
			case __GPIO_OUT: 		printf_P( PSTR("GPIO output"));
											break;
			case __GPIO_IN:  		printf_P( PSTR("GPIO input"));
											break;
			case __GPIO_IN_PULLUP:	printf_P( PSTR("GPIO input with Pullup"));
											break;
			case __ADC:				printf_P( PSTR("ADC"));
											break;
			case __TWI:				printf_P( PSTR("TWI"));
											break;
			case __ONEWIRE:			printf_P( PSTR("1-Wire"));
											break;
			case __PCINT:			printf_P( PSTR("Pin-Change Interrupt"));
											break;
			case __EXTINT:			printf_P( PSTR("Externe Interrupt"));
											break;
			case __UART:				printf_P( PSTR("RS232"));
											break;	
			case __SPI:				printf_P( PSTR("SPI-Bus"));
											break;
			case __LCD:				printf_P( PSTR("LCD-Display"));
											break;
			case __MMC:				printf_P( PSTR("MMC-Interface"));
											break;
			case __XRAM:			printf_P( PSTR("XRAM-Interface"));
											break;
			case __LED:				printf_P( PSTR("LED-Anzeige"));
											break;
			default:						printf_P( PSTR("unknown"));
											break;
		}
		printf_P( PSTR("\r\n"));
	}
}

