/*
 * Third actor — Bugs Bunny & Taz: Time Busters (SLUS-01144). Phase 4.
 *
 * Feature "p3" of package bbt.third-character, plugin
 * bbt.third-character.p3-actor (with bbt.third-character.p3, the third
 * player's input, third_player.c).
 *
 * P4-M1 (spawn): in 2-player mode, every level with Taz gets a third
 * character: a clone of Taz's actor (his behaviour script, moves and
 * physics) drawn with the TRCA01 enemy's model, animations and textures,
 * while Taz stays Taz. P4-M2 (control): the clone's update reads the third
 * player's input record instead of player 2's.
 *
 * How the game runs its actors (Ghidra, NOTES.md "Phase 4 — third actor"):
 *   - FUN_80013030 (per view, every frame, and once right after a level
 *     load) walks the placed-actor list 0x8008DDCC and builds the list of
 *     actors to update and draw (DAT_8008DD60). Every actor with the hero
 *     flag (type word +0x08 bit 0x04000000, "objetat_Hero") is not put in
 *     that list but registered as a player instead: identity +0x34 == 2 ->
 *     P1 (0x8008DD04), anything else -> P2 (0x8008DD08), so a second Taz
 *     would take player 2's place. Player 1 and 2 are then put at the
 *     front of the list.
 *   - FUN_800150E0 runs each listed actor's class update from the table at
 *     0x80081518 by its class byte +0x31; class 0x0E is Actor_Update
 *     (0x800161B8), the characters' update. It reads input slot
 *     (+0x0C & 0x800) != 0 (0x80010008 + slot * 0x28) for scripts and
 *     movement.
 *   - The model is bound lazily: when +0x9C (draw-node list) is 0, the
 *     animation player calls FUN_80057768, which takes the model from the
 *     resource-list entry with sub 0x0D and plays the set-up animation
 *     (sub 0x0001) to build +0x9C.
 *   - The game spawns actors itself the same way (FUN_80015758: copy a
 *     template actor into a free slot of the actor pool 0x8008DD74,
 *     capacity 0x8008DEAC, fix +0x00 / +0xE4 / position, append to
 *     0x8008DDCC).
 *
 * So, on the first actor pass after a 2-player level load (Taz still
 * untouched: +0x24 bit 0x8000 "INITOK" clear, no draw nodes yet), this
 * plugin copies Taz's actor into a free pool slot, gives the copy its own
 * resource list (clone_res.c; in mod memory: the game's pools are too small
 * for a second one), places it next to him and appends it to 0x8008DDCC.
 * During every actor pass its hero flag is cleared, so it is listed like any
 * other actor (and updated and drawn after the players) instead of
 * replacing player 2; the flag is back before its update runs. Around its
 * Actor_Update, input slot 1's fields +0x00..+0x0D hold the third player's
 * record (third_player.c) and player 2's own are put back afterwards.
 *
 * Textures: Taz keeps his VRAM, so the enemy's texels and CLUTs go to VRAM
 * nothing else uses: x 512..1023, y 240..255, the rows below the first
 * displayed frame buffer (display 512x240 at 512,0 / 512,256, draw areas
 * 512,16 and 512,272 of 512x216: gpu_state; the frame's fills cover only
 * the draw areas and every animated-texture copy lands at x < 512: GPU
 * frame dumps in Granwich). Those 16 rows are lower than most of the
 * enemy's blocks (up to 32 rows), so such blocks keep every second texel
 * row and the model's records sample them at half the vertical resolution.
 *
 * State: everything that changes during play is in guest memory (the mod
 * block below and the pool slot), so savestates and rollback carry it.
 * Host-side, only what is a pure function of the disc: the prepared donor
 * image and the texture plan.
 *
 * Tests: {"cmd":"mod_cmd","name":"bbt.p3actor","args":"show"|"group <n>"},
 * counters bbt.third-character.p3-actor.*, tests/test_game_p3_actor.py.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clone_res.h"
#include "cpu_state.h"
#include "donor_runtime.h"
#include "func_override.h"
#include "mod_plugins.h"
#include "third_player.h"
#include "trca01_profile.h"

#define PLUGIN_ACTOR "bbt.third-character.p3-actor"

#define ADDR_ACTOR_PASS     0x80013030u   /* FUN_80013030 */
#define ADDR_ACTOR_UPDATE   0x800161B8u   /* Actor_Update, class 0x0E */
#define ADDR_PLACED         0x8008DDCCu   /* placed-actor list head */
#define ADDR_P1             0x8008DD04u
#define ADDR_P2             0x8008DD08u
#define ADDR_ACTOR_POOL     0x8008DD74u   /* actor pool base */
#define ADDR_ACTOR_POOL_CAP 0x8008DEACu   /* u16 capacity */
#define ADDR_MODE_FLAGS     0x800100A4u
#define MODE_TWO_PLAYER     0x10u
#define ADDR_LEVEL_ID       0x80010000u
#define ADDR_INPUT_SLOT1    0x80010030u   /* 0x80010008 + 1 * 0x28 */
#define INPUT_FIELDS        0x0Eu         /* +0x00..+0x0D, what Pad_FillSlot writes */

