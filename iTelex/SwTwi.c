#include <avr/io.h>
#include <avr/interrupt.h>

//#include <string.h>

#include <stdbool.h>
#include "bits.h"

#include "SwTwi.h"

#include "hardware/timer1/timer1.h"

#include "iTelex.h"


//! Zählwert für Timer1: Wenn TCNT1 > 4 * TaktViertel, dann ist ein Bit gesendet
static uint8_t TaktViertel;

//! Zählwert für Timer1.
static uint8_t Mikrosek;


static bool InWaitstate;

TSwTwiFehler SwTwiLetzterFehler;

//nicht benuztzt... static uint8_t AktByte;

static uint8_t LongTimer; 


// Ports: SDA auf PORTG3, SCL auf PORTG4

inline void SDAset0() {	SET_BIT(DDRG, 3); }
inline void SDAset1() {	CLR_BIT(DDRG, 3); }
inline bool SDAget() { return BIT_IS_SET(PING, 3); }
inline void SDAinit() { CLR_BIT(PORTG, 3); CLR_BIT(DDRG, 3); } // SDA in (normal)

inline void SCLset0() {	SET_BIT(DDRG, 4); }
inline void SCLset1() {	CLR_BIT(DDRG, 4); }
inline bool SCLget() { return BIT_IS_SET(PING, 4); }
inline void SCLinit() { CLR_BIT(PORTG, 4); CLR_BIT(DDRG, 4); } // SCL in (normal)


static uint8_t CheckLongTimer()
	{
	static int LastTimer1Val;
	int Timer1Akt = timer1_getcounter();
	if (Timer1Akt < LastTimer1Val) // es war ein Überlauf eingetreten
		LongTimer++;
	LastTimer1Val = Timer1Akt;
	return LongTimer;
	}
	

static bool SendBit(bool val, bool ExitWhenWaitstate)
// Rückgabe: true, wenn nicht verfälscht...
// Ablauf: SDA setzen, Vierteltakt, SCL 1, 2 Vierteltakte, SCL 0, Vierteltakt
	{
	// SDA setzen und prüfen
	if (!InWaitstate)
		{
		if (val)
			SDAset1();
		else
			SDAset0();

		timer1_wait(TaktViertel);

		if (val)
			{
			if (!SDAget())
				{
				SwTwiLetzterFehler = ArbitLost;
				return false;
				}
			}
		else
			{
			if (SDAget())
				{
				SwTwiLetzterFehler = StoerungSDA;
				SDAset1();
				SCLset1();
				return false;
				}
			}

		// SCL auf 1
		SCLset1();

		// 1 Vierteltakt
		LongTimer = 0;
		timer1_wait(TaktViertel);
		}

	// ist SCL oben?
	if (!SCLget())
		{
		if (ExitWhenWaitstate)
			{
			InWaitstate = true;
			if (CheckLongTimer() > 100) // 1 Sekunde
				{
				SwTwiLetzterFehler = Blockiert;
				SDAset1(); // SCL ist schon 1
				}
			return false;
			}
		else
			while (!SCLget())
				{
				if (CheckLongTimer() > 100) // 1 Sekunde
					{
					SwTwiLetzterFehler = Blockiert;
					return false;
					}
				}
		}

	InWaitstate = false;

	// 1 Vierteltakt
	timer1_wait(TaktViertel);

	// SCL auf 0
	SCLset0();

	// Vierteltakt
	timer1_wait(TaktViertel);

	if (SCLget())
		{
		SwTwiLetzterFehler = StoerungSCL;
		SCLset1();
		SDAset1();
		return false;
		}

	return true;
	}


