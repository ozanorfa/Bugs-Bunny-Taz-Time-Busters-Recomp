/*
 * Donor textures — see donor_tex.h. Formats (NOTES.md "M3"):
 *   - model blob: u32 [0] (part count << 16), [1] primitive list offset,
 *     [2] first part offset; primitive records follow each other, the
 *     record size in bits 16-23 of the record's word 3, the GPU opcode in
 *     bits 0-7. Textured records: u16 +0 uv0, +2 CLUT, +4 uv1, +6 texture
 *     page, +8 uv2, +10 uv3 (quads; not every quad stores its fourth texel
 *     there).
 *   - TIM list: record kind 0x28, tags 0x29 = (offset, size) into the
 *     type-3 chunk, in upload order; each a TIM (0x10, flags, [CLUT block
 *     if flags & 8], image block; block = u32 size, x, y, w, h, pixels).
 *     The game uploads the image block, then the CLUT block, and the
 *     resulting CLUT colours have bit 15 set when non-zero (checked against
 *     VRAM in TRCA01).
 */
#include "donor_tex.h"

#include <stdlib.h>
#include <string.h>

#include "bouze.h"

#define CHUNK_TEXTURES   3u
#define KIND_TIM_LIST    0x28u
#define TAG_TIM_ENTRY    0x29u
#define TIM_MAGIC        0x10u
#define TIM_HAS_CLUT     0x08u

static int op_quad(uint8_t op) { return (op & 0x08) != 0; }

/* A textured gouraud triangle (GPU 0x34-0x37, 32-byte record) or quad
 * (0x3C-0x3F, 36 bytes) with a CLUT. Records with the same opcode but 4
 * more bytes and flag 0x80 in byte 1 of word 3 carry per-vertex colours
 * where the texel fields would be (seen in Taz's model). */
static int record_textured(uint32_t w3, uint32_t size, uint16_t clut)
{
    const uint8_t op = (uint8_t)w3;
    if (op < 0x34 || op > 0x3F || !(op & 0x04) || (w3 & 0x8000u) || clut == 0)
        return 0;
    return size == (op_quad(op) ? 36u : 32u);
}

/* ---- model analysis ---------------------------------------------------- */

typedef struct Box { int u0, u1, v0, v1; } Box;