#define ACTOR_SIZE   0x1B0u
#define A_NEXT       0x000u
#define A_POOL_FLAGS 0x004u   /* u16, bit 0 = free pool slot */
#define A_TYPE       0x008u
#define A_STATE1     0x024u   /* bit 0x8000 = INITOK (Actor_Update's first run done) */
#define A_SHADOW     0x088u   /* allocated on the first update */
#define A_RES        0x098u
#define A_DRAW       0x09Cu
#define A_MATRIX_T   0x0B8u   /* s32 translation of the matrix at +0xA4 */
#define A_MATRIX2_T  0x0D8u   /* second copy at +0xC4 */
#define A_E4         0x0E4u   /* -> own scale (+0xEC) while it has no parent */
#define A_E8         0x0E8u
#define A_SCALE      0x0ECu
#define A_POS        0x0FCu   /* s16 x, y, z */
#define A_RES_B      0x170u
#define A_RES_C      0x174u
#define HERO_FLAG    0x04000000u
#define INITOK       0x8000u
#define POOL_KEEP_FREE 8      /* actor slots left to the game's own spawns */
/* Size option ("size" of feature p3): percent of the enemy's own size,
 * written to the clone's scale +0xEC/+0xEE/+0xF0 (s16, 0x1000 = 1.0;
 * verified in RAM in Phase 3: shrinks the drawn model, movement unchanged). */
#define DEFAULT_SIZE_PERCENT 75
#define SPAWN_GAP    160      /* distance from Taz, away from Bugs */
#define MAX_LIST_WALK 1024

/* Mod guest block (allocated once; fixed layout so savestates match). */
#define BLK_SIZE     0x20000u
#define BLK_STATE    0x0000u
#define BLK_SOURCE   0x0100u   /* Taz's actor as it was copied (diagnostics) */
#define BLK_NODES    0x0300u
#define BLK_ENTRIES  0x0900u
#define BLK_RECORDS  0x1700u
#define BLK_IMAGE    0x2000u
#define BLK_IMAGE_MAX (BLK_SIZE - BLK_IMAGE)
/* state fields */
#define ST_MAGIC     0x00u    /* 'BBT3' once initialised */
#define ST_ACTOR     0x04u    /* the clone (pool slot), 0 = none */
#define ST_LEVEL     0x08u    /* level id it was spawned in */
#define ST_SOURCE    0x0Cu    /* Taz's actor it was copied from */
#define ST_HIDDEN    0x10u    /* 1 while its hero flag is cleared for a pass */
#define ST_SPAWNS    0x14u
#define ST_SPAWN_XZ  0x18u    /* where it was placed: s16 x | s16 z << 16 */
#define ST_SLOT      0x20u    /* player 2's slot fields during the clone's update */
#define STATE_MAGIC  0x33544242u


