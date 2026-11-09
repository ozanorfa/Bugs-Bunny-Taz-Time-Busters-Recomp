/*
 * Animation retargeting (see retarget.h). Integer math only.
 */
#include "retarget.h"

#include <stdlib.h>
#include <string.h>

#include "rt_sin_table.h"

#define ONE 16384

static uint16_t rd16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void wr16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static int8_t s8(unsigned v)
{
    return (int8_t)(uint8_t)v;
}

/* u16 arguments after a command word (tools/bouze/keyframes.py nargs). */
static int nargs(uint16_t cmd)
{
    const unsigned op = cmd & 0xF00u, fl = cmd & 0xF000u;
#define A3(bit) ((fl & (bit)) ? 3 : 0)
    switch (op) {
    case 0x100: case 0xD00:
        return A3(0x2000u) + A3(0x4000u) + ((fl & 0x8000u) ? 3 : (fl & 0x1000u) ? 2 : 0);
    case 0x200: case 0x300: case 0xC00:
        return 1;
    case 0x400: case 0x700:
        return 4;
    case 0x600: case 0xE00:
        return 2;
    case 0x900:
        return (fl & 0x1000u) ? 4 : 7;
    case 0xB00:
        return (A3(0x2000u) + A3(0x4000u) + A3(0x8000u) + 1) >> 1;
    default:
        return 0;
    }
#undef A3
}

/* Walks the frames of a blob: the offset of frame `f` (its 8-byte header)
 * relative to the blob, or 0 if malformed. */
static uint32_t frame_offset(const uint8_t* blob, uint32_t size, int f)
{
    if (size < 8u)
        return 0;
    uint32_t p = 8;
    for (int i = 0; i < f; ++i) {
        if (p + 8u > size)
            return 0;
        const unsigned count = rd16(blob + p + 2);
        p += 8;
        for (unsigned c = 0; c < count; ++c) {
            if (p + 2u > size)
                return 0;
            p += 2u + 2u * (uint32_t)nargs(rd16(blob + p));
        }
    }
    return p + 8u <= size ? p : 0;
}

int rt_skeleton(const uint8_t* blob, uint32_t size, uint8_t* parent, int max)
{
    const uint32_t f0 = frame_offset(blob, size, 0);
    if (!f0)
        return -1;
    memset(parent, 0, (size_t)max);
    const unsigned count = rd16(blob + f0 + 2);
    uint32_t p = f0 + 8;
    int n = 0;
    for (unsigned c = 0; c < count; ++c) {
        if (p + 2u > size)
            return -1;
        const uint16_t cmd = rd16(blob + p);
        const int node = cmd & 0xFF;
        const unsigned op = cmd & 0xF00u;
        const int na = nargs(cmd);
        if (p + 2u + 2u * (uint32_t)na > size)
            return -1;
        if (node > 0 && node < max) {
            if ((op == 0x800u || op == 0xC00u) && !(cmd & 0x1000u)) {
                parent[node] = op == 0xC00u ? (uint8_t)(rd16(blob + p + 2) >> 8) : 0;
                if (node > n)
                    n = node;
            } else if (op == 0x300u) {
                parent[node] = (uint8_t)rd16(blob + p + 2);
            }
        }
        p += 2u + 2u * (uint32_t)na;
    }
    return n;
}

