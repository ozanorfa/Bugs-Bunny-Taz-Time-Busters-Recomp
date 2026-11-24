/* bouze.c: header parsing, LZ decoding, record and tag iteration. */
#include <stdlib.h>
#include <string.h>

#include "bouze.h"
#include "check.h"
#include "synth.h"

static void decompress_literals(void)
{
    static const uint8_t kData[] = "The quick brown fox jumps over the lazy dog";
    for (size_t n = 1; n < sizeof kData; ++n) {     /* every length, incl. 8k +- 1 */
        Buf s;
        buf_init(&s);
        synth_store(kData, n, &s);
        size_t len = 0;
        uint8_t* out = bouze_decompress(s.p, s.n, &len);
        CHECK(out != NULL);
        CHECK_EQ(len, n);
        CHECK(out && memcmp(out, kData, n) == 0);
        free(out);
        buf_free(&s);
    }
}

static void decompress_stops_at_the_token_count(void)
{
    /* Chunks are padded to 0x800 on the disc and the decoder is handed the
     * padded size in places: it must stop after count + 1 tokens, even
     * when the flag byte still has literal bits left and bytes follow. */
    static const uint8_t kData[] = "ABC";
    Buf s;
    buf_init(&s);
    synth_store(kData, 3, &s);
    put_zero(&s, 16);
    size_t len = 0;
    uint8_t* out = bouze_decompress(s.p, s.n, &len);
    CHECK(out != NULL);
    CHECK_EQ(len, 3);
    free(out);
    buf_free(&s);
}

static void decompress_match(void)
{
    /* k = 0: a match is a 9-bit distance and a 7-bit length code; codes
     * up to 0x13 mean code + 3 bytes. "ABC" + (distance 3, 7 bytes) gives
     * an overlapping copy. Tokens: 3 literals + 1 match -> count 3. */
    static const uint8_t kStream[] = {0x00, 0x00, 0x00, 0x03, 0x07, 'A', 'B', 'C', 0x01, 0x84};
    size_t len = 0;
    uint8_t* out = bouze_decompress(kStream, sizeof kStream, &len);
    REQUIRE(out != NULL);
    CHECK_EQ(len, 10);
    CHECK(memcmp(out, "ABCABCABCA", 10) == 0);
    free(out);
}

static void decompress_long_length(void)
{
    /* Parameters 0x0A: k = 2 (11-bit distance, 5-bit code), growth shift
     * 1. Code 0x15 is past the threshold 0x13: 0x13 + (2 << 1) = 0x17, so
     * the match copies 0x17 + 3 = 26 bytes. */
    static const uint8_t kStream[] = {0x0A, 0x00, 0x00, 0x01, 0x01, 'Z', (1 << 5 | 0x15) >> 8,
                                      (uint8_t)(1 << 5 | 0x15)};
    size_t len = 0;
    uint8_t* out = bouze_decompress(kStream, sizeof kStream, &len);
    REQUIRE(out != NULL);
    CHECK_EQ(len, 27);
    int all_z = 1;
    for (size_t i = 0; i < len; ++i)
        all_z &= out[i] == 'Z';
    CHECK(all_z);
    free(out);
}

static void decompress_longest_match_grows_the_buffer(void)
{
    /* BUGS #58. Parameters 0x18: k = 0, growth shift 3, so code 0x7F copies
     * 0x13 + ((0x7F - 0x13) << 3) + 3 = 886 bytes, more than the decoder's
     * old 0x200 growth margin. 'Z' then 7 such matches (distance 1): the
     * buffer starts at 4 * 20 + 1024 bytes and must grow before each copy. */
    static const uint8_t kStream[] = {0x18, 0x00, 0x00, 0x07, 0x01, 'Z',
                                      0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
                                      0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF};
    size_t len = 0;
    uint8_t* out = bouze_decompress(kStream, sizeof kStream, &len);
    REQUIRE(out != NULL);
    CHECK_EQ(len, 1 + 7 * 886);
    int all_z = 1;
    for (size_t i = 0; i < len; ++i)
        all_z &= out[i] == 'Z';
    CHECK(all_z);
    free(out);
}

