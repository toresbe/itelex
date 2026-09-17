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

//! Protocol command codes used by the RemoteServer link.
enum {
	ITELEXC_NULL = 0x00,
	ITELEXC_ENDE = 0x03,
	ITELEXC_STOP = 0x04,
	ITELEXC_REMOTE_CONNECT = 0x81,
	ITELEXC_REMOTE_CONFIRM = 0x82,
	ITELEXC_REMOTE_CALL = 0x83,
	ITELEXC_REMOTE_CALLACCEPT = 0x84,
	};

//*** RemoteServer ****:

/*
Diese Funktion erlaubt die Verwendung des i-Telex an IP-Anschlüssen mit nicht öffentlicher 
IP-Adresse. 
Dazu baut das i-Telex im Ruhezustand eine Verbindung zum "Remote Server" auf.
Sobald dann ein anderer Teilnehmer eine Verbindung zum Remote Server herstellt, wird
diese auf das i-Telex hier durchverbunden.

Ablauf: 

Anrufer     Tln-Server      Remote Server       Anschluss (dieser)

                                        RemConnect       X
                                X      <----------       X
                                X                        X
                        (waehlt freien Port)             X
                                X                        X
                    SELSBTAKT   X       Heartbeat        X
                X   <--------   X      ----------->      X
                X               X                        X
                X               X       Heartbeat        X
                X   IPRUECKMELD X      <-----------      X
                X   ----------> X                        X
                                X       RemConfirm       X
                                X      ----------->      X
                                X       Heartbeat        X
                                X      <-----------      X
                                X       Heartbeat        X
                                X      ----------->      X
                                X                        X
       ABFRAGE                  X                        X
    X --------> X               X                        X
    X  AUSKUNFT X               X                        X
    X <-------- X               X                        X
    X                           X                        X
    X       normaler Anruf      X                        X
    X        ----------->       X                        X
    X                           X       RemCall          X
    X                           X      ----------->      X
    X                           X (keine Heartbeat mehr) X
    X                           X             (Anrufbehandlung wie bei) 
    X        ggf. Daten         X               (Öffnen eines Socket) 
    X        ----------->       X                        X
    X              (Daten zwischenspeichern)             X
    X                           X                        X
    X                           X       RemAck           X
    X                           X      <-----------      X
    X                           X                        X
    X                   (Durchverbindung)                X
    X                           X   gespeicherte Daten   X
    X                           X      ----------->      X
    X                           X                        X
    X       i-Telex-Daten       X     i-Telex-Daten      X
    X       <----------->       X     <----------->      X
    X                           X                        X
    
RemoteServer beendet Durchverbindung sobald eine der 
beteiligten Nachbarn die Verbindung trennt.
    
*/

static bool RemoteServerActive;
	//! Generelle Aktivierung der Verbindung über den RemoteServer
	//! Schließt sich mit DynIPAktiv aus!


static enum { 
	RemServNotConnected, // Remote server not connected, but may be needed
	RemServStarting,  // Remote server connected, but no confirmation received yet
	RemServConnected, // Remote server is connected
	RemServSendStop,  // Remote server is connected but Stop signal shall be sent
	RemServDisconnecting, // Hangup sent to remote server, waiting for disconnection
} RemoteServerLinkStatus;


static int RemoteServerLinkSocketHandle;
	//!< Über diesen Socket wird eine Verbindung zum "Remote Server" gehalten.


static char RemoteServerSocketBuf[SocketInBufMax+4]; 
	//!< Buffer for sending and receiving on RemoteServer

static uint16_t RemoteServerSocketBufUsed;
	//!< Amount of used Buffer in RemoteServerSocketBuf
	
static TKurzTimer RemoteServerActionTimer;
	//!< Timer for several purposes regarding actions with RemoteServerLink: 
	//!< Periodic sending of heartbeat, delay for closing the socket after sending "END",
	//!< delay for re-opening the connection.

static uint16_t RemoteServerReconnectTimerEnd;
	//!< Handles increasing (each failed attempt) delay times for reconnection.

static TKurzTimer RemoteServerCheckTimer;
	//!< Timer for several purposes regarding checking of RemoteServerLink: 
	//!< Periodic receiption of heartbeat, Maximum delay of confirmation

static uint8_t RemoteServerAddressIndex; 
	//!< Which remote server shall be used next time?
	
