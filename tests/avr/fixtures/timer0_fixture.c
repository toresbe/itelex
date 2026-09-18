#include <avr/interrupt.h>
#include <avr/io.h>

#include "hardware/timer0/timer0.h"

enum { TEST_TIMER_FREQUENCY = 900 };

static void toggle_probe_pin(void)
{
	PINB = (1 << PB0);
}

int main(void)
{
	DDRB |= (1 << PB0);
	PORTB &= ~(1 << PB0);

	timer0_init(TEST_TIMER_FREQUENCY);
	if (!timer0_RegisterCallbackFunction(toggle_probe_pin))
		return 1;

	sei();
	for (;;)
		;
}
