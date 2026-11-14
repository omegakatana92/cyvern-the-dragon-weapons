#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
/* Super Kaneko Nova System board: memory map, I/O, the "hit" protection/math
 * unit, SH7604 on-chip modules, interrupts and the scanline scheduler.
 * Behaviour follows MAME 0.264 kaneko/suprnova.cpp and cpu/sh/sh7604.cpp. */
#include "cy/board.h"
#include "miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include <time.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

static cy_board *B;   /* the board the SH-2 bus talks to */

/* ------------------------------------------------------------------ ROMs */

typedef struct rom_entry { const char *name; uint32_t size, crc; const char *role; } rom_entry;

static const rom_entry GAME_FILES[] = {
    { "cv-usa.u10", 0x100000, 0x1023DDCA, "SH-2 program (even)" },
    { "cv-usa.u8", 0x100000, 0xF696F6BE, "SH-2 program (odd)" },
    { "cv100-00.u24", 0x400000, 0xCD4AE88A, "sprites" },
    { "cv101-00.u20", 0x400000, 0xA6CB3F0B, "sprites" },
    { "cv200-00.u16", 0x400000, 0xDDC8C67E, "tilemap A tiles" },
    { "cv201-00.u13", 0x400000, 0x65863321, "tilemap A tiles" },
    { "cv210-00.u18", 0x400000, 0x7486BF3A, "tilemap B tiles" },
    { "cv300-00.u4", 0x400000, 0xFBEDA465, "YMZ280B samples" },
};

typedef struct bios_entry { char region; const char *name; uint32_t crc; } bios_entry;
static const bios_entry BIOS_FILES[] = {
    { 'U', "sknsu1.u10", 0x384D21EC }, { 'J', "sknsj1.u10", 0x7E2B836C }, { 'E', "sknse2.u10", 0xE2B9D7D1 },
    { 'A', "sknsa1.u10", 0x745E5212 }, { 'K', "sknsk1.u10", 0xFF1C9F79 },
};

static int zip_find(mz_zip_archive *z, const char *name)
{
    int n = (int)mz_zip_reader_get_num_files(z);
    for (int i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(z, (mz_uint)i, &st)) continue;
        const char *base = strrchr(st.m_filename, '/');
        base = base ? base + 1 : st.m_filename;
        if (!strcasecmp(base, name)) return i;
    }
    return -1;
}

static void *zip_extract(mz_zip_archive *z, const char *name, size_t *size)
{
    int i = zip_find(z, name);
    if (i < 0) return NULL;
    return mz_zip_reader_extract_to_heap(z, (mz_uint)i, size, 0);
}

static int open_zip(mz_zip_archive *z, const char *path)
{
    memset(z, 0, sizeof(*z));
    return path && *path && mz_zip_reader_init_file(z, path, 0);
}

static const bios_entry *bios_for(char region)
{
    for (size_t i = 0; i < sizeof(BIOS_FILES) / sizeof(BIOS_FILES[0]); i++)
        if (BIOS_FILES[i].region == region) return &BIOS_FILES[i];
    return &BIOS_FILES[0];
}

int cy_rom_check(const char *game_zip, const char *bios_zip, char *err, size_t n)
{
    mz_zip_archive g, s;
    if (!open_zip(&g, game_zip)) { snprintf(err, n, "Cannot open the game archive \"%s\".", game_zip ? game_zip : ""); return 0; }
    if (zip_find(&g, "cv-jpn.u10") >= 0) {
        snprintf(err, n, "This is the Japanese set (cyvernj). This build is translated from the US set 'cyvern' (cv-usa.u10/u8).");
        mz_zip_reader_end(&g);
        return 0;
    }
    char missing[400] = "", bad[400] = "";
    for (size_t i = 0; i < sizeof(GAME_FILES) / sizeof(GAME_FILES[0]); i++) {
        int idx = zip_find(&g, GAME_FILES[i].name);
        mz_zip_archive_file_stat st;
        if (idx < 0 || !mz_zip_reader_file_stat(&g, (mz_uint)idx, &st)) {
            strncat(missing, " ", sizeof(missing) - strlen(missing) - 1);
            strncat(missing, GAME_FILES[i].name, sizeof(missing) - strlen(missing) - 1);
        } else if (st.m_uncomp_size != GAME_FILES[i].size || st.m_crc32 != GAME_FILES[i].crc) {
            strncat(bad, " ", sizeof(bad) - strlen(bad) - 1);
            strncat(bad, GAME_FILES[i].name, sizeof(bad) - strlen(bad) - 1);
        }
    }
    mz_zip_reader_end(&g);
    if (missing[0] || bad[0]) {
        snprintf(err, n, "Not the MAME 'cyvern' set.%s%s%s%s", missing[0] ? " Missing:" : "", missing,
                 bad[0] ? " Wrong size/CRC:" : "", bad);
        return 0;
    }
    /* the BIOS may sit in its own skns.zip or inside a merged archive */
    const bios_entry *be = bios_for('U');
    int found = 0;
    const char *paths[2] = { bios_zip, game_zip };
    for (int p = 0; p < 2 && !found; p++) {
        if (!open_zip(&s, paths[p])) continue;
        int idx = zip_find(&s, be->name);
        mz_zip_archive_file_stat st;
        if (idx >= 0 && mz_zip_reader_file_stat(&s, (mz_uint)idx, &st)) {
            if (st.m_uncomp_size != 0x80000 || st.m_crc32 != be->crc) {
                snprintf(err, n, "%s in the BIOS archive has the wrong size or CRC.", be->name);
                mz_zip_reader_end(&s);
                return 0;
            }
            found = 1;
        }
        mz_zip_reader_end(&s);
    }
    if (!found) {
        snprintf(err, n, "The Super Kaneko Nova System BIOS (MAME 'skns' set, file %s) was not found. "
                         "Put skns.zip next to cyvern.zip or select it.", be->name);
        return 0;
    }
    return 1;
}

