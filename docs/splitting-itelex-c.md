# Splitting `iTelex/iTelex.c`

Handoff notes for the work of breaking up the firmware's largest file. Written
against commit `f019e7e`; every line number below is from that commit, so
re-check them before acting. Steps 0 and 1 have since reduced `iTelex.c` by
708 lines in total, so the recorded line numbers are now only landmarks.

Steps 0 and 1 are done; steps 2 and 3 are not. The host unit tests
([`tests/`](../tests/README.md)) are in place and are the safety net for part
of the rest.

## Standing conventions for this work

These apply to every step below.

- **Everything new or moved is written in English** — identifiers, comments,
  Doxygen text. Code that is only relocated keeps its behaviour, but its names
  and comments are translated as part of the step that touches it.
- **Keep the original name in a comment.** When a function or variable is
  renamed, leave `// ehem. <OriginalName>` (*ehem.* = *ehemals*, formerly) on
  the declaration. The project's history, its German notes, the changelog in
  `docs/notes/Verbesserungen.txt` and the manual test plan all refer to the old
  names, and they have to stay searchable.
- **`iTelex/StringTab.h` is a reference for terminology, not an authority.** It
  gives Fred's own German → English pairs for the domain vocabulary
  (*Teilnehmer* → subscriber, *Verbindung* → connection, *Rufnummer* → number,
  *Durchwahl* → secondary/direct call), which is the right starting point. But
  some entries are plainly wrong — *Teilnehmer gestoert* is rendered
  "Subscriber derailed" where it means *out of order* — so translate for
  meaning and use the table to stay consistent, not to settle a word.
- **Doxygen stays neat and complete.** Every new file gets a `\file` comment,
  every function a brief and its parameters, and each new module its own
  `\defgroup` so the generated documentation gains a page rather than a pile of
  loose symbols. The tree already warns plentifully — 244 warnings from
  `doxygen Doxyfile.github` with Doxygen 1.9.8, and the count moves with the
  Doxygen version, so compare before and after rather than trusting the
  number. A step should never add to it.

## The hard constraint: flash on the Light card

Measured on `f019e7e` with avr-gcc 7.3.0:

| Variant | Device | Flash used | Free |
| --- | --- | --- | --- |
| `standard` | ATmega2561, 256 KiB | 171,084 B — 65.3% | ~91 KB |
| `light` | ATmega1284P, 128 KiB | **128,820 B — 98.3%** | **~1.2 KB** |

The Light card has about 1.2 KB of usable flash left once the 1 KiB FastBoot
bootloader at the top of memory is accounted for. **The Light build is the
gate on this entire refactor.**

Moving functions out of `iTelex.c` into a new translation unit can cost flash:
at `-Os`, GCC inlines and clones `static` functions within a translation unit,
and it cannot do either across the boundary. `-ffunction-sections` with
`--gc-sections` is already on, which limits the damage but does not remove it.

So every step is measured, and `avr-size` output before and after belongs in
the commit message. **Measure clean builds.** An incremental `make` after
editing a header reported half the true cost of step 0; the numbers here are
all from `make clean` first.

### What step 0 cost, and why it generalises badly

Step 0 was the cheapest cut available — four functions, no file-scope state,
one entry point, two call sites — and it still grew the Light image:

| Variant | Before | After | Change |
| --- | --- | --- | --- |
| `standard` | 171,084 B | 171,112 B | +28 |
| `light` | 128,820 B | 128,856 B | **+36** |

Light free space went from about 1,228 to about 1,192 bytes.

The cost is not in the moved code. `avr-nm` across the two builds shows the
moved functions byte-identical and the growth confined to the two
`DetermineBaudRate` call sites left behind in `iTelex.c`: `ExternEepromOeffnen`
+2 and `cgi_Isp` +16 on Light. What GCC loses at a translation-unit boundary is
not only inlining but interprocedural register allocation — it can no longer
see which registers the callee clobbers, so every call site pays to save more
of them.

That is a per-call-site cost, which means **the price of an extraction scales
with how often the rest of the file calls into it**, not with how much code
moves. Step 0 has two call sites. RemoteServer has seven. Neither of the
obvious mitigations helps: making a module's internal helpers `static` changes
nothing, and `__attribute__((pure))` on the entry point made it 36 bytes worse
still.

### `-flto` is worth more than every extraction costs

