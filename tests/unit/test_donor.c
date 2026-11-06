/* donor.c: extraction, the synthetic spin, model lookup, stream patching. */
#include <stdlib.h>
#include <string.h>

#include "bouze.h"
#include "check.h"
#include "donor.h"
#include "synth.h"

static void extract_donor(void)
{
    SynthLevel l;
    synth_level_build(&l);
    Donor d;
    REQUIRE(donor_extract(l.file.p, l.file.n, SYN_TYPE_DONOR, &d));
    CHECK_EQ(d.model_count, 1);
    CHECK_EQ(d.models[0].sub, 0x000D);
    CHECK_EQ(d.models[0].id, 10);
    CHECK_EQ(d.anim_count, 2);
    CHECK_EQ(d.anims[0].sub, 0x0001);
    CHECK_EQ(d.anims[0].frames, 2);
    CHECK_EQ(d.anims[0].ticks, 1);
    CHECK_EQ(d.anims[1].sub, 0x0033);
    CHECK_EQ(d.anims[1].frames, 4);
    CHECK_EQ(d.anims[1].ticks, 2);
    /* Blobs are copied byte for byte, 4-byte aligned, in record order. */
    const DonorBlob* all[3] = {&d.models[0], &d.anims[0], &d.anims[1]};
    const uint16_t ids[3] = {10, 11, 12};
    uint32_t end = 0;
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(all[i]->off % 4, 0);
        CHECK(all[i]->off >= end);
        CHECK_EQ(all[i]->size, l.size[ids[i]]);
        CHECK(memcmp(d.image + all[i]->off, l.blobs.p + l.off[ids[i]], l.size[ids[i]]) == 0);
        end = all[i]->off + all[i]->size;
    }
    CHECK_EQ(d.image_size, (end + 3u) & ~3u);
    donor_free(&d);
    CHECK(d.image == NULL);
    synth_level_free(&l);
}

static void extract_rejects(void)
{
    SynthLevel l;
    synth_level_build(&l);
    Donor d;
    CHECK(!donor_extract(l.file.p, l.file.n, 0x12345678u, &d));      /* no such actor */
    CHECK(d.image == NULL);
    CHECK(!donor_extract(l.file.p, 0x900, SYN_TYPE_DONOR, &d));      /* truncated */
    CHECK(donor_extract(l.file.p, l.file.n, SYN_TYPE_OTHER, &d));    /* model only */
    CHECK_EQ(d.model_count, 1);
    CHECK_EQ(d.anim_count, 0);
    donor_free(&d);
    synth_level_free(&l);
}

