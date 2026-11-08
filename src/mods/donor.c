/*
 * Donor character data — see donor.h. Record grammar and tag meanings:
 * NOTES.md "Bouze files" and "Resources" (verified against the
 * Level_LoadParse decompile, 0x80068174).
 */
#include "donor.h"

#include <stdlib.h>
#include <string.h>

#include "bouze.h"

#define ID_BITMAP_BYTES (65536u / 8u)

static void id_set(uint8_t* bm, uint16_t id) { bm[id >> 3] |= (uint8_t)(1u << (id & 7u)); }
static int id_has(const uint8_t* bm, uint16_t id) { return (bm[id >> 3] >> (id & 7u)) & 1u; }

/* Mark the resource ids referenced by every actor record of type `type`.
 * Returns the number of such actor records, or -1 on a malformed stream. */
static int collect_actor_ids(const uint8_t* s, size_t len, uint32_t type,
                             uint8_t* ids, int* distinct)
{
    size_t pos = 0;
    BouzeRecord r;
    int k, records = 0;
    *distinct = 0;
    while ((k = bouze_next_record(s, len, &pos, &r)) == 1) {
        if (!bouze_is_actor_kind(r.kind))
            continue;
        size_t tp = r.tags;
        BouzeTag t;
        int is_target = 0;
        while (bouze_next_tag(s, &r, &tp, &t))
            if (t.tag == BOUZE_TAG_ACTOR_HEADER && t.len == 28 &&
                bouze_u32(s + t.payload) == type)
                is_target = 1;
        if (!is_target)
            continue;
        ++records;
        tp = r.tags;
        while (bouze_next_tag(s, &r, &tp, &t)) {
            if (t.tag != BOUZE_TAG_ACTOR_RESOURCE || t.len != 2)
                continue;
            const uint16_t id = bouze_u16(s + t.payload);
            if (!id_has(ids, id)) {
                id_set(ids, id);
                ++*distinct;
            }
        }
    }
    return k == 0 ? records : -1;
}

/* Fields of one kind-0x22 resource record. */
typedef struct ResInfo {
    int has_id;
    uint16_t sub, id;
    int blob_tag;          /* 0, BOUZE_TAG_RES_MODEL or BOUZE_TAG_RES_ANIM */
    size_t blob_payload;   /* stream offset of the (offset, size) payload */
} ResInfo;

static void resource_info(const uint8_t* s, const BouzeRecord* r, ResInfo* ri)
{
    memset(ri, 0, sizeof(*ri));
    size_t tp = r->tags;
    BouzeTag t;
    while (bouze_next_tag(s, r, &tp, &t)) {
        if (t.tag == BOUZE_TAG_RES_ID && t.len == 4) {
            ri->has_id = 1;
            ri->sub = bouze_u16(s + t.payload);
            ri->id = bouze_u16(s + t.payload + 2);
        } else if ((t.tag == BOUZE_TAG_RES_MODEL || t.tag == BOUZE_TAG_RES_ANIM) &&
                   t.len == 8) {
            ri->blob_tag = t.tag;
            ri->blob_payload = t.payload;
        }
    }
}

/* The ids to take: those the actors of `actor_type` reference, or, when
 * `res_ids` is given, exactly those. */
