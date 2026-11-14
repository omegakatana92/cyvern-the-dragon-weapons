/* Super Kaneko Nova System board model (Cyvern: The Dragon Weapons).
 *
 * Reference: MAME 0.264 src/mame/kaneko/suprnova.cpp, suprnova_v.cpp,
 * sknsspr.cpp, src/devices/sound/ymz280b.cpp and src/devices/cpu/sh/sh7604.cpp
 * (behaviour only; none of it is linked). */
#ifndef CY_BOARD_H
#define CY_BOARD_H

#include <stddef.h>
#include <stdint.h>
#include "sh2/sh2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Screen: 340 x 262 total (set_size), 320 x 240 visible, 59.5971 Hz (measured).
 * The monitor is rotated 90 degrees (vertical game). */
#define CY_SCREEN_W 320
#define CY_SCREEN_H 240
#define CY_LINES 262
#define CY_REFRESH 59.5971
#define CY_CPU_CLOCK 28636000u
#define CY_YMZ_CLOCK 16666666u                   /* 33.3333 MHz / 2 */
#define CY_AUDIO_RATE (CY_YMZ_CLOCK / 192.0)      /* YMZ280B internal rate, 86805.6 Hz */
#define CY_MAX_FRAME_SAMPLES 2048

enum {
    CY_IN_P1_UP = 1u << 0, CY_IN_P1_DOWN = 1u << 1, CY_IN_P1_LEFT = 1u << 2, CY_IN_P1_RIGHT = 1u << 3,
    CY_IN_P1_B1 = 1u << 4, CY_IN_P1_B2 = 1u << 5, CY_IN_P1_START = 1u << 7,
    CY_IN_P2_UP = 1u << 8, CY_IN_P2_DOWN = 1u << 9, CY_IN_P2_LEFT = 1u << 10, CY_IN_P2_RIGHT = 1u << 11,
    CY_IN_P2_B1 = 1u << 12, CY_IN_P2_B2 = 1u << 13, CY_IN_P2_START = 1u << 15,
    CY_IN_COIN1 = 1u << 16, CY_IN_COIN2 = 1u << 17, CY_IN_SERVICE = 1u << 18, CY_IN_TILT = 1u << 19,
    CY_IN_SERVICE1 = 1u << 20, CY_IN_TEST = 1u << 21
};

typedef struct cy_ymz_voice {
    uint8_t playing, ended, keyon, looping, mode;
    uint16_t fnum;
    uint8_t level, pan;
    uint32_t start, stop, loop_start, loop_end, position;
    int32_t signal, step, loop_signal, loop_step;
    uint32_t loop_count;
    int32_t output_left, output_right, output_step, output_pos;
    int16_t last_sample, curr_sample;
    uint8_t irq_schedule;
} cy_ymz_voice;

typedef struct cy_ymz {
    uint8_t current_register, status_register, irq_state, irq_mask, irq_enable, keyon_enable, ext_mem_enable;
    uint8_t ext_readlatch;
    uint32_t ext_mem_address_hi, ext_mem_address_mid, ext_mem_address;
    cy_ymz_voice voice[8];
} cy_ymz;

typedef struct cy_hit {
    uint16_t x1p, y1p, z1p, x1s, y1s, z1s, x2p, y2p, z2p, x2s, y2s, z2s, org;
    uint16_t x1_p1, x1_p2, y1_p1, y1_p2, z1_p1, z1_p2, x2_p1, x2_p2, y2_p1, y2_p2, z2_p1, z2_p2;
    uint16_t x1tox2, y1toy2, z1toz2;
    int16_t x_in, y_in, z_in;
    uint16_t flag;
    uint8_t disconnect;
} cy_hit;

/* SH7604 on-chip modules used by the board software */
typedef struct cy_onchip {
    uint8_t regs[0x200];               /* 0xFFFFFE00-0xFFFFFFFF raw register file */
    uint32_t dvsr, dvdntl, dvdnth;
    uint8_t divu_ovf, divu_ovfie;
    /* FRT (free-running timer), after MAME sh7604_device::sh2_timer_* */
    uint8_t tier, ftcsr, frc_tcr, tocr;
    uint16_t frc, ocra, ocrb;
    uint64_t frc_base;                 /* CPU cycle the FRC count was last synchronised at */
    uint64_t frt_event;                /* CPU cycle of the next FRT event (UINT64_MAX: none) */
} cy_onchip;

