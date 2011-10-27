#include "SeriellUmsetz.h"

#include "bits.h"

#include "MsTimer.h"

// Empfang: Umsetzung Seriell (Baudot) --> Parallel (Daten)
	
volatile uint8_t SerUmEmpfBitNr; 
	// 0 = Grundzustand, 1 = Startbit-Prüfung, 2-6 = Datenbits 1-5, 7 = Stopbit-Prüfung, 
	// 8 = Empfang beendet, Daten zur Verarbeitung bereit

volatile uint8_t SerUmEmpfDaten; 

volatile bool SerUmEmpfFehler; // Stop-Bit war nicht 1

volatile int16_t SerUmEmpfPegel; // zur Ausfilterung von Störimpulsen

// Senden: Umsetzung Parallel (Daten) --> Seriell (Baudot)

volatile uint8_t SerUmSendBitNr; 
	// 0 = Grundzustand, 1 = Sendedaten bereit, 2 = Startbit, 3-7 = Datenbits 1-5, 8 = Stopbit

volatile uint8_t SerUmSendDaten;

void SeriellUmsetzInit()
	{
	SerUmEmpfBitNr = SerUmEmpfWarte;
	SerUmSendBitNr = SerUmSendWarte;
	SerUmEmpfPegel = 128;
	}

	
TMsTimer SerUmTimerE, SerUmTimerA;
	

void SeriellUmsetzung(bool SeriellEing, bool *SeriellAusg) // und auswerten
	{
	// Code-Ausgabe
	// ------------
	switch (SerUmSendBitNr)
		{
		case 1: // Ausgabe starten, aber nur, wenn nicht gerade empfangen wird
			if (SerUmEmpfBitNr == SerUmEmpfWarte || SerUmEmpfBitNr == SerUmEmpfFertig)
				{
				*SeriellAusg = false;
				StartTimer(&SerUmTimerA);
				SerUmSendBitNr = 2;
				SerUmSendDaten <<= 3; // Bit 7 wird zuerst gesendet
				SerUmSendDaten |= ~0xF8; // damit als letztes eine 1 gesendet wird
				}
			break;

		case 2 ... 7: // Start- oder Daten-Bit läuft gerade
			if (TimerVal(&SerUmTimerA) >= 20)
				{ // Bit beendet
				DecrementTimer(&SerUmTimerA, 20);
				*SeriellAusg = BIT_IS_SET(SerUmSendDaten, 7);
				SerUmSendDaten <<= 1;
				SerUmSendBitNr++;
				}
			break;

		case 8: // Stop-Bit läuft gerade
			if (TimerVal(&SerUmTimerA) >= 30)
				{ // Stopbit beendet
				DecrementTimer(&SerUmTimerA, 30);
				SerUmSendBitNr = SerUmSendWarte; // fertig für die nächsten Daten
				*SeriellAusg = true; //XXX NEU
				}
			break;

		} // switch SerUmSendBitNr

	// Code-Interpretation
	// -------------------
	
	switch (SerUmEmpfBitNr)
		{
		case 0: // warte auf Start-Bit
		case 8: // letzer Empfang fertig, aber nicht ausgewertet, neues Zeichen überschreibt...
			// HACK: TODO warum SendBitNr == 0 ???::: if (!SeriellEing && SerUmSendBitNr == 0) // Pausenschritt
			if (!SeriellEing) // Pausenschritt
				{
				StartTimer(&SerUmTimerE);
				SerUmEmpfBitNr = 1;
				SerUmEmpfDaten = 0;
				SerUmEmpfPegel = 128; // Mittel
				SerUmEmpfFehler = false;
				}
			break;

		case 1: // im Start-Bit
			if (SeriellEing // Strom wieder da
				&& (++SerUmEmpfPegel > 150)) // zu viele 1-Impulse im Startbit --> von vorn
				SerUmEmpfBitNr = SerUmEmpfWarte;
			else if (TimerVal(&SerUmTimerE) >= 11) // Startbit gültig, Daten empfangen
				{
				SerUmEmpfBitNr = 2;
				SerUmEmpfPegel = 128;
				StartTimer(&SerUmTimerE);
				}
			break;

		case 2 ... 7 : // Datenbit oder Stopbits
			if (TimerVal(&SerUmTimerE) >= 20) // Bit beendet
				{
				if (SerUmEmpfBitNr == 7)
					{ // es war das Stopbit
					if (SeriellEing) // Strom wieder da
						{
						SerUmEmpfFehler = (SerUmEmpfPegel < 128); 
						SerUmEmpfBitNr++;
						}
					else
						{ // Strom immer noch unterbrochen
						}
					}
				else
					{ // das war ein Datenbit
					SerUmEmpfDaten <<= 1;
					if (SerUmEmpfPegel >= 128)
						SerUmEmpfDaten |= 1;
					SerUmEmpfBitNr++;
					StartTimer(&SerUmTimerE);
					SerUmEmpfPegel = 128;
					}
				}
			else if (TimerVal(&SerUmTimerE) >= 17) // die letzten 3 Milli-Sekunden auswerten
				{
				if (SeriellEing)
					{
					if (SerUmEmpfPegel < 255)
						SerUmEmpfPegel++;
					}
				else
					{
					if (SerUmEmpfPegel > 0)
						SerUmEmpfPegel--;
					}
				}
			break;

		} // switch (SerUmEmpfBitNr)
	}



