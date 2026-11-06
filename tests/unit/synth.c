/* Synthetic game data for the hermetic tests — see synth.h. */
#include "synth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "donor.h"

/* ---- buffers ------------------------------------------------------------ */

void buf_init(Buf* b) { memset(b, 0, sizeof(*b)); }

void buf_free(Buf* b)
{
    free(b->p);
    memset(b, 0, sizeof(*b));
}

static void grow(Buf* b, size_t extra)
{
    if (b->n + extra <= b->cap)
        return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->n + extra)
        cap *= 2;
    uint8_t* p = (uint8_t*)realloc(b->p, cap);
    if (!p) {
        fprintf(stderr, "synth: out of memory\n");
        exit(2);
    }
    b->p = p;
    b->cap = cap;
}

void put8(Buf* b, uint8_t v)
{
    grow(b, 1);
    b->p[b->n++] = v;
}
void put16(Buf* b, uint16_t v)
{
    put8(b, (uint8_t)v);
    put8(b, (uint8_t)(v >> 8));
}
void put32(Buf* b, uint32_t v)
{
    put16(b, (uint16_t)v);
    put16(b, (uint16_t)(v >> 16));
}
void put_bytes(Buf* b, const void* p, size_t n)
{
    grow(b, n);
    memcpy(b->p + b->n, p, n);
    b->n += n;
}
void put_zero(Buf* b, size_t n)
{
    grow(b, n);
    memset(b->p + b->n, 0, n);
    b->n += n;
}
void put_align4(Buf* b)
{
    while (b->n & 3u)
        put8(b, 0);
}
void set32(Buf* b, size_t at, uint32_t v)
{
    b->p[at] = (uint8_t)v;
    b->p[at + 1] = (uint8_t)(v >> 8);
    b->p[at + 2] = (uint8_t)(v >> 16);
    b->p[at + 3] = (uint8_t)(v >> 24);
}

/* ---- Bouze containers --------------------------------------------------- */

void synth_store(const uint8_t* data, size_t n, Buf* out)
{
    /* byte 0 = parameters (0: 16-bit match = 9-bit distance, 7-bit length),
     * bytes 1..3 = token count - 1 in the order b3 | b1 << 16 | b2 << 8
     * (the decoder runs one token past the count), then groups of one flag
     * byte (1 = literal, LSB first) and up to eight literals. */
    const uint32_t count = (uint32_t)n - 1u;
    put8(out, 0);
    put8(out, (uint8_t)(count >> 16));
    put8(out, (uint8_t)(count >> 8));
    put8(out, (uint8_t)count);
    for (size_t i = 0; i < n; i += 8) {
        put8(out, 0xFF);
        put_bytes(out, data + i, n - i < 8 ? n - i : 8);
    }
}

void synth_bzz(const SynthChunk* c, int n, Buf* out)
{
    Buf streams[8];
    put32(out, 1);           /* version */
    put32(out, 0);           /* data offset (unused here) */
    put32(out, (uint32_t)n);
    for (int i = 0; i < n; ++i) {
        buf_init(&streams[i]);
        synth_store(c[i].data, c[i].len, &streams[i]);
        put32(out, c[i].type);
        put32(out, (uint32_t)streams[i].n);
        put32(out, (uint32_t)((streams[i].n + 0x7FFu) & ~(size_t)0x7FFu));
    }
    put_zero(out, 0x800u - out->n);
    for (int i = 0; i < n; ++i) {
        put_bytes(out, streams[i].p, streams[i].n);
        put_zero(out, ((streams[i].n + 0x7FFu) & ~(size_t)0x7FFu) - streams[i].n);
        buf_free(&streams[i]);
    }
}

/* ---- blobs -------------------------------------------------------------- */

void synth_model(const SynthPrim* prims, int n, Buf* out)
{
    uint32_t total = 0;
    for (int i = 0; i < n; ++i)
        total += prims[i].size;
    put32(out, 1u << 16);    /* one part */
    put32(out, 12);          /* primitive list */
    put32(out, 12 + total);  /* first part */
    for (int i = 0; i < n; ++i) {
        const SynthPrim* p = &prims[i];
        const size_t at = out->n;
        put8(out, p->uv[0][0]); put8(out, p->uv[0][1]);
        put16(out, p->clut);
        put8(out, p->uv[1][0]); put8(out, p->uv[1][1]);
        put16(out, p->tpage);
        put8(out, p->uv[2][0]); put8(out, p->uv[2][1]);
        put8(out, p->uv[3][0]); put8(out, p->uv[3][1]);
        put32(out, (uint32_t)p->op | ((uint32_t)p->flags << 8) | ((uint32_t)p->size << 16));
        put_zero(out, p->size - (out->n - at));
    }
    put32(out, 0);           /* part area */
}

