/*
 * retarget.c on synthetic data: the fixed-point angle / matrix math, the
 * keyframe interpreter it mirrors, and a retarget of a skeleton onto
 * itself, which must reproduce the source's rotations.
 */
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "retarget.h"

static int32_t iabs(int32_t v)
{
    return v < 0 ? -v : v;
}

static int32_t mat_diff(const int32_t a[9], const int32_t b[9])
{
    int32_t m = 0;
    for (int i = 0; i < 9; ++i)
        if (iabs(a[i] - b[i]) > m)
            m = iabs(a[i] - b[i]);
    return m;
}

static void euler_round_trip(void)
{
    int worst = 0;
    for (int x = 0; x < 4096; x += 211)
        for (int y = 0; y < 4096; y += 223)
            for (int z = 0; z < 4096; z += 227) {
                const int16_t r[3] = {(int16_t)x, (int16_t)y, (int16_t)z};
                int32_t m[9], m2[9];
                int16_t back[3];
                rt_euler_to_matrix(r, m);
                rt_matrix_to_euler(m, back);
                rt_euler_to_matrix(back, m2);
                const int d = mat_diff(m, m2);
                if (d > worst)
                    worst = d;
                CHECK(back[0] >= 0 && back[0] < 4096 && back[1] >= 0 && back[1] < 4096);
            }
    /* one angle unit is ~25 in Q14 */
    CHECK(worst <= 64);
}

static void euler_matches_rotmatrix_layout(void)
{
    /* a quarter turn about y: x -> -z (Rx*Ry*Rz with Ry = [cy 0 sy; 0 1 0; -sy 0 cy]) */
    const int16_t r[3] = {0, 1024, 0};
    int32_t m[9];
    rt_euler_to_matrix(r, m);
    CHECK_EQ(m[2], 16384);
    CHECK_EQ(m[6], -16384);
    CHECK_EQ(m[4], 16384);
}

static void align_turns_a_onto_b(void)
{
    static const int32_t v[][3] = {
        {0, 52, 5}, {0, 20, 29}, {-15, -9, -13}, {60, 0, -3}, {0, -50, 17}, {1, 116, -62},
        {0, 7, 62}, {-36, 28, -16}, {0, -1, 0}, {0, 1, 0},
    };
    const int n = (int)(sizeof v / sizeof v[0]);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            int32_t c[9];
            rt_align(v[i], v[j], c);
            /* c * a ~ b in direction: compare cross and dot products (c * a
             * kept in Q14 so short vectors keep their precision) */
            int64_t ca[3];
            for (int k = 0; k < 3; ++k)
                ca[k] = (int64_t)c[k * 3] * v[i][0] + (int64_t)c[k * 3 + 1] * v[i][1] +
                        (int64_t)c[k * 3 + 2] * v[i][2];
            const int64_t cross[3] = {ca[1] * v[j][2] - ca[2] * v[j][1],
                                      ca[2] * v[j][0] - ca[0] * v[j][2],
                                      ca[0] * v[j][1] - ca[1] * v[j][0]};
            const int64_t dot = ca[0] * v[j][0] + ca[1] * v[j][1] + ca[2] * v[j][2];
            int64_t worst = 0;
            for (int k = 0; k < 3; ++k) {
                const int64_t a = cross[k] < 0 ? -cross[k] : cross[k];
                if (a > worst)
                    worst = a;
            }
            CHECK(dot > 0);
            CHECK(worst * 50 <= dot);    /* within ~1.2 degrees */
            /* orthonormal: c * c^T ~ I */
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b) {
                    int64_t s = 0;
                    for (int k = 0; k < 3; ++k)
                        s += (int64_t)c[a * 3 + k] * c[b * 3 + k];
                    s >>= 14;
                    CHECK(iabs((int32_t)s - (a == b ? 16384 : 0)) <= 64);
                }
        }
}

/* ---- synthetic blobs ---------------------------------------------------- */

typedef struct Blob {
    uint8_t b[4096];
    uint32_t len;
    uint32_t frame_hdr;
    int frames;
    int cmds;
} Blob;