static int analyse_model(const uint8_t* m, uint32_t size, uint32_t image_off,
                         DonorTex* t, Box* boxes)
{
    if (size < 12)
        return 0;
    const uint32_t prim = bouze_u32(m + 4), first = bouze_u32(m + 8);
    if (prim < 12 || prim >= first || first > size)
        return 0;

    /* Pass 1: the 4-bit page most textured records use. */
    uint32_t page_count[32] = {0};
    for (uint32_t p = prim; p + 16u <= first;) {
        const uint32_t w3 = bouze_u32(m + p + 12);
        const uint32_t rs = (w3 >> 16) & 0xFFu;
        if (rs < 16u || rs > first - p)
            break;
        const uint16_t tp = bouze_u16(m + p + 6);
        if (record_textured(w3, rs, bouze_u16(m + p + 2)) && tp < 0x200u && ((tp >> 7) & 3u) == 0)
            ++page_count[tp & 0x1Fu];
        p += rs;
    }
    uint32_t best = 0;
    for (uint32_t i = 1; i < 32; ++i)
        if (page_count[i] > page_count[best])
            best = i;
    if (page_count[best] == 0)
        return 0;
    t->tpage = (uint16_t)best;
    t->page_x = (uint16_t)((best & 0xFu) * 64u);
    t->page_y = (uint16_t)(((best >> 4) & 1u) * 256u);

    /* Pass 2: records of that page, grouped by CLUT, with their texel
     * boxes (uv0..uv2; the fourth texel of a quad is not always stored). */
    t->umin = 255;
    t->umax = 0;
    for (uint32_t p = prim; p + 16u <= first;) {
        const uint32_t w3 = bouze_u32(m + p + 12);
        const uint32_t rs = (w3 >> 16) & 0xFFu;
        if (rs < 16u || rs > first - p)
            break;
        const uint8_t op = (uint8_t)w3;
        const uint16_t tp = bouze_u16(m + p + 6);
        if (record_textured(w3, rs, bouze_u16(m + p + 2)) && tp < 0x200u) {
            if ((tp & 0x1Fu) != best || ((tp >> 7) & 3u) != 0) {
                ++t->skipped_records;
            } else {
                const uint16_t clut = bouze_u16(m + p + 2);
                int g = 0;
                while (g < t->clut_count && t->cluts[g] != clut)
                    ++g;
                if (g == t->clut_count) {
                    if (t->clut_count >= DONOR_TEX_MAX_CLUTS)
                        return 0;
                    t->cluts[t->clut_count++] = clut;
                    boxes[g].u0 = boxes[g].v0 = 255;
                    boxes[g].u1 = boxes[g].v1 = 0;
                }
                if (t->record_count >= DONOR_TEX_MAX_RECORDS)
                    return 0;
                DonorTexRecord* r = &t->records[t->record_count++];
                r->off = image_off + p;
                r->group = (uint8_t)g;
                r->shift_u3 = 0;
                for (int k = 0; k < 3; ++k) {
                    const int u = m[p + 4u * (uint32_t)k], v = m[p + 4u * (uint32_t)k + 1u];
                    if (u < boxes[g].u0) boxes[g].u0 = u;
                    if (u > boxes[g].u1) boxes[g].u1 = u;
                    if (v < boxes[g].v0) boxes[g].v0 = v;
                    if (v > boxes[g].v1) boxes[g].v1 = v;
                }
                r->shift_u3 = op_quad(op) ? 1 : 0;   /* refined below */
            }
        }
        p += rs;
    }
    if (t->record_count == 0)
        return 0;
    for (int i = 0; i < t->record_count; ++i) {
        DonorTexRecord* r = &t->records[i];
        if (r->shift_u3) {
            const uint32_t p = r->off - image_off;
            const int u = m[p + 10], v = m[p + 11];
            const Box* b = &boxes[r->group];
            r->shift_u3 = u >= b->u0 && u <= b->u1 && v >= b->v0 && v <= b->v1;
        }
        const Box* b = &boxes[r->group];
        if (b->u0 < t->umin) t->umin = (uint8_t)b->u0;
        if (b->u1 > t->umax) t->umax = (uint8_t)b->u1;
    }
    return 1;
}

/* ---- TIM replay -------------------------------------------------------- */

/* Uploads one TIM block. Some CLUT blocks declare 16 rows but carry one:
 * the game uploads the rows the block holds (16x1, seen in the upload
 * trace) and computes the other rows itself (shaded copies uploaded from a
 * stack buffer); those are not rebuilt here. Returns the rect in *rect. */
static int put_block(uint16_t* vram, const uint8_t* c3, size_t len, size_t p, size_t* next,
                     uint32_t rect[4])
{
    if (p + 12u > len)
        return 0;
    const uint32_t bnum = bouze_u32(c3 + p);
    const uint32_t x = bouze_u16(c3 + p + 4), y = bouze_u16(c3 + p + 6);
    const uint32_t w = bouze_u16(c3 + p + 8);
    uint32_t h = bouze_u16(c3 + p + 10);
    if (w == 0 || h == 0 || bnum < 12u + w * 2u || bnum > len - p)
        return 0;
    if ((bnum - 12u) / (w * 2u) < h)
        h = (bnum - 12u) / (w * 2u);
    if (x + w > VRAM_W || y + h > VRAM_H)
        return 0;
    const uint8_t* px = c3 + p + 12;
    for (uint32_t j = 0; j < h; ++j)
        for (uint32_t i = 0; i < w; ++i)
            vram[(y + j) * VRAM_W + x + i] = bouze_u16(px + 2u * (j * w + i));
    *next = p + bnum;
    rect[0] = x; rect[1] = y; rect[2] = w; rect[3] = h;
    return 1;
}

