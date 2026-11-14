/* Yamaha YMZ280B PCMD8, after MAME 0.264 src/devices/sound/ymz280b.cpp
 * (BSD-3-Clause, Aaron Giles).  Output is produced at the chip's internal
 * rate (clock / 192) and mixed as MAME does: each voice adds
 sample * level / 512 in 16-bit units (accumulated exactly, divided once).  The IRQ output is not connected
 * on the SKNS. */
#include "cy/board.h"

#include <string.h>

#define FRAC_BITS 9
#define FRAC_ONE (1 << FRAC_BITS)
#define MAX_SAMPLE_CHUNK 10000

static const int index_scale[8] = { 0x0e6, 0x0e6, 0x0e6, 0x0e6, 0x133, 0x199, 0x200, 0x266 };
static int diff_lookup[16];
static int tables_ready;
static int16_t scratch[MAX_SAMPLE_CHUNK];

static void compute_tables(void)
{
    for (int nib = 0; nib < 16; nib++) {
        int value = (nib & 0x07) * 2 + 1;
        diff_lookup[nib] = (nib & 0x08) ? -value : value;
    }
    tables_ready = 1;
}

static inline uint8_t rom(const cy_board *b, uint32_t off) { return b->samples[off & 0x3FFFFF]; }

static void update_step(cy_ymz_voice *v)
{
    int frequency = v->mode == 1 ? (v->fnum & 0x0ff) : (v->fnum & 0x1ff);
    v->output_step = frequency + 1;
}

static void update_volumes(cy_ymz_voice *v)
{
    if (v->pan == 8) { v->output_left = v->level; v->output_right = v->level; }
    else if (v->pan < 8) { v->output_left = v->level; v->output_right = (v->pan == 0) ? 0 : v->level * (v->pan - 1) / 7; }
    else { v->output_left = v->level * (15 - v->pan) / 7; v->output_right = v->level; }
}

static int generate_adpcm(const cy_board *b, cy_ymz_voice *v, int16_t *buf, int samples)
{
    int position = (int)v->position, signal = v->signal, step = v->step, val;
    while (samples) {
        val = rom(b, (uint32_t)position / 2) >> ((~position & 1) << 2);
        signal += (step * diff_lookup[val & 15]) / 8;
        if (signal > 32767) signal = 32767; else if (signal < -32768) signal = -32768;
        step = (step * index_scale[val & 7]) >> 8;
        if (step > 0x6000) step = 0x6000; else if (step < 0x7f) step = 0x7f;
        *buf++ = (int16_t)signal;
        samples--;
        position++;
        if (v->looping) {
            if ((uint32_t)position == v->loop_start && v->loop_count == 0) { v->loop_signal = signal; v->loop_step = step; }
            if ((uint32_t)position >= v->loop_end && v->keyon) {
                position = (int)v->loop_start; signal = v->loop_signal; step = v->loop_step; v->loop_count++;
            }
        }
        if ((uint32_t)position >= v->stop) { v->ended = 1; break; }
    }
    v->position = (uint32_t)position; v->signal = signal; v->step = step;
    return samples;
}

static int generate_pcm8(const cy_board *b, cy_ymz_voice *v, int16_t *buf, int samples)
{
    uint32_t position = v->position;
    while (samples) {
        *buf++ = (int16_t)((int8_t)rom(b, position / 2) * 256);
        samples--;
        position += 2;
        if (v->looping && position >= v->loop_end && v->keyon) position = v->loop_start;
        if (position >= v->stop) { v->ended = 1; break; }
    }
    v->position = position;
    return samples;
}

static int generate_pcm16(const cy_board *b, cy_ymz_voice *v, int16_t *buf, int samples)
{
    uint32_t position = v->position;
    while (samples) {
        *buf++ = (int16_t)((rom(b, position / 2 + 1) << 8) + rom(b, position / 2));
        samples--;
        position += 4;
        if (v->looping && position >= v->loop_end && v->keyon) position = v->loop_start;
        if (position >= v->stop) { v->ended = 1; break; }
    }
    v->position = position;
    return samples;
}