static void b16(Blob* x, uint16_t v)
{
    x->b[x->len++] = (uint8_t)v;
    x->b[x->len++] = (uint8_t)(v >> 8);
}

static void blob_begin(Blob* x)
{
    memset(x, 0, sizeof *x);
    b16(x, 0x50); b16(x, 4); b16(x, 0); b16(x, 0);    /* header: ticks 2, frames set later */
}

static void frame_begin(Blob* x)
{
    x->frame_hdr = x->len;
    b16(x, 0); b16(x, 0); b16(x, (uint16_t)x->frames); b16(x, 0);
    x->cmds = 0;
}

static void frame_end(Blob* x)
{
    const uint32_t body = x->len - x->frame_hdr - 8;
    x->b[x->frame_hdr] = (uint8_t)(body / 2 + 2);
    x->b[x->frame_hdr + 1] = (uint8_t)((body / 2 + 2) >> 8);
    x->b[x->frame_hdr + 2] = (uint8_t)x->cmds;
    x->b[x->frame_hdr + 3] = (uint8_t)(x->cmds >> 8);
    ++x->frames;
    x->b[4] = (uint8_t)x->frames;
    x->b[5] = (uint8_t)(x->frames >> 8);
}

static void cmd(Blob* x, uint16_t c, const uint16_t* args, int n)
{
    b16(x, c);
    for (int i = 0; i < n; ++i)
        b16(x, args[i]);
    ++x->cmds;
}

/* 0x400 full pose (rotation packed, translation s8 x | s8 z, s16 y) */
static void pose(Blob* x, int node, int rx, int ry, int rz, int tx, int ty, int tz)
{
    const uint16_t a[4] = {
        (uint16_t)((rx & 0xFFF) | (((rz >> 4) & 0xF) << 12)),
        (uint16_t)((ry & 0xFFF) | ((rz & 0xF) << 12)),
        (uint16_t)((uint8_t)tx | ((uint16_t)(uint8_t)tz << 8)),
        (uint16_t)ty,
    };
    cmd(x, (uint16_t)(node | 0x400 | (((rz >> 8) & 0xF) << 12)), a, 4);
}

/* A 5-node chain: 1 root, 2 hips (parent 1), 3 chest (2), 4 thigh (2), 5 toe (4). */
static void skeleton(Blob* s)
{
    blob_begin(s);
    frame_begin(s);
    cmd(s, 0x0801, NULL, 0);
    const uint16_t p2 = 0x0101, p3 = 0x0202, p4 = 0x0203, p5 = 0x0404;
    cmd(s, 0x0C02, &p2, 1);
    cmd(s, 0x0C03, &p3, 1);
    cmd(s, 0x0C04, &p4, 1);
    cmd(s, 0x0C05, &p5, 1);
    frame_end(s);
}

static void rest(Blob* r)
{
    blob_begin(r);
    frame_begin(r);
    pose(r, 1, 0, 0, 0, 0, 0, 0);
    pose(r, 2, 0, 0, 0, 0, -100, 0);
    pose(r, 3, 0, 0, 0, 0, -50, 10);
    pose(r, 4, 0, 0, 0, 10, 0, 0);
    pose(r, 5, 0, 0, 0, 0, 100, 0);
    frame_end(r);
}

static void moving(Blob* m, int frames)
{
    blob_begin(m);
    for (int f = 0; f < frames; ++f) {
        frame_begin(m);
        pose(m, 1, 0, 0, 0, 0, 0, 0);
        pose(m, 2, 0, (f * 40) & 4095, 0, 0, -100, 0);
        pose(m, 3, (100 + f * 30) & 4095, 0, (f * 17) & 4095, 0, -50, 10);
        pose(m, 4, (3000 + f * 50) & 4095, 200, 0, 10, 0, 0);
        pose(m, 5, 0, 0, 0, 0, 100, 0);
        frame_end(m);
    }
}

static const RtJoint kSelf[] = {
    {1, 1, 0, 0}, {2, 2, 3, 3}, {3, 3, 0, 0}, {4, 4, 5, 5}, {5, 5, 0, 0},
};
static const uint8_t kToes[2] = {5, 5};

