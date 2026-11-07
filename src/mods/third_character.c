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
 *
 * Milestone M3 (same feature): the donor's textures. Its model draws with
 * texture page / CLUT positions of TRCA01's VRAM; elsewhere those hold the
 * level's own textures. donor_tex.c rebuilds the donor's texels and CLUTs
 * from TRCA01.BZZ the way the game uploads them. They go into Taz's own
 * VRAM: the texel boxes and CLUT rows of his original model in the level
 * being loaded (read from the type-4 chunk before his records are
 * redirected), counting only VRAM this load really uploaded. A map of
 * that is kept from the PsyQ LoadImage core (func_80071110, the only
 * uploader below x = 512; checked from boot to gameplay). Free-looking
 * VRAM is not used: the level's animated textures copy into VRAM no upload
 * covers (seen in Granwich, which overwrote CLUTs placed there). After a
 * patched load outside TRCA01 the pixels are uploaded
 * (psx_mod_psyq_load_image) and the model's records pointed at them.
 * Host state: the upload map and Taz's pool of the last load (rebuilt on
 * every load; a savestate carries VRAM and the patched model together).
 *
 * Tests: {"cmd":"mod_cmd","name":"bbt.trca01","args":"show"|"group <n>"}
 * reports the donor and where each texture group was uploaded, with pixel
 * hashes to compare against VRAM (tests/test_game_trca01.py).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "donor.h"
#include "donor_runtime.h"
#include "donor_tex.h"
#include "func_override.h"
#include "mod_plugins.h"
#include "trca01_profile.h"

#define RT (bbt_donor_runtime())

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

/* The donor (file, actor type) and the role table: trca01_profile.h. */

/* Animation timing fixes, kept in mod memory after the donor image (part of
 * savestates): word 0 = 0x80000000 (pending) | count, then two words per
 * fix: res_id | frames << 16, ticks. Applied after Level_LoadParse. */
#define FIX_TABLE_OFF   0x1F000u
#define MAX_FIXES       256
#define FIX_PENDING     0x80000000u
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

static int32_t g_scale = 0x1000 * DEFAULT_SIZE_PERCENT / 100;
static uint32_t g_donor_guest;     /* process-lifetime mod memory, 0 if none */

/* ---- M3: textures ------------------------------------------------------ */
#define ADDR_LEVEL_ID      0x80010000u  /* Bouze id of the current level */
#define ADDR_LOAD_IMAGE    0x80071110u  /* PsyQ LoadImage core (RECT *a0, data *a1) */
/* Frame buffers live at x >= 512 (GPU state: display 512,0 / 512,256, draw
 * areas 512..1023), so new texels and CLUTs go left of that. */
#define VRAM_TEXTURE_LIMIT 512
#define MAX_TAZ_MODEL      0x10000u     /* Taz's model blob is 11,528 bytes */
#define GPR_A0 4
#define GPR_SP 29

/* addiu sp,sp,-0x30 / sw s1,0x14(sp) / move s1,a0 / sw s2,0x18(sp) */
static const uint32_t kLoadImageGuard[4] = {
    0x27BDFFD0u, 0xAFB10014u, 0x00808821u, 0xAFB20018u,
};

static VramMap* g_occ_level;       /* VRAM uploaded by the current level load */
static int g_in_level_load;
static DonorTexPool g_pool;        /* Taz's own VRAM in the level being loaded */
static int g_pool_valid;

/* The last relocation, reported by the debug command bbt.trca01 (tests).
 * Written on the emulation thread at the end of a level load; the
 * generation is odd while it is being written, and readers retry or give
 * up then (tests read it between loads). */
static DonorTexPlan g_last_plan;
static volatile uint32_t g_last_plan_gen;
static volatile uint32_t g_last_plan_level;

static void count(const char* name, uint32_t n)
{
    psx_mod_counter_add(name, n);
}


/* The donor is shared with the Phase 4 third actor (donor_runtime.c). The
 * fix table follows the image in this plugin's reservation, so a larger
 * image than FIX_TABLE_OFF cannot be used here. */
static int g_donor_reported;       /* load outcome counted (once per process) */

