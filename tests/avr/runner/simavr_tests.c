#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avr_ioport.h"
#include "sim_avr.h"
#include "sim_elf.h"

enum {
	CPU_FREQUENCY = 16000000,
	EXPECTED_TIMER_PERIOD_CYCLES = 17920,
	TIMER_EDGE_COUNT = 9,
	SERIAL_TICKS_PER_BIT = 18,
	SERIAL_TX_EDGE_COUNT = 12,
	SERIAL_RUN_TICKS = 270,
	FIFO_SIZE = 50,
	EXPECTED_RX_CODE = 0x13,
	SRAM_ELF_OFFSET = 0x800000
};

typedef struct {
	avr_t *avr;
	avr_cycle_count_t edge_cycles[TIMER_EDGE_COUNT];
	unsigned int edge_count;
	uint32_t last_value;
	bool have_value;
} timer_probe_t;

typedef struct {
	avr_t *avr;
	bool ready;
	unsigned int tick_count;
	uint16_t receive_level_address;
	bool drive_receive;
	uint32_t last_tick_value;
	bool have_tick_value;
	uint32_t last_tx_value;
	bool have_tx_value;
	unsigned int tx_edge_count;
	unsigned int tx_edge_ticks[SERIAL_TX_EDGE_COUNT];
	uint8_t tx_edge_values[SERIAL_TX_EDGE_COUNT];
	avr_cycle_count_t busy_start;
	avr_cycle_count_t max_busy_cycles;
} serial_probe_t;

static void record_timer_edge(struct avr_irq_t *irq, uint32_t value, void *parameter)
{
	(void)irq;
	timer_probe_t *probe = parameter;
	value = value != 0;

	if (!probe->have_value) {
		probe->last_value = value;
		probe->have_value = true;
		return;
	}
	if (value == probe->last_value)
		return;

	probe->last_value = value;
	if (probe->edge_count < TIMER_EDGE_COUNT)
		probe->edge_cycles[probe->edge_count++] = probe->avr->cycle;
}

static avr_t *load_fixture(const char *path)
{
	elf_firmware_t firmware;
	memset(&firmware, 0, sizeof(firmware));
	if (elf_read_firmware(path, &firmware) != 0) {
		fprintf(stderr, "could not read AVR fixture: %s\n", path);
		return NULL;
	}

	strncpy(firmware.mmcu, "atmega1284p", sizeof(firmware.mmcu) - 1);
	firmware.mmcu[sizeof(firmware.mmcu) - 1] = '\0';
	firmware.frequency = CPU_FREQUENCY;

	avr_t *avr = avr_make_mcu_by_name(firmware.mmcu);
	if (avr == NULL) {
		fprintf(stderr, "simavr does not provide %s\n", firmware.mmcu);
		return NULL;
	}
	avr_init(avr);
	avr_load_firmware(avr, &firmware);
	avr->log = LOG_ERROR;
	return avr;
}

static avr_irq_t *port_pin(avr_t *avr, unsigned int pin)
{
	return avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), pin);
}

static bool run_avr_until(avr_t *avr, bool (*finished)(void *), void *parameter,
	avr_cycle_count_t cycle_limit)
{
	while (!finished(parameter) && avr->cycle < cycle_limit) {
		int state = avr_run(avr);
		if (state == cpu_Done || state == cpu_Crashed)
			return false;
	}
	return finished(parameter);
}

static bool timer_edges_complete(void *parameter)
{
	timer_probe_t *probe = parameter;
	return probe->edge_count == TIMER_EDGE_COUNT;
}