static void retarget_onto_itself_keeps_the_rotations(void)
{
    static Blob s, r, m;
    skeleton(&s);
    rest(&r);
    moving(&m, 6);
    uint8_t parent[RT_MAX_NODES];
    CHECK_EQ(rt_skeleton(s.b, s.len, parent, RT_MAX_NODES), 5);
    CHECK_EQ(parent[5], 4);
    RtRig* rig = (RtRig*)malloc(sizeof *rig);
    CHECK(rig && rt_rig_build(rig, s.b, s.len, r.b, r.len, s.b, s.len, r.b, r.len, kSelf, 5, 2, 2,
                              kToes, kToes));
    if (!rig)
        return;
    static uint8_t out[8192];
    const uint8_t hdr[8] = {0x50, 0, 4, 0, 6, 0, 0, 0};
    const uint32_t len = rt_retarget(rig, m.b, m.len, hdr, 6, out, sizeof out);
    CHECK(len > 8);
    CHECK_EQ(out[4], 6);
    /* run both and compare every frame's rotations */
    RtState a[RT_MAX_NODES], b[RT_MAX_NODES];
    for (int f = 1; f <= 6; ++f) {
        for (int i = 0; i < RT_MAX_NODES; ++i) {
            memset(&a[i], 0, sizeof a[i]);
            memset(&b[i], 0, sizeof b[i]);
        }
        CHECK(rt_simulate(m.b, m.len, f, a, 5));
        CHECK(rt_simulate(out, len, f, b, 5));
        for (int n = 2; n <= 5; ++n) {
            int32_t ma[9], mb[9];
            rt_euler_to_matrix(a[n].rot, ma);
            rt_euler_to_matrix(b[n].rot, mb);
            CHECK(mat_diff(ma, mb) <= 80);
        }
        CHECK_EQ(b[4].tr[0], 10);           /* the target's own bone offsets */
        CHECK_EQ(b[5].tr[1], 100);
    }
    /* the still pose: frame 0, then empty frames */
    const uint32_t still = rt_still(rig, m.b, m.len, 3, hdr, 4, out, sizeof out);
    CHECK(still > 8);
    CHECK_EQ(out[4], 4);
    free(rig);
}

static void retarget_resamples_to_the_asked_length(void)
{
    static Blob s, r, m;
    skeleton(&s);
    rest(&r);
    moving(&m, 9);
    RtRig* rig = (RtRig*)malloc(sizeof *rig);
    CHECK(rig && rt_rig_build(rig, s.b, s.len, r.b, r.len, s.b, s.len, r.b, r.len, kSelf, 5, 2, 2,
                              kToes, kToes));
    if (!rig)
        return;
    static uint8_t out[8192];
    const uint8_t hdr[8] = {0x50, 0, 2, 0, 0, 0, 0, 0};
    for (int frames = 1; frames <= 20; frames += 3) {
        const uint32_t len = rt_retarget(rig, m.b, m.len, hdr, (uint16_t)frames, out, sizeof out);
        CHECK(len > 8);
        CHECK_EQ(out[4] | (out[5] << 8), frames);
        CHECK_EQ(out[2], 2);                 /* the header's ticks are kept */
        /* every frame header: halfwords + 2, index */
        uint32_t p = 8;
        for (int f = 0; f < frames; ++f) {
            const uint16_t hw = (uint16_t)(out[p] | (out[p + 1] << 8));
            CHECK_EQ(out[p + 4] | (out[p + 5] << 8), f);
            p += 8 + 2u * (uint32_t)(hw - 2);
        }
        CHECK_EQ(p, len);
    }
    CHECK_EQ(rt_retarget(rig, m.b, m.len, hdr, 9, out, 16), 0u);   /* does not fit */
    free(rig);
}

/* Root commands (0xA00) in a retargeted blob: walks every frame's commands
 * with the interpreter's argument counts (tools/bouze/keyframes.py). */
