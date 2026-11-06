/*
 * Third character mod — Bugs Bunny & Taz: Time Busters (SLUS-01144).
 *
 * Phase 3 of the project: in 2-player mode, Taz is replaced by another
 * in-game character. This file is a trusted static plugin (see
 * psxrecomp/psxmod.md "When you need C or C++"); the package manifest in
 * mods/preloaded/packages/bbt.third-character/ only selects it by id and
 * ships no game data. Anything taken from the game is read from the
 * player's own disc at runtime.
 *
 * Milestone M1 ("swap-test" feature): proves the swap path end to end with
 * data that is already loaded. After every successful level load, each Taz
 * actor gets Bugs' resource list (model + animations), so Taz is drawn with
 * Bugs' model while keeping Taz's behaviour script and controller.
 *
 * Addresses come from the Phase 2 mapping (NOTES.md):
 *   Level_LoadParse  0x80068174  level stream parser, wrapped here
 *   actor lists      0x8008DD44 (definition actors), 0x8008DDCC (placed)
 *   actor +0x00 next, +0x08 type word, +0x98 resource node list
 *   (+0x170 / +0x174 hold the same list head)
 *   2-player flag    0x800100A4 bit 0x10
 * The resource list is only read when the actor becomes active, after
 * loading, so changing it right after the load takes effect.
 *
 * M1 keeps no host state (everything lives in guest RAM).
 *
 * Milestone M2 ("trca01" feature): Taz is drawn with the model and the
 * animations of the Transylvania enemy (LEVEL5/TRCA01.BZZ, actor type
 * 0x01008381, model id 635). The donor data is read from the player's disc
 * and packed by donor.c; the game's own loader then builds Taz's resources
 * from it:
 *   - activation reserves DONOR_RESERVE bytes of mod guest memory
 *     (Expansion 1; part of savestates) and extracts the donor;
 *   - Bouze_Decompress (0x8006B67C) is wrapped. A level load calls it four
 *     times (write trace of DAT_8008DC18 on a Granwich load): record
 *     stream (return address 0x800684F0), chunk 1 at the data part
 *     (0x80068514), a variant chunk (0x80068E08; one of seven, picked by
 *     the byte at 0x80010093), and, after the
 *     0x2B marker has laid out the pools, the type-4 resource chunk: the
 *     loader stores its destination at 0x80068C20 and jumps back into the
 *     jal at 0x8006850C, so it also returns to 0x80068514 and the parser
 *     continues (0x8006AB90). Only that last call is acted on; it is told
 *     apart from the data-part call, whose destination is $s2 + $s3
 *     (0x800684F8), by its registers at entry. Its $a1 (= DAT_8008DC18) is the base that tags
 *     0x24 / 0x25 offsets are added to (no range check; verified in the
 *     Level_LoadParse decompile and against TRCA01 RAM), and the 0x2B
 *     marker precedes every resource record in all 63 level files;
 *   - in 2-player mode the donor image is copied into the reserved memory
 *     and the model / animation payloads of Taz's still unparsed resource
 *     records (stream at DAT_8008DC50, DAT_8008DCC4 bytes reserved) are
 *     pointed at it. Lengths do not change and no pool grows. The loader
 *     relocates the model copy itself (FUN_8005BB84), which is why the
 *     image is copied fresh on every load.
 * Host state: the donor image is a pure function of the disc and never
 * changes after it is loaded; the guest copy is rewritten on every load.
 * Status is visible through the debug server: {"cmd":"mod_counters"}.
 */
#include <stdint.h>
#include <stdlib.h>

#include "cpu_state.h"
#include "donor.h"
#include "func_override.h"
#include "mod_plugins.h"

#define ADDR_LEVEL_LOAD_PARSE 0x80068174u
#define ADDR_ACTOR_LIST_DEFS  0x8008DD44u
#define ADDR_ACTOR_LIST_PLACED 0x8008DDCCu
#define ADDR_MODE_FLAGS       0x800100A4u
#define MODE_FLAG_TWO_PLAYER  0x10u

#define ACTOR_OFF_NEXT   0x000u
#define ACTOR_OFF_TYPE   0x008u
#define ACTOR_OFF_RES    0x098u
#define ACTOR_OFF_RES_B  0x170u
#define ACTOR_OFF_RES_C  0x174u