#define GPR_A0 4
#define GPR_SP 29

/* addiu sp,sp,-0x888 / sw s2,0x868(sp) / move s2,a0 / lui v0,0x8009 */
static const uint32_t kActorPassGuard[4] = {
    0x27BDF778u, 0xAFB20868u, 0x00809021u, 0x3C028009u,
};
/* addiu sp,sp,-0x1c8 / sw s4,0x1b0(sp) / move s4,a0 / lui t7,0x1f80 */
static const uint32_t kActorUpdateGuard[4] = {
    0x27BDFE38u, 0xAFB401B0u, 0x0080A021u, 0x3C0F1F80u,
};

/* Host side: a pure function of the disc once prepared. */
static uint32_t g_block;           /* guest address of the mod block, 0 if none */
static int g_prepared;             /* 0 = not yet, 1 = ready, -1 = unusable */
static uint8_t* g_image;           /* donor image: relocated for g_block, records patched */
static uint32_t g_image_size;
static CloneResLayout g_layout;
static DonorTexPlan g_plan;
static int g_tex_planned;
static int16_t g_scale = 0x1000 * DEFAULT_SIZE_PERCENT / 100;
/* The third actor's own copy of the donor: the shared one (donor_runtime.c,
 * also Phase 3's) plus the hold animation (trca01_profile.h). */
static Donor g_donor;
static DonorAnimMap g_map[TAZ_MAP_COUNT + TAZ_IDLE_COUNT];
static int g_map_count;
static uint16_t g_defaults[DONOR_DEFAULT_COUNT];
static int g_default_count;

#define RT (bbt_donor_runtime())

/* psx_mod_counter_add keeps the name pointer: literal names only. */
#define count(event, n) psx_mod_counter_add(PLUGIN_ACTOR "." event, (uint32_t)(n))

static int in_main_ram(uint32_t p)
{
    return p >= 0x80000000u && p < 0x80200000u;
}

static uint32_t st_get(uint32_t f)
{
    return psx_mod_read_word(g_block + BLK_STATE + f);
}

static void st_set(uint32_t f, uint32_t v)
{
    psx_mod_write_word(g_block + BLK_STATE + f, v);
}

