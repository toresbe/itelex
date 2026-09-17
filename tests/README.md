# Host unit tests

These suites test the parts of the firmware that are pure logic: the
Baudot/ITA2 character conversion, the shared ring buffer, the backplane CRC,
and the small byte-order, Base64 and hex-parsing helpers. They are compiled by
the **host** compiler and run on the build machine, so they need no AVR
toolchain and no i-Telex hardware, and they finish in well under a second.

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

## What is deliberately not covered

Anything that touches a register, a timer or the network is not pure logic and
does not belong here; testing it needs a simulator or the real card, which is a
separate piece of work.

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

## Adding a suite

1. Write `suites/test_<name>.c`. Declare cases with `TEST_CASE`, list them in a
   table built from `TEST_ENTRY`, and hand the table to `test_run_suite` from
   `main`. See `framework/test_framework.h` for the assertions.
2. Add `<name>` to `SUITES` in `GNUmakefile` and set `<name>_UNITS` to the
   firmware sources the suite links against.

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