static bool ReadBit(bool* val, bool ExitWhenWaitstate)
// Rückgabe: true, wenn bit Empfangen
// Ablauf: SDA auf 1, Vierteltakt, SCL 1, Vierteltakt, SDA testen, Vierteltakt, SCL 0, Vierteltakt
	{
	if (!InWaitstate)
		{
		// SDA freigeben
		SDAset1();

		// Vierteltakt warten
		timer1_wait(TaktViertel);

		// SCL auf 1
		SCLset1();

		// kurz warten
		timer1_wait(Mikrosek);
		
		}

	// ist SCL oben?
	if (!SCLget())
		{
		if (ExitWhenWaitstate)
			{
			InWaitstate = true;
			if (CheckLongTimer() > 100) // 1 Sekunde
				SwTwiLetzterFehler = Blockiert;
			return false;
			}
		else
			while (!SCLget())
				{
				if (CheckLongTimer() > 100) // 1 Sekunde
					{
					SwTwiLetzterFehler = Blockiert;
					return false;
					}
				}
		
		}

	InWaitstate = false;

	// Vierteltakt
	timer1_wait(TaktViertel);

	// Pegelfeststellung von SDA durch mehrfaches Prüfen
	// mindestens 5 von 6 Prüfungen müssen gleiches Ergebnis haben
	// also muss 'Pegel' mindestens AnzSamples * 4/6 haben, da falsches Sample den Pegel in andere Richtung
	// ändert
	// damit mindestens 4 Messungen gemacht werden, startet AnzSamples bei 2

	int8_t Pegel = 0;
	uint8_t AnzSamples = 2;

	// Samples nehmen, mindestens 4
	while (++AnzSamples <= 100)
		{
		timer1_wait(1);
		
		if (SDAget())
			Pegel++;
		else
			Pegel--;

		if (((Pegel > 0) ? Pegel : -Pegel) * 3 >= AnzSamples * 2)
			break; 
		}

	*val = Pegel > 0;

	// TODO if (AnzSamples > 100) Bitfehler

	if (AnzSamples < TaktViertel)
		timer1_wait(TaktViertel - AnzSamples);

	// SCL auf 0
	SCLset0();

	// Vierteltakt
	timer1_wait(TaktViertel);
	
	if (SCLget())
		{
		SwTwiLetzterFehler = StoerungSCL;
		SCLset1();
		SDAset1();
		return false;
		}

	return true;
	}


bool SwTwiStart()
// Rückgabe: true, wenn erfolgreich
// Ablauf: SDA auf 0, Vierteltakt, SDA prüfen, 3 Vierteltakte, SCL 0, 2 Vierteltakte, SCL prüfen
// TODO: Busfreiheit prüfen
	{
	// ist SCL schon auf 1?
	InWaitstate = false;
	LongTimer = 0;

	if (!SCLget())
		{ // RESTART!
		SDAset1();
		SCLset1();

		while (!SCLget())
			{
			if (CheckLongTimer() > 100) // 1 Sekunde
				{
				SwTwiLetzterFehler = Blockiert;
				return false;
				}
			}

		timer1_wait(TaktViertel);
		}

	// SDA auf 0
	SDAset0();

	timer1_wait(TaktViertel);

	// SDA prüfen
	if (SDAget())
		{
		SwTwiLetzterFehler = StoerungSDA;
		SDAset1();
		SCLset1();
		return false;
		}

	timer1_wait(TaktViertel);
	
	// SCL auf 0
	SCLset0();

	timer1_wait(2 * TaktViertel);
	
	// SCL prüfen
	if (SCLget())
		{
		SwTwiLetzterFehler = StoerungSCL;
		SCLset1();
		SDAset1();
		return false;
		}

	return true;
	}


bool SwTwiStop(bool ExitWhenWaitstate)
// Rückgabe: true, wenn erfolgreich
// Ablauf: SDA 0, SDA prüfen, Vierteltakt, SCL 1, SCL prüfen, 2 Vierteltakte, SDA 1, SDA prüfen, Vierteltakt
	{
	if (!InWaitstate)
		{
		// SDA setzen und prüfen
		if (SDAget())
			{
			SDAset0();

			timer1_wait(TaktViertel);

			if (SDAget())
				{
				SwTwiLetzterFehler = StoerungSDA;
				SDAset1();
				SCLset1();
				return false;
				}
			}
		else
			SDAset0(); // nur sicherheitshalber

		// SCL auf 1
		SCLset1();

		// 1 Vierteltakt
		timer1_wait(TaktViertel);
		}
		
	// ist SCL oben?
	if (!SCLget())
		{
		if (ExitWhenWaitstate)
			{
			InWaitstate = true;
			if (CheckLongTimer() > 100) // 1 Sekunde
				SwTwiLetzterFehler = Blockiert;
			return false;
			}
		else
			while (!SCLget())
				{
				if (CheckLongTimer() > 100) // 1 Sekunde
					{
					SwTwiLetzterFehler = Blockiert;
					return false;
					}
				}
		}

	InWaitstate = false;

	// 1 Vierteltakt
	timer1_wait(TaktViertel);

	// SDA auf 1
	SDAset1();

	// Vierteltakt
	timer1_wait(TaktViertel);

	// SDA prüfen
	while (!SDAget())
		{
		if (CheckLongTimer() > 100) // 1 Sekunde
			{
			SwTwiLetzterFehler = Blockiert;
			return false;
			}
		}

	// Vierteltakt
	timer1_wait(TaktViertel);

	return true;
	}


