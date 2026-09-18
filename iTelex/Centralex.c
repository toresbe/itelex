/*! \file Centralex.c
 *  \brief Centralex remote incoming-call relay.
 */

#include <avr/pgmspace.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#ifdef ITELEX_ANSCHLUSS

#include "system/net/dns.h"
#include "system/net/ip.h"
#include "system/net/tcp.h"

#include "Centralex.h"

/* These legacy interfaces still declare avr-libc's deprecated prog_char.
 * The declarations are outside this module's scope to modernize. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "iTelex.h"
#include "Protokoll.h"
#include "StringTab.h"
#pragma GCC diagnostic pop

/** \ingroup centralex
 *  @{ */

//! Protocol command codes used by the Centralex link.
enum {
	ITELEXC_NULL = 0x00,
	ITELEXC_END = 0x03, // ehem. ITELEXC_ENDE
	ITELEXC_STOP = 0x04,
	ITELEXC_REMOTE_CONNECT = 0x81,
	ITELEXC_REMOTE_CONFIRM = 0x82,
	ITELEXC_REMOTE_CALL = 0x83,
	ITELEXC_REMOTE_CALLACCEPT = 0x84,
	};

// Centralex protocol overview

/*
This feature lets an i-Telex interface receive calls while connected through
an IP service without a public address. While idle, the interface maintains a
connection to Centralex. When another subscriber calls through Centralex, the
server hands that connection to this interface.

Sequence:

Caller      Subscriber      Centralex           This interface
            server

                                        RemConnect       X
                                X      <----------       X
                                X                        X
                        (selects free port)               X
                                X                        X
                SELBSTAKT       X       Heartbeat        X
                X   <--------   X      ----------->      X
                X               X                        X
                X               X       Heartbeat        X
                X IPRUECKMELD   X      <-----------      X
                X ----------->  X                        X
                                X       RemConfirm       X
                                X      ----------->      X
                                X       Heartbeat        X
                                X      <-----------      X
                                X       Heartbeat        X
                                X      ----------->      X
                                X                        X
       ABFRAGE (query)          X                        X
    X --------> X               X                        X
    X AUSKUNFT (response)       X                        X
    X <-------- X               X                        X
    X                           X                        X
    X         normal call       X                        X
    X        ----------->       X                        X
    X                           X       RemCall          X
    X                           X      ----------->      X
    X                           X (no more heartbeats)   X
    X                           X          (call handling proceeds like)
    X      optional data        X                 (opening a socket)
    X        ----------->       X                        X
    X                     (buffers data)                 X
    X                           X                        X
    X                           X       RemAck           X
    X                           X      <-----------      X
    X                           X                        X
    X                    (relay established)             X
    X                           X     buffered data      X
    X                           X      ----------->      X
    X                           X                        X
    X       i-Telex data        X     i-Telex data       X
    X       <----------->       X     <----------->      X
    X                           X                        X

Centralex ends the relayed connection as soon as either peer disconnects.

*/

static bool CentralexEnabled; // ehem. RemoteServerActive
	//! Whether the Centralex connection is enabled.
	//! Mutually exclusive with dynamic IP registration.


static enum {
	CentralexDisconnected, // ehem. RemServNotConnected; may need a connection
	CentralexAwaitingConfirmation, // ehem. RemServStarting; socket open, confirmation pending
	CentralexConnected, // ehem. RemServConnected
	CentralexStopPending, // ehem. RemServSendStop; a stop signal must be sent
	CentralexDisconnecting, // ehem. RemServDisconnecting; hangup sent, awaiting close
} CentralexLinkState; // ehem. RemoteServerLinkStatus


static int CentralexSocketHandle; // ehem. RemoteServerLinkSocketHandle
	//!< Socket used to maintain the Centralex connection.


static char CentralexBuffer[SocketInBufMax+4]; // ehem. RemoteServerSocketBuf
	//!< Buffer used to send and receive Centralex messages.

