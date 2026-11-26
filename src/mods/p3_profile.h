/*
 * The third character (Daffy, daffy_profile.h): the game's hero types, the
 * synthetic animations the plugin adds to his donor, and where his texels
 * and CLUTs go in VRAM.
 *
 * Shared by the plugin (third_actor.c, donor_runtime.c, daffy_moves.c) and
 * the offline tests (tests/probe/bbt_disc_probe.c, tests/bbt/p3_vram.py),
 * so both use the same values. Host-side data only; no guest access.
 *
 * Until 2026-11-26 these lived in trca01_profile.h with the Transylvania
 * enemy's table (TRCA01), which could also be the third character or Taz's
 * replacement; both were removed (user: Daffy only).
 */
#ifndef BBT_P3_PROFILE_H
#define BBT_P3_PROFILE_H

#include <stdint.h>

#define TYPE_BUGS 0x05000181u
#define TYPE_TAZ  0x05000190u

/* Synthetic animations (donor.c): the spin built from the donor's skeleton
 * (donor_add_spin; Daffy's fallback when Bugs' attack cannot be fitted),
 * and the still pose held for every move without a role (donor_add_hold,
 * Taz's longest animation is 83 frames). Not real animation ids. */
#define DONOR_SUB_SPIN    0xF001u
#define DONOR_SPIN_FRAMES 32u
#define DONOR_SUB_HOLD    0xF002u
#define DONOR_HOLD_FRAMES 128u

/* Where the third character's texels and CLUTs go (third_actor.c). VRAM
 * nothing else uses: below the first displayed frame buffer (display
 * 512x240 at 512,0 and 512,256; the frame's fills cover only the draw
 * areas 512,16 / 512,272 of 512x216; every animated-texture copy lands at
 * x < 512 -- gpu_state and GPU frame dumps). 16 rows: blocks taller than
 * that go to P3_TALL_* below. */
#define P3_STRIP_X 512
#define P3_STRIP_Y 240
#define P3_STRIP_W 512
#define P3_STRIP_H 16

/* BUGS #25: the blocks taller than the strip go to an area below x = 512
 * that no level writes while it is played, at full height, in shelves of
 * blocks. tests/soak_vram_free.py (2026-11-26) sampled VRAM x 0..511 during
 * ~30 s of three-player play in 32 levels: x 488..511, y 256..510 was the one
 * tall area unused in all of them (NOTES "Daffy's textures at full height").
 * Daffy's two tall blocks (8 units wide, up to 32 rows) take one shelf; the
 * area keeps room for four. */
#define P3_TALL_X 488
#define P3_TALL_Y 256
#define P3_TALL_W 24
#define P3_TALL_H 128

#endif /* BBT_P3_PROFILE_H */