Adding `-flto` to `COMMON_CFLAGS` builds this tree and produces a Light image
of **128,116 B** — 740 bytes below the post-step-0 build and 704 below the
original baseline. That is roughly twenty times what step 0 cost, from one
flag, and it is exactly the optimisation whose absence this refactor keeps
paying for: LTO restores cross-module inlining and register allocation.

It is not committed and not validated. LTO changes code generation everywhere,
AVR LTO has known sharp edges around interrupt handlers and `PROGMEM`, and
nothing about it can be trusted without running
[`docs/notes/Testumfang.txt`](notes/Testumfang.txt) on real hardware. But if it
survives a bench test, the flash gate stops being the binding constraint on
this work, and the third open decision below largely dissolves.

Until then, a step that grows the Light image is a judgement call rather than
an automatic refusal. If a step is worth keeping but costs flash, the honest
options are to compile the module out of the Light build entirely where the
feature is already absent, or to abandon that particular cut — not to spend the
last kilobyte on tidiness.

## What the file actually is

8,386 lines at `f019e7e`, 87 function definitions, 131 file-scope variables
(91 `static`, 40 externally visible through `iTelex.h`). Step 0 took it to
8,240 lines and 83 functions; the state count is untouched, which is the point
— the file is large because of its state, and step 0 deliberately moved the
only code that had none.

The ten largest functions account for roughly half the file:

| Function | Line | Lines |
| --- | --- | --- |
| `itelex_thread` | 4808 | 1,397 |
| `SocketBearbeiten` | 2592 | 444 |
| `ITelexOderAsciiEmpfangVerarbeiten` | 3481 | 368 |
| `RemoteServerBearbeiten` | 2231 | 361 |
| `ModusWechsel` | 1603 | 341 |
| `itelex_timerEvent` | 1257 | 310 |
| `itelex_init1` | 7935 | 276 |
| `itelex_cgi_debug` | 6442 | 236 |
| `Verbindungsaufbau` | 4207 | 204 |
| `AsciiDruckPufferVerarbeiten` | 4440 | 162 |

## Why the obvious split does not work

The tempting first cut is the HTTP/CGI layer (lines 6205–7812, ~1,600 lines of
`itelex_cgi_*` handlers): a web UI looks like a separate concern from carrying
telex traffic.

It is not separable as it stands. Those handlers reference **97 of the 131
file-scope variables**, and share 96 of them with the rest of the file. That is
inherent rather than sloppy: the configuration pages write nearly every
setting, and the debug page prints nearly every timer and counter. Moving them
to their own file would mean publishing ~96 private `static` variables in a
header — trading one big file for a much wider interface, which is worse.

Measured per candidate block, `vars` counts the file-scope variables the block
touches and `exclusive` the ones no other block touches:

| Candidate block | Lines | vars | exclusive |
| --- | --- | --- | --- |
| utilities (`low` … `Diagnoseausgabe_P`) | 328 | 6 | 0 |
| socket state logging | 61 | 1 | 0 |
| serial + timer + mode | 747 | 58 | 5 |
| connection handling | 1,525 | 52 | 2 |
| data path (receive/print) | 535 | 34 | 0 |
| subscriber server + dialling | 684 | 36 | 0 |
| server socket log | 120 | 2 | 0 |
| `itelex_thread` | 1,397 | 68 | 1 |
| HTTP/CGI handlers | 1,517 | 97 | 1 |
| ISR + init | 574 | 66 | 0 |

**The lesson: split by state, not by function.** The file is large because its
state is large and shared. A cut is only clean where a cluster of variables is
touched almost entirely by a small, adjacent set of functions.

Grouping the variables by name prefix gives the real picture:

| State cluster | Refs | Where they are |
| --- | --- | --- |
| `RemoteServer*` (11 statics) | 222 | 186 in 3 adjacent functions |
| `iTelexSocket*` | 205 | spread over 14 functions — this is the core protocol machine |
| `AsciiDruck*`/`Html*` | 226 | spread over 15 functions |
| `SelbstAnruf*` | 150 | 113 inside `itelex_thread` |
| `SerUm*` (soft serial) | 108 | 74 inside `itelex_timerEvent`, the timer ISR path |

## Method

Two rules make each step reviewable and reversible.

**Separate the move from the translation.** Each extraction is two commits:

1. *Move verbatim.* Create the new `.c`/`.h`, move the code and its state
   unchanged, adjust includes, add the header's `\file` and `\defgroup`.
   A reviewer can diff the moved text against the original and see that
   nothing changed.
2. *Translate.* Rename the module's identifiers to English, translate its
   comments, add the `ehem.` notes. A rename-only diff.