/* Build the host-side image once the donor is loaded. */
static void prepare(void)
{
    if (g_prepared != 0 || !g_block)
        return;
    if (bbt_donor_runtime_load() != 1) {
        if (RT->state < 0)
            g_prepared = -1;
        return;
    }
    if (!donor_copy(&RT->donor, &g_donor)) {
        g_prepared = -1;
        return;
    }
    /* Roles: Phase 3's table, plus Taz's idle chain on the enemy's stance at
     * its own length and speed; everything else holds a still pose. */
    g_map_count = 0;
    for (int i = 0; i < TAZ_MAP_COUNT; ++i)
        g_map[g_map_count++] = kTazToTrca01[i];
    for (int i = 0; i < TAZ_IDLE_COUNT; ++i)
        g_map[g_map_count++] = (DonorAnimMap){kTazIdleIds[i], DONOR_IDLE_SUB};
    if (donor_add_hold(&g_donor, DONOR_IDLE_SUB, DONOR_SUB_HOLD, DONOR_HOLD_FRAMES)) {
        g_defaults[0] = DONOR_SUB_HOLD;
        g_default_count = 1;
        count("hold_ready", 1);
    } else {
        /* Without it: Phase 3's standing defaults (moving loops, cut). */
        for (int i = 0; i < DONOR_DEFAULT_COUNT; ++i)
            g_defaults[i] = kDonorDefaults[i];
        g_default_count = DONOR_DEFAULT_COUNT;
        count("hold_unavailable", 1);
    }
    const Donor* d = &g_donor;
    if (d->image_size > BLK_IMAGE_MAX || d->model_count == 0 || d->anim_count == 0) {
        g_prepared = -1;
        count("donor_unusable", 1);
        return;
    }
    g_image = (uint8_t*)malloc(d->image_size);
    if (!g_image) {
        g_prepared = -1;
        return;
    }
    memcpy(g_image, d->image, d->image_size);
    g_image_size = d->image_size;
    const uint32_t image = g_block + BLK_IMAGE;

    g_tex_planned = RT->tex_ready &&
                    donor_tex_plan_strip(&RT->tex, P3_STRIP_X, P3_STRIP_Y, P3_STRIP_W,
                                         P3_STRIP_H, &g_plan);
    if (g_tex_planned) {
        for (int i = 0; i < RT->tex.record_count; ++i) {
            const DonorTexRecord* r = &RT->tex.records[i];
            uint8_t out[12];
            donor_tex_patch_record(&RT->tex, &g_plan, r, d->image + r->off, out);
            memcpy(g_image + r->off, out, 12);
        }
    }
    if (g_tex_planned)
        count("tex_planned", 1);
    else
        count("tex_unavailable", 1);

    memset(&g_layout, 0, sizeof g_layout);
    g_layout.nodes = g_block + BLK_NODES;
    g_layout.entries = g_block + BLK_ENTRIES;
    g_layout.records = g_block + BLK_RECORDS;
    g_layout.image = image;
    for (int k = 0; k < d->model_count; ++k) {
        const int rel = donor_relocate_model(g_image, g_image_size, &d->models[k], image);
        if (rel == 0) {
            g_prepared = -1;
            count("model_unusable", 1);
            return;
        }
        g_layout.model_data[k] = image + d->models[k].off + (rel == 1 ? 8u : 0u);
    }
    g_prepared = 1;
}

static void read_size_option(void)
{
    char v[16];
    int pct = DEFAULT_SIZE_PERCENT;
    if (psx_mod_option_value("bbt.third-character", "p3", "size", v, sizeof v)) {
        const int p = atoi(v);
        if (p >= 25 && p <= 150)
            pct = p;
    }
    g_scale = (int16_t)(0x1000 * pct / 100);
}

static void activate(void)
{
    read_size_option();
    if (!g_block)
        g_block = psx_mod_alloc_guest_memory(BLK_SIZE, 16);
    prepare();
}

/* Is actor `a` in the placed-actor list? (The clone is a pool slot like
 * any other actor; the walk follows main RAM pointers only.) */
static int linked(uint32_t a)
{
    uint32_t p = psx_mod_read_word(ADDR_PLACED);
    for (int n = 0; in_main_ram(p) && n < MAX_LIST_WALK; ++n) {
        if (p == a)
            return 1;
        p = psx_mod_read_word(p + A_NEXT);
    }
    return 0;
}

static uint32_t list_tail(void)
{
    uint32_t p = psx_mod_read_word(ADDR_PLACED), last = 0;
    for (int n = 0; in_main_ram(p) && n < MAX_LIST_WALK; ++n) {
        last = p;
        p = psx_mod_read_word(p + A_NEXT);
    }
    return p == 0 ? last : 0;
}

/* The game's Pool_Alloc (0x80015434) on the actor pool, keeping
 * POOL_KEEP_FREE slots for the game: first free slot (+0x04 bit 0), its
 * words +0x00 and +0x04 cleared. Returns 0 if the pool is too full. */
static uint32_t alloc_actor_slot(void)
{
    const uint32_t pool = psx_mod_read_word(ADDR_ACTOR_POOL);
    const uint32_t cap = psx_mod_read_half(ADDR_ACTOR_POOL_CAP);
    if (!in_main_ram(pool) || cap == 0 || !in_main_ram(pool + cap * ACTOR_SIZE - 1u))
        return 0;
    uint32_t first = 0, free_slots = 0;
    for (uint32_t i = 0; i < cap; ++i) {
        const uint32_t a = pool + i * ACTOR_SIZE;
        if (psx_mod_read_word(a + A_POOL_FLAGS) & 1u) {
            if (!first)
                first = a;
            ++free_slots;
        }
    }
    if (!first || free_slots <= POOL_KEEP_FREE)
        return 0;
    psx_mod_write_word(first + A_NEXT, 0);
    psx_mod_write_word(first + A_POOL_FLAGS, 0);
    return first;
}