int cy_board_load(cy_board *b, const char *game_zip, const char *bios_zip, char bios_region)
{
    memset(b, 0, sizeof(*b));
    b->region = bios_region ? bios_region : 'U';
    if (!cy_rom_check(game_zip, bios_zip, b->error, sizeof(b->error))) return 0;
    b->bios = (uint8_t *)calloc(1, 0x80000);
    b->game = (uint8_t *)calloc(1, 0x200000);
    b->sprite_rom = (uint8_t *)calloc(1, 0x800000);
    b->tiles_a = (uint8_t *)calloc(1, 0x800000);
    b->tiles_b = (uint8_t *)calloc(1, 0x800000);
    b->samples = (uint8_t *)calloc(1, 0x400000);
    if (!b->bios || !b->game || !b->sprite_rom || !b->tiles_a || !b->tiles_b || !b->samples) {
        snprintf(b->error, sizeof(b->error), "out of memory");
        return 0;
    }
    memset(b->nvram, 0xFF, sizeof(b->nvram));   /* NVRAM default: all 1s (no backup yet) */
    mz_zip_archive g;
    open_zip(&g, game_zip);
    size_t sz = 0;
    uint8_t *ev = (uint8_t *)zip_extract(&g, "cv-usa.u10", &sz), *od = (uint8_t *)zip_extract(&g, "cv-usa.u8", &sz);
    for (size_t i = 0; ev && od && i < 0x100000; i++) { b->game[i * 2] = ev[i]; b->game[i * 2 + 1] = od[i]; }
    mz_free(ev); mz_free(od);
    struct { const char *name; uint8_t *dst; } loads[] = {
        { "cv100-00.u24", b->sprite_rom }, { "cv101-00.u20", b->sprite_rom + 0x400000 },
        { "cv200-00.u16", b->tiles_a }, { "cv201-00.u13", b->tiles_a + 0x400000 },
        { "cv210-00.u18", b->tiles_b + 0x400000 }, { "cv300-00.u4", b->samples },
    };
    for (size_t i = 0; i < sizeof(loads) / sizeof(loads[0]); i++) {
        uint8_t *d = (uint8_t *)zip_extract(&g, loads[i].name, &sz);
        if (d) memcpy(loads[i].dst, d, sz > 0x400000 ? 0x400000 : sz);
        mz_free(d);
    }
    mz_zip_reader_end(&g);
    const bios_entry *be = bios_for(b->region);
    const char *paths[2] = { bios_zip, game_zip };
    for (int p = 0; p < 2; p++) {
        mz_zip_archive s;
        if (!open_zip(&s, paths[p])) continue;
        uint8_t *d = (uint8_t *)zip_extract(&s, be->name, &sz);
        mz_zip_reader_end(&s);
        if (d) { memcpy(b->bios, d, sz > 0x80000 ? 0x80000 : sz); mz_free(d); break; }
    }
    cy_board_reset(b);
    return 1;
}

void cy_board_free(cy_board *b)
{
    free(b->bios); free(b->game); free(b->sprite_rom); free(b->tiles_a); free(b->tiles_b); free(b->samples);
    b->bios = b->game = b->sprite_rom = b->tiles_a = b->tiles_b = b->samples = NULL;
}

void cy_board_attach(cy_board *b) { B = b; }
void cy_board_set_inputs(cy_board *b, uint32_t m) { b->inputs = m; }

/* ---------------------------------------------------------------- helpers */

