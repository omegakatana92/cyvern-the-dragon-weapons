# Cyvern: The Dragon Weapons — native port (static recompilation)

A native Windows / Linux / Steam Deck build of Kaneko's 1998 arcade shooter
*Cyvern: The Dragon Weapons* (MAME set `cyvern`, US version) on the Super
Kaneko Nova System (SKNS). The SH-2 game program and the SKNS BIOS are
translated ahead of time into C, and the board is reimplemented around them.

You need your own `cyvern.zip` **and** the SKNS BIOS `skns.zip` (the USA BIOS
file `sknsu1.u10`). No ROM data, BIOS, extracted graphics or sound is in this
repository or in any package built from it.

## What this is (and is not)

| Technique | Used here? |
|---|---|
| **Static recompilation** — every SH-2 instruction the analysis can reach (vector tables, call/branch targets, literal-pool code pointers, pointer tables, entry points found by running the game) is translated to a C function at build time (`tools/recompilesh2.py`) and linked into the executable. | **Yes**, in the *recompiled* build (~154,000 instructions of game + BIOS code). |
| **Decompilation** — readable, hand-named C source of the game logic. | No. The generated C is mechanical, one function per instruction. |
| **Dynamic recompilation (JIT)** | No. Nothing is translated at run time. |
| **Interpretation** of the CPU | A fallback SH-2 interpreter (`runtime/src/sh2_interp.c`) built from the *same* decoder and instruction semantics. In the recompiled build it only runs code the static analysis missed (in testing: none after seed discovery). The **generic** build uses it for everything — see below. |
| **Hardware reimplementation** of video, sound and I/O | Yes: two ROZ tilemaps with line/column scroll, the RLE/zooming sprite generator, palette brightness and the sprite/background mixer, the YMZ280B, the SH7604 on-chip DIVU and free-running timer, RTC, the "hit" math unit. MAME 0.264 is the behavioural reference (no MAME code is linked). |

### Two kinds of build

| Build | Contains | Redistributable? |
|---|---|---|
| **Recompiled** (`-DCYVERN_ROM=... -DSKNS_BIOS=...`) | C translated from *your* ROM and BIOS | **No — keep it private.** It is code derived from copyrighted program ROMs. |
| **Generic** (`-DCY_GENERIC_BUILD=ON`) | no ROM-derived code; the SH-2 runs on the fallback interpreter | Yes. Public release packages are generic builds. |

Both use the same board runtime and frontend, load your ROMs at run time and
produce bit-identical output (checked: identical per-frame RAM hashes over
5,000 frames of random play, and identical screenshots). The generic build is
about 15 % slower in the headless runner; both run far above the game's rate.

## Status

| Area | Status |
|---|---|
| ROM + BIOS loading and validation (size/CRC32 in the runtime, SHA-1 in `tools/rom_set.py`; the Japanese set is recognised and refused) | implemented + tested |
| SH-2 translation (game + BIOS) | implemented + tested; 30,000-frame runs with nine input scripts reach no untranslated code after seed discovery |
| Boot, BIOS POST, attract mode | implemented + tested — screen **pixel-identical to MAME 0.264 snapshots at frames 60–1200**; later attract frames show the same scenes about one frame out of phase (see Known issues) |
| Graphics (ROZ tilemaps, line/column scroll, sprites incl. zoom, priorities, additive sprites, brightness) | implemented + tested against MAME snapshots |
| Flip Screen DIP | implemented + tested — pixel-identical to MAME with the DIP on |
| Audio (YMZ280B, ADPCM/PCM, driven by the SH7604 FRT interrupt) | implemented + tested against MAME's recording: loudness envelope correlation 0.998, level within 0.5 %; not sample-identical |
| Gameplay | tested with scripted/random inputs through several stages (headless); **not played by a person** |
| Save states, autosave, rewind, input movies | implemented + tested (600-frame headless round trip; corrupted/truncated states rejected) |
| NVRAM (8 KB backup RAM) persistence | implemented + tested (written and restored by the frontend) |
| Frontend (menus, vertical display, controllers, remapping) | implemented; tested under Xvfb on Linux; **controllers, Steam Deck and a Windows PC not tested** |
| Windows x64 packages | built (MinGW); headless runner verified under Wine; **not run on Windows 10/11** |
| Linux / Steam Deck AppImage | built (glibc ≥ 2.29, static SDL2); **not run on a Steam Deck** |
| Gadgets | frontend gadgets (FPS, frame time, input display). No game-value gadgets: no RAM address for score/lives has been verified yet |

## Quick start

1. Get the package for your platform (or build it, see below).
2. Put `cyvern.zip` and `skns.zip` in the same folder.
3. Start the program and choose **ROM Selection**, then pick `cyvern.zip`
   (keyboard, mouse or controller). Both archives are checked; `skns.zip` is
   found next to it (or pass `--bios PATH`, or set `SKNS_BIOS`).
4. **Start Game**. Coin = `5`, P1 Start = `Enter`, Shot/Bomb = `Z`/`X`.
   `Esc` (or Back + Start on a controller) opens the quick menu. The picture is
   shown vertically, as on the arcade monitor. See [docs/CONTROLS.md](docs/CONTROLS.md).

## Building

```bash
git clone <this repo> && cd cyvern-the-dragon-weapons
# recompiled build (needs your ROMs at configure time; keep the result private)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCYVERN_ROM=/path/to/cyvern.zip -DSKNS_BIOS=/path/to/skns.zip
cmake --build build
./build/bin/Cyvern                     # frontend
./build/bin/cyvern_headless --rom /path/to/cyvern.zip --bios /path/to/skns.zip --frames 3000 --png shot.png

# generic build (no ROM needed to build)
cmake -S . -B build-generic -G Ninja -DCMAKE_BUILD_TYPE=Release -DCY_GENERIC_BUILD=ON
cmake --build build-generic
```

The CMake files are distributed in the source package attached to each release
(they are not kept in the git repository). Details:
[docs/BUILD_LINUX.md](docs/BUILD_LINUX.md), [docs/BUILD_WINDOWS.md](docs/BUILD_WINDOWS.md),
[docs/STEAM_DECK.md](docs/STEAM_DECK.md).

## Documentation

- [docs/ROM_SETUP.md](docs/ROM_SETUP.md) — which files are needed and how they are checked
- [docs/CONTROLS.md](docs/CONTROLS.md) — default mappings, service/test
- [docs/FRONTEND.md](docs/FRONTEND.md) — menus, display/audio options, profiles, gadgets
- [docs/SAVE_STATES.md](docs/SAVE_STATES.md) — format and guarantees
- [docs/HARDWARE.md](docs/HARDWARE.md) — the board, the translator and how it was verified
- [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md)
- [docs/MODIFIED_FILES.md](docs/MODIFIED_FILES.md) — what is shared with the other arcade recompilation projects

## Licences

This project's own code is GPL-2.0 (`LICENSE`). The SH-2 instruction semantics
(`runtime/src/sh2_ops.c`) are converted from MAME's `sh.cpp` and the board
models follow MAME's drivers (BSD-3-Clause, see `LICENSES/`). SDL2 (zlib),
miniz (MIT), Inter font (OFL). *Cyvern* is © Kaneko; this project is not
affiliated with or endorsed by Kaneko.