static void write_bytes(uint32_t addr, const uint8_t* p, uint32_t n)
{
    uint32_t i = 0;
    for (; i + 4 <= n; i += 4)
        psx_mod_write_word(addr + i, (uint32_t)p[i] | ((uint32_t)p[i + 1] << 8) |
                                     ((uint32_t)p[i + 2] << 16) | ((uint32_t)p[i + 3] << 24));
    for (; i < n; ++i)
        psx_mod_write_byte(addr + i, p[i]);
}

static void read_bytes(uint32_t addr, uint8_t* p, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        p[i] = psx_mod_read_byte(addr + i);
}

static int16_t rd16(const uint8_t* p) { return (int16_t)(p[0] | (p[1] << 8)); }
static int32_t rd32(const uint8_t* p) { return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }
static void wr16(uint8_t* p, int16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)((uint16_t)v >> 8); }
static void wr32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* Taz untouched since the load: the clone then starts exactly as a freshly
 * loaded actor does (its own first Actor_Update initialises it). */
static int source_pristine(uint32_t taz)
{
    return in_main_ram(taz) && in_main_ram(taz + ACTOR_SIZE - 1u) &&
           psx_mod_read_word(taz + A_TYPE) == TYPE_TAZ &&
           (psx_mod_read_word(taz + A_POOL_FLAGS) & 1u) == 0 &&
           (psx_mod_read_word(taz + A_STATE1) & INITOK) == 0 &&
           psx_mod_read_word(taz + A_DRAW) == 0 && psx_mod_read_word(taz + A_SHADOW) == 0;
}

/* Taz's placed actor (list 0x8008DDCC, identity 3). Right after a load
 * player 2 (0x8008DD08) still names his definition actor (list 0x8008DD44,
 * the template the game spawns copies from; seen: identity 182, position
 * of the template); the first actor pass registers the placed one. */
static uint32_t placed_taz(void)
{
    uint32_t p = psx_mod_read_word(ADDR_PLACED);
    for (int n = 0; in_main_ram(p) && n < MAX_LIST_WALK; ++n) {
        if (psx_mod_read_word(p + A_TYPE) == TYPE_TAZ)
            return p;
        p = psx_mod_read_word(p + A_NEXT);
    }
    return 0;
}

static int upload_textures(uint32_t sp)
{
    if (!g_tex_planned)
        return 0;
    const DonorTex* t = &RT->tex;
    uint16_t px[64 * 16];
    int failed = 0;
    /* CLUTs first: an upload of 16 words or more ends with GP1(04h) = DMA
     * CPU->GP0, the state the game keeps between frames; a CLUT (8 words)
     * leaves it off. */
    for (int g = 0; g < t->clut_count; ++g)
        failed += !bbt_upload_rect(sp, g_plan.clut_x[g], g_plan.clut_y[g], 16, 1,
                                   t->pixels + t->clut_px + 16u * (uint32_t)g);
    for (int i = 0; i < t->block_count; ++i) {
        const uint32_t n = donor_tex_block_pixels(t, &g_plan, i, px, (uint32_t)(sizeof px / sizeof px[0]));
        failed += !n || !bbt_upload_rect(sp, g_plan.block_x[i], g_plan.block_y[i],
                                         t->blocks[i].w, g_plan.block_h[i], px);
    }
    if (failed)
        count("tex_upload_failed", (uint32_t)failed);
    else
        count("tex_uploaded", 1);
    return failed == 0;
}

/* Spawn the clone of Taz. Called at the start of an actor pass, before the
 * game builds its update list. */