#define TYPE_BUGS 0x05000181u
#define TYPE_TAZ  0x05000190u

#define MAX_ACTORS_PER_LIST 512

/* First four instructions of Level_LoadParse in SLUS_011.44
 * (addiu sp,sp,-0x170 / sw s7,0x164(sp) / move s7,a1 / sw s0,0x148(sp)).
 * On any other code the override declines and the game runs unchanged. */
static const uint32_t kLevelLoadParseGuard[4] = {
    0x27BDFE90u, 0xAFB70164u, 0x00A0B821u, 0xAFB00148u,
};

static int in_main_ram(uint32_t p)
{
    return p >= 0x80000000u && p < 0x80200000u;
}

static uint32_t find_actor_of_type(uint32_t list_head, uint32_t type)
{
    uint32_t p = psx_mod_read_word(list_head);
    for (int n = 0; in_main_ram(p) && n < MAX_ACTORS_PER_LIST; ++n) {
        if (psx_mod_read_word(p + ACTOR_OFF_TYPE) == type)
            return p;
        p = psx_mod_read_word(p + ACTOR_OFF_NEXT);
    }
    return 0;
}

/* Give every Taz actor in one list the resource list of the Bugs actor in
 * the same list. Returns the number of actors changed. */
static int swap_taz_resources_in_list(uint32_t list_head)
{
    const uint32_t bugs = find_actor_of_type(list_head, TYPE_BUGS);
    if (!bugs)
        return 0;
    const uint32_t res   = psx_mod_read_word(bugs + ACTOR_OFF_RES);
    const uint32_t res_b = psx_mod_read_word(bugs + ACTOR_OFF_RES_B);
    const uint32_t res_c = psx_mod_read_word(bugs + ACTOR_OFF_RES_C);
    if (!in_main_ram(res))
        return 0;

    int changed = 0;
    uint32_t p = psx_mod_read_word(list_head);
    for (int n = 0; in_main_ram(p) && n < MAX_ACTORS_PER_LIST; ++n) {
        if (psx_mod_read_word(p + ACTOR_OFF_TYPE) == TYPE_TAZ) {
            psx_mod_write_word(p + ACTOR_OFF_RES, res);
            psx_mod_write_word(p + ACTOR_OFF_RES_B, res_b);
            psx_mod_write_word(p + ACTOR_OFF_RES_C, res_c);
            ++changed;
        }
        p = psx_mod_read_word(p + ACTOR_OFF_NEXT);
    }
    return changed;
}

/* Wrap of Level_LoadParse(id, buffer): run the game's loader unchanged,
 * then adjust the freshly created actors. Level_LoadParse returns 0 on
 * success and 1 when the caller must retry the read. */
static int level_load_parse_wrap(CPUState* cpu)
{
    func_override_call_original(cpu);
    if (cpu->gpr[2] != 0)
        return 1;
    if ((psx_mod_read_byte(ADDR_MODE_FLAGS) & MODE_FLAG_TWO_PLAYER) == 0)
        return 1;
    (void)swap_taz_resources_in_list(ADDR_ACTOR_LIST_DEFS);
    (void)swap_taz_resources_in_list(ADDR_ACTOR_LIST_PLACED);
    return 1;
}

/* ---- M2: TRCA01 enemy as donor -------------------------------------- */

#define PLUGIN_TRCA01 "bbt.third-character.trca01"

#define ADDR_BOUZE_DECOMPRESS 0x8006B67Cu
/* Return addresses of Level_LoadParse's Bouze_Decompress calls. */
#define RA_STREAM_CALL        0x800684F0u   /* chunk 0: record stream */
#define RA_DATA_PART_CALL     0x80068514u   /* chunk 1, then the type-4 chunk */
#define RA_VARIANT_CALL       0x80068E08u   /* variant chunk (types 5-11) */
#define ADDR_STREAM_BASE      0x8008DC50u   /* record stream (0x800AC000) */
#define ADDR_STREAM_RESERVED  0x8008DCC4u   /* bytes reserved for the stream */
#define ADDR_BLOB_BASE        0x8008DC18u   /* destination of the current call */