static void spin_attaches_to_the_donor_root_node(void)
{
    SynthLevel l;
    synth_level_build(&l);
    Donor d;
    REQUIRE(donor_extract(l.file.p, l.file.n, SYN_TYPE_DONOR, &d));
    const uint32_t before = d.image_size;
    /* Root commands: node 5 once, node 23 four times -> node 23. */
    CHECK_EQ(donor_add_spin(&d, 0xF001, 32), 23);
    REQUIRE(d.anim_count == 3);
    const DonorBlob* s = &d.anims[2];
    CHECK_EQ(s->sub, 0xF001);
    CHECK_EQ(s->frames, 32);
    CHECK_EQ(s->ticks, 1);
    CHECK_EQ(s->off % 4, 0);
    CHECK(s->off >= before);
    CHECK_EQ(s->size, 8 + (8 + 2 * 14) + 31 * (8 + 2 * 5));
    CHECK_EQ(d.image_size % 4, 0);
    CHECK(d.image_size >= s->off + s->size);

    /* Walk the frames: every frame ends with a root command on node 23;
     * frame 0 starts with a zero collision box. */
    const uint8_t* b = d.image + s->off;
    CHECK_EQ(bouze_u16(b + 4), 32);
    CHECK_EQ(b[2] >> 1, 1);
    uint32_t p = 8;
    int roots = 0;
    for (uint16_t f = 0; f < 32; ++f) {
        const uint16_t n = bouze_u16(b + p + 2);
        CHECK_EQ(bouze_u16(b + p + 4), f);
        const uint32_t body = (bouze_u16(b + p) - 2u) * 2u;
        CHECK_EQ(n, f == 0 ? 4 : 2);
        if (f == 0) {
            CHECK_EQ(bouze_u16(b + p + 8), 0x0900);
            int zero = 1;
            for (int a = 0; a < 7; ++a)
                zero &= bouze_u16(b + p + 10 + 2 * a) == 0;
            CHECK(zero);
        }
        roots += bouze_u16(b + p + 8 + body - 2) == 0x0A17;
        p += 8 + body;
    }
    CHECK_EQ(p, s->size);
    CHECK_EQ(roots, 32);

    CHECK_EQ(donor_add_spin(&d, 0xF001, 32), 0);   /* id already used */
    CHECK_EQ(donor_add_spin(&d, 0xF002, 0), 0);    /* no frames */
    donor_free(&d);

    /* A donor without root commands has no attach node. */
    REQUIRE(donor_extract(l.file.p, l.file.n, SYN_TYPE_OTHER, &d));
    CHECK_EQ(donor_add_spin(&d, 0xF001, 32), 0);
    donor_free(&d);
    synth_level_free(&l);
}

static void find_model(void)
{
    SynthLevel l;
    synth_level_build(&l);
    uint32_t off = 0, size = 0;
    CHECK(donor_find_model(l.stream.p, l.stream.n, SYN_TYPE_TARGET, 0x000D, &off, &size));
    CHECK_EQ(off, l.off[20]);
    CHECK_EQ(size, l.size[20]);
    CHECK(donor_find_model(l.stream.p, l.stream.n, SYN_TYPE_DONOR, 0x000D, &off, &size));
    CHECK_EQ(off, l.off[10]);
    CHECK(!donor_find_model(l.stream.p, l.stream.n, SYN_TYPE_TARGET, 0x000C, &off, &size));
    CHECK(!donor_find_model(l.stream.p, l.stream.n, 0x12345678u, 0x000D, &off, &size));
    synth_level_free(&l);
}

typedef struct HdrCtx {
    const Buf* blobs;
    int fail;
} HdrCtx;

static int read_hdr(void* ctx, uint32_t off, uint8_t out[8])
{
    const HdrCtx* c = (const HdrCtx*)ctx;
    if (c->fail || off + 8u > c->blobs->n)
        return 0;
    memcpy(out, c->blobs->p + off, 8);
    return 1;
}

/* Offset of the (offset, size) payload of resource `id` in the stream. */
static size_t payload_of(const Buf* s, uint16_t id)
{
    size_t pos = 0;
    BouzeRecord r;
    while (bouze_next_record(s->p, s->n, &pos, &r) == 1) {
        if (r.kind != BOUZE_KIND_RESOURCE)
            continue;
        size_t tp = r.tags, blob = 0;
        int match = 0;
        BouzeTag t;
        while (bouze_next_tag(s->p, &r, &tp, &t)) {
            if (t.tag == 0x27 && bouze_u16(s->p + t.payload + 2) == id)
                match = 1;
            if (t.tag == 0x24 || t.tag == 0x25)
                blob = t.payload;
        }
        if (match)
            return blob;
    }
    return 0;
}

static const DonorAnimMap kMap[] = {{0x0033, 0x0033}, {0x0034, 0x0777}};
static const uint16_t kDefaults[] = {0x0001};

