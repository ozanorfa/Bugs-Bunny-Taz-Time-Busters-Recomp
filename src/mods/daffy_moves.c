/*
 * Daffy's moves (Phase 5): Bugs' animations retargeted onto Daffy's
 * skeleton (retarget.c), one per animation id that Taz's script plays and
 * Bugs also has, at Taz's own frame count and speed. Host-side, run once
 * when the Daffy donor is loaded; integer math only (the output is guest
 * data and must be the same on every netplay peer).
 *
 * Why Bugs: the third actor runs Taz's behaviour script, so it asks for
 * Taz's animation ids; 46 of Taz's 67 ids in Granwich are also Bugs' ids
 * (the two heroes share the move numbering: idle chain 0x001F.., run
 * 0x0033, jump 0x0036, hurt 0x00AA, death 0x00AE...). Bugs stands upright
 * on long legs like Daffy; Taz's crouch put on Daffy left his feet in the
 * air (NOTES.md "Daffy as the third character").
 */
#include "daffy_moves.h"

#include <stdlib.h>
#include <string.h>

#include "daffy_profile.h"
#include "mod_plugins.h"
#include "retarget.h"
#include "trca01_profile.h"

#define SOURCE_FILE "LEVEL1/GRANNY.BZZ"   /* Bugs and Taz, every animation */
#define BUGS_IDLE   0x001Fu

/* Bugs node -> Daffy node, with the main child of each (the bone whose
 * direction is matched). Skeletons from their set-up animations
 * (tools: scratchpad daffy/show_skel.py): Bugs 41 nodes, Daffy 39. */
static const RtJoint kJoints[] = {
    {1, 1, 0, 0},      /* actor root */
    {2, 2, 3, 3},      /* hips -> chest */
    {3, 3, 4, 4},      /* chest -> neck */
    {4, 4, 5, 5},      /* neck -> head */
    {5, 5, 0, 0},      /* head */
    {15, 16, 16, 17}, {16, 17, 17, 18}, {17, 18, 0, 0},   /* one arm, hand */
    {18, 19, 0, 0}, {19, 20, 0, 0}, {20, 21, 0, 0},       /* its fingers */
    {21, 23, 22, 24}, {22, 24, 23, 25}, {23, 25, 0, 0},   /* the other arm */
    {24, 26, 0, 0}, {25, 27, 0, 0}, {26, 28, 0, 0},
    {28, 30, 29, 31}, {29, 31, 30, 32}, {30, 32, 31, 33}, {31, 33, 0, 0},  /* one leg */
    {32, 34, 33, 35}, {33, 35, 34, 36}, {34, 36, 35, 37}, {35, 37, 0, 0},  /* the other */
    {36, 39, 0, 0},    /* attach node (root commands, the tornado) */
};
static const uint8_t kToesBugs[2] = {31, 35};
static const uint8_t kToesDaffy[2] = {33, 37};

/* Taz's spin family keeps the synthetic spin; Bugs' animation of the same
 * number is another move (0x004F: 4 frames for Bugs, Taz's 24-frame spin). */
static int spin_family(uint16_t sub)
{
    return sub == 0x0072u || sub == 0x004Fu || sub == 0x0097u || sub == 0x0073u;
}

static const DonorBlob* find(const Donor* d, uint16_t sub)
{
    for (int i = 0; i < d->anim_count; ++i)
        if (d->anims[i].sub == sub)
            return &d->anims[i];
    return NULL;
}

int bbt_daffy_add_moves(Donor* daffy, DonorAnimMap* map, int cap, uint16_t hold_sub,
                        uint16_t hold_frames, BbtDaffyMoveStats* stats)
{
    memset(stats, 0, sizeof *stats);
    uint32_t size = 0;
    if (!psx_mod_read_disc_file(SOURCE_FILE, NULL, 0, &size) || size == 0)
        return -1;
    uint8_t* file = (uint8_t*)malloc(size);
    if (!file)
        return -1;
    uint32_t got = 0;
    Donor bugs, taz;
    memset(&bugs, 0, sizeof bugs);
    memset(&taz, 0, sizeof taz);
    int n = -1;
    uint8_t* buf = NULL;
    RtRig* rig = (RtRig*)malloc(sizeof *rig);
    if (!rig || !psx_mod_read_disc_file(SOURCE_FILE, file, size, &got) || got != size ||
        !donor_extract(file, size, TYPE_BUGS, &bugs) || !donor_extract(file, size, TYPE_TAZ, &taz))
        goto out;
    const DonorBlob* bs = find(&bugs, 0x0001u);
    const DonorBlob* br = find(&bugs, BUGS_IDLE);
    const DonorBlob* ds = find(daffy, 0x0001u);
    const DonorBlob* dr = find(daffy, DAFFY_POSE_SUB);
    if (!bs || !br || !ds || !dr ||
        !rt_rig_build(rig, bugs.image + bs->off, bs->size, bugs.image + br->off, br->size,
                      daffy->image + ds->off, ds->size, daffy->image + dr->off, dr->size,
                      kJoints, (int)(sizeof kJoints / sizeof kJoints[0]), 2, 2, kToesBugs,
                      kToesDaffy))
        goto out;
    const uint32_t cap_bytes = 0x10000u;
    buf = (uint8_t*)malloc(cap_bytes);
    if (!buf)
        goto out;
    n = 0;
    /* the still pose: Bugs' idle, frame 0 */
    {
        const DonorBlob* ti = find(&taz, BUGS_IDLE);
        const uint8_t* hdr = ti ? taz.image + ti->off : bugs.image + br->off;
        const uint32_t len = rt_still(rig, bugs.image + br->off, br->size, 0, hdr, hold_frames,
                                      buf, cap_bytes);
        if (len && donor_append_anim(daffy, hold_sub, buf, len)) {
            stats->hold = 1;
            stats->bytes += len;
        }
    }
    for (int i = 0; i < taz.anim_count && n < cap; ++i) {
        const DonorBlob* t = &taz.anims[i];
        if (t->sub == 0x0001u || spin_family(t->sub) || find(daffy, t->sub))
            continue;
        const DonorBlob* b = find(&bugs, t->sub);
        if (!b) {
            ++stats->without_source;
            continue;
        }
        const uint32_t len = rt_retarget(rig, bugs.image + b->off, b->size, taz.image + t->off,
                                         t->frames, buf, cap_bytes);
        if (!len || !donor_append_anim(daffy, t->sub, buf, len)) {
            ++stats->failed;
            continue;
        }
        map[n++] = (DonorAnimMap){t->sub, t->sub};
        ++stats->moves;
        stats->bytes += len;
    }
out:
    free(buf);
    free(rig);
    donor_free(&bugs);
    donor_free(&taz);
    free(file);
    return n;
}
