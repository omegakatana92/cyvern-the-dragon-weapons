/* Super Kaneko Nova System video, after MAME 0.264 kaneko/suprnova_v.cpp
 * (two ROZ tilemaps with line/column scroll, palette brightness, the
 * sprite/background mixer with additive sprites) and kaneko/sknsspr.cpp (RLE
 * sprite generator with zoom).  BSD-3-Clause originals by David Haywood,
 * Sylvain Glaize, Paul Priest, Olivier Galibert. */
#include "cy/board.h"

#include <string.h>

static inline uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline uint32_t reg(const uint8_t *r, unsigned off) { return be32(r + off); }

/* ---------------------------------------------------------------- palette */

static void set_pen(cy_board *b, unsigned offset, int br, int bg, int bb)
{
    uint32_t v = be32(&b->palette_ram[offset * 4]);
    int bl = (int)(v & 0x1f), g = (int)((v >> 5) & 0x1f), r = (int)((v >> 10) & 0x1f);
    int use = offset < 0x40 * 256 ? b->use_spc_bright : b->use_v3_bright;
    if (use) {
        bl = bb ? ((bl << 3) * (bb + 1)) >> 8 : 0;
        g = bg ? ((g << 3) * (bg + 1)) >> 8 : 0;
        r = br ? ((r << 3) * (br + 1)) >> 8 : 0;
    } else {
        bl <<= 3; g <<= 3; r <<= 3;
    }
    b->pens[offset] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)bl;
}

void cy_palette_ram_write(cy_board *b, unsigned offset)
{
    if (offset < 0x40 * 256) set_pen(b, offset, b->bright_spc_r, b->bright_spc_g, b->bright_spc_b);
    else set_pen(b, offset, b->bright_v3_r, b->bright_v3_g, b->bright_v3_b);
}

void cy_palette_reg_write(cy_board *b, unsigned offset)
{
    uint32_t data = be32(&b->pal_regs[offset * 4]);
    b->palette_updated = 1;
    switch (offset) {
    case 0: if (b->use_spc_bright != (data & 1)) { b->use_spc_bright = data & 1; b->spc_changed = 1; }
            b->alt_enable_sprites = (data >> 8) & 1; break;
    case 1: if (b->bright_spc_g != (data & 0xff)) { b->bright_spc_g = data & 0xff; b->spc_changed = 1; }
            b->bright_spc_g_trans = (data >> 8) & 0xff; break;
    case 2: if (b->bright_spc_r != (data & 0xff)) { b->bright_spc_r = data & 0xff; b->spc_changed = 1; }
            b->bright_spc_r_trans = (data >> 8) & 0xff; break;
    case 3: if (b->bright_spc_b != (data & 0xff)) { b->bright_spc_b = data & 0xff; b->spc_changed = 1; }
            b->bright_spc_b_trans = (data >> 8) & 0xff; break;
    case 4: if (b->use_v3_bright != (data & 1)) { b->use_v3_bright = data & 1; b->v3_changed = 1; }
            b->alt_enable_background = (data >> 8) & 1; break;
    case 5: if (b->bright_v3_g != (data & 0xff)) { b->bright_v3_g = data & 0xff; b->v3_changed = 1; }
            b->bright_v3_g_trans = (data >> 8) & 0xff; break;
    case 6: if (b->bright_v3_r != (data & 0xff)) { b->bright_v3_r = data & 0xff; b->v3_changed = 1; }
            b->bright_v3_r_trans = (data >> 8) & 0xff; break;
    case 7: if (b->bright_v3_b != (data & 0xff)) { b->bright_v3_b = data & 0xff; b->v3_changed = 1; }
            b->bright_v3_b_trans = (data >> 8) & 0xff; break;
    }
}

/* palette_update(): MAME never clears the *_changed flags, so after the first
 * brightness change every frame with a palette register write recomputes the half */
static void palette_update(cy_board *b)
{
    if (!b->palette_updated) return;
    if (b->spc_changed)
        for (unsigned i = 0; i < 0x40 * 256; i++) set_pen(b, i, b->bright_spc_r, b->bright_spc_g, b->bright_spc_b);
    if (b->v3_changed)
        for (unsigned i = 0x40 * 256; i < 0x80 * 256; i++) set_pen(b, i, b->bright_v3_r, b->bright_v3_g, b->bright_v3_b);
    b->palette_updated = 0;
}

