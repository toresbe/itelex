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

/** Reset all private RemoteServer connection state. */
void RemoteServerInitialize(void);

/** Process one cycle of RemoteServer network activity. */
void RemoteServerBearbeiten(void);

/** Return whether RemoteServer use is enabled. */
bool RemoteServerIsActive(void);

/** Enable or disable RemoteServer use.
 *  \param active True to maintain the relay connection while idle.
 */
void RemoteServerSetActive(bool active);

/** Set the TCP port used to reach the RemoteServer.
 *  \param port RemoteServer TCP port in host byte order.
 */
void RemoteServerSetPort(uint16_t port);

/** Print the module's diagnostic rows to the current standard output. */
void RemoteServerPrintDebug(void);

/** @} */

#endif /* ITELEX_ANSCHLUSS */

#endif /* ITELEX_CENTRALEX_H */