static int extract(const uint8_t* file, size_t file_len, uint32_t actor_type,
                   const uint16_t* res_ids, int res_count, Donor* out)
{
    memset(out, 0, sizeof(*out));
    BouzeHeader h;
    if (!bouze_parse_header(file, file_len, &h))
        return 0;
    const int cs = bouze_find_chunk(&h, BOUZE_CHUNK_STREAM);
    const int cb = bouze_find_chunk(&h, BOUZE_CHUNK_BLOBS);
    if (cs < 0 || cb < 0)
        return 0;
    size_t slen = 0, dlen = 0;
    uint8_t* s = bouze_decompress(file + h.file_off[cs], h.size[cs], &slen);
    uint8_t* data = bouze_decompress(file + h.file_off[cb], h.size[cb], &dlen);
    uint8_t* ids = (uint8_t*)calloc(1, ID_BITMAP_BYTES);
    int ok = 0;
    if (!s || !data || !ids)
        goto out;

    int distinct = 0;
    if (res_ids) {
        for (int i = 0; i < res_count; ++i)
            if (!id_has(ids, res_ids[i])) {
                id_set(ids, res_ids[i]);
                ++distinct;
            }
        if (distinct == 0)
            goto out;
    } else if (collect_actor_ids(s, slen, actor_type, ids, &distinct) <= 0) {
        goto out;
    }

    /* Two passes over the resource records: size the image, then fill it. */
    for (int pass = 0; pass < 2; ++pass) {
        size_t pos = 0, at = 0;
        BouzeRecord r;
        int k;
        out->model_count = out->anim_count = 0;
        while ((k = bouze_next_record(s, slen, &pos, &r)) == 1) {
            if (r.kind != BOUZE_KIND_RESOURCE)
                continue;
            ResInfo ri;
            resource_info(s, &r, &ri);
            if (!ri.has_id || !ri.blob_tag || !id_has(ids, ri.id))
                continue;
            const uint32_t off = bouze_u32(s + ri.blob_payload);
            const uint32_t size = bouze_u32(s + ri.blob_payload + 4);
            if (size == 0 || off > dlen || size > dlen - off)
                goto out;
            DonorBlob* b;
            if (ri.blob_tag == BOUZE_TAG_RES_MODEL) {
                if (out->model_count >= DONOR_MAX_MODELS)
                    goto out;
                b = &out->models[out->model_count++];
            } else {
                if (out->anim_count >= DONOR_MAX_ANIMS)
                    goto out;
                b = &out->anims[out->anim_count++];
            }
            b->sub = ri.sub;
            b->id = ri.id;
            b->off = (uint32_t)at;
            b->size = size;
            b->frames = size >= 8 ? bouze_u16(data + off + 4) : 0;
            b->ticks = size >= 8 ? (uint8_t)(data[off + 2] >> 1) : 0;
            if (pass == 1)
                memcpy(out->image + at, data + off, size);
            at = (at + size + 3u) & ~(size_t)3u;
        }
        if (k != 0 || out->model_count == 0 || at > 0xFFFFFFFFu)
            goto out;
        if (pass == 0) {
            out->image = (uint8_t*)calloc(1, at ? at : 1);
            if (!out->image)
                goto out;
            out->image_size = (uint32_t)at;
        }
    }
    ok = 1;
out:
    free(s);
    free(data);
    free(ids);
    if (!ok)
        donor_free(out);
    return ok;
}

int donor_extract(const uint8_t* file, size_t file_len, uint32_t actor_type, Donor* out)
{
    return extract(file, file_len, actor_type, NULL, 0, out);
}

int donor_extract_ids(const uint8_t* file, size_t file_len, const uint16_t* res_ids,
                      int res_count, Donor* out)
{
    if (!res_ids || res_count <= 0) {
        memset(out, 0, sizeof(*out));
        return 0;
    }
    return extract(file, file_len, 0, res_ids, res_count, out);
}

int donor_find_model(const uint8_t* s, size_t len, uint32_t actor_type, uint16_t sub,
                     uint32_t* off, uint32_t* size)
{
    uint8_t* ids = (uint8_t*)calloc(1, ID_BITMAP_BYTES);
    if (!ids)
        return 0;
    int distinct = 0, found = 0;
    if (collect_actor_ids(s, len, actor_type, ids, &distinct) > 0) {
        size_t pos = 0;
        BouzeRecord r;
        while (!found && bouze_next_record(s, len, &pos, &r) == 1) {
            if (r.kind != BOUZE_KIND_RESOURCE)
                continue;
            ResInfo ri;
            resource_info(s, &r, &ri);
            if (ri.has_id && ri.blob_tag == BOUZE_TAG_RES_MODEL && ri.sub == sub &&
                id_has(ids, ri.id)) {
                *off = bouze_u32(s + ri.blob_payload);
                *size = bouze_u32(s + ri.blob_payload + 4);
                found = 1;
            }
        }
    }
    free(ids);
    return found;
}

void donor_free(Donor* d)
{
    free(d->image);
    memset(d, 0, sizeof(*d));
}

static void put_u32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static const DonorBlob* find_sub(const DonorBlob* v, int n, uint16_t sub)
{
    for (int i = 0; i < n; ++i)
        if (v[i].sub == sub)
            return &v[i];
    return NULL;
}

