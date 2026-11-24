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
 * Enemies: scripts that ask for the nearer / farther hero (FUN_80029424
 * codes 4-7) also consider the clone (player_ref_wrap).
 *
 * When it appears (option "appear", default "auto"): only with a third
 * player: in netplay when seat 2 is occupied, offline when seat 2 has a
 * controller (Settings -> Input, Player 3) and has pressed a button. "always" spawns it anyway (the
 * tests drive seat 2 through the debug command).
 *
 * Daffy (option "character" = daffy): his attack is drawn (draw_pass_wrap)
 * and his sounds are Bugs' (sound_start_wrap, option "sounds"); NOTES.md
 * "Daffy's spin and sounds".
 *
 * Its death: out, then back next to a hero in play after 5 s (option
 * "comeback" = "auto", the default), or, with "team", the game's rule for
 * Bugs and Taz: out until the team respawns (watch_team); a package brings
 * it back in both cases (package_wrap).
 *
 * Tests: {"cmd":"mod_cmd","name":"bbt.p3actor","args":"show"|"group <n>"|
 * "ref-third off"|"ref-third on"|"package <hex actor>"} ("ref-third"
 * switches the codes 4-7 rule, for an A/B; "package" runs the game's package
 * action for that actor at the next actor pass; tests only),
 * counters bbt.third-character.p3-actor.*, tests/test_game_p3_actor.py.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor_update_hub.h"
#include "clone_res.h"
#include "daffy_moves.h"
#include "daffy_profile.h"
#include "cpu_state.h"
#include "donor_runtime.h"
#include "func_override.h"
#include "mod_plugins.h"
#include "psx_netplay.h"
#include "sio.h"
#include "third_player.h"
#include "trca01_profile.h"
#include "mod_blocks.h"

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
#define A_ACTION     0x020u   /* ACTIONSTATE: 0x2000000 = DEATH */
#define DEATH_FLAG   0x02000000u
#define A_SPEED      0x070u   /* s32 + s16 at +0x74, zeroed by the death step */
#define A_HEALTH     0x03Cu   /* s16 */
#define A_CLIP       0x06Eu   /* u16 clip flags */
#define CLIP_PASVISIBLE 0x20u /* "not visible": the actor pass does not list it (no update, no draw) */
#define FULL_HEALTH  6        /* what the game's respawn gives both heroes (FUN_80022510) */
#define ADDR_BUGS_HERO   0x8008DDD0u   /* the identity-2 hero (Bugs) */
#define ADDR_TAZ_HERO    0x8008DDE4u   /* the identity-3 hero (Taz) */
/* Globals Actor_Update keys to a hero's identity (+0x34; the clone keeps
 * Taz's 3), NOTES.md "The third player's death". */
#define ADDR_HERO_DOWN   0x800100A2u   /* 0x10 / 0x20 Bugs / Taz died, 1 / 2 out */
#define ADDR_TAZ_HEALTH  0x800100A6u   /* identity-3 hero's health, kept between levels */
#define ADDR_TAZ_HIT     0x8008DA0Du   /* Taz's hit flash (HUD) */
#define ADDR_GAUGE       0x80093951u   /* gauge of a ForceToGauge (+0x0C 0x200) actor */
#define ADDR_DEATH_DONE  0x80022510u   /* FUN_80022510(hero, ...): its death is over */
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
#define BLK_SOURCE   0x0100u   /* Taz's actor as it was at the level's first actor pass (the clone's template) */
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
#define ST_NO_INPUT  0x30u    /* 1 while the clone's update runs without input (dead) */
#define ST_OUT       0x34u    /* 1 while the third player is out (kept across level loads) */
#define ST_TEAM_DOWN 0x38u    /* 1 while Bugs and Taz are both down */
#define ST_REF_OFF   0x3Cu    /* tests only (debug command "ref-third off"): scripts never pick the clone */
#define ST_HIT       0x40u    /* the clone's own hit counter (Taz's is 0x8008DA0D) */
#define ST_PREV_ANIM 0x44u    /* the clone's animation id after its last update (Daffy's attack sound) */
#define ST_P3_JOINED 0x48u    /* one PC: seat 2 has pressed a button (the third player joined) */
#define ST_TEST_PACKAGE 0x4Cu /* tests only (debug command "package"): the actor to run the package action for */
#define ST_OUT_FRAMES 0x50u   /* game frames since it went out (option "comeback" = "auto") */
#define ST_KEPT_TAZ  0x54u    /* Taz's placed actor whose untouched copy BLK_SOURCE holds (0 = none): a join during a level */
#define ST_KEPT_LEVEL 0x58u   /* and the level id it was kept in */
#define ST_LAST      ST_KEPT_LEVEL
#define COMEBACK_FRAMES 150u  /* 5 s at the game's 30 frames a second (0.5 per vblank, test_game_ownview3 test 3) */
#define ADDR_VIEW_INDEX 0x8008DEA0u /* byte: the view FUN_80011AF0 is running (own_view.c) */
#define STATE_MAGIC  0x33544242u


#define GPR_A0 4
#define GPR_SP 29

/* addiu sp,sp,-0x888 / sw s2,0x868(sp) / move s2,a0 / lui v0,0x8009 */
static const uint32_t kActorPassGuard[4] = {
    0x27BDF778u, 0xAFB20868u, 0x00809021u, 0x3C028009u,
};
/* Actor_Update (ADDR_ACTOR_UPDATE) is hooked through the shared hub,
 * src/mods/actor_update_hub.c: our features cannot each override it. */

