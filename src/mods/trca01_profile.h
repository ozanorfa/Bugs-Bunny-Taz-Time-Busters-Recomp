/*
 * The TRCA01 enemy as Taz's replacement: which character, which file and
 * which donor animation plays for each of Taz's moves.
 *
 * Shared by the plugin (third_character.c) and the offline tests
 * (tests/probe/bbt_disc_probe.c), so both use the same table. Host-side
 * data only; no guest access.
 */
#ifndef BBT_TRCA01_PROFILE_H
#define BBT_TRCA01_PROFILE_H

#include <stdint.h>

#include "donor.h"

#define TYPE_BUGS 0x05000181u
#define TYPE_TAZ  0x05000190u

#define DONOR_FILE        "LEVEL5/TRCA01.BZZ"
#define DONOR_ACTOR_TYPE  0x01008381u
#define DONOR_LEVEL_ID    55u        /* TRCA01's Bouze id (verified by level jump) */
#define TAZ_MODEL_SUB     0x000Du    /* model kind of Taz's (and the donor's) body */

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
    {0x0037u, 0x00E8u},   /* fall -> arms thrown up (instead of standing upright in the air) */
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
#define TAZ_MAP_COUNT ((int)(sizeof(kTazToTrca01) / sizeof(kTazToTrca01[0])))

/* Every other Taz animation: a standing pose with the arms down, the first
 * of these that is at least as long as Taz's own animation (so a play-once
 * animation can keep Taz's frame count without running past the donor
 * data). 0x001C (21 frames) is the stance the enemy plays most in its own
 * level; 0x013E (34) and 0x01C3 (71) are longer standing animations. The
 * earlier default 0x0018 is a 2-frame arms-out pose (the spread arms). */
static const uint16_t kDonorDefaults[] = {0x001Cu, 0x013Eu, 0x01C3u};
#define DONOR_DEFAULT_COUNT ((int)(sizeof(kDonorDefaults) / sizeof(kDonorDefaults[0])))

#endif /* BBT_TRCA01_PROFILE_H */
