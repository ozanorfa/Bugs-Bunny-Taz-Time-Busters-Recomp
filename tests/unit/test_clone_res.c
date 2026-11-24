/* clone_res.c: the cloned actor's own resource list. */
#include <stdlib.h>
#include <string.h>

#include "bouze.h"
#include "check.h"
#include "clone_res.h"
#include "donor.h"
#include "synth.h"

static void entry(CloneResIn* r, uint16_t id, uint16_t type, uint16_t sub, uint32_t data,
                  uint32_t script, uint16_t frames, uint8_t ticks)
{
    memset(r, 0, sizeof(*r));
    uint8_t* e = r->entry;
    e[0] = 0x44; e[1] = 0x33; e[2] = 0x22; e[3] = 0x81;          /* global list link */
    e[6] = (uint8_t)id; e[7] = (uint8_t)(id >> 8);
    e[8] = (uint8_t)type; e[9] = (uint8_t)(type >> 8);
    e[10] = (uint8_t)sub; e[11] = (uint8_t)(sub >> 8);
    e[0x0E] = 0x5A;
    for (int k = 0; k < 4; ++k) {
        e[0x14 + k] = (uint8_t)(script >> (8 * k));                /* per-keyframe script */
        e[0x18 + k] = (uint8_t)(data >> (8 * k));
    }
    r->record[0x08] = (uint8_t)frames;
    r->record[0x09] = (uint8_t)(frames >> 8);
    r->record[0x0A] = 0x77;
    r->record[0x0B] = ticks;
}

static void build_list(void)
{
    SynthLevel l;
    synth_level_build(&l);
    Donor d;
    REQUIRE(donor_extract(l.file.p, l.file.n, SYN_TYPE_DONOR, &d));
    /* donor: model sub 0xD; anims 0x0001 (2 frames), 0x0033 (4 frames) */
    CloneResIn in[5];
    entry(&in[0], 157, RES_TYPE_MODEL, 0x000D, 0x801383BCu, 0, 0, 0);
    entry(&in[1], 160, RES_TYPE_ANIM, 0x0001, 0x80102000u, 0x80125000u, 1, 0);
    entry(&in[2], 231, RES_TYPE_ANIM, 0x0033, 0x80102010u, 0x80125010u, 9, 1);
    entry(&in[3], 240, RES_TYPE_ANIM, 0x0099, 0x80102020u, 0, 3, 2);
    entry(&in[4], 250, 1, 0x0000, 0x80140000u, 0, 0, 0);            /* another kind */
    CloneResLayout lay;
    memset(&lay, 0, sizeof lay);
    lay.nodes = 0x9F000300u;
    lay.entries = 0x9F000900u;
    lay.records = 0x9F001700u;
    lay.image = 0x9F002000u;
    lay.model_data[0] = 0x9F002008u + d.models[0].off;
    const DonorAnimMap map[1] = {{0x0033, 0x0033}};
    const uint16_t defaults[1] = {0x0001};
    uint8_t nodes[5 * RES_NODE_SIZE], entries[5 * RES_ENTRY_SIZE], records[5 * ANIM_RECORD_SIZE];
    memset(records, 0xEE, sizeof records);
    CloneResStats st;
    const int n = clone_build_resources(in, 5, &d, &lay, map, 1, defaults, 1, NULL, 0, nodes, entries,
                                        records, &st);
    CHECK_EQ(n, 3);
    CHECK_EQ(st.models, 1);
    CHECK_EQ(st.anims_mapped, 1);
    CHECK_EQ(st.anims_default, 2);
    CHECK_EQ(st.other, 1);
    /* nodes: a chain in source order, each pointing at its entry */
    for (int i = 0; i < 5; ++i) {
        const uint8_t* nd = nodes + RES_NODE_SIZE * i;
        CHECK_EQ(bouze_u32(nd), i < 4 ? lay.nodes + RES_NODE_SIZE * (uint32_t)(i + 1) : 0u);
        CHECK_EQ(bouze_u32(nd + 4), 0);
        CHECK_EQ(bouze_u32(nd + 8), lay.entries + RES_ENTRY_SIZE * (uint32_t)i);
    }
    /* entries: copies, out of the game's global list, data redirected */
    for (int i = 0; i < 5; ++i) {
        const uint8_t* e = entries + RES_ENTRY_SIZE * i;
        CHECK_EQ(bouze_u32(e), 0);
        CHECK(memcmp(e + 4, in[i].entry + 4, 0x18 - 4) == 0);       /* ids, subs, script */
    }
    CHECK_EQ(bouze_u32(entries + 0x18), lay.model_data[0]);
    CHECK_EQ(bouze_u32(entries + RES_ENTRY_SIZE * 4 + 0x18), 0x80140000u);   /* untouched */
    const uint32_t want_off[3] = {d.anims[0].off, d.anims[1].off, d.anims[0].off};
    const uint16_t want_frames[3] = {1, 4, 2};     /* own frames, capped at the donor's */
    const uint8_t want_ticks[3] = {0, 1, 2};       /* own ticks */
    for (int k = 0; k < 3; ++k) {
        const uint8_t* e = entries + RES_ENTRY_SIZE * (k + 1);
        const uint8_t* r = records + ANIM_RECORD_SIZE * k;
        CHECK_EQ(bouze_u32(e + 0x18), lay.records + ANIM_RECORD_SIZE * (uint32_t)k);
        CHECK(memcmp(r, in[k + 1].record, 8) == 0);
        CHECK_EQ(bouze_u16(r + 0x08), want_frames[k]);
        CHECK_EQ(r[0x0A], d.image[want_off[k] + 12]);              /* as the loader sets it */
        CHECK_EQ(r[0x0B], want_ticks[k]);
        CHECK_EQ(bouze_u32(r + 0x0C), lay.image + want_off[k] + 8u);
    }
    CHECK_EQ(records[3 * ANIM_RECORD_SIZE], 0xEE);                  /* nothing past them */

    /* a model sub the donor lacks takes its first model */
    entry(&in[0], 157, RES_TYPE_MODEL, 0x000E, 0, 0, 0, 0);
    CHECK_EQ(clone_build_resources(in, 1, &d, &lay, map, 1, defaults, 1, NULL, 0, nodes, entries,
                                   records, &st), 0);
    CHECK_EQ(bouze_u32(entries + 0x18), lay.model_data[0]);
    CHECK_EQ(bouze_u32(nodes), 0);
    donor_free(&d);
    synth_level_free(&l);
}

