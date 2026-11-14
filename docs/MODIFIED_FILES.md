# Shared code and what changed

The frontend, packaging and conventions come from the earlier arcade
recompilation projects (Guardians / Denjin Makai II, Violent Storm).

| Path | Origin | Changes for Cyvern |
|---|---|---|
| `frontend/` | Violent Storm `frontend/` | `fe_game.rotation` (original monitor orientation) used as the default rotation; profiles may set `display.rotation`; window size, menu previews, slot thumbnails and screenshots follow the rotation; `--rotate N`; the data folder name drops characters Windows forbids (":"); audio help text made chip-neutral. All changes are backward compatible (`rotation` defaults to 0). |
| `frontend/assets/fe_font.h`, `tools/make_font.py` | unchanged | — |
| `runtime/third_party/miniz` | unchanged | — |
| `cmake/mingw-w64-x86_64.cmake` | unchanged | — |
| `scripts/*.sh`, `packaging/appimage/*` | Violent Storm | renamed; `KIND=generic|recompiled` selects the build and names private packages `...-PRIVATE` |
| `ports/headless/main.c` | Violent Storm headless runner | SKNS board API, `--bios`, `--dips`, `--nvram-in/out`, `--watch`, `--dump-cache`, `--strict`, fallback report; the state test snapshots before the frame's inputs are generated |
| `ports/sdl2/main.c` | Violent Storm adapter (structure) | new adapter: 2 players, 2 buttons, SKNS DIPs, BIOS discovery, NVRAM, vertical monitor |
| `runtime/src/state.c` | Violent Storm serializer (stream, sections, CRC, scratch-board validation) | new sections for the SKNS board |
| `tools/iterate_seeds.py` | Violent Storm | harvests fallback-interpreter entry points instead of stopping at the first missing address |

New for this project: `tools/sh2dec.py`, `tools/recompilesh2.py`,
`tools/make_sh2_interp.py`, `tools/rom_set.py`, `runtime/src/sh2_ops.c`
(converted from MAME `sh.cpp`), `sh2_runtime.c`, `sh2_interp.c`, `board.c`,
`video.c`, `ymz280b.c`, `sh2_program_none.c`.
