# Firmware tests

The default suite contains fast host unit tests. Register-level integration
tests run separately under simavr; see [AVR emulator tests](avr/README.md).

## Host unit tests

These suites test the parts of the firmware that are pure logic: the
Baudot/ITA2 character conversion, the shared ring buffer, the backplane CRC,
the parsers for the text an operator types into the web interface, the framing
of the data received on the i-Telex socket, and the small byte-order, Base64
and hex-parsing helpers. They are compiled by the **host** compiler and run on
the build machine, so they need no AVR toolchain and no i-Telex hardware, and
they finish in well under a second.

Nothing here is linked into the firmware.

## Running them

```sh
make test          # from the repository root
make -C tests test # or directly
```

Both build every suite and run it. A suite that fails does not stop the others,
so one run reports the whole picture; the target then exits non-zero. `make
bootstrap` has to have been run once, because most suites compile sources from
the `itelex-misc` submodule.

Other targets, in this directory:

```sh
make all    # build the suites without running them
make list   # print the suite names
make clean  # remove tests/build
```

Output is [TAP version 13](https://testanything.org/). A failure names the
file, the line and both values:

```
not ok 8 - decoding_handles_the_whole_alphabet
  ---
  suites/test_base64.c:152: (unsigned char)scratch[47]: expected 251 (0xfb), got 191 (0xbf)
  ...
```

## What is covered

| Suite | Unit under test | What it pins down |
| --- | --- | --- |
| `baudot_code` | `dependencies/itelex-misc/Gemeinsam/BaudotCode.c` | The ITA2 alphabet in both cases, the shift-state machine, when a shift code is inserted or elided, and an encode/decode round trip |
| `fifo_buffer` | `dependencies/itelex-misc/Gemeinsam/FifoPuffer.c` | FIFO order, the count across a wrap, the two-slot margin in the full report, and that the accessors restore the interrupt-enable bit |
| `crc8` | `system/math/crc8.c` | The published CRC-8/MAXIM check value, the append-and-check-to-zero property, and detection of every single-bit error in an eight-byte frame |
| `math` | `system/math/math.c` | The 16- and 32-bit byte swaps and the packed-BCD conversions over their whole range |
| `base64` | `system/base64/base64.c` | The RFC 4648 vectors in both directions, the output-size refusal, and a round trip over non-text bytes |
| `string_utils` | `system/string/string.c` | Hex digit and MAC address parsing, separator handling, and the length check |
| `parsing` | `iTelex/Parsing.c` | Signed-number and extension-number scanning, and the baud-rate table format — which entry wins, where a malformed table is reported, and the two answers that are not baud rates |
| `frame_scanner` | `iTelex/FrameScanner.h` | How the bytes arriving on the i-Telex socket are cut into frames: ASCII against blocks, the two ASCII substitutions, what settles the protocol, and what happens to a fragmented, a short, an unknown or a malformed block |

## What is deliberately not covered

Anything that touches a register, a timer or the network is not pure logic and
does not belong in the host suite. Timer0 and the software serial converter are
covered by the simavr suite; network behaviour and electrical characteristics
still need a real card.

Two units that look like candidates are left out for a specific reason, and
both reasons are worth fixing:

- **`system/math/checksum.c`** relies on `int` being 16 bits and `long` 32,
  which holds on the AVR but not on a host: `Checksum_16` returns `0x220a` here
  where the RFC 1071 worked example gives `0x220d`. Giving it `uint16_t` and
  `uint32_t` would be behaviour-preserving on the AVR and would make it
  testable.
- **`system/net/endian.c`** has a non-AVR branch that does not compile: it
  refers to a parameter `A` that no longer exists. Nothing builds that branch
  today, so the breakage is invisible.

`system/base64/base64.c` is covered, but note that `base64_decode` indexes its
reverse lookup table with `character - 43` without a range check, so any byte
below `'+'` or above `'z'` reads outside the table. The suite stays inside the
valid alphabet; feeding the decoder hostile input is a defect to fix, not a
behaviour to pin down.

## What the tests record rather than fix

`frame_scanner` is a characterization suite: it describes what the receive
path does today so that it can be changed without changing behaviour, and
three of its cases pin down answers that look like defects. A block other than
`ITELEXC_BAUDOT_DATA` whose payload has not all arrived is not held back for
the rest of the stream — the cursor runs past the received data and the
buffer is discarded. A disconnect block with not even its length byte received
wraps to a payload of 255. And on a connection carrying ASCII, the command
codes that share a value with an ASCII control code (`ITELEXC_VERSION`,
`ITELEXC_SELBSTANRUF`, `ITELEXC_FERNKONFIG`) cannot be received at all. Each is
a change to what goes over the wire, so each belongs in its own commit with its
own reasoning, not in a refactoring step.

## The one stubbed dependency

`parsing` is the only suite that supplies a firmware function itself rather
than linking the real one. `Parsing.c` calls `WahlZuAdresse` to turn an
extension number into a bus address, and that function lives in `BusKomm.c`
alongside the TWI interrupt handler, which reaches for ATmega registers the
shims do not model. `test_parsing.c` therefore defines it, recording the
arguments it was handed and reproducing the mapping documented in `BusKomm.c`.

The recorded arguments are what the `ParseExtensionAddress` cases assert on,
because choosing the number and the digit count is the whole of what that
function decides; the reproduced return value only exists so the baud-rate
cases can use the addresses the firmware really uses. Linking the real
function instead would mean shimming the TWI peripheral, which is worth doing
if a second suite ever needs `BusKomm.c`.

## Adding a suite

1. Write `suites/test_<name>.c`. Declare cases with `TEST_CASE`, list them in a
   table built from `TEST_ENTRY`, and hand the table to `test_run_suite` from
   `main`. See `framework/test_framework.h` for the assertions.
2. Add `<name>` to `SUITES` in `GNUmakefile` and set `<name>_UNITS` to the
   firmware sources the suite links against. A header-only module — such as
   `FrameScanner.h`, whose one function is force-inlined into its single call
   site to keep it off the Light card's flash budget — has no source to link,
   so its `_UNITS` is left empty and the suite simply includes the header.

Two things to keep in mind when writing a case:

**Derive the expected value from the specification, not from the code.** A test
that echoes the implementation cannot catch a wrong table entry. The ITA2 codes
in `test_baudot_code.c` come from the published alphabet and the Base64 vectors
from RFC 4648, which is why they are worth having.

**Keep the host honest about the AVR.** `ABI_CFLAGS` in `GNUmakefile` carries
the flags that change what the code under test *means* — `-funsigned-char`
above all, because a host `char` is signed and the firmware relies on avr-gcc's
unsigned default. A unit whose result depends on the width of `int` or `long`
cannot be tested faithfully on a host at all; fix the types first, as the
`checksum.c` note above describes.

## Conventions

New code here is written in English, including identifiers and comments, and
carries Doxygen comments as the rest of the tree does — the suites appear in
the generated documentation under the *Host test suites* group. Where a test
exercises an existing German-named function, that name stays as it is, because
it is the API; translations belong in the units themselves, and when one is
translated the original name is worth keeping in a `ehem.` comment so it stays
searchable.

The firmware's own style is mixed — the i-Telex application code is
`PascalCase` and the inherited stack is `snake_case`. New test code uses
`snake_case`, as the stack and the wider C ecosystem do.

Case names double as their description in the report, so they are written as a
sentence about the behaviour: `stores_bytes_until_one_slot_remains`, not
`test_buffer_3`.

## The AVR header shims

`shim/avr/` stands in for the avr-libc headers the units include:

- `pgmspace.h` reduces `PROGMEM` to nothing and `pgm_read_byte` to a
  dereference, which is what lets the flash-resident conversion tables be read
  on a host.
- `io.h` models `SREG` as an ordinary byte, and `interrupt.h` makes `cli()` and
  `sei()` clear and set its interrupt-enable bit. That is what allows
  `fifo_buffer` to assert that the buffer accessors leave interrupts as they
  found them.

Add to the shims rather than working around them, so every suite sees the same
model of the machine.