static int replay_tims(const uint8_t* s, size_t slen, const uint8_t* c3, size_t c3len,
                       uint16_t* vram)
{
    size_t pos = 0;
    BouzeRecord r;
    int k, tims = 0;
    while ((k = bouze_next_record(s, slen, &pos, &r)) == 1) {
        if (r.kind != KIND_TIM_LIST)
            continue;
        size_t tp = r.tags;
        BouzeTag t;
        while (bouze_next_tag(s, &r, &tp, &t)) {
            if (t.tag != TAG_TIM_ENTRY || t.len != 8)
                continue;
            const size_t off = bouze_u32(s + t.payload);
            if (off + 8u > c3len || bouze_u32(c3 + off) != TIM_MAGIC)
                return -1;
            const uint32_t flags = bouze_u32(c3 + off + 4);
            size_t p = off + 8, after_clut = p, end = 0;
            size_t clut_at = 0;
            if (flags & TIM_HAS_CLUT) {
                /* Skip the CLUT block to reach the image block first. */
                if (p + 12u > c3len)
                    return -1;
                clut_at = p;
                after_clut = p + bouze_u32(c3 + p);
                if (after_clut > c3len)
                    return -1;
            }
            uint32_t rect[4];
            if (!put_block(vram, c3, c3len, after_clut, &end, rect))
                return -1;
            if (clut_at) {
                size_t dummy;
                if (!put_block(vram, c3, c3len, clut_at, &dummy, rect))
                    return -1;
                /* CLUT colours as the game leaves them in VRAM. */
                for (uint32_t j = 0; j < rect[3]; ++j)
                    for (uint32_t i = 0; i < rect[2]; ++i) {
                        uint16_t* v = &vram[(rect[1] + j) * VRAM_W + rect[0] + i];
                        if (*v)
                            *v |= 0x8000u;
                    }
            }
            ++tims;
        }
    }
    return k == 0 ? tims : -1;
}

int donor_tex_extract(const uint8_t* file, size_t file_len, const Donor* d,
                      const DonorBlob* model, DonorTex* out)
{
    memset(out, 0, sizeof(*out));
    if (!d->image || !model || model->off > d->image_size ||
        model->size > d->image_size - model->off)
        return 0;
    Box boxes[DONOR_TEX_MAX_CLUTS];
    if (!analyse_model(d->image + model->off, model->size, model->off, out, boxes))
        return 0;

    BouzeHeader h;
    if (!bouze_parse_header(file, file_len, &h))
        return 0;
    const int cs = bouze_find_chunk(&h, BOUZE_CHUNK_STREAM);
    const int ct = bouze_find_chunk(&h, CHUNK_TEXTURES);
    if (cs < 0 || ct < 0)
        return 0;
    size_t slen = 0, tlen = 0;
    uint8_t* s = bouze_decompress(file + h.file_off[cs], h.size[cs], &slen);
    uint8_t* c3 = bouze_decompress(file + h.file_off[ct], h.size[ct], &tlen);
    uint16_t* vram = (uint16_t*)calloc(VRAM_W * VRAM_H, sizeof(uint16_t));
    int ok = 0;
    if (!s || !c3 || !vram || replay_tims(s, slen, c3, tlen, vram) <= 0)
        goto out;

    /* One block per CLUT group, then the CLUTs. */
    uint32_t n = 0;
    for (int g = 0; g < out->clut_count; ++g) {
        DonorTexBlock* b = &out->blocks[out->block_count++];
        b->x = (uint16_t)(out->page_x + boxes[g].u0 / 4);
        b->w = (uint16_t)(boxes[g].u1 / 4 - boxes[g].u0 / 4 + 1);
        b->y = (uint16_t)(out->page_y + boxes[g].v0);
        b->h = (uint16_t)(boxes[g].v1 - boxes[g].v0 + 1);
        b->px = n;
        n += (uint32_t)b->w * b->h;
    }
    out->clut_px = n;
    n += 16u * (uint32_t)out->clut_count;
    out->pixels = (uint16_t*)malloc(n * sizeof(uint16_t));
    if (!out->pixels)
        goto out;
    out->pixel_count = n;
    for (int i = 0; i < out->block_count; ++i) {
        const DonorTexBlock* b = &out->blocks[i];
        for (uint32_t j = 0; j < b->h; ++j)
            memcpy(out->pixels + b->px + j * b->w, vram + (b->y + j) * VRAM_W + b->x,
                   (size_t)b->w * sizeof(uint16_t));
    }
    for (int g = 0; g < out->clut_count; ++g) {
        const uint32_t cx = (out->cluts[g] & 0x3Fu) * 16u, cy = (uint32_t)out->cluts[g] >> 6;
        if (cy >= VRAM_H)
            goto out;
        memcpy(out->pixels + out->clut_px + 16u * (uint32_t)g, vram + cy * VRAM_W + cx,
               16u * sizeof(uint16_t));
    }
    ok = 1;
out:
    free(s);
    free(c3);
    free(vram);
    if (!ok)
        donor_tex_free(out);
    return ok;
}

