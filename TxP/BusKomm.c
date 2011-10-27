#include <inttypes.h>
#include <avr/wdt.h>

#include <bool.h>
#include "TwiEvents.h"

#include "TxP2-Defs.h"
#include "BusKomm.h"
//#include "MsTimer.h"
#include "BaudotCode.h"
#include "SeriellUmsetz.h"


volatile uint8_t Status;


// HACK (gilt nur für TW39):
//#define ROT_EIN SET_BIT(PORTD,1);
//#define ROT_AUS CLR_BIT(PORTD,1);


#define EMPF_PUFFER_GROESSE 20

// Variablen für Datenaustausch über I²C
// -------------------------------------

volatile TBusAuftrag BusAuftrag; //
volatile TBusErgebnis BusErgebnis; // Besetzt nur nach BedSenden 
uint8_t BusEigenAdresse; // als I²C-Adresse, also << 1. Bei Mehrfach-Adressen nur Basisadresse
uint8_t BusEigenAdrMehrfach; // muss Potenz von 2 sein, Standard = 1
volatile uint8_t BusAnrufSubAdresse; // tatsächlich als Adresse verwendete Nummer 
volatile uint8_t BusVerbPartner; // als I²C-Adresse, also << 1
volatile uint8_t BusSendeDaten;
volatile uint8_t BusEmpfPuffer[EMPF_PUFFER_GROESSE];
volatile uint8_t BusEmpfPufferSchreibPos;
volatile uint8_t BusEmpfPufferLesePos;
static volatile uint8_t BusEmpfFremdStatus;
volatile bool BusFrei;
volatile bool BusSlaveSend; // für Debugging-Zwecke
volatile uint8_t BusKollisionZaehler; // nur zum Testen / Statistik
volatile bool BusEmpfMark;
volatile bool BusEmpfMarkwechsel;
volatile uint16_t TwiIsrCount;

// Makros für Debugging
// --------------------
// ggf mit Aktionen füllen

#define DEBUG_BUSTRANSFER_FEHLER
#define DEBUG_BUSTRANSFER_INIT 				
#define DEBUG_BUSTRANSFER_INITEND
#define DEBUG_BUSTRANSFER_START
#define DEBUG_BUSTRANSFER_KOLLISION
#define DEBUG_BUSTRANSFER_FERTIG			
#define DEBUG_BUSTRANSFER_EMPFANGSTART
#define DEBUG_BUSTRANSFER_EMPFANGFERTIG


void CLR_BIT_Status(uint8_t BitNr)
	{
	uint8_t SregAlt = SREG;
	cli();
	CLR_BIT(Status, BitNr);
	SREG = SregAlt;
	}
	
	
void SET_BIT_Status(uint8_t BitNr)
	{
	uint8_t SregAlt = SREG;
	cli();
	SET_BIT(Status, BitNr);
	SREG = SregAlt;
	}
	
	
void FehlerStop(int Nummer);


static void EmpfByteSpeichern(uint8_t RecData)
	{
	switch (RecData)
		{
		case BusLebenszeichen:
			break; // wird ignoriert

		case BusKdoSpaceWdh:
			// TODO fehlenden ersten Wechsel melden
		case BusKdoSpace:
			BusEmpfMarkwechsel = true;
			BusEmpfMark = false;
			break;

		case BusKdoMarkWdh:
			// TODO fehlenden ersten Wechsel melden
		case BusKdoMark:
			BusEmpfMarkwechsel = true;
			BusEmpfMark = true;
			break;

		default: // alles andere in den Puffer
			BusEmpfPuffer[BusEmpfPufferSchreibPos++] = RecData;
			if (BusEmpfPufferSchreibPos >= EMPF_PUFFER_GROESSE)
				{
				if (BusEmpfPufferLesePos == 0) // Es würde ein Überlauf entstehen, also zurück...
					BusEmpfPufferSchreibPos = EMPF_PUFFER_GROESSE - 1;
				else // kein Überlauf...
					BusEmpfPufferSchreibPos = 0;
				}
			else
				{ 
				if (BusEmpfPufferSchreibPos == BusEmpfPufferLesePos) // Es würde ein Überlauf entstehen, also zurück...
					BusEmpfPufferSchreibPos--;
				}
			SET_BIT(Status, StatBit_BusKdoEmpfangen); // hier nicht SET_BIT_Status, da sonst das Interrupt-Flag wieder gesetzt wird!
			CLR_BIT(Status, StatBit_Frei); // hier nicht SET_BIT_Status, da sonst das Interrupt-Flag wieder gesetzt wird!
			break;

		} // switch RecData

	} // EmpfByteSpeichern
	