/* Host side: a pure function of the disc once prepared. */
static uint32_t g_block;           /* guest address of the mod block, 0 if none */
static int g_prepared;             /* 0 = not yet, 1 = ready, -1 = unusable */
static int g_active;               /* this plugin is in the session's plan */
static uint8_t* g_image;           /* donor image: relocated for g_block, records patched */
static uint32_t g_image_size;
static CloneResLayout g_layout;
static DonorTexPlan g_plan;
static int g_tex_planned;
static int16_t g_scale = 0x1000 * DEFAULT_SIZE_PERCENT / 100;
/* The third actor's own copy of the donor: the shared one (donor_runtime.c,
 * also Phase 3's) plus the hold animation (trca01_profile.h). */
static Donor g_donor;
#define MAP_CAP 112
static DonorAnimMap g_map[MAP_CAP];
static int g_map_count;
static uint16_t g_defaults[DONOR_DEFAULT_COUNT];
static int g_default_count;

/* Which character the clone is drawn as (option "character", read at
 * activation): 0 the TRCA01 enemy, 1 Daffy (daffy_profile.h). */
static int g_daffy_char;
/* The g_daffy_char that prepare()'s results are for, -1 before the first
 * activation (BUGS #55: the option can change between sessions). */
static int g_prepared_for = -1;
#define RT (g_daffy_char ? bbt_daffy_runtime() : bbt_donor_runtime())
/* Daffy's attack moves were built from Bugs' attack (daffy_moves.c): his
 * body is drawn during the spin (draw_pass_wrap). */
static int g_daffy_attack;
/* Option "sounds" (Daffy only): which sounds his moves make. */
enum { SOUNDS_BUGS, SOUNDS_TAZ, SOUNDS_QUIET };
static int g_sounds = SOUNDS_BUGS;
/* Option "appear": 1 = always, 0 = only with a third player (the default). */
static int g_appear_always;
static int g_comeback_team;        /* option "comeback" = "team": out until the team respawns or a package */
#define P3_SEAT_INDEX 2   /* the third player's seat (third_player.c) */

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
    if ((g_daffy_char ? bbt_daffy_runtime_load() : bbt_donor_runtime_load()) != 1) {
        if (RT->state < 0)
            g_prepared = -1;
        return;
    }
    if (!donor_copy(&RT->donor, &g_donor)) {
        g_prepared = -1;
        return;
    }
    g_map_count = 0;
    uint16_t hold_src = DONOR_IDLE_SUB;
    if (g_daffy_char) {
        /* Daffy: his set-up; Bugs' moves retargeted onto him (daffy_moves.c:
         * his attack for Taz's spin family, the still pose from Bugs' idle);
         * everything else on the still pose. */
        g_map[g_map_count++] = (DonorAnimMap){0x0001u, 0x0001u};
        hold_src = DAFFY_POSE_SUB;
        BbtDaffyMoveStats ms;
        const int k = bbt_daffy_add_moves(&g_donor, g_map + g_map_count, MAP_CAP - g_map_count,
                                          DONOR_SUB_HOLD, DONOR_HOLD_FRAMES, &ms);
        if (k > 0)
            g_map_count += k;
        /* Without Bugs' attack: the synthetic spin (spin start too: it must
         * name the attach node for the tornado). */
        static const uint16_t kSpin[] = {0x0072u, 0x004Fu, 0x0097u};
        for (int i = 0; i < 3; ++i) {
            int mapped = 0;
            for (int m = 0; m < g_map_count; ++m)
                mapped |= g_map[m].target_sub == kSpin[i];
            if (!mapped && g_map_count < MAP_CAP)
                g_map[g_map_count++] = (DonorAnimMap){kSpin[i], DONOR_SUB_SPIN};
        }
        g_daffy_attack = ms.attack == 3;
        count("daffy_attack", ms.attack);
        count("daffy_moves", ms.moves);
        count("daffy_move_bytes", ms.bytes);
        count("daffy_without_source", ms.without_source);
        count("daffy_failed", ms.failed);
        if (ms.hold)
            hold_src = 0;
    } else {
        /* Roles: Phase 3's table, plus Taz's idle chain on the enemy's
         * stance at its own length and speed; everything else holds a
         * still pose. */
        for (int i = 0; i < TAZ_MAP_COUNT; ++i)
            g_map[g_map_count++] = kTazToTrca01[i];
        for (int i = 0; i < TAZ_IDLE_COUNT; ++i)
            g_map[g_map_count++] = (DonorAnimMap){kTazIdleIds[i], DONOR_IDLE_SUB};
    }
    if (!hold_src || donor_add_hold(&g_donor, hold_src, DONOR_SUB_HOLD, DONOR_HOLD_FRAMES)) {
        g_defaults[0] = DONOR_SUB_HOLD;
        g_default_count = 1;
        count("hold_ready", 1);
    } else if (g_daffy_char) {
        g_prepared = -1;
        count("hold_unavailable", 1);
        return;
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

static void read_options(void)
{
    char v[16];
    /* the manifest's defaults: Daffy, Bugs' sounds, only with a third player */
    g_daffy_char = psx_mod_option_value("bbt.third-character", "p3", "character", v, sizeof v)
                       ? strcmp(v, "daffy") == 0
                       : 1;
    g_sounds = SOUNDS_BUGS;
    if (psx_mod_option_value("bbt.third-character", "p3", "sounds", v, sizeof v))
        g_sounds = strcmp(v, "taz") == 0 ? SOUNDS_TAZ : strcmp(v, "quiet") == 0 ? SOUNDS_QUIET : SOUNDS_BUGS;
    g_appear_always = psx_mod_option_value("bbt.third-character", "p3", "appear", v, sizeof v) &&
                      strcmp(v, "always") == 0;
    g_comeback_team = psx_mod_option_value("bbt.third-character", "p3", "comeback", v, sizeof v) &&
                      strcmp(v, "team") == 0;
    if (g_daffy_char) {
        /* his own size: a little shorter than Bugs, as in the cartoons; the
         * size option is the enemy's */
        g_scale = 0x1000;
        return;
    }
    int pct = DEFAULT_SIZE_PERCENT;
    if (psx_mod_option_value("bbt.third-character", "p3", "size", v, sizeof v)) {
        const int p = atoi(v);
        if (p >= 25 && p <= 150)
            pct = p;
    }
    g_scale = (int16_t)(0x1000 * pct / 100);
}

/* Drop what prepare() built (BUGS #55), so the next prepare() builds the
 * image, animation map, texture plan and attack flag of the character the
 * option names now. Activation only: no clone is in a level then. */
static void unprepare(void)
{
    free(g_image);
    g_image = NULL;
    g_image_size = 0;
    donor_free(&g_donor);
    memset(&g_layout, 0, sizeof g_layout);
    memset(&g_plan, 0, sizeof g_plan);
    g_tex_planned = 0;
    g_map_count = 0;
    g_default_count = 0;
    g_daffy_attack = 0;
    g_prepared = 0;
}

static const BbtActorUpdateHook kInputHook;
static void activate(void)
{
    g_active = 1;
    bbt_actor_update_hook(BBT_ACTOR_HOOK_INPUT, &kInputHook);
    read_options();
    /* fixed address whatever was played before (BUGS #56, mod_blocks.h) */
    g_block = bbt_mod_block(BBT_BLOCK_P3_ACTOR);
    if (g_prepared_for != g_daffy_char) {
        if (g_prepared_for >= 0) {
            unprepare();
            count("character_changed", 1);
        }
        g_prepared_for = g_daffy_char;
    }
    prepare();
}
BBT_BLOCK_SIZE_IS(p3_actor, BLK_SIZE, BBT_BLOCK_P3_ACTOR_BYTES);

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

static int place_next_to_partner(uint32_t a);

/* Spawn the clone of Taz. Called at the start of an actor pass, before the
 * game builds its update list. `kept`: Taz has been played since the load
 * (a join during a level), so the clone starts from the untouched copy of
 * him taken at the level's first actor pass (BLK_SOURCE) and is put next to
 * a hero in play; his resource list is the same all level. */
static void spawn(CPUState* cpu, uint32_t taz, int kept)
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
                                           g_defaults, g_default_count,
                                           g_daffy_char ? NULL : kTazIdleIds,
                                           g_daffy_char ? 0 : TAZ_IDLE_COUNT,
                                           nodes, entries, records, &st);
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
    if (kept) {
        read_bytes(g_block + BLK_SOURCE, act, ACTOR_SIZE);
    } else {
        read_bytes(taz, act, ACTOR_SIZE);
        write_bytes(g_block + BLK_SOURCE, act, ACTOR_SIZE);
    }
    st_set(ST_KEPT_TAZ, 0);
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
    if (kept) {
        (void)place_next_to_partner(a);    /* where the heroes are now, not where the level began */
        count("spawned_in_level", 1);
    }

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

