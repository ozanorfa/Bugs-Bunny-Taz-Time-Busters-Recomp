/*
 * Daffy Duck as the third character (Phase 5): where he is on the disc and
 * which animation gives his still pose.
 *
 * The only standalone Daffy on the disc is the actor of LEVEL1/RETRY.BZZ
 * (record kind 8, header type 0, so it is chosen by resource id, not by
 * type): model 19 (sub 0x000D, 18,964 bytes, 37 parts, 39 nodes), set-up
 * animation 21 (0x0001) and animation 51 (0x00D9, 272 frames, the only
 * real Daffy motion on the disc). Verified with tools/bouze; that it is
 * Daffy rests on its parts being equal to the intro cutscene's Daffy
 * (NOTES.md "Daffy Duck on the disc"). Host-side data only.
 */
#ifndef BBT_DAFFY_PROFILE_H
#define BBT_DAFFY_PROFILE_H

#include <stdint.h>

#define DAFFY_FILE       "LEVEL1/RETRY.BZZ"
#define DAFFY_MODEL_SUB  0x000Du
#define DAFFY_POSE_SUB   0x00D9u   /* its frame 0 sets every node: the still pose */

static const uint16_t kDaffyResIds[] = {19u, 21u, 51u};
#define DAFFY_RES_COUNT ((int)(sizeof(kDaffyResIds) / sizeof(kDaffyResIds[0])))

#endif /* BBT_DAFFY_PROFILE_H */