#define DONOR_FILE        "LEVEL5/TRCA01.BZZ"
#define DONOR_ACTOR_TYPE  0x01008381u
/* Taz animation id -> donor animation id, by role. Animation ids are per
 * character (equal numbers mean nothing), so every entry comes from
 * watching the donor's animations played on Taz and Taz's own moves traced
 * per frame (filmstrips of both characters in Granwich, 2026-11-06;
 * NOTES.md "M2" and "M4"). DONOR_SUB_SPIN is the synthetic spin built from
 * the donor's skeleton (donor_add_spin), not a real animation id. */
#define DONOR_SUB_SPIN    0xF001u
#define DONOR_SPIN_FRAMES 32u
static const DonorAnimMap kTazToTrca01[] = {
    /* Set-up animation every character plays first: its keyframe commands
     * push the actor's draw node (+0x9C). Without it the model is never
     * drawn. */
    {0x0001u, 0x0001u},
    {0x0033u, 0x01B1u},   /* run (loops while the pad is held) -> running stride */
    {0x0034u, 0x0145u},   /* stop after running (skid round) -> upright turning */
    {0x0036u, 0x01C9u},   /* jump -> leap through the air and land */
    {0x0039u, 0x014Bu},   /* land -> crouch low */
    /* Spin (square; triangle in the air). Spin start must carry a root
     * command (0xA00) so the game attaches the tornado: 0x01C6 (arm raised)
     * has one on the attach node in 25 of 26 frames. The spin itself (and
     * 0x0097, which hides Taz's body the same way) keeps the tornado on the
     * ground; spin end -> crouched, shaking. */
    {0x0072u, 0x01C6u},
    {0x004Fu, DONOR_SUB_SPIN},
    {0x0097u, DONOR_SUB_SPIN},
    {0x0073u, 0x0151u},
};
/* Every other Taz animation: a standing pose with the arms down, the first
 * of these that is at least as long as Taz's own animation (so a play-once
 * animation can keep Taz's frame count without running past the donor
 * data). 0x001C (21 frames) is the stance the enemy plays most in its own
 * level; 0x013E (34) and 0x01C3 (71) are longer standing animations. The
 * earlier default 0x0018 is a 2-frame arms-out pose (the spread arms). */
static const uint16_t kDonorDefaults[] = {0x001Cu, 0x013Eu, 0x01C3u};
#define DONOR_DEFAULT_COUNT ((int)(sizeof(kDonorDefaults) / sizeof(kDonorDefaults[0])))

/* Animation timing fixes, kept in mod memory after the donor image (part of
 * savestates): word 0 = 0x80000000 (pending) | count, then two words per
 * fix: res_id | frames << 16, ticks. Applied after Level_LoadParse. */
#define FIX_TABLE_OFF   0x1F000u
#define MAX_FIXES       256
#define FIX_PENDING     0x80000000u
#define TAZ_MAP_COUNT ((int)(sizeof(kTazToTrca01) / sizeof(kTazToTrca01[0])))
/* Fixed reservation, so the savestate layout does not depend on the disc
 * read. The TRCA01 donor needs 93,920 bytes. */
#define DONOR_RESERVE     0x20000u
#define MAX_STREAM_BYTES  0x100000u
#define MAX_PATCHED       256

#define GPR_V0 2
#define GPR_A1 5
#define GPR_S2 18
#define GPR_S3 19
#define GPR_RA 31

/* First four instructions of Bouze_Decompress (sw ra,0(a2) / sw s0,8(a2) /
 * sw s1,0xC(a2) / sw s2,0x10(a2)). */
static const uint32_t kBouzeDecompressGuard[4] = {
    0xACDF0000u, 0xACD00008u, 0xACD1000Cu, 0xACD20010u,
};

/* Size option ("size" in the manifest): percent of the enemy's own size.
 * Read once at activation; the scale goes into Taz's actor records so the
 * loader sets actor +0xEC/+0xEE/+0xF0 itself. */
#define DEFAULT_SIZE_PERCENT 75