/* --------------------------------------------------------------- tilemaps */

/* One pixel of a 1024x1024 tilemap (64x64 tiles of 16x16): returns the pen
 * index (colour * 256 + pen, as MAME's pixmap) and the tile category. */
static inline uint16_t tile_pixel(const cy_board *b, int which, int depth4, uint32_t x, uint32_t y, uint8_t *flags)
{
    const uint8_t *map = which ? b->tilemap_b : b->tilemap_a;
    const uint8_t *gfx = which ? b->tiles_b : b->tiles_a;
    uint32_t e = be32(&map[(((y >> 4) & 63) * 64 + ((x >> 4) & 63)) * 4]);
    uint32_t code = e & 0x001fffff;
    uint32_t colr = (e >> 24) & 0x3f;
    *flags = (uint8_t)((e >> 21) & 7);
    int px = (int)(x & 15), py = (int)(y & 15);
    if (e & 0x80000000u) px = 15 - px;
    if (e & 0x40000000u) py = 15 - py;
    uint32_t pen;
    if (!depth4) {
        pen = gfx[(size_t)(code & 0x7FFF) * 256 + (size_t)py * 16 + (size_t)px];
    } else {
        uint8_t v = gfx[(size_t)(code & 0xFFFF) * 128 + (size_t)py * 8 + (size_t)(px >> 1)];
        pen = (px & 1) ? (v >> 4) : (v & 15);
    }
    return (uint16_t)(((0x40 + colr) & 0x7F) * 256 + pen);
}

static uint16_t layer_pix[CY_SCREEN_H * CY_SCREEN_W][2];
static uint8_t layer_flag[CY_SCREEN_H * CY_SCREEN_W][2];

/* draw_roz with line scroll (or column scroll) into the layer buffer */
static void draw_roz(const cy_board *b, int which, uint32_t startx, uint32_t starty, int incxx, int incxy,
                     int incyx, int incyy, int wraparound, int columnscroll, const uint8_t *scrollram, int depth4)
{
    const uint32_t xmask = 1023, ymask = 1023;
    const uint32_t widthshifted = 1024u << 16, heightshifted = 1024u << 16;
    for (int sy = 0; sy < CY_SCREEN_H; sy++) {
        uint32_t cx = startx, cy = starty;
        for (int x = 0; x < CY_SCREEN_W; x++) {
            if (wraparound || (cx < widthshifted && cy < heightshifted)) {
                uint32_t px, py;
                if (columnscroll) {
                    py = ((cy >> 16) - be32(&scrollram[((cx >> 16) & 0x3ff) * 4])) & ymask;
                    px = (cx >> 16) & xmask;
                } else {
                    py = (cy >> 16) & ymask;
                    px = ((cx >> 16) - be32(&scrollram[((cy >> 16) & 0x3ff) * 4])) & xmask;
                }
                uint8_t f;
                layer_pix[sy * CY_SCREEN_W + x][which] = tile_pixel(b, which, depth4, px, py, &f);
                layer_flag[sy * CY_SCREEN_W + x][which] = f;
            }
            cx += (uint32_t)incxx;
            cy += (uint32_t)incxy;
        }
        startx += (uint32_t)incyx;
        starty += (uint32_t)incyy;
    }
}

static void draw_layer(const cy_board *b, int which)
{
    const uint8_t *r = b->v3_regs;
    unsigned ctl = which ? 0x34 : 0x10;
    int enable = (int)(reg(r, ctl) & 1), nowrap = (int)(reg(r, ctl) & 4);
    if (!enable || !b->alt_enable_background) return;
    unsigned base = which ? 0x40 : 0x1c;   /* startx, starty, incxx, incxy, incyx, incyy */
    uint32_t startx = reg(r, base), starty = reg(r, base + 4);
    int incxx = (int)(reg(r, base + 8) & 0x7ffff); if (incxx & 0x40000) incxx -= 0x80000;
    int incxy = (int)reg(r, base + 12);
    int incyx = (int)reg(r, base + 16);
    int incyy = (int)(reg(r, base + 20) & 0x7ffff); if (incyy & 0x40000) incyy -= 0x80000;
    int columnscroll = which ? (int)((reg(r, 0x0c) >> 9) & 1) : (int)((reg(r, 0x0c) >> 1) & 1);
    int depth4 = which ? (reg(r, 0x0c) & 0x0100) != 0 : (reg(r, 0x0c) & 0x0001) != 0;
    draw_roz(b, which, startx << 8, starty << 8, incxx << 8, incxy << 8, incyx << 8, incyy << 8, !nowrap,
             columnscroll, which ? &b->v3slc[0x1000] : &b->v3slc[0], depth4);
}