ISR(TWI_vect)
	{
	uint8_t NewStat, RecData;

	NewStat = (0<<TWINT) | (1<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (1<<TWIE);

#ifdef TWI_DEBUG
	void DebSp(uint8_t x);
	DebSp(TWSR & TwiEv_Mask);
#endif //TWI_DEBUG
	
	TwiIsrCount++;
	
	switch (TWSR & TwiEv_Mask)
		{
		// Allgemein
		// ---------
        case TwiEv_BusError			:
        	DEBUG_BUSTRANSFER_FEHLER;
			SET_BIT(NewStat, TWSTO);
			BusFrei = true;
			break;

        case TwiEv_MasterStart		:
        	DEBUG_BUSTRANSFER_START;
			if (BusAuftrag == Senden)
				TWDR = BusVerbPartner & ~1; // Bit 0 = 0 -> Write
			else
				TWDR = BusVerbPartner | 1; // Bit 0 = 1 -> Read
#ifdef TWI_DEBUG
			DebSp(BusVerbPartner);
#endif //TWI_DEBUG
			BusFrei = false;
			break;

        case TwiEv_MasterRepStart	: // kann nur bei Funktion BedSenden auftreten
			TWDR = BusVerbPartner & ~1; // Bit 0 = 0 -> Write
#ifdef TWI_DEBUG
			DebSp(BusVerbPartner);
#endif //TWI_DEBUG
			BusFrei = false;
			break;

		// Master-Transmit
		// ---------------
        case TwiEv_MT_AddrACK		:
			TWDR = BusSendeDaten;
#ifdef TWI_DEBUG
			DebSp(BusSendeDaten);
#endif //TWI_DEBUG
			break;

        case TwiEv_MT_AddrNACK		:
			BusAuftrag = Fertig;
			BusErgebnis = KeineAntwort; // wiederholung hat keinen Sinn
			SET_BIT(NewStat, TWSTO);
			BusFrei = true;
			DEBUG_BUSTRANSFER_FERTIG;
			break;

        case TwiEv_MT_DataACK		:
			BusAuftrag = Fertig;
			BusErgebnis = Ok; // Zeichen der erfolgreichen Erledigung
			SET_BIT(NewStat, TWSTO); // weil nur ein Byte zu übertragen ist
			BusFrei = true;
			DEBUG_BUSTRANSFER_FERTIG;
			break;

        case TwiEv_MT_DataNACK		:
			BusAuftrag = Fertig;
			BusErgebnis = Ok; // Zeichen der erfolgreichen Erledigung (das vorherige Byte wurde angenommen!)
			SET_BIT(NewStat, TWSTO); 
			BusFrei = true;
			DEBUG_BUSTRANSFER_FERTIG;
			break;

        case TwiEv_MT_ArbitrLost	:
			SET_BIT(NewStat, TWSTA); // gleich nochmal probieren
				// Bei BedSenden wird auf jeden Fall zuerst wieder das Status-Lesen begonnen
			BusKollisionZaehler++;
			DEBUG_BUSTRANSFER_KOLLISION;
			break;

		// Master-Receive
		// --------------
        case TwiEv_MR_AddrACK		:
			CLR_BIT(NewStat, TWEA); // damit nach erstem Datenbyte NACK gesendet wird
			break;

        case TwiEv_MR_AddrNACK		:
			BusAuftrag = Fertig;
			BusErgebnis = KeineAntwort; // wiederholung hat keinen Sinn
			SET_BIT(NewStat, TWSTO);
			BusFrei = true;
			DEBUG_BUSTRANSFER_FERTIG;
			break;

        case TwiEv_MR_DataACK		:
        case TwiEv_MR_DataNACK		:
			BusEmpfFremdStatus = TWDR;
#ifdef TWI_DEBUG
			DebSp(BusEmpfFremdStatus);
#endif //TWI_DEBUG
			if (BusAuftrag == BedSenden) // kommt nur bei Verbindungsaufnahme vor...
				if (BIT_IS_SET(BusEmpfFremdStatus, StatBit_Frei)) 
					{ // Empfänger ist frei
					SET_BIT(NewStat, TWSTA); // repeated Start
					}
				else
					{
					BusAuftrag = Fertig;
					BusErgebnis = Besetzt; // Zeichen der nicht erfolgreichen Erledigung
					SET_BIT(NewStat, TWSTO);
					BusFrei = true;
					DEBUG_BUSTRANSFER_FERTIG;
					}
			else // !BedSenden
				{
				BusAuftrag = Fertig;
				BusErgebnis = Ok; // Zeichen der erfolgreichen Erledigung
				SET_BIT(NewStat, TWSTO);
				BusFrei = true;
				DEBUG_BUSTRANSFER_FERTIG;
				}
			break;

		// Slave-Receive
		// -------------
        case TwiEv_SR_AddrACK_AL	: // after lost arbitration 
			BusKollisionZaehler++;
			// kein break
			
		case TwiEv_SR_AddrACK		:
			wdt_reset();
        	CLR_BIT(NewStat, TWEA); // damit nach erstem Datenbyte NACK gesendet wird 
			BusFrei = false;
			DEBUG_BUSTRANSFER_EMPFANGSTART;
			BusAnrufSubAdresse = (TWDR >> 1) & (BusEigenAdrMehrfach - 1);
			break;

        case TwiEv_SR_DataACK		:
			RecData = TWDR;
#ifdef TWI_DEBUG
			DebSp(RecData);
#endif //TWI_DEBUG
			EmpfByteSpeichern(RecData);
			CLR_BIT(NewStat, TWEA); // damit weiter NACK gesendet wird 
			break;

        case TwiEv_SR_DataNACK		:
			RecData = TWDR;
#ifdef TWI_DEBUG
			DebSp(RecData);
#endif //TWI_DEBUG
			EmpfByteSpeichern(RecData);
			BusFrei = true;
			DEBUG_BUSTRANSFER_FERTIG;
			if (BusAuftrag == Lesen || BusAuftrag == Senden || BusAuftrag == BedSenden)
				SET_BIT(NewStat, TWSTA);
			break;

        case TwiEv_SR_Stop			:
			BusFrei = true;
			DEBUG_BUSTRANSFER_FERTIG;
			if (BusAuftrag == Lesen || BusAuftrag == Senden || BusAuftrag == BedSenden)
				SET_BIT(NewStat, TWSTA);
			break;

		// Slave-Transmit
		// --------------
        case TwiEv_ST_AddrACK_AL	:
			BusKollisionZaehler++;
			// kein break
			
        case TwiEv_ST_AddrACK		:
			DEBUG_BUSTRANSFER_EMPFANGSTART;
			// weiter mit TWDR = Status...
			       	
        case TwiEv_ST_DataACK		:
			TWDR = Status;
#ifdef TWI_DEBUG
			DebSp(Status);
#endif //TWI_DEBUG
			BusFrei = false;
			BusSlaveSend = true;
			break;

        case TwiEv_ST_DataNACK		: // Beendigung durch Master
        case TwiEv_ST_DataLast 		: // Beendigung durch Slave
			BusFrei = true;
			DEBUG_BUSTRANSFER_EMPFANGFERTIG;
			if (BusAuftrag == Lesen || BusAuftrag == Senden || BusAuftrag == BedSenden)
				SET_BIT(NewStat, TWSTA);
			break;

		default:
			DEBUG_BUSTRANSFER_FEHLER;
#ifdef FALSCHKDO_FEHLERSTOP
			FehlerStop(2);
#endif
			break;

		}

	TWCR = NewStat | (1<<TWINT);
	}
	

void BusWarteFertig()
	{
	while (BusAuftrag != Nichts && BusAuftrag != Fertig)
		; // warten, bis eigene Kommandos bearbeitet sind. kein wdt_reset, da es schnell gehen sollte.
	}
	

void BusSenden(uint8_t Kdo)
	{ // an BusVerbPartner 
	DEBUG_BUSTRANSFER_INIT;

	BusWarteFertig();

	uint8_t sreg_alt = SREG;
	cli();
	
	BusSendeDaten = Kdo;
	
	if (Kdo > BusKdoVerbAufnahme)
		BusAuftrag = Senden;
	else
		BusAuftrag = BedSenden;

	if (BusFrei)
		{
		while (BIT_IS_SET(TWCR, TWSTO))
			;
		SET_BIT(TWCR, TWSTA);
		}
	else
		DEBUG_BUSTRANSFER_KOLLISION;
		
	SREG = sreg_alt; // setzt ales Interrupt-Enable zurück

	DEBUG_BUSTRANSFER_INITEND;

	}
	
	
int16_t GetStatus(uint8_t Adr)
	// liefert Status >= 0 oder -1 für keine Verbindung
	{
	DEBUG_BUSTRANSFER_INIT;
		
	BusWarteFertig();
	
	uint8_t AltVerbPartner = BusVerbPartner;
	BusVerbPartner = Adr; // vorübergehend
	BusAuftrag = Lesen;
	if (BusFrei)
		{
		while (BIT_IS_SET(TWCR, TWSTO))
			;
		SET_BIT(TWCR, TWSTA);
		}
	else
		DEBUG_BUSTRANSFER_KOLLISION;

	BusWarteFertig();

	BusVerbPartner = AltVerbPartner;

	DEBUG_BUSTRANSFER_INITEND;
		
	if (BusErgebnis != Ok)
		return -1;
	else
		return BusEmpfFremdStatus; // wurde von der ISR gefüllt
	}
	

bool BusEigenAdressePruefenUndSetzen(uint8_t neu)
	{
	if (neu >= BusAdrMin
		&& neu <= BusAdrMax
		&& !BIT_IS_SET(neu, 0)
		&& GetStatus(neu) < 0)
		{ // TODO BusEigenAdrMehrfach prüfen!
		cli();
		TWAR = neu;
		SET_BIT(TWCR, TWEA);
		sei();
		BusEigenAdresse = neu;
		return true;
		}
	else
		{
		cli();
		TWAR = 0;
		CLR_BIT(TWCR, TWEA);
		sei();
		BusEigenAdresse = BusAdrUngueltig;
		return false;
		}
	}


void TwiInit()
// vorher muss BusEigenAdresse und BusEigenAdrMehrfach gesetzt sein!
	{
	// TWI initilaisieren
	BusFrei = true;
	BusAuftrag = Nichts;

#define TWI_PSBITS 1
#define TWI_PRESCALER (1<<(2*TWI_PSBITS))

	TWSR = TWI_PSBITS;
	TWBR = ((F_CPU / BusFrequenz) - 16) / (2 * TWI_PRESCALER);
	TWAR = BusEigenAdresse & 0xFE;
	TWCR = (1<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (0<<TWSTO) | (1<<TWEN) | (0<<TWIE);
#ifdef TWAMR
	TWAMR = (BusEigenAdrMehrfach - 1) << 1; // muss Potenz von 2 sein, Standard = 1
#else
	if (BusEigenAdrMehrfach != 1)
		FehlerStop(12);
#endif //def TWAMR
	BusKollisionZaehler = 0;
	BusEmpfPufferLesePos = 0;
	BusEmpfPufferSchreibPos = 0;
	TwiIsrCount = 0; 
	}


/*
	
void WarteSchlussQuittung(uint16_t MaxTimer)
	{
	TMsTimer Timer;
	
	StartTimer(&Timer);
	while (true)
		{
		uint8_t Code;

		if (GetEmpfByte(&Code)) 
			{
			if (Code == BusQuittSchluss)
				break;
			}
		if (TimerVal(&Timer) > MaxTimer) 
			break;
		wdt_reset(); // nach Schluss-Kommando keine Lebenszeichen mehr...
		}
	}
*/
	

uint8_t WahlZuAdresse(uint8_t Wahl, uint8_t AnzZiffern)
	// 0 - 9 --> 110, 101-109
	// 00-99 --> 100, 1 - 99
	{
	if (AnzZiffern == 1)
		return ((Wahl == 0) ? 110 : 100 + Wahl) << 1;
	else
		return ((Wahl == 0) ? 100 : Wahl) << 1;
	}


uint8_t AdresseZuWahl(uint8_t Adresse, uint8_t *AnzZiffern)
	// 0 - 9 <-- 110, 101-109
	// 00-99 <-- 100, 1 - 99
	{
	Adresse = Adresse >> 1;
	if (Adresse > 100)
		{
		*AnzZiffern = 1;
		return (Adresse == 110) ? 0 : (Adresse - 100);
		}
	else
		{
		*AnzZiffern = 2;
		return (Adresse == 100) ? 0 : Adresse;
		}
	}


/*	
static TMsTimer LebenszTimer;


void SendeLebenszeichen()
	{
	if (BusVerbPartner == 0)
		StartTimer(&LebenszTimer);
	else if (TimerVal(&LebenszTimer) > 674 && BusFrei && (BusAuftrag == Nichts || BusAuftrag == Fertig))
		{
		BusSenden(BusLebenszeichen);
		StartTimer(&LebenszTimer);
		}
	}
*/
	

bool GetEmpfByte(uint8_t *Code)
	//!< holt aus dem Empfangspuffer den nächsten Code
	//!< \retval false, wenn Empfangspuffer leer ist.
	{
	if (BusEmpfPufferLesePos == BusEmpfPufferSchreibPos)
		return false;
	*Code = BusEmpfPuffer[BusEmpfPufferLesePos++];
	if (BusEmpfPufferLesePos >= EMPF_PUFFER_GROESSE)
		BusEmpfPufferLesePos = 0;
	if (BusEmpfPufferLesePos == BusEmpfPufferSchreibPos)
		CLR_BIT_Status(StatBit_BusKdoEmpfangen);
	return true;
	}
	
	
bool EmpfPufferLeer()
	{
	return BusEmpfPufferLesePos == BusEmpfPufferSchreibPos;
	}
	