static void apply(RtState* st, int n, const uint8_t* c)
{
    const uint16_t cmd = rd16(c);
    const int node = cmd & 0xFF;
    const unsigned op = cmd & 0xF00u, fl = cmd & 0xF000u;
    if (node < 1 || node > n)
        return;
    RtState* s = &st[node];
    const uint8_t* a = c + 2;
#define ARG(i) rd16(a + 2 * (i))
    if (op == 0x400u || op == 0x700u || op == 0xE00u) {
        const uint16_t a0 = ARG(0), a1 = ARG(1);
        s->rot[0] = (int16_t)(a0 & 0xFFFu);
        s->rot[1] = (int16_t)(a1 & 0xFFFu);
        s->rot[2] = (int16_t)(((a0 >> 12) << 4) | (a1 >> 12) | ((cmd >> 4) & 0xF00u));
        if (op != 0xE00u) {
            const uint16_t a2 = ARG(2);
            s->tr[0] = s8(a2 & 0xFFu);
            s->tr[1] = (int16_t)ARG(3);
            s->tr[2] = s8(a2 >> 8);
        }
        if (op == 0x400u)
            s->scale[0] = s->scale[1] = s->scale[2] = 0x1000;
    } else if (op == 0x100u || op == 0xD00u) {
        int k = 0;
        if (fl & 0x2000u) {
            for (int i = 0; i < 3; ++i)
                s->rot[i] = (int16_t)ARG(i);
            k = 3;
        } else if (op == 0xD00u) {
            s->rot[0] = s->rot[1] = s->rot[2] = 0;
        }
        if (fl & 0x4000u) {
            for (int i = 0; i < 3; ++i)
                s->scale[i] = (int16_t)ARG(k + i);
            k += 3;
        } else if (op == 0xD00u) {
            s->scale[0] = s->scale[1] = s->scale[2] = 0x1000;
        }
        if (fl & 0x8000u) {
            for (int i = 0; i < 3; ++i)
                s->tr[i] = (int16_t)ARG(k + i);
        } else if (fl & 0x1000u) {
            const uint16_t t = ARG(k);
            s->tr[0] = s8(t & 0xFFu);
            s->tr[1] = (int16_t)ARG(k + 1);
            s->tr[2] = s8(t >> 8);
        }
    } else if (op == 0xB00u) {
        int i = 0;
        if (fl & 0x2000u) {
            for (int j = 0; j < 3; ++j)
                s->rot[j] = (int16_t)(s->rot[j] + s8(a[i + j]));
            i += 3;
        }
        if (fl & 0x4000u) {
            for (int j = 0; j < 3; ++j)
                s->scale[j] = (int16_t)(s->scale[j] + s8(a[i + j]));
            i += 3;
        }
        if (fl & 0x8000u) {
            for (int j = 0; j < 3; ++j)
                s->tr[j] = (int16_t)(s->tr[j] + s8(a[i + j]));
        }
    }
#undef ARG
}

static void reset_states(RtState* st, int n)
{
    for (int i = 0; i <= n; ++i) {
        memset(&st[i], 0, sizeof st[i]);
        st[i].scale[0] = st[i].scale[1] = st[i].scale[2] = 0x1000;
    }
}

/* Runs one frame at blob offset `p` (its header). Returns the offset after
 * it, or 0. */
static uint32_t run_frame(const uint8_t* blob, uint32_t size, uint32_t p, RtState* st, int n)
{
    if (p + 8u > size)
        return 0;
    const unsigned count = rd16(blob + p + 2);
    p += 8;
    for (unsigned c = 0; c < count; ++c) {
        if (p + 2u > size)
            return 0;
        const int na = nargs(rd16(blob + p));
        if (p + 2u + 2u * (uint32_t)na > size)
            return 0;
        apply(st, n, blob + p);
        p += 2u + 2u * (uint32_t)na;
    }
    return p;
}

int rt_simulate(const uint8_t* blob, uint32_t size, int frames_to_run, RtState* st, int n)
{
    uint32_t p = 8;
    for (int f = 0; f < frames_to_run; ++f) {
        p = run_frame(blob, size, p, st, n);
        if (!p)
            return 0;
    }
    return 1;
}

/* ---- fixed-point trigonometry and matrices ------------------------------ */

static int32_t sin14(int a)
{
    a &= 4095;
    if (a <= 1024)
        return kRtSinQ14[a];
    if (a <= 2048)
        return kRtSinQ14[2048 - a];
    if (a <= 3072)
        return -kRtSinQ14[a - 2048];
    return -kRtSinQ14[4096 - a];
}

static int32_t cos14(int a)
{
    return sin14(a + 1024);
}

static int32_t mul14(int64_t a, int64_t b)
{
    return (int32_t)((a * b) >> 14);
}

void rt_euler_to_matrix(const int16_t rot[3], int32_t m[9])
{
    const int32_t sx = sin14(rot[0]), cx = cos14(rot[0]);
    const int32_t sy = sin14(rot[1]), cy = cos14(rot[1]);
    const int32_t sz = sin14(rot[2]), cz = cos14(rot[2]);
    /* RotMatrix (Ghidra 0x80074274): Rx * Ry * Rz */
    m[0] = mul14(cz, cy);
    m[1] = -mul14(sz, cy);
    m[2] = sy;
    m[3] = mul14(sz, cx) + mul14(mul14(cz, sy), sx);
    m[4] = mul14(cz, cx) - mul14(mul14(sz, sy), sx);
    m[5] = -mul14(sx, cy);
    m[6] = mul14(sz, sx) - mul14(mul14(cz, sy), cx);
    m[7] = mul14(cz, sx) + mul14(mul14(sz, sy), cx);
    m[8] = mul14(cx, cy);
}