/* The third player's death follows the game's rule for Bugs and Taz (user's
 * choice, 2026-11-07): from the end of its death it is out (not listed by
 * the actor pass: no update, no draw), also in the next levels, until the
 * team respawns, or a package brings an out hero back (package_wrap). The
 * team respawns when Bugs and Taz are both down: the second death's step
 * (FUN_80022510) or a level exit with both down (FUN_8002A384) takes a life
 * and clears both down bits of 0x800100A2 at once. The third player then
 * comes back next to Taz with full health. */
static void take_out(uint32_t a)
{
    psx_mod_write_half(a + A_CLIP, (uint16_t)(psx_mod_read_half(a + A_CLIP) | CLIP_PASVISIBLE));
}

/* Puts a SPAWN_GAP from ref's position (+0xFC), on the side away from
 * `away` (a straight +x step when away is not an actor or at ref). */
static void place_beside(uint32_t a, uint32_t ref, uint32_t away)
{
    const int tx = (int16_t)psx_mod_read_half(ref + A_POS), ty = (int16_t)psx_mod_read_half(ref + A_POS + 2);
    const int tz = (int16_t)psx_mod_read_half(ref + A_POS + 4);
    int dx = SPAWN_GAP, dz = 0;
    if (in_main_ram(away)) {
        const int vx = tx - (int16_t)psx_mod_read_half(away + A_POS);
        const int vz = tz - (int16_t)psx_mod_read_half(away + A_POS + 4);
        const int ax = vx < 0 ? -vx : vx, az = vz < 0 ? -vz : vz;
        const int len = ax > az ? ax : az;
        if (len > 0) {
            dx = vx * SPAWN_GAP / len;
            dz = vz * SPAWN_GAP / len;
        }
    }
    const int nx = tx + dx, nz = tz + dz;
    psx_mod_write_half(a + A_POS, (uint16_t)nx);
    psx_mod_write_half(a + A_POS + 2, (uint16_t)ty);
    psx_mod_write_half(a + A_POS + 4, (uint16_t)nz);
    for (uint32_t m = 0; m < 2; ++m) {
        const uint32_t t = a + (m ? A_MATRIX2_T : A_MATRIX_T);
        psx_mod_write_word(t, (uint32_t)nx);
        psx_mod_write_word(t + 4, (uint32_t)ty);
        psx_mod_write_word(t + 8, (uint32_t)nz);
    }
}

static void place_next_to_taz(uint32_t a)
{
    const uint32_t taz = psx_mod_read_word(ADDR_TAZ_HERO), bugs = psx_mod_read_word(ADDR_BUGS_HERO);
    if (!in_main_ram(taz) || taz == a)
        return;
    place_beside(a, taz, bugs);
}

