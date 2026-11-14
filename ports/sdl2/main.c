/* Cyvern: The Dragon Weapons - native port, SDL2 frontend adapter.
 *
 * Connects the Super Kaneko Nova System board runtime (runtime/) and the
 * statically translated SH-2 program (build/generated/) to the shared
 * frontend (frontend/).  The game and BIOS ROM sets are read from the
 * player's archives at run time; no ROM data is built in. */
#include "fe.h"
#include "cy/board.h"
#include "cy/state.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CY_VERSION
#define CY_VERSION "0.1.0"
#endif

static cy_board *board;
static int loaded;
static uint8_t dip_bits = 0xFF;
static char bios_override[1024];

#define K(x) SDL_SCANCODE_##x
#define P(x) SDL_CONTROLLER_BUTTON_##x
/* MAME suprnova.cpp INPUT_PORTS_START(cyvern): two players, 8-way stick and
 * two buttons each (button 1 = shot, button 2 = bomb), start, two coin
 * slots, service coin, tilt and the test switch (port 400004 bit 0). */
static const fe_input_def INPUTS[] = {
    { "p1_up", "P1 Up", 1, CY_IN_P1_UP, K(UP), K(W), P(DPAD_UP), 0 },
    { "p1_down", "P1 Down", 1, CY_IN_P1_DOWN, K(DOWN), K(S), P(DPAD_DOWN), 0 },
    { "p1_left", "P1 Left", 1, CY_IN_P1_LEFT, K(LEFT), K(A), P(DPAD_LEFT), 0 },
    { "p1_right", "P1 Right", 1, CY_IN_P1_RIGHT, K(RIGHT), K(D), P(DPAD_RIGHT), 0 },
    { "p1_b1", "P1 Shot (Button 1)", 1, CY_IN_P1_B1, K(Z), K(J), P(A), 0 },
    { "p1_b2", "P1 Bomb (Button 2)", 1, CY_IN_P1_B2, K(X), K(K), P(B), 0 },
    { "p1_start", "P1 Start", 1, CY_IN_P1_START, K(RETURN), K(1), P(START), 0 },
    { "p2_up", "P2 Up", 2, CY_IN_P2_UP, K(I), -1, P(DPAD_UP), 0 },
    { "p2_down", "P2 Down", 2, CY_IN_P2_DOWN, K(COMMA), -1, P(DPAD_DOWN), 0 },
    { "p2_left", "P2 Left", 2, CY_IN_P2_LEFT, K(U), -1, P(DPAD_LEFT), 0 },
    { "p2_right", "P2 Right", 2, CY_IN_P2_RIGHT, K(O), -1, P(DPAD_RIGHT), 0 },
    { "p2_b1", "P2 Shot (Button 1)", 2, CY_IN_P2_B1, K(KP_1), -1, P(A), 0 },
    { "p2_b2", "P2 Bomb (Button 2)", 2, CY_IN_P2_B2, K(KP_2), -1, P(B), 0 },
    { "p2_start", "P2 Start", 2, CY_IN_P2_START, K(2), -1, P(START), 0 },
    /* coins are pulses, like a real coin switch */
    { "coin1", "Coin 1", 1, CY_IN_COIN1, K(5), -1, P(BACK), 5 },
    { "coin2", "Coin 2", 2, CY_IN_COIN2, K(6), -1, P(BACK), 5 },
    { "service1", "Service Coin", 0, CY_IN_SERVICE1, K(9), -1, -1, 5 },
    { "service", "Service", 0, CY_IN_SERVICE, K(0), -1, -1, 0 },
    { "tilt", "Tilt", 0, CY_IN_TILT, K(T), -1, -1, 0 },
    { "test", "Test / Service Mode", 0, CY_IN_TEST, K(F2), -1, -1, 0 },
};

/* Port 400004 bits 1-7 (MAME PORT_DIPNAME, active low).  Coinage, difficulty
 * and lives are set in the game's own test menu (F2) and kept in NVRAM. */