static TLangTimer RemoteServerErrorMessageDelay;
	//!< in case of temporary failures of the remote server this time delays the warning message for some minutes
	
static bool RemoteServerErrorMessageSent;
	//!< if this flag is set the user shall already be informed about the failed remote servers
	
static uint16_t RemoteServerPort;

	

void RemoteServerInitialize(void)
	{
	RemoteServerLinkSocketHandle = NO_SOCKET_USED;
	RemoteServerLinkStatus = RemServNotConnected;
	RemoteServerSocketBufUsed = 0;
	RemoteServerAddressIndex = 0;
	RemoteServerReconnectTimerEnd = 0;
	StartLangTimer(&RemoteServerErrorMessageDelay);
	RemoteServerErrorMessageSent = false;
	}


bool RemoteServerIsActive(void)
	{
	return RemoteServerActive;
	}


void RemoteServerSetActive(bool active)
	{
	RemoteServerActive = active;
	}


void RemoteServerSetPort(uint16_t port)
	{
	RemoteServerPort = port;
	}


void RemoteServerPrintDebug(void)
	{
#define PRINTVAL(Var) printf_P(PSTR("<br>" #Var " = %u"), Var)

	PRINTVAL(RemoteServerActive);
	PRINTVAL(RemoteServerLinkStatus);
	PRINTVAL(RemoteServerLinkSocketHandle);
	PRINTVAL(RemoteServerSocketBufUsed); // sollte immer 0 sein
	PRINTVAL(KurzTimerVal(&RemoteServerActionTimer));
	PRINTVAL(RemoteServerReconnectTimerEnd);
	PRINTVAL(KurzTimerVal(&RemoteServerCheckTimer));
	PRINTVAL(RemoteServerAddressIndex);

#undef PRINTVAL
	}

//! Unterfunktionen von RemoteServerBearbeiten()

static void RemoteServerUseAnotherOne()
	{
	uint8_t i; // counts the changes...
	
	RemoteServerReconnectTimerEnd += RemoteServerReconnectTimerEnd / 2;
	
	if (RemoteServerReconnectTimerEnd < 4 * KurzTimerFreq)
		RemoteServerReconnectTimerEnd = 4 * KurzTimerFreq;
	else if (RemoteServerReconnectTimerEnd > 120 * KurzTimerFreq)
		RemoteServerReconnectTimerEnd = 120 * KurzTimerFreq;
	
	if (ProtokollAktivFuer(NurFehler))
		{
		ProtokollierenITelex_P(PSTR("* Remote Server Wechsel, neue Verzoegerung "));
		ProtokollierenInt_P(PSTR("%d/100 sek\r\n"), RemoteServerReconnectTimerEnd);
		}
	
	for (i = 0 ; i < ANZ_TEILNEHMER_SERVER ; i++)
		{
		RemoteServerAddressIndex++;
		if (RemoteServerAddressIndex >= ANZ_TEILNEHMER_SERVER)
			RemoteServerAddressIndex = 0;
		
		if (TeilnehmerServerAdresse[RemoteServerAddressIndex][0] != '\0')
			return;
		
		}
	}

	
static void RemoteServerSendBufferIfNotEmpty()
	{
	if (RemoteServerSocketBufUsed > 0)
		{
		if (RemoteServerLinkSocketHandle != NO_SOCKET_USED)
			{
			int Res = PutSocketData_RPE(RemoteServerLinkSocketHandle, RemoteServerSocketBufUsed, RemoteServerSocketBuf, RAM);

			if (ProtokollAktivFuer(AuchRegelmaessiges)
				|| (ProtokollAktivFuer(DatenDetailliert) && RemoteServerSocketBuf[0] != 0))
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("RemoteServer Sendung: (%u)" ), RemoteServerSocketBufUsed);
				ProtokollierenPuffer(RemoteServerSocketBuf, RemoteServerSocketBufUsed);
				ProtokollierenInt_P(PSTR(" --> Res %d\r\n"), Res);
				}
				
			if (Res != RemoteServerSocketBufUsed)
				{
				ProtokollierenITelex_P(PSTR("! RemoteServer wird wegen Sendefehler getrennt.\r\n"));
				CloseTCPSocket(RemoteServerLinkSocketHandle);
				RemoteServerLinkSocketHandle = NO_SOCKET_USED;
				RemoteServerLinkStatus = RemServNotConnected;
				RemoteServerUseAnotherOne();
				}

			}
		else
			{
			if (ProtokollAktivFuer(AblaufInfo))
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("RemoteServer verwerfe %u Bytes Daten aus Sende-Puffer bei geschlossenem Socket\r\n"), RemoteServerSocketBufUsed);
				}
			}
		RemoteServerSocketBufUsed = 0;
		StartKurzTimer(&RemoteServerActionTimer);
		} // 
	} // RemoteServerSendBufferIfNotEmpty()
	