/* Next to Taz if he is in play, else next to Bugs if he is; 0 when neither
 * is (an out hero keeps DEATH and stands where he fell). */
static int place_next_to_partner(uint32_t a)
{
    const uint32_t taz = psx_mod_read_word(ADDR_TAZ_HERO), bugs = psx_mod_read_word(ADDR_BUGS_HERO);
    const int taz_in = in_main_ram(taz) && taz != a && !(psx_mod_read_word(taz + A_ACTION) & DEATH_FLAG);
    const int bugs_in = in_main_ram(bugs) && !(psx_mod_read_word(bugs + A_ACTION) & DEATH_FLAG);
    if (taz_in)
        place_beside(a, taz, bugs);
    else if (bugs_in)
        place_beside(a, bugs, taz);
    return taz_in || bugs_in;
}

/* In play again with full health (where it stands is the caller's). */
static void revive(uint32_t a)
{
    psx_mod_write_half(a + A_HEALTH, FULL_HEALTH);
    psx_mod_write_word(a + A_ACTION, psx_mod_read_word(a + A_ACTION) & ~DEATH_FLAG);
    psx_mod_write_half(a + A_CLIP, (uint16_t)(psx_mod_read_half(a + A_CLIP) & ~CLIP_PASVISIBLE));
    psx_mod_write_word(a + A_SPEED, 0);
    psx_mod_write_half(a + A_SPEED + 4u, 0);
}

static void third_player_back(uint32_t a)
{
    revive(a);
    place_next_to_taz(a);
}

static void watch_team(uint32_t a)
{
    if (psx_mod_read_word(ADDR_LEVEL_ID) == 0) {
        /* the title: a new game starts with nobody out (the game clears
         * its own down bits on that path, FUN_800110C8) */
        st_set(ST_OUT, 0);
        st_set(ST_TEAM_DOWN, 0);
        st_set(ST_OUT_FRAMES, 0);
        return;
    }
    const uint8_t down = psx_mod_read_byte(ADDR_HERO_DOWN);
    if ((down & 0x11u) && (down & 0x22u)) {
        st_set(ST_TEAM_DOWN, 1);
    } else if (st_get(ST_TEAM_DOWN) && !(down & 0x33u)) {
        st_set(ST_TEAM_DOWN, 0);
        const int dying = a && (psx_mod_read_word(a + A_ACTION) & DEATH_FLAG);
        if (st_get(ST_OUT) || dying) {
            st_set(ST_OUT, 0);
            if (a)
                third_player_back(a);
            count("back", 1);
        }
    }
    /* Option "comeback" = "auto" (the default since 2026-11-16, user: "Yes,
     * on his own"): the third player does not wait for the team, it comes
     * back COMEBACK_FRAMES game frames after it went out, next to a hero in
     * play. Counted once per frame: the actor pass runs once per view, with
     * the view index 0x8008DEA0 (FUN_80011AF0, NOTES.md "Phase 4 - third
     * actor"), so only view 0's pass counts. Not while both heroes are down
     * (the team respawn brings it back). Guest state only. */
    if (!g_comeback_team && a && st_get(ST_OUT) && !st_get(ST_TEAM_DOWN) &&
        psx_mod_read_byte(ADDR_VIEW_INDEX) == 0) {
        const uint32_t n = st_get(ST_OUT_FRAMES) + 1u;
        st_set(ST_OUT_FRAMES, n);
        if (n >= COMEBACK_FRAMES && place_next_to_partner(a)) {
            st_set(ST_OUT, 0);
            revive(a);
            count("back_timer", 1);
        }
    }
}

/* The package (Ghidra, NOTES.md "The third player's death"): script action
 * 161 of the action table 0x800815EC, func_8004E908(obj). If player 2 (the
 * out hero after the swap) has DEATH, he is put at obj's position with
 * health 6 and nobody out (0x800100A2 &= 0xFC); no life is lost. It runs on
 * an event, not every frame: no call in a minute of random play in Granwich,
 * with or without a hero out (fntrace, 2026-11-16). Since the third player
 * came back only with the team respawn, a package that brought Bugs or Taz
 * back left Daffy out (user's Windows test 2026-11-16, BUGS.md #62). Now
 * whenever the game runs it while the third player is out (or still in its
 * death), it comes back too, beside obj, away from player 1. Guest state
 * only, the same on every peer. */
#define ADDR_PACKAGE     0x8004E908u
/* lui a1,0x8009 / lw a1,-0x22F8(a1) / nop / lw v0,0x20(a1) */
static const uint32_t kPackageGuard[4] = {0x3C058009u, 0x8CA5DD08u, 0x00000000u, 0x8CA20020u};

static int package_wrap(CPUState* cpu)
{
    const uint32_t obj = cpu->gpr[GPR_A0];
    const uint32_t a = bbt_p3_actor();
    if (!a)
        return 0;
    func_override_call_original(cpu);
    count("package", 1);
    const int dying = (psx_mod_read_word(a + A_ACTION) & DEATH_FLAG) != 0;
    if (!in_main_ram(obj) || (!st_get(ST_OUT) && !dying))
        return 1;
    st_set(ST_OUT, 0);
    revive(a);
    place_beside(a, obj, psx_mod_read_word(ADDR_P1));
    count("back_package", 1);
    return 1;
}