static const fe_dip_def DIPS[] = {
    { "flip_screen", "Flip Screen", 0, 0x02, 0x02, 2, { { "Off", 0x02 }, { "On", 0x00 } } },
    { "backup_ram", "Use Backup RAM", 0, 0x40, 0x40, 2, { { "Yes", 0x40 }, { "No", 0x00 } } },
    { "freeze", "Freeze", 0, 0x80, 0x80, 2, { { "Off", 0x80 }, { "On (freezes the game)", 0x00 } } },
};

/* --- ROM handling: the game archive plus the SKNS BIOS (skns.zip) --------- */

static void dir_of(const char *path, char *out, size_t n)
{
    snprintf(out, n, "%s", path);
    char *s1 = strrchr(out, '/'), *s2 = strrchr(out, '\\');
    char *s = s1 > s2 ? s1 : s2;
    if (s) s[1] = 0; else out[0] = 0;
}

static FILE *open_quiet(const char *p) { return fopen(p, "rb"); }

/* Order: --bios / SKNS_BIOS, then skns.zip next to the game archive.  The
 * BIOS files may also be merged into the game archive itself. */
static const char *find_bios(const char *game_zip, char *buf, size_t n)
{
    if (bios_override[0]) return bios_override;
    const char *env = getenv("SKNS_BIOS");
    if (env && *env) return env;
    char dir[1024];
    dir_of(game_zip, dir, sizeof(dir));
    static const char *names[] = { "skns.zip", "SKNS.zip", "SKNS.ZIP" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(buf, n, "%s%s", dir, names[i]);
        FILE *f = open_quiet(buf);
        if (f) { fclose(f); return buf; }
    }
    return NULL;
}

static int g_validate(const char *path, char *err, size_t n)
{
    char b[1100];
    return cy_rom_check(path, find_bios(path, b, sizeof(b)), err, n);
}

static void apply_dips(void) { board->dips = dip_bits; }

static int g_load(const char *path, char *err, size_t n)
{
    char b[1100];
    if (!board) board = (cy_board *)calloc(1, sizeof(cy_board));
    if (!board) { snprintf(err, n, "out of memory"); return 0; }
    if (loaded) { cy_board_free(board); loaded = 0; }
    if (!cy_board_load(board, path, find_bios(path, b, sizeof(b)), 'U')) { snprintf(err, n, "%s", board->error); return 0; }
    sh2_program_init();
    apply_dips();
    cy_board_reset(board);
    loaded = 1;
    return 1;
}

static void g_unload(void) { if (loaded) cy_board_free(board); loaded = 0; }
static void g_reset(void) { apply_dips(); cy_board_reset(board); }
static void g_set_dips(const uint16_t *w) { dip_bits = (uint8_t)(w[0] | 0x01); if (loaded) apply_dips(); }
static void g_set_inputs(uint32_t m) { cy_board_set_inputs(board, m); }

static int g_run(int16_t *audio, int *frames, char *err, size_t n)
{
    int rc = cy_board_run_frame(board, audio, frames);
    if (rc != CY_RUN_OK) {
        snprintf(err, n, "The SH-2 program stopped at address %08X (an instruction the runtime cannot execute).",
                 sh2_last_missing_pc());
        return 1;
    }
    return 0;
}

static void g_render32(uint32_t *px) { cy_video_render(board, px); }
static uint8_t *g_save(size_t *size) { return cy_state_save(board, size); }
static int g_validate_state(const uint8_t *d, size_t s, char *e, size_t n) { return cy_state_validate(board, d, s, e, n); }
static int g_load_state(const uint8_t *d, size_t s, char *e, size_t n) { return cy_state_load(board, d, s, e, n); }
static uint32_t g_fingerprint(void) { return loaded ? cy_board_rom_fingerprint(board) : 0; }

