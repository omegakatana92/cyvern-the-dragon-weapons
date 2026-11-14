# Building for Windows 10/11 (x86-64)

Two routes produce the same program: a native MinGW build on Windows, or a
cross-compile from Linux.

## A. Native build on Windows (MSYS2)

1. Install MSYS2, open the "MSYS2 MINGW64" shell and run
   `pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2 mingw-w64-x86_64-python git`
2. Build:

```bash
git clone <this repo> && cd cyvern-the-dragon-weapons
# recompiled (private) build:
cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCYVERN_ROM=C:/path/to/cyvern.zip -DSKNS_BIOS=C:/path/to/skns.zip
# or the generic build: -DCY_GENERIC_BUILD=ON instead of the two ROM paths
cmake --build build/cmake
cp /mingw64/bin/SDL2.dll build/cmake/bin/
cp -r profiles build/cmake/bin/
./build/cmake/bin/Cyvern-Windows-x86_64.exe
```

The SDL2 "mingw" development package also works; pass
`-DSDL2_DIR=<SDL2>\x86_64-w64-mingw32\lib\cmake\SDL2`.

## B. Cross-compile from Linux

```bash
sudo apt install gcc-mingw-w64-x86-64 cmake ninja-build python3
# SDL2 2.30.x mingw development package from https://github.com/libsdl-org/SDL/releases
tar xzf SDL2-devel-2.30.9-mingw.tar.gz
cmake -S . -B build/win64-generic -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
  -DSDL2_DIR=$PWD/SDL2-2.30.9/x86_64-w64-mingw32/lib/cmake/SDL2 \
  -DCY_GENERIC_BUILD=ON
cmake --build build/win64-generic
SDL2_DLL=$PWD/SDL2-2.30.9/x86_64-w64-mingw32/bin/SDL2.dll KIND=generic ./scripts/package-windows.sh
```

The package (`dist/Cyvern-Windows-x86_64.zip`) contains the executable,
`SDL2.dll`, `profiles/cyvern.json`, licences and docs. The executable is
statically linked against libgcc; it needs only system DLLs and `SDL2.dll`.
For a recompiled package use `-DCYVERN_ROM=... -DSKNS_BIOS=...`, the build
directory `build/win64-recompiled` and `KIND=recompiled` (the zip is then named
`...-PRIVATE.zip`: do not share it).

Settings, saves and NVRAM go to `%APPDATA%\OmegaKatana\Cyvern - The Dragon Weapons\`
(or beside the executable with `--portable` / a `portable.txt` file).

## Verified (2026-11-13)

- Cross-compiled with MinGW-w64 GCC 13. Under Wine 9: the headless runner
  (recompiled and generic) gives per-frame RAM hashes identical to the Linux
  builds over 5,000 frames of random play.
- **Not run on a Windows 10/11 PC.**