/* FUN_80029424(code, actor, ...): the actor a script means by a reference
 * code (Ghidra). Codes 8/9 and 0xE/0xF are players 1/2, 2/3 Bugs/Taz (unless
 * out), 4/6 the nearer and 5/7 the farther of the two heroes from the caller
 * (3D distance FUN_80029304 for 4/5, horizontal FUN_80029398 for 6/7), 0xA a
 * remembered hero; while a hero is out they give player 1. Enemies pick
 * their target with codes 4/6 (measured in TRCA01: the three model-635
 * enemies, ~1,200 calls of code 4 and 600 of code 6 in 1,200 frames), so
 * they never went for the third actor: one placed 500 units from an enemy
 * was ignored, Taz in the same place was chased and hit (NOTES.md "Pick-ups
 * and enemies for the third player"). Here the third actor takes part in
 * codes 4-7 while it is in play: the original answers, then the clone wins
 * if it is nearer (4, 6) / farther (5, 7) by the same distance. Guest state
 * only, the same on every peer. */
#define ADDR_PLAYER_REF  0x80029424u
/* addiu sp,sp,-0x48 / sw s2,0x38(sp) / move s2,a0 / lui v0,0xE000 */
static const uint32_t kPlayerRefGuard[4] = {0x27BDFFB8u, 0xAFB20038u, 0x00809021u, 0x3C02E000u};

/* Squared distance between two actors' positions (+0xFC, s16 x y z); the
 * game compares square roots of the same sums, integer only here. */
static int64_t ref_distance2(uint32_t a, uint32_t b, int horizontal)
{
    const int64_t dx = (int16_t)psx_mod_read_half(a + A_POS) - (int16_t)psx_mod_read_half(b + A_POS);
    const int64_t dy = (int16_t)psx_mod_read_half(a + A_POS + 2) - (int16_t)psx_mod_read_half(b + A_POS + 2);
    const int64_t dz = (int16_t)psx_mod_read_half(a + A_POS + 4) - (int16_t)psx_mod_read_half(b + A_POS + 4);
    return dx * dx + dz * dz + (horizontal ? 0 : dy * dy);
}

static int player_ref_wrap(CPUState* cpu)
{
    const uint32_t code = cpu->gpr[4];
    if (code < 4u || code > 7u || !g_active || !g_block || g_prepared != 1 || st_get(ST_MAGIC) != STATE_MAGIC)
        return 0;
    /* the clone the actor pass keeps up to date (it drops a stale one) */
    const uint32_t third = st_get(ST_ACTOR);
    if (!in_main_ram(third) || st_get(ST_OUT) || st_get(ST_REF_OFF) ||
        (psx_mod_read_word(third + A_ACTION) & DEATH_FLAG))
        return 0;
    const uint32_t caller = cpu->gpr[5];
    func_override_call_original(cpu);
    const uint32_t chosen = cpu->gpr[2];
    if (!in_main_ram(caller) || !in_main_ram(chosen) || chosen == third)
        return 1;
    const int horizontal = code >= 6u;
    const int64_t d_third = ref_distance2(caller, third, horizontal);
    const int64_t d_chosen = ref_distance2(caller, chosen, horizontal);
    if ((code & 1u) == 0 ? d_third < d_chosen : d_third > d_chosen) {
        cpu->gpr[2] = third;
        count("ref_third", 1);
    }
    return 1;
}

uint8_t bbt_p3_hit_flash(void)
{
    return bbt_p3_actor() ? (uint8_t)st_get(ST_HIT) : 0u;
}

int bbt_p3_out(void)
{
    return bbt_p3_actor() && st_get(ST_OUT) != 0;
}

/* Is there a third player? In netplay: seat 2 is occupied (the session's
 * seats are the same on every peer). Offline: seat 2 has a controller
 * (Player 3 is not "none" in Settings -> Input; a build or config with two
 * seats has no seat 2) AND has joined: pressed a button at least once in
 * this run (ST_P3_JOINED). A device setting alone is not a player: a seat
 * set to "gamepad" counts as connected even with no pad plugged in, so a
 * player who once set Player 3 up got an idle Daffy in every 2-player game
 * (pointed out by the Fighting Force session, which drops players in with
 * X). A join during a level brings the clone at once (actor_pass_wrap, the
 * kept copy of Taz); in a match the seats are fixed when it starts. */
static int third_player_present(void)
{
    if (g_appear_always)
        return 1;
    if (psx_netplay_active()) {
        uint32_t seats = psx_netplay_peer_seats();
        const int me = psx_netplay_local_slot();
        if (me >= 0 && me < 32)
            seats |= 1u << me;
        return (seats & (1u << P3_SEAT_INDEX)) != 0;
    }
    return sio_get_pad_connected(P3_SEAT_INDEX) && st_get(ST_P3_JOINED);
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
        for (uint32_t f = 0; f <= ST_LAST; f += 4)
            st_set(f, 0);
        st_set(ST_MAGIC, STATE_MAGIC);
    }
    /* one PC: the third player joins with any button on seat 2 (its input
     * record, +0x02 held buttons, refreshed every pad poll) */
    if (!psx_netplay_active() && !st_get(ST_P3_JOINED)) {
        const uint32_t rec = bbt_p3_record();
        if (rec && psx_mod_read_half(rec + 0x02u) != 0) {
            st_set(ST_P3_JOINED, 1);
            count("p3_joined", 1);
        }
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
    /* Not inside a netplay own-view draw (bbt.own-view: a sandboxed 1-view
     * rerun of a frame): a spawn uploads textures through libgpu, whose
     * queue needs interrupts a pass never takes (the rerun hit the watchdog
     * once per peer, on the frame the clone appeared). That one image goes
     * without the clone; the shared game spawns it as usual. */
    if (!a && (psx_mod_read_byte(ADDR_MODE_FLAGS) & MODE_TWO_PLAYER) && !psx_mod_local_view_scope()) {
        const uint32_t taz = placed_taz();
        const uint32_t level = psx_mod_read_word(ADDR_LEVEL_ID);
        const int pristine = source_pristine(taz);
        if (pristine && !third_player_present()) {
            /* Nobody at seat 3 yet: keep Taz as he is now, so a player who
             * joins during this level gets a clone that starts as a freshly
             * loaded actor does (the same on every netplay peer; only one PC
             * uses it). */
            uint8_t act[ACTOR_SIZE];
            read_bytes(taz, act, ACTOR_SIZE);
            write_bytes(g_block + BLK_SOURCE, act, ACTOR_SIZE);
            st_set(ST_KEPT_TAZ, taz);
            st_set(ST_KEPT_LEVEL, level);
            count("no_third_player", 1);
        } else if (pristine) {
            spawn(cpu, taz, 0);
        } else if (taz && taz == st_get(ST_KEPT_TAZ) && level == st_get(ST_KEPT_LEVEL) &&
                   !psx_netplay_active() && third_player_present()) {
            /* One PC: seat 3 pressed a button during the level. In a match
             * the seats are fixed when it starts. */
            spawn(cpu, taz, 1);
        }
        a = st_get(ST_ACTOR);
        if (a && st_get(ST_OUT))
            take_out(a);                       /* still out from an earlier level */
    }
    if (!psx_mod_local_view_scope())
        watch_team(a);
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
    /* tests only (debug command "package"): the game's own package action
     * for the actor named, after the pass has registered players 1 and 2,
     * as a script in the actors' updates runs it */
    const uint32_t test_obj = st_get(ST_TEST_PACKAGE);
    if (test_obj && !psx_mod_local_view_scope()) {
        st_set(ST_TEST_PACKAGE, 0);
        (void)psx_mod_call_guest(cpu, ADDR_PACKAGE, cpu->gpr[31], test_obj, 0, 0, 0);
    }
    return 1;
}