const DonorBlob* donor_pick_anim(const Donor* d, uint16_t target_sub, uint16_t own_frames,
                                 const DonorAnimMap* map, int map_count,
                                 const uint16_t* defaults, int default_count, int* mapped)
{
    *mapped = 0;
    if (d->anim_count == 0)
        return NULL;
    for (int m = 0; m < map_count; ++m)
        if (map[m].target_sub == target_sub) {
            const DonorBlob* b = find_sub(d->anims, d->anim_count, map[m].donor_sub);
            if (b) {
                *mapped = 1;
                return b;
            }
        }
    const DonorBlob* longest = NULL;
    for (int k = 0; k < default_count; ++k) {
        const DonorBlob* c = find_sub(d->anims, d->anim_count, defaults[k]);
        if (!c)
            continue;
        if (c->frames >= own_frames)
            return c;
        if (!longest || c->frames > longest->frames)
            longest = c;
    }
    return longest ? longest : &d->anims[0];
}

/* The replaced character's state machine times its states by one pass of
 * the animation it plays, loops included (Taz's 4-frame, 2-tick landing
 * replaced by a 21-frame donor loop kept him ~80 instead of ~12 display
 * frames in that state). So every record keeps its own frame count, capped
 * at the donor's so the donor data is never read past its end. */
uint16_t donor_anim_frames(const DonorBlob* b, uint16_t own_frames)
{
    return own_frames < b->frames ? own_frames : b->frames;
}

int donor_relocate_model(uint8_t* image, uint32_t image_size, const DonorBlob* m,
                         uint32_t guest_image)
{
    if (m->off > image_size || m->size < 12u || m->size > image_size - m->off)
        return 0;
    uint8_t* blob = image + m->off;
    if (m->size >= 0x24u && bouze_u32(blob + 0x20) == 0xFFFF0000u)
        return 2;                      /* the loader leaves these as they are */
    if (blob[1] & 1u)
        return 0;                      /* already relocated */
    const uint32_t count = bouze_u16(blob + 2);
    if (8u + 8u * count > m->size)
        return 0;
    const uint32_t base = guest_image + m->off;
    blob[1] |= 1u;
    if (count == 0)
        return 1;
    put_u32(blob + 4, bouze_u32(blob + 4) + base);
    for (uint32_t i = 0; i < count; ++i)
        put_u32(blob + 8u + 8u * i, bouze_u32(blob + 8u + 8u * i) + base + 8u);
    return 1;
}

/* ---- Keyframe streams ----------------------------------------------------
 * Format read by the game's keyframe interpreter FUN_800563EC (NOTES.md
 * "Keyframe stream format"; same rules as tools/bouze/keyframes.py): after
 * the 8-byte blob header, per frame an 8-byte header (u16 [0] = frame
 * halfwords + 2, [1] = command count, [2] = frame index) and the commands,
 * each a u16 (bits 0-7 node, 8-11 opcode, 12-15 flags) plus u16 args. */
#define KF_OP_MASK  0x0F00u
#define KF_OP_ROOT  0x0A00u   /* actor +0x84 = this node: attach point */

static uint32_t kf_nargs(uint16_t cmd)
{
    const uint32_t op = cmd & KF_OP_MASK, fl = cmd & 0xF000u;
    const uint32_t r = (fl & 0x2000u) ? 3u : 0u, s = (fl & 0x4000u) ? 3u : 0u;
    switch (op) {
    case 0x100u: case 0xD00u:
        return r + s + ((fl & 0x8000u) ? 3u : ((fl & 0x1000u) ? 2u : 0u));
    case 0x200u: case 0x300u: case 0xC00u: return 1u;
    case 0x400u: case 0x700u: return 4u;
    case 0x600u: case 0xE00u: return 2u;
    case 0x900u: return (fl & 0x1000u) ? 4u : 7u;
    case 0xB00u: return (r + s + ((fl & 0x8000u) ? 3u : 0u) + 1u) >> 1;
    default: return 0u;
    }
}

/* Counts, per node, the root (attach point) commands of one animation blob.
 * Returns 1 if the blob parses exactly to its end. */