static inline uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* MAME running_machine::rand() (used by the hit unit's random register) */
static uint32_t machine_rand(cy_board *b)
{
    b->rng = 1664525u * b->rng + 1013904223u;
    return (b->rng >> 16) | (b->rng << 16);
}

/* -------------------------------------------------------------- hit unit */

static void hit_calc_orig(uint16_t p, uint16_t s, uint16_t org, uint16_t *l, uint16_t *r)
{
    switch (org & 3) {
    case 0: *l = p; *r = (uint16_t)(p + s); break;
    case 1: *l = (uint16_t)(p - s / 2); *r = (uint16_t)(*l + s); break;
    case 2: *l = (uint16_t)(p - s); *r = p; break;
    case 3: *l = (uint16_t)(p - s); *r = (uint16_t)(p + s); break;
    }
}

static void hit_calc_axis(uint16_t x1p, uint16_t x1s, uint16_t x2p, uint16_t x2s, uint16_t org,
                          uint16_t *x1_p1, uint16_t *x1_p2, uint16_t *x2_p1, uint16_t *x2_p2, int16_t *x_in, uint16_t *x1tox2)
{
    uint16_t x1l = 0, x1r = 0, x2l = 0, x2r = 0;
    hit_calc_orig(x1p, x1s, org, &x1l, &x1r);
    hit_calc_orig(x2p, x2s, (uint16_t)(org >> 8), &x2l, &x2r);
    *x1tox2 = (uint16_t)(x2p - x1p);
    *x1_p1 = x1p; *x2_p1 = x2p; *x1_p2 = x1r; *x2_p2 = x2l;
    *x_in = (int16_t)(x1r - x2l);
}

static void hit_recalc(cy_hit *h)
{
    hit_calc_axis(h->x1p, h->x1s, h->x2p, h->x2s, h->org, &h->x1_p1, &h->x1_p2, &h->x2_p1, &h->x2_p2, &h->x_in, &h->x1tox2);
    hit_calc_axis(h->y1p, h->y1s, h->y2p, h->y2s, h->org, &h->y1_p1, &h->y1_p2, &h->y2_p1, &h->y2_p2, &h->y_in, &h->y1toy2);
    hit_calc_axis(h->z1p, h->z1s, h->z2p, h->z2s, h->org, &h->z1_p1, &h->z1_p2, &h->z2_p1, &h->z2_p2, &h->z_in, &h->z1toz2);
    uint16_t f = 0;
    f |= h->y2p > h->y1p ? 0x8000 : h->y2p == h->y1p ? 0x4000 : 0x2000;
    f |= h->y_in >= 0 ? 0 : 0x1000;
    f |= h->x2p > h->x1p ? 0x0800 : h->x2p == h->x1p ? 0x0400 : 0x0200;
    f |= h->x_in >= 0 ? 0 : 0x0100;
    f |= h->z2p > h->z1p ? 0x0080 : h->z2p == h->z1p ? 0x0040 : 0x0020;
    f |= h->z_in >= 0 ? 0 : 0x0010;
    f |= h->x_in >= 0 && h->y_in >= 0 && h->z_in >= 0 ? 8 : 0;
    f |= h->z_in >= 0 && h->x_in >= 0 ? 4 : 0;
    f |= h->y_in >= 0 && h->z_in >= 0 ? 2 : 0;
    f |= h->x_in >= 0 && h->y_in >= 0 ? 1 : 0;
    h->flag = f;
}

static void hit_w(cy_hit *h, unsigned adr, uint32_t data)
{
    uint16_t d = (uint16_t)data;
    switch (adr) {
    case 0x00: case 0x28: h->x1p = d; break;
    case 0x08: case 0x30: h->y1p = d; break;
    case 0x38: case 0x50: h->z1p = d; break;
    case 0x04: case 0x2c: h->x1s = d; break;
    case 0x0c: case 0x34: h->y1s = d; break;
    case 0x3c: case 0x54: h->z1s = d; break;
    case 0x10: case 0x58: h->x2p = d; break;
    case 0x18: case 0x60: h->y2p = d; break;
    case 0x20: case 0x68: h->z2p = d; break;
    case 0x14: case 0x5c: h->x2s = d; break;
    case 0x1c: case 0x64: h->y2s = d; break;
    case 0x24: case 0x6c: h->z2s = d; break;
    case 0x70: h->org = d; break;
    default: break;
    }
    hit_recalc(h);
}

static uint32_t hit_r(cy_board *b, unsigned adr)
{
    cy_hit *h = &b->hit;
    if (h->disconnect) return 0;
    switch (adr) {
    case 0x28: case 0x2a: return (uint16_t)machine_rand(b);
    case 0x00: case 0x10: return (uint16_t)h->x_in;
    case 0x04: case 0x14: return (uint16_t)h->y_in;
    case 0x18: return (uint16_t)h->z_in;
    case 0x08: case 0x1c: return h->flag;
    case 0x40: return h->x1p;
    case 0x48: return h->y1p;
    case 0x50: return h->z1p;
    case 0x44: return h->x1s;
    case 0x4c: return h->y1s;
    case 0x54: return h->z1s;
    case 0x58: return h->x2p;
    case 0x60: return h->y2p;
    case 0x68: return h->z2p;
    case 0x5c: return h->x2s;
    case 0x64: return h->y2s;
    case 0x6c: return h->z2s;
    case 0x70: return h->org;
    case 0x80: return h->x1tox2;
    case 0x84: return h->y1toy2;
    case 0x88: return h->z1toz2;
    case 0x90: return h->x1_p1;
    case 0xa0: return h->y1_p1;
    case 0xb0: return h->z1_p1;
    case 0x98: return h->x1_p2;
    case 0xa8: return h->y1_p2;
    case 0xb8: return h->z1_p2;
    case 0x94: return h->x2_p1;
    case 0xa4: return h->y2_p1;
    case 0xb4: return h->z2_p1;
    case 0x9c: return h->x2_p2;
    case 0xac: return h->y2_p2;
    case 0xbc: return h->z2_p2;
    default: return 0;
    }
}

/* hit2_w: the protection PLD unlocks the hit unit for the BIOS region */
static void hit2_w(cy_board *b, uint32_t data)
{
    data >>= 24;
    cy_hit *h = &b->hit;
    h->disconnect = 1;
    switch (b->region) {
    case 'J': if (data == 0) h->disconnect = 0; break;
    case 'U': if (data == 1) h->disconnect = 0; break;
    case 'K': if (data == 2) h->disconnect = 0; break;
    case 'E': if (data == 3) h->disconnect = 0; break;
    case 'A': if (data < 2) h->disconnect = 0; break;
    default: h->disconnect = 0; break;
    }
}

/* ------------------------------------------------------------------ RTC */

/* MSM6242: 16 BCD-digit registers, read through the current time */
static uint8_t rtc_r(cy_board *b, unsigned reg)
{
    double secs = (double)b->cpu.cycles / CY_CPU_CLOCK;
    time_t t = (time_t)(b->rtc_base_time + (int64_t)secs);
    struct tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    int year = tmv.tm_year % 100, mon = tmv.tm_mon + 1;
    switch (reg & 15) {
    case 0: return (uint8_t)(tmv.tm_sec % 10);
    case 1: return (uint8_t)(tmv.tm_sec / 10);
    case 2: return (uint8_t)(tmv.tm_min % 10);
    case 3: return (uint8_t)(tmv.tm_min / 10);
    case 4: return (uint8_t)(tmv.tm_hour % 10);
    case 5: return (uint8_t)(tmv.tm_hour / 10);
    case 6: return (uint8_t)(tmv.tm_mday % 10);
    case 7: return (uint8_t)(tmv.tm_mday / 10);
    case 8: return (uint8_t)(mon % 10);
    case 9: return (uint8_t)(mon / 10);
    case 10: return (uint8_t)(year % 10);
    case 11: return (uint8_t)(year / 10);
    case 12: return (uint8_t)tmv.tm_wday;
    default: return b->rtc[reg & 15] & 0x0F;
    }
}

/* ----------------------------------------------------------- SH7604 regs */

/* ---- FRT: MAME sh7604_device sh2_timer_resync/activate/callback ---- */
enum { FRT_ICF = 0x80, FRT_OCFA = 0x08, FRT_OCFB = 0x04, FRT_OVF = 0x02, FRT_CCLRA = 0x01 };
static const int frt_div[4] = { 3, 5, 7, 0 };

static void frt_resync(cy_board *b)
{
    cy_onchip *o = &b->onchip;
    int divider = frt_div[o->frc_tcr & 3];
    uint64_t add = (b->cpu.cycles - o->frc_base) >> divider;
    if (add > 0) {
        if (divider) o->frc = (uint16_t)(o->frc + add);
        o->frc_base = b->cpu.cycles;
    }
}

static void frt_activate(cy_board *b)
{
    cy_onchip *o = &b->onchip;
    int max_delta = 0xfffff;
    o->frt_event = UINT64_MAX;
    uint16_t frc = o->frc;
    if (!(o->ftcsr & FRT_OCFA)) { uint16_t d = (uint16_t)(o->ocra - frc); if (d < max_delta) max_delta = d; }
    if (!(o->ftcsr & FRT_OCFB) && (o->ocra <= o->ocrb || !(o->ftcsr & FRT_CCLRA))) {
        uint16_t d = (uint16_t)(o->ocrb - frc); if (d < max_delta) max_delta = d;
    }
    if (!(o->ftcsr & FRT_OVF) && !(o->ftcsr & FRT_CCLRA)) { int d = 0x10000 - frc; if (d < max_delta) max_delta = d; }
    if (max_delta != 0xfffff) {
        int divider = frt_div[o->frc_tcr & 3];
        if (divider) {
            o->frc_base = b->cpu.cycles;
            o->frt_event = b->cpu.cycles + ((uint64_t)max_delta << divider);
        }
    }
}

/* sh2_recalc_irq (FRT part; the WDT runs in watchdog mode and the DMAC is unused) */
static void onchip_recalc_irq(cy_board *b)
{
    cy_onchip *o = &b->onchip;
    int irq = 0, vector = -1;
    if (o->tier & o->ftcsr & (FRT_ICF | FRT_OCFA | FRT_OCFB | FRT_OVF)) {
        int level = (o->regs[0x60] & 0x0F);           /* IPRB bits 8-11 */
        if (level > irq) {
            int mask = o->tier & o->ftcsr;
            irq = level;
            if (mask & FRT_ICF) vector = o->regs[0x66] & 0x7f;                 /* VCRC FIC */
            else if (mask & (FRT_OCFA | FRT_OCFB)) vector = o->regs[0x67] & 0x7f;  /* VCRC FOC */
            else vector = o->regs[0x66] & 0x7f;        /* MAME's vcrd_w takes FOV from VCRC */
        }
    }
    b->cpu.internal_irq_level = irq;
    b->cpu.internal_irq_vector = (uint32_t)vector;
    b->cpu.test_irq = 1;
}

static void frt_callback(cy_board *b)
{
    cy_onchip *o = &b->onchip;
    frt_resync(b);
    uint16_t frc = o->frc;
    if (frc == o->ocrb) o->ftcsr |= FRT_OCFB;
    if (frc == 0x0000) o->ftcsr |= FRT_OVF;
    if (frc == o->ocra) {
        o->ftcsr |= FRT_OCFA;
        if (o->ftcsr & FRT_CCLRA) o->frc = 0;
    }
    onchip_recalc_irq(b);
    frt_activate(b);
}

static uint8_t frt_r8(cy_board *b, unsigned off)
{
    cy_onchip *o = &b->onchip;
    switch (off) {
    case 0x10: return o->tier;
    case 0x11: return o->ftcsr;
    case 0x12: frt_resync(b); return (uint8_t)(o->frc >> 8);
    case 0x13: frt_resync(b); return (uint8_t)o->frc;
    case 0x14: return (uint8_t)(((o->tocr & 0x10) ? o->ocrb : o->ocra) >> 8);
    case 0x15: return (uint8_t)((o->tocr & 0x10) ? o->ocrb : o->ocra);
    case 0x16: return o->frc_tcr & 0x83;
    case 0x17: return (uint8_t)((o->tocr & 0x13) | 0xe0);
    default: return 0;   /* FICR: input capture is not connected */
    }
}

static void frt_w8(cy_board *b, unsigned off, uint8_t data)
{
    cy_onchip *o = &b->onchip;
    frt_resync(b);
    switch (off) {
    case 0x10: o->tier = data; break;
    case 0x11: {
        uint8_t old = o->ftcsr, f = FRT_ICF | FRT_OCFA | FRT_OCFB | FRT_OVF;
        o->ftcsr = (uint8_t)((data & ~f) | (old & data & f));
        break;
    }
    case 0x12: o->frc = (uint16_t)((o->frc & 0x00ff) | data << 8); break;
    case 0x13: o->frc = (uint16_t)((o->frc & 0xff00) | data); break;
    case 0x14: case 0x15: {
        uint16_t *r = (o->tocr & 0x10) ? &o->ocrb : &o->ocra;
        *r = off == 0x14 ? (uint16_t)((*r & 0x00ff) | data << 8) : (uint16_t)((*r & 0xff00) | data);
        break;
    }
    case 0x16: o->frc_tcr = data & 0x83; break;
    case 0x17: o->tocr = data & 0x13; break;
    default: return;
    }
    frt_activate(b);
    onchip_recalc_irq(b);
}

static void divu32(cy_onchip *o)
{
    int32_t a = (int32_t)o->dvdntl, d = (int32_t)o->dvsr;
    if (d == 0 || (a == INT32_MIN && d == -1)) {
        o->divu_ovf = 1;
        o->dvdntl = 0x7fffffff; o->dvdnth = 0x7fffffff;
        return;
    }
    o->dvdntl = (uint32_t)(a / d);
    o->dvdnth = (uint32_t)(a % d);
}

static void divu64(cy_onchip *o)
{
    int64_t a = (int64_t)((uint64_t)o->dvdntl | ((uint64_t)o->dvdnth << 32));
    int64_t d = (int32_t)o->dvsr;
    if (d == 0 || (a == INT64_MIN && d == -1)) { o->divu_ovf = 1; o->dvdntl = o->dvdnth = 0x7fffffff; return; }
    int64_t q = a / d;
    if (q != (int32_t)q) { o->divu_ovf = 1; o->dvdntl = o->dvdnth = 0x7fffffff; return; }
    o->dvdntl = (uint32_t)q;
    o->dvdnth = (uint32_t)(a % d);
}

static uint32_t onchip_r32(cy_board *b, uint32_t a)
{
    cy_onchip *o = &b->onchip;
    unsigned off = a & 0x1FC;
    switch (off) {
    case 0x100: return o->dvsr;
    case 0x104: return o->dvdntl;
    case 0x108: return (o->divu_ovfie ? 2u : 0u) | (o->divu_ovf ? 1u : 0u);
    case 0x110: case 0x118: return o->dvdnth;
    case 0x114: case 0x11C: return o->dvdntl;
    case 0x010: case 0x014: case 0x018:
        return (uint32_t)frt_r8(b, off) << 24 | (uint32_t)frt_r8(b, off + 1) << 16 | (uint32_t)frt_r8(b, off + 2) << 8 | frt_r8(b, off + 3);
    default: return be32(&o->regs[off]);
    }
}

static void onchip_after_write(cy_board *b, unsigned off, unsigned size)
{
    cy_onchip *o = &b->onchip;
    unsigned w = off & 0x1FC;
    uint32_t v = be32(&o->regs[w]);
    switch (w) {
    case 0x100: o->dvsr = v; break;
    case 0x104: o->dvdntl = v; divu32(o); break;
    case 0x108: if (v & 1) o->divu_ovf = 0; if (v & 2) o->divu_ovfie = 1; break;
    case 0x110: o->dvdnth = v; break;
    case 0x114: o->dvdntl = v; divu64(o); break;
    case 0x060: case 0x064: case 0x068: onchip_recalc_irq(b); break;
    default: break;
    }
}

static void onchip_w(cy_board *b, uint32_t a, uint32_t v, unsigned size)
{
    unsigned off = a & 0x1FF;
    if (off >= 0x10 && off < 0x18) {   /* FRT: byte registers (FRC/OCR as byte pairs) */
        unsigned n = size, base = size == 4 ? (off & 0x1FC) : size == 2 ? (off & 0x1FE) : off;
        for (unsigned i = 0; i < n; i++) frt_w8(b, base + i, (uint8_t)(v >> (8 * (n - 1 - i))));
        return;
    }
    if (size == 4) put32(&b->onchip.regs[off & 0x1FC], v);
    else if (size == 2) { b->onchip.regs[off & 0x1FE] = (uint8_t)(v >> 8); b->onchip.regs[(off & 0x1FE) + 1] = (uint8_t)v; }
    else b->onchip.regs[off] = (uint8_t)v;
    onchip_after_write(b, off, size);
}

/* ------------------------------------------------------------------- I/O */

static uint32_t port_400000(cy_board *b)
{
    uint32_t v = 0xFFFFFFFFu, in = b->inputs;
    static const struct { uint32_t in, bit; } map[] = {
        { CY_IN_P1_START, 0x100 }, { CY_IN_P2_START, 0x200 }, { CY_IN_COIN1, 0x400 }, { CY_IN_COIN2, 0x800 },
        { CY_IN_SERVICE, 0x1000 }, { CY_IN_TILT, 0x2000 }, { CY_IN_SERVICE1, 0x4000 },
        { CY_IN_P2_UP, 0x10000 }, { CY_IN_P2_DOWN, 0x20000 }, { CY_IN_P2_LEFT, 0x40000 }, { CY_IN_P2_RIGHT, 0x80000 },
        { CY_IN_P2_B1, 0x100000 }, { CY_IN_P2_B2, 0x200000 },
        { CY_IN_P1_UP, 0x1000000 }, { CY_IN_P1_DOWN, 0x2000000 }, { CY_IN_P1_LEFT, 0x4000000 }, { CY_IN_P1_RIGHT, 0x8000000 },
        { CY_IN_P1_B1, 0x10000000 }, { CY_IN_P1_B2, 0x20000000 },
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) if (in & map[i].in) v &= ~map[i].bit;
    return v;
}

static uint32_t port_400004(cy_board *b)
{
    uint32_t v = 0xFFFFFF00u | b->dips;
    if (b->inputs & CY_IN_TEST) v &= ~1u;          /* PORT_SERVICE, active low */
    return v;
}

/* --------------------------------------------------------------- the bus */

#define AM 0xC7FFFFFFu

static inline uint32_t canon(uint32_t a)
{
    uint32_t top = a & 0xF0000000u;
    return (top == 0 || top == 0x20000000u) ? (a & AM) : a;
}

/* Read a 32-bit bus word containing address a (a aligned to 4). */
static uint32_t read_word32(cy_board *b, uint32_t a)
{
    if (a < 0x00080000) return be32(&b->bios[a]);
    if (a >= 0x04000000 && a < 0x04200000) return be32(&b->game[a - 0x04000000]);
    if (a >= 0x06000000 && a < 0x06100000) {
        if (b->idle_skip && a == 0x0604d3c8 && b->cpu.pc == 0x0402ebd2) b->cpu.spin = 1;   /* cyvern_speedup_r */
        return be32(&b->main_ram[a - 0x06000000]);
    }
    if (a >= 0xC0000000 && a < 0xC0001000) return be32(&b->cache_ram[a - 0xC0000000]);
    if (a >= 0x02000000 && a < 0x02004000) return be32(&b->spriteram[a - 0x02000000]);
    if (a >= 0x02100000 && a < 0x02100040) return be32(&b->spc_regs[a - 0x02100000]);
    if (a >= 0x02400000 && a < 0x02400080) return be32(&b->v3_regs[a - 0x02400000]);
    if (a >= 0x02500000 && a < 0x02504000) return be32(&b->tilemap_a[a - 0x02500000]);
    if (a >= 0x02504000 && a < 0x02508000) return be32(&b->tilemap_b[a - 0x02504000]);
    if (a >= 0x02600000 && a < 0x02608000) return be32(&b->v3slc[a - 0x02600000]);
    if (a >= 0x02a00000 && a < 0x02a00020) return be32(&b->pal_regs[a - 0x02a00000]);
    if (a >= 0x02a40000 && a < 0x02a60000) return be32(&b->palette_ram[a - 0x02a40000]);
    if (a >= 0x04800000 && a < 0x04840000) return be32(&b->v3t_ram[a - 0x04800000]);
    if (a >= 0x00800000 && a < 0x00802000) return be32(&b->nvram[a - 0x00800000]);
    if (a == 0x00400000) return port_400000(b);
    if (a == 0x00400004) return port_400004(b);
    if (a == 0x0040000c) return 0xFFFFFFFFu;
    if (a >= 0x02f00000 && a < 0x02f00100) return hit_r(b, a - 0x02f00000);
    if (a >= 0xFFFFFE00u) return onchip_r32(b, a);
    return 0;
}

static inline int mmio_byte(uint32_t a)
{
    return (a >= 0x00c00000 && a < 0x00c00004) || (a >= 0x01000000 && a < 0x01000010) ||
           (a >= 0xFFFFFE00u && a < 0xFFFFFF00u);
}

static uint8_t mmio_r8(cy_board *b, uint32_t a)
{
    if (a >= 0x00c00000 && a < 0x00c00002) return cy_ymz_read(b, a & 1);
    if (a >= 0x01000000 && a < 0x01000010) return rtc_r(b, a & 15);
    if (a >= 0xFFFFFE00u) {
        if ((a & 0x1FF) >= 0x10 && (a & 0x1FF) < 0x18) return frt_r8(b, a & 0x1FF);
        return b->onchip.regs[a & 0x1FF];
    }
    return 0;
}

uint32_t sh2_rd32(uint32_t a)
{
    a = canon(a);
    return read_word32(B, a & ~3u);
}

uint16_t sh2_rd16(uint32_t a)
{
    a = canon(a);
    if (mmio_byte(a)) return (uint16_t)(mmio_r8(B, a & ~1u) << 8 | mmio_r8(B, (a & ~1u) + 1));
    uint32_t w = read_word32(B, a & ~3u);
    return (uint16_t)((a & 2) ? w : w >> 16);
}

uint8_t sh2_rd8(uint32_t a)
{
    a = canon(a);
    if (mmio_byte(a)) return mmio_r8(B, a);
    uint32_t w = read_word32(B, a & ~3u);
    return (uint8_t)(w >> (8 * (3 - (a & 3))));
}

/* Write 'size' bytes of data at a (naturally aligned). */
static void bus_write(cy_board *b, uint32_t a, uint32_t v, unsigned size)
{
    a = canon(a);
    uint8_t buf[4];
    if (size == 4) put32(buf, v);
    else if (size == 2) { buf[0] = (uint8_t)(v >> 8); buf[1] = (uint8_t)v; }
    else buf[0] = (uint8_t)v;
#define STORE(base, arr) do { memcpy(&b->arr[a - (base)], buf, size); } while (0)
    if (a >= 0x06000000 && a < 0x06100000) { STORE(0x06000000, main_ram); return; }
    if (a >= 0xC0000000 && a < 0xC0001000) { STORE(0xC0000000, cache_ram); return; }
    if (a >= 0x02000000 && a < 0x02004000) { STORE(0x02000000, spriteram); return; }
    if (a >= 0x02100000 && a < 0x02100040) { STORE(0x02100000, spc_regs); return; }
    if (a >= 0x02400000 && a < 0x02400080) { STORE(0x02400000, v3_regs); return; }
    if (a >= 0x02500000 && a < 0x02504000) { STORE(0x02500000, tilemap_a); return; }
    if (a >= 0x02504000 && a < 0x02508000) { STORE(0x02504000, tilemap_b); return; }
    if (a >= 0x02600000 && a < 0x02608000) { STORE(0x02600000, v3slc); return; }
    if (a >= 0x02a00000 && a < 0x02a00020) { STORE(0x02a00000, pal_regs); cy_palette_reg_write(b, (a - 0x02a00000) >> 2); return; }
    if (a >= 0x02a40000 && a < 0x02a60000) { STORE(0x02a40000, palette_ram); cy_palette_ram_write(b, (a - 0x02a40000) >> 2); return; }
    if (a >= 0x04800000 && a < 0x04840000) {
        STORE(0x04800000, v3t_ram);
        /* v3t_w: RAM-based tiles appear at the start of the tilemap B graphics */
        uint32_t w = (a - 0x04800000) & ~3u;
        memcpy(&b->tiles_b[w], &b->v3t_ram[w], 4);
        return;
    }
    if (a >= 0x00800000 && a < 0x00802000) { STORE(0x00800000, nvram); return; }
    if (a >= 0x00c00000 && a < 0x00c00002) {   /* 8-bit device on byte lanes 0-1 */
        for (unsigned i = 0; i < size && a + i < 0x00c00002; i++) cy_ymz_write(b, (a + i) & 1, buf[i]);
        return;
    }
    if (a >= 0x01000000 && a < 0x01000010) { for (unsigned i = 0; i < size; i++) b->rtc[(a + i) & 15] = buf[i]; return; }
    if (a >= 0x01800000 && a < 0x01800004) {
        /* hit2_w is a 32-bit handler: the data byte arrives in bits 24-31 */
        if (size == 4) hit2_w(b, v);
        else if (size == 2 && (a & 2) == 0) hit2_w(b, v << 16);
        else if (size == 1 && (a & 3) == 0) hit2_w(b, v << 24);
        return;
    }
    if (a >= 0x02f00000 && a < 0x02f00100) {
        /* hit_w: 32-bit handler, takes the whole (masked) word */
        hit_w(&b->hit, (a - 0x02f00000) & ~3u, size == 4 ? v : size == 2 ? ((a & 2) ? v : v << 16) : v << (8 * (3 - (a & 3))));
        return;
    }
    if (a >= 0xFFFFFE00u) { onchip_w(b, a, v, size); return; }
    /* 0x400000 io_w (coin counters / interrupt clear: no effect), 0x5000000 watchdog, ROM: ignored */
#undef STORE
}

void sh2_wr32(uint32_t a, uint32_t d) { bus_write(B, a & ~3u, d, 4); }
void sh2_wr16(uint32_t a, uint16_t d) { bus_write(B, a & ~1u, d, 2); }
void sh2_wr8(uint32_t a, uint8_t d) { bus_write(B, a, d, 1); }

/* ------------------------------------------------------------- scheduler */

#define INT15_PERIOD 57272u     /* 2 ms at 28.636 MHz */
#define INT11_PERIOD 229088u    /* 8 ms */
#define INT9_PERIOD 1824u       /* 28.636 MHz / 1824 */

void cy_board_reset(cy_board *b)
{
    cy_board_attach(b);
    memset(b->main_ram, 0, sizeof(b->main_ram));
    memset(b->cache_ram, 0, sizeof(b->cache_ram));
    memset(b->spriteram, 0, sizeof(b->spriteram));
    memset(b->spc_regs, 0, sizeof(b->spc_regs));
    memset(b->v3_regs, 0, sizeof(b->v3_regs));
    memset(b->tilemap_a, 0, sizeof(b->tilemap_a));
    memset(b->tilemap_b, 0, sizeof(b->tilemap_b));
    memset(b->v3slc, 0, sizeof(b->v3slc));
    memset(b->pal_regs, 0, sizeof(b->pal_regs));
    memset(b->palette_ram, 0, sizeof(b->palette_ram));
    memset(b->v3t_ram, 0, sizeof(b->v3t_ram));
    memset(b->tiles_b, 0, 0x40000);
    memset(b->pens, 0, sizeof(b->pens));
    memset(b->sprite_bitmap, 0, sizeof(b->sprite_bitmap));
    memset(&b->onchip, 0, sizeof(b->onchip));
    b->onchip.frt_event = UINT64_MAX;
    memset(&b->hit, 0, sizeof(b->hit));
    /* video_reset */
    b->use_spc_bright = b->use_v3_bright = 1;
    b->bright_spc_r = b->bright_spc_g = b->bright_spc_b = 0;
    b->bright_spc_r_trans = b->bright_spc_g_trans = b->bright_spc_b_trans = 0;
    b->bright_v3_r = b->bright_v3_g = b->bright_v3_b = 0;
    b->bright_v3_r_trans = b->bright_v3_g_trans = b->bright_v3_b_trans = 0;
    b->spc_changed = b->v3_changed = b->palette_updated = 0;
    b->alt_enable_background = b->alt_enable_sprites = 1;
    b->hit.disconnect = b->region != 'A';     /* machine_reset */
    cy_ymz_reset(&b->ymz);
    if (!b->dips) b->dips = 0xFF;
    b->rng = 0x9d14abd7u;
    b->idle_skip = 1;   /* MAME init_cyvern: spin at the main-loop wait (pc 0402EBD2) until an interrupt */
    if (!b->rtc_base_time) b->rtc_base_time = (int64_t)time(NULL);
    b->lines_run = 0;
    b->vpos = 0;
    if (!b->frame_end_line || b->frame_end_line >= CY_LINES) b->frame_end_line = 240;
    b->frames_run = 0;
    b->next_int15 = INT15_PERIOD;
    b->next_int11 = INT11_PERIOD;
    b->next_int9 = INT9_PERIOD;
    b->audio_phase = 0;
    sh2_reset(&b->cpu);
}

static uint64_t line_cycles(uint64_t lines)
{
    return (uint64_t)((double)lines * ((double)CY_CPU_CLOCK / (CY_REFRESH * CY_LINES)) + 0.5);
}

int cy_board_run_frame(cy_board *b, int16_t *stereo, int *frames)
{
    static int32_t L[CY_MAX_FRAME_SAMPLES + 8], R[CY_MAX_FRAME_SAMPLES + 8];
    cy_board_attach(b);
    int status = CY_RUN_OK, produced = 0;
    /* samples per line in 16.16 fixed point */
    const uint32_t spl = (uint32_t)(CY_AUDIO_RATE / (CY_REFRESH * CY_LINES) * 65536.0 + 0.5);

    do {
        int line = b->vpos;
        if (line == 240) sh2_set_irq_line(&b->cpu, 5, 1);       /* vblank */
        else if (line == 0) sh2_set_irq_line(&b->cpu, 1, 1);    /* spc */
        b->lines_run++;
        uint64_t end = line_cycles(b->lines_run);
        while (status == CY_RUN_OK && b->cpu.cycles < end) {
            uint64_t next = end;
            if (b->next_int9 < next) next = b->next_int9;
            if (b->next_int15 < next) next = b->next_int15;
            if (b->next_int11 < next) next = b->next_int11;
            if (b->onchip.frt_event < next) next = b->onchip.frt_event;
            if (sh2_run_until(&b->cpu, next) != SH2_RUN_OK) { status = CY_RUN_MISSING; break; }
            if (b->cpu.cycles >= b->next_int15) { sh2_set_irq_line(&b->cpu, 15, 1); b->next_int15 += INT15_PERIOD; }
            if (b->cpu.cycles >= b->next_int11) { sh2_set_irq_line(&b->cpu, 11, 1); b->next_int11 += INT11_PERIOD; }
            if (b->cpu.cycles >= b->next_int9) { sh2_set_irq_line(&b->cpu, 9, 1); b->next_int9 += INT9_PERIOD; }
            if (b->cpu.cycles >= b->onchip.frt_event) frt_callback(b);
        }
        uint32_t acc = b->audio_phase + spl;
        int n = (int)(acc >> 16);
        b->audio_phase = acc & 0xFFFF;
        if (produced + n > CY_MAX_FRAME_SAMPLES) n = CY_MAX_FRAME_SAMPLES - produced;
        if (n > 0) { cy_ymz_render(b, &L[produced], &R[produced], n); produced += n; }
        b->vpos = (uint16_t)((line + 1) % CY_LINES);
        if (b->vpos == 240) {
            /* MAME vblank_begin: the screen is composed first (with the sprite
             * bitmap drawn at the previous vblank), then screen_vblank draws
             * the sprites for the next frame */
            cy_video_compose(b);
            cy_draw_sprites(b);
        }
    } while (b->vpos != b->frame_end_line && status == CY_RUN_OK);

    for (int i = 0; i < produced; i++) {
        int32_t l = L[i] / 512, r = R[i] / 512;   /* YMZ280B: sample * level / 2 / 256 */
        if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
        if (stereo) { stereo[i * 2] = (int16_t)l; stereo[i * 2 + 1] = (int16_t)r; }
    }
    if (frames) *frames = produced;
    b->frames_run++;
    return status;
}