static int trca01_load_donor(void)
{
    const int state = bbt_donor_runtime_load();
    if (state != 0 && !g_donor_reported) {
        g_donor_reported = 1;
        if (state == 1 && RT->spin_node)
            count(PLUGIN_TRCA01 ".spin_attach_node", (uint32_t)RT->spin_node);
        else if (state == 1)
            count(PLUGIN_TRCA01 ".spin_unavailable", 1);
        if (state == 1)
            count(RT->tex_ready ? PLUGIN_TRCA01 ".tex_ready" : PLUGIN_TRCA01 ".tex_unavailable", 1);
    }
    return state == 1 && RT->donor.image_size <= FIX_TABLE_OFF;
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
    if (!g_occ_level)
        g_occ_level = (VramMap*)calloc(1, sizeof(VramMap));
    trca01_load_donor();
}

/* Wrap of the LoadImage core: record where the game puts pixels. */
static int load_image_wrap(CPUState* cpu)
{
    const uint32_t rect = cpu->gpr[GPR_A0];
    if (g_in_level_load && g_occ_level && in_main_ram(rect) && in_main_ram(rect + 7u))
        vram_map_mark(g_occ_level, (int16_t)psx_mod_read_half(rect),
                      (int16_t)psx_mod_read_half(rect + 2u), (int16_t)psx_mod_read_half(rect + 4u),
                      (int16_t)psx_mod_read_half(rect + 6u));
    func_override_call_original(cpu);
    return 1;
}

/* After a patched load outside the donor's level: put the donor's texels
 * and CLUTs into Taz's own VRAM and point the model's records at them. */
static void relocate_textures(CPUState* cpu)
{
    if (!RT->tex_ready || !g_occ_level || !g_donor_guest)
        return;
    if (psx_mod_read_word(ADDR_LEVEL_ID) == DONOR_LEVEL_ID) {
        count(PLUGIN_TRCA01 ".tex_native_level", 1);
        return;
    }
    if (!g_pool_valid) {
        count(PLUGIN_TRCA01 ".tex_no_pool", 1);
        return;
    }
    DonorTexPlan plan;
    if (!donor_tex_plan(&RT->tex, &g_pool, g_occ_level, &plan)) {
        count(PLUGIN_TRCA01 ".tex_no_space", 1);
        return;                    /* model keeps TRCA01's texture fields */
    }
    const uint32_t sp = cpu->gpr[GPR_SP];
    int failed = 0;
    for (int i = 0; i < RT->tex.block_count; ++i) {
        const DonorTexBlock* b = &RT->tex.blocks[i];
        failed += !bbt_upload_rect(sp, plan.block_x[i], plan.block_y[i], b->w, b->h,
                               RT->tex.pixels + b->px);
    }
    for (int g = 0; g < RT->tex.clut_count; ++g)
        failed += !bbt_upload_rect(sp, plan.clut_x[g], plan.clut_y[g], 16, 1,
                               RT->tex.pixels + RT->tex.clut_px + 16u * (uint32_t)g);
    if (failed) {
        count(PLUGIN_TRCA01 ".tex_upload_failed", (uint32_t)failed);
        return;
    }
    for (int i = 0; i < RT->tex.record_count; ++i) {
        const DonorTexRecord* r = &RT->tex.records[i];
        uint8_t out[12];
        donor_tex_patch_record(&RT->tex, &plan, r, RT->donor.image + r->off, out);
        for (uint32_t k = 0; k < 12u; k += 2u)
            psx_mod_write_half(g_donor_guest + r->off + k, (uint16_t)(out[k] | (out[k + 1] << 8)));
    }
    ++g_last_plan_gen;
    g_last_plan = plan;
    g_last_plan_level = psx_mod_read_word(ADDR_LEVEL_ID);
    ++g_last_plan_gen;
    count(PLUGIN_TRCA01 ".tex_relocated", 1);
    count(PLUGIN_TRCA01 ".tex_records_patched", (uint32_t)RT->tex.record_count);
}

/* Debug command (debug-server thread; reads only host state that is
 * immutable once loaded, or the generation-guarded last plan):
 *   "show"      donor and texture state, the last relocation's level
 *   "group <n>" where group n's block and CLUT went, and FNV-1a hashes of
 *               the pixels that were uploaded there. */
