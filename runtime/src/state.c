/* Save-state serialization (see cy/state.h).  One function walks the board
 * for both directions so the save and load layouts cannot drift apart. */
#include "cy/state.h"
#include "miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t cy_crc32(uint32_t crc, const void *data, size_t size)
{
    return (uint32_t)mz_crc32(crc, (const unsigned char *)data, size);
}

uint32_t cy_board_rom_fingerprint(const cy_board *b)
{
    static const uint8_t *cached_game;
    static uint32_t cached;
    if (cached && cached_game == b->game) return cached;
    uint32_t crc = cy_crc32(0, b->bios, 0x80000);
    crc = cy_crc32(crc, b->game, 0x200000);
    crc = cy_crc32(crc, b->sprite_rom, 0x800000);
    crc = cy_crc32(crc, b->tiles_a, 0x800000);
    crc = cy_crc32(crc, b->tiles_b + 0x40000, 0x800000 - 0x40000);
    crc = cy_crc32(crc, b->samples, 0x400000);
    cached_game = b->game;
    cached = crc ? crc : 1;
    return cached;
}

/* ---- stream ----------------------------------------------------------- */

typedef struct io {
    int writing;
    uint8_t *p;           /* write buffer, or read source */
    size_t n, cap, at;
    int bad;
} io;