static int test_nargs(uint16_t c)
{
    const unsigned op = c & 0xF00u, fl = c & 0xF000u;
    switch (op) {
    case 0x400: case 0x700: return 4;
    case 0x200: case 0x300: case 0xC00: return 1;
    case 0x600: case 0xE00: return 2;
    case 0x900: return (fl & 0x1000u) ? 4 : 7;
    case 0x100: case 0xD00:
        return ((fl & 0x2000u) ? 3 : 0) + ((fl & 0x4000u) ? 3 : 0) +
               ((fl & 0x8000u) ? 3 : (fl & 0x1000u) ? 2 : 0);
    case 0xB00:
        return (((fl & 0x2000u) ? 3 : 0) + ((fl & 0x4000u) ? 3 : 0) + ((fl & 0x8000u) ? 3 : 0) + 1) >> 1;
    default: return 0;
    }
}

static int root_commands(const uint8_t* b, uint32_t len)
{
    const int frames = b[4] | (b[5] << 8);
    uint32_t p = 8;
    int roots = 0;
    for (int f = 0; f < frames && p + 8u <= len; ++f) {
        const unsigned count = (unsigned)(b[p + 2] | (b[p + 3] << 8));
        uint32_t q = p + 8;
        for (unsigned k = 0; k < count && q + 2u <= len; ++k) {
            const uint16_t c = (uint16_t)(b[q] | (b[q + 1] << 8));
            if ((c & 0xF00u) == 0xA00u)
                ++roots;
            q += 2u + 2u * (uint32_t)test_nargs(c);
        }
        p += 8u + 2u * (uint32_t)((b[p] | (b[p + 1] << 8)) - 2);
    }
    return roots;
}

static void retarget_can_drop_root_commands(void)
{
    static Blob s, r, m;
    skeleton(&s);
    rest(&r);
    blob_begin(&m);
    for (int f = 0; f < 4; ++f) {           /* poses plus a root command on node 5 */
        frame_begin(&m);
        pose(&m, 1, 0, 0, 0, 0, 0, 0);
        pose(&m, 2, 0, (f * 40) & 4095, 0, 0, -100, 0);
        pose(&m, 5, 0, 0, 0, 0, 100, 0);
        cmd(&m, 0x0A05, NULL, 0);
        frame_end(&m);
    }
    CHECK_EQ(root_commands(m.b, m.len), 4);
    RtRig* rig = (RtRig*)malloc(sizeof *rig);
    CHECK(rig && rt_rig_build(rig, s.b, s.len, r.b, r.len, s.b, s.len, r.b, r.len, kSelf, 5, 2, 2,
                              kToes, kToes));
    if (!rig)
        return;
    CHECK_EQ(rig->no_root, 0);
    static uint8_t out[8192];
    const uint8_t hdr[8] = {0x50, 0, 2, 0, 4, 0, 0, 0};
    uint32_t len = rt_retarget(rig, m.b, m.len, hdr, 4, out, sizeof out);
    CHECK(len > 8);
    CHECK_EQ(root_commands(out, len), 4);   /* kept, node 5 -> 5 */
    rig->no_root = 1;
    len = rt_retarget(rig, m.b, m.len, hdr, 4, out, sizeof out);
    CHECK(len > 8);
    CHECK_EQ(root_commands(out, len), 0);
    len = rt_still(rig, m.b, m.len, 0, hdr, 3, out, sizeof out);
    CHECK(len > 8);
    CHECK_EQ(root_commands(out, len), 0);
    free(rig);
}

const TestCase kRetargetTests[] = {
    {"retarget_can_drop_root_commands", retarget_can_drop_root_commands},
    {"euler_round_trip", euler_round_trip},
    {"euler_matches_rotmatrix_layout", euler_matches_rotmatrix_layout},
    {"align_turns_a_onto_b", align_turns_a_onto_b},
    {"retarget_onto_itself_keeps_the_rotations", retarget_onto_itself_keeps_the_rotations},
    {"retarget_resamples_to_the_asked_length", retarget_resamples_to_the_asked_length},
};
const int kRetargetTestCount = (int)(sizeof(kRetargetTests) / sizeof(kRetargetTests[0]));
