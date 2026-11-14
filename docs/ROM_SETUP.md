# ROM setup

Two MAME-format archives are needed. Nothing else (no CHD, no samples).

## `cyvern.zip` — MAME 0.264 set `cyvern` (US version)

| File | Size | CRC32 | Role |
|---|---|---|---|
| `cv-usa.u10` | 1 MiB | `1023ddca` | SH-2 program, even bytes |
| `cv-usa.u8` | 1 MiB | `f696f6be` | SH-2 program, odd bytes |
| `cv100-00.u24` | 4 MiB | `cd4ae88a` | sprites (RLE) |
| `cv101-00.u20` | 4 MiB | `a6cb3f0b` | sprites (RLE) |
| `cv200-00.u16` | 4 MiB | `ddc8c67e` | tilemap A tiles |
| `cv201-00.u13` | 4 MiB | `65863321` | tilemap A tiles |
| `cv210-00.u18` | 4 MiB | `7486bf3a` | tilemap B tiles |
| `cv300-00.u4` | 4 MiB | `fbeda465` | YMZ280B samples |

The Japanese set `cyvernj` (`cv-jpn.u10/u8`) is recognised and refused with a
message: the program differs and this build translates the US program.

## `skns.zip` — MAME set `skns` (Super Kaneko Nova System BIOS)

Only `sknsu1.u10` (USA BIOS, 512 KiB, CRC32 `384d21ec`) is used; the other
region BIOS files in the archive are ignored. The BIOS files may also be
merged into `cyvern.zip` (as some ROM managers do).

## How the files are found and checked

- **Frontend:** ROM Selection lists `.zip` files; pick `cyvern.zip`. The BIOS is
  looked for as `--bios PATH`, then the `SKNS_BIOS` environment variable, then
  `skns.zip` next to `cyvern.zip`, then inside `cyvern.zip`. At start-up the
  frontend also looks for `cyvern.zip` in the data folder, `~/Downloads` and
  `~/roms`.
- **Runtime:** every file's size and CRC32 (from the ZIP directory) is checked
  before anything is loaded; a missing, wrong or swapped file is named in the
  error message.
- **Tools / build:** `python3 tools/rom_set.py cyvern.zip skns.zip` checks
  sizes, CRC32 and SHA-1 of the extracted data.

## What the build does with your ROMs

A *recompiled* build reads the SH-2 program and BIOS at configure time and
writes C translations to `build/generated/`. That C is derived from your ROM:
keep `build/`, `dist/` and any recompiled package private, and never commit
them. A *generic* build (`-DCY_GENERIC_BUILD=ON`) does not read any ROM at
build time. Graphics, sound samples and the BIOS are always read from your
archives at run time; nothing is extracted to disk.