static void build_rejects(void)
{
    SynthLevel l;
    synth_level_build(&l);
    Donor d;
    REQUIRE(donor_extract(l.file.p, l.file.n, SYN_TYPE_DONOR, &d));
    CloneResIn in[1];
    entry(&in[0], 1, RES_TYPE_MODEL, 0x000D, 0, 0, 0, 0);
    CloneResLayout lay;
    memset(&lay, 0, sizeof lay);
    static uint8_t nodes[(CLONE_RES_MAX + 1) * RES_NODE_SIZE];
    static uint8_t entries[(CLONE_RES_MAX + 1) * RES_ENTRY_SIZE];
    static uint8_t records[(CLONE_RES_MAX + 1) * ANIM_RECORD_SIZE];
    CloneResStats st;
    CHECK_EQ(clone_build_resources(in, 0, &d, &lay, NULL, 0, NULL, 0, NULL, 0, nodes, entries, records, &st), -1);
    CHECK_EQ(clone_build_resources(in, CLONE_RES_MAX + 1, &d, &lay, NULL, 0, NULL, 0, NULL, 0, nodes,
                                   entries, records, &st), -1);
    /* BUGS #58: an animation blob too short for the header byte [12] that
     * the record copies (a corrupted file; the disc's are 16 bytes or more) */
    {
        CloneResIn anim[1];
        entry(&anim[0], 160, RES_TYPE_ANIM, 0x0001, 0x80102000u, 0x80125000u, 1, 0);
        const uint16_t defaults[1] = {0x0001};
        CHECK(clone_build_resources(anim, 1, &d, &lay, NULL, 0, defaults, 1, NULL, 0, nodes, entries,
                                    records, &st) >= 0);
        Donor short_anims = d;
        for (int k = 0; k < short_anims.anim_count; ++k)
            short_anims.anims[k].size = 12;
        CHECK_EQ(clone_build_resources(anim, 1, &short_anims, &lay, NULL, 0, defaults, 1, NULL, 0, nodes,
                                       entries, records, &st), -1);
    }
    Donor no_anims = d;
    no_anims.anim_count = 0;
    CHECK_EQ(clone_build_resources(in, 1, &no_anims, &lay, NULL, 0, NULL, 0, NULL, 0, nodes, entries,
                                   records, &st), -1);
    Donor no_model = d;
    no_model.model_count = 0;
    CHECK_EQ(clone_build_resources(in, 1, &no_model, &lay, NULL, 0, NULL, 0, NULL, 0, nodes, entries,
                                   records, &st), -1);
    donor_free(&d);
    synth_level_free(&l);
}


static void native_timing_plays_the_whole_donor_animation(void)
{
    SynthLevel l;
    synth_level_build(&l);
    Donor d;
    REQUIRE(donor_extract(l.file.p, l.file.n, SYN_TYPE_DONOR, &d));
    /* donor anim 0x0033: 4 frames, 2 ticks */
    CloneResIn in[2];
    entry(&in[0], 1, RES_TYPE_ANIM, 0x0021, 0, 0, 2, 1);    /* idle: 2 frames, 1 tick */
    entry(&in[1], 2, RES_TYPE_ANIM, 0x0034, 0, 0, 2, 1);    /* other move */
    CloneResLayout lay;
    memset(&lay, 0, sizeof lay);
    lay.nodes = 0x9F000300u; lay.entries = 0x9F000900u; lay.records = 0x9F001700u;
    lay.image = 0x9F002000u;
    const DonorAnimMap map[2] = {{0x0021, 0x0033}, {0x0034, 0x0033}};
    const uint16_t native[1] = {0x0021};
    uint8_t nodes[2 * RES_NODE_SIZE], entries[2 * RES_ENTRY_SIZE], records[2 * ANIM_RECORD_SIZE];
    CloneResStats st;
    REQUIRE(clone_build_resources(in, 2, &d, &lay, map, 2, NULL, 0, native, 1, nodes, entries,
                                  records, &st) == 2);
    CHECK_EQ(st.native, 1);
    CHECK_EQ(bouze_u16(records + 0x08), 4);                 /* the donor's frames */
    CHECK_EQ(records[0x0B], 2);                             /* and its ticks */
    CHECK_EQ(bouze_u16(records + ANIM_RECORD_SIZE + 0x08), 2);   /* own timing */
    CHECK_EQ(records[ANIM_RECORD_SIZE + 0x0B], 1);
    donor_free(&d);
    synth_level_free(&l);
}

const TestCase kCloneResTests[] = {
    {"build_list", build_list},
    {"build_rejects", build_rejects},
    {"native_timing_plays_the_whole_donor_animation", native_timing_plays_the_whole_donor_animation},
};
const int kCloneResTestCount = (int)(sizeof(kCloneResTests) / sizeof(kCloneResTests[0]));
