# Splitting `iTelex/iTelex.c`

Handoff notes for the work of breaking up the firmware's largest file. Written
against commit `f019e7e`; every line number and measurement below is from that
commit, so re-check them before acting if the file has moved on.

Nothing in this plan has been implemented yet. The host unit tests
([`tests/`](../tests/README.md)) are in place and are the safety net for part
of it.

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
  loose symbols. The tree currently has 81 Doxygen warnings; a step should
  never add to them.

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

So every step is measured, and a step that grows the Light image is not
committed as it stands. `avr-size` output before and after belongs in the
commit message. If a step is worth keeping but costs flash, the honest options
are to mark the new module's entry points so the compiler can still see them
together, to compile the module out of the Light build entirely where the
feature is already absent, or to abandon that particular cut — not to spend the
last kilobyte on tidiness.

## What the file actually is

8,386 lines, 87 function definitions, 131 file-scope variables (91 `static`, 40
externally visible through `iTelex.h`).

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

### Step 0 — extract the pure helpers, and test them

The cheapest useful step, and the only one the host tests can cover directly.
These functions touch no file-scope state at all:

| Function | Line | Lines |
| --- | --- | --- |
| `ParseInt16` | 863 | 44 |
| `ParseNstAddresse` | 907 | 26 |
| `BaudrateErmitteln` | 946 | 68 |
| `IntelHexWriteLine` | 7722 | 21 |
| `low` / `high` | 808 / 813 | 5 / 6 |

`ParseSkipSpace` (933), `IsCommonAsciiControl` (3469) and `AdresseZuWahlStr`
(6958) are nearly pure — each appears to touch one variable — and should be
checked individually before being included.

Move them to `iTelex/Parsing.c` / `.h` (name open to preference), translated,
then add a `parsing` suite under `tests/suites/` following
`tests/README.md`. `BaudrateErmitteln` is worth testing on its own: it parses
the operator-facing baud-rate list (`70-79:75,*:50`), a small string format
that users get wrong and that nothing currently checks.

This step is small, is verified by tests rather than by inspection, and
establishes the two-commit pattern on low-risk code.

### Step 1 — extract RemoteServer (Centralex)

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

### Step 2 — extract the self-call check (`SelbstAnruf*`)

A self-contained watchdog: the interface periodically calls itself through the
network to prove it is still reachable, and reports a fault if it cannot.
About 8 variables and 113 references, but they live *inside* `itelex_thread`,
so this step means carving a coherent block out of a 1,397-line function
before it can move. Worth doing, and the natural way to start shrinking the
thread, but it is a genuine refactor rather than a relocation.

### Step 3 — the software serial converter (`SerUm*`), with care

The lowest layer: bit-banging the 50/75 baud current-loop line in the timer
interrupt. Eleven variables, and a natural module boundary — the sibling
repository already has a shared `Gemeinsam/SeriellUmsetz.c` for the other cards,
so there may be an opportunity to converge on it rather than keep a private
copy.

But 74 of its 108 references are inside `itelex_timerEvent`, the most
timing-sensitive code in the firmware, and none of it can be verified without
hardware: a regression here is a garbled character on a real teleprinter. Leave
it until the earlier steps have proved the pattern, and treat it as the step
that needs a bench test rather than a green CI run.

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
2. `make standard light` — both variants build with no new warnings.
3. `avr-size` on both `.elf` files, compared against the numbers above; the
   Light figure must not grow. Record both in the commit message.
4. For a verbatim move, diff the moved text against the original and confirm
   only whitespace and includes changed.
5. `doxygen Doxyfile.github` — no new warnings, and the new `\defgroup` appears.
6. State plainly in the commit message what is *not* verified: anything
   touching sockets, timers or the serial line is unverified until someone runs
   the manual test plan in `docs/notes/Testumfang.txt` on real hardware.

## Open decisions

- **Module naming.** `Centralex` is the name users and the i-Telex
  documentation use for the RemoteServer feature; `RemoteServer` is the name the
  code uses. Picking the user-facing name is the better documentation but makes
  the `ehem.` comments carry more weight.
- **How far to translate in one step.** A module's own identifiers are
  straightforward. Its *callers* elsewhere in `iTelex.c` have to be updated in
  the same commit for the build to work, which spreads a rename across the big
  file. An alternative is to keep the old names as thin macros for one release
  so the rename lands separately — more churn, smaller diffs.
- **Whether flash headroom forces a Light-only decision.** If extraction
  reliably costs flash, one clean answer is to compile whole features out of
  the Light build. That is a product decision about which cards get which
  features, not a refactoring one, and it needs Fred's view as much as anyone's.

## Reproducing the measurements

```sh
make bootstrap
make standard light
avr-size --format=avr --mcu=atmega2561  build/standard/Main.elf
avr-size --format=avr --mcu=atmega1284p build/light/Main_Light.elf
```

The cluster and state tables above were produced by scripted analysis of
`iTelex/iTelex.c`: function spans by brace matching from each definition,
file-scope variables as the declarations outside those spans, and references
counted per span. The scripts were throwaway; the numbers are reproducible by
any equivalent means, and worth re-deriving rather than trusted if the file has
changed.