void synth_anim(uint16_t frames, uint8_t ticks, const uint8_t* roots, Buf* out)
{
    put8(out, 0x50); put8(out, 0); put8(out, (uint8_t)(ticks << 1)); put8(out, 0);
    put16(out, frames); put16(out, 0);
    for (uint16_t f = 0; f < frames; ++f) {
        const int root = roots && roots[f];
        const uint16_t halfwords = (uint16_t)(3 + (root ? 1 : 0));
        put16(out, (uint16_t)(halfwords + 2u));
        put16(out, (uint16_t)(root ? 2 : 1));
        put16(out, f);
        put16(out, 0);
        put16(out, 0x1101); put16(out, (uint16_t)(f * 3)); put16(out, 7);
        if (root)
            put16(out, (uint16_t)(0x0A00u | roots[f]));
    }
}

/* ---- the synthetic level ----------------------------------------------- */

static void actor(Buf* s, uint32_t type, const uint16_t* ids, int n, int scale, size_t* scale_at)
{
    put8(s, 0x2C);
    put8(s, 0x07);
    put8(s, 0x16);
    put32(s, type);
    put_zero(s, 24);
    for (int i = 0; i < n; ++i) {
        put8(s, 0x0F);
        put16(s, ids[i]);
    }
    if (scale) {
        put8(s, 0x12);
        if (scale_at)
            *scale_at = s->n;
        put32(s, 0x1000); put32(s, 0x1000); put32(s, 0x1000);
    }
    put8(s, 0x2D);
}

static void resource(Buf* s, uint16_t sub, uint16_t id, int anim, uint32_t off, uint32_t size)
{
    put8(s, 0x2C);
    put8(s, 0x22);
    put8(s, 0x27); put16(s, sub); put16(s, id);
    put8(s, anim ? 0x25 : 0x24); put32(s, off); put32(s, size);
    put8(s, 0x2D);
}

static void add_blob(SynthLevel* l, uint16_t id, const Buf* b)
{
    put_align4(&l->blobs);
    l->off[id] = (uint32_t)l->blobs.n;
    l->size[id] = (uint32_t)b->n;
    put_bytes(&l->blobs, b->p, b->n);
}