static int kf_count_roots(const uint8_t* blob, uint32_t size, uint32_t counts[256])
{
    if (size < 8)
        return 0;
    const uint16_t frames = bouze_u16(blob + 4);
    uint32_t p = 8;
    for (uint32_t f = 0; f < frames; ++f) {
        if (p + 8u > size)
            return 0;
        const uint16_t n = bouze_u16(blob + p + 2);
        p += 8;
        for (uint32_t c = 0; c < n; ++c) {
            if (p + 2u > size)
                return 0;
            const uint16_t cmd = bouze_u16(blob + p);
            const uint32_t len = 2u + 2u * kf_nargs(cmd);
            if (len > size - p)
                return 0;
            if ((cmd & KF_OP_MASK) == KF_OP_ROOT)
                ++counts[cmd & 0xFFu];
            p += len;
        }
    }
    return p == size;
}

static void put_u16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* Integer wobble of the attach point (radius 30, 12 steps of 30 degrees):
 * the tornado sways a little around the actor, as Taz's own spin does. */
static const int8_t kWobbleCos[12] = {30, 26, 15, 0, -15, -26, -30, -26, -15, 0, 15, 26};

int donor_add_spin(Donor* d, uint16_t sub, uint16_t frames)
{
    if (!d->image || frames == 0 || find_sub(d->anims, d->anim_count, sub) ||
        d->anim_count >= DONOR_MAX_ANIMS)
        return 0;
    /* The attach point is the node the donor's own animations use as root
     * reference (TRCA01 enemy: node 23, a direct child of node 1). */
    uint32_t counts[256] = {0};
    for (int i = 0; i < d->anim_count; ++i)
        if (!kf_count_roots(d->image + d->anims[i].off, d->anims[i].size, counts))
            return 0;
    uint16_t attach = 0;
    for (uint32_t n = 2; n < 256; ++n)
        if (counts[n] > counts[attach])
            attach = (uint16_t)n;
    if (attach == 0 || counts[attach] == 0)
        return 0;

    /* Frame 0: zero collision box (like Taz's spin), node 1 at the actor
     * origin (the body is not drawn in the spin state; the next animation's
     * first frame poses every node again), attach point at the wobble
     * offset, root = attach point. Later frames: move the attach point. */
    const uint32_t size = 8u + (8u + 2u * 14u) + (uint32_t)(frames - 1u) * (8u + 2u * 5u);
    const uint32_t at = (d->image_size + 3u) & ~3u;
    uint8_t* img = (uint8_t*)realloc(d->image, at + size + 3u);
    if (!img)
        return 0;
    d->image = img;
    memset(img + d->image_size, 0, at + size + 3u - d->image_size);
    uint8_t* b = img + at;
    b[0] = 0x50;            /* header byte 0 as in the game's blobs */
    b[2] = 2;               /* 1 tick per keyframe */
    put_u16(b + 4, frames);
    uint32_t p = 8;
    for (uint16_t f = 0; f < frames; ++f) {
        const int k = (f * 5) % 12;
        const uint16_t tx = (uint16_t)(int16_t)kWobbleCos[k];
        const uint16_t tz = (uint16_t)(int16_t)kWobbleCos[(k + 9) % 12];
        const uint32_t hdr = p;
        p += 8;
        uint16_t n = 0;
        if (f == 0) {
            put_u16(b + p, 0x0900u); p += 2 + 14; ++n;        /* box: 7 zero args */
            put_u16(b + p, 0x0D01u); p += 2; ++n;             /* node 1 at origin */
            put_u16(b + p, (uint16_t)(0x8D00u | attach));     /* translation only */
        } else {
            put_u16(b + p, (uint16_t)(0x8100u | attach));
        }
        put_u16(b + p + 2, tx); put_u16(b + p + 4, 0); put_u16(b + p + 6, tz);
        p += 8; ++n;
        put_u16(b + p, (uint16_t)(KF_OP_ROOT | attach)); p += 2; ++n;
        put_u16(b + hdr, (uint16_t)((p - hdr - 8u) / 2u + 2u));
        put_u16(b + hdr + 2, n);
        put_u16(b + hdr + 4, f);
    }
    if (p != size)
        return 0;
    DonorBlob* nb = &d->anims[d->anim_count++];
    memset(nb, 0, sizeof(*nb));
    nb->sub = sub;
    nb->off = at;
    nb->size = size;
    nb->frames = frames;
    nb->ticks = 1;
    d->image_size = (at + size + 3u) & ~3u;
    return attach;
}