static void patch_stream(void)
{
    SynthLevel l;
    synth_level_build(&l);
    Donor d;
    REQUIRE(donor_extract(l.file.p, l.file.n, SYN_TYPE_DONOR, &d));
    uint8_t* orig = (uint8_t*)malloc(l.stream.n);
    memcpy(orig, l.stream.p, l.stream.n);

    const uint32_t image = 0x9F000000u, base = 0x800E0000u;
    HdrCtx ctx = {&l.blobs, 0};
    DonorWrite w[16];
    DonorAnimFix fx[16];
    int nf = -1;
    DonorPatchStats st;
    const int n = donor_patch_stream(l.stream.p, l.stream.n, SYN_TYPE_TARGET, &d, image, base,
                                     kMap, 2, kDefaults, 1, read_hdr, &ctx, w, 16, fx, 16,
                                     &nf, &st);
    CHECK_EQ(n, 3);
    CHECK_EQ(st.actor_records, 1);
    CHECK_EQ(st.resource_ids, 3);
    CHECK_EQ(st.models, 1);
    CHECK_EQ(st.anims_mapped, 1);
    CHECK_EQ(st.anims_default, 1);
    CHECK_EQ(st.other, 0);

    /* Each payload points at the donor blob through the loader's
     * base + offset (32-bit wrap). */
    const size_t pm = payload_of(&l.stream, 20), p21 = payload_of(&l.stream, 21),
                 p22 = payload_of(&l.stream, 22);
    CHECK_EQ((uint32_t)(base + bouze_u32(l.stream.p + pm)), image + d.models[0].off);
    CHECK_EQ(bouze_u32(l.stream.p + pm + 4), d.models[0].size);
    CHECK_EQ((uint32_t)(base + bouze_u32(l.stream.p + p21)), image + d.anims[1].off); /* mapped 0x33 */
    CHECK_EQ((uint32_t)(base + bouze_u32(l.stream.p + p22)), image + d.anims[0].off); /* default */
    for (int i = 0; i < n; ++i)
        CHECK_EQ(w[i].len, 8);

    /* Only those payloads changed; the donor's and bystander's records did not. */
    size_t outside = 0;
    for (size_t i = 0; i < l.stream.n; ++i) {
        if (l.stream.p[i] == orig[i])
            continue;
        int in = 0;
        for (int k = 0; k < n; ++k)
            in |= i >= w[k].off && i < w[k].off + w[k].len;
        outside += !in;
    }
    CHECK_EQ(outside, 0);
    CHECK(memcmp(l.stream.p + payload_of(&l.stream, 30), orig + payload_of(&l.stream, 30), 8) == 0);

    /* Timing fixes keep the target's frame count (capped at the donor's)
     * and the target's ticks: anim 21 (3 frames, 2 ticks) on a 4-frame
     * donor anim; anim 22 (10 frames, 3 ticks) on the 2-frame default. */
    REQUIRE(nf == 2);
    CHECK_EQ(fx[0].res_id, 21);
    CHECK_EQ(fx[0].frames, 3);
    CHECK_EQ(fx[0].ticks, 2);
    CHECK_EQ(fx[1].res_id, 22);
    CHECK_EQ(fx[1].frames, 2);
    CHECK_EQ(fx[1].ticks, 3);

    /* The patched stream still parses to its end. */
    size_t pos = 0;
    BouzeRecord r;
    int k, recs = 0;
    while ((k = bouze_next_record(l.stream.p, l.stream.n, &pos, &r)) == 1)
        ++recs;
    CHECK_EQ(k, 0);
    CHECK_EQ(recs, 11);

    free(orig);
    donor_free(&d);
    synth_level_free(&l);
}

