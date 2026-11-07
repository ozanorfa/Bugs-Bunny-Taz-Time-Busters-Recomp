/*
 * Donor textures for the third-character mod (host-side logic only; no
 * guest access, so it can be tested offline against the disc).
 *
 * The donor's model draws with texture page / CLUT positions of its own
 * level's VRAM layout (NOTES.md "M3"). In any other level those VRAM areas
 * hold that level's textures, so the donor shows wrong colours. This module
 *   - finds the model's textured primitive records and the VRAM blocks and
 *     CLUTs they use,
 *   - rebuilds those pixels from the donor level file the way the game
 *     uploads them (the TIM list: record kind 0x28, tag 0x29, into the
 *     type-3 chunk; image block first, then its CLUT),
 *   - plans a new place for them in a target level's free VRAM, and
 *   - computes the patched record fields (texture page, CLUT, texel u).
 */
#ifndef BBT_DONOR_TEX_H
#define BBT_DONOR_TEX_H

#include <stddef.h>
#include <stdint.h>

#include "donor.h"

#define DONOR_TEX_MAX_BLOCKS   32
#define DONOR_TEX_MAX_CLUTS    32
#define DONOR_TEX_MAX_RECORDS  512

#define VRAM_W 1024u
#define VRAM_H 512u

/* One rectangle of VRAM (16-bit units) used by the donor model. */
typedef struct DonorTexBlock {
    uint16_t x, y, w, h;      /* source position in the donor level's VRAM */
    uint32_t px;              /* first pixel in DonorTex.pixels */
} DonorTexBlock;

typedef struct DonorTexRecord {
    uint32_t off;             /* record offset inside Donor.image */
    uint8_t group;            /* index into DonorTex.cluts */
    uint8_t shift_u3;         /* quad whose +10/+11 field is a texel of the block */
} DonorTexRecord;

typedef struct DonorTex {
    uint16_t tpage;           /* source texture page field (4-bit page) */
    uint16_t page_x, page_y;  /* its VRAM origin */
    uint8_t umin, umax;       /* texel u range over all patched records */
    DonorTexBlock blocks[DONOR_TEX_MAX_BLOCKS];   /* one per CLUT group */
    int block_count;
    uint16_t cluts[DONOR_TEX_MAX_CLUTS];          /* source CLUT fields */
    int clut_count;
    DonorTexRecord records[DONOR_TEX_MAX_RECORDS];
    int record_count;
    int skipped_records;      /* textured records on another page (left as is) */
    uint16_t* pixels;         /* malloc'd: block pixels, then 16 per CLUT */
    uint32_t pixel_count;
    uint32_t clut_px;         /* first CLUT pixel in `pixels` */
} DonorTex;

/* Analyse the model blob `model` of `d` and rebuild its texels and CLUTs
 * from the donor level file. Returns 1 on success. */
int donor_tex_extract(const uint8_t* file, size_t file_len, const Donor* d,
                      const DonorBlob* model, DonorTex* out);
void donor_tex_free(DonorTex* t);

/* VRAM occupancy bitmap, one bit per 16-bit unit. */
typedef struct VramMap {
    uint8_t bits[VRAM_W * VRAM_H / 8u];
} VramMap;

void vram_map_clear(VramMap* m);
void vram_map_mark(VramMap* m, int x, int y, int w, int h);
int vram_map_is_free(const VramMap* m, int x, int y, int w, int h);

/* VRAM the replaced character's own model uses in a target level (its
 * texel boxes per page / CLUT group, and its CLUT rows). With the
 * character replaced nothing draws from it, and unlike VRAM the level does
 * not upload it is never the target of the level's own copies (animated
 * textures copy into VRAM that no upload covers). */
#define DONOR_POOL_MAX 64
typedef struct DonorTexPool {
    uint16_t x[DONOR_POOL_MAX], y[DONOR_POOL_MAX], w[DONOR_POOL_MAX], h[DONOR_POOL_MAX];
    int rect_count;
    uint16_t clut_x[DONOR_POOL_MAX], clut_y[DONOR_POOL_MAX];
    int clut_count;
} DonorTexPool;

/* Builds the pool from the replaced character's model blob (same record
 * format as the donor's). Returns 1 if it has any texel box. */
int donor_tex_pool_from_model(const uint8_t* model, uint32_t size, DonorTexPool* pool);

/* Where each donor CLUT group goes. */
typedef struct DonorTexPlan {
    uint16_t tpage[DONOR_TEX_MAX_CLUTS];    /* new page bits (x / 64 | (y / 256) << 4) */
    int16_t du[DONOR_TEX_MAX_CLUTS];        /* texel u shift */
    int16_t dv[DONOR_TEX_MAX_CLUTS];        /* texel v shift */
    uint16_t block_x[DONOR_TEX_MAX_CLUTS], block_y[DONOR_TEX_MAX_CLUTS];
    uint16_t clut_x[DONOR_TEX_MAX_CLUTS], clut_y[DONOR_TEX_MAX_CLUTS];
    /* Texel rows: a record's v becomes ((v - vsrc) >> vshift) + vdst (mod
     * 256); vshift 0 keeps every row (then the same as v + dv), 1 keeps
     * every second row, and so on. block_h = rows uploaded. */
    uint8_t vshift[DONOR_TEX_MAX_CLUTS];
    uint8_t vsrc[DONOR_TEX_MAX_CLUTS], vdst[DONOR_TEX_MAX_CLUTS];
    uint16_t block_h[DONOR_TEX_MAX_CLUTS];
} DonorTexPlan;

/* Pack the donor's CLUTs and blocks into the pool, counting only VRAM the
 * level really uploaded (`uploaded`): CLUTs first (the pool's own CLUT
 * rows, then 16-aligned rows inside its texel boxes), then the blocks,
 * largest first, each inside one texture page. Returns 1 if all fit. */
int donor_tex_plan(const DonorTex* t, const DonorTexPool* pool, const VramMap* uploaded,
                   DonorTexPlan* plan);

/* Pack the donor's blocks and CLUTs into one free strip of VRAM (x0, y0,
 * w x h, inside one texture-page row): blocks left to right, each inside one
 * 64-unit texture page, a block taller than the strip keeping every 2nd
 * (4th, ...) row; then the CLUTs, 16-aligned, after the blocks. For VRAM
 * that nothing else uses at all, e.g. below the displayed frame buffers.
 * Returns 1 if everything fits. */
int donor_tex_plan_strip(const DonorTex* t, int x0, int y0, int w, int h, DonorTexPlan* plan);

/* The pixels to upload for block `i` under `plan` (plan->block_h rows of
 * the block's width): the rows the plan keeps. Returns the pixel count. */
uint32_t donor_tex_block_pixels(const DonorTex* t, const DonorTexPlan* plan, int i,
                                uint16_t* out, uint32_t cap);

/* The 12 patched bytes (+0..+11: uv0, CLUT, uv1, texture page, uv2, uv3)
 * of record `r`, from its original bytes. */
void donor_tex_patch_record(const DonorTex* t, const DonorTexPlan* plan,
                            const DonorTexRecord* r, const uint8_t in[12],
                            uint8_t out[12]);

#endif /* BBT_DONOR_TEX_H */
