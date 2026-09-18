/*! \file FrameScanner.h \brief Framing of the data received on the i-Telex socket */
/***************************************************************************
 *            FrameScanner.h
 *
 ****************************************************************************/
///	\ingroup software
///	\defgroup FrameScanner Framing of the data received on the i-Telex socket
///	\code #include "FrameScanner.h" \endcode
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

#ifndef _FRAMESCANNER_H_
#define _FRAMESCANNER_H_

#include <inttypes.h>
#include <stdbool.h>

#include "BaudotCode.h"


// Die Datem auf dem iTelex-Port haben folgende Struktur:
// - ASCII-Zeichen einschl. WR (CR) und ZL (LF) werden "pur" übertragen.
// - Ansonsten werden Datenblöcke übertragen, die stets aus folgenden Teilen bestehen:
//   * ein Byte Kommandocode (siehe die folgenden Konstanten mit ITELEXC_*)
//   * ein Byte Länge _folgender_ Daten (kann 0 sein).
//   * zugehörige Daten

//! Nur Konstanten-Definitionen.
enum { 
	ITELEXC_NULL = 0x00, //!< Füllzeichen
	ITELEXC_DURCHWAHL = 0x01, //!< Startzeichen, Datenblock enthält ein Byte Durchwahl 
	ITELEXC_BAUDOT_DATA = 0x02, //!< Datenblock mit puren Baudot-Codes
	ITELEXC_ENDE = 0x03, //!< Beabsichtigter Verbindungsabbau.
	ITELEXC_STOP = 0x04, //!< Es können noch Daten angehängt werden. Ursache: Besetzt oder Störung
	// \005 freigehalten für ^E = WerDa.
	ITELEXC_QUITT = 0x06, //!< Meldet Empfangsbereitschaft und Anzahl bereits verarbeiteter Zeichen.
	ITELEXC_VERSION = 0x07, 
		//!< Version der Kommunikation. Originate schlägt vor, Answer bestätigt.
		//!< Erst wenn andere Seite mit gleicher Nummer antwortet, ist Protokollversion abgestimmt.
	ITELEXC_SELBSTANRUF = 0x08, //!< Kennung für einen testweisen Selbst-Anruf.
	ITELEXC_FERNKONFIG = 0x09, 
		//!< Telegramm für Änderungen an Teilnehmer-Einstellungen aus der Ferne.
		//!< Inhalt: 1 Byte Länge (PIN, Kennung, Daten), 2 Byte PIN der Gegenstelle, 1 Byte Kennung ITELEXC_FKK_xxx, x Byte Daten.
		//!< Wenn Daten ein String ist, wird dieser mit abschließendem \\0 übertragen.
	} ;


//! Substitute characters the ASCII (telnet) protocol uses for the two
//! telex control codes that have no printable ASCII equivalent.
enum {
	AsciiProtZeichenWerDa = '@',
	//!< Ersatz-Zeichen für Werda im Socket-ASCII-Modus.

	AsciiProtZeichenKlingel = '%',
	//!< Ersatz-Zeichen für Klingel im Socket-ASCII-Modus.
	} ;


//! \brief What the socket is currently understood to be carrying.
//!
//! The firmware's own \c TiTelexSocketProtokoll has two further members for
//! the mail sockets, which never reach this module. The three members here
//! carry the same numeric values, so that the caller can hand its state
//! straight over; \c iTelex.c asserts that at compile time. A value from
//! outside this range is treated as "not settled yet", which is what the
//! receive path did with it before.
typedef enum {
	FrameProtocolUnknown = 0,	//!< ehem. ProtUnknown — nothing decided yet.
	FrameProtocolITelex = 1,	//!< ehem. iTelexProt — the block protocol.
	FrameProtocolAscii = 2,		//!< ehem. Ascii — plain characters, i.e. telnet.
	} TFrameProtocol;


