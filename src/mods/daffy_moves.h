/*
 * Daffy's moves: Bugs' animations retargeted onto Daffy (daffy_moves.c).
 */
#ifndef BBT_DAFFY_MOVES_H
#define BBT_DAFFY_MOVES_H

#include <stdint.h>

#include "donor.h"

typedef struct BbtDaffyMoveStats {
    int moves;            /* animations built (one per Taz id Bugs also has) */
    int without_source;   /* Taz ids Bugs lacks (they keep the still pose) */
    int failed;
    int hold;             /* the still pose was built */
    uint32_t bytes;
} BbtDaffyMoveStats;

/* Reads Bugs' and Taz's animations from the disc, appends to `daffy` one
 * animation per Taz id that Bugs also has (not the spin family) and the
 * still pose `hold_sub` (`hold_frames` frames), and writes the role map
 * (id -> id) to `map`. Emulation thread (disc read). Returns the map
 * entries written, or -1 when the sources could not be read. */
int bbt_daffy_add_moves(Donor* daffy, DonorAnimMap* map, int cap, uint16_t hold_sub,
                        uint16_t hold_frames, BbtDaffyMoveStats* stats);

#endif /* BBT_DAFFY_MOVES_H */
