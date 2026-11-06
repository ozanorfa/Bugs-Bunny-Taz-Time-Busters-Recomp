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

int donor_extract(const uint8_t* file, size_t file_len, uint32_t actor_type,
                  Donor* out)
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
    if (collect_actor_ids(s, slen, actor_type, ids, &distinct) <= 0)
        goto out;

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

void donor_free(Donor* d)
{
    free(d->image);
    memset(d, 0, sizeof(*d));
}

static const DonorBlob* find_sub(const DonorBlob* v, int n, uint16_t sub)
{
    for (int i = 0; i < n; ++i)
        if (v[i].sub == sub)
            return &v[i];
    return NULL;
}

static void put_u32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int donor_patch_stream(uint8_t* s, size_t len, uint32_t target_type,
                       const Donor* d, uint32_t image_addr, uint32_t blob_base,
                       uint16_t fallback_sub, size_t* changed, int cap,
                       DonorPatchStats* st)
{
    memset(st, 0, sizeof(*st));
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
            b = find_sub(d->anims, d->anim_count, ri.sub);
            if (b) {
                ++st->anims_same;
            } else {
                b = find_sub(d->anims, d->anim_count, fallback_sub);
                if (!b)
                    b = &d->anims[0];
                ++st->anims_fallback;
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
        changed[n++] = ri.blob_payload;
    }
    if (k != 0)
        n = -1;
out:
    free(ids);
    return n;
}