static bool timer0_period_is_stable(const char *fixture_path)
{
	avr_t *avr = load_fixture(fixture_path);
	if (avr == NULL)
		return false;

	timer_probe_t probe = { .avr = avr };
	avr_irq_t *pin = port_pin(avr, 0);
	if (pin == NULL) {
		fprintf(stderr, "could not observe simulated PB0\n");
		avr_terminate(avr);
		return false;
	}
	avr_irq_register_notify(pin, record_timer_edge, &probe);

	bool passed = run_avr_until(avr, timer_edges_complete, &probe,
		CPU_FREQUENCY / 10);
	if (!passed) {
		fprintf(stderr, "expected %u PB0 edges, observed %u before cycle %" PRI_avr_cycle_count "\n",
			TIMER_EDGE_COUNT, probe.edge_count, avr->cycle);
	} else {
		for (unsigned int i = 1; i < TIMER_EDGE_COUNT; ++i) {
			avr_cycle_count_t period = probe.edge_cycles[i] - probe.edge_cycles[i - 1];
			if (period < EXPECTED_TIMER_PERIOD_CYCLES - 1 ||
				period > EXPECTED_TIMER_PERIOD_CYCLES + 1) {
				fprintf(stderr,
					"PB0 edge %u was %" PRI_avr_cycle_count
					" cycles after the previous edge; expected %u +/- 1\n",
					i, period, EXPECTED_TIMER_PERIOD_CYCLES);
				passed = false;
			}
		}
		for (unsigned int i = 2; i < TIMER_EDGE_COUNT; i += 2) {
			avr_cycle_count_t two_periods =
				probe.edge_cycles[i] - probe.edge_cycles[i - 2];
			if (two_periods != 2 * EXPECTED_TIMER_PERIOD_CYCLES) {
				fprintf(stderr,
					"PB0 edges %u..%u span %" PRI_avr_cycle_count
					" cycles; expected %u\n",
					i - 2, i, two_periods,
					2 * EXPECTED_TIMER_PERIOD_CYCLES);
				passed = false;
			}
		}
	}

	avr_terminate(avr);
	return passed;
}

static uint8_t receive_level_for_tick(unsigned int tick)
{
	if (tick < 5)
		return 1;
	if (tick < 23)
		return 0; /* start */
	if (tick < 41)
		return 1; /* data: 1 */
	if (tick < 59)
		return 0; /* data: 0 */
	if (tick < 77)
		return 0; /* data: 0 */
	if (tick < 95)
		return 1; /* data: 1 */
	if (tick < 113)
		return 1; /* data: 1 */
	return 1; /* stop */
}

static void serial_ready_changed(struct avr_irq_t *irq, uint32_t value, void *parameter)
{
	(void)irq;
	serial_probe_t *probe = parameter;
	if (value != 0) {
		probe->ready = true;
		probe->tick_count = 0;
	}
}

static void serial_tick_changed(struct avr_irq_t *irq, uint32_t value, void *parameter)
{
	(void)irq;
	serial_probe_t *probe = parameter;
	value = value != 0;
	if (!probe->have_tick_value) {
		probe->last_tick_value = value;
		probe->have_tick_value = true;
		return;
	}
	if (value == probe->last_tick_value)
		return;
	probe->last_tick_value = value;
	if (!probe->ready)
		return;

	probe->tick_count++;
	if (probe->drive_receive)
		probe->avr->data[probe->receive_level_address] =
			receive_level_for_tick(probe->tick_count);
}

static void serial_tx_changed(struct avr_irq_t *irq, uint32_t value, void *parameter)
{
	(void)irq;
	serial_probe_t *probe = parameter;
	value = value != 0;
	if (!probe->have_tx_value) {
		probe->last_tx_value = value;
		probe->have_tx_value = true;
		return;
	}
	if (value == probe->last_tx_value)
		return;
	probe->last_tx_value = value;
	if (!probe->ready || probe->tx_edge_count >= SERIAL_TX_EDGE_COUNT)
		return;

	probe->tx_edge_ticks[probe->tx_edge_count] = probe->tick_count;
	probe->tx_edge_values[probe->tx_edge_count] = (uint8_t)value;
	probe->tx_edge_count++;
}

static void serial_busy_changed(struct avr_irq_t *irq, uint32_t value, void *parameter)
{
	(void)irq;
	serial_probe_t *probe = parameter;
	if (!probe->ready)
		return;
	if (value != 0) {
		probe->busy_start = probe->avr->cycle;
	} else if (probe->busy_start != 0) {
		avr_cycle_count_t duration = probe->avr->cycle - probe->busy_start;
		if (duration > probe->max_busy_cycles)
			probe->max_busy_cycles = duration;
		probe->busy_start = 0;
	}
}

