/*
 * Donor character data for the third-character mod (host-side logic only;
 * no guest access, so it can be tested offline against the disc).
 *
 * A "donor" is a character taken from another level file: its model and
 * animation blobs (resource records of kind 0x22, tags 0x24 / 0x25, stored
 * in the file's type-4 chunk) are packed into one image that the plugin
 * copies into mod guest memory. The level being loaded is then made to use that image by rewriting, in its
 * decompressed record stream, the blob payloads of the resource records
 * owned by the replaced character. Lengths never change, so the game's
 * pools and parser see the same stream shape.
 */
#ifndef BBT_DONOR_H
#define BBT_DONOR_H

#include <stddef.h>
#include <stdint.h>

#define DONOR_MAX_MODELS 8
#define DONOR_MAX_ANIMS 256

typedef struct DonorBlob {
    uint16_t sub;     /* tag 0x27 low half: model kind or animation id */
    uint16_t id;      /* tag 0x27 high half: resource id in the source file */
    uint32_t off;     /* offset inside Donor.image */
    uint32_t size;
    uint16_t frames;  /* animations: header u16 [4] (frame count) */
    uint8_t ticks;    /* animations: header [2] >> 1 = game ticks per keyframe */
} DonorBlob;

typedef struct Donor {
    uint8_t* image;               /* malloc'd; blobs, each 4-byte aligned */
    uint32_t image_size;
    DonorBlob models[DONOR_MAX_MODELS];
    int model_count;
    DonorBlob anims[DONOR_MAX_ANIMS];
    int anim_count;
} Donor;

/* Extract the model and animation blobs referenced by the actor records
 * whose header type (tag 0x16 word 0) is `actor_type`, from a whole .BZZ
 * file. Returns 1 on success, 0 if the file or actor is not as expected. */
int donor_extract(const uint8_t* file, size_t file_len, uint32_t actor_type,
                  Donor* out);
/* The same for exactly the resource records with the given ids (for an
 * actor whose header type is shared with others, e.g. 0 for Daffy in
 * LEVEL1/RETRY.BZZ). Returns 1 on success. */
int donor_extract_ids(const uint8_t* file, size_t file_len, const uint16_t* res_ids,
                      int res_count, Donor* out);
void donor_free(Donor* d);

/* Find, in a decompressed record stream, the model resource of kind `sub`
 * referenced by actors of `actor_type`: its blob offset (relative to the
 * type-4 chunk) and size. Returns 1 if found. */
int donor_find_model(const uint8_t* s, size_t len, uint32_t actor_type, uint16_t sub,
                     uint32_t* off, uint32_t* size);

/* Append a synthetic spin animation with id `sub` (`frames` keyframes) to
 * the donor. Taz's spin draws a tornado: a separate actor the game attaches
 * at the node his spin animations name with the root command (0xA00, actor
 * +0x84; Ghidra: Actor_Update, the grab/attach code before 0x8001D0F8)
 * while the body is not drawn. The donor's ordinary animations either have
 * no root command (no tornado, nothing visible) or put it on a node high up
 * the body. This one keeps node 1 at the actor origin and the donor's
 * attach node (the node its own root commands use, found by parsing all
 * its animations) next to it, so the tornado sits on the ground. Returns
 * the attach node used, or 0 if it could not be built. */
int donor_add_spin(Donor* d, uint16_t sub, uint16_t frames);

/* Deep copy of a donor (its own image buffer). Returns 1 on success. */
int donor_copy(const Donor* src, Donor* out);

/* Append a "hold" animation `hold_sub` of `frames` keyframes that keeps one
 * pose: frame 0 of donor animation `src_sub` (the enemy's stances set every
 * node absolutely on frame 0; later frames are deltas), then frames without
 * commands. The keyframe interpreter FUN_800563EC runs exactly the command
 * count of each frame header (Ghidra), so an empty frame changes nothing.
 * For moves the donor has no animation for: they keep the replaced
 * character's frame count (its state timing) without the jitter of a
 * moving donor loop cut short. Returns 1 on success. */
int donor_add_hold(Donor* d, uint16_t src_sub, uint16_t hold_sub, uint16_t frames);

/* Append an animation blob (8-byte header, keyframes) built elsewhere
 * (e.g. retarget.c) with id `sub`. Returns 1 on success, 0 when the id is
 * taken or there is no room. */
int donor_append_anim(Donor* d, uint16_t sub, const uint8_t* blob, uint32_t size);

/* Which donor animation plays for one animation id of the replaced
 * character. Animation ids are per character (the same number means a
 * different move for another character), so roles are mapped explicitly. */
typedef struct DonorAnimMap {
    uint16_t target_sub;  /* animation id the replaced character asks for */
    uint16_t donor_sub;   /* donor animation id played instead */
} DonorAnimMap;

/* Post-load correction of one animation record (the 16-byte record the
 * loader builds from the blob header): +0x08 frame count, +0x0B game ticks
 * per keyframe (the animation player FUN_80057F78 counts it down; whether
 * an animation loops or plays once is chosen by the caller, not the record). */
