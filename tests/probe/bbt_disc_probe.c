/*
 * bbt_disc_probe — runs the mod's host-side code (src/mods/bouze.c, donor.c,
 * donor_tex.c, with the plugin's own table from trca01_profile.h) on the
 * player's disc image and prints facts as JSON lines for the disc tests
 * (tests/test_disc_*.py). It prints counts, sizes, positions and hashes,
 * never game data. Files are addressed by (lba, size), which the tests take
 * from the disc's ISO 9660 directory.
 *
 *   bbt_disc_probe bzz    <image> <lba> <size>
 *       header, every chunk decoded (length + FNV-1a 64), record stream
 *   bbt_disc_probe donor  <image> <lba> <size> [<spin_blob_out>]
 *       the TRCA01 enemy as the plugin extracts it; optionally writes the
 *       synthetic spin blob (built by donor_add_spin, not disc data)
 *   bbt_disc_probe levels <image> <donor_lba> <donor_size> <name>:<lba>:<size> ...
 *       per level: Taz's records patched as the plugin does, and the
 *       texture plan into Taz's VRAM
 *
 * Images: raw 2352-byte sectors (MODE1 or MODE2 form 1) or 2048-byte ISO.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bouze.h"
#include "donor.h"
#include "donor_tex.h"
#include "trca01_profile.h"

/* ---- disc --------------------------------------------------------------- */

static long g_sector = 2048, g_user = 0;

static int detect_format(FILE* f)
{
    static const uint8_t kSync[12] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};
    uint8_t h[16];
    if (fseek(f, 16L * 2352L, SEEK_SET) != 0 || fread(h, 1, 16, f) != 16)
        return 0;
    if (memcmp(h, kSync, 12) == 0) {
        g_sector = 2352;
        g_user = h[15] == 2 ? 24 : 16;
    }
    return 1;
}

static uint8_t* read_file(const char* image, long lba, long size)
{
    FILE* f = fopen(image, "rb");
    if (!f)
        return NULL;
    uint8_t* b = NULL;
    if (detect_format(f) && size > 0)
        b = (uint8_t*)malloc((size_t)size + 2048);
    for (long i = 0; b && i * 2048 < size; ++i) {
        if (fseek(f, (lba + i) * g_sector + g_user, SEEK_SET) != 0 ||
            fread(b + i * 2048, 1, 2048, f) != 2048) {
            free(b);
            b = NULL;
        }
    }
    fclose(f);
    return b;
}