int donor_copy(const Donor* src, Donor* out)
{
    memset(out, 0, sizeof(*out));
    if (!src->image)
        return 0;
    uint8_t* img = (uint8_t*)malloc(src->image_size ? src->image_size : 1u);
    if (!img)
        return 0;
    memcpy(img, src->image, src->image_size);
    *out = *src;
    out->image = img;
    return 1;
}

int donor_append_anim(Donor* d, uint16_t sub, const uint8_t* blob, uint32_t size)
{
    if (!d->image || !blob || size < 16u || d->anim_count >= DONOR_MAX_ANIMS ||
        find_sub(d->anims, d->anim_count, sub))
        return 0;
    const uint32_t at = (d->image_size + 3u) & ~3u;
    uint8_t* img = (uint8_t*)realloc(d->image, at + size + 3u);
    if (!img)
        return 0;
    d->image = img;
    memset(img + d->image_size, 0, at + size + 3u - d->image_size);
    memcpy(img + at, blob, size);
    DonorBlob* nb = &d->anims[d->anim_count++];
    memset(nb, 0, sizeof(*nb));
    nb->sub = sub;
    nb->off = at;
    nb->size = size;
    nb->frames = (uint16_t)(blob[4] | (blob[5] << 8));
    nb->ticks = (uint8_t)(blob[2] >> 1);
    d->image_size = (at + size + 3u) & ~3u;
    return 1;
}

int donor_add_hold(Donor* d, uint16_t src_sub, uint16_t hold_sub, uint16_t frames)
{
    const DonorBlob* src = find_sub(d->anims, d->anim_count, src_sub);
    if (!d->image || !src || frames == 0 || find_sub(d->anims, d->anim_count, hold_sub) ||
        d->anim_count >= DONOR_MAX_ANIMS || src->size < 16u)
        return 0;
    /* Frame 0 of the source: 8-byte header (u16 [1] = command count), then
     * the commands with their arguments. */
    const uint8_t* sb = d->image + src->off;
    const uint32_t count = bouze_u16(sb + 8 + 2);
    uint32_t p = 16;
    for (uint32_t c = 0; c < count; ++c) {
        if (p + 2 > src->size)
            return 0;
        p += 2u + 2u * kf_nargs(bouze_u16(sb + p));
    }
    if (p > src->size)
        return 0;
    const uint32_t frame0 = p - 8u;               /* header + commands */
    const uint32_t size = 8u + frame0 + (uint32_t)(frames - 1u) * 8u;
    const uint32_t src_off = src->off;
    const uint32_t at = (d->image_size + 3u) & ~3u;
    uint8_t* img = (uint8_t*)realloc(d->image, at + size + 3u);
    if (!img)
        return 0;
    d->image = img;
    memset(img + d->image_size, 0, at + size + 3u - d->image_size);
    uint8_t* b = img + at;
    memcpy(b, img + src_off, 8u + frame0);       /* blob header and frame 0 */
    put_u16(b + 4, frames);
    put_u16(b + 8 + 4, 0);                        /* frame index */
    for (uint32_t f = 1; f < frames; ++f) {
        uint8_t* h = b + 8u + frame0 + (f - 1u) * 8u;
        put_u16(h, 2);                            /* no command halfwords */
        put_u16(h + 2, 0);                        /* no commands: nothing changes */
        put_u16(h + 4, (uint16_t)f);
    }
    DonorBlob* nb = &d->anims[d->anim_count++];
    memset(nb, 0, sizeof(*nb));
    nb->sub = hold_sub;
    nb->off = at;
    nb->size = size;
    nb->frames = frames;
    nb->ticks = (uint8_t)(b[2] >> 1);
    d->image_size = (at + size + 3u) & ~3u;
    return 1;
}