static void spawn(CPUState* cpu, uint32_t taz)
{
    /* Taz's resource list. */
    static CloneResIn in[CLONE_RES_MAX];
    int n = 0;
    uint32_t node = psx_mod_read_word(taz + A_RES);
    for (; in_main_ram(node) && n < CLONE_RES_MAX; ++n) {
        const uint32_t e = psx_mod_read_word(node + 8u);
        if (!in_main_ram(e)) {
            count("spawn_bad_list", 1);
            return;
        }
        read_bytes(e, in[n].entry, RES_ENTRY_SIZE);
        memset(in[n].record, 0, ANIM_RECORD_SIZE);
        const uint32_t rec = psx_mod_read_word(e + 0x18u);
        if ((in[n].entry[8] | (in[n].entry[9] << 8)) == RES_TYPE_ANIM && in_main_ram(rec))
            read_bytes(rec, in[n].record, ANIM_RECORD_SIZE);
        node = psx_mod_read_word(node);
    }
    if (node != 0 || n == 0) {
        count("spawn_bad_list", 1);
        return;
    }
    static uint8_t nodes[CLONE_RES_MAX * RES_NODE_SIZE];
    static uint8_t entries[CLONE_RES_MAX * RES_ENTRY_SIZE];
    static uint8_t records[CLONE_RES_MAX * ANIM_RECORD_SIZE];
    CloneResStats st;
    const int nrec = clone_build_resources(in, n, &g_donor, &g_layout, g_map, g_map_count,
                                           g_defaults, g_default_count, kTazIdleIds,
                                           TAZ_IDLE_COUNT, nodes, entries, records, &st);
    if (nrec < 0) {
        count("spawn_bad_list", 1);
        return;
    }
    const uint32_t tail = list_tail();
    if (!tail) {
        count("spawn_bad_list", 1);
        return;
    }
    const uint32_t a = alloc_actor_slot();
    if (!a) {
        count("spawn_pool_full", 1);
        return;
    }

    /* Donor first, then the list that points at it, then the actor. */
    write_bytes(g_block + BLK_IMAGE, g_image, g_image_size);
    write_bytes(g_block + BLK_NODES, nodes, (uint32_t)n * RES_NODE_SIZE);
    write_bytes(g_block + BLK_ENTRIES, entries, (uint32_t)n * RES_ENTRY_SIZE);
    write_bytes(g_block + BLK_RECORDS, records, (uint32_t)nrec * ANIM_RECORD_SIZE);

    uint8_t act[ACTOR_SIZE];
    read_bytes(taz, act, ACTOR_SIZE);
    write_bytes(g_block + BLK_SOURCE, act, ACTOR_SIZE);
    wr32(act + A_NEXT, 0);
    act[A_POOL_FLAGS] &= (uint8_t)~1u;
    wr32(act + A_RES, g_layout.nodes);
    wr32(act + A_RES_B, g_layout.nodes);
    wr32(act + A_RES_C, g_layout.nodes);
    wr32(act + A_DRAW, 0);
    wr32(act + A_E4, a + A_SCALE);
    wr32(act + A_E8, 0);
    for (uint32_t k = 0; k < 3; ++k)
        wr16(act + A_SCALE + 2u * k, g_scale);

    /* Next to Taz, on the side away from Bugs. */
    const uint32_t bugs = psx_mod_read_word(ADDR_P1);
    int dx = SPAWN_GAP, dz = 0;
    if (in_main_ram(bugs)) {
        const int vx = rd16(act + A_POS) - (int16_t)psx_mod_read_half(bugs + A_POS);
        const int vz = rd16(act + A_POS + 4) - (int16_t)psx_mod_read_half(bugs + A_POS + 4);
        const int ax = vx < 0 ? -vx : vx, az = vz < 0 ? -vz : vz;
        const int len = ax > az ? ax : az;
        if (len > 0) {
            dx = vx * SPAWN_GAP / len;
            dz = vz * SPAWN_GAP / len;
        }
    }
    const int16_t ox = rd16(act + A_POS), oz = rd16(act + A_POS + 4);
    wr16(act + A_POS, (int16_t)(ox + dx));
    wr16(act + A_POS + 4, (int16_t)(oz + dz));
    for (uint32_t m = 0; m < 2; ++m) {
        uint8_t* t = act + (m ? A_MATRIX2_T : A_MATRIX_T);
        if (rd32(t) == ox && rd32(t + 8) == oz) {
            wr32(t, (uint32_t)(ox + dx));
            wr32(t + 8, (uint32_t)(oz + dz));
        }
    }
    write_bytes(a, act, ACTOR_SIZE);
    psx_mod_write_word(tail + A_NEXT, a);

    st_set(ST_ACTOR, a);
    st_set(ST_LEVEL, psx_mod_read_word(ADDR_LEVEL_ID));
    st_set(ST_SOURCE, taz);
    st_set(ST_HIDDEN, 0);
    st_set(ST_SPAWNS, st_get(ST_SPAWNS) + 1u);
    st_set(ST_SPAWN_XZ, (uint16_t)(ox + dx) | ((uint32_t)(uint16_t)(oz + dz) << 16));
    count("spawned", 1);
    count("anims_mapped", (uint32_t)st.anims_mapped);
    count("anims_default", (uint32_t)st.anims_default);
    count("models", (uint32_t)st.models);
    count("anims_native", (uint32_t)st.native);
    (void)upload_textures(cpu->gpr[GPR_SP]);
}