static uint16_t CentralexBufferUsed; // ehem. RemoteServerSocketBufUsed
	//!< Number of bytes in #CentralexBuffer.

static TKurzTimer CentralexActionTimer; // ehem. RemoteServerActionTimer
	//!< Times heartbeats, delayed socket close, and reconnection attempts.

static uint16_t CentralexReconnectDelay; // ehem. RemoteServerReconnectTimerEnd
	//!< Backoff delay before the next reconnection attempt.

static TKurzTimer CentralexCheckTimer; // ehem. RemoteServerCheckTimer
	//!< Times incoming heartbeats and the initial confirmation deadline.

static uint8_t CentralexServerIndex; // ehem. RemoteServerAddressIndex
	//!< Index of the subscriber-server address used for the next attempt.

static TLangTimer CentralexErrorMessageTimer; // ehem. RemoteServerErrorMessageDelay
	//!< Delays the user warning during temporary Centralex failures.

static bool CentralexErrorReported; // ehem. RemoteServerErrorMessageSent
	//!< Whether the user has already been told that all servers failed.

static uint16_t CentralexPort; // ehem. RemoteServerPort



void CentralexInitialize(void) // ehem. RemoteServerInitialize
	{
	CentralexSocketHandle = NO_SOCKET_USED;
	CentralexLinkState = CentralexDisconnected;
	CentralexBufferUsed = 0;
	CentralexServerIndex = 0;
	CentralexReconnectDelay = 0;
	StartLangTimer(&CentralexErrorMessageTimer);
	CentralexErrorReported = false;
	}


bool CentralexIsEnabled(void) // ehem. RemoteServerIsActive
	{
	return CentralexEnabled;
	}


void CentralexSetEnabled(bool enabled) // ehem. RemoteServerSetActive
	{
	CentralexEnabled = enabled;
	}


void CentralexSetPort(uint16_t port) // ehem. RemoteServerSetPort
	{
	CentralexPort = port;
	}


void CentralexPrintDiagnostics(void) // ehem. RemoteServerPrintDebug
	{
#define PRINTVAL(Var) printf_P(PSTR("<br>" #Var " = %u"), Var)

	PRINTVAL(CentralexEnabled);
	PRINTVAL(CentralexLinkState);
	PRINTVAL(CentralexSocketHandle);
	PRINTVAL(CentralexBufferUsed); // Should always be zero.
	PRINTVAL(KurzTimerVal(&CentralexActionTimer));
	PRINTVAL(CentralexReconnectDelay);
	PRINTVAL(KurzTimerVal(&CentralexCheckTimer));
	PRINTVAL(CentralexServerIndex);

#undef PRINTVAL
	}

/** Select the next configured server and increase the reconnect backoff. */
static void SelectNextCentralexServer(void) // ehem. RemoteServerUseAnotherOne
	{
	uint8_t i; // Counts server changes.

	CentralexReconnectDelay += CentralexReconnectDelay / 2;

	if (CentralexReconnectDelay < 4 * KurzTimerFreq)
		CentralexReconnectDelay = 4 * KurzTimerFreq;
	else if (CentralexReconnectDelay > 120 * KurzTimerFreq)
		CentralexReconnectDelay = 120 * KurzTimerFreq;

	if (ProtokollAktivFuer(NurFehler))
		{
		ProtokollierenITelex_P(PSTR("* Remote Server Wechsel, neue Verzoegerung "));
		ProtokollierenInt_P(PSTR("%d/100 sek\r\n"), CentralexReconnectDelay);
		}

	for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
		{
		CentralexServerIndex++;
		if (CentralexServerIndex >= ANZ_TEILNEHMER_SERVER)
			CentralexServerIndex = 0;

		if (TeilnehmerServerAdresse[CentralexServerIndex][0] != '\0')
			return;

		}
	}