static int trca01_command(const char* args, char* reply, int cap)
{
    if (args && strncmp(args, "group ", 6) == 0) {
        const int g = atoi(args + 6);
        if (!RT->tex_ready || g < 0 || g >= RT->tex.clut_count) {
            snprintf(reply, (size_t)cap, "{\"ok\":false,\"err\":\"no such group\"}");
            return 0;
        }
        const uint32_t gen = g_last_plan_gen;
        const DonorTexPlan p = g_last_plan;
        if (gen == 0 || (gen & 1u) || gen != g_last_plan_gen) {
            snprintf(reply, (size_t)cap, "{\"ok\":false,\"err\":\"no relocation yet\"}");
            return 0;
        }
        const DonorTexBlock* b = &RT->tex.blocks[g];
        snprintf(reply, (size_t)cap,
                 "{\"ok\":true,\"group\":%d,\"src_clut\":%u,\"block\":[%u,%u,%u,%u],"
                 "\"clut\":[%u,%u],\"tpage\":%u,\"du\":%d,\"dv\":%d,"
                 "\"block_fnv\":\"%08X\",\"clut_fnv\":\"%08X\"}",
                 g, RT->tex.cluts[g], p.block_x[g], p.block_y[g], b->w, b->h, p.clut_x[g],
                 p.clut_y[g], p.tpage[g], p.du[g], p.dv[g],
                 bbt_fnv1a_pixels(RT->tex.pixels + b->px, (uint32_t)b->w * b->h),
                 bbt_fnv1a_pixels(RT->tex.pixels + RT->tex.clut_px + 16u * (uint32_t)g, 16u));
        return 1;
    }
    const uint32_t gen = g_last_plan_gen;
    snprintf(reply, (size_t)cap,
             "{\"ok\":true,\"donor_state\":%d,\"donor\":\"0x%08X\",\"image_size\":%u,"
             "\"models\":%d,\"anims\":%d,\"tex_ready\":%d,\"groups\":%d,\"records\":%d,"
             "\"plan_gen\":%u,\"plan_level\":%u}",
             RT->state, g_donor_guest, RT->donor.image_size, RT->donor.model_count,
             RT->donor.anim_count, RT->tex_ready, RT->tex_ready ? RT->tex.clut_count : 0,
             RT->tex_ready ? RT->tex.record_count : 0, gen, g_last_plan_level);
    return 1;
}

/* Copy the donor image into mod memory. psx_mod_host_write_ram only
 * accepts main RAM, so Expansion 1 is written through the ordinary guest
 * store path, one word at a time (image and address are word aligned). */
static void write_donor_image(void)
{
    const uint8_t* p = RT->donor.image;
    for (uint32_t i = 0; i < RT->donor.image_size; i += 4)
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
    if (!trca01_load_donor() || !g_donor_guest) {
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

    /* Taz's own model, still unredirected, gives the VRAM his textures use
     * in this level: the pool the donor's textures go to (M3). */
    g_pool_valid = 0;
    uint32_t moff = 0, msize = 0;
    if (RT->tex_ready && donor_find_model(s, len, TYPE_TAZ, TAZ_MODEL_SUB, &moff, &msize) &&
        msize >= 12u && msize <= MAX_TAZ_MODEL && in_main_ram(blob_base + moff) &&
        in_main_ram(blob_base + moff + msize - 1u)) {
        uint8_t* m = (uint8_t*)malloc(msize);
        if (m) {
            for (uint32_t k = 0; k < msize; ++k)
                m[k] = psx_mod_read_byte(blob_base + moff + k);
            g_pool_valid = donor_tex_pool_from_model(m, msize, &g_pool);
            free(m);
        }
    }

    DonorWrite changed[MAX_PATCHED];
    DonorAnimFix fixes[MAX_FIXES];
    int fix_count = 0;
    DonorPatchStats st;
    uint32_t base_ctx = blob_base;
    int n = donor_patch_stream(s, len, TYPE_TAZ, &RT->donor, g_donor_guest,
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
    /* Uploads seen from here on belong to this level's VRAM layout. */
    if (g_occ_level)
        vram_map_clear(g_occ_level);
    g_in_level_load = 1;
    func_override_call_original(cpu);
    g_in_level_load = 0;
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
    relocate_textures(cpu);
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
    (void)psx_mod_register_function_override(
        PLUGIN_TRCA01 ":load-image", ADDR_LOAD_IMAGE,
        load_image_wrap, kLoadImageGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_command("bbt.trca01", trca01_command);
}