static bool serial_run_complete(void *parameter)
{
	serial_probe_t *probe = parameter;
	return probe->ready && probe->tick_count >= SERIAL_RUN_TICKS;
}

static bool prepare_serial_probe(avr_t *avr, serial_probe_t *probe)
{
	avr_irq_t *tick = port_pin(avr, 0);
	avr_irq_t *tx = port_pin(avr, 1);
	avr_irq_t *ready = port_pin(avr, 2);
	avr_irq_t *busy = port_pin(avr, 3);
	if (tick == NULL || tx == NULL || ready == NULL || busy == NULL) {
		fprintf(stderr, "could not attach serial fixture GPIO probes\n");
		return false;
	}
	avr_irq_register_notify(tick, serial_tick_changed, probe);
	avr_irq_register_notify(tx, serial_tx_changed, probe);
	avr_irq_register_notify(ready, serial_ready_changed, probe);
	avr_irq_register_notify(busy, serial_busy_changed, probe);
	return true;
}

static bool serial_transmit_framing_is_correct(const char *fixture_path,
	avr_cycle_count_t *max_callback_cycles)
{
	avr_t *avr = load_fixture(fixture_path);
	if (avr == NULL)
		return false;
	serial_probe_t probe = { .avr = avr };
	if (!prepare_serial_probe(avr, &probe)) {
		avr_terminate(avr);
		return false;
	}

	bool passed = run_avr_until(avr, serial_run_complete, &probe,
		CPU_FREQUENCY * 4);
	if (!passed) {
		fprintf(stderr, "serial transmit fixture did not reach %u timer ticks\n",
			SERIAL_RUN_TICKS);
	} else if (probe.tx_edge_count != SERIAL_TX_EDGE_COUNT) {
		fprintf(stderr, "expected %u transmit edges, observed %u\n",
			SERIAL_TX_EDGE_COUNT, probe.tx_edge_count);
		passed = false;
	} else {
		for (unsigned int i = 0; i < SERIAL_TX_EDGE_COUNT; ++i) {
			uint8_t expected_level = (uint8_t)(i & 1);
			if (probe.tx_edge_values[i] != expected_level) {
				fprintf(stderr, "transmit edge %u selected level %u; expected %u\n",
					i, probe.tx_edge_values[i], expected_level);
				passed = false;
			}
		}
		for (unsigned int i = 1; i < 6; ++i) {
			unsigned int ticks = probe.tx_edge_ticks[i] - probe.tx_edge_ticks[i - 1];
			if (ticks != SERIAL_TICKS_PER_BIT) {
				fprintf(stderr, "first character bit %u lasted %u ticks; expected %u\n",
					i, ticks, SERIAL_TICKS_PER_BIT);
				passed = false;
			}
		}
		unsigned int inter_character = probe.tx_edge_ticks[6] - probe.tx_edge_ticks[5];
		if (inter_character != 45) {
			fprintf(stderr, "data-to-next-start interval was %u ticks; expected 45\n",
				inter_character);
			passed = false;
		}
		for (unsigned int i = 7; i < SERIAL_TX_EDGE_COUNT; ++i) {
			unsigned int ticks = probe.tx_edge_ticks[i] - probe.tx_edge_ticks[i - 1];
			if (ticks != SERIAL_TICKS_PER_BIT) {
				fprintf(stderr, "second character bit %u lasted %u ticks; expected %u\n",
					i - 6, ticks, SERIAL_TICKS_PER_BIT);
				passed = false;
			}
		}
	}

	*max_callback_cycles = probe.max_busy_cycles;
	avr_terminate(avr);
	return passed;
}