static void io_bytes(io *s, void *v, size_t n)
{
    if (s->bad) { if (!s->writing) memset(v, 0, n); return; }
    if (s->writing) {
        if (s->n + n > s->cap) {
            size_t cap = s->cap ? s->cap : 1u << 18;
            while (cap < s->n + n) cap *= 2;
            uint8_t *p = (uint8_t *)realloc(s->p, cap);
            if (!p) { s->bad = 1; return; }
            s->p = p; s->cap = cap;
        }
        memcpy(s->p + s->n, v, n);
        s->n += n;
    } else {
        if (s->at + n > s->n) { s->bad = 1; memset(v, 0, n); return; }
        memcpy(v, s->p + s->at, n);
        s->at += n;
    }
}
static void io_u8(io *s, uint8_t *v) { io_bytes(s, v, 1); }
static void io_u16(io *s, uint16_t *v)
{
    uint8_t b[2] = { (uint8_t)*v, (uint8_t)(*v >> 8) };
    io_bytes(s, b, 2);
    if (!s->writing) *v = (uint16_t)(b[0] | b[1] << 8);
}
static void io_u32(io *s, uint32_t *v)
{
    uint8_t b[4] = { (uint8_t)*v, (uint8_t)(*v >> 8), (uint8_t)(*v >> 16), (uint8_t)(*v >> 24) };
    io_bytes(s, b, 4);
    if (!s->writing) *v = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static void io_u64(io *s, uint64_t *v)
{
    uint32_t lo = (uint32_t)*v, hi = (uint32_t)(*v >> 32);
    io_u32(s, &lo); io_u32(s, &hi);
    if (!s->writing) *v = (uint64_t)hi << 32 | lo;
}
static void io_i32(io *s, int32_t *v) { uint32_t u = (uint32_t)*v; io_u32(s, &u); *v = (int32_t)u; }
static void io_int(io *s, int *v) { uint32_t u = (uint32_t)*v; io_u32(s, &u); *v = (int)(int32_t)u; }
static void io_u16a(io *s, uint16_t *a, size_t count) { for (size_t i = 0; i < count; i++) io_u16(s, &a[i]); }

/* Sections: 4-byte tag + 32-bit length; on load the tag and length must match. */
static size_t sec_begin(io *s, const char tag[4])
{
    char t[4];
    memcpy(t, tag, 4);
    io_bytes(s, t, 4);
    if (!s->writing && memcmp(t, tag, 4) != 0) s->bad = 2;
    size_t at = s->writing ? s->n : s->at;
    uint32_t len = 0;
    io_u32(s, &len);
    if (!s->writing) return (size_t)len | ((size_t)1 << 62);   /* remember the claimed length */
    return at;
}
static void sec_end(io *s, size_t mark, size_t start)
{
    if (s->bad) return;
    if (s->writing) {
        uint32_t len = (uint32_t)(s->n - mark - 4);
        memcpy(s->p + mark, &len, 4);   /* little-endian hosts only (x86-64, ARM64) */
    } else {
        size_t claimed = mark & ~((size_t)1 << 62);
        if (s->at - start != claimed) s->bad = 2;
    }
}
#define SECTION(tag, ...) do { size_t m_ = sec_begin(s, tag); size_t st_ = s->writing ? 0 : s->at; __VA_ARGS__; sec_end(s, m_, st_); } while (0)

static void io_i64(io *s, int64_t *v) { uint64_t u = (uint64_t)*v; io_u64(s, &u); *v = (int64_t)u; }
static void io_i16(io *s, int16_t *v) { uint16_t u = (uint16_t)*v; io_u16(s, &u); *v = (int16_t)u; }

static void io_sh2(io *s, sh2_state *c)
{
    for (int i = 0; i < 16; i++) io_u32(s, &c->r[i]);
    io_u32(s, &c->pc); io_u32(s, &c->pr); io_u32(s, &c->sr); io_u32(s, &c->gbr); io_u32(s, &c->vbr);
    io_u32(s, &c->mach); io_u32(s, &c->macl); io_u32(s, &c->ea); io_u32(s, &c->delay);
    io_u64(s, &c->cycles); io_u64(s, &c->instructions);
    io_u32(s, &c->pending_irq); io_i32(s, &c->internal_irq_level); io_u32(s, &c->internal_irq_vector);
    io_u8(s, &c->pending_nmi); io_u8(s, &c->sleep_mode); io_u8(s, &c->test_irq); io_u8(s, &c->spin);
}

static void io_ymz(io *s, cy_ymz *z)
{
    io_u8(s, &z->current_register); io_u8(s, &z->status_register); io_u8(s, &z->irq_state); io_u8(s, &z->irq_mask);
    io_u8(s, &z->irq_enable); io_u8(s, &z->keyon_enable); io_u8(s, &z->ext_mem_enable); io_u8(s, &z->ext_readlatch);
    io_u32(s, &z->ext_mem_address_hi); io_u32(s, &z->ext_mem_address_mid); io_u32(s, &z->ext_mem_address);
    for (int i = 0; i < 8; i++) {
        cy_ymz_voice *v = &z->voice[i];
        io_u8(s, &v->playing); io_u8(s, &v->ended); io_u8(s, &v->keyon); io_u8(s, &v->looping); io_u8(s, &v->mode);
        io_u16(s, &v->fnum); io_u8(s, &v->level); io_u8(s, &v->pan);
        io_u32(s, &v->start); io_u32(s, &v->stop); io_u32(s, &v->loop_start); io_u32(s, &v->loop_end); io_u32(s, &v->position);
        io_i32(s, &v->signal); io_i32(s, &v->step); io_i32(s, &v->loop_signal); io_i32(s, &v->loop_step);
        io_u32(s, &v->loop_count);
        io_i32(s, &v->output_left); io_i32(s, &v->output_right); io_i32(s, &v->output_step); io_i32(s, &v->output_pos);
        io_i16(s, &v->last_sample); io_i16(s, &v->curr_sample); io_u8(s, &v->irq_schedule);
    }
}

static void io_hit(io *s, cy_hit *h)
{
    uint16_t *w[] = { &h->x1p, &h->y1p, &h->z1p, &h->x1s, &h->y1s, &h->z1s, &h->x2p, &h->y2p, &h->z2p, &h->x2s, &h->y2s, &h->z2s, &h->org,
                      &h->x1_p1, &h->x1_p2, &h->y1_p1, &h->y1_p2, &h->z1_p1, &h->z1_p2, &h->x2_p1, &h->x2_p2, &h->y2_p1, &h->y2_p2,
                      &h->z2_p1, &h->z2_p2, &h->x1tox2, &h->y1toy2, &h->z1toz2, &h->flag };
    for (size_t i = 0; i < sizeof(w) / sizeof(w[0]); i++) io_u16(s, w[i]);
    io_i16(s, &h->x_in); io_i16(s, &h->y_in); io_i16(s, &h->z_in);
    io_u8(s, &h->disconnect);
}

static void io_board(io *s, cy_board *b)
{
    SECTION("SH2 ", io_sh2(s, &b->cpu));
    SECTION("ONCH", {
        io_bytes(s, b->onchip.regs, sizeof(b->onchip.regs));
        io_u32(s, &b->onchip.dvsr); io_u32(s, &b->onchip.dvdntl); io_u32(s, &b->onchip.dvdnth);
        io_u8(s, &b->onchip.divu_ovf); io_u8(s, &b->onchip.divu_ovfie);
        io_u8(s, &b->onchip.tier); io_u8(s, &b->onchip.ftcsr); io_u8(s, &b->onchip.frc_tcr); io_u8(s, &b->onchip.tocr);
        io_u16(s, &b->onchip.frc); io_u16(s, &b->onchip.ocra); io_u16(s, &b->onchip.ocrb);
        io_u64(s, &b->onchip.frc_base); io_u64(s, &b->onchip.frt_event);
    });
    SECTION("RAM ", io_bytes(s, b->main_ram, sizeof(b->main_ram)); io_bytes(s, b->cache_ram, sizeof(b->cache_ram)));
    SECTION("NVRM", io_bytes(s, b->nvram, sizeof(b->nvram)));
    SECTION("VID ", {
        io_bytes(s, b->spriteram, sizeof(b->spriteram));
        io_bytes(s, b->spc_regs, sizeof(b->spc_regs));
        io_bytes(s, b->v3_regs, sizeof(b->v3_regs));
        io_bytes(s, b->tilemap_a, sizeof(b->tilemap_a));
        io_bytes(s, b->tilemap_b, sizeof(b->tilemap_b));
        io_bytes(s, b->v3slc, sizeof(b->v3slc));
        io_bytes(s, b->pal_regs, sizeof(b->pal_regs));
        io_bytes(s, b->palette_ram, sizeof(b->palette_ram));
        io_bytes(s, b->v3t_ram, sizeof(b->v3t_ram));
        for (size_t i = 0; i < sizeof(b->pens) / 4; i++) io_u32(s, &b->pens[i]);
        uint8_t *f[] = { &b->use_spc_bright, &b->use_v3_bright, &b->spc_changed, &b->v3_changed, &b->palette_updated,
                         &b->bright_spc_r, &b->bright_spc_g, &b->bright_spc_b, &b->bright_spc_r_trans, &b->bright_spc_g_trans, &b->bright_spc_b_trans,
                         &b->bright_v3_r, &b->bright_v3_g, &b->bright_v3_b, &b->bright_v3_r_trans, &b->bright_v3_g_trans, &b->bright_v3_b_trans,
                         &b->alt_enable_background, &b->alt_enable_sprites };
        for (size_t i = 0; i < sizeof(f) / sizeof(f[0]); i++) io_u8(s, f[i]);
        io_u16a(s, b->sprite_bitmap, sizeof(b->sprite_bitmap) / 2);
        for (size_t i = 0; i < sizeof(b->screen) / 4; i++) io_u32(s, &b->screen[i]);
    });
    SECTION("HIT ", io_hit(s, &b->hit));
    SECTION("YMZ ", io_ymz(s, &b->ymz));
    SECTION("MISC", {
        io_bytes(s, b->rtc, sizeof(b->rtc));
        io_u8(s, &b->dips);
        io_u32(s, &b->rng);
        io_i64(s, &b->rtc_base_time);
        io_u64(s, &b->lines_run); io_u16(s, &b->vpos); io_u64(s, &b->frames_run);
        io_u64(s, &b->next_int15); io_u64(s, &b->next_int11); io_u64(s, &b->next_int9);
        io_u32(s, &b->audio_phase);
    });
}

uint8_t *cy_state_save(const cy_board *b, size_t *size)
{
    io s = { 1, NULL, 0, 0, 0, 0 };
    uint32_t version = CY_STATE_VERSION, fp = cy_board_rom_fingerprint(b), entries = sh2_program_entry_count();
    io_bytes(&s, (void *)CY_STATE_MAGIC, 8);
    io_u32(&s, &version); io_u32(&s, &fp); io_u32(&s, &entries);
    io_board(&s, (cy_board *)b);       /* writing does not modify the board */
    uint32_t crc = s.bad ? 0 : cy_crc32(0, s.p, s.n);
    io_u32(&s, &crc);
    if (s.bad) { free(s.p); return NULL; }
    *size = s.n;
    return s.p;
}

static int fail(char *err, size_t n, const char *m) { if (err && n) snprintf(err, n, "%s", m); return 0; }

/* Parse into a scratch board so the real one is untouched on failure. */
static cy_board *parse(const cy_board *b, const uint8_t *data, size_t size, char *err, size_t n)
{
    if (!data || size < 8 + 12 + 4) { fail(err, n, "state file is truncated"); return NULL; }
    if (memcmp(data, CY_STATE_MAGIC, 8) != 0) { fail(err, n, "not a Cyvern save state"); return NULL; }
    uint32_t stored = (uint32_t)data[size - 4] | (uint32_t)data[size - 3] << 8 | (uint32_t)data[size - 2] << 16 | (uint32_t)data[size - 1] << 24;
    if (cy_crc32(0, data, size - 4) != stored) { fail(err, n, "state is damaged (checksum mismatch)"); return NULL; }
    io s = { 0, (uint8_t *)data, size - 4, 0, 8, 0 };
    uint32_t version = 0, fp = 0, entries = 0;
    io_u32(&s, &version); io_u32(&s, &fp); io_u32(&s, &entries);
    if (version != CY_STATE_VERSION) {
        char m[96]; snprintf(m, sizeof(m), "state format version %u is not supported (expected %u)", version, CY_STATE_VERSION);
        fail(err, n, m); return NULL;
    }
    if (fp != cy_board_rom_fingerprint(b)) { fail(err, n, "state was made with a different ROM set"); return NULL; }
    cy_board *t = (cy_board *)malloc(sizeof(*t));
    if (!t) { fail(err, n, "out of memory"); return NULL; }
    memcpy(t, b, sizeof(*t));
    io_board(&s, t);
    if (s.bad || s.at != s.n) { free(t); fail(err, n, "state has an unexpected layout or is truncated"); return NULL; }
    /* no check on the resume address: code the build did not translate runs
     * through the fallback interpreter, so states move freely between the
     * recompiled and the generic build of the same ROM set */
    return t;
}

int cy_state_validate(const cy_board *b, const uint8_t *data, size_t size, char *err, size_t n)
{
    cy_board *t = parse(b, data, size, err, n);
    if (!t) return 0;
    free(t);
    return 1;
}

int cy_state_load(cy_board *b, const uint8_t *data, size_t size, char *err, size_t n)
{
    cy_board *t = parse(b, data, size, err, n);
    if (!t) return 0;
    uint32_t inputs = 0;
    memcpy(b, t, sizeof(*b));
    free(t);
    memcpy(b->tiles_b, b->v3t_ram, 0x40000);   /* RAM-based tiles live at the start of tilemap B graphics */
    b->inputs = inputs;   /* inputs held when the state was made must not stay latched */
    cy_board_attach(b);
    return 1;
}