void cy_video_compose(cy_board *b)
{
    palette_update(b);
    memset(layer_pix, 0, sizeof(layer_pix));
    memset(layer_flag, 0, sizeof(layer_flag));
    draw_layer(b, 1);   /* B -> lower */
    draw_layer(b, 0);   /* A -> higher */
    const uint32_t *clut = b->pens;
    int pri_a = (int)((reg(b->v3_regs, 0x10) & 2) >> 1);
    int pri_b = (int)((reg(b->v3_regs, 0x34) & 2) >> 1);
    for (int i = 0; i < CY_SCREEN_W * CY_SCREEN_H; i++) {
        uint16_t pendata = layer_pix[i][1] & 0x7fff, pendata2 = layer_pix[i][0] & 0x7fff;
        uint16_t pendata3 = b->alt_enable_sprites ? (b->sprite_bitmap[i] & 0x3fff) : 0;
        uint16_t pri = (uint16_t)(((layer_flag[i][1] & 7) << 1) | pri_b);
        uint16_t pri2 = (uint16_t)(((layer_flag[i][0] & 7) << 1) | pri_a);
        uint16_t pri3 = (uint16_t)(((b->sprite_bitmap[i] & 0xc000) >> 12) + 3);
        uint16_t bgpendata, bgpri;
        if (pri <= pri2) {
            if (pendata2 & 0xff) { bgpendata = pendata2; bgpri = pri2; }
            else if (pendata & 0xff) { bgpendata = pendata; bgpri = pri; }
            else { bgpendata = pendata2; bgpri = 0; }
        } else {
            if (pendata & 0xff) { bgpendata = pendata; bgpri = pri; }
            else if (pendata2 & 0xff) { bgpendata = pendata2; bgpri = pri2; }
            else { bgpendata = 0; bgpri = 0; }
        }
        uint32_t out;
        if (pri3 > bgpri && (pendata3 & 0xff)) {
            uint32_t palvalue = be32(&b->palette_ram[pendata3 * 4]);
            if (palvalue & 0x8000) {
                /* additive sprite pixel (MAME's channel naming kept: "r" is the low byte) */
                uint32_t s = clut[bgpendata & 0x7fff], d = clut[pendata3 & 0x3fff];
                int r = (int)(s & 0xff), g = (int)((s >> 8) & 0xff), bl = (int)((s >> 16) & 0xff);
                int r2 = (int)(d & 0xff), g2 = (int)((d >> 8) & 0xff), b2 = (int)((d >> 16) & 0xff);
                r2 = (r2 * b->bright_spc_r_trans) >> 8;
                g2 = (g2 * b->bright_spc_g_trans) >> 8;
                b2 = (b2 * b->bright_spc_b_trans) >> 8;
                r += r2; if (r > 255) r = 255;
                g += g2; if (g > 255) g = 255;
                bl += b2; if (bl > 255) bl = 255;
                out = (uint32_t)r | (uint32_t)g << 8 | (uint32_t)bl << 16;
            } else {
                out = clut[pendata3];
            }
        } else {
            out = clut[bgpendata];
        }
        b->screen[i] = out & 0xFFFFFF;
    }
}

void cy_video_render(cy_board *b, uint32_t *pixels)
{
    memcpy(pixels, b->screen, sizeof(b->screen));
}

/* ---------------------------------------------------------------- sprites */

#define DECODE_SIZE 0x2000
static uint8_t decodebuffer[DECODE_SIZE];