/* Actor_Update(actor) through the shared hub (actor_update_hub.h): the clone
 * reads the third player's input. Player 2's slot is kept in the guest block
 * for the length of the call. The clone has Taz's identity, so its update
 * writes Taz's identity-keyed globals (its death sets "Taz died", its health
 * becomes Taz's kept health, a hit flashes Taz's HUD panel): they are put
 * back after it (save: the four bytes). A dead clone gets no input. */
/* Actor_Update's return address, from the clone's input_pre to its
 * input_post (one call; the guest call in daffy_attack_sound returns there). */
static uint32_t g_update_ra;
static void daffy_attack_sound(CPUState* cpu, uint32_t a);

static int input_pre(CPUState* cpu, uint32_t a, uint32_t* save)
{
    if (!g_block || g_prepared != 1 || a == 0 || a != st_get(ST_ACTOR))
        return 0;
    g_update_ra = cpu->gpr[31];
    *save = (uint32_t)psx_mod_read_byte(ADDR_HERO_DOWN) |
            (uint32_t)psx_mod_read_byte(ADDR_TAZ_HEALTH) << 8 |
            (uint32_t)psx_mod_read_byte(ADDR_TAZ_HIT) << 16 |
            (uint32_t)psx_mod_read_byte(ADDR_GAUGE) << 24;
    /* The clone's own hit counter, in Taz's byte for its update. A hero's
     * hit sets the byte to 0x80 (identity 3) and the invincibility +0x28
     * 0x200; the frame start (FUN_80011AF0) counts a running one up in bits
     * 2-5, and Actor_Update clears the invincibility when they are full
     * (0x3C). Putting Taz's byte back after the clone (the isolation above)
     * kept the clone's count from ever running: one hit made it invincible
     * for good. Counted here, once per update, as the frame start would. */
    uint8_t hit = (uint8_t)st_get(ST_HIT);
    if ((hit & 0xC0u) && (hit & 0x3Cu) < 0x3Cu)
        hit = (uint8_t)(hit + 1u);
    psx_mod_write_byte(ADDR_TAZ_HIT, hit);
    const uint32_t rec = bbt_p3_record();
    const int dead = (psx_mod_read_word(a + A_ACTION) & DEATH_FLAG) != 0;
    uint8_t p2[INPUT_FIELDS], p3[INPUT_FIELDS];
    read_bytes(ADDR_INPUT_SLOT1, p2, INPUT_FIELDS);
    write_bytes(g_block + BLK_STATE + ST_SLOT, p2, INPUT_FIELDS);
    if (rec && !dead)
        read_bytes(rec, p3, INPUT_FIELDS);
    else
        memset(p3, 0, sizeof p3);
    st_set(ST_NO_INPUT, dead ? 1u : 0u);
    write_bytes(ADDR_INPUT_SLOT1, p3, INPUT_FIELDS);
    return 1;
}

static void input_post(CPUState* cpu, uint32_t a, uint32_t save)
{
    const uint32_t rec = bbt_p3_record();
    uint8_t p2[INPUT_FIELDS], p3[INPUT_FIELDS];
    /* What the update consumed stays consumed for the third player. */
    if (rec && !st_get(ST_NO_INPUT)) {
        read_bytes(ADDR_INPUT_SLOT1, p3, INPUT_FIELDS);
        write_bytes(rec, p3, INPUT_FIELDS);
    }
    st_set(ST_NO_INPUT, 0);
    read_bytes(g_block + BLK_STATE + ST_SLOT, p2, INPUT_FIELDS);
    write_bytes(ADDR_INPUT_SLOT1, p2, INPUT_FIELDS);
    if (psx_mod_read_byte(ADDR_HERO_DOWN) != (uint8_t)save)
        count("taz_down_kept", 1);
    st_set(ST_HIT, psx_mod_read_byte(ADDR_TAZ_HIT));
    psx_mod_write_byte(ADDR_HERO_DOWN, (uint8_t)save);
    psx_mod_write_byte(ADDR_TAZ_HEALTH, (uint8_t)(save >> 8));
    psx_mod_write_byte(ADDR_TAZ_HIT, (uint8_t)(save >> 16));
    psx_mod_write_byte(ADDR_GAUGE, (uint8_t)(save >> 24));
    daffy_attack_sound(cpu, a);
    count("updates", 1);
}

