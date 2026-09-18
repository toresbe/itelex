/*! \file Centralex.h
 *  \brief Interface to the Centralex remote incoming-call relay.
 */

#ifndef ITELEX_CENTRALEX_H
#define ITELEX_CENTRALEX_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

/** \defgroup centralex Centralex relay
 *  Maintains the persistent relay connection used to receive calls when the
 *  i-Telex interface does not have a public IP address.
 *  \ingroup iTelex */

#ifdef ITELEX_ANSCHLUSS

/** \addtogroup centralex
 *  @{ */

enum {
	CentralexDefaultPort = 49491, //!< Default Centralex TCP port. ehem. RemoteServerPortDefault
	};

/** Reset all private Centralex connection state. */
void CentralexInitialize(void); // ehem. RemoteServerInitialize

/** Process one cycle of Centralex network activity. */
void CentralexProcess(void); // ehem. RemoteServerBearbeiten

/** Return whether Centralex use is enabled. */
bool CentralexIsEnabled(void); // ehem. RemoteServerIsActive

/** Enable or disable Centralex use.
 *  \param enabled True to maintain the relay connection while idle.
 */
void CentralexSetEnabled(bool enabled); // ehem. RemoteServerSetActive

/** Set the TCP port used to reach Centralex.
 *  \param port Centralex TCP port in host byte order.
 */
void CentralexSetPort(uint16_t port); // ehem. RemoteServerSetPort

/** Print the module's diagnostic rows to the current standard output. */
void CentralexPrintDiagnostics(void); // ehem. RemoteServerPrintDebug

/** @} */

#endif /* ITELEX_ANSCHLUSS */

#endif /* ITELEX_CENTRALEX_H */
