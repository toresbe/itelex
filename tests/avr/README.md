# AVR emulator tests

These tests compile small ATmega1284P firmware images and execute them in
simavr. Unlike the host unit tests, they use avr-libc, the real Timer0 driver,
and the production `itelex_timerEvent` callback.

Run them from the repository root:

```sh
make test-avr
```

The required tools are avr-gcc, avr-binutils, avr-libc, simavr, the simavr
development headers, and libelf. On Debian 13 the corresponding packages are
installed by the CI workflow. On macOS, the test makefile automatically finds
the Homebrew `osx-cross/avr/simavr` and `libelf` prefixes.

The suite currently verifies:

- Timer0 calls back every 17,920 CPU cycles at the requested 900 Hz setting.
  A one-cycle observation offset is allowed on an individual simulated GPIO
  edge, but each pair of periods must total exactly 35,840 cycles.
- Two 50-baud Baudot characters produce the expected start/data/stop levels,
  18 Timer0 ticks per bit, and a 1.5-bit stop interval.
- A simulated 50-baud input waveform is sampled into the expected receive
  FIFO entry.
- The longest observed production serial callback completes before the next
  Timer0 interrupt. The measured maximum is printed in TAP output.

The fixtures expose simulation-only probe pins and provide no-op substitutes
for unrelated network, logging, LED and TWI facilities. They do not replace
the serial state machine: the ELF links the actual `iTelex/iTelex.c` and
`hardware/timer0/timer0.c` implementations.

This is aimed at deterministic regressions in register setup, tick counts,
framing, state transitions and CPU-cycle budget. It cannot validate oscillator
tolerance, interrupt latency caused by omitted peripherals, TWI bus timing,
the current-loop electronics, or behavior specific to the ATmega2561. Those
remain hardware-in-the-loop tests.