void synth_level_build(SynthLevel* l)
{
    memset(l, 0, sizeof(*l));
    Buf b;

    /* Donor model 10, page 5 (x 320): two CLUT groups, plus records the
     * texture analysis must leave out. */
    static const SynthPrim kDonorPrims[] = {
        {0x34, 0x00, 32, 0x7D00, 0x0005, {{0, 0}, {31, 0}, {0, 15}, {0, 0}}},
        {0x3C, 0x00, 36, 0x7D01, 0x0005, {{64, 32}, {95, 32}, {64, 47}, {95, 47}}},
        {0x3C, 0x00, 36, 0x7D00, 0x0025, {{8, 4}, {16, 4}, {8, 8}, {200, 200}}},
        {0x34, 0x00, 32, 0x7D00, 0x0006, {{0, 0}, {4, 0}, {0, 4}, {0, 0}}},     /* other page */
        {0x3C, 0x80, 40, 0x7D00, 0x0005, {{1, 1}, {2, 2}, {3, 3}, {4, 4}}},     /* vertex colours */
        {0x34, 0x00, 32, 0x0000, 0x0005, {{1, 1}, {2, 2}, {3, 3}, {0, 0}}},     /* no CLUT */
        {0x30, 0x00, 28, 0x7D00, 0x0005, {{1, 1}, {2, 2}, {3, 3}, {0, 0}}},     /* untextured */
    };
    buf_init(&b); synth_model(kDonorPrims, 7, &b); add_blob(l, 10, &b); buf_free(&b);
    static const uint8_t kRoots11[2] = {5, 23};
    buf_init(&b); synth_anim(2, 1, kRoots11, &b); add_blob(l, 11, &b); buf_free(&b);
    static const uint8_t kRoots12[4] = {23, 23, 23, 0};
    buf_init(&b); synth_anim(4, 2, kRoots12, &b); add_blob(l, 12, &b); buf_free(&b);

    /* Target model 20, page 7 (x 448) with semi-transparency bits set:
     * one 16x64 texel box with CLUT (32, 481). */
    static const SynthPrim kTargetPrims[] = {
        {0x3C, 0x00, 36, 0x7842, 0x0027, {{0, 0}, {63, 0}, {0, 63}, {63, 63}}},
    };
    buf_init(&b); synth_model(kTargetPrims, 1, &b); add_blob(l, 20, &b); buf_free(&b);
    buf_init(&b); synth_anim(3, 2, NULL, &b); add_blob(l, 21, &b); buf_free(&b);
    buf_init(&b); synth_anim(10, 3, NULL, &b); add_blob(l, 22, &b); buf_free(&b);
    static const SynthPrim kOtherPrims[] = {
        {0x34, 0x00, 32, 0x7843, 0x0008, {{0, 0}, {7, 0}, {0, 7}, {0, 0}}},
    };
    buf_init(&b); synth_model(kOtherPrims, 1, &b); add_blob(l, 30, &b); buf_free(&b);
    put_align4(&l->blobs);

    /* Record stream. */
    static const uint16_t kDonorIds[] = {10, 11, 12};
    static const uint16_t kTargetIds[] = {20, 21, 22};
    static const uint16_t kOtherIds[] = {30};
    actor(&l->stream, SYN_TYPE_DONOR, kDonorIds, 3, 0, NULL);
    actor(&l->stream, SYN_TYPE_TARGET, kTargetIds, 3, 1, &l->target_scale_payload);
    actor(&l->stream, SYN_TYPE_OTHER, kOtherIds, 1, 1, NULL);
    put8(&l->stream, 0x99);  /* a kind the loader does not handle: skipped */
    resource(&l->stream, 0x000D, 10, 0, l->off[10], l->size[10]);
    resource(&l->stream, 0x0001, 11, 1, l->off[11], l->size[11]);
    resource(&l->stream, 0x0033, 12, 1, l->off[12], l->size[12]);
    resource(&l->stream, 0x000D, 20, 0, l->off[20], l->size[20]);
    resource(&l->stream, 0x0033, 21, 1, l->off[21], l->size[21]);
    resource(&l->stream, 0x0099, 22, 1, l->off[22], l->size[22]);
    resource(&l->stream, 0x000D, 30, 0, l->off[30], l->size[30]);

    /* Texture chunk: one TIM. Its CLUT block (at y 500) declares 16 rows
     * but carries one, as some on the disc do: the declared rows would run
     * past the bottom of VRAM. One CLUT colour is 0. */
    Buf* t = &l->textures;
    put32(t, 0x10); put32(t, 8);
    put32(t, 12 + 32 * 2); put16(t, 0); put16(t, 500); put16(t, 32); put16(t, 16);
    for (int k = 0; k < 32; ++k)
        put16(t, (uint16_t)(k == 5 ? 0 : 0x0100 + k));
    put32(t, 12 + 32 * 64 * 2); put16(t, 320); put16(t, 0); put16(t, 32); put16(t, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 32; ++x)
            put16(t, (uint16_t)(0x1000 + x + 32 * y));
    put8(&l->stream, 0x2C);
    put8(&l->stream, 0x28);
    put8(&l->stream, 0x29); put32(&l->stream, 0); put32(&l->stream, (uint32_t)t->n);
    put8(&l->stream, 0x2D);
    put8(&l->stream, 0x2E);

    const SynthChunk chunks[3] = {
        {1, l->stream.p, l->stream.n},
        {3, l->textures.p, l->textures.n},
        {4, l->blobs.p, l->blobs.n},
    };
    synth_bzz(chunks, 3, &l->file);
}

void synth_level_free(SynthLevel* l)
{
    buf_free(&l->stream);
    buf_free(&l->blobs);
    buf_free(&l->textures);
    buf_free(&l->file);
}

static int write_file(const char* path, const uint8_t* p, size_t n)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return 0;
    const int ok = fwrite(p, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

int synth_dump_for_python(const char* spin_path, const char* dir)
{
    SynthLevel l;
    synth_level_build(&l);
    Donor d;
    int ok = donor_extract(l.file.p, l.file.n, SYN_TYPE_DONOR, &d);
    if (ok) {
        ok = donor_add_spin(&d, 0xF001, 32) != 0;
        const DonorBlob* s = &d.anims[d.anim_count - 1];
        ok = ok && write_file(spin_path, d.image + s->off, s->size);
        donor_free(&d);
    }
    if (ok && dir) {
        char path[1024];
        snprintf(path, sizeof path, "%s/synth.bzz", dir);
        ok = write_file(path, l.file.p, l.file.n);
        const Buf* plain[3] = {&l.stream, &l.textures, &l.blobs};
        for (int i = 0; ok && i < 3; ++i) {
            snprintf(path, sizeof path, "%s/synth_chunk%d.bin", dir, i);
            ok = write_file(path, plain[i]->p, plain[i]->n);
        }
    }
    synth_level_free(&l);
    return ok;
}