static int rle_decode(const cy_board *b, int romoffset, int size)
{
    int decodeoffset = 0;
    const int mask = (1 << 27) - 1;
    while (size > 0) {
        uint8_t code = b->sprite_rom[(romoffset++ & mask) & 0x7FFFFF];
        size -= (code & 0x7f) + 1;
        if (code & 0x80) {
            code &= 0x7f;
            do {
                decodebuffer[(decodeoffset++) & (DECODE_SIZE - 1)] = b->sprite_rom[(romoffset++ & mask) & 0x7FFFFF];
                code--;
            } while (code != 0xff);
        } else {
            uint8_t val = b->sprite_rom[(romoffset++ & mask) & 0x7FFFFF];
            do {
                decodebuffer[(decodeoffset++) & (DECODE_SIZE - 1)] = val;
                code--;
            } while (code != 0xff);
        }
    }
    return romoffset & mask;
}

typedef struct clip_t { int min_x, max_x, min_y, max_y; } clip_t;

static inline void plot(uint16_t *bm, int x, int y, int v) { bm[y * CY_SCREEN_W + x] = (uint16_t)v; }

/* the zooming blitters of sknsspr.cpp (z_* macros expanded) */
static void blit_z(uint16_t *bm, int flip, const uint8_t *src, int x, int y, int sx, int sy,
                   uint16_t zx_m, uint16_t zx_s, uint16_t zy_m, uint16_t zy_s, int colour)
{
    uint32_t zxs = 0x10000 - zx_m, zxd = 0x10000 - zx_s, zys = 0x10000 - zy_m, zyd = 0x10000 - zy_s;
    uint32_t bxs = 0, bys = 0, xs, ys;
    int xd, yd, old, old2;
    int step_spr = sx;
    clip_t clip = { 0, CY_SCREEN_W << 16, 0, CY_SCREEN_H << 16 };
    int fx = flip & 2, fy = flip & 1;
    sx <<= 16; sy <<= 16; x <<= 10; y <<= 10;
    if (!fx) { if (x < clip.min_x) do { bxs += zxs; x += (int)zxd; } while (x < clip.min_x); }
    else { if (x >= clip.max_x) do { bxs += zxs; x -= (int)zxd; } while (x >= clip.max_x); }
    if (!fy) { if (y < clip.min_y) { do { bys += zys; y += (int)zyd; } while (y < clip.min_y); src += (bys >> 16) * step_spr; } }
    else { if (y >= clip.max_y) { do { bys += zys; y -= (int)zyd; } while (y >= clip.max_y); src += (bys >> 16) * step_spr; } }
    ys = bys; yd = y;
    while (ys < (uint32_t)sy && (fy ? yd >= clip.min_y : yd < clip.max_y)) {
        xs = bxs; xd = x;
        while (xs < (uint32_t)sx && (fx ? xd >= clip.min_x : xd < clip.max_x)) {
            uint8_t val = src[(xs >> 16) & 0x3f];
            if (val) plot(bm, xd >> 16, yd >> 16, val + colour);
            old = xd;
            do { xs += zxs; if (fx) xd -= (int)zxd; else xd += (int)zxd; } while (!((xd ^ old) & ~0xffff));
        }
        old = yd; old2 = (int)ys;
        do { ys += zys; if (fy) yd -= (int)zyd; else yd += (int)zyd; } while (!((yd ^ old) & ~0xffff));
        while ((ys ^ (uint32_t)old2) & ~0xffffu) { src += step_spr; old2 += 0x10000; }
    }
}

