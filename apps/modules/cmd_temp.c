/***************************************************************************
 *            cmd_temp.c
 *
 *  Thu Nov  5 17:02:56 2009
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

/**
 * \ingroup modules
 * \addtogroup cmd_temp Shell-Interface um Tempsensoren auszulesen (cmd_temp.c)
 *
 * @{
 */

/**
 * \file
 *
 * \author Dirk Broßwick
 */
 
#include <avr/pgmspace.h>
#include <avr/version.h>
#include <avr/eeprom.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "config.h"

#include "system/shell/shell.h"
#include "system/sensor/temp.h"

#include "cmd_temp.h"

void init_cmd_temp( void )
{
#if defined(SHELL)
	SHELL_RegisterCMD( cmd_temp, PSTR("temp"));
#endif
}

int cmd_temp( int argc, char ** argv )
{		
	int Temp;
	char TempString[10];

	if ( argc == 2 )
	{
		Temp = TEMP_readtempstr( argv[ 1 ] );

		TEMP_Temp2String( Temp, TempString );
		printf_P( PSTR("%s"), TempString );

	}
	return(0);
}

/**
 * @}
 */