static void decompress_rejects_bad_distance(void)
{
    /* A match before any output (distance 1, nothing written yet). */
    static const uint8_t kStream[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x84};
    size_t len = 0;
    CHECK(bouze_decompress(kStream, sizeof kStream, &len) == NULL);
    CHECK(bouze_decompress(kStream, 3, &len) == NULL);     /* shorter than the prefix */
}

static void header_and_chunks(void)
{
    static const uint8_t a[] = {1, 2, 3, 4, 5};
    uint8_t big[3000];
    for (size_t i = 0; i < sizeof big; ++i)
        big[i] = (uint8_t)(i * 7);
    const SynthChunk c[2] = {{1, a, sizeof a}, {4, big, sizeof big}};
    Buf f;
    buf_init(&f);
    synth_bzz(c, 2, &f);
    BouzeHeader h;
    REQUIRE(bouze_parse_header(f.p, f.n, &h));
    CHECK_EQ(h.version, 1);
    CHECK_EQ(h.count, 2);
    CHECK_EQ(h.file_off[0], 0x800);
    CHECK_EQ(h.file_off[1], 0x800 + h.aligned[0]);
    CHECK_EQ(h.aligned[0] % 0x800, 0);
    CHECK_EQ(bouze_find_chunk(&h, 4), 1);
    CHECK_EQ(bouze_find_chunk(&h, 3), -1);
    size_t len = 0;
    uint8_t* out = bouze_decompress(f.p + h.file_off[1], h.size[1], &len);
    CHECK(out && len == sizeof big && memcmp(out, big, len) == 0);
    free(out);

    /* BUGS #58: chunk offsets that wrap past 4 GiB. aligned[0] = 0xFFFFF800
     * puts chunk 1 at 0x800 + 0xFFFFF800 = 0 in 32 bits: the header itself. */
    {
        uint8_t keep[4];
        memcpy(keep, f.p + 0x0C + 8, 4);
        const uint8_t wrap[4] = {0x00, 0xF8, 0xFF, 0xFF};
        memcpy(f.p + 0x0C + 8, wrap, 4);
        CHECK(!bouze_parse_header(f.p, f.n, &h));
        memcpy(f.p + 0x0C + 8, keep, 4);
        /* a chunk size that wraps with its offset */
        memcpy(keep, f.p + 0x0C + 12 + 4, 4);
        const uint8_t huge[4] = {0x00, 0xF8, 0xFF, 0xFF};
        memcpy(f.p + 0x0C + 12 + 4, huge, 4);
        CHECK(!bouze_parse_header(f.p, f.n, &h));
        memcpy(f.p + 0x0C + 12 + 4, keep, 4);
        REQUIRE(bouze_parse_header(f.p, f.n, &h));
    }

    /* Truncated file, too short a header, too many chunks. */
    CHECK(!bouze_parse_header(f.p, f.n - 0x800, &h));
    CHECK(!bouze_parse_header(f.p, 100, &h));
    f.p[8] = 17;
    CHECK(!bouze_parse_header(f.p, f.n, &h));
    f.p[8] = 0;
    CHECK(!bouze_parse_header(f.p, f.n, &h));
    buf_free(&f);
}