/* atan2 in the game's unit: the angle whose sine is proportional to y and
 * cosine to x, 0..4095. Binary search over the table. */
static int atan2_units(int64_t y, int64_t x)
{
    if (x == 0 && y == 0)
        return 0;
    const int64_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    /* smallest t in 0..1024 with sin(t) * ax >= cos(t) * ay */
    int lo = 0, hi = 1024;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if ((int64_t)kRtSinQ14[mid] * ax >= (int64_t)kRtSinQ14[1024 - mid] * ay)
            hi = mid;
        else
            lo = mid + 1;
    }
    int t = lo;
    if (t > 0) {
        const int64_t e1 = (int64_t)kRtSinQ14[t] * ax - (int64_t)kRtSinQ14[1024 - t] * ay;
        const int64_t e0 = (int64_t)kRtSinQ14[1024 - (t - 1)] * ay - (int64_t)kRtSinQ14[t - 1] * ax;
        if (e0 < e1)
            t -= 1;
    }
    int a;
    if (x >= 0)
        a = y >= 0 ? t : 4096 - t;
    else
        a = y >= 0 ? 2048 - t : 2048 + t;
    return a & 4095;
}

static int asin_units(int32_t s)
{
    const int neg = s < 0;
    int64_t v = neg ? -(int64_t)s : s;
    if (v > ONE)
        v = ONE;
    int lo = 0, hi = 1024;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (kRtSinQ14[mid] >= v)
            hi = mid;
        else
            lo = mid + 1;
    }
    int t = lo;
    if (t > 0 && v - kRtSinQ14[t - 1] < kRtSinQ14[t] - v)
        t -= 1;
    return neg ? -t : t;
}

void rt_matrix_to_euler(const int32_t m[9], int16_t rot[3])
{
    const int vy = asin_units(m[2]);
    int vx, vz;
    if (cos14(vy) > 64) {
        vx = atan2_units(-(int64_t)m[5], m[8]);
        vz = atan2_units(-(int64_t)m[1], m[0]);
    } else {
        vz = 0;
        vx = atan2_units(m[7], m[4]);
    }
    rot[0] = (int16_t)(vx & 4095);
    rot[1] = (int16_t)(vy & 4095);
    rot[2] = (int16_t)(vz & 4095);
}

static void mat_mul(const int32_t a[9], const int32_t b[9], int32_t out[9])
{
    int32_t r[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            int64_t s = 0;
            for (int k = 0; k < 3; ++k)
                s += (int64_t)a[i * 3 + k] * b[k * 3 + j];
            r[i * 3 + j] = (int32_t)(s >> 14);
        }
    memcpy(out, r, sizeof r);
}

static void mat_transpose(const int32_t a[9], int32_t out[9])
{
    int32_t r[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 3 + j] = a[j * 3 + i];
    memcpy(out, r, sizeof r);
}

static void mat_identity(int32_t m[9])
{
    memset(m, 0, 9 * sizeof m[0]);
    m[0] = m[4] = m[8] = ONE;
}

