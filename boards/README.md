# Development board configurations

These headers configure the upstream microwebserver project that the i-Telex
firmware's supporting stack (`apps/`, `hardware/`, `system/`) derives from, for
the development boards it was written against:

| File | Board |
| --- | --- |
| `AVRNETIO.config.h` | Pollin AVR-NET-IO |
| `myAVR.config.h` | myAVR board |
| `ATXM2.config.h` | ATxmega board |
| `XPLAIN.config.h` | Atmel XPLAIN |
| `OpenMCP.config.h` | OpenMCP |
| `UPP.config.h` | UPP |

**None of them describes an i-Telex card and none is used by any build here.**
The i-Telex builds use `iTelex.config.h` in the repository root; the `iTelex`
and `iTelex_Light` defines set by `GNUmakefile` select the Standard or Light
card within it.

These files are retained for reference, because they document which peripheral
options the shared code expects to find and are useful when tracing a setting
back to its origin. `../docs/workbooks/AvrNetIO-Anschluesse.xlsx` holds the
AvrNetIO pinout.
