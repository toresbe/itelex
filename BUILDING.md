# Building i-Telex firmware

The repository uses one portable GNU Make build for the Standard firmware,
Light firmware, and bootloader. Build output is kept below `build/`.

## Dependencies

On Debian 13:

```sh
sudo apt install make gcc-avr binutils-avr avr-libc gawk subversion git
make bootstrap
```

`bootstrap` initializes the pinned `itelex-misc` Git submodule and exports
revision 13 of Fred Sonnenrein's AVR-Clibs from its upstream SourceForge SVN
repository. AVR-Clibs is not copied here because its upstream project does not
declare a license. The exact URL and revision are pinned in `GNUmakefile`.

## Targets

```sh
make standard    # ATmega2561, build/standard/Main.*
make light       # ATmega1284P, build/light/Main_Light.*
make fastboot    # build/fastboot/bootload.*
make all
make clean
```

Set `AVR_TOOLCHAIN_ROOT` if the AVR tools are not on `PATH`:

```sh
make AVR_TOOLCHAIN_ROOT='/opt/microchip/avr8-gnu-toolchain' all
```

## Microchip Studio

In **Project Properties → Build**, enable **Use External Makefile** and select
`GNUmakefile`. Use `standard` or `light` as the build target and the matching
`clean-standard` or `clean-light` clean target. The ELF output contains DWARF-2
debug information for source-level debugging.