static void patch_stream_limits(void)
{
    SynthLevel l;
    synth_level_build(&l);
    Donor d;
    REQUIRE(donor_extract(l.file.p, l.file.n, SYN_TYPE_DONOR, &d));
    uint8_t* orig = (uint8_t*)malloc(l.stream.n);
    memcpy(orig, l.stream.p, l.stream.n);
    HdrCtx ctx = {&l.blobs, 0};
    DonorWrite w[16];
    DonorAnimFix fx[16];
    int nf;
    DonorPatchStats st;

    /* Absent target: nothing changes. */
    CHECK_EQ(donor_patch_stream(l.stream.p, l.stream.n, 0x12345678u, &d, 0x9F000000u, 0x800E0000u,
                                kMap, 2, kDefaults, 1, read_hdr, &ctx, w, 16, fx, 16, &nf, &st), 0);
    CHECK(memcmp(l.stream.p, orig, l.stream.n) == 0);
    /* Too small a change list or fix list, or an unreadable header: -1. */
    CHECK_EQ(donor_patch_stream(l.stream.p, l.stream.n, SYN_TYPE_TARGET, &d, 0x9F000000u,
                                0x800E0000u, kMap, 2, kDefaults, 1, read_hdr, &ctx, w, 2, fx, 16,
                                &nf, &st), -1);
    memcpy(l.stream.p, orig, l.stream.n);
    CHECK_EQ(donor_patch_stream(l.stream.p, l.stream.n, SYN_TYPE_TARGET, &d, 0x9F000000u,
                                0x800E0000u, kMap, 2, kDefaults, 1, read_hdr, &ctx, w, 16, fx, 1,
                                &nf, &st), -1);
    memcpy(l.stream.p, orig, l.stream.n);
    ctx.fail = 1;
    CHECK_EQ(donor_patch_stream(l.stream.p, l.stream.n, SYN_TYPE_TARGET, &d, 0x9F000000u,
                                0x800E0000u, kMap, 2, kDefaults, 1, read_hdr, &ctx, w, 16, fx, 16,
                                &nf, &st), -1);
    /* A donor without a model is refused. */
    Donor empty;
    memset(&empty, 0, sizeof empty);
    CHECK_EQ(donor_patch_stream(l.stream.p, l.stream.n, SYN_TYPE_TARGET, &empty, 0x9F000000u,
                                0x800E0000u, kMap, 2, kDefaults, 1, read_hdr, &ctx, w, 16, fx, 16,
                                &nf, &st), -1);
    free(orig);
    donor_free(&d);
    synth_level_free(&l);
}

static void patch_scale(void)
{
    SynthLevel l;
    synth_level_build(&l);
    uint8_t* orig = (uint8_t*)malloc(l.stream.n);
    memcpy(orig, l.stream.p, l.stream.n);
    DonorWrite w[4];
    CHECK_EQ(donor_patch_actor_scale(l.stream.p, l.stream.n, SYN_TYPE_TARGET, 0x0C00, w, 1, 4), 2);
    CHECK_EQ(w[1].off, l.target_scale_payload);
    CHECK_EQ(w[1].len, 12);
    for (int a = 0; a < 3; ++a)
        CHECK_EQ(bouze_u32(l.stream.p + l.target_scale_payload + 4u * (uint32_t)a), 0x0C00);
    size_t diff = 0;
    for (size_t i = 0; i < l.stream.n; ++i)
        diff += l.stream.p[i] != orig[i];
    CHECK_EQ(diff, 3);         /* 0x00001000 -> 0x00000C00: one byte per value */
    memcpy(l.stream.p, orig, l.stream.n);
    CHECK_EQ(donor_patch_actor_scale(l.stream.p, l.stream.n, SYN_TYPE_TARGET, 0x0C00, w, 4, 4), -1);
    free(orig);
    synth_level_free(&l);
}

const TestCase kDonorTests[] = {
    {"extract_donor", extract_donor},
    {"extract_rejects", extract_rejects},
    {"spin_attaches_to_the_donor_root_node", spin_attaches_to_the_donor_root_node},
    {"find_model", find_model},
    {"patch_stream", patch_stream},
    {"patch_stream_limits", patch_stream_limits},
    {"patch_scale", patch_scale},
};
const int kDonorTestCount = (int)(sizeof(kDonorTests) / sizeof(kDonorTests[0]));