typedef struct DonorAnimFix {
    uint16_t res_id;      /* resource id (entry +0x06) of the record */
    uint16_t frames;
    uint8_t ticks;
} DonorAnimFix;

/* The donor animation played for animation id `target_sub` of the
 * replaced character, whose own animation has `own_frames` frames: the
 * mapped one (`map[i].target_sub == target_sub`, *mapped = 1), else the
 * first of `defaults` with at least `own_frames` frames, else the longest
 * of `defaults`, else the donor's first animation. NULL without animations. */
const DonorBlob* donor_pick_anim(const Donor* d, uint16_t target_sub, uint16_t own_frames,
                                 const DonorAnimMap* map, int map_count,
                                 const uint16_t* defaults, int default_count, int* mapped);

/* Frame count a record playing donor animation `b` gets: the replaced
 * character's own (its state machine times states by one pass), capped at
 * the donor's. */
uint16_t donor_anim_frames(const DonorBlob* b, uint16_t own_frames);

/* Relocate model blob `m` of a donor image that will live at guest address
 * `guest_image`, as the game's loader does for a tag-0x24 model
 * (FUN_8005BB84, Ghidra: byte [1] bit 0 = done, u16 [2] = part count,
 * word [4] += blob, then every 8 bytes from [8] word += blob + 8; skipped
 * when word [0x20] is 0xFFFF0000; call site 0x80068FC0, a0 = the blob).
 * Returns 1 when relocated (the resource entry then points at blob + 8, as
 * the loader sets it), 2 when the loader would leave it as it is (entry =
 * blob), 0 if the blob is malformed or already relocated. */
int donor_relocate_model(uint8_t* image, uint32_t image_size, const DonorBlob* m,
                         uint32_t guest_image);

/* Reads the 8-byte header of the replaced character's own blob at
 * `blob_off` (relative to the level's type-4 chunk). Returns 1 on success. */
typedef int (*DonorTargetHeaderFn)(void* ctx, uint32_t blob_off, uint8_t out[8]);

/* One rewritten span of the record stream (offset, length). */
typedef struct DonorWrite {
    size_t off;
    uint32_t len;
} DonorWrite;

typedef struct DonorPatchStats {
    int actor_records;    /* records of the replaced character */
    int resource_ids;     /* distinct resource ids they reference */
    int models;           /* model payloads rewritten */
    int anims_mapped;     /* animation payloads given a mapped animation */
    int anims_default;    /* animation payloads given the default animation */
    int other;            /* referenced records with neither blob tag */
} DonorPatchStats;

/* Rewrite, in place, the tag 0x24 / 0x25 payloads of every resource record
 * referenced by actors of `target_type` in the decompressed record stream
 * `s`, so they point into the donor image located at guest address
 * `image_addr` (payload offsets are relative to `blob_base`, the RAM
 * address of the level's type-4 chunk, which the loader adds them to). A
 * model keeps its sub kind if the donor has one of the same kind (else the
 * donor's first model); an animation with id S gets the donor animation
 * `map[i].donor_sub` where `map[i].target_sub == S`, else the first of
 * `defaults` that has at least as many frames as the replaced animation
 * (read through `read_hdr`), else the longest of `defaults`. Tag 0x27 is
 * never changed, so the character's script still finds every animation id
 * it asks for.
 * The replaced character's state machine times its states by one pass of
 * the animation it plays (loops included), so for every animation whose
 * donor length or speed differs, a fix is emitted that gives the record the
 * original's frame count (capped at the donor's, so the donor data is
 * never read past its end) and the original's ticks per keyframe.
 * Each rewritten payload is stored in `changed` (up to `cap`). Returns the
 * number of payloads rewritten, or
 * -1 on a malformed stream or when more than `cap` would change (then
 * nothing is guaranteed about `s`; the caller must not write it back). */
int donor_patch_stream(uint8_t* s, size_t len, uint32_t target_type,
                       const Donor* d, uint32_t image_addr, uint32_t blob_base,
                       const DonorAnimMap* map, int map_count,
                       const uint16_t* defaults, int default_count,
                       DonorTargetHeaderFn read_hdr, void* ctx,
                       DonorWrite* changed, int cap,
                       DonorAnimFix* fixes, int fix_cap, int* fix_count,
                       DonorPatchStats* stats);

/* Set the scale (tag 0x12: three 32-bit values the loader stores as s16 at
 * actor +0xEC / +0xEE / +0xF0; 0x1000 = 1.0, verified by changing it in
 * RAM) of every actor record of `target_type` in the stream, in place.
 * Appends the rewritten payloads to `changed` starting at index `n`.
 * Returns the new count, or -1 on a malformed stream / full list. */
int donor_patch_actor_scale(uint8_t* s, size_t len, uint32_t target_type,
                            int32_t scale, DonorWrite* changed, int n, int cap);

#endif /* BBT_DONOR_H */
