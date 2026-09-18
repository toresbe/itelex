# Documentation and project material

This directory collects the non-source material that accompanies the i-Telex
firmware. Almost all of it predates this GitHub clone and comes from Fred
Sonnenrein's original SourceForge SVN repository, so most of it is in German
and describes the project as it stood when it was written. It is kept for
reference and is not maintained.

Build instructions, firmware variants, and dependency notes live in the
[top-level README](../README.md), not here.

Three files here are exceptions to all of the above: they are maintained as
part of this clone rather than inherited from SVN.

## `mainpage.dox` — landing page of the generated documentation

Doxygen reads it as the `\mainpage` of the [published
documentation](https://toresbe.github.io/itelex/): what the firmware is, which
parts of the tree do what, and where to go next. The inherited introduction to
Dirk Broßwick's AVR mini webserver, which the supporting stack comes from,
stays in `main.c` as a subordinate page.

## `splitting-itelex-c.md` — notes on breaking up the largest source file

Handoff notes for the work of splitting `iTelex/iTelex.c`: what the file's
state coupling and the Light card's flash budget allow, the plan that follows
from it, and what each step has measured so far.

## `refactor-optimization.md` — notes on the second pass over that file

Working notes for the steps that make the big functions in `iTelex/iTelex.c`
readable and, where tests allow it, smaller, without moving state into new
modules. Records what each step measured and what its tests found.

## `notes/` — project notes and release material

| File | Contents |
| --- | --- |
| `Verbesserungen.txt` | Per-build changelog of firmware improvements. An identical copy ships to end users in `update/` and `update_light/`. |
| `updatemeldung.txt` | Circular telex sent to i-Telex users announcing a firmware release, in the flat lowercase style of a telex message. |
| `MeldungenUndKuerzel.txt` | The short service codes the firmware sends (`occ`, `nc`, `abs`, …) with their meanings. |
| `Testumfang.txt` | Manual test plan: the call cases, service messages, and configurations to exercise before a release. |
| `LastenheftTeilnehmerserver.docx` | Requirements specification for the i-Telex subscriber server (`Teilnehmerserver`), the directory service the firmware queries. |
| `NotizenInstallationTeilnehmerserverUnix.txt` | Installation notes for running a subscriber server on Unix. Describes a third-party Node.js implementation, not code in this repository. |

## `captures/` — recorded traces

| File | Contents |
| --- | --- |
| `MusterPOPEmpfang.log` | Sample POP3 session, captured as reference for the firmware's e-mail retrieval code in `iTelex/eMail.c`. |
| `twi-prot.txt` | Raw trace of TWI (I²C) backplane traffic, used when debugging `iTelex/SwTwi.c`. |

## `workbooks/` — Excel helper tools

These are the author's working spreadsheets. Several contain macros (`.xlsm`)
and expect Microsoft Excel; they are not part of any build.

| File | Contents |
| --- | --- |
| `AblaeufeVerbindung.xls` | State-machine overview of i-Telex call setup and teardown. The connection handling in `iTelex/iTelex.c` refers to this diagram. |
| `TlnVerzEditor.xls` | Editor for the local subscriber directory (`Teilnehmerverzeichnis`). |
| `TlnBuchHilfe.xls` | Helper for the subscriber book (`TlnBuch`) data format. |
| `TlnListeAuswerter.xlsm` | Analyses subscriber lists retrieved from the subscriber server. |
| `TlnServerTester.xlsm` | Exercises the subscriber server protocol for testing. |
| `AsciiArtHilfe.xlsm` | Builds the ASCII-art banners the firmware prints. |
| `AvrNetIO-Anschluesse.xlsx` | Pinout of the AvrNetIO development board (see `../boards/`), not of an i-Telex card. |

## `images/`

`PuttySetting1.png` and `PuttySetting2.png` show the PuTTY serial settings for
reaching the firmware's shell over the console port.

## `svn-era/` — historical version-stamping helpers

The firmware embeds its build number through a generated `SvnVersion.h`. Under
SVN this header was written by a TortoiseSVN post-update hook; the files here
are that mechanism, kept as a record of how the build numbers in
`Verbesserungen.txt` were produced.

| File | Contents |
| --- | --- |
| `README_SvnVersion.h` | Explains how to register `SvnAfterUpdate.bat` as a TortoiseSVN post-update action. |
| `SvnAfterUpdate.bat` | The hook itself: writes `SvnVersion.h` from the revision number SVN passes it. |
| `_SwitchSvn.bat` | Swaps between two SVN working-copy administrative directories, for a checkout tracking both the i-Telex and OpenMCP repositories. |

None of these run under Git. The portable build generates `SvnVersion.h` with
`scripts/gen-version-header.sh` instead, so nothing here needs to be set up to
build the firmware.