void donor_tex_free(DonorTex* t)
{
    free(t->pixels);
    memset(t, 0, sizeof(*t));
}

/* ---- VRAM map ---------------------------------------------------------- */

void vram_map_clear(VramMap* m) { memset(m->bits, 0, sizeof(m->bits)); }

void vram_map_mark(VramMap* m, int x, int y, int w, int h)
{
    for (int j = y < 0 ? 0 : y; j < y + h && j < (int)VRAM_H; ++j)
        for (int i = x < 0 ? 0 : x; i < x + w && i < (int)VRAM_W; ++i) {
            const uint32_t bit = (uint32_t)j * VRAM_W + (uint32_t)i;
            m->bits[bit >> 3] |= (uint8_t)(1u << (bit & 7u));
        }
}

int vram_map_is_free(const VramMap* m, int x, int y, int w, int h)
{
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > (int)VRAM_W || y + h > (int)VRAM_H)
        return 0;
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) {
            const uint32_t bit = (uint32_t)j * VRAM_W + (uint32_t)i;
            if (m->bits[bit >> 3] & (1u << (bit & 7u)))
                return 0;
        }
    return 1;
}

/* ---- target pool ------------------------------------------------------- */

int donor_tex_pool_from_model(const uint8_t* m, uint32_t size, DonorTexPool* pool)
{
    memset(pool, 0, sizeof(*pool));
    if (size < 12)
        return 0;
    const uint32_t prim = bouze_u32(m + 4), first = bouze_u32(m + 8);
    if (prim < 12 || prim >= first || first > size)
        return 0;
    uint16_t key[DONOR_POOL_MAX];
    int u0[DONOR_POOL_MAX], u1[DONOR_POOL_MAX], v0[DONOR_POOL_MAX], v1[DONOR_POOL_MAX];
    int n = 0;
    for (uint32_t p = prim; p + 16u <= first;) {
        const uint32_t w3 = bouze_u32(m + p + 12);
        const uint32_t rs = (w3 >> 16) & 0xFFu;
        if (rs < 16u || rs > first - p)
            break;
        const uint16_t tp = bouze_u16(m + p + 6), clut = bouze_u16(m + p + 2);
        if (record_textured(w3, rs, clut) && tp < 0x200u && ((tp >> 7) & 3u) == 0 &&
            (clut >> 6) < VRAM_H) {
            /* One box per (page, CLUT) pair. */
            int g = 0;
            while (g < n && !(key[g] == (tp & 0x1Fu) && pool->clut_x[g] == (clut & 0x3Fu) * 16u &&
                              pool->clut_y[g] == (clut >> 6)))
                ++g;
            if (g == n) {
                if (n >= DONOR_POOL_MAX)
                    return 0;
                key[n] = (uint16_t)(tp & 0x1Fu);
                pool->clut_x[n] = (uint16_t)((clut & 0x3Fu) * 16u);
                pool->clut_y[n] = (uint16_t)(clut >> 6);
                u0[n] = v0[n] = 255;
                u1[n] = v1[n] = 0;
                ++n;
            }
            for (int k = 0; k < 3; ++k) {
                const int u = m[p + 4u * (uint32_t)k], v = m[p + 4u * (uint32_t)k + 1u];
                if (u < u0[g]) u0[g] = u;
                if (u > u1[g]) u1[g] = u;
                if (v < v0[g]) v0[g] = v;
                if (v > v1[g]) v1[g] = v;
            }
        }
        p += rs;
    }
    for (int g = 0; g < n; ++g) {
        const uint32_t px = (key[g] & 0xFu) * 64u, py = ((key[g] >> 4) & 1u) * 256u;
        pool->x[g] = (uint16_t)(px + (uint32_t)u0[g] / 4u);
        pool->y[g] = (uint16_t)(py + (uint32_t)v0[g]);
        pool->w[g] = (uint16_t)(u1[g] / 4 - u0[g] / 4 + 1);
        pool->h[g] = (uint16_t)(v1[g] - v0[g] + 1);
    }
    pool->rect_count = pool->clut_count = n;
    return n > 0;
}