//! \brief The kind of frame found at the cursor.
//!
//! \ref FrameText is a single character; every other member but
//! \ref FrameFiller is a block of a command code, a length byte and a
//! payload.
typedef enum {
	FrameText = 0,		//!< A character for the printer.
	FrameFiller,		//!< #ITELEXC_NULL, which carries no block.
	FrameExtension,		//!< #ITELEXC_DURCHWAHL
	FrameBaudotData,	//!< #ITELEXC_BAUDOT_DATA
	FrameDisconnect,	//!< #ITELEXC_STOP or #ITELEXC_ENDE
	FrameAcknowledge,	//!< #ITELEXC_QUITT
	FrameVersion,		//!< #ITELEXC_VERSION
	FrameSelfCall,		//!< #ITELEXC_SELBSTANRUF
	FrameRemoteConfig,	//!< #ITELEXC_FERNKONFIG
	FrameUnknown,		//!< A command code this firmware does not know.
	} TFrameKind;


//! \brief One frame, as \ref ScanFrame read it out of the receive buffer.
typedef struct {
	TFrameKind Kind;
		//!< What was found at the cursor.

	TFrameProtocol Protocol;
		//!< The protocol state after this frame. Equal to the state handed
		//!< to \ref ScanFrame wherever the frame does not settle it.

	uint16_t Length;
		//!< How far the cursor advances. This can reach past the end of the
		//!< received data for an incomplete frame; see \ref Complete.

	uint8_t PayloadLength;
		//!< Payload bytes the caller may read. This is the length byte as it
		//!< arrived, except for a short \ref FrameDisconnect, where it is cut
		//!< down to what arrived — old i-Telex versions sent a data block
		//!< shorter than the length they declared.

	bool Complete;
		//!< Whether the whole declared block has arrived.

	char Text;
		//!< \ref FrameText only: the character to print, after the two
		//!< ASCII-protocol substitutions.
	} TFrame;


//! \brief Whether a code is one an ASCII peer may send outside the printable range.
//!
//! Used only to tell a control code that belongs in the text from one that
//! starts a block, and only once the socket is known to carry ASCII.
//!
//! \param c The code to classify.
//! \retval true \a c is a printable character, a line ending, or one of the
//!              control codes the ASCII protocol carries.
static inline bool IsCommonAsciiControl(char c)
	{
	return c == '\r' || c == '\n' || c == CodeChrKlingel || c == CodeChrWerDa 
		|| c == '\005' /*ENQ = Werda*/
		|| c == '\010' /*Backspace*/ || c == '\011' /*Tab*/
		|| c == '\033' /*ESC*/
		|| c == CodeChrBuUm || c == CodeChrZiUm || (c >= ' ' && c <= '~') || c >= 0xa0;
	}


//! \brief Reads the frame that starts at \a Cursor.
//!
//! Pure: it reads \a Buffer and decides nothing about sockets, modes or
//! output buffers, which stay with the caller. A frame is always described,
//! never rejected — an unusable one comes back with \ref TFrame::Complete
//! clear, and it is the caller that decides whether to act on it, to wait
//! for more data, or to skip it.
//!
//! \param[in] Buffer The receive buffer.
//! \param[in] Used Number of bytes received into \a Buffer. Must be > \a Cursor.
//! \param[in] Cursor Index of the first byte of the frame.
//! \param[in] Protocol What the socket is understood to be carrying.
//! \param[out] Frame The frame found. Every member is written.
//!
//! \note For a block frame the length byte is read even when it has not
//!       arrived yet. The firmware's receive buffer is declared four bytes
//!       longer than it is ever filled, which is what keeps that read inside
//!       the array; a caller with a tighter buffer has to allow for it.
static inline void ScanFrame(const char *Buffer, uint16_t Used, uint16_t Cursor,
	TFrameProtocol Protocol, TFrame *Frame)
	__attribute__((always_inline));
	// Forced inline: this module is a header rather than a translation unit
	// because letting avr-gcc keep ScanFrame out of line costs 186 bytes on
	// the Light image, which does not have them to spare. It has one call
	// site in the firmware; a second would duplicate the code.

