/*
 * Synthetic game data for the hermetic tests: byte buffers, Bouze LZ
 * streams and files, model and animation blobs, and one small "level" with
 * a donor character, a target character and a bystander, built from the
 * formats documented in NOTES.md ("Bouze files", "Resources", "M3",
 * "Keyframe stream format"). Nothing is copied from the disc.
 */
#ifndef BBT_SYNTH_H
#define BBT_SYNTH_H

#include <stddef.h>
#include <stdint.h>

typedef struct Buf {
    uint8_t* p;
    size_t n, cap;
} Buf;

void buf_init(Buf* b);
void buf_free(Buf* b);
void put8(Buf* b, uint8_t v);
void put16(Buf* b, uint16_t v);
void put32(Buf* b, uint32_t v);
void put_bytes(Buf* b, const void* p, size_t n);
void put_zero(Buf* b, size_t n);
void put_align4(Buf* b);
void set32(Buf* b, size_t at, uint32_t v);

/* A valid Bouze LZ stream holding `n` (>= 1) literal bytes. */
void synth_store(const uint8_t* data, size_t n, Buf* out);

/* A Bouze file: header sector, then each chunk's stream padded to 0x800. */
typedef struct SynthChunk {
    uint32_t type;
    const uint8_t* data;   /* plain bytes; stored with synth_store */
    size_t len;
} SynthChunk;
void synth_bzz(const SynthChunk* c, int n, Buf* out);

/* One primitive record of a model blob (NOTES.md "M3"): u16 +0 uv0, +2 CLUT,
 * +4 uv1, +6 texture page, +8 uv2, +10 uv3, +12 word 3 = op | flags << 8 |
 * size << 16; the rest is zero. */
typedef struct SynthPrim {
    uint8_t op, flags, size;
    uint16_t clut, tpage;
    uint8_t uv[4][2];
} SynthPrim;
/* Model blob: u32 part count << 16, primitive list offset (12), first part
 * offset; the primitives; a 4-byte part area. */
void synth_model(const SynthPrim* prims, int n, Buf* out);

/* Animation blob: 8-byte header ([0] 0x50, [2] ticks << 1, u16 [4]
 * frames), then per frame an 8-byte header (u16 halfwords + 2, command
 * count, frame index) and commands: a node translation (0x1101, 2 args) and,
 * when roots[f] != 0, a root command (0x0A00 | node). */
void synth_anim(uint16_t frames, uint8_t ticks, const uint8_t* roots, Buf* out);

/* Actor types used by the synthetic level. */
#define SYN_TYPE_DONOR    0x01008381u
#define SYN_TYPE_TARGET   0x05000190u
#define SYN_TYPE_OTHER    0x05000181u

/* The synthetic level: chunk types 1 (record stream), 3 (textures) and 4
 * (blobs). Resource ids: donor model 10 (sub 0xD), donor anims 11 (sub
 * 0x0001, 2 frames) and 12 (sub 0x0033, 4 frames); target model 20, anims
 * 21 (sub 0x0033, 3 frames, 2 ticks) and 22 (sub 0x0099, 10 frames, 3
 * ticks); bystander model 30. */
typedef struct SynthLevel {
    Buf stream, blobs, textures, file;
    uint32_t off[64], size[64];   /* blob offsets / sizes by resource id */
    size_t target_scale_payload;  /* stream offset of the target's tag 0x12 payload */
} SynthLevel;
void synth_level_build(SynthLevel* l);
void synth_level_free(SynthLevel* l);

/* Writes the synthetic spin (donor_add_spin on the synthetic donor) to
 * `spin_path`; with `dir`, also synth.bzz and its plain chunks
 * (synth_chunk<i>.bin) for the Python tools' tests. 1 on success. */
int synth_dump_for_python(const char* spin_path, const char* dir);

#endif /* BBT_SYNTH_H */
