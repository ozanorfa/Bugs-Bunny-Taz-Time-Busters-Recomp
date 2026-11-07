/*
 * The TRCA01 donor (trca01_profile.h) as read from the player's own disc,
 * shared by the plugins that draw it: bbt.third-character.trca01 (Taz
 * becomes the enemy, Phase 3) and bbt.third-character.p3-actor (the enemy
 * as a third character, Phase 4). Nothing of the game is stored anywhere
 * but in memory, read from the mounted disc.
 *
 * Emulation thread only for loading. Once state is 1 the record never
 * changes again, so other threads (debug commands) may read it.
 */
#ifndef BBT_DONOR_RUNTIME_H
#define BBT_DONOR_RUNTIME_H

#include "donor.h"
#include "donor_tex.h"

typedef struct BbtDonorRuntime {
    int state;          /* 0 = not loaded yet, 1 = ready, -1 = unusable */
    Donor donor;        /* models, animations (+ synthetic spin), image */
    int spin_node;      /* attach node of the synthetic spin, 0 if none */
    DonorTex tex;       /* texels and CLUTs of its model (sub TAZ_MODEL_SUB) */
    int tex_ready;
} BbtDonorRuntime;

/* The shared record (never NULL). */
const BbtDonorRuntime* bbt_donor_runtime(void);

/* Load the donor if that has not happened yet; no-op once ready or
 * unusable. While no disc is mounted it stays 0 and can be retried.
 * Returns the state. Each plugin reports the outcome under its own
 * counters (psx_mod_counter_add keeps the name pointer: literals only). */
int bbt_donor_runtime_load(void);

/* Upload w*h 16-bit pixels to VRAM (x, y) through the PsyQ LoadImage path
 * (psx_mod_psyq_load_image), staged in guest RAM below the stack pointer
 * `sp` (unused at that moment; the staged words are restored afterwards).
 * Emulation thread, inside a guest call. At most 1024 pixels. Returns 1 on
 * success. */
int bbt_upload_rect(uint32_t sp, int x, int y, int w, int h, const uint16_t* px);

/* FNV-1a (32-bit) over pixels as little-endian bytes, as the tests compute
 * it over VRAM read back with the debug server's vram_peek. */
uint32_t bbt_fnv1a_pixels(const uint16_t* px, uint32_t n);

#endif /* BBT_DONOR_RUNTIME_H */