/** Send and clear buffered Centralex data, if any. */
static void SendCentralexBuffer(void) // ehem. RemoteServerSendBufferIfNotEmpty
	{
	if (CentralexBufferUsed > 0)
		{
		if (CentralexSocketHandle != NO_SOCKET_USED)
			{
			int result = PutSocketData_RPE(CentralexSocketHandle, CentralexBufferUsed, CentralexBuffer, RAM); // ehem. Res

			if (ProtokollAktivFuer(AuchRegelmaessiges)
				|| (ProtokollAktivFuer(DatenDetailliert) && CentralexBuffer[0] != 0))
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("RemoteServer Sendung: (%u)" ), CentralexBufferUsed);
				ProtokollierenPuffer(CentralexBuffer, CentralexBufferUsed);
				ProtokollierenInt_P(PSTR(" --> Res %d\r\n"), result);
				}

			if (result != CentralexBufferUsed)
				{
				ProtokollierenITelex_P(PSTR("! RemoteServer wird wegen Sendefehler getrennt.\r\n"));
				CloseTCPSocket(CentralexSocketHandle);
				CentralexSocketHandle = NO_SOCKET_USED;
				CentralexLinkState = CentralexDisconnected;
				SelectNextCentralexServer();
				}

			}
		else
			{
			if (ProtokollAktivFuer(AblaufInfo))
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("RemoteServer verwerfe %u Bytes Daten aus Sende-Puffer bei geschlossenem Socket\r\n"), CentralexBufferUsed);
				}
			}
		CentralexBufferUsed = 0;
		StartKurzTimer(&CentralexActionTimer);
		}
	} // SendCentralexBuffer()