/* 8 KB battery-backed RAM at 0x00800000: bookkeeping, settings, high scores. */
static uint8_t nv_shadow[sizeof(((cy_board *)0)->nvram)];
static size_t g_nvram_save(uint8_t *buf, size_t max)
{
    if (!loaded || max < sizeof(board->nvram)) return 0;
    memcpy(buf, board->nvram, sizeof(board->nvram));
    memcpy(nv_shadow, board->nvram, sizeof(board->nvram));
    return sizeof(board->nvram);
}
static int g_nvram_load(const uint8_t *d, size_t size)
{
    if (!loaded || size != sizeof(board->nvram)) return 0;
    memcpy(board->nvram, d, size);
    memcpy(nv_shadow, d, size);
    return 1;
}
static int g_nvram_dirty(void) { return loaded && memcmp(nv_shadow, board->nvram, sizeof(nv_shadow)) != 0; }

static const fe_game GAME = {
    .id = "cyvern",
    .title = "Cyvern: The Dragon Weapons",
    .subtitle = "native port",
    .about =
        "Cyvern: The Dragon Weapons  (Kaneko, 1998, Super Kaneko Nova System)\n"
        "Native port " CY_VERSION ": the SH-2 game program and the SKNS BIOS are translated ahead\n"
        "of time to C; code the static analysis did not reach runs through a fallback SH-2\n"
        "interpreter built from the same instruction semantics. The board (ROZ tilemaps, RLE\n"
        "sprite generator, palette/mixer, YMZ280B, SH7604 timers/divider, RTC) is reimplemented.\n"
        "\n"
        "Requires your own MAME 'cyvern' ROM set and the 'skns' BIOS set (sknsu1.u10).\n"
        "No game data is distributed. Hardware behaviour references MAME (BSD-3-Clause).\n"
        "Frontend font: Inter (SIL Open Font License 1.1). SDL2 (zlib). miniz (MIT).\n"
        "This project's own code is GPL-2.0. See the LICENSES folder.",
    .rom_hint = "cyvern.zip",
    .width = CY_SCREEN_W,
    .height = CY_SCREEN_H,
    .display_aspect = 4.0 / 3.0,
    .rotation = 1,                       /* ROT90: vertical monitor */
    .refresh_hz = CY_REFRESH,
    .audio_rate = CY_AUDIO_RATE,
    .max_audio_frames = CY_MAX_FRAME_SAMPLES,
    .dip_words = 1,
    .players = 2,
    .inputs = INPUTS, .input_count = (int)(sizeof(INPUTS) / sizeof(INPUTS[0])),
    .dips = DIPS, .dip_count = (int)(sizeof(DIPS) / sizeof(DIPS[0])),
    .gadgets = NULL, .gadget_count = 0,
    .validate_rom = g_validate,
    .load = g_load,
    .unload = g_unload,
    .reset = g_reset,
    .set_dips = g_set_dips,
    .set_inputs = g_set_inputs,
    .run_frame = g_run,
    .render = NULL,
    .render32 = g_render32,
    .save_state = g_save,
    .validate_state = g_validate_state,
    .load_state = g_load_state,
    .rom_fingerprint = g_fingerprint,
    .runtime_version = CY_VERSION,
    .nvram_save = g_nvram_save,
    .nvram_load = g_nvram_load,
    .nvram_dirty = g_nvram_dirty,
    .profile_name = "cyvern",
};

int main(int argc, char **argv)
{
    /* --bios PATH is ours; everything else goes to the frontend */
    char **args = (char **)calloc((size_t)argc + 1, sizeof(char *));
    int n = 0;
    for (int i = 0; i < argc; i++) {
        if (i > 0 && !strcmp(argv[i], "--bios") && i + 1 < argc) { snprintf(bios_override, sizeof(bios_override), "%s", argv[++i]); continue; }
        args[n++] = argv[i];
    }
    int rc = fe_run(&GAME, n, args);
    if (loaded) cy_board_free(board);
    free(board);
    free(args);
    return rc;
}