Doing both at once produces a diff nobody can check.

**Measure both builds on every step.** `make standard light` and record
`avr-size`. The Light number is the one that decides.

## Work plan, in order

### Step 0 — extract the pure helpers, and test them — **done**

`iTelex/Parsing.c` / `.h` now holds four functions, moved and translated in
the two-commit pattern, with a `parsing` suite of 17 cases under
`tests/suites/`:

| Was | Is now |
| --- | --- |
| `ParseInt16` | `ParseInt16` |
| `ParseSkipSpace` | `ParseSkipSpace` |
| `ParseNstAddresse` | `ParseExtensionAddress` |
| `BaudrateErmitteln` | `DetermineBaudRate` |

The module came out cleaner than the candidate list suggested. Only
`DetermineBaudRate` is called from the rest of `iTelex.c`, from two sites; the
other three are used solely by it and by each other. `ParseSkipSpace` had to
come along — leaving a dependency of `DetermineBaudRate` behind would have
been three more cross-module calls for nothing — and it is fully pure, not
nearly so.

`Parsing.h` is deliberately not wrapped in `ITELEX_BASIS`. That guard is an
artefact of `iTelex.c` being one 8,000-line conditional; nothing in the module
depends on the firmware configuration, and staying clear of `config.h` — which
is generated per variant into `build/<variant>/generated/` — is what lets the
host tests compile the module at all. Any future module that wants host tests
needs the same restraint.

Three candidates from the original list stayed in `iTelex.c`:

- **`IntelHexWriteLine`** (7722) is `printf_P` output formatting belonging to
  the hexdump feature, not parsing. Its checksum is worth testing, but it
  should move with the hexdump code or into an output module, not into a file
  named for parsing.
- **`low` / `high`** (808/813) are `static inline`, cost nothing where they
  are, and are not parsing either.
- **`IsCommonAsciiControl`** (3469) is pure, but it classifies Baudot and
  ASCII control codes and sits in the receive path; it belongs with the data
  path if anywhere.

`AdresseZuWahlStr` (6958) is pure and is the formatting counterpart to
`ParseExtensionAddress`, so the two probably belong together eventually. It is
inside the `ITELEX_ANSCHLUSS` guard and already published in `iTelex.h`, which
is why it was left for a step that can deal with both.

**What the tests found.** `DetermineBaudRate` reads the operator-facing
baud-rate list (`70-79:75,*:50`), a format with no specification outside the
code and no check on it anywhere else. Four behaviours were undocumented:

- `1` is not a baud rate. It means the table was read to its end without a
  match, which is why `SeriellUmsetzInit` treats everything `<= 1` as failure.
- A negative answer is the offset of the offending character, negated — so an
  error at the first character comes back as `0` and carries no position.
- The scan returns at the first matching entry, so a malformed table validates
  clean if the error sits after the match. This is precisely what the CGI
  handler's lookup for extension `0` is for: no entry can match extension `0`,
  so the whole string has to be read. The comment at that call site says as
  much; the behaviour it relies on was written down nowhere.
- A reversed range (`79-70`) parses without complaint and matches nothing, so
  an operator who inverts one gets the fallback rate and no error at all.

`ParseExtensionAddress` also accepts `+4` as the two-digit number 4, because
the sign counts toward its two-character limit. Almost certainly not intended;
recorded rather than fixed, since step 0 was not to change behaviour.

**The one stub.** `Parsing.c` calls `WahlZuAdresse`, which lives in
`BusKomm.c` alongside the TWI interrupt handler and will not compile on a
host. The suite defines it, recording its arguments — which is what the
`ParseExtensionAddress` cases assert on — and reproducing the documented
mapping so the baud-rate cases can use real addresses. `tests/README.md`
explains the trade. Linking the real thing would mean shimming the TWI
peripheral, and is worth doing if a second suite ever needs `BusKomm.c`.

**Cost:** +36 bytes on Light, diagnosed above. See that section before
planning step 1.

### Step 1 — extract RemoteServer (Centralex) — **done**

The cleanest real module in the file. `RemoteServer` is the relay that lets an
i-Telex behind a non-public IP receive calls — `iTelex.h:104` records that it is
also called *Centralex*, which is the name users know, and the new module
should probably carry that name.

**Move:** the declaration block and protocol diagram at lines 354–464, the 11
`static` variables, the `RemoteServerLinkStatus` enum and variable, and three
functions — `RemoteServerUseAnotherOne` (2152), `RemoteServerSendBufferIfNotEmpty`
(2182) and `RemoteServerBearbeiten` (2231). About 440 lines of body plus 110 of
declarations and commentary.

