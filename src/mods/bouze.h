/*
 * Bouze (.BZZ) helpers for trusted mod plugins — C port of tools/bouze/.
 *
 * Bouze is the engine's name for its level/cutscene packs. These helpers
 * mirror the game's own Bouze_Decompress (0x8006B67C) and the record
 * grammar of Level_LoadParse (0x80068174); see NOTES.md "Bouze files" and
 * "Resources". They only read data from the player's own disc at runtime.
 */
#ifndef BBT_BOUZE_H
#define BBT_BOUZE_H

#include <stddef.h>
#include <stdint.h>

#define BOUZE_MAX_CHUNKS 16

typedef struct BouzeHeader {
    uint32_t version;
    uint32_t data_off;            /* where chunk 1 lands after chunk 0 */
    uint32_t count;
    uint32_t type[BOUZE_MAX_CHUNKS];
    uint32_t size[BOUZE_MAX_CHUNKS];     /* compressed size */
    uint32_t aligned[BOUZE_MAX_CHUNKS];  /* size rounded to 0x800 */
    uint32_t file_off[BOUZE_MAX_CHUNKS]; /* computed: header sector + chunks */
} BouzeHeader;

/* Parse the 2048-byte header sector of a Bouze file. 1 on success. */
int bouze_parse_header(const uint8_t* file, size_t file_len, BouzeHeader* h);

/* Chunk types (header table). Type 1 = record stream (chunk 0). Type 4 =
 * resource blob chunk: the offsets of record tags 0x24 / 0x25 are relative
 * to the RAM address it is decompressed to (verified in TRCA01: the
 * enemy's blobs sit at <type-4 base> + offset, and the base equals
 * DAT_8008DC18, the destination of Level_LoadParse's third
 * Bouze_Decompress call). */
#define BOUZE_CHUNK_STREAM 1u
#define BOUZE_CHUNK_BLOBS  4u

/* Index of the first chunk of `type`, or -1. */
int bouze_find_chunk(const BouzeHeader* h, uint32_t type);

/* Decode one LZ stream of `src_len` compressed bytes. Returns a malloc'd
 * buffer (caller frees) and its length, or NULL on malformed input. */
uint8_t* bouze_decompress(const uint8_t* src, size_t src_len, size_t* out_len);

/* One record of a decompressed record stream. */
typedef struct BouzeRecord {
    uint8_t kind;
    size_t start;      /* offset of the kind byte */
    size_t tags;       /* offset of the first tag */
    size_t end;        /* offset of the 0x2D terminator */
} BouzeRecord;

/* One tag inside a record. */
typedef struct BouzeTag {
    uint8_t tag;
    size_t payload;    /* offset of the payload */
    uint32_t len;      /* payload length */
} BouzeTag;

/* Iterate records: start with *pos = 0. Returns 1 with a record, 0 at the
 * stream's 0x2E terminator, -1 on a malformed stream. Unknown kind bytes
 * are skipped one byte at a time, as the game's loader does. */
int bouze_next_record(const uint8_t* s, size_t len, size_t* pos, BouzeRecord* r);

/* Iterate the tags of a record: start with *pos = r->tags. Returns 1 with a
 * tag, 0 at the record's end. */
int bouze_next_tag(const uint8_t* s, const BouzeRecord* r, size_t* pos, BouzeTag* t);

/* Record kinds that describe an actor (tag 0x16 = header, 0x0F = resource
 * id); kind 0x22 = resource record (0x27 = sub/id, 0x24 = model blob,
 * 0x25 = animation blob, both (offset, size) into the data part). */
#define BOUZE_KIND_RESOURCE 0x22u
#define BOUZE_TAG_ACTOR_HEADER 0x16u
#define BOUZE_TAG_ACTOR_RESOURCE 0x0Fu
#define BOUZE_TAG_ACTOR_SCALE 0x12u
#define BOUZE_TAG_RES_ID 0x27u
#define BOUZE_TAG_RES_MODEL 0x24u
#define BOUZE_TAG_RES_ANIM 0x25u

int bouze_is_actor_kind(uint8_t kind);

/* Little-endian readers over a host buffer. */
static inline uint32_t bouze_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static inline uint16_t bouze_u16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

#endif /* BBT_BOUZE_H */