typedef struct cy_board {
    sh2_state cpu;

    uint8_t *bios;          /* 0x80000 */
    uint8_t *game;          /* 0x200000 */
    uint8_t *sprite_rom;    /* 0x800000 RLE sprites */
    uint8_t *tiles_a;       /* 0x800000 */
    uint8_t *tiles_b;       /* 0x800000; first 0x40000 are RAM-based tiles (v3t) */
    uint8_t *samples;       /* 0x400000 */

    uint8_t main_ram[0x100000];
    uint8_t cache_ram[0x1000];
    uint8_t nvram[0x2000];
    uint8_t spriteram[0x4000];
    uint8_t spc_regs[0x40];
    uint8_t v3_regs[0x80];
    uint8_t tilemap_a[0x4000];
    uint8_t tilemap_b[0x4000];
    uint8_t v3slc[0x8000];
    uint8_t pal_regs[0x20];
    uint8_t palette_ram[0x20000];
    uint8_t v3t_ram[0x40000];
    uint8_t rtc[16];

    /* palette pens as MAME computes them (brightness applied at write time) */
    uint32_t pens[32768];
    uint8_t use_spc_bright, use_v3_bright, spc_changed, v3_changed, palette_updated;
    uint8_t bright_spc_r, bright_spc_g, bright_spc_b, bright_spc_r_trans, bright_spc_g_trans, bright_spc_b_trans;
    uint8_t bright_v3_r, bright_v3_g, bright_v3_b, bright_v3_r_trans, bright_v3_g_trans, bright_v3_b_trans;
    uint8_t alt_enable_background, alt_enable_sprites;
    uint16_t sprite_bitmap[CY_SCREEN_H * CY_SCREEN_W];   /* persists between frames (trails) */
    uint32_t screen[CY_SCREEN_H * CY_SCREEN_W];          /* last composed picture (xRGB8888) */

    cy_hit hit;
    cy_ymz ymz;
    cy_onchip onchip;

    uint32_t inputs;        /* active-high CY_IN_* */
    uint8_t dips;           /* 0x400004 bits 0-7 (active low) */
    char region;            /* 'U' */
    uint32_t rng;
    int64_t rtc_base_time;  /* seconds since epoch when the board was powered on */
    uint64_t lines_run;
    uint16_t vpos;
    uint16_t frame_end_line;
    uint64_t frames_run;
    uint64_t next_int15, next_int11, next_int9;   /* CPU cycle of the next periodic interrupt */
    uint32_t audio_phase;   /* audio sample accumulator (fixed point) */
    int idle_skip;          /* MAME's cyvern idle-loop speedup */

    char error[512];
} cy_board;

int  cy_board_load(cy_board *b, const char *game_zip, const char *bios_zip, char bios_region);
int  cy_rom_check(const char *game_zip, const char *bios_zip, char *err, size_t n);
void cy_board_free(cy_board *b);
void cy_board_reset(cy_board *b);
void cy_board_attach(cy_board *b);
void cy_board_set_inputs(cy_board *b, uint32_t active_high);

#define CY_RUN_OK 0
#define CY_RUN_MISSING 1
int cy_board_run_frame(cy_board *b, int16_t *stereo, int *frames);

/* video.c: cy_video_compose builds the picture at VBLANK (unrotated, as the
 * board outputs it); cy_video_render copies the last composed picture. */
void cy_video_compose(cy_board *b);
void cy_video_render(cy_board *b, uint32_t *pixels);
void cy_draw_sprites(cy_board *b);
void cy_palette_reg_write(cy_board *b, unsigned word);
void cy_palette_ram_write(cy_board *b, unsigned word);

/* ymz280b.c */
void cy_ymz_reset(cy_ymz *z);
uint8_t cy_ymz_read(cy_board *b, unsigned offset);
void cy_ymz_write(cy_board *b, unsigned offset, uint8_t data);
void cy_ymz_render(cy_board *b, int32_t *left, int32_t *right, int samples);

#ifdef __cplusplus
}
#endif
#endif