//! RemoteServer bearbeiten.
//---------------------------------------------------------------------------
//! Aufgaben:
//!  - Verbindung zum RemoteServer herstellen wenn aktiv und Verbindungszustand "Leerlauf"
//!  - Verbindung zum RemoteServer trennen wenn nicht mehr benötigt
//!  - Bei bestehender Verbindung Daten senden und empfangen


void RemoteServerBearbeiten()
	{
	// Daten empfangen bei bestehender Verbindung
	// ------------------------------------------
	if (RemoteServerLinkSocketHandle != NO_SOCKET_USED)
		{
		int InCount = GetBytesInSocketData(RemoteServerLinkSocketHandle);
		
		if (InCount > 0 && InCount >= SocketInBufMax - RemoteServerSocketBufUsed)
			// InCount > 0 wirg geprüft, da zweiter Vergleich mit unsigned Werten arbeitet.
			{
			if (ProtokollAktivFuer(NurFehler)) 
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("* RemoteServer Empfang drohender Ueberlauf: Empfang von %d " ), InCount);
				ProtokollierenInt_P(PSTR("limitiert auf %d\r\n" ), SocketInBufMax - RemoteServerSocketBufUsed);
				}
				
			InCount = SocketInBufMax - RemoteServerSocketBufUsed;
			}
		
		if (InCount > 0)
			{
			int Res = GetSocketData(RemoteServerLinkSocketHandle, InCount, RemoteServerSocketBuf + RemoteServerSocketBufUsed);
			
			if (ProtokollAktivFuer(AuchRegelmaessiges)
				|| (ProtokollAktivFuer(DatenDetailliert) && RemoteServerSocketBuf[0] != 0))
				{
				ProtokollierenITelex();
				ProtokollierenInt_P(PSTR("RemoteServer Empfang: (%d/" ), InCount);
				ProtokollierenInt_P(PSTR("%d)"), Res);
				if (Res > 0)
					ProtokollierenPuffer(RemoteServerSocketBuf + RemoteServerSocketBufUsed, Res);
				ProtokollierenInt_P(PSTR(" --> BufUsed %u\r\n"), RemoteServerSocketBufUsed + Res);
				}		
				
			if (Res > 0)
				{
				RemoteServerSocketBufUsed += Res;
				}
			}
		}

	// Empfangene Daten auswerten
	// --------------------------
	if (RemoteServerSocketBufUsed > 0)
		{
		int BytesProcessed = 0;

		switch (RemoteServerSocketBuf[0])
			{
			case ITELEXC_REMOTE_CONFIRM:
				if (RemoteServerLinkStatus == RemServStarting)
					RemoteServerLinkStatus = RemServConnected;
				StartKurzTimer(&RemoteServerCheckTimer);
				if (RemoteServerSocketBufUsed > 1)
					BytesProcessed = RemoteServerSocketBuf[1] + 2;
				else
					BytesProcessed = 1;
				// TODO? process additional data?
				FalschGeheimzahlZaehler = 0;
				if (RemoteServerErrorMessageSent)
					{
					Diagnoseausgabe_P(ISTR(TeilnehmerServerWiederErreichbar, LokaleSprache), 1);	
					RemoteServerErrorMessageSent = false;
					}
				StartLangTimer(&RemoteServerErrorMessageDelay);
				break;
				
			case ITELEXC_REMOTE_CALL:
				if (iTelexSocketMode == SocketIdle && Modus == ModRuhe)
					{ // alles in Ruhezustend, jetzt geht's richtig los... 
					if (ProtokollAktivFuer(AblaufInfo))
						{
						ProtokollierenITelex_P(PSTR("RemoteServer hat Verbindungswunsch geschickt, wird angenommen:\r\n"));
						}
				
					// Bestätigung senden:
					RemoteServerSocketBuf[0] = ITELEXC_REMOTE_CALLACCEPT;
					RemoteServerSocketBuf[1] = 0; // keine Daten
					RemoteServerSocketBufUsed = 2;
					RemoteServerSendBufferIfNotEmpty();
					
					// Socket 'übergeben':
					iTelexSocketHandle = RemoteServerLinkSocketHandle;
					RemoteServerLinkSocketHandle = NO_SOCKET_USED;
					RemoteServerLinkStatus = RemServNotConnected; // da der Socket 'übergeben' wurde

					KommendeVerbindungInitialisieren(); 
						// alles initialisieren, was mit der ankommenden Verbindung zu tun hat.
						
					return; // Fertig!
					}
				else
					{ // besetzt o.ä. senden, da Verbindung nicht angenommen werden kann.
					if (ProtokollAktivFuer(AblaufInfo))
						{
						ProtokollierenITelex_P(PSTR("RemoteServer hat Verbindungswunsch geschickt, wird abgewiesen:\r\n"));
						}
				
					// Abweisund senden.
					RemoteServerSocketBuf[0] = ITELEXC_STOP;
					RemoteServerSocketBuf[1] = 3; // für occ
					RemoteServerSocketBuf[2] = 'o'; 
					RemoteServerSocketBuf[3] = 'c'; 
					RemoteServerSocketBuf[4] = 'c'; 
					RemoteServerSocketBufUsed = 5;
					}
					
				break;
				
			case ITELEXC_NULL:
				StartKurzTimer(&RemoteServerCheckTimer);
				StartLangTimer(&RemoteServerErrorMessageDelay);
				if (RemoteServerSocketBufUsed > 1)
					BytesProcessed = RemoteServerSocketBuf[1] + 2;
				else
					BytesProcessed = 1;
				break;
				
			case ITELEXC_STOP:
				FalschGeheimzahlWurdeGemeldet();
				// kein break, weiter wie bei ENDE
			
			case ITELEXC_ENDE: // Remote Server will nicht mehr...
				if (ProtokollAktivFuer(AblaufInfo))
					{
					ProtokollierenITelex_P(PSTR("* RemoteServer hat Ende-Befehl geschickt\r\n"));
					}
				BytesProcessed = RemoteServerSocketBufUsed;
				CloseTCPSocket(RemoteServerLinkSocketHandle);
				RemoteServerLinkSocketHandle = NO_SOCKET_USED;
				StartKurzTimer(&RemoteServerActionTimer); // damit Wiederaufbau verzögert wird.
				RemoteServerLinkStatus = RemServNotConnected;
				RemoteServerUseAnotherOne();
				break;
				
			default:
				if (ProtokollAktivFuer(NurFehler))
					{
					ProtokollierenITelex_P(PSTR("! RemoteServer Ungueltiges Telegramm empfangen:" ));
					ProtokollierenPuffer(RemoteServerSocketBuf, RemoteServerSocketBufUsed);
					Protokollieren_P(PSTR(" --> wird verworfen\r\n"));
					}
				BytesProcessed = RemoteServerSocketBufUsed;
				break;
				
			} // switch (RemoteServerSocketBuf[0])
			
		if (BytesProcessed >= RemoteServerSocketBufUsed)
			RemoteServerSocketBufUsed = 0; // Puffer ist komplett bearbeitet
		else if (BytesProcessed > 0)
			{ // Puffer ist nur TEILWEISE bearbeitet
			RemoteServerSocketBufUsed -= BytesProcessed;
			memmove(RemoteServerSocketBuf, RemoteServerSocketBuf + BytesProcessed, RemoteServerSocketBufUsed);
			}
		
		} // if (RemoteServerSocketBufUsed > 0) <-- das sind die empfangenen Daten

	// Verbindungsabbau des Servers detektieren
	// ----------------------------------------
	if (RemoteServerLinkSocketHandle != NO_SOCKET_USED 
		&& CheckSocketState(RemoteServerLinkSocketHandle) == SOCKET_NOT_USE)
		{
		if (ProtokollAktivFuer(AblaufInfo))
			{
			if (RemoteServerLinkStatus == RemServDisconnecting)
				ProtokollierenITelex_P(PSTR("RemoteServer hat Socket korrekt geschlossen.\r\n"));
			else
				{
				ProtokollierenITelex_P(PSTR("* RemoteServer hat Socket UNGEPLANT geschlossen.\r\n"));
				RemoteServerUseAnotherOne();
				}
			}
		RemoteServerLinkSocketHandle = NO_SOCKET_USED;
		RemoteServerLinkStatus = RemServNotConnected;
		StartKurzTimer(&RemoteServerActionTimer);
		RemoteServerSocketBufUsed = 0; // ggf. noch vorhandener Pufferinhalt verwerfen.
		}

	// Timeout prüfen
	// --------------
	if ((RemoteServerLinkStatus == RemServConnected && KurzTimerVal(&RemoteServerCheckTimer) >= 35 * KurzTimerFreq)
		|| (RemoteServerLinkStatus == RemServStarting && KurzTimerVal(&RemoteServerCheckTimer) >= 25 * KurzTimerFreq))
		{
		RemoteServerLinkStatus = RemServSendStop; // Ende und dann trennen.
		}
	else if (RemoteServerSocketBufUsed > 0)
		return; // auf weitere Daten warten, da ein unvollständiges Telegramm empfangen wurde. Aber nur, wenn es keinen Timeout gegeben hat.
		
	// Verbindung abbauen wenn nicht (mehr) benötigt
	// ---------------------------------------------
	if (Modus != ModRuhe || !RemoteServerActive || RemoteServerLinkStatus == RemServSendStop)
		{
		if (RemoteServerLinkStatus != RemServNotConnected && 
		    RemoteServerLinkStatus != RemServDisconnecting)
			{
			if (ProtokollAktivFuer(AblaufInfo))
				{
				ProtokollierenITelex_P(PSTR("RemoteServer sende Ende\r\n"));
				}
				
			if (Modus == ModRuhe || !RemoteServerActive)
				{ // Nicht mehr über den Remote-Server verbunden -> nc
				RemoteServerSocketBuf[0] = ITELEXC_ENDE;
				RemoteServerSocketBuf[1] = 0x02;
				RemoteServerSocketBuf[2] = 'n';
				RemoteServerSocketBuf[3] = 'c';
				RemoteServerSocketBufUsed = 4;
				}
			else if (Modus == ModDeaktiviert)
				{ // Abgeschaltet --> abc
				RemoteServerSocketBuf[0] = ITELEXC_ENDE;
				RemoteServerSocketBuf[1] = 0x03;
				RemoteServerSocketBuf[2] = 'a';
				RemoteServerSocketBuf[3] = 'b';
				RemoteServerSocketBuf[4] = 's';
				RemoteServerSocketBufUsed = 5;
				}
			else
				{ // Besetzt --> occ
				RemoteServerSocketBuf[0] = ITELEXC_ENDE;
				RemoteServerSocketBuf[1] = 0x03;
				RemoteServerSocketBuf[2] = 'o';
				RemoteServerSocketBuf[3] = 'c';
				RemoteServerSocketBuf[4] = 'c';
				RemoteServerSocketBufUsed = 5;
				}
				
			RemoteServerLinkStatus = RemServDisconnecting;
			// KEIN Wechsel des Servers!

			StartKurzTimer(&RemoteServerActionTimer);
			StartKurzTimer(&RemoteServerCheckTimer);
			
			RemoteServerReconnectTimerEnd = 3 * KurzTimerFreq; // 3 seconds
			}
		
		if (RemoteServerLinkStatus == RemServNotConnected)
			{ // wenn die Verbindung gewollt nicht genutzt wird, alle Timer rücksetzen
			StartKurzTimer(&RemoteServerActionTimer);
			StartKurzTimer(&RemoteServerCheckTimer);
			StartLangTimer(&RemoteServerErrorMessageDelay);
			}
			
		} // if Modus != ModRuhe || !RemoteServerActive || Status == RemServSendStop
		
	// Verbindung aufbauen wenn sinnvoll
	// ---------------------------------
	if (Modus == ModRuhe 
		&& RemoteServerActive 
		&& RemoteServerLinkStatus == RemServNotConnected 
		&& KurzTimerVal(&RemoteServerActionTimer) >= RemoteServerReconnectTimerEnd)
		{
		// because of two return statements below handling errors the printout of the 
		// diagnostic message is done here.
		if (!RemoteServerErrorMessageSent && LangTimerVal(&RemoteServerErrorMessageDelay) >= 10 * LangTimerMinuteFaktor)
			{
			Diagnoseausgabe_P(ISTR(KeinTeilnehmerServerErreichbar, LokaleSprache), 1);	
			RemoteServerErrorMessageSent = true;
			}
		
		long RemoteServerIP;
	
		// TODO umstellen auf eine allgemeinere "Server-Verbinden-Funktion"
		
		RemoteServerIP = strtoip(TeilnehmerServerAdresse[RemoteServerAddressIndex]);	// Annahme: eine IP-Adresse angegeben

		if (RemoteServerIP == 0) // ist es doch eine Hostname?
			RemoteServerIP = DNS_ResolveName(TeilnehmerServerAdresse[RemoteServerAddressIndex]); 
		
		if (RemoteServerIP == -1)
			{
			if (ProtokollAktivFuer(NurFehler))
				{
				ProtokollierenITelex_P(PSTR("! Remote Server Hostname "));
				Protokollieren(TeilnehmerServerAdresse[RemoteServerAddressIndex]);
				Protokollieren_P(PSTR(" unbekannt\r\n"));
				}
			RemoteServerLinkSocketHandle = NO_SOCKET_USED;
			RemoteServerUseAnotherOne();
			StartKurzTimer(&RemoteServerActionTimer); 
			return; // nichts mehr machbar hier.
			}
			
		RemoteServerLinkSocketHandle = Connect2IP(RemoteServerIP, RemoteServerPort); 
		
		StartKurzTimer(&RemoteServerActionTimer); // erst nach dem Connect starten, damit der 5 sekunden-Timeout nicht mit gemessen wird.
		
		if (RemoteServerLinkSocketHandle == SOCKET_ERROR)
			{ // Verbindung konnte nicht aufgebaut werden
			if (ProtokollAktivFuer(NurFehler))
				{
				ProtokollierenITelex_P(PSTR("! Remote Server "));
				Protokollieren(TeilnehmerServerAdresse[RemoteServerAddressIndex]);
				Protokollieren_P(PSTR(" konnte nicht geoeffnet werden\r\n"));
				}
			RemoteServerLinkSocketHandle = NO_SOCKET_USED;
			RemoteServerUseAnotherOne();
			return; // nichts mehr machbar hier.
			}
			
		if (ProtokollAktivFuer(AblaufInfo))
			{
			ProtokollierenITelex_P(PSTR("Remote Server "));
			Protokollieren(TeilnehmerServerAdresse[RemoteServerAddressIndex]);
			Protokollieren_P(PSTR(" erfolgreich geoeffnet\r\n"));
			}
		
		RemoteServerSocketBuf[0] = ITELEXC_REMOTE_CONNECT;
		RemoteServerSocketBuf[1] = 0x06; // 4 Byte eigene Nummer und 2 Byte Geheimzahl
		*((uint32_t *)(RemoteServerSocketBuf+2)) = NetzRufnummer; 
		*((uint16_t *)(RemoteServerSocketBuf+6)) = Geheimzahl;
		RemoteServerSocketBufUsed = 8;

		RemoteServerLinkStatus = RemServStarting;

		StartKurzTimer(&RemoteServerCheckTimer);
		RemoteServerReconnectTimerEnd = 3 * KurzTimerFreq; 
			// bei erfolgreichem Verbinden sollte der nächste Wiederaufbau nicht so schnell nötig sein.
		
		} // if (ModRuhe && RemoteServerActive && RemServNotConnected && RemoteServerActionTimer >= RemoteServerReconnectTimerEnd)
	
	// Lebenszeichen
	// -------------
	if (RemoteServerLinkStatus == RemServConnected 
		&& RemoteServerSocketBufUsed == 0 
		&& KurzTimerVal(&RemoteServerActionTimer) >= 15 * KurzTimerFreq)
		{
		RemoteServerSocketBuf[0] = ITELEXC_NULL;
		RemoteServerSocketBuf[1] = 0; // keine Daten
		RemoteServerSocketBufUsed = 2;
		}
	
	// Daten senden bei Bedarf
	// -----------------------
	RemoteServerSendBufferIfNotEmpty();
	
	// Socket aktiv schließen wenn Ruhe eingekehrt
	// -------------------------------------------
	if (RemoteServerLinkStatus == RemServDisconnecting && KurzTimerVal(&RemoteServerActionTimer) > 3 * KurzTimerFreq)
		{
		CloseTCPSocket(RemoteServerLinkSocketHandle);
		RemoteServerLinkSocketHandle = NO_SOCKET_USED;
		RemoteServerLinkStatus = RemServNotConnected;
		StartKurzTimer(&RemoteServerActionTimer);
		}

	} // RemoteServerBearbeiten()

/** @} */

#endif /* ITELEX_ANSCHLUSS */