static void write_to_register(cy_board *b, int data)
{
    cy_ymz *z = &b->ymz;
    if (z->current_register < 0x80) {
        cy_ymz_voice *v = &z->voice[(z->current_register >> 2) & 7];
        switch (z->current_register & 0xe3) {
        case 0x00: v->fnum = (uint16_t)((v->fnum & 0x100) | (data & 0xff)); update_step(v); break;
        case 0x01:
            v->fnum = (uint16_t)((v->fnum & 0xff) | ((data & 0x01) << 8));
            v->looping = (uint8_t)((data & 0x10) >> 4);
            if ((data & 0x60) == 0) data &= 0x7f;
            else v->mode = (uint8_t)((data & 0x60) >> 5);
            if (!v->keyon && (data & 0x80) && z->keyon_enable) {
                v->playing = 1; v->position = v->start;
                v->signal = v->loop_signal = 0; v->step = v->loop_step = 0x7f; v->loop_count = 0;
                v->irq_schedule = 0;
            } else if (v->keyon && !(data & 0x80)) {
                v->playing = 0; v->irq_schedule = 0;
            }
            v->keyon = (uint8_t)((data & 0x80) >> 7);
            update_step(v);
            break;
        case 0x02: v->level = (uint8_t)data; update_volumes(v); break;
        case 0x03: v->pan = (uint8_t)(data & 0x0f); update_volumes(v); break;
        case 0x20: v->start = (v->start & (0x00ffffu << 1)) | ((uint32_t)data << 17); break;
        case 0x21: v->loop_start = (v->loop_start & (0x00ffffu << 1)) | ((uint32_t)data << 17); break;
        case 0x22: v->loop_end = (v->loop_end & (0x00ffffu << 1)) | ((uint32_t)data << 17); break;
        case 0x23: v->stop = (v->stop & (0x00ffffu << 1)) | ((uint32_t)data << 17); break;
        case 0x40: v->start = (v->start & (0xff00ffu << 1)) | ((uint32_t)data << 9); break;
        case 0x41: v->loop_start = (v->loop_start & (0xff00ffu << 1)) | ((uint32_t)data << 9); break;
        case 0x42: v->loop_end = (v->loop_end & (0xff00ffu << 1)) | ((uint32_t)data << 9); break;
        case 0x43: v->stop = (v->stop & (0xff00ffu << 1)) | ((uint32_t)data << 9); break;
        case 0x60: v->start = (v->start & (0xffff00u << 1)) | ((uint32_t)data << 1); break;
        case 0x61: v->loop_start = (v->loop_start & (0xffff00u << 1)) | ((uint32_t)data << 1); break;
        case 0x62: v->loop_end = (v->loop_end & (0xffff00u << 1)) | ((uint32_t)data << 1); break;
        case 0x63: v->stop = (v->stop & (0xffff00u << 1)) | ((uint32_t)data << 1); break;
        default: break;
        }
    } else {
        switch (z->current_register) {
        case 0x84: z->ext_mem_address_hi = (uint32_t)data << 16; break;
        case 0x85: z->ext_mem_address_mid = (uint32_t)data << 8; break;
        case 0x86:
            z->ext_mem_address = z->ext_mem_address_hi | z->ext_mem_address_mid | (uint32_t)data;
            if (z->ext_mem_enable) z->ext_readlatch = rom(b, z->ext_mem_address);
            break;
        case 0x87:
            if (z->ext_mem_enable) z->ext_mem_address = (z->ext_mem_address + 1) & 0xffffff;   /* ROM: write ignored */
            break;
        case 0xfe: z->irq_mask = (uint8_t)data; break;
        case 0xff:
            z->ext_mem_enable = (uint8_t)((data & 0x40) >> 6);
            z->irq_enable = (uint8_t)((data & 0x10) >> 4);
            if (z->keyon_enable && !(data & 0x80)) {
                for (int i = 0; i < 8; i++) { z->voice[i].playing = 0; z->voice[i].irq_schedule = 0; }
            } else if (!z->keyon_enable && (data & 0x80)) {
                for (int i = 0; i < 8; i++) if (z->voice[i].keyon && z->voice[i].looping) z->voice[i].playing = 1;
            }
            z->keyon_enable = (uint8_t)((data & 0x80) >> 7);
            break;
        default: break;
        }
    }
}

void cy_ymz_reset(cy_ymz *z)
{
    if (!tables_ready) compute_tables();
    memset(z, 0, sizeof(*z));
    /* device_reset writes 0 to every register (from 0xff down); with a zeroed
     * chip that only matters for voice state, which memset already cleared */
    for (int i = 0; i < 8; i++) { z->voice[i].output_pos = FRAC_ONE; update_step(&z->voice[i]); update_volumes(&z->voice[i]); }
}