/* Wrap of the actor pass FUN_80013030(list, ...). */
static int actor_pass_wrap(CPUState* cpu)
{
    if (!g_block || g_prepared != 1) {
        prepare();
        func_override_call_original(cpu);
        return 1;
    }
    if (st_get(ST_MAGIC) != STATE_MAGIC) {
        for (uint32_t f = 0; f < 0x40u; f += 4)
            st_set(f, 0);
        st_set(ST_MAGIC, STATE_MAGIC);
    }
    uint32_t a = st_get(ST_ACTOR);
    /* Still ours? After a level load the pool slot may hold one of the new
     * level's actors (same pool layout), which never points at our list. */
    if (a && (!linked(a) || (psx_mod_read_word(a + A_POOL_FLAGS) & 1u) ||
              psx_mod_read_word(a + A_RES) != g_layout.nodes)) {
        st_set(ST_ACTOR, 0);       /* removed by the game, or a new level */
        a = 0;
        count("gone", 1);
    }
    if (!a && (psx_mod_read_byte(ADDR_MODE_FLAGS) & MODE_TWO_PLAYER)) {
        const uint32_t taz = placed_taz();
        if (source_pristine(taz)) {
            spawn(cpu, taz);
            a = st_get(ST_ACTOR);
        }
    }
    uint32_t hidden = 0;
    if (a) {
        const uint32_t type = psx_mod_read_word(a + A_TYPE);
        if (type & HERO_FLAG) {
            psx_mod_write_word(a + A_TYPE, type & ~HERO_FLAG);
            hidden = 1;
        }
    }
    st_set(ST_HIDDEN, hidden);
    func_override_call_original(cpu);
    if (hidden) {
        psx_mod_write_word(a + A_TYPE, psx_mod_read_word(a + A_TYPE) | HERO_FLAG);
        st_set(ST_HIDDEN, 0);
    }
    return 1;
}