static Donor g_donor;              /* immutable once loaded */
static int32_t g_scale = 0x1000 * DEFAULT_SIZE_PERCENT / 100;
static int g_donor_state;          /* 0 = not loaded yet, 1 = ready, -1 = unusable */
static uint32_t g_donor_guest;     /* process-lifetime mod memory, 0 if none */

static void trca01_load_donor(void)
{
    if (g_donor_state != 0)
        return;
    uint32_t size = 0;
    if (!psx_mod_read_disc_file(DONOR_FILE, NULL, 0, &size) || size == 0)
        return;                    /* no disc mounted yet: try again later */
    uint8_t* file = (uint8_t*)malloc(size);
    if (!file)
        return;
    uint32_t got = 0;
    int spin_node = 0;
    int ok = psx_mod_read_disc_file(DONOR_FILE, file, size, &got) && got == size &&
             donor_extract(file, size, DONOR_ACTOR_TYPE, &g_donor);
    if (ok) {
        /* Without the synthetic spin, the spin falls back to a default
         * pose (no tornado): reported, not fatal. */
        spin_node = donor_add_spin(&g_donor, DONOR_SUB_SPIN, DONOR_SPIN_FRAMES);
        ok = g_donor.image_size <= FIX_TABLE_OFF;
    }
    if (ok) {
        g_donor_state = 1;
        if (spin_node)
            psx_mod_counter_add(PLUGIN_TRCA01 ".spin_attach_node", (uint32_t)spin_node);
        else
            psx_mod_counter_add(PLUGIN_TRCA01 ".spin_unavailable", 1);
    } else {
        donor_free(&g_donor);
        g_donor_state = -1;
    }
    free(file);
}

static void trca01_read_size_option(void)
{
    char v[16];
    int pct = DEFAULT_SIZE_PERCENT;
    if (psx_mod_option_value("bbt.third-character", "trca01", "size", v, sizeof v)) {
        const int p = atoi(v);
        if (p >= 25 && p <= 150)
            pct = p;
    }
    g_scale = 0x1000 * pct / 100;
}

static void trca01_activate(void)
{
    trca01_read_size_option();
    if (!g_donor_guest)
        g_donor_guest = psx_mod_alloc_guest_memory(DONOR_RESERVE, 16);
    trca01_load_donor();
}

static void count(const char* name, uint32_t n)
{
    psx_mod_counter_add(name, n);
}

/* Copy the donor image into mod memory. psx_mod_host_write_ram only
 * accepts main RAM, so Expansion 1 is written through the ordinary guest
 * store path, one word at a time (image and address are word aligned). */
static void write_donor_image(void)
{
    const uint8_t* p = g_donor.image;
    for (uint32_t i = 0; i < g_donor.image_size; i += 4)
        psx_mod_write_word(g_donor_guest + i,
                           (uint32_t)p[i] | ((uint32_t)p[i + 1] << 8) |
                           ((uint32_t)p[i + 2] << 16) | ((uint32_t)p[i + 3] << 24));
}

/* Reads the replaced character's own blob header from the type-4 chunk. */
static int read_target_header(void* ctx, uint32_t blob_off, uint8_t out[8])
{
    const uint32_t a = *(const uint32_t*)ctx + blob_off;
    if (!in_main_ram(a) || !in_main_ram(a + 7u))
        return 0;
    for (uint32_t i = 0; i < 8; ++i)
        out[i] = psx_mod_read_byte(a + i);
    return 1;
}

/* Wrap of Bouze_Decompress(src, dst, work): runs the original unchanged;
 * after the type-4 chunk of a 2-player level load is unpacked, retargets
 * Taz's resource records before the loader parses them. The call counters
 * document which path each load took. */