All 11 variables are `static` today, so they stay private and the module's
interface is *narrower* than the status quo.

**What the rest of the file needs from it** — the whole external surface, which
is the header:

| Call site | Line | Needs |
| --- | --- | --- |
| `itelex_thread` | 5597 | a per-cycle tick (`RemoteServerBearbeiten`) |
| `itelex_init2` | 8164–8170 | an init that resets link state |
| `UseEEConfig` | 8040–8054 | set port and active flag from stored configuration |
| `itelex_cgi_config_extern` | 7494, 7531–7539 | read and write the active flag |
| `itelex_cgi_config_sperren` | 7407 | read the active flag |
| `itelex_cgi_debug` | 6501–6508 | print 8 diagnostic values |
| `FalschGeheimzahlWurdeGemeldet` | 2143 | clear the active flag |

The debug page is the one thing that needs redesigning rather than moving: it
uses a `PRINTVAL` macro that prints a variable's name and value, which cannot
reach a `static` in another file. Give the module a function that prints its own
rows in the same format, and call that from `itelex_cgi_debug`. Do not publish
the variables to keep the macro working.

**What it needs from the rest of the file:** eight non-`RemoteServer`
variables — `Modus`, `iTelexSocketHandle`, `iTelexSocketMode`, `SendePuffer`,
`Geheimzahl`, `FalschGeheimzahlZaehler`, `NetzRufnummer`, `LokaleSprache` — and
`KommendeVerbindungInitialisieren`, plus the logging helpers and the socket API,
which are already external. Those eight are the coupling that remains; they are
mostly read, and the first five are already declared in `iTelex.h`.

**Risk:** nothing here is host-testable (it is all sockets and timers), so
verification is the build, the size check, and reading the moved diff. The
feature is also exercised by the manual test plan, which is the real check
before a release.

**Result:** `Centralex.c` and `Centralex.h` now own the 11 private state
variables and the three state-machine functions. The public interface is
`CentralexInitialize`, `CentralexProcess`, `CentralexIsEnabled`,
`CentralexSetEnabled`, `CentralexSetPort`, and `CentralexPrintDiagnostics`;
the last function keeps the debug-page access inside the module instead of
exposing its state. The module's identifiers and comments are English, with
the former names retained in `ehem.` comments. Configuration keys, protocol
bytes, and user-visible diagnostic strings were deliberately left unchanged.

The move and terminology pass were separate commits. With avr-gcc 9.5.0, the
clean final images are 26 bytes larger for `standard` and 42 bytes larger for
`light` than the untouched pre-step-1 commit. The compiler differs from the
avr-gcc 7.3.0 used for the measurements above, so only same-toolchain deltas
are comparable. All 87 host tests pass, neither the firmware build nor Doxygen
gains warnings, and the Centralex object has byte-identical instructions
before and after the rename. Socket and timer behaviour still needs the manual
test plan on hardware.

### Step 2 — extract the self-call check (`SelbstAnruf*`) — **started**

A self-contained watchdog: the interface periodically calls itself through the
network to prove it is still reachable, and reports a fault if it cannot.
About 8 variables and 113 references, but they live *inside* `itelex_thread`,
so this step means carving a coherent block out of a 1,397-line function
before it can move. Worth doing, and the natural way to start shrinking the
thread, but it is a genuine refactor rather than a relocation.

**First slice:** the state machine and its socket-timeout recovery are now
carved into `ProcessSelfCall` and `ProcessSelfCallSocketTimeout`, private
helpers which remain in `iTelex.c`. Their calls occupy the exact positions of
the former inline blocks, preserving the ordering around dynamic-IP updates.
Both helpers are forced inline: allowing avr-gcc 9.5.0 to outline them cost 14
bytes on Light, while forced-inline clean builds with `VERSION=969be1c` are
exactly the same size as the pre-carve builds (`standard` 173,490 B; `light`
130,908 B).

This is intentionally not called an extraction yet. The private state still
lives in `iTelex.c`, and receive handling, configuration, diagnostics and
subscriber-server responses still touch it directly. The next slice must turn
those sites into a small semantic interface before the state and helpers can
move without publishing the eight variables. With only 164 bytes between the
current Light image and the raw 128 KiB device limit, that interface must be
measured before committing to a separate translation unit.

### Step 3 — the software serial converter (`SerUm*`), with care

