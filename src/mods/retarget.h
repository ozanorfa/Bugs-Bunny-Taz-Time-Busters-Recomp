/*
 * Animation retargeting: one character's keyframe animations played on
 * another character's skeleton (Phase 5: Bugs' moves on Daffy). Host-side
 * and pure (no guest access), so it can be tested offline. Integer math
 * only: the output becomes guest data, so every machine (macOS, Windows)
 * must produce the same bytes for netplay.
 *
 * Keyframe streams as the game's interpreter FUN_800563EC runs them
 * (Ghidra; NOTES.md "Keyframe stream format" and "Daffy as the third
 * character"): per frame an 8-byte header (u16[1] = command count), then
 * commands u16 = node (bits 0-7, 1-based) | opcode (8-11) | flags (12-15)
 * with u16 arguments. Each node keeps a state: rotation (three angles,
 * 4096 = a turn; the matrix is RotMatrix's Rx * Ry * Rz), scale and
 * translation (offset from the parent, in the parent's frame).
 *   0x400 / 0x700: rotation packed in two words + the flags' high nibble,
 *     translation s8 x | s8 z << 8, s16 y (0x400 also sets scale 1.0);
 *   0xE00: the packed rotation only;
 *   0x100 / 0xD00: flag 0x2000 three raw angles (0xD00 without it: rotation
 *     0), 0x4000 three scales, 0x8000 three s16 translations or 0x1000 the
 *     packed s8 / s16 translation;
 *   0xB00: signed byte deltas for the rotation, scale and translation sets
 *     its flags name, in that order.
 *
 * The retarget: for each mapped joint the target's local rotation is
 * C_parent^T * R_source * C_joint, where C_joint turns the target's bone
 * (the offset of its main child at rest) onto the source's, so every
 * mapped bone points where the source's does. Translations are the
 * target's own (its bone lengths); the hips follow the source's hips,
 * moved up or down so the lowest toe is as high above the actor's origin
 * as the source's. The output has the target length (frame count) chosen
 * by the caller, sampling the source evenly.
 */
#ifndef BBT_RETARGET_H
#define BBT_RETARGET_H

#include <stddef.h>
#include <stdint.h>

#define RT_MAX_NODES 64

typedef struct RtState {
    int16_t rot[3];
    int16_t scale[3];
    int16_t tr[3];
} RtState;

/* Nodes of a set-up animation (0x0001): parent[n] for nodes 1..count
 * (0 = none). Returns the node count, or -1 if the blob is malformed. */
int rt_skeleton(const uint8_t* blob, uint32_t size, uint8_t* parent, int max);

/* Runs frame `frame` of an animation blob on the states (nodes 1..n; index
 * 0 unused), as the interpreter does, after frames 0..frame-1 have run.
 * Use rt_simulate for a whole animation. Returns 1, or 0 if malformed. */
int rt_simulate(const uint8_t* blob, uint32_t size, int frames_to_run, RtState* st, int n);

/* Matrices are 3x3, row-major, Q14 (16384 = 1.0). */
void rt_euler_to_matrix(const int16_t rot[3], int32_t m[9]);
void rt_matrix_to_euler(const int32_t m[9], int16_t rot[3]);   /* angles 0..4095 */
/* A rotation that turns the direction of `a` onto that of `b`. */
void rt_align(const int32_t a[3], const int32_t b[3], int32_t c[9]);

typedef struct RtJoint {
    uint8_t src, dst;            /* node numbers in each skeleton */
    uint8_t src_child, dst_child;/* their main child (0: none; the joint takes its parent's correction) */
} RtJoint;

typedef struct RtRig {
    int src_n, dst_n;
    uint8_t src_parent[RT_MAX_NODES], dst_parent[RT_MAX_NODES];
    uint8_t src_of[RT_MAX_NODES];      /* dst node -> src node (0: not driven) */
    uint8_t dst_of[RT_MAX_NODES];      /* src node -> dst node */
    int32_t corr[RT_MAX_NODES][9];     /* per dst node */
    RtState dst_rest[RT_MAX_NODES];    /* frame 0 of the target's rest animation */
    uint8_t hips_src, hips_dst;
    uint8_t toes_src[2], toes_dst[2];
} RtRig;

/* Builds the rig from both skeletons' set-up animations and a rest
 * animation of each (its frame 0 gives the bone offsets). Returns 1 on
 * success. */
int rt_rig_build(RtRig* rig,
                 const uint8_t* src_setup, uint32_t src_setup_size,
                 const uint8_t* src_rest, uint32_t src_rest_size,
                 const uint8_t* dst_setup, uint32_t dst_setup_size,
                 const uint8_t* dst_rest, uint32_t dst_rest_size,
                 const RtJoint* joints, int joint_count,
                 uint8_t hips_src, uint8_t hips_dst,
                 const uint8_t toes_src[2], const uint8_t toes_dst[2]);

/* Retargets one source animation into a new blob of `frames` frames, with
 * the 8-byte blob header `header` (its frame count is set to `frames`).
 * Frame 0 sets every target node; later frames only what changed. Returns
 * the blob size, or 0 if it does not fit in `cap` or the source is
 * malformed. */
uint32_t rt_retarget(const RtRig* rig, const uint8_t* src, uint32_t src_size,
                     const uint8_t header[8], uint16_t frames, uint8_t* out, uint32_t cap);

/* A still pose: frame 0 of rt_retarget's output for source frame
 * `src_frame`, then `frames - 1` empty frames. Returns the size or 0. */
uint32_t rt_still(const RtRig* rig, const uint8_t* src, uint32_t src_size, int src_frame,
                  const uint8_t header[8], uint16_t frames, uint8_t* out, uint32_t cap);

#endif /* BBT_RETARGET_H */