static int bouze_decompress_wrap(CPUState* cpu)
{
    const uint32_t ra = cpu->gpr[GPR_RA];
    const uint32_t blob_base = cpu->gpr[GPR_A1];
    /* The data-part call computes its destination as $s2 (stream base) +
     * $s3 (header data offset), at 0x800684F8; the type-4 call loads it
     * from DAT_8008DC18 while $s2 is the parser's cursor. */
    const int data_part = blob_base == cpu->gpr[GPR_S2] + cpu->gpr[GPR_S3];
    func_override_call_original(cpu);
    if (ra == RA_STREAM_CALL) {
        count(PLUGIN_TRCA01 ".call.stream", 1);
        return 1;
    }
    if (ra == RA_VARIANT_CALL) {
        count(PLUGIN_TRCA01 ".call.variant", 1);
        return 1;
    }
    if (ra != RA_DATA_PART_CALL) {
        count(PLUGIN_TRCA01 ".call.other", 1);
        return 1;
    }
    if (data_part) {
        count(PLUGIN_TRCA01 ".call.data_part", 1);
        return 1;
    }
    /* Both were set by the loader before the 0x2B marker, which used them
     * to lay out the pools just before this call. */
    const uint32_t stream_base = psx_mod_read_word(ADDR_STREAM_BASE);
    const uint32_t len = psx_mod_read_word(ADDR_STREAM_RESERVED);
    count(PLUGIN_TRCA01 ".call.blob_chunk", 1);
    if (cpu->gpr[GPR_V0] == 0)
        return 1;
    if ((psx_mod_read_byte(ADDR_MODE_FLAGS) & MODE_FLAG_TWO_PLAYER) == 0)
        return 1;
    trca01_load_donor();
    if (g_donor_state != 1 || !g_donor_guest) {
        count(PLUGIN_TRCA01 ".donor_unavailable", 1);
        return 1;
    }
    if (!in_main_ram(stream_base) || !in_main_ram(blob_base) || len == 0 ||
        len > MAX_STREAM_BYTES || !in_main_ram(stream_base + len - 1u) ||
        blob_base <= stream_base + len ||
        blob_base != psx_mod_read_word(ADDR_BLOB_BASE)) {
        count(PLUGIN_TRCA01 ".unexpected_state", 1);
        return 1;
    }

    uint8_t* s = (uint8_t*)malloc(len);
    if (!s)
        return 1;
    uint32_t i = 0;
    for (; i + 4 <= len; i += 4) {
        const uint32_t w = psx_mod_read_word(stream_base + i);
        s[i] = (uint8_t)w;
        s[i + 1] = (uint8_t)(w >> 8);
        s[i + 2] = (uint8_t)(w >> 16);
        s[i + 3] = (uint8_t)(w >> 24);
    }
    for (; i < len; ++i)
        s[i] = psx_mod_read_byte(stream_base + i);

    DonorWrite changed[MAX_PATCHED];
    DonorAnimFix fixes[MAX_FIXES];
    int fix_count = 0;
    DonorPatchStats st;
    uint32_t base_ctx = blob_base;
    int n = donor_patch_stream(s, len, TYPE_TAZ, &g_donor, g_donor_guest,
                               blob_base, kTazToTrca01, TAZ_MAP_COUNT,
                               kDonorDefaults, DONOR_DEFAULT_COUNT,
                               read_target_header, &base_ctx, changed,
                               MAX_PATCHED, fixes, MAX_FIXES, &fix_count, &st);
    const int blobs = n;
    if (n > 0)
        n = donor_patch_actor_scale(s, len, TYPE_TAZ, g_scale, changed, n,
                                    MAX_PATCHED);
    if (n < 0) {
        count(PLUGIN_TRCA01 ".stream_rejected", 1);
    } else if (n > 0) {
        /* Donor first, then the records that point at it. */
        write_donor_image();
        const uint32_t table = g_donor_guest + FIX_TABLE_OFF;
        for (int k = 0; k < fix_count; ++k) {
            psx_mod_write_word(table + 4u + 8u * (uint32_t)k,
                               (uint32_t)fixes[k].res_id | ((uint32_t)fixes[k].frames << 16));
            psx_mod_write_word(table + 8u + 8u * (uint32_t)k, fixes[k].ticks);
        }
        psx_mod_write_word(table, FIX_PENDING | (uint32_t)fix_count);
        int failed = 0;
        for (int k = 0; k < n; ++k)
            if (!psx_mod_host_write_ram(stream_base + (uint32_t)changed[k].off,
                                        s + changed[k].off, changed[k].len))
                ++failed;
        if (failed)
            count(PLUGIN_TRCA01 ".record_write_failed", (uint32_t)failed);
        count(PLUGIN_TRCA01 ".loads_patched", 1);
        count(PLUGIN_TRCA01 ".models_patched", (uint32_t)st.models);
        count(PLUGIN_TRCA01 ".anims_mapped", (uint32_t)st.anims_mapped);
        count(PLUGIN_TRCA01 ".anims_default", (uint32_t)st.anims_default);
        count(PLUGIN_TRCA01 ".scale_records", (uint32_t)(n - blobs));
    } else {
        count(PLUGIN_TRCA01 ".no_taz_in_level", 1);
    }
    free(s);
    return 1;
}