static const BbtActorUpdateHook kInputHook = {input_pre, input_post};

uint32_t bbt_p3_actor(void)
{
    if (!g_active || !g_block || g_prepared != 1 || st_get(ST_MAGIC) != STATE_MAGIC)
        return 0;
    const uint32_t a = st_get(ST_ACTOR);
    return in_main_ram(a) && linked(a) ? a : 0;
}

/* FUN_80022510(hero, ...): the end of a hero's death (behaviour machine
 * FUN_80020B80, while DEATH is set). With no hero out yet it marks this one
 * out (0x800100A2 bit 0 for Bugs, bit 1 for anyone else); with one out
 * already a life is lost and both heroes respawn. For the clone that marked
 * Taz out (taken out of the update list while alive) and, called again every
 * death loop, took a life each time (measured: lives 3 -> 1 in 8 s; BUGS
 * #23). The clone's death is its own: none of that runs for it; the third
 * player is out until the comeback time, the team respawn (watch_team) or
 * a package (package_wrap). */
/* addiu sp,sp,-0x20 / sw s0,0x18(sp) / move s0,a1 / lui a1,0x8001 */
static const uint32_t kDeathDoneGuard[4] = {0x27BDFFE0u, 0xAFB00018u, 0x00A08021u, 0x3C058001u};

static int death_done_wrap(CPUState* cpu)
{
    const uint32_t a = cpu->gpr[4];
    if (a == 0 || a != bbt_p3_actor())
        return 0;
    psx_mod_write_word(a + A_SPEED, 0);            /* as the game's step does first */
    psx_mod_write_half(a + A_SPEED + 4u, 0);
    if (!st_get(ST_OUT)) {
        st_set(ST_OUT, 1);
        st_set(ST_OUT_FRAMES, 0);
        take_out(a);
        count("out", 1);
    }
    count("death_steps", 1);
    return 1;
}

/* Daffy's sounds. FUN_800640C8(actor, id, flags, byte) starts (flags bit
 * 0) or stops (bit 1) a sound by id: the table at 0x80092AB4 (12-byte
 * entries, the count at 0x8008DBB8), entry +2 = tone << 8 | VAB program.
 * The clone runs Taz's code, so it plays Taz's bank (program 4: ids 61-82
 * in every level looked at, Granwich, 8, 27, 55, 57). Bugs' bank has the
 * same layout for his moves (steps tones 0 / 1, jump 2, land 3), so each
 * of Taz's move sounds has a Bugs counterpart (measured per action, NOTES.md
 * "Daffy's spin and sounds"). "bugs": those are played as Bugs', the rest of
 * Taz's bank is not played; "quiet": none of Taz's bank; "taz": all of it.
 * Other banks (impacts, pick-ups) play as they are. Guest state only, the
 * same on every peer. */
#define ADDR_SOUND_START 0x800640C8u
#define ADDR_SOUND_TABLE 0x80092AB4u
#define ADDR_SOUND_COUNT 0x8008DBB8u
#define SOUND_ENTRY      12u
#define TAZ_PROGRAM      4u
/* addiu sp,sp,-0x30 / sw s5,0x24(sp) / move s5,a0 / lui v0,0x8009 */
static const uint32_t kSoundStartGuard[4] = {0x27BDFFD0u, 0xAFB50024u, 0x0080A821u, 0x3C028009u};
static const uint16_t kTazToBugsSound[][2] = {
    {71, 10}, {72, 11},   /* steps */
    {74, 19}, {75, 23},   /* jump, land */
    {63, 2},              /* stop after running */
    {82, 22},             /* the spin -> Bugs' attack */
};

static int taz_bank(uint16_t id)
{
    const int n = (int16_t)psx_mod_read_half(ADDR_SOUND_COUNT);
    for (int i = 0; i < n && i < 512; ++i) {
        const uint32_t e = ADDR_SOUND_TABLE + SOUND_ENTRY * (uint32_t)i;
        if (psx_mod_read_half(e) == id)
            return (psx_mod_read_half(e + 2u) & 0xFFu) == TAZ_PROGRAM;
    }
    return 0;
}

static int sound_start_wrap(CPUState* cpu)
{
    if (!g_daffy_char || g_sounds == SOUNDS_TAZ || cpu->gpr[4] == 0 || cpu->gpr[4] != bbt_p3_actor())
        return 0;
    const uint16_t id = (uint16_t)cpu->gpr[5];
    if (!taz_bank(id))
        return 0;
    if (g_sounds == SOUNDS_BUGS)
        for (uint32_t k = 0; k < sizeof kTazToBugsSound / sizeof kTazToBugsSound[0]; ++k)
            if (kTazToBugsSound[k][0] == id) {
                const uint32_t actor = cpu->gpr[4], flags = cpu->gpr[6];
                cpu->gpr[5] = kTazToBugsSound[k][1];
                func_override_call_original(cpu);
                count("sound_as_bugs", 1);
                /* diagnostics: did a start get a sound slot (0x80093274,
                 * 24 x 0x1C: +0 flags, +4 actor, +8 entry)? */
                if (flags & 1u) {
                    int slot = 0;
                    for (uint32_t s = 0; s < 24 && !slot; ++s) {
                        const uint32_t base = 0x80093274u + 0x1Cu * s;
                        slot = (psx_mod_read_half(base) & 0x8000u) && psx_mod_read_word(base + 4u) == actor &&
                               psx_mod_read_half(psx_mod_read_word(base + 8u)) == kTazToBugsSound[k][1];
                    }
                    if (slot)
                        count("sound_slot", 1);
                    else
                        count("sound_no_slot", 1);
                }
                return 1;
            }
    count("sound_muted", 1);
    return 1;                 /* not played (the function returns nothing) */
}

