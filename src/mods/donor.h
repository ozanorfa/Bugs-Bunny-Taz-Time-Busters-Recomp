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
void donor_free(Donor* d);

typedef struct DonorPatchStats {
    int actor_records;    /* records of the replaced character */
    int resource_ids;     /* distinct resource ids they reference */
    int models;           /* model payloads rewritten */
    int anims_same;       /* animation payloads given the same anim id */
    int anims_fallback;   /* animation payloads given the fallback anim */
    int other;            /* referenced records with neither blob tag */
} DonorPatchStats;

/* Rewrite, in place, the tag 0x24 / 0x25 payloads of every resource record
 * referenced by actors of `target_type` in the decompressed record stream
 * `s`, so they point into the donor image located at guest address
 * `image_addr` (payload offsets are relative to `blob_base`, the RAM
 * address of the level's type-4 chunk, which the loader adds them to). A
 * model keeps its sub kind if the donor has one of the same kind (else the donor's first model); an animation
 * gets the donor animation with the same id, else `fallback_sub`, else the
 * donor's first animation. Tag 0x27 is never changed, so the character's
 * script still finds every animation id it asks for.
 * The stream offset of each rewritten 8-byte payload is stored in
 * `changed` (up to `cap`). Returns the number of payloads rewritten, or
 * -1 on a malformed stream or when more than `cap` would change (then
 * nothing is guaranteed about `s`; the caller must not write it back). */
int donor_patch_stream(uint8_t* s, size_t len, uint32_t target_type,
                       const Donor* d, uint32_t image_addr, uint32_t blob_base,
                       uint16_t fallback_sub, size_t* changed, int cap,
                       DonorPatchStats* stats);

#endif /* BBT_DONOR_H */
