/***************************************************************************
 *            apps_init.c
 *
 *  Sat Dec 19 23:32:24 2009
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
#include "system/thread/thread.h"
#include "apps/cron/cron.h"
#include "apps/apps_init.h"
#include "apps/telnet/telnet.h"
#include "apps/httpd/httpd2.h"

#include "config.h"

#if defined(LCD)
	#include "apps/modules/lcd_info.h"
#endif
#ifdef __AVR_ATmega2561__
	#include "apps/mp3-streamingclient/mp3-streaming.h"
#endif
#if defined(TWI) || defined(ONEWIRE)
	#include "system/sensor/temp.h"
#endif
#if defined(CAN)
	#include "apps/can/can_eth_rep.h"
#endif

void apps_init( void )
{
	// Cron-Dienst starten
	CRON_init();

	CRON_reloadcrontable();

	// Dienste starten
	#ifdef HTTPSERVER
		httpd_init();
	#endif
	#if defined(TELNETSERVER)
		telnet_init();
	#endif
	#if defined(HTTPSERVER_STREAM) && defined(OpenMCP) && defined(__AVR_ATmega2561__)
		mp3client_init();
	#endif
	#if defined(LCD)
		LCDINFO_init();
	#endif
	#if defined(TWI) || defined(ONEWIRE)
		TEMP_init();
	#endif
	#if defined(CAN)
		CANETHREP_init();
	#endif
}
