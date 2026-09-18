#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>

#include "hardware/timer0/timer0.h"
#include "FifoPuffer.h"
#include "iTelex/iTelex.h"

enum {
	SERIAL_BAUD = 50,
	SERIAL_TEST_CODE = 0x15
};

extern void ITelexSimTestSerialInit(uint16_t Baud);
extern void itelex_timerEvent(void);

static void serial_timer_event(void)
{
	PORTB |= _BV(PB3);
	PINB = _BV(PB0);
	itelex_timerEvent();
	PORTB &= (uint8_t)~_BV(PB3);
}

int main(void)
{
	DDRB |= _BV(PB0) | _BV(PB1) | _BV(PB2) | _BV(PB3);
	PORTB |= _BV(PB1);

	ITelexSimTestSerialInit(SERIAL_BAUD);
#ifdef SERIAL_TEST_TRANSMIT
	PufferSpeich(&SendePuffer, SERIAL_TEST_CODE);
	PufferSpeich(&SendePuffer, SERIAL_TEST_CODE);
#endif

	timer0_init(900);
	if (!timer0_RegisterCallbackFunction(serial_timer_event))
		return 1;
	PORTB |= _BV(PB2);
	sei();

	for (;;)
		;
}