int donor_patch_stream(uint8_t* s, size_t len, uint32_t target_type,
                       const Donor* d, uint32_t image_addr, uint32_t blob_base,
                       const DonorAnimMap* map, int map_count,
                       const uint16_t* defaults, int default_count,
                       DonorTargetHeaderFn read_hdr, void* ctx,
                       DonorWrite* changed, int cap,
                       DonorAnimFix* fixes, int fix_cap, int* fix_count,
                       DonorPatchStats* st)
{
    memset(st, 0, sizeof(*st));
    *fix_count = 0;
    if (!d->image || d->model_count == 0)
        return -1;
    uint8_t* ids = (uint8_t*)calloc(1, ID_BITMAP_BYTES);
    if (!ids)
        return -1;
    int n = -1;
    st->actor_records = collect_actor_ids(s, len, target_type, ids, &st->resource_ids);
    if (st->actor_records < 0)
        goto out;
    n = 0;
    if (st->actor_records == 0)
        goto out;

    size_t pos = 0;
    BouzeRecord r;
    int k;
    while ((k = bouze_next_record(s, len, &pos, &r)) == 1) {
        if (r.kind != BOUZE_KIND_RESOURCE)
            continue;
        ResInfo ri;
        resource_info(s, &r, &ri);
        if (!ri.has_id || !id_has(ids, ri.id))
            continue;
        const DonorBlob* b = NULL;
        if (ri.blob_tag == BOUZE_TAG_RES_MODEL) {
            b = find_sub(d->models, d->model_count, ri.sub);
            if (!b)
                b = &d->models[0];
            ++st->models;
        } else if (ri.blob_tag == BOUZE_TAG_RES_ANIM && d->anim_count > 0) {
            uint8_t hdr[8];
            const uint32_t own_off = bouze_u32(s + ri.blob_payload);
            if (!read_hdr(ctx, own_off, hdr)) {
                n = -1;
                goto out;
            }
            const uint16_t own_frames = bouze_u16(hdr + 4);
            const uint8_t own_ticks = (uint8_t)(hdr[2] >> 1);
            int mapped = 0;
            b = donor_pick_anim(d, ri.sub, own_frames, map, map_count, defaults,
                                default_count, &mapped);
            if (mapped)
                ++st->anims_mapped;
            else
                ++st->anims_default;
            const uint16_t frames = donor_anim_frames(b, own_frames);
            if (frames != b->frames || own_ticks != b->ticks) {
                if (*fix_count >= fix_cap) {
                    n = -1;
                    goto out;
                }
                DonorAnimFix* f = &fixes[(*fix_count)++];
                f->res_id = ri.id;
                f->frames = frames;
                f->ticks = own_ticks;
            }
        } else {
            ++st->other;
            continue;
        }
        if (n >= cap) {
            n = -1;
            goto out;
        }
        /* The loader computes base + offset with 32-bit wrap (MIPS addu). */
        put_u32(s + ri.blob_payload, image_addr + b->off - blob_base);
        put_u32(s + ri.blob_payload + 4, b->size);
        changed[n].off = ri.blob_payload;
        changed[n].len = 8;
        ++n;
    }
    if (k != 0)
        n = -1;
out:
    free(ids);
    return n;
}

int donor_patch_actor_scale(uint8_t* s, size_t len, uint32_t target_type,
                            int32_t scale, DonorWrite* changed, int n, int cap)
{
    size_t pos = 0;
    BouzeRecord r;
    int k;
    while ((k = bouze_next_record(s, len, &pos, &r)) == 1) {
        if (!bouze_is_actor_kind(r.kind))
            continue;
        size_t tp = r.tags;
        BouzeTag t;
        int is_target = 0;
        while (bouze_next_tag(s, &r, &tp, &t))
            if (t.tag == BOUZE_TAG_ACTOR_HEADER && t.len == 28 &&
                bouze_u32(s + t.payload) == target_type)
                is_target = 1;
        if (!is_target)
            continue;
        tp = r.tags;
        while (bouze_next_tag(s, &r, &tp, &t)) {
            if (t.tag != BOUZE_TAG_ACTOR_SCALE || t.len != 12)
                continue;
            if (n >= cap)
                return -1;
            for (int a = 0; a < 3; ++a)
                put_u32(s + t.payload + 4u * (uint32_t)a, (uint32_t)scale);
            changed[n].off = t.payload;
            changed[n].len = 12;
            ++n;
        }
    }
    return k == 0 ? n : -1;
}
