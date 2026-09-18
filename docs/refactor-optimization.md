# Refactoring and optimising `iTelex/iTelex.c`

Working notes for the second pass over the firmware's largest source file.
[`splitting-itelex-c.md`](splitting-itelex-c.md) covers the first pass —
moving self-contained state into modules of its own — and remains the
authority on the constraints. This file covers the steps that do not move
state anywhere: making the big functions readable and, where the tests allow
it, smaller.

**Provenance.** This file did not exist on any branch when step 2 was carried
out, though the step briefs handed to the people doing the work referred to it
by name. It was written to record step 2 and to give the later steps somewhere
to land. The step titles below are the ones from those briefs, reproduced as
they were given; everything beyond a title is left for whoever does the step,
except for step 2, which is done and is described in full. The briefs began at
step 2, so whatever steps 0 and 1 of this pass were meant to be is not
recorded here.

## What carries over from the first pass

These hold for every step here, and the reasoning behind them is in
[`splitting-itelex-c.md`](splitting-itelex-c.md).

- **The Light build is the gate.** The ATmega1284P image has about 1.1 KB of
  usable flash left. Every step measures clean `standard` and `light` builds
  and records `avr-size` in the commit message. Incremental builds after a
  header change under-report the cost.
- **Everything new or moved is written in English**, and a renamed identifier
  keeps `// ehem. <OriginalName>` on its declaration so the German notes, the
  changelog and the manual test plan stay searchable.
- **Behaviour fixes are separate commits from refactoring.** Where a test
  finds something that looks like a defect, the step records it and moves on.
- **Doxygen stays neat.** No step adds warnings, and each new module gets its
  own `\defgroup`.

Measure with a pinned `VERSION`, because the version string is compiled into
the image and moves the numbers on its own:

```sh
make clean
make VERSION=<something-fixed> standard light
avr-size --format=avr --mcu=atmega2561  build/standard/Main.elf
avr-size --format=avr --mcu=atmega1284p build/light/Main_Light.elf
avr-nm --print-size --size-sort --radix=d build/light/Main_Light.elf
```

Ignore the anonymous `__c.NNNN` string literals in the `avr-nm` output: they
renumber whenever line numbers move. What matters is which named functions
changed size.

### What this pass costs, and how to keep it near zero

The first pass established that moving code into another translation unit
costs flash per *call site*, because avr-gcc loses both inlining and
interprocedural register allocation at the boundary. Step 2 confirms it and
adds the way around it for a function with a single caller: put the module in
a header and force-inline it. The same carve measured both ways:

| Shape | standard | light |
| --- | --- | --- |
| baseline | 171,130 B | 128,886 B |
| `FrameScanner.c`, ordinary call | 171,324 B (+194) | 129,072 B (**+186**) |
| `FrameScanner.h`, `always_inline` | 171,138 B (+8) | 128,894 B (**+8**) |

Two further things were worth 12 and 32 bytes on their own and are worth
remembering:

- **Drop derived fields from the descriptor.** A field the caller can compute
  — here the payload offset, always the cursor plus two — costs more to carry
  than to recompute.
- **Prefer a comparison chain to a `switch` over the command codes.** avr-gcc
  builds a jump table for the `switch`, which is larger than the chain for ten
  cases. This is also why the original loop carries a comment forbidding a
  `switch`, for a different reason: `break` there has to leave the loop.

A header-only module keeps the host tests, which include the header directly,
and leaves nothing to link. It only works where there is one call site; a
second would duplicate the code.

## Step 2 — protocol frame scanner — **done**

`ITelexOderAsciiEmpfangVerarbeiten` decided three things in one pass: what the
byte at the cursor is, how far the cursor moves, and what that means for the
socket, the mode and the output buffers. The first two are pure and are now
`ScanFrame` in [`../iTelex/FrameScanner.h`](../iTelex/FrameScanner.h); the
effects stayed in `iTelex.c`, as the step required.

The header also took over the `ITELEXC_*` command codes and the two
substitution characters the ASCII protocol uses, which are the frame grammar
rather than decoder state. `Centralex.c` keeps its own private subset, which
it extends with the relay's own codes, and was left alone. The move and the
rename are two commits, as the first pass settled.

`ScanFrame` reports the kind of frame, how far the cursor advances, the
payload length the caller may read, whether the whole declared block has
arrived, the protocol state after the frame, and — for a character — the
character to print. The suite is
[`tests/suites/test_frame_scanner.c`](../tests/suites/test_frame_scanner.c),
29 cases covering fragmented, concatenated, malformed, unknown, ASCII and
i-Telex frames.