/* Wrap of Actor_Update(actor): the clone reads the third player's input. */
static int actor_update_wrap(CPUState* cpu)
{
    const uint32_t a = cpu->gpr[GPR_A0];
    if (!g_block || g_prepared != 1 || a == 0 || a != st_get(ST_ACTOR)) {
        func_override_call_original(cpu);
        return 1;
    }
    const uint32_t rec = bbt_p3_record();
    uint8_t p2[INPUT_FIELDS], p3[INPUT_FIELDS];
    read_bytes(ADDR_INPUT_SLOT1, p2, INPUT_FIELDS);
    write_bytes(g_block + BLK_STATE + ST_SLOT, p2, INPUT_FIELDS);
    if (rec)
        read_bytes(rec, p3, INPUT_FIELDS);
    else
        memset(p3, 0, sizeof p3);
    write_bytes(ADDR_INPUT_SLOT1, p3, INPUT_FIELDS);
    func_override_call_original(cpu);
    /* What the update consumed stays consumed for the third player. */
    if (rec) {
        read_bytes(ADDR_INPUT_SLOT1, p3, INPUT_FIELDS);
        write_bytes(rec, p3, INPUT_FIELDS);
    }
    read_bytes(g_block + BLK_STATE + ST_SLOT, p2, INPUT_FIELDS);
    write_bytes(ADDR_INPUT_SLOT1, p2, INPUT_FIELDS);
    count("updates", 1);
    return 1;
}

/* Debug command (debug-server thread): host state that is immutable once
 * prepared, and the guest block's address (tests read the rest with
 * read_ram). */
static int p3actor_command(const char* args, char* reply, int cap)
{
    if (args && strncmp(args, "group ", 6) == 0) {
        const int g = atoi(args + 6);
        if (g_prepared != 1 || !g_tex_planned || g < 0 || g >= RT->tex.clut_count) {
            snprintf(reply, (size_t)cap, "{\"ok\":false,\"err\":\"no such group\"}");
            return 0;
        }
        const DonorTex* t = &RT->tex;
        uint16_t px[64 * 32];
        const uint32_t n = donor_tex_block_pixels(t, &g_plan, g, px, (uint32_t)(sizeof px / sizeof px[0]));
        snprintf(reply, (size_t)cap,
                 "{\"ok\":true,\"group\":%d,\"src_clut\":%u,\"block\":[%u,%u,%u,%u],"
                 "\"clut\":[%u,%u],\"tpage\":%u,\"vshift\":%u,\"block_fnv\":\"%08X\","
                 "\"clut_fnv\":\"%08X\"}",
                 g, t->cluts[g], g_plan.block_x[g], g_plan.block_y[g], t->blocks[g].w,
                 g_plan.block_h[g], g_plan.clut_x[g], g_plan.clut_y[g], g_plan.tpage[g],
                 g_plan.vshift[g], bbt_fnv1a_pixels(px, n),
                 bbt_fnv1a_pixels(t->pixels + t->clut_px + 16u * (uint32_t)g, 16u));
        return 1;
    }
    snprintf(reply, (size_t)cap,
             "{\"ok\":true,\"block\":\"0x%08X\",\"prepared\":%d,\"donor_state\":%d,"
             "\"image\":\"0x%08X\",\"image_size\":%u,\"tex\":%d,\"groups\":%d,"
             "\"state\":\"0x%08X\",\"source_copy\":\"0x%08X\",\"nodes\":\"0x%08X\","
             "\"entries\":\"0x%08X\",\"records\":\"0x%08X\"}",
             g_block, g_prepared, RT->state, g_block ? g_block + BLK_IMAGE : 0u, g_image_size,
             g_tex_planned, g_tex_planned ? RT->tex.clut_count : 0, g_block + BLK_STATE,
             g_block + BLK_SOURCE, g_block + BLK_NODES, g_block + BLK_ENTRIES,
             g_block + BLK_RECORDS);
    return 1;
}

PSX_MOD_CONSTRUCTOR(register_bbt_third_actor)
{
    (void)psx_mod_register_activation_plugin(PLUGIN_ACTOR, activate);
    (void)psx_mod_register_function_override(
        PLUGIN_ACTOR ":actor-pass", ADDR_ACTOR_PASS,
        actor_pass_wrap, kActorPassGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(
        PLUGIN_ACTOR ":actor-update", ADDR_ACTOR_UPDATE,
        actor_update_wrap, kActorUpdateGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_command("bbt.p3actor", p3actor_command);
}