//! Gibt im Falle einer Störung alles wieder frei.
void SwTwiForceStop()
	{
	SCLset0();

	// 1 Halbtakt
	timer1_wait(2 * TaktViertel);
		
	SDAset1();

	// 1 Halbtakt
	timer1_wait(2 * TaktViertel);
	
	uint8_t Bits = 0;
	for (uint8_t EinsBits = 0 ; EinsBits <= 10 && Bits < 255 ; EinsBits++, Bits++)
		{
		SCLset0();
		// 1 Halbtakt
		timer1_wait(2 * TaktViertel);

		SCLset1();
		// 1 Halbtakt
		timer1_wait(2 * TaktViertel);

		if (!SDAget())
			EinsBits = 0;
		}
		
	SDAset0();

	// 1 Volltakt
	timer1_wait(4 * TaktViertel);
	
	SCLset1();

	// 1 Volltakt
	timer1_wait(4 * TaktViertel);
	
	SDAset1();

	// 1 Volltakt
	timer1_wait(4 * TaktViertel);
	} // SwTwiForceStop()
	

bool SwTwiSendByte(uint8_t x, bool* Ack, bool ExitWhenWaitstate)
	{
	if (!SendBit((x & 0x80) != 0, ExitWhenWaitstate))
		return false;
		
	for (uint8_t Mask = 0x40 ; Mask > 0 ; Mask >>= 1)
		if (!SendBit((x & Mask) != 0, false))
			return false;
	
	if (!ReadBit(Ack, false))
		return false;
		
	*Ack = !*Ack; // 0 heißt ja, alles gut...
	
	return true;
	}


bool SwTwiReadByte(uint8_t* x, bool Ack, bool ExitWhenWaitstate)
	{
	bool Bit;
	if (!ReadBit(&Bit, ExitWhenWaitstate))
		return false;
	*x = Bit ? 0x80 : 0; // wird noch geschoben...
	for (uint8_t Mask = 0x40 ; Mask > 0 ; Mask >>= 1)
		{
		if (!ReadBit(&Bit, false))
			return false;
		if (Bit)
			*x |= Mask;
		}
	
	if (!SendBit(!Ack, false)) // !Ack da: 0 heißt JA
		return false;
		
	return true;
	}
	
	
#ifdef GELOESCHT
	