static bool serial_receive_decodes_character(const char *fixture_path,
	uint16_t receive_level_address, uint16_t receive_fifo_address)
{
	avr_t *avr = load_fixture(fixture_path);
	if (avr == NULL)
		return false;
	serial_probe_t probe = {
		.avr = avr,
		.receive_level_address = receive_level_address,
		.drive_receive = true
	};
	if (!prepare_serial_probe(avr, &probe)) {
		avr_terminate(avr);
		return false;
	}

	bool passed = run_avr_until(avr, serial_run_complete, &probe,
		CPU_FREQUENCY * 4);
	if (!passed) {
		fprintf(stderr, "serial receive fixture did not reach %u timer ticks\n",
			SERIAL_RUN_TICKS);
	} else {
		uint8_t code = avr->data[receive_fifo_address];
		uint8_t write_index = avr->data[receive_fifo_address + FIFO_SIZE];
		uint8_t read_index = avr->data[receive_fifo_address + FIFO_SIZE + 1];
		if (write_index != 1 || read_index != 0 || code != EXPECTED_RX_CODE) {
			fprintf(stderr,
				"receive FIFO was code=0x%02x write=%u read=%u; expected 0x%02x/1/0\n",
				code, write_index, read_index, EXPECTED_RX_CODE);
			passed = false;
		}
	}

	avr_terminate(avr);
	return passed;
}

static bool callback_has_headroom(avr_cycle_count_t max_callback_cycles)
{
	if (max_callback_cycles == 0 ||
		max_callback_cycles >= EXPECTED_TIMER_PERIOD_CYCLES) {
		fprintf(stderr,
			"maximum serial callback runtime was %" PRI_avr_cycle_count
			" cycles; it must remain below the %u-cycle Timer0 period\n",
			max_callback_cycles, EXPECTED_TIMER_PERIOD_CYCLES);
		return false;
	}
	return true;
}

static bool parse_sram_address(const char *text, uint16_t *address)
{
	char *end = NULL;
	unsigned long value = strtoul(text, &end, 0);
	if (end == text || *end != '\0')
		return false;
	if (value >= SRAM_ELF_OFFSET)
		value -= SRAM_ELF_OFFSET;
	if (value > UINT16_MAX)
		return false;
	*address = (uint16_t)value;
	return true;
}

static void tap_result(unsigned int number, bool passed, const char *description)
{
	printf("%s %u - %s\n", passed ? "ok" : "not ok", number, description);
}

int main(int argc, char **argv)
{
	if (argc != 6) {
		fprintf(stderr,
			"usage: %s TIMER.elf SERIAL_TX.elf SERIAL_RX.elf BUS_MARK_ADDR RX_FIFO_ADDR\n",
			argv[0]);
		return EXIT_FAILURE;
	}

	uint16_t receive_level_address;
	uint16_t receive_fifo_address;
	if (!parse_sram_address(argv[4], &receive_level_address) ||
		!parse_sram_address(argv[5], &receive_fifo_address)) {
		fprintf(stderr, "invalid AVR SRAM symbol address\n");
		return EXIT_FAILURE;
	}

	printf("TAP version 13\n");
	printf("1..4\n");
	fflush(stdout);

	bool timer_passed = timer0_period_is_stable(argv[1]);
	tap_result(1, timer_passed, "Timer0 callback period is 17920 CPU cycles");

	avr_cycle_count_t max_callback_cycles = 0;
	bool transmit_passed = serial_transmit_framing_is_correct(argv[2],
		&max_callback_cycles);
	tap_result(2, transmit_passed, "50-baud transmit framing and stop interval");

	bool receive_passed = serial_receive_decodes_character(argv[3],
		receive_level_address, receive_fifo_address);
	tap_result(3, receive_passed, "50-baud receive sampling decodes one character");

	bool headroom_passed = callback_has_headroom(max_callback_cycles);
	char description[128];
	snprintf(description, sizeof(description),
		"serial callback retains Timer0 headroom (max %" PRI_avr_cycle_count " cycles)",
		max_callback_cycles);
	tap_result(4, headroom_passed, description);

	return timer_passed && transmit_passed && receive_passed && headroom_passed
		? EXIT_SUCCESS : EXIT_FAILURE;
}
