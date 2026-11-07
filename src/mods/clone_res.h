/*
 * Resources of a cloned character actor (host-side logic only; no guest
 * access, so it is unit-tested offline).
 *
 * An actor's model and animations are found through its resource list
 * (actor +0x98, mirrored at +0x170 / +0x174): nodes {next, 0, entry} from
 * the game's node pool, each pointing at a 28-byte resource entry, which for
 * an animation points at a 16-byte animation record. All verified in the
 * Level_LoadParse decompile (0x80068174: tag 0x0F builds the node, record
 * kind 0x22 the entry, tags 0x24 / 0x25 the model / animation) and in the
 * model bind FUN_80057768 (the model is the entry with sub 0x0D, the set-up
 * animation the one with sub 0x0001).
 *
 * A clone of Taz must not share his list (Taz keeps his own model and
 * animations), and the game's pools have too little room for a second one
 * (Granwich: 60 free animation records, 63 free entries; Taz uses 67 and
 * 68). So the clone gets a list of its own in mod memory: the same entries
 * (same ids and subs, so Taz's behaviour script finds every animation it
 * asks for, and the same per-animation scripts at entry +0x14), with the
 * model pointed at the donor's and every animation record rebuilt from the
 * donor animation its role maps to.
 */
#ifndef BBT_CLONE_RES_H
#define BBT_CLONE_RES_H

#include <stdint.h>

#include "donor.h"

#define CLONE_RES_MAX     128
#define RES_NODE_SIZE     0x0Cu
#define RES_ENTRY_SIZE    0x1Cu
#define ANIM_RECORD_SIZE  0x10u
#define RES_TYPE_MODEL    2u
#define RES_TYPE_ANIM     4u

/* One node of the source actor's resource list, as read from the guest. */
typedef struct CloneResIn {
    uint8_t entry[RES_ENTRY_SIZE];     /* the resource entry */
    uint8_t record[ANIM_RECORD_SIZE];  /* its animation record (type 4 only) */
} CloneResIn;

/* Where the clone's data lives in guest memory. */
typedef struct CloneResLayout {
    uint32_t nodes;                    /* RES_NODE_SIZE each */
    uint32_t entries;                  /* RES_ENTRY_SIZE each */
    uint32_t records;                  /* ANIM_RECORD_SIZE each */
    uint32_t image;                    /* the donor image (models relocated) */
    uint32_t model_data[DONOR_MAX_MODELS];  /* what entry +0x18 holds per donor model */
} CloneResLayout;

typedef struct CloneResStats {
    int models;
    int anims_mapped;
    int anims_default;
    int other;                         /* other types: entry copied unchanged */
    int native;                        /* animations at the donor's own length and speed */
} CloneResStats;

/* Build the clone's resource list for a source list `in[0..n)`.
 *   nodes_out   n * RES_NODE_SIZE bytes: node i = {next = node i+1 or 0,
 *               0, entry i}, in the source order;
 *   entries_out n * RES_ENTRY_SIZE: entry i = source entry i with +0x00
 *               (the game's global entry list) cleared and +0x18 pointing
 *               at the donor model (same sub, else the first) or at the
 *               clone's record of that animation;
 *   records_out up to n * ANIM_RECORD_SIZE: built like the loader builds one
 *               from a blob (tag 0x25: +0x08 u16 = blob [4], +0x0A = blob
 *               [12], +0x0B = blob [2] >> 1, +0x0C = blob + 8), except the
 *               frame count and ticks per keyframe, which are the source
 *               record's own (frames capped at the donor's): the
 *               character's state machine times its states by them --
 *               except for the ids in `native`, which play the whole donor
 *               animation at its own speed (states that only loop, such as
 *               standing idle: a donor loop cut to a few frames jitters).
 * The donor animation for each id comes from donor_pick_anim (map, then
 * defaults). Returns the number of records used, or -1 if `n` is out of
 * range or the donor has no model / animation. */
int clone_build_resources(const CloneResIn* in, int n, const Donor* d,
                          const CloneResLayout* lay, const DonorAnimMap* map,
                          int map_count, const uint16_t* defaults, int default_count,
                          const uint16_t* native, int native_count,
                          uint8_t* nodes_out, uint8_t* entries_out, uint8_t* records_out,
                          CloneResStats* st);

#endif /* BBT_CLONE_RES_H */