void cy_draw_sprites(cy_board *b)
{
    const uint8_t *sr = b->spc_regs;
    uint16_t *bm = b->sprite_bitmap;
    int clear_bitmap = (int)(~reg(sr, 0x04) & 0x04);
    int disabled = (int)(reg(sr, 0x04) & 0x08);
    if (clear_bitmap) memset(bm, 0, sizeof(b->sprite_bitmap));
    if (disabled) return;
    int group_enable = (int)((reg(sr, 0x00) & 0x0040) >> 6);
    int sprite_flip = (int)(reg(sr, 0x04) & 3);
    int16_t sprite_y_scroll = (int16_t)((reg(sr, 0x08) & 0x7fff) << 1) >> 1;
    int16_t sprite_x_scroll = (int16_t)((reg(sr, 0x10) & 0x7fff) << 1) >> 1;
    int16_t gx[4]; int gy[4];
    for (int g = 0; g < 4; g++) { gx[g] = (int16_t)(reg(sr, 0x18 + g * 8) & 0xffff); gy[g] = (int)(reg(sr, 0x1c + g * 8) & 0xffff); }
    sprite_x_scroll = (int16_t)(sprite_x_scroll + (0 << 6));   /* skns_sprite_kludge(+0, +2) for cyvern */
    sprite_y_scroll = (int16_t)(sprite_y_scroll + (2 << 6));
    int16_t xpos = 0, ypos = 0;
    int colour = 0, pri = 0, romoffset = 0, endromoffs = 0;
    for (unsigned o = 0; o < sizeof(b->spriteram); o += 16) {
        uint32_t s0 = be32(&b->spriteram[o]), s1 = be32(&b->spriteram[o + 4]), s2 = be32(&b->spriteram[o + 8]), s3 = be32(&b->spriteram[o + 12]);
        int xflip = (int)((s0 & 0x200) >> 9), yflip = (int)((s0 & 0x100) >> 8);
        int ysize = ((int)((s0 & 0x30000000) >> 28) + 1) * 16;
        int xsize = ((int)((s0 & 0x03000000) >> 24) + 1) * 16;
        int size = xsize * ysize;
        int joint = (int)((s0 & 0xe000) >> 13);
        if (!(joint & 1)) {
            xpos = (int16_t)(s2 & 0xffff);
            ypos = (int16_t)(s3 & 0xffff);
            xpos = (int16_t)(xpos + sprite_x_scroll);
            ypos = (int16_t)(ypos + sprite_y_scroll);
            if (group_enable) {
                int gn = (int)((s0 & 0x1800) >> 11);
                xpos = (int16_t)(xpos + gx[gn]);
                ypos = (int16_t)(ypos + gy[gn]);
            }
        } else {
            xpos = (int16_t)(xpos + (int16_t)(s2 & 0xffff));
            ypos = (int16_t)(ypos + (int16_t)(s3 & 0xffff));
        }
        int sx = xpos, sy = ypos;
        if (sprite_flip & 2) { xflip ^= 1; sx = (CY_SCREEN_W << 6) - sx; }
        if (sprite_flip & 1) { yflip ^= 1; sy = (CY_SCREEN_H << 6) - sy; }
        if (!(joint & 2)) colour = (int)(s0 & 0x3f);
        if (!(joint & 4)) { romoffset = (int)(s1 & 0x07ffffff); pri = (int)((s0 & 0xc0) >> 6); }
        else romoffset = endromoffs;
        int grow = (int)((s0 >> 23) & 1);
        uint16_t zoomx_m, zoomx_s, zoomy_m, zoomy_s;
        if (!grow) {
            zoomx_m = (uint16_t)((s2 & 0xff000000) >> 16); zoomx_s = (uint16_t)((s2 & 0x00ff0000) >> 8);
            zoomy_m = (uint16_t)((s3 & 0xff000000) >> 16); zoomy_s = (uint16_t)((s3 & 0x00ff0000) >> 8);
        } else {
            zoomx_m = 0; zoomx_s = (uint16_t)((s2 & 0xffff0000) >> 16);
            zoomy_m = 0; zoomy_s = (uint16_t)((s3 & 0xffff0000) >> 16);
        }
        endromoffs = rle_decode(b, romoffset, size);
        int nc = (colour << 8) | (pri << 14);
        if (zoomx_m || zoomx_s || zoomy_m || zoomy_s) {
            blit_z(bm, (xflip << 1) | yflip, decodebuffer, sx, sy, xsize, ysize, zoomx_m, zoomx_s, zoomy_m, zoomy_s, nc);
            continue;
        }
        sx >>= 6; sy >>= 6;
        if (xflip) sx -= xsize;
        if (yflip) sy -= ysize;
        for (int xx = 0; xx < xsize; xx++) {
            int dx = xflip ? sx + (xsize - 1 - xx) : sx + xx;
            if (dx >= CY_SCREEN_W || dx < 0) continue;
            for (int yy = 0; yy < ysize; yy++) {
                int dy = yflip ? sy + (ysize - 1 - yy) : sy + yy;
                if (dy >= CY_SCREEN_H || dy < 0) continue;
                int pix = decodebuffer[xsize * yy + xx];
                if (pix) plot(bm, dx, dy, pix + nc);
            }
        }
    }
}