static void records_and_tags(void)
{
    /* 0x2C (not a kind: skipped), a resource record, a stray 0x2D, an
     * unhandled kind byte, a TIM-list record, then the end marker. */
    Buf s;
    buf_init(&s);
    put8(&s, 0x2C);
    put8(&s, 0x22);
    put8(&s, 0x27); put16(&s, 0x0033); put16(&s, 0x2D2D);   /* payload bytes that look like terminators */
    put8(&s, 0x25); put32(&s, 0x100); put32(&s, 0x40);
    put8(&s, 0x77);                                          /* unknown tag of a known kind: no payload */
    put8(&s, 0x2D);
    put8(&s, 0x2D);
    put8(&s, 0x99);
    put8(&s, 0x28); put8(&s, 0x29); put32(&s, 0); put32(&s, 8); put8(&s, 0x2D);
    put8(&s, 0x2E);

    size_t pos = 0;
    BouzeRecord r;
    REQUIRE(bouze_next_record(s.p, s.n, &pos, &r) == 1);
    CHECK_EQ(r.kind, 0x22);
    CHECK_EQ(r.start, 1);
    size_t tp = r.tags;
    BouzeTag t;
    REQUIRE(bouze_next_tag(s.p, &r, &tp, &t));
    CHECK_EQ(t.tag, 0x27);
    CHECK_EQ(t.len, 4);
    CHECK_EQ(bouze_u16(s.p + t.payload), 0x0033);
    REQUIRE(bouze_next_tag(s.p, &r, &tp, &t));
    CHECK_EQ(t.tag, 0x25);
    CHECK_EQ(t.len, 8);
    CHECK_EQ(bouze_u32(s.p + t.payload), 0x100);
    REQUIRE(bouze_next_tag(s.p, &r, &tp, &t));
    CHECK_EQ(t.tag, 0x77);
    CHECK_EQ(t.len, 0);
    CHECK(!bouze_next_tag(s.p, &r, &tp, &t));

    REQUIRE(bouze_next_record(s.p, s.n, &pos, &r) == 1);
    CHECK_EQ(r.kind, 0x28);
    CHECK_EQ(bouze_next_record(s.p, s.n, &pos, &r), 0);
    CHECK_EQ(s.p[pos], 0x2E);

    /* No terminator: malformed. */
    size_t p2 = 0;
    CHECK_EQ(bouze_next_record(s.p, 12, &p2, &r), -1);
    buf_free(&s);
}

static void actor_kinds(void)
{
    static const uint8_t kActor[] = {0x07, 0x08, 0x09, 0x0A, 0x63};
    for (size_t i = 0; i < sizeof kActor; ++i)
        CHECK(bouze_is_actor_kind(kActor[i]));
    CHECK(!bouze_is_actor_kind(0x22));
    CHECK(!bouze_is_actor_kind(0x00));
}

static void synthetic_level_parses(void)
{
    SynthLevel l;
    synth_level_build(&l);
    size_t pos = 0;
    BouzeRecord r;
    int k, actors = 0, resources = 0, tims = 0;
    while ((k = bouze_next_record(l.stream.p, l.stream.n, &pos, &r)) == 1) {
        actors += bouze_is_actor_kind(r.kind);
        resources += r.kind == BOUZE_KIND_RESOURCE;
        tims += r.kind == 0x28;
    }
    CHECK_EQ(k, 0);
    CHECK_EQ(pos, l.stream.n - 1);
    CHECK_EQ(actors, 3);
    CHECK_EQ(resources, 7);
    CHECK_EQ(tims, 1);
    synth_level_free(&l);
}

const TestCase kBouzeTests[] = {
    {"decompress_literals", decompress_literals},
    {"decompress_stops_at_the_token_count", decompress_stops_at_the_token_count},
    {"decompress_match", decompress_match},
    {"decompress_long_length", decompress_long_length},
    {"decompress_longest_match_grows_the_buffer", decompress_longest_match_grows_the_buffer},
    {"decompress_rejects_bad_distance", decompress_rejects_bad_distance},
    {"header_and_chunks", header_and_chunks},
    {"records_and_tags", records_and_tags},
    {"actor_kinds", actor_kinds},
    {"synthetic_level_parses", synthetic_level_parses},
};
const int kBouzeTestCount = (int)(sizeof(kBouzeTests) / sizeof(kBouzeTests[0]));