/* Apply the timing fixes to the animation records the loader built for
 * Taz: record +0x08 = frame count, +0x0B = ticks per keyframe. Runs after a successful
 * Level_LoadParse of a load that was patched (table pending). */
static int apply_anim_fixes_in_list(uint32_t list_head, uint32_t table, uint32_t count)
{
    int fixed = 0;
    uint32_t p = psx_mod_read_word(list_head);
    for (int a = 0; in_main_ram(p) && a < MAX_ACTORS_PER_LIST; ++a) {
        if (psx_mod_read_word(p + ACTOR_OFF_TYPE) == TYPE_TAZ) {
            uint32_t node = psx_mod_read_word(p + ACTOR_OFF_RES);
            for (int k = 0; in_main_ram(node) && k < 512; ++k) {
                const uint32_t e = psx_mod_read_word(node + 8u);
                if (in_main_ram(e) && psx_mod_read_half(e + 8u) == 4u) {
                    const uint16_t id = psx_mod_read_half(e + 6u);
                    const uint32_t rec = psx_mod_read_word(e + 0x18u);
                    for (uint32_t f = 0; f < count && in_main_ram(rec); ++f) {
                        const uint32_t w = psx_mod_read_word(table + 4u + 8u * f);
                        if ((uint16_t)w != id)
                            continue;
                        psx_mod_write_half(rec + 8u, (uint16_t)(w >> 16));
                        psx_mod_write_byte(rec + 0xBu,
                                           (uint8_t)psx_mod_read_word(table + 8u + 8u * f));
                        ++fixed;
                        break;
                    }
                }
                node = psx_mod_read_word(node);
            }
        }
        p = psx_mod_read_word(p + ACTOR_OFF_NEXT);
    }
    return fixed;
}

static int trca01_level_load_wrap(CPUState* cpu)
{
    func_override_call_original(cpu);
    if (cpu->gpr[GPR_V0] != 0 || !g_donor_guest)
        return 1;
    const uint32_t table = g_donor_guest + FIX_TABLE_OFF;
    const uint32_t head = psx_mod_read_word(table);
    if ((head & FIX_PENDING) == 0)
        return 1;
    const uint32_t n_fixes = head & 0xFFFFu;
    int fixed = 0;
    if (n_fixes <= MAX_FIXES) {
        fixed += apply_anim_fixes_in_list(ADDR_ACTOR_LIST_DEFS, table, n_fixes);
        fixed += apply_anim_fixes_in_list(ADDR_ACTOR_LIST_PLACED, table, n_fixes);
    }
    psx_mod_write_word(table, n_fixes);
    count(PLUGIN_TRCA01 ".anim_records_fixed", (uint32_t)fixed);
    return 1;
}

PSX_MOD_CONSTRUCTOR(register_bbt_third_character)
{
    (void)psx_mod_register_function_override(
        "bbt.third-character.swap-test:level-load", ADDR_LEVEL_LOAD_PARSE,
        level_load_parse_wrap, kLevelLoadParseGuard, 4, FO_CREDIT_SELF);

    (void)psx_mod_register_activation_plugin(PLUGIN_TRCA01, trca01_activate);
    (void)psx_mod_register_function_override(
        PLUGIN_TRCA01 ":bouze-data", ADDR_BOUZE_DECOMPRESS,
        bouze_decompress_wrap, kBouzeDecompressGuard, 4, FO_CREDIT_SELF);
    /* Same address as the swap test's wrap: only one of the two features
     * may be enabled at a time. */
    (void)psx_mod_register_function_override(
        PLUGIN_TRCA01 ":level-load", ADDR_LEVEL_LOAD_PARSE,
        trca01_level_load_wrap, kLevelLoadParseGuard, 4, FO_CREDIT_SELF);
}
