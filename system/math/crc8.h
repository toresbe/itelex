/*! \file checksum.h \brief Berechnet die Checksumme eine Blockes */
/***************************************************************************
 *            crc8.h
 *
 *  Sun Mai 01 01:30:22 2011
 *  ------------------------
 *  Email
 ****************************************************************************/
///	\ingroup math
//@{
#ifndef __CRC8_H__

	#define __CRC8_H__

	#include "stdint.h"

	char crc8( char *data, char len );

#endif
//@}