static unsigned long long fnv64(const uint8_t* p, size_t n)
{
    unsigned long long h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static int count_records(const uint8_t* s, size_t n, int* end_ok)
{
    size_t pos = 0;
    BouzeRecord r;
    int k, c = 0;
    while ((k = bouze_next_record(s, n, &pos, &r)) == 1)
        ++c;
    *end_ok = k == 0;
    return c;
}

/* ---- bzz ---------------------------------------------------------------- */

static int cmd_bzz(const char* image, long lba, long size)
{
    uint8_t* f = read_file(image, lba, size);
    BouzeHeader h;
    if (!f || !bouze_parse_header(f, (size_t)size, &h)) {
        printf("{\"ok\":false,\"err\":\"header\"}\n");
        free(f);
        return 1;
    }
    printf("{\"ok\":true,\"version\":%u,\"chunks\":[", h.version);
    int ok = 1, records = -1, end_ok = 0;
    for (uint32_t c = 0; c < h.count; ++c) {
        size_t n = 0;
        uint8_t* o = bouze_decompress(f + h.file_off[c], h.size[c], &n);
        printf("%s{\"type\":%u,\"size\":%u,\"len\":%zu,\"fnv\":\"%016llx\"}", c ? "," : "",
               h.type[c], h.size[c], o ? n : 0, o ? fnv64(o, n) : 0ULL);
        if (!o)
            ok = 0;
        else if (c == 0)
            records = count_records(o, n, &end_ok);
        free(o);
    }
    printf("],\"decoded\":%s,\"records\":%d,\"stream_end\":%s}\n", ok ? "true" : "false",
           records, end_ok ? "true" : "false");
    free(f);
    return ok && end_ok ? 0 : 1;
}

/* ---- donor -------------------------------------------------------------- */

static int load_donor(const char* image, long lba, long size, uint8_t** file, Donor* d,
                      DonorTex* t, int* spin_node)
{
    *file = read_file(image, lba, size);
    if (!*file || !donor_extract(*file, (size_t)size, DONOR_ACTOR_TYPE, d))
        return 0;
    *spin_node = donor_add_spin(d, DONOR_SUB_SPIN, DONOR_SPIN_FRAMES);
    const DonorBlob* model = NULL;
    for (int i = 0; i < d->model_count; ++i)
        if (d->models[i].sub == TAZ_MODEL_SUB)
            model = &d->models[i];
    return model && donor_tex_extract(*file, (size_t)size, d, model, t);
}

static const DonorBlob* find_anim(const Donor* d, uint16_t sub)
{
    for (int i = 0; i < d->anim_count; ++i)
        if (d->anims[i].sub == sub)
            return &d->anims[i];
    return NULL;
}

static int cmd_donor(const char* image, long lba, long size, const char* spin_out)
{
    uint8_t* file = NULL;
    Donor d;
    DonorTex t;
    int spin = 0;
    memset(&d, 0, sizeof d);
    memset(&t, 0, sizeof t);
    const int tex_ok = load_donor(image, lba, size, &file, &d, &t, &spin);
    if (!d.image) {
        printf("{\"ok\":false,\"err\":\"extract\"}\n");
        free(file);
        return 1;
    }
    printf("{\"ok\":true,\"image_size\":%u,\"models\":[", d.image_size);
    for (int i = 0; i < d.model_count; ++i)
        printf("%s{\"sub\":%u,\"id\":%u,\"size\":%u}", i ? "," : "", d.models[i].sub, d.models[i].id,
               d.models[i].size);
    printf("],\"anims\":%d,\"anim_subs\":[", d.anim_count);
    for (int i = 0; i < d.anim_count; ++i)
        printf("%s%u", i ? "," : "", d.anims[i].sub);
    printf("],\"map\":[");
    for (int i = 0; i < TAZ_MAP_COUNT; ++i) {
        const DonorBlob* b = find_anim(&d, kTazToTrca01[i].donor_sub);
        printf("%s{\"taz\":%u,\"donor\":%u,\"found\":%s,\"frames\":%u}", i ? "," : "",
               kTazToTrca01[i].target_sub, kTazToTrca01[i].donor_sub, b ? "true" : "false",
               b ? b->frames : 0);
    }
    printf("],\"defaults\":[");
    for (int i = 0; i < DONOR_DEFAULT_COUNT; ++i) {
        const DonorBlob* b = find_anim(&d, kDonorDefaults[i]);
        printf("%s{\"sub\":%u,\"found\":%s,\"frames\":%u}", i ? "," : "", kDonorDefaults[i],
               b ? "true" : "false", b ? b->frames : 0);
    }
    const DonorBlob* s = find_anim(&d, DONOR_SUB_SPIN);
    printf("],\"spin_node\":%d,\"spin_frames\":%u,\"spin_size\":%u", spin, s ? s->frames : 0,
           s ? s->size : 0);
    if (s && spin_out) {
        FILE* o = fopen(spin_out, "wb");
        if (o) {
            fwrite(d.image + s->off, 1, s->size, o);
            fclose(o);
        }
    }
    printf(",\"tex\":{\"ok\":%s", tex_ok ? "true" : "false");
    if (tex_ok) {
        int q3 = 0;
        for (int i = 0; i < t.record_count; ++i)
            q3 += t.records[i].shift_u3;
        printf(",\"tpage\":%u,\"groups\":%d,\"records\":%d,\"skipped\":%d,\"shift_u3\":%d,"
               "\"pixels\":%u,\"pixels_fnv\":\"%016llx\",\"blocks\":[",
               t.tpage, t.clut_count, t.record_count, t.skipped_records, q3, t.pixel_count,
               fnv64((const uint8_t*)t.pixels, (size_t)t.pixel_count * 2u));
        for (int i = 0; i < t.block_count; ++i)
            printf("%s[%u,%u,%u,%u]", i ? "," : "", t.blocks[i].x, t.blocks[i].y, t.blocks[i].w,
                   t.blocks[i].h);
        printf("],\"cluts\":[");
        for (int i = 0; i < t.clut_count; ++i)
            printf("%s%u", i ? "," : "", t.cluts[i]);
        printf("]");
    }
    printf("}}\n");
    donor_tex_free(&t);
    donor_free(&d);
    free(file);
    return tex_ok && spin ? 0 : 1;
}

/* ---- levels ------------------------------------------------------------- */

/* VRAM a level's TIM list uploads (image block, then its CLUT block). */
static void mark_tims(VramMap* m, const uint8_t* s, size_t sl, const uint8_t* c3, size_t cl)
{
    size_t pos = 0;
    BouzeRecord r;
    while (bouze_next_record(s, sl, &pos, &r) == 1) {
        if (r.kind != 0x28)
            continue;
        size_t tp = r.tags;
        BouzeTag t;
        while (bouze_next_tag(s, &r, &tp, &t)) {
            if (t.tag != 0x29 || t.len != 8)
                continue;
            const uint32_t off = bouze_u32(s + t.payload);
            if ((size_t)off + 8u > cl || bouze_u32(c3 + off) != 0x10u)
                continue;
            const uint32_t flags = bouze_u32(c3 + off + 4);
            size_t p = off + 8u;
            for (int k = 0; k < 2; ++k) {
                if (k == 0 && !(flags & 8u))
                    continue;
                if (p + 12u > cl)
                    break;
                const uint32_t bn = bouze_u32(c3 + p);
                const int x = bouze_u16(c3 + p + 4), y = bouze_u16(c3 + p + 6);
                const int w = bouze_u16(c3 + p + 8);
                int h = bouze_u16(c3 + p + 10);
                if (w && bn >= 12u && (int)((bn - 12u) / (2u * (uint32_t)w)) < h)
                    h = (int)((bn - 12u) / (2u * (uint32_t)w));
                vram_map_mark(m, x, y, w, h);
                p += bn;
            }
        }
    }
}

typedef struct HdrCtx {
    const uint8_t* blobs;
    size_t len;
} HdrCtx;

static int read_hdr(void* ctx, uint32_t off, uint8_t out[8])
{
    const HdrCtx* c = (const HdrCtx*)ctx;
    if ((size_t)off + 8u > c->len)
        return 0;
    memcpy(out, c->blobs + off, 8);
    return 1;
}

static uint8_t* chunk(const uint8_t* f, const BouzeHeader* h, uint32_t type, size_t* len)
{
    const int i = bouze_find_chunk(h, type);
    *len = 0;
    return i < 0 ? NULL : bouze_decompress(f + h->file_off[i], h->size[i], len);
}

static int overlaps(const int* r, int n)
{
    int c = 0;
    for (int i = 0; i < n; ++i)
        for (int k = i + 1; k < n; ++k)
            c += r[4 * i] < r[4 * k] + r[4 * k + 2] && r[4 * k] < r[4 * i] + r[4 * i + 2] &&
                 r[4 * i + 1] < r[4 * k + 1] + r[4 * k + 3] && r[4 * k + 1] < r[4 * i + 1] + r[4 * i + 3];
    return c;
}

static void probe_level(const char* image, const char* name, long lba, long size, const Donor* d,
                        const DonorTex* t)
{
    uint8_t* f = read_file(image, lba, size);
    BouzeHeader h;
    if (!f || !bouze_parse_header(f, (size_t)size, &h)) {
        printf("{\"name\":\"%s\",\"ok\":false,\"err\":\"header\"}\n", name);
        free(f);
        return;
    }
    size_t sl, bl, cl;
    uint8_t* s = chunk(f, &h, BOUZE_CHUNK_STREAM, &sl);
    uint8_t* b4 = chunk(f, &h, BOUZE_CHUNK_BLOBS, &bl);
    uint8_t* c3 = chunk(f, &h, 3, &cl);
    printf("{\"name\":\"%s\",\"ok\":%s", name, s && b4 ? "true" : "false");
    uint32_t moff = 0, msize = 0;
    const int has_taz = s && donor_find_model(s, sl, TYPE_TAZ, TAZ_MODEL_SUB, &moff, &msize);
    printf(",\"has_taz\":%s", has_taz ? "true" : "false");
    if (s && b4 && has_taz) {
        /* Patch exactly as the plugin does (any image address works: only
         * the arithmetic is checked). */
        uint8_t* orig = (uint8_t*)malloc(sl);
        memcpy(orig, s, sl);
        const uint32_t base = 0x80135000u, img = 0x9F000000u;
        DonorWrite w[256];
        DonorAnimFix fx[256];
        int nf = 0;
        DonorPatchStats st;
        HdrCtx ctx = {b4, bl};
        int recs0_end, recs1_end;
        const int recs0 = count_records(orig, sl, &recs0_end);
        int n = donor_patch_stream(s, sl, TYPE_TAZ, d, img, base, kTazToTrca01, TAZ_MAP_COUNT,
                                   kDonorDefaults, DONOR_DEFAULT_COUNT, read_hdr, &ctx, w, 256, fx,
                                   256, &nf, &st);
        const int blobs = n;
        if (n > 0)
            n = donor_patch_actor_scale(s, sl, TYPE_TAZ, 0x0C00, w, n, 256);
        int outside = 0, in_image = 1;
        for (size_t i = 0; i < sl; ++i)
            if (s[i] != orig[i]) {
                int in = 0;
                for (int k = 0; k < n; ++k)
                    in |= i >= w[k].off && i < w[k].off + w[k].len;
                outside += !in;
            }
        for (int k = 0; k < blobs; ++k) {
            const uint32_t a = base + bouze_u32(s + w[k].off), len = bouze_u32(s + w[k].off + 4);
            if (a < img || a + len > img + d->image_size)
                in_image = 0;
        }
        const int recs1 = count_records(s, sl, &recs1_end);
        printf(",\"patch\":{\"result\":%d,\"blobs\":%d,\"scale\":%d,\"actors\":%d,\"ids\":%d,"
               "\"models\":%d,\"anims_mapped\":%d,\"anims_default\":%d,\"other\":%d,\"fixes\":%d,"
               "\"outside_payloads\":%d,\"targets_in_image\":%s,\"records\":[%d,%d],"
               "\"stream_end\":%s}",
               n, blobs, n - blobs, st.actor_records, st.resource_ids, st.models,
               st.anims_mapped, st.anims_default, st.other, nf, outside,
               in_image ? "true" : "false", recs0, recs1,
               recs0_end && recs1_end ? "true" : "false");
        free(orig);

        DonorTexPool pool;
        const int pool_ok = moff + (size_t)msize <= bl &&
                            donor_tex_pool_from_model(b4 + moff, msize, &pool);
        printf(",\"pool\":{\"ok\":%s,\"rects\":%d,\"cluts\":%d}", pool_ok ? "true" : "false",
               pool_ok ? pool.rect_count : 0, pool_ok ? pool.clut_count : 0);
        if (pool_ok && c3) {
            VramMap* up = (VramMap*)calloc(1, sizeof(VramMap));
            mark_tims(up, s, sl, c3, cl);
            DonorTexPlan plan;
            const int ok = donor_tex_plan(t, &pool, up, &plan);
            printf(",\"plan\":{\"ok\":%s", ok ? "true" : "false");
            if (ok) {
                /* Every patched texel inside its group's new block; blocks and
                 * CLUTs inside uploaded VRAM left of the frame buffers, no
                 * overlaps. */
                int bad = 0, not_uploaded = 0, beyond = 0;
                for (int i = 0; i < t->record_count; ++i) {
                    const DonorTexRecord* r = &t->records[i];
                    uint8_t o[12];
                    donor_tex_patch_record(t, &plan, r, d->image + r->off, o);
                    const int g = r->group, bx = plan.block_x[g], by = plan.block_y[g];
                    const int px = (plan.tpage[g] & 0xF) * 64, py = ((plan.tpage[g] >> 4) & 1) * 256;
                    for (int k = 0; k < (r->shift_u3 ? 4 : 3); ++k) {
                        const int u = o[k == 3 ? 10 : 4 * k], v = o[k == 3 ? 11 : 4 * k + 1];
                        const int x = px + u / 4, y = py + v;
                        bad += x < bx || x >= bx + t->blocks[g].w || y < by || y >= by + t->blocks[g].h;
                    }
                }
                int rects[4 * 2 * DONOR_TEX_MAX_CLUTS], nr = 0;
                for (int g = 0; g < t->clut_count; ++g) {
                    const int bw = t->blocks[g].w, bh = t->blocks[g].h;
                    rects[4 * nr] = plan.block_x[g]; rects[4 * nr + 1] = plan.block_y[g];
                    rects[4 * nr + 2] = bw; rects[4 * nr + 3] = bh; ++nr;
                    rects[4 * nr] = plan.clut_x[g]; rects[4 * nr + 1] = plan.clut_y[g];
                    rects[4 * nr + 2] = 16; rects[4 * nr + 3] = 1; ++nr;
                    for (int j = 0; j < bh; ++j)
                        for (int i = 0; i < bw; ++i)
                            not_uploaded += vram_map_is_free(up, plan.block_x[g] + i, plan.block_y[g] + j, 1, 1);
                    for (int i = 0; i < 16; ++i)
                        not_uploaded += vram_map_is_free(up, plan.clut_x[g] + i, plan.clut_y[g], 1, 1);
                    beyond += plan.block_x[g] + bw > 512 || plan.clut_x[g] + 16 > 512;
                }
                printf(",\"texels_outside\":%d,\"overlaps\":%d,\"not_uploaded\":%d,"
                       "\"beyond_512\":%d",
                       bad, overlaps(rects, nr), not_uploaded, beyond);
            }
            printf("}");
            free(up);
        }
    }
    printf("}\n");
    fflush(stdout);
    free(s);
    free(b4);
    free(c3);
    free(f);
}

static int cmd_levels(const char* image, long dlba, long dsize, int argc, char** argv)
{
    uint8_t* file = NULL;
    Donor d;
    DonorTex t;
    int spin = 0;
    memset(&d, 0, sizeof d);
    memset(&t, 0, sizeof t);
    if (!load_donor(image, dlba, dsize, &file, &d, &t, &spin)) {
        printf("{\"ok\":false,\"err\":\"donor\"}\n");
        return 1;
    }
    for (int i = 0; i < argc; ++i) {
        char name[128];
        long lba = 0, size = 0;
        if (sscanf(argv[i], "%127[^:]:%ld:%ld", name, &lba, &size) != 3) {
            printf("{\"ok\":false,\"err\":\"bad level argument\"}\n");
            continue;
        }
        probe_level(image, name, lba, size, &d, &t);
    }
    donor_tex_free(&t);
    donor_free(&d);
    free(file);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc >= 5 && strcmp(argv[1], "bzz") == 0)
        return cmd_bzz(argv[2], atol(argv[3]), atol(argv[4]));
    if (argc >= 5 && strcmp(argv[1], "donor") == 0)
        return cmd_donor(argv[2], atol(argv[3]), atol(argv[4]), argc >= 6 ? argv[5] : NULL);
    if (argc >= 5 && strcmp(argv[1], "levels") == 0)
        return cmd_levels(argv[2], atol(argv[3]), atol(argv[4]), argc - 5, argv + 5);
    fprintf(stderr,
            "usage: bbt_disc_probe bzz <image> <lba> <size>\n"
            "       bbt_disc_probe donor <image> <lba> <size> [<spin_out>]\n"
            "       bbt_disc_probe levels <image> <donor_lba> <donor_size> <name>:<lba>:<size> ...\n");
    return 2;
}