uint8_t cy_ymz_read(cy_board *b, unsigned offset)
{
    cy_ymz *z = &b->ymz;
    if ((offset & 1) == 0) {
        if (!z->ext_mem_enable) return 0xff;
        uint8_t ret = z->ext_readlatch;
        z->ext_readlatch = rom(b, z->ext_mem_address);
        z->ext_mem_address = (z->ext_mem_address + 1) & 0xffffff;
        return ret;
    }
    /* status: voices that ended set their bit (MAME raises it on the next CPU slice) */
    uint8_t result = z->status_register;
    for (int i = 0; i < 8; i++)
        if (z->voice[i].irq_schedule) { result |= (uint8_t)(1u << i); z->voice[i].irq_schedule = 0; z->voice[i].playing = 0; }
    z->status_register = 0;
    return result;
}

void cy_ymz_write(cy_board *b, unsigned offset, uint8_t data)
{
    if ((offset & 1) == 0) b->ymz.current_register = data;
    else write_to_register(b, data);
}

void cy_ymz_render(cy_board *b, int32_t *lacc, int32_t *racc, int samples)
{
    cy_ymz *z = &b->ymz;
    if (!tables_ready) compute_tables();
    for (int i = 0; i < samples; i++) lacc[i] = racc[i] = 0;
    for (int vi = 0; vi < 8; vi++) {
        cy_ymz_voice *v = &z->voice[vi];
        int16_t prev = v->last_sample, curr = v->curr_sample;
        int16_t *curr_data = scratch;
        int sampindex = 0, remaining = samples;
        int lvol = v->output_left, rvol = v->output_right;
        if (!v->playing && curr == 0 && prev == 0) { v->output_pos = FRAC_ONE; continue; }
        while (remaining > 0 && v->output_pos < FRAC_ONE) {
            int s = ((int32_t)prev * (FRAC_ONE - v->output_pos) + (int32_t)curr * v->output_pos) >> FRAC_BITS;
            lacc[sampindex] += s * lvol;
            racc[sampindex] += s * rvol;
            sampindex++;
            v->output_pos += v->output_step;
            remaining--;
        }
        if (v->output_pos >= FRAC_ONE) v->output_pos -= FRAC_ONE;
        else continue;
        uint32_t final_pos = (uint32_t)v->output_pos + (uint32_t)remaining * (uint32_t)v->output_step;
        uint32_t new_samples = (final_pos + FRAC_ONE) >> FRAC_BITS;
        if (new_samples > MAX_SAMPLE_CHUNK) new_samples = MAX_SAMPLE_CHUNK;
        int samples_left;
        switch (v->playing << 7 | v->mode) {
        case 0x81: samples_left = generate_adpcm(b, v, scratch, (int)new_samples); break;
        case 0x82: samples_left = generate_pcm8(b, v, scratch, (int)new_samples); break;
        case 0x83: samples_left = generate_pcm16(b, v, scratch, (int)new_samples); break;
        default: samples_left = 0; memset(scratch, 0, new_samples * sizeof(scratch[0])); break;
        }
        if (samples_left || v->ended) {
            v->ended = 0;
            int base = (int)new_samples - samples_left;
            int t = (base == 0) ? curr : scratch[base - 1];
            for (int i = 0; i < samples_left; i++) {
                if (t < 0) t = -((-t * 15) >> 4);
                else if (t > 0) t = (t * 15) >> 4;
                scratch[base + i] = (int16_t)t;
            }
            if (base != 0) { v->playing = 0; v->irq_schedule = 1; }
        }
        prev = curr;
        curr = *curr_data++;
        while (remaining > 0) {
            while (remaining > 0 && v->output_pos < FRAC_ONE) {
                int s = ((int32_t)prev * (FRAC_ONE - v->output_pos) + (int32_t)curr * v->output_pos) >> FRAC_BITS;
                lacc[sampindex] += s * lvol;
                racc[sampindex] += s * rvol;
                sampindex++;
                v->output_pos += v->output_step;
                remaining--;
            }
            if (v->output_pos >= FRAC_ONE) { v->output_pos -= FRAC_ONE; prev = curr; curr = *curr_data++; }
        }
        v->last_sample = prev;
        v->curr_sample = curr;
    }
}