static int64_t isqrt64(int64_t v)
{
    if (v <= 0)
        return 0;
    int64_t r = 0, bit = (int64_t)1 << 62;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* Unit vector in Q14. The length is taken in Q14 too (the square root of
 * the squared length scaled by 2^28): bone offsets are small integers, and
 * a whole-unit length (52 for 52.8) left the "unit" vectors up to 2% long,
 * which bent the alignment of wide angles out of a rotation. */
static int normalize(const int32_t v[3], int64_t out[3])
{
    const int64_t sq = (int64_t)v[0] * v[0] + (int64_t)v[1] * v[1] + (int64_t)v[2] * v[2];
    if (sq == 0 || sq > ((int64_t)1 << 34))
        return 0;
    const int64_t len_q14 = isqrt64(sq << 28);
    if (len_q14 == 0)
        return 0;
    for (int i = 0; i < 3; ++i)
        out[i] = ((int64_t)v[i] << 28) / len_q14;
    return 1;
}

/* Shortest-arc rotation between unit vectors (Q14) whose angle is at most
 * a quarter turn and a bit (u . w >= 0 keeps 1 / (1 + cos) small): Rodrigues,
 * I + [v]x + [v]x^2 / (1 + c) with v = u x w, c = u . w. */
static void arc(const int64_t u[3], const int64_t w[3], int32_t c[9])
{
    const int64_t v[3] = {
        (u[1] * w[2] - u[2] * w[1]) >> 14,
        (u[2] * w[0] - u[0] * w[2]) >> 14,
        (u[0] * w[1] - u[1] * w[0]) >> 14,
    };
    const int64_t d = (u[0] * w[0] + u[1] * w[1] + u[2] * w[2]) >> 14;
    const int64_t k[9] = {0, -v[2], v[1], v[2], 0, -v[0], -v[1], v[0], 0};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            int64_t k2 = 0;
            for (int t = 0; t < 3; ++t)
                k2 += k[i * 3 + t] * k[t * 3 + j];
            k2 >>= 14;
            c[i * 3 + j] = (int32_t)((i == j ? ONE : 0) + k[i * 3 + j] + k2 * ONE / (ONE + d));
        }
}