**What the tests found.** Three answers look like defects. They are recorded
rather than corrected, because each changes what goes over the wire:

- **A fragmented block is lost, not waited for.** Only `ITELEXC_BAUDOT_DATA`
  checks that its payload has arrived; the decoder waits for the rest and
  retries. Every other block is acted on as it stands, and the cursor then
  advances past the received data, at which point the caller discards the
  whole receive buffer. A call-setup block split across two TCP segments is
  therefore dropped.
- **A disconnect block with only its command byte received wraps to 255.**
  The length is cut down to what arrived with `Used - Cursor - 2`, which is
  one short of zero in that case. The decoder then reads 255 bytes of
  "reason" from beyond the data and advances the cursor by 257.
- **Three commands cannot be received on an ASCII connection.**
  `ITELEXC_VERSION` (0x07), `ITELEXC_SELFCALL` (0x08) and
  `ITELEXC_REMOTECONFIG` (0x09) share their values with control codes the
  ASCII protocol carries, and the text reading wins once the socket is known
  to carry ASCII.

Two smaller things the tests pin down and that a reader would not guess:
`ITELEXC_REMOTECONFIG` is the one block that does not settle the protocol
state, and the length byte of a block is read whether or not it has arrived —
the receive buffer is declared four bytes longer than it is ever filled, which
is what keeps that read inside the array.

**Verification.** All 116 host cases pass. A throwaway differential harness
compared `ScanFrame` against a verbatim transcription of the old branch chain
over 20,643,840 combinations of byte value, protocol state, cursor position
and buffer length, agreeing on every field; no mismatches. `avr-nm` shows the
decoder as the only symbol whose size changed. Socket behaviour and the mode
transitions the decoder drives are not covered by anything but the manual test
plan in [`notes/Testumfang.txt`](notes/Testumfang.txt) on real hardware.

**Cost:** +8 bytes on both variants, diagnosed in the table above.

| Variant | Before | After | Change |
| --- | --- | --- | --- |
| `standard` | 171,130 B | 171,138 B | +8 |
| `light` | 128,886 B | 128,894 B | +8 |

Measured with avr-gcc 7.3.0 on clean builds with `VERSION` pinned. Light free
space goes from about 1,162 to about 1,154 bytes.

## Step 3 — shrink the protocol decoder

Not started. Uses the step 2 suite as its safety net to simplify and reduce
the size of `ITelexOderAsciiEmpfangVerarbeiten`, centralising bounds checks,
length decoding and buffer advancement without changing established
behaviour, and reporting total and per-symbol size changes.

## Step 4 — make `itelex_thread` readable

Not started. Refactors `itelex_thread` into roughly 6–8 named private helpers
that expose its scheduling phases, kept in `iTelex.c` and force-inlined where
necessary, without moving state or changing execution order. The target is
identical firmware size and preferably byte-identical generated instructions.

## Step 5 — ASCII printing and wrapping

Not started. Characterises the transformation logic in
`AsciiDruckPufferVerarbeiten` — substitutions, wrapping, CR/LF, `WerDa`,
partial output and retained line position — then simplifies and optimises the
tested logic without adding production test overhead.

## Step 6 — mode-entry behaviour

Not started. Characterises every `ModusWechsel` destination by its observable
status bits, LEDs, buffer handling, timers, serial flags and socket state,
then consolidates the repeated entry actions with inline helpers or a smaller
measured representation, preserving every transition.

## Step 7 — reassess module extraction and LTO

Not started. Re-measures the promising architectural extractions against the
current tree, evaluates what LTO does to them, and recommends the next
state-owning module boundary with measurements, coupling analysis, risks and
the hardware tests it would need. An LTO build is not release-ready without
documented real-hardware validation.

## Verification checklist for each step

The same as the first pass, in
[`splitting-itelex-c.md`](splitting-itelex-c.md#verification-checklist-for-each-step):
`make test`, `make test-avr` where the timer path is touched, clean
`make standard light` with no new warnings, `avr-size` on both images against
the numbers above, `doxygen Doxyfile.github` with no new warnings, and a
commit message that says plainly what is *not* verified.

One addition for the steps here, which change existing logic rather than move
it: where a step carves a pure function out of a larger one, write a throwaway
harness that runs the old logic and the new one over every input that matters
and compare. It is cheap, it catches what a handful of test cases will not,
and it does not belong in the committed suite — the suite describes the
behaviour, the harness proves the carve did not change it.