/** Process received data, connection state, timeouts, and pending sends. */
void CentralexProcess(void) // ehem. RemoteServerBearbeiten
	{
	// Receive data on an existing connection.
	if (CentralexSocketHandle != NO_SOCKET_USED)
		{
		int incomingCount = GetBytesInSocketData(CentralexSocketHandle); // ehem. InCount

		if (incomingCount > 0 && incomingCount >= SocketInBufMax - CentralexBufferUsed)
			// The second comparison is unsigned, so check incomingCount > 0 first.
			{
			if (ProtokollAktivFuer(NurFehler))
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("* RemoteServer Empfang drohender Ueberlauf: Empfang von %d " ), incomingCount);
				ProtokollierenInt_P(PSTR("limitiert auf %d\r\n" ), SocketInBufMax - CentralexBufferUsed);
				}

			incomingCount = SocketInBufMax - CentralexBufferUsed;
			}

		if (incomingCount > 0)
			{
			int result = GetSocketData(CentralexSocketHandle, incomingCount, CentralexBuffer + CentralexBufferUsed); // ehem. Res

			if (ProtokollAktivFuer(AuchRegelmaessiges)
				|| (ProtokollAktivFuer(DatenDetailliert) && CentralexBuffer[0] != 0))
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("RemoteServer Empfang: (%d/" ), incomingCount);
				ProtokollierenInt_P(PSTR("%d)"), result);
				if (result > 0)
					ProtokollierenPuffer(CentralexBuffer + CentralexBufferUsed, result);
				ProtokollierenInt_P(PSTR(" --> BufUsed %u\r\n"), CentralexBufferUsed + result);
				}

			if (result > 0)
				{
				CentralexBufferUsed += result;
				}
			}
		}

	// Process received data.
	if (CentralexBufferUsed > 0)
		{
		int bytesProcessed = 0; // ehem. BytesProcessed

		switch (CentralexBuffer[0])
			{
			case ITELEXC_REMOTE_CONFIRM:
				if (CentralexLinkState == CentralexAwaitingConfirmation)
					CentralexLinkState = CentralexConnected;
				StartKurzTimer(&CentralexCheckTimer);
				if (CentralexBufferUsed > 1)
					bytesProcessed = CentralexBuffer[1] + 2;
				else
					bytesProcessed = 1;
				// TODO? process additional data?
				FalschGeheimzahlZaehler = 0;
				if (CentralexErrorReported)
					{
					Diagnoseausgabe_P(ISTR(TeilnehmerServerWiederErreichbar, LokaleSprache), 1);
					CentralexErrorReported = false;
					}
				StartLangTimer(&CentralexErrorMessageTimer);
				break;

			case ITELEXC_REMOTE_CALL:
				if (iTelexSocketMode == SocketIdle && Modus == ModRuhe)
					{ // The interface is idle and can accept the call.
					if (ProtokollAktivFuer(AblaufInfo))
						{
						ProtokollierenITelex_P(PSTR("RemoteServer hat Verbindungswunsch geschickt, wird angenommen:\r\n"));
						}

					// Send confirmation.
					CentralexBuffer[0] = ITELEXC_REMOTE_CALLACCEPT;
					CentralexBuffer[1] = 0; // No data.
					CentralexBufferUsed = 2;
					SendCentralexBuffer();

					// Hand the socket to the main i-Telex connection.
					iTelexSocketHandle = CentralexSocketHandle;
					CentralexSocketHandle = NO_SOCKET_USED;
					CentralexLinkState = CentralexDisconnected; // Centralex no longer owns the socket.

					KommendeVerbindungInitialisieren();
						// Initialize all incoming-call state.

					return;
					}
				else
					{ // Reject the call as busy because it cannot be accepted.
					if (ProtokollAktivFuer(AblaufInfo))
						{
						ProtokollierenITelex_P(PSTR("RemoteServer hat Verbindungswunsch geschickt, wird abgewiesen:\r\n"));
						}

					// Send the rejection.
					CentralexBuffer[0] = ITELEXC_STOP;
					CentralexBuffer[1] = 3; // Length of "occ".
					CentralexBuffer[2] = 'o';
					CentralexBuffer[3] = 'c';
					CentralexBuffer[4] = 'c';
					CentralexBufferUsed = 5;
					}

				break;

			case ITELEXC_NULL:
				StartKurzTimer(&CentralexCheckTimer);
				StartLangTimer(&CentralexErrorMessageTimer);
				if (CentralexBufferUsed > 1)
					bytesProcessed = CentralexBuffer[1] + 2;
				else
					bytesProcessed = 1;
				break;

			case ITELEXC_STOP:
				FalschGeheimzahlWurdeGemeldet();
				// Fall through and handle this like END.

			case ITELEXC_END: // Centralex requests disconnection.
				if (ProtokollAktivFuer(AblaufInfo))
					{
					ProtokollierenITelex_P(PSTR("* RemoteServer hat Ende-Befehl geschickt\r\n"));
					}
				bytesProcessed = CentralexBufferUsed;
				CloseTCPSocket(CentralexSocketHandle);
				CentralexSocketHandle = NO_SOCKET_USED;
				StartKurzTimer(&CentralexActionTimer); // Delay the reconnection attempt.
				CentralexLinkState = CentralexDisconnected;
				SelectNextCentralexServer();
				break;

			default:
				if (ProtokollAktivFuer(NurFehler))
					{
					ProtokollierenITelex_P(PSTR("! RemoteServer Ungueltiges Telegramm empfangen:" ));
					ProtokollierenPuffer(CentralexBuffer, CentralexBufferUsed);
					Protokollieren_P(PSTR(" --> wird verworfen\r\n"));
					}
				bytesProcessed = CentralexBufferUsed;
				break;

			} // switch (CentralexBuffer[0])

		if (bytesProcessed >= CentralexBufferUsed)
			CentralexBufferUsed = 0; // The complete buffer was processed.
		else if (bytesProcessed > 0)
			{ // Only part of the buffer was processed.
			CentralexBufferUsed -= bytesProcessed;
			memmove(CentralexBuffer, CentralexBuffer + bytesProcessed, CentralexBufferUsed);
			}

		} // if CentralexBufferUsed > 0

	// Detect when the server closes the connection.
	if (CentralexSocketHandle != NO_SOCKET_USED
		&& CheckSocketState(CentralexSocketHandle) == SOCKET_NOT_USE)
		{
		if (ProtokollAktivFuer(AblaufInfo))
			{
			if (CentralexLinkState == CentralexDisconnecting)
				ProtokollierenITelex_P(PSTR("RemoteServer hat Socket korrekt geschlossen.\r\n"));
			else
				{
				ProtokollierenITelex_P(PSTR("* RemoteServer hat Socket UNGEPLANT geschlossen.\r\n"));
				SelectNextCentralexServer();
				}
			}
		CentralexSocketHandle = NO_SOCKET_USED;
		CentralexLinkState = CentralexDisconnected;
		StartKurzTimer(&CentralexActionTimer);
		CentralexBufferUsed = 0; // Discard any remaining buffered data.
		}

	// Check timeouts.
	if ((CentralexLinkState == CentralexConnected && KurzTimerVal(&CentralexCheckTimer) >= 35 * KurzTimerFreq)
		|| (CentralexLinkState == CentralexAwaitingConfirmation && KurzTimerVal(&CentralexCheckTimer) >= 25 * KurzTimerFreq))
		{
		CentralexLinkState = CentralexStopPending; // Send END, then disconnect.
		}
	else if (CentralexBufferUsed > 0)
		return; // Wait for the rest of an incomplete message unless a timeout occurred.

	// Disconnect when the link is no longer needed.
	if (Modus != ModRuhe || !CentralexEnabled || CentralexLinkState == CentralexStopPending)
		{
		if (CentralexLinkState != CentralexDisconnected &&
		    CentralexLinkState != CentralexDisconnecting)
			{
			if (ProtokollAktivFuer(AblaufInfo))
				{
				ProtokollierenITelex_P(PSTR("RemoteServer sende Ende\r\n"));
				}

			if (Modus == ModRuhe || !CentralexEnabled)
				{ // Centralex is no longer used -> nc
				CentralexBuffer[0] = ITELEXC_END;
				CentralexBuffer[1] = 0x02;
				CentralexBuffer[2] = 'n';
				CentralexBuffer[3] = 'c';
				CentralexBufferUsed = 4;
				}
			else if (Modus == ModDeaktiviert)
				{ // Interface disabled -> abc
				CentralexBuffer[0] = ITELEXC_END;
				CentralexBuffer[1] = 0x03;
				CentralexBuffer[2] = 'a';
				CentralexBuffer[3] = 'b';
				CentralexBuffer[4] = 's';
				CentralexBufferUsed = 5;
				}
			else
				{ // Interface busy -> occ
				CentralexBuffer[0] = ITELEXC_END;
				CentralexBuffer[1] = 0x03;
				CentralexBuffer[2] = 'o';
				CentralexBuffer[3] = 'c';
				CentralexBuffer[4] = 'c';
				CentralexBufferUsed = 5;
				}

			CentralexLinkState = CentralexDisconnecting;
			// Do not select another server after an intentional disconnect.

			StartKurzTimer(&CentralexActionTimer);
			StartKurzTimer(&CentralexCheckTimer);

			CentralexReconnectDelay = 3 * KurzTimerFreq; // 3 seconds
			}

		if (CentralexLinkState == CentralexDisconnected)
			{ // Reset all timers while Centralex is intentionally unused.
			StartKurzTimer(&CentralexActionTimer);
			StartKurzTimer(&CentralexCheckTimer);
			StartLangTimer(&CentralexErrorMessageTimer);
			}

		} // if Modus != ModRuhe || !CentralexEnabled || CentralexLinkState == CentralexStopPending

	// Establish the connection when appropriate.
	if (Modus == ModRuhe
		&& CentralexEnabled
		&& CentralexLinkState == CentralexDisconnected
		&& KurzTimerVal(&CentralexActionTimer) >= CentralexReconnectDelay)
		{
		// Error paths below return early, so emit the delayed diagnostic here.
		if (!CentralexErrorReported && LangTimerVal(&CentralexErrorMessageTimer) >= 10 * LangTimerMinuteFaktor)
			{
			Diagnoseausgabe_P(ISTR(KeinTeilnehmerServerErreichbar, LokaleSprache), 1);
			CentralexErrorReported = true;
			}

		long centralexIp; // ehem. RemoteServerIP

		// TODO: Replace this with a general server-connection helper.

		centralexIp = strtoip(TeilnehmerServerAdresse[CentralexServerIndex]); // First assume a numeric IP address.

		if (centralexIp == 0) // Otherwise, resolve the value as a hostname.
			centralexIp = DNS_ResolveName(TeilnehmerServerAdresse[CentralexServerIndex]);

		if (centralexIp == -1)
			{
			if (ProtokollAktivFuer(NurFehler))
				{
				ProtokollierenITelex_P(PSTR("! Remote Server Hostname "));
				Protokollieren(TeilnehmerServerAdresse[CentralexServerIndex]);
				Protokollieren_P(PSTR(" unbekannt\r\n"));
				}
			CentralexSocketHandle = NO_SOCKET_USED;
			SelectNextCentralexServer();
			StartKurzTimer(&CentralexActionTimer);
			return; // Nothing else can be done this cycle.
			}

		CentralexSocketHandle = Connect2IP(centralexIp, CentralexPort);

		StartKurzTimer(&CentralexActionTimer); // Exclude the five-second connect timeout.

		if (CentralexSocketHandle == SOCKET_ERROR)
			{ // The connection could not be established.
			if (ProtokollAktivFuer(NurFehler))
				{
				ProtokollierenITelex_P(PSTR("! Remote Server "));
				Protokollieren(TeilnehmerServerAdresse[CentralexServerIndex]);
				Protokollieren_P(PSTR(" konnte nicht geoeffnet werden\r\n"));
				}
			CentralexSocketHandle = NO_SOCKET_USED;
			SelectNextCentralexServer();
			return; // Nothing else can be done this cycle.
			}

		if (ProtokollAktivFuer(AblaufInfo))
			{
			ProtokollierenITelex_P(PSTR("Remote Server "));
			Protokollieren(TeilnehmerServerAdresse[CentralexServerIndex]);
			Protokollieren_P(PSTR(" erfolgreich geoeffnet\r\n"));
			}

		CentralexBuffer[0] = ITELEXC_REMOTE_CONNECT;
		CentralexBuffer[1] = 0x06; // Four-byte local number and two-byte PIN.
		*((uint32_t *)(CentralexBuffer+2)) = NetzRufnummer;
		*((uint16_t *)(CentralexBuffer+6)) = Geheimzahl;
		CentralexBufferUsed = 8;

		CentralexLinkState = CentralexAwaitingConfirmation;

		StartKurzTimer(&CentralexCheckTimer);
		CentralexReconnectDelay = 3 * KurzTimerFreq;
			// A successful connection should not need an immediate retry.

		} // if (ModRuhe && CentralexEnabled && CentralexDisconnected && CentralexActionTimer >= CentralexReconnectDelay)

	// Send a heartbeat.
	if (CentralexLinkState == CentralexConnected
		&& CentralexBufferUsed == 0
		&& KurzTimerVal(&CentralexActionTimer) >= 15 * KurzTimerFreq)
		{
		CentralexBuffer[0] = ITELEXC_NULL;
		CentralexBuffer[1] = 0; // No data.
		CentralexBufferUsed = 2;
		}

	// Send pending data.
	SendCentralexBuffer();

	// Close the socket after the disconnect delay.
	if (CentralexLinkState == CentralexDisconnecting && KurzTimerVal(&CentralexActionTimer) > 3 * KurzTimerFreq)
		{
		CloseTCPSocket(CentralexSocketHandle);
		CentralexSocketHandle = NO_SOCKET_USED;
		CentralexLinkState = CentralexDisconnected;
		StartKurzTimer(&CentralexActionTimer);
		}

	} // CentralexProcess()

/** @} */

#endif /* ITELEX_ANSCHLUSS */