The lowest layer: bit-banging the 50/75 baud current-loop line in the timer
interrupt. Eleven variables, and a natural module boundary — the sibling
repository already has a shared `Gemeinsam/SeriellUmsetz.c` for the other cards,
so there may be an opportunity to converge on it rather than keep a private
copy.

But 74 of its 108 references are inside `itelex_timerEvent`, the most
timing-sensitive code in the firmware, where a regression is a garbled
character on a real teleprinter. The simavr integration suite now gives this
step a useful CI gate: it executes the production callback and Timer0 driver on
an emulated ATmega1284P, checking the 900 Hz period, 50-baud transmit framing,
one receive waveform, and callback cycle headroom. That makes state-machine and
gross timing regressions visible before a bench test. It does not model the
current-loop electronics, oscillator tolerance, all competing interrupts, or
the ATmega2561 target, so this step still needs the real-hardware test plan.

### What to leave alone

- **`iTelexSocket*` and `SocketBearbeiten`.** 205 references across 14
  functions: this is the i-Telex protocol itself, not a module hiding inside
  one. It gets smaller by extracting *other* things, not by being moved.
- **The CGI handlers**, until the state they read has owners. Once
  RemoteServer, the self-call check and the soft serial each own their state
  and expose accessors, the handlers' dependency list shrinks on its own and
  the layer may become separable. That is a consequence of the other steps,
  not a step.

## Verification checklist for each step

1. `make test` — the host suites still pass (and cover the new module, if it is
   pure logic).
2. `make test-avr` — the emulated Timer0 and serial integration tests pass for
   any change touching the callback, its state, or the surrounding timer code.
3. `make clean && make standard light` — both variants build with no new
   warnings. Clean, because an incremental build after a header change
   under-reports size.
4. `avr-size` on both `.elf` files, compared against the numbers above. Record
   both in the commit message, and if Light grew, say by how much and where —
   `avr-nm` names the functions that changed.
5. For a verbatim move, diff the moved text against the original and confirm
   only whitespace and includes changed. For a rename-only commit, `avr-objdump
   -d` on the module's `.o` before and after should be byte-identical.
6. `doxygen Doxyfile.github` — no new warnings, and the new `\defgroup`
   appears. The count to beat is 244 on this tree with Doxygen 1.9; count
   before and after rather than trusting an absolute number, since it moves
   with the Doxygen version.
7. State plainly in the commit message what is *not* verified. Socket behavior,
   electrical timing, peripheral-interrupt interactions, and ATmega2561 timing
   remain unverified until someone runs the manual test plan in
   `docs/notes/Testumfang.txt` on real hardware.

## Open decisions

- **Module naming.** Settled for step 1: the module and its public API use the
  user-facing name `Centralex`; the former `RemoteServer` names remain
  searchable in `ehem.` comments.
- **How far to translate in one step.** Settled for steps 0 and 1 and worth
  keeping: rename the module's own identifiers and its callers in a separate
  commit after the verbatim move. In both steps the renamed module produced
  byte-identical machine instructions, making the result cheap to verify and
  review. A future module with more callers may still want the thin-macro
  alternative — old names kept as macros for one release — but nothing so far
  has needed it.
- **Whether flash headroom forces a Light-only decision.** Step 0 confirms
  extraction reliably costs flash, and shows the cost scales with call sites
  rather than with code moved. But `-flto` is worth more than all of it (see
  above), so the first question is now whether LTO survives a bench test, not
  which features to cut from Light. Compiling features out of the Light build
  remains the fallback, and stays a product decision needing Fred's view.

## Reproducing the measurements

```sh
make bootstrap
make clean          # incremental builds under-report the cost of a change
make standard light
avr-size --format=avr --mcu=atmega2561  build/standard/Main.elf
avr-size --format=avr --mcu=atmega1284p build/light/Main_Light.elf
```

To find out *where* a change spent its bytes rather than only how many,
compare per-symbol sizes across the two builds:

```sh
avr-nm --print-size --size-sort --radix=d build/light/Main_Light.elf
```

The anonymous `__c.NNNN` string literals renumber whenever line numbers move,
so ignore them; what matters is which named functions changed size.

The cluster and state tables above were produced by scripted analysis of
`iTelex/iTelex.c`: function spans by brace matching from each definition,
file-scope variables as the declarations outside those spans, and references
counted per span. The scripts were throwaway; the numbers are reproducible by
any equivalent means, and worth re-deriving rather than trusted if the file has
changed.