/* ---- planning and patching -------------------------------------------- */

static int rect_ok(const VramMap* allowed, const VramMap* used, int x, int y, int w, int h)
{
    if ((x / 64) != ((x + w - 1) / 64) || (y / 256) != ((y + h - 1) / 256))
        return 0;                  /* a block must stay inside one texture page */
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i)
            if (vram_map_is_free(allowed, i, j, 1, 1) || !vram_map_is_free(used, i, j, 1, 1))
                return 0;
    return 1;
}

int donor_tex_plan(const DonorTex* t, const DonorTexPool* pool, const VramMap* uploaded,
                   DonorTexPlan* plan)
{
    memset(plan, 0, sizeof(*plan));
    if (t->clut_count == 0 || pool->rect_count == 0)
        return 0;
    VramMap* allowed = (VramMap*)calloc(1, sizeof(VramMap));
    VramMap* used = (VramMap*)calloc(1, sizeof(VramMap));
    int ok = 0;
    if (!allowed || !used)
        goto out;
    /* Allowed = inside the pool's texel boxes and uploaded by this level. */
    int x0 = (int)VRAM_W, y0 = (int)VRAM_H, x1 = 0, y1 = 0;
    for (int r = 0; r < pool->rect_count; ++r) {
        for (int j = pool->y[r]; j < pool->y[r] + pool->h[r]; ++j)
            for (int i = pool->x[r]; i < pool->x[r] + pool->w[r]; ++i)
                if (!vram_map_is_free(uploaded, i, j, 1, 1))
                    vram_map_mark(allowed, i, j, 1, 1);
        if (pool->x[r] < x0) x0 = pool->x[r];
        if (pool->y[r] < y0) y0 = pool->y[r];
        if (pool->x[r] + pool->w[r] > x1) x1 = pool->x[r] + pool->w[r];
        if (pool->y[r] + pool->h[r] > y1) y1 = pool->y[r] + pool->h[r];
    }

    /* CLUTs: the replaced character's own rows first, then rows inside its
     * texel boxes. */
    int g = 0;
    for (int i = 0; i < pool->clut_count && g < t->clut_count; ++i) {
        const int cx = pool->clut_x[i], cy = pool->clut_y[i];
        int dup = 0;
        for (int k = 0; k < g; ++k)
            dup |= plan->clut_x[k] == cx && plan->clut_y[k] == cy;
        if (dup || vram_map_is_free(uploaded, cx, cy, 1, 1) ||
            !vram_map_is_free(used, cx, cy, 16, 1))
            continue;
        int all = 1;
        for (int k = 0; k < 16; ++k)
            all &= !vram_map_is_free(uploaded, cx + k, cy, 1, 1);
        if (!all)
            continue;
        plan->clut_x[g] = (uint16_t)cx;
        plan->clut_y[g] = (uint16_t)cy;
        vram_map_mark(used, cx, cy, 16, 1);
        ++g;
    }
    for (int y = y0; y < y1 && g < t->clut_count; ++y)
        for (int x = (x0 + 15) & ~15; x + 16 <= x1 && g < t->clut_count; x += 16)
            if (rect_ok(allowed, used, x, y, 16, 1)) {
                plan->clut_x[g] = (uint16_t)x;
                plan->clut_y[g] = (uint16_t)y;
                vram_map_mark(used, x, y, 16, 1);
                ++g;
            }
    if (g < t->clut_count)
        goto out;

    /* Blocks, largest first. */
    int order[DONOR_TEX_MAX_BLOCKS];
    for (int i = 0; i < t->block_count; ++i)
        order[i] = i;
    for (int i = 1; i < t->block_count; ++i)
        for (int k = i; k > 0; --k) {
            const DonorTexBlock* a = &t->blocks[order[k - 1]];
            const DonorTexBlock* b = &t->blocks[order[k]];
            if ((int)a->w * a->h >= (int)b->w * b->h)
                break;
            const int tmp = order[k - 1];
            order[k - 1] = order[k];
            order[k] = tmp;
        }
    for (int n = 0; n < t->block_count; ++n) {
        const int bi = order[n];
        const DonorTexBlock* b = &t->blocks[bi];
        int placed = 0;
        for (int x = x0; x + b->w <= x1 && !placed; ++x)
            for (int y = y0; y + b->h <= y1 && !placed; ++y)
                if (!vram_map_is_free(allowed, x, y, 1, 1) && rect_ok(allowed, used, x, y, b->w, b->h)) {
                    vram_map_mark(used, x, y, b->w, b->h);
                    plan->block_x[bi] = (uint16_t)x;
                    plan->block_y[bi] = (uint16_t)y;
                    const int px = x & ~63, py = y & ~255;
                    plan->tpage[bi] = (uint16_t)((px / 64) | ((py / 256) << 4));
                    plan->du[bi] = (int16_t)(4 * ((x - px) - (b->x - t->page_x)));
                    plan->dv[bi] = (int16_t)((y - py) - (b->y - t->page_y));
                    placed = 1;
                }
        if (!placed)
            goto out;
    }
    ok = 1;
out:
    free(allowed);
    free(used);
    return ok;
}

void donor_tex_patch_record(const DonorTex* t, const DonorTexPlan* plan,
                            const DonorTexRecord* r, const uint8_t in[12], uint8_t out[12])
{
    (void)t;
    memcpy(out, in, 12);
    const int g = r->group, du = plan->du[g], dv = plan->dv[g];
    for (int k = 0; k < 3; ++k) {
        out[4 * k] = (uint8_t)(in[4 * k] + du);
        out[4 * k + 1] = (uint8_t)(in[4 * k + 1] + dv);
    }
    if (r->shift_u3) {
        out[10] = (uint8_t)(in[10] + du);
        out[11] = (uint8_t)(in[11] + dv);
    }
    const uint16_t clut = (uint16_t)(((uint32_t)plan->clut_y[g] << 6) | (plan->clut_x[g] >> 4));
    out[2] = (uint8_t)clut;
    out[3] = (uint8_t)(clut >> 8);
    const uint16_t tp = (uint16_t)(in[6] | (in[7] << 8));
    const uint16_t ntp = (uint16_t)((tp & ~0x1Fu) | plan->tpage[g]);
    out[6] = (uint8_t)ntp;
    out[7] = (uint8_t)(ntp >> 8);
}