void SwTwiMain()
	{
	switch (SwTwiModus)
		{
		case Wartend:
			break; // weiter warten...
			
		case Startbereit:
			SwTwiResultat = 0;
			AktByte = 0;
			
			if (BIT_IS_SET(SwTwiAdresse, 0))
				SwTwiModus = Lesen;
			else
				SwTwiModus = Schreiben;
				
			if (!Start())
				SwTwiModus = Beendet;
			else
				{
				SwTwiLetzterFehler = KeinFehler;
				bool Ack;
				SendByte(SwTwiAdresse, &Ack, false);
				if (!Ack)
					{
					Stop(false);
					SwTwiLetzterFehler = SlaveOhneAntwort;
					SwTwiModus = Beendet;
					return;
					}
				SwTwiModus = Laeuft;
				}
			break; // case Startbereit

		case Laeuft:
			if (TransferTyp == Lesen) // TODO || TransferTyp == LesenNachSchreiben)
				{
				if (AktByte < SwTwiAnzahlDaten)
					{ // noch SwTwiDaten zu lesen
					if (!ReadByte(&SwTwiDaten[AktByte], AktByte + 1 < SwTwiAnzahlDaten, true))
						{ // kann auch Waitstate sein
						if (SwTwiLetzterFehler != KeinFehler)
							SwTwiModus = Beendet;
						return;
						}
					AktByte++;
					} // if AktByte < SwTwiAnzahlDaten
				else 
					{ // alle SwTwiDaten gelesen, also Stop
					if (!Stop(true))
						{ // kann auch Waitstate sein
						if (SwTwiLetzterFehler != KeinFehler)
							SwTwiModus = Beendet;
						return;
						}
					SwTwiModus = Beendet;
					} // else AktByte >= SwTwiAnzahlDaten
				} // if TransferTyp == Lesen || TransferTyp == LesenNachSchreiben
			else
				{ // Schreiben oder SchreibenVorLesen
				if (AktByte < SwTwiAnzahlDaten)
					{ // noch SwTwiDaten zu senden
					bool Ack;
					if (!SendByte(SwTwiDaten[AktByte], &Ack, true))
						{ // kann auch Waitstate sein
						if (SwTwiLetzterFehler != KeinFehler)
							SwTwiModus = Beendet;
						return;
						}
					AktByte++;
					if (!Ack) // Abbruch durch SLAVE
						{
						SwTwiLetzterFehler = SlaveAbbruch;
						SwTwiAnzahlDaten = AktByte; // damit gleich Stop gesendet wird
						}
					} // if AktByte < SwTwiAnzahlDaten
				else 
					{ // alle SwTwiDaten geschrieben, also ...
					if (TransferTyp == Schreiben)
						{ // ... Stop
						if (!Stop(true))
							{ // kann auch Waitstate sein
							if (SwTwiLetzterFehler != KeinFehler)
								SwTwiModus = Beendet;
							return;
							}
						SwTwiModus = Beendet;
						} // if TransferTyp == Schreiben
					/* TODO else 
						{ // bleibt nur TransferTyp == SchreibenVorLesen
						// Bei Fehler im schreibvorgang auch nicht lesen
						if (SwTwiLetzterFehler == SlaveAbbruch)
							{
							Stop(true);
							SwTwiModus = Beendet;
							return;
							}
						QuittBuf[QuittBufInPos + 2] = AktByte; // Anzahl tatsächlich geschriebene Byte
						AktByte = 0;
						TransferTyp = LesenNachSchreiben;
						SET_BIT(SwTwiAdresse, 0); // ansonsten noch die alte SwTwiAdresse
						// SwTwiAnzahlDaten wurde schon gesetzt
						DatenZeiger = 4;
						if (!Start()) // hier RESTART
							{
							SwTwiModus = Beendet;
							return;
							}
						bool Ack;
						SendByte(SwTwiAdresse, &Ack, false);
						if (!Ack)
							{
							Stop(false);
							SwTwiLetzterFehler = SlaveOhneAntwort; // hä... sollte eigentlich nicht sein
							SwTwiModus = Beendet;
							return;
							}
						// TODO prüfen, ob Zwischenergebnis richtig abgelegt...
						} // else TransferTyp == SchreibLesen
					} // else AktByte >= SwTwiAnzahlDaten */
				} // else TransferTyp == Schreiben oder SchreibLesen
			break; // case Laeuft (SwTwiModus)

		case Beendet:
			// Ergebnisse abspeichern
			switch (TransferTyp)
				{
				case Schreiben:
					SwTwiResultat = AktByte; // Anzahl geschriebene Byte
					break;

				case Lesen:
					SwTwiResultat = AktByte; // Anzahl gelesene Byte
					break;

				/* TODO
				case SchreibenVorLesen:
					// kann nur bei Abbruch während des Schreibens auftreten...
					QuittBuf[QuittBufInPos + 2] = AktByte; // Anzahl tatsächlich geschriebene Byte
					QuittBuf[QuittBufInPos + 3] = 0; // Anzahl gelesene Byte = 0
					FertigVerarbeitet(3); // SwTwiAdresse, Anzahl geschrieben, Anzahl gelesen (=0)
					break;
					
				case LesenNachSchreiben:
					// QuittBuf[QuittBufPosNorm(QuittBufInPos+2)] wurde schon oben beschrieben
					QuittBuf[QuittBufInPos + 3] = AktByte; // Anzahl tatsächlich geschriebene Byte
					FertigVerarbeitet(3 + AktByte); // SwTwiAdresse, Anzahl geschrieben, Anzahl gelesen (=0)
					break;
				*/
					
				} // switch TransferTyp
				
			SwTwiModus = Wartend; // FehlerSpeichern wird dort ggf. nachgeholt...
			
			break; // case Beendet (SwTwiModus)
		} // switch (SwTwiModus)
	}


void i2cTest()
	{ // Test zumm Nachvollziehen am Debugger...
#ifdef SIM_DEBUG
	//uint8_t TestDaten[] = { 's'&0x1F, 0x20, 2, 0x0F, 0xF0 } ;
	//uint8_t TestDaten[] = { 'l'&0x1F, 0x20, 2 } ;
	uint8_t TestDaten[] = { '0', '0', ' ', '1', '1' } ;

	i2cInit();
	memcpy(KdoBuf, TestDaten, sizeof(TestDaten));
	KdoBufInPos = sizeof(TestDaten);
	while (KdoBufOutPos != KdoBufInPos)
		SwTwiMain();
#endif //def SIM_DEBUG
	}

#endif //def GELOESCHT	


void SwTwiInit()
	{
	//SwTwiModus = Wartend;
	InWaitstate = false;
	LongTimer = 0;
	SwTwiLetzterFehler = KeinFehler;
	//AktByte = 0;
	
	SDAinit();
	SCLinit();
	SDAset1();
	SCLset1();

#define TIMER1FREQ (F_CPU / 8) // ist in timer1.c - timer1_init festgelegt.

#define TwiBUSFREQ 100000 // 100 kHz

	TaktViertel = TIMER1FREQ / TwiBUSFREQ / 4; 
		// Trigger in Viertel-Taktzyklus

	Mikrosek = TIMER1FREQ / 1000000 + 2;

	SwTwiForceStop();
	
	}
	