void rt_align(const int32_t a[3], const int32_t b[3], int32_t c[9])
{
    int64_t u[3], w[3];
    mat_identity(c);
    if (!normalize(a, u) || !normalize(b, w))
        return;
    const int64_t d = (u[0] * w[0] + u[1] * w[1] + u[2] * w[2]) >> 14;
    if (d >= 0) {
        arc(u, w, c);
        return;
    }
    /* Wide angle: half a turn about the axis across both (a x b; any axis
     * across a when they are opposite) takes a to -a, then the short arc
     * from -a to b. */
    int32_t axis[3] = {
        (int32_t)((u[1] * w[2] - u[2] * w[1]) >> 14),
        (int32_t)((u[2] * w[0] - u[0] * w[2]) >> 14),
        (int32_t)((u[0] * w[1] - u[1] * w[0]) >> 14),
    };
    if ((int64_t)axis[0] * axis[0] + (int64_t)axis[1] * axis[1] + (int64_t)axis[2] * axis[2] <
        (int64_t)256 * 256) {
        const int64_t ax = u[0] < 0 ? -u[0] : u[0], ay = u[1] < 0 ? -u[1] : u[1];
        if (ax <= ay) {
            axis[0] = 0; axis[1] = (int32_t)-u[2]; axis[2] = (int32_t)u[1];
        } else {
            axis[0] = (int32_t)-u[2]; axis[1] = 0; axis[2] = (int32_t)u[0];
        }
    }
    int64_t n[3];
    if (!normalize(axis, n))
        return;
    int32_t flip[9], rest[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            flip[i * 3 + j] = (int32_t)((2 * n[i] * n[j]) >> 14) - (i == j ? ONE : 0);
    const int64_t neg[3] = {-u[0], -u[1], -u[2]};
    arc(neg, w, rest);
    mat_mul(rest, flip, c);
}

/* ---- the rig ----------------------------------------------------------- */

static int blob_frames(const uint8_t* blob, uint32_t size)
{
    return size >= 8u ? rd16(blob + 4) : 0;
}

int rt_rig_build(RtRig* rig,
                 const uint8_t* src_setup, uint32_t src_setup_size,
                 const uint8_t* src_rest, uint32_t src_rest_size,
                 const uint8_t* dst_setup, uint32_t dst_setup_size,
                 const uint8_t* dst_rest, uint32_t dst_rest_size,
                 const RtJoint* joints, int joint_count,
                 uint8_t hips_src, uint8_t hips_dst,
                 const uint8_t toes_src[2], const uint8_t toes_dst[2])
{
    memset(rig, 0, sizeof *rig);
    rig->src_n = rt_skeleton(src_setup, src_setup_size, rig->src_parent, RT_MAX_NODES);
    rig->dst_n = rt_skeleton(dst_setup, dst_setup_size, rig->dst_parent, RT_MAX_NODES);
    if (rig->src_n <= 0 || rig->dst_n <= 0)
        return 0;
    RtState src_st[RT_MAX_NODES];
    reset_states(src_st, rig->src_n);
    reset_states(rig->dst_rest, rig->dst_n);
    if (!rt_simulate(src_rest, src_rest_size, 1, src_st, rig->src_n) ||
        !rt_simulate(dst_rest, dst_rest_size, 1, rig->dst_rest, rig->dst_n))
        return 0;
    for (int i = 0; i < joint_count; ++i) {
        const RtJoint* j = &joints[i];
        if (j->src < 1 || j->src > rig->src_n || j->dst < 1 || j->dst > rig->dst_n)
            return 0;
        rig->src_of[j->dst] = j->src;
        rig->dst_of[j->src] = j->dst;
    }
    /* corrections, parents first (a node's parent has a smaller number) */
    for (int d = 1; d <= rig->dst_n; ++d) {
        const int p = rig->dst_parent[d];
        if (p > 0 && p < d)
            memcpy(rig->corr[d], rig->corr[p], sizeof rig->corr[d]);
        else
            mat_identity(rig->corr[d]);
    }
    for (int d = 1; d <= rig->dst_n; ++d) {
        for (int i = 0; i < joint_count; ++i) {
            const RtJoint* j = &joints[i];
            if (j->dst != d || !j->src_child || !j->dst_child)
                continue;
            const int32_t a[3] = {rig->dst_rest[j->dst_child].tr[0], rig->dst_rest[j->dst_child].tr[1],
                                  rig->dst_rest[j->dst_child].tr[2]};
            const int32_t b[3] = {src_st[j->src_child].tr[0], src_st[j->src_child].tr[1],
                                  src_st[j->src_child].tr[2]};
            rt_align(a, b, rig->corr[d]);
        }
        /* children without a correction of their own take this one */
        for (int c = d + 1; c <= rig->dst_n; ++c) {
            int own = 0;
            for (int i = 0; i < joint_count; ++i)
                if (joints[i].dst == c && joints[i].src_child && joints[i].dst_child)
                    own = 1;
            if (!own && rig->dst_parent[c] == d)
                memcpy(rig->corr[c], rig->corr[d], sizeof rig->corr[c]);
        }
    }
    rig->hips_src = hips_src;
    rig->hips_dst = hips_dst;
    memcpy(rig->toes_src, toes_src, 2);
    memcpy(rig->toes_dst, toes_dst, 2);
    return 1;
}

/* Lowest point (largest y: y grows downwards) of two nodes, from the
 * actor's origin, by forward kinematics over the states. */
static int32_t toe_y(const uint8_t* parent, const RtState* st, int n, const uint8_t toes[2])
{
    int32_t w[RT_MAX_NODES][9];
    int64_t pos[RT_MAX_NODES][3];
    for (int i = 1; i <= n; ++i) {
        int32_t r[9];
        rt_euler_to_matrix(st[i].rot, r);
        const int p = parent[i];
        if (p > 0 && p < i) {
            mat_mul(w[p], r, w[i]);
            for (int k = 0; k < 3; ++k)
                pos[i][k] = pos[p][k] + (((int64_t)w[p][k * 3 + 0] * st[i].tr[0] +
                                          (int64_t)w[p][k * 3 + 1] * st[i].tr[1] +
                                          (int64_t)w[p][k * 3 + 2] * st[i].tr[2]) >> 14);
        } else {
            memcpy(w[i], r, sizeof r);
            for (int k = 0; k < 3; ++k)
                pos[i][k] = st[i].tr[k];
        }
    }
    int64_t y0 = (toes[0] >= 1 && toes[0] <= n) ? pos[toes[0]][1] : 0;
    int64_t y1 = (toes[1] >= 1 && toes[1] <= n) ? pos[toes[1]][1] : y0;
    return (int32_t)(y0 > y1 ? y0 : y1);
}

/* The target's local states for one source frame. */
static void pose_for(const RtRig* rig, const RtState* src, RtState* out)
{
    memcpy(out, rig->dst_rest, sizeof(RtState) * (size_t)(rig->dst_n + 1));
    for (int d = 1; d <= rig->dst_n; ++d) {
        const int s = rig->src_of[d];
        out[d].scale[0] = out[d].scale[1] = out[d].scale[2] = 0x1000;
        if (!s)
            continue;
        int32_t rs[9], cpt[9], m[9];
        rt_euler_to_matrix(src[s].rot, rs);
        const int p = rig->dst_parent[d];
        if (p > 0)
            mat_transpose(rig->corr[p], cpt);
        else
            mat_identity(cpt);
        mat_mul(cpt, rs, m);
        mat_mul(m, rig->corr[d], m);
        rt_matrix_to_euler(m, out[d].rot);
    }
    if (rig->hips_dst && rig->hips_src) {
        for (int k = 0; k < 3; ++k)
            out[rig->hips_dst].tr[k] = src[rig->hips_src].tr[k];
        const int32_t want = toe_y(rig->src_parent, src, rig->src_n, rig->toes_src);
        const int32_t got = toe_y(rig->dst_parent, out, rig->dst_n, rig->toes_dst);
        int32_t y = out[rig->hips_dst].tr[1] + (want - got);
        if (y < -32768) y = -32768;
        if (y > 32767) y = 32767;
        out[rig->hips_dst].tr[1] = (int16_t)y;
    }
}

/* ---- writing ------------------------------------------------------------ */

typedef struct Out {
    uint8_t* buf;
    uint32_t cap, len;
    int ok;
} Out;

static void put(Out* o, uint16_t v)
{
    if (o->len + 2u > o->cap) {
        o->ok = 0;
        return;
    }
    wr16(o->buf + o->len, v);
    o->len += 2;
}

static int fits8(int v)
{
    return v >= -128 && v <= 127;
}

/* An absolute pose for one node: 0x400 when the translation's x / z fit a
 * byte (also sets scale 1.0), else 0x100 with rotation, scale and
 * translation. */
static void put_pose(Out* o, int node, const RtState* s)
{
    const unsigned r0 = (unsigned)s->rot[0] & 0xFFFu, r1 = (unsigned)s->rot[1] & 0xFFFu,
                   r2 = (unsigned)s->rot[2] & 0xFFFu;
    if (fits8(s->tr[0]) && fits8(s->tr[2])) {
        put(o, (uint16_t)(node | 0x400u | ((r2 >> 8) << 12)));
        put(o, (uint16_t)(r0 | (((r2 >> 4) & 0xFu) << 12)));
        put(o, (uint16_t)(r1 | ((r2 & 0xFu) << 12)));
        put(o, (uint16_t)((uint8_t)s->tr[0] | ((uint16_t)(uint8_t)s->tr[2] << 8)));
        put(o, (uint16_t)s->tr[1]);
    } else {
        put(o, (uint16_t)(node | 0x100u | 0xE000u));
        put(o, (uint16_t)r0); put(o, (uint16_t)r1); put(o, (uint16_t)r2);
        put(o, 0x1000); put(o, 0x1000); put(o, 0x1000);
        put(o, (uint16_t)s->tr[0]); put(o, (uint16_t)s->tr[1]); put(o, (uint16_t)s->tr[2]);
    }
}

/* The smallest change of angle a -> b (mod 4096), -2048..2047. */
static int angle_delta(int a, int b)
{
    int d = (b - a) & 4095;
    return d >= 2048 ? d - 4096 : d;
}

/* Writes the commands that turn `have` into `want` for one node, updating
 * `have` exactly as the interpreter will. Returns the commands written. */
static int put_change(Out* o, int node, RtState* have, const RtState* want, int absolute)
{
    const int tr_same = !memcmp(have->tr, want->tr, sizeof want->tr);
    int dr[3], rot_same = 1, small = 1;
    for (int i = 0; i < 3; ++i) {
        dr[i] = angle_delta(have->rot[i] & 4095, want->rot[i] & 4095);
        rot_same &= dr[i] == 0 && (have->rot[i] & 4095) == (have->rot[i]);
        const int next = have->rot[i] + dr[i];
        small &= fits8(dr[i]) && next >= 0 && next <= 4095;
    }
    if (!absolute && rot_same && tr_same)
        return 0;
    if (!absolute && tr_same && small) {
        put(o, (uint16_t)(node | 0xB00u | 0x2000u));
        put(o, (uint16_t)((uint8_t)dr[0] | ((uint16_t)(uint8_t)dr[1] << 8)));
        put(o, (uint16_t)(uint8_t)dr[2]);
        for (int i = 0; i < 3; ++i)
            have->rot[i] = (int16_t)(have->rot[i] + dr[i]);
        return 1;
    }
    put_pose(o, node, want);
    for (int i = 0; i < 3; ++i)
        have->rot[i] = (int16_t)(want->rot[i] & 4095);
    memcpy(have->tr, want->tr, sizeof have->tr);
    return 1;
}

/* Copies the source frame's collision box (0x900, node 0) and root command
 * (0xA00, node remapped; dropped for a node the target lacks, or for all
 * of them with rig->no_root). */
static int put_extras(Out* o, const RtRig* rig, const uint8_t* src, uint32_t size, uint32_t frame)
{
    const unsigned count = rd16(src + frame + 2);
    uint32_t p = frame + 8;
    int n = 0;
    for (unsigned c = 0; c < count; ++c) {
        const uint16_t cmd = rd16(src + p);
        const int na = nargs(cmd);
        if (p + 2u + 2u * (uint32_t)na > size)
            return n;
        const unsigned op = cmd & 0xF00u;
        if (op == 0x900u) {
            for (int i = 0; i <= na; ++i)
                put(o, rd16(src + p + 2u * (uint32_t)i));
            ++n;
        } else if (op == 0xA00u && !rig->no_root) {
            const int s = cmd & 0xFF;
            const int d = (s > 0 && s < RT_MAX_NODES) ? rig->dst_of[s] : 0;
            if (d) {
                put(o, (uint16_t)((cmd & 0xFF00u) | (unsigned)d));
                for (int i = 1; i <= na; ++i)
                    put(o, rd16(src + p + 2u * (uint32_t)i));
                ++n;
            }
        }
        p += 2u + 2u * (uint32_t)na;
    }
    return n;
}

static uint32_t build(const RtRig* rig, const uint8_t* src, uint32_t src_size, int still_frame,
                      const uint8_t header[8], uint16_t frames, uint8_t* out, uint32_t cap)
{
    const int ns = blob_frames(src, src_size);
    if (ns <= 0 || frames == 0 || cap < 8u || rig->src_n <= 0)
        return 0;
    /* source states and frame offsets, frame by frame */
    RtState* sst = (RtState*)malloc(sizeof(RtState) * (size_t)ns * RT_MAX_NODES);
    uint32_t* soff = (uint32_t*)malloc(sizeof(uint32_t) * (size_t)ns);
    if (!sst || !soff) {
        free(sst);
        free(soff);
        return 0;
    }
    RtState cur[RT_MAX_NODES];
    reset_states(cur, rig->src_n);
    uint32_t p = 8;
    int ok = 1;
    for (int f = 0; f < ns && ok; ++f) {
        soff[f] = p;
        p = run_frame(src, src_size, p, cur, rig->src_n);
        if (!p)
            ok = 0;
        else
            memcpy(&sst[(size_t)f * RT_MAX_NODES], cur, sizeof cur);
    }
    Out o = {out, cap, 0, ok};
    if (ok) {
        memcpy(out, header, 8);
        wr16(out + 4, frames);
        o.len = 8;
    }
    RtState have[RT_MAX_NODES];
    for (int f = 0; f < frames && o.ok; ++f) {
        const uint32_t h = o.len;
        put(&o, 0); put(&o, 0); put(&o, (uint16_t)f); put(&o, 0);
        int count = 0;
        if (still_frame < 0 || f == 0) {
            int sf = still_frame >= 0 ? still_frame : (int)((int64_t)f * ns / frames);
            if (sf >= ns)
                sf = ns - 1;
            RtState want[RT_MAX_NODES];
            pose_for(rig, &sst[(size_t)sf * RT_MAX_NODES], want);
            for (int d = 1; d <= rig->dst_n; ++d) {
                if (f == 0)
                    have[d] = want[d];
                count += put_change(&o, d, &have[d], &want[d], f == 0);
            }
            count += put_extras(&o, rig, src, src_size, soff[sf]);
        }
        if (!o.ok)
            break;
        wr16(out + h, (uint16_t)((o.len - h - 8u) / 2u + 2u));
        wr16(out + h + 2, (uint16_t)count);
    }
    free(sst);
    free(soff);
    return o.ok ? o.len : 0;
}

uint32_t rt_retarget(const RtRig* rig, const uint8_t* src, uint32_t src_size,
                     const uint8_t header[8], uint16_t frames, uint8_t* out, uint32_t cap)
{
    return build(rig, src, src_size, -1, header, frames, out, cap);
}

uint32_t rt_still(const RtRig* rig, const uint8_t* src, uint32_t src_size, int src_frame,
                  const uint8_t header[8], uint16_t frames, uint8_t* out, uint32_t cap)
{
    if (src_frame < 0)
        src_frame = 0;
    return build(rig, src, src_size, src_frame, header, frames, out, cap);
}