/* Daffy's body during the spin. The draw pass FUN_8005EEDC skips an actor
 * whose animation id +0x14C is 0x004F (the spin) or 0x00F6 (Ghidra,
 * 0x8005F020): Taz is drawn as his tornado only. Daffy's spin is Bugs'
 * attack (no tornado is attached), so for the pass his id reads as spin
 * start 0x0072 and is put back after it. Nothing the pass calls reads
 * +0x14C (its readers: Actor_Update, func_8001A088 / 8001EB14 / 8001F7E4,
 * FUN_8005154C under Camera_Update, and this check). */
#define ADDR_DRAW_PASS  0x8005EEDCu
#define A_ANIM_ID       0x14Cu
#define ANIM_SPIN       0x004Fu
#define ANIM_SPIN_START 0x0072u
/* addiu sp,sp,-0x30 / sw s2,0x18(sp) / lui s2,0x1F80 / ori s2,s2,0x3C */
static const uint32_t kDrawPassGuard[4] = {0x27BDFFD0u, 0xAFB20018u, 0x3C121F80u, 0x3652003Cu};

static int draw_pass_wrap(CPUState* cpu)
{
    const uint32_t a = (g_daffy_char && g_daffy_attack) ? bbt_p3_actor() : 0u;
    if (!a || psx_mod_read_half(a + A_ANIM_ID) != ANIM_SPIN)
        return 0;
    psx_mod_write_half(a + A_ANIM_ID, ANIM_SPIN_START);
    func_override_call_original(cpu);
    if (psx_mod_read_half(a + A_ANIM_ID) == ANIM_SPIN_START)
        psx_mod_write_half(a + A_ANIM_ID, ANIM_SPIN);
    count("spin_drawn", 1);
    return 1;
}

/* Taz's spin sound (82) is started by his tornado, an actor of its own:
 * Daffy's spin (no tornado) made only the stop call (measured). His attack
 * starts its sound here, on the update that enters spin start 0x0072:
 * Bugs' attack (22) with "bugs", Taz's spin (82) with "taz", nothing with
 * "quiet". The flags and last argument are those of the game's own sound
 * events (FUN_8002D6D4: 1 = start, the byte at 0x8008DD28). */
#define SOUND_BUGS_ATTACK 22u
#define SOUND_TAZ_SPIN    82u
#define ADDR_SOUND_ARG    0x8008DD28u

static void daffy_attack_sound(CPUState* cpu, uint32_t a)
{
    const uint16_t anim = psx_mod_read_half(a + A_ANIM_ID);
    const uint32_t prev = st_get(ST_PREV_ANIM);
    st_set(ST_PREV_ANIM, anim);
    if (!g_daffy_char || !g_daffy_attack || g_sounds == SOUNDS_QUIET || anim != ANIM_SPIN_START ||
        prev == ANIM_SPIN_START || !g_update_ra)
        return;
    const uint32_t id = g_sounds == SOUNDS_TAZ ? SOUND_TAZ_SPIN : SOUND_BUGS_ATTACK;
    (void)psx_mod_call_guest(cpu, ADDR_SOUND_START, g_update_ra, a, id, 1u,
                             psx_mod_read_byte(ADDR_SOUND_ARG));
    count("attack_sound", 1);
}

static void session_reset(void)
{
    g_active = 0;
    bbt_actor_update_hook(BBT_ACTOR_HOOK_INPUT, NULL);
    bbt_mod_blocks_session_reset();
}

/* Debug command (debug-server thread): host state that is immutable once
 * prepared, and the guest block's address (tests read the rest with
 * read_ram). */
static int p3actor_command(const char* args, char* reply, int cap)
{
    if (args && strncmp(args, "ref-third ", 10) == 0 && g_block) {
        st_set(ST_REF_OFF, strcmp(args + 10, "off") == 0 ? 1u : 0u);
        snprintf(reply, (size_t)cap, "{\"ok\":true,\"ref_third\":%s}", st_get(ST_REF_OFF) ? "false" : "true");
        return 1;
    }
    if (args && strncmp(args, "package ", 8) == 0 && g_block) {
        const uint32_t obj = (uint32_t)strtoul(args + 8, NULL, 16);
        if (!in_main_ram(obj)) {
            snprintf(reply, (size_t)cap, "{\"ok\":false,\"err\":\"not an actor address\"}");
            return 0;
        }
        st_set(ST_TEST_PACKAGE, obj);
        snprintf(reply, (size_t)cap, "{\"ok\":true,\"package\":\"0x%08X\"}", obj);
        return 1;
    }
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
        PLUGIN_ACTOR ":death-done", ADDR_DEATH_DONE,
        death_done_wrap, kDeathDoneGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(
        PLUGIN_ACTOR ":package", ADDR_PACKAGE,
        package_wrap, kPackageGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(
        PLUGIN_ACTOR ":player-ref", ADDR_PLAYER_REF,
        player_ref_wrap, kPlayerRefGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(
        PLUGIN_ACTOR ":daffy-sounds", ADDR_SOUND_START,
        sound_start_wrap, kSoundStartGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(
        PLUGIN_ACTOR ":daffy-attack-drawn", ADDR_DRAW_PASS,
        draw_pass_wrap, kDrawPassGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_session_reset_plugin(PLUGIN_ACTOR, session_reset);
    (void)psx_mod_register_command("bbt.p3actor", p3actor_command);
}
