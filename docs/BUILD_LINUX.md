# Building on Linux

Requirements: CMake ≥ 3.20, Ninja (or make), a C11 compiler, Python ≥ 3.9,
SDL2 development files (`libsdl2-dev`) for the frontend.

```bash
# recompiled build: translates your ROM + BIOS at configure time (keep private)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCYVERN_ROM=/path/to/cyvern.zip -DSKNS_BIOS=/path/to/skns.zip
cmake --build build
cp -r profiles build/bin/
./build/bin/Cyvern

# generic build: no ROM needed at build time, redistributable
cmake -S . -B build-generic -G Ninja -DCMAKE_BUILD_TYPE=Release -DCY_GENERIC_BUILD=ON
cmake --build build-generic
```

`scripts/build-local.sh cyvern.zip skns.zip` checks the ROMs, builds and runs
the smoke tests (`ctest`: boot, save-state round trip, 30,000-frame attract,
25,000-frame random play).

## Headless runner

`cyvern_headless --rom cyvern.zip --bios skns.zip --frames N` runs the game
without a window and prints progress. Options: `--png FILE`, `--png-every N`,
`--wav FILE`, `--dump-ram FILE`, `--dump-cache FILE`, `--dump-video FILE`,
`--hashes FILE` (per-frame RAM hash), `--inputs none|auto|random:N|hold:MASK`,
`--state-test FRAME --state-test-len N`, `--nvram-in/--nvram-out FILE`,
`--dips 0xNN`, `--watch ADDR` (print five RAM words per frame), `--strict`
(stop at untranslated code instead of using the interpreter). At exit it
reports how many instructions ran on the fallback interpreter and where they
started.

## Translation coverage

`tools/recompilesh2.py` finds code from the BIOS and game vector tables, from
branch/call targets, from code pointers in literal pools (iterated to a fixed
point) and from pointer tables, with a plausibility filter. Entry points that
only appear at run time (computed jumps, returns into code reached only by an
interrupt) are found with `tools/iterate_seeds.py cyvern.zip skns.zip`, which
runs the headless program with several input scripts and appends every
fallback entry address to `tools/sh2_seeds.txt` (addresses only, no ROM data).

## Release build (portable binary for SteamOS and older distributions)

The release binary links SDL2 statically and targets glibc 2.31 (Steam Runtime
"sniper" / Debian 11 baseline). SDL2 still loads X11, Wayland, PulseAudio
(PipeWire's pulse server on the Deck) and ALSA from the system at run time.
The packages were built with zig as the C compiler:

```bash
pip install ziglang
mkdir -p ~/zigcc && cat > ~/zigcc/cc <<'EOS'
#!/bin/sh
exec python3 -m ziglang cc -target x86_64-linux-gnu.2.31 -idirafter /usr/include -idirafter /usr/include/x86_64-linux-gnu "$@"
EOS
printf '#!/bin/sh\nexec python3 -m ziglang ar "$@"\n' > ~/zigcc/ar
printf '#!/bin/sh\nexec python3 -m ziglang ranlib "$@"\n' > ~/zigcc/ranlib
chmod +x ~/zigcc/*

# static SDL2 2.30.x (backends loaded dynamically)
CC=~/zigcc/cc cmake -S SDL2-2.30.9 -B sdl-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_AR=$HOME/zigcc/ar -DCMAKE_RANLIB=$HOME/zigcc/ranlib \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_STATIC_PIC=ON -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX=$PWD/sdl-static
cmake --build sdl-build && cmake --install sdl-build

CC=~/zigcc/cc cmake -S . -B build/linux-generic -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_AR=$HOME/zigcc/ar -DCMAKE_RANLIB=$HOME/zigcc/ranlib \
  -DSDL2_DIR=$PWD/sdl-static/lib/cmake/SDL2 -DCY_GENERIC_BUILD=ON
cmake --build build/linux-generic
APPIMAGETOOL=/path/to/appimagetool KIND=generic ./scripts/build-appimage.sh
```

For a private recompiled AppImage use `-DCYVERN_ROM=... -DSKNS_BIOS=...`
instead of `-DCY_GENERIC_BUILD=ON`, the build directory `build/linux-recompiled`
and `KIND=recompiled`.

Check: `ldd build/linux-generic/bin/Cyvern` lists only libc, libm,
libpthread, libdl; `objdump -T ... | grep GLIBC_` tops out at 2.29.

## Verified (2026-11-13, Ubuntu 24.04 container)

- Development build (GCC 13) and release builds (zig, glibc 2.31), recompiled
  and generic: per-frame RAM hashes identical over 5,000 frames of random play;
  save-state test passes.
- The frontend runs under Xvfb (Mesa llvmpipe) at the game's full rate.
- Not run on physical Linux desktop hardware or a Steam Deck.