static inline void ScanFrame(const char *Buffer, uint16_t Used, uint16_t Cursor,
	TFrameProtocol Protocol, TFrame *Frame)
	{
	char c = Buffer[Cursor];

	// The members that hold for a character, which is the shortest frame
	// there is. The block branch below overwrites what it needs.
	Frame->Kind = FrameText;
	Frame->Protocol = Protocol;
	Frame->Length = 1;
	Frame->PayloadLength = 0;
	Frame->Complete = true;
	Frame->Text = c;

	// A printable character or a line ending is text whatever the socket is
	// carrying. Any other code the ASCII protocol uses is text only once the
	// socket is known to carry ASCII, and only where the next code is text
	// as well — otherwise it is read as the start of a block. That is what
	// keeps a lone \\010 from being mistaken for #ITELEXC_SELBSTANRUF, and it
	// is also why, on an ASCII connection, the command codes that share a
	// value with one of those control codes cannot be received at all.
	// ID#246 ID#344
	if (c == '\r' || c == '\n' || (c >= ' ' && c <= '~')
		|| (IsCommonAsciiControl(c) && Protocol == FrameProtocolAscii
			&& (Cursor == Used-1 || IsCommonAsciiControl(Buffer[Cursor+1]))))
		{
		Frame->Protocol = FrameProtocolAscii;
		if (c == AsciiProtZeichenKlingel)
			Frame->Text = CodeChrKlingel;
		else if (c == AsciiProtZeichenWerDa) 
			Frame->Text = CodeChrWerDa;
		return;
		}

	// im Folgenden KEIN switch: eine Kette von Vergleichen ist auf dem AVR
	// kleiner als die Sprungtabelle, die der Compiler sonst anlegt.
	if (c == ITELEXC_NULL)
		{
		Frame->Kind = FrameFiller;
		return;
		}
	else if (c == ITELEXC_DURCHWAHL)
		{
		Frame->Kind = FrameExtension;
		Frame->Protocol = FrameProtocolITelex;
		}
	else if (c == ITELEXC_BAUDOT_DATA)
		{
		Frame->Kind = FrameBaudotData;
		Frame->Protocol = FrameProtocolITelex;
		}
	else if (c == ITELEXC_STOP || c == ITELEXC_ENDE)
		{
		Frame->Kind = FrameDisconnect;
		Frame->Protocol = FrameProtocolITelex;
		}
	else if (c == ITELEXC_QUITT)
		{
		Frame->Kind = FrameAcknowledge;
		Frame->Protocol = FrameProtocolITelex;
		}
	else if (c == ITELEXC_VERSION)
		{
		Frame->Kind = FrameVersion;
		Frame->Protocol = FrameProtocolITelex;
		}
	else if (c == ITELEXC_SELBSTANRUF)
		{
		Frame->Kind = FrameSelfCall;
		Frame->Protocol = FrameProtocolITelex;
		}
	else if (c == ITELEXC_FERNKONFIG)
		{
		Frame->Kind = FrameRemoteConfig;
		// Leaves the protocol state alone, unlike every other block:
		// remote configuration arrives before a connection has settled
		// what it is.
		}
	else
		{ // ID#245 ID#313 ID#346
		Frame->Kind = FrameUnknown;
		if (Protocol != FrameProtocolITelex)
			// Not known to be the block protocol, so there is no length
			// byte to trust: skip this one code and look again.
			return;
		}

	// Every frame that reaches here is a block: command code, length byte,
	// payload. The length byte is read whether or not it has arrived; see
	// the note on ScanFrame.
	uint8_t Declared = Buffer[Cursor+1];

	Frame->PayloadLength = Declared;
	Frame->Length = 2 + Declared;
	Frame->Complete = Cursor + 2 + Declared <= Used;

	if (Frame->Kind == FrameDisconnect && !Frame->Complete)
		{ // dies ist implementiert, weil alte i-Telex-Versionen einen zu kurzen Datenblock sendeten.
		// Note the wrap: where not even the length byte has arrived, the
		// subtraction is one short of zero and the payload length becomes
		// 255, which sends the cursor far past the received data. Recorded
		// here rather than corrected, because correcting it is a change of
		// behaviour and not a refactoring.
		Frame->PayloadLength = Used - Cursor - 2;
		Frame->Length = 2 + Frame->PayloadLength;
		}
	}

#endif /* _FRAMESCANNER_H_ */

//@}
