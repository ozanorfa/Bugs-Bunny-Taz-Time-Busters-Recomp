/*
 * bbt_disc_probe — runs the mod's host-side code (src/mods/bouze.c, donor.c,
 * donor_tex.c, with the plugin's own values from daffy_profile.h and
 * p3_profile.h) on the player's disc image and prints facts as JSON lines
 * for the disc tests (tests/test_disc_*.py). It prints counts, sizes,
 * positions and hashes, never game data. Files are addressed by (lba, size),
 * which the tests take from the disc's ISO 9660 directory.
 *
 *   bbt_disc_probe bzz   <image> <lba> <size>
 *       header, every chunk decoded (length + FNV-1a 64), record stream
 *   bbt_disc_probe daffy <image> <lba> <size>
 *       Daffy (LEVEL1/RETRY.BZZ) as the third actor's plugin extracts him:
 *       model, animations, synthetic spin, hold pose, textures and their
 *       place in VRAM (the strip and the tall area)
 *
 * (Until 2026-11-26 the "donor" and "levels" modes probed the Transylvania
 * enemy, then a third-character choice and Taz's replacement; removed.)
 *
 * Images: raw 2352-byte sectors (MODE1 or MODE2 form 1) or 2048-byte ISO.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bouze.h"
#include "donor.h"
#include "donor_tex.h"
#include "daffy_profile.h"
#include "p3_profile.h"

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
    /* as bbt_daffy_runtime_load (src/mods/donor_runtime.c) */
    *file = read_file(image, lba, size);
    if (!*file || !donor_extract_ids(*file, (size_t)size, kDaffyResIds, DAFFY_RES_COUNT, d))
        return 0;
    *spin_node = donor_add_spin(d, DONOR_SUB_SPIN, DONOR_SPIN_FRAMES);
    const DonorBlob* model = NULL;
    for (int i = 0; i < d->model_count; ++i)
        if (d->models[i].sub == DAFFY_MODEL_SUB)
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

static int cmd_daffy(const char* image, long lba, long size)
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
    const DonorBlob* s = find_anim(&d, DONOR_SUB_SPIN);
    printf("],\"spin_node\":%d,\"spin_frames\":%u,\"spin_size\":%u", spin, s ? s->frames : 0,
           s ? s->size : 0);
    /* The hold pose built from his own animation (the plugin's fallback
     * when Bugs' idle cannot be fitted), on a copy of the donor. */
    {
        Donor c;
        int hold_ok = donor_copy(&d, &c) &&
                      donor_add_hold(&c, DAFFY_POSE_SUB, DONOR_SUB_HOLD, DONOR_HOLD_FRAMES);
        const DonorBlob* h = hold_ok ? find_anim(&c, DONOR_SUB_HOLD) : NULL;
        const DonorBlob* src = find_anim(&d, DAFFY_POSE_SUB);
        int empty = 0;
        uint32_t f0 = 0;
        if (h && src) {
            const uint8_t* b = c.image + h->off;
            f0 = h->size - 8u - (uint32_t)(h->frames - 1u) * 8u;
            for (uint32_t f = 1; f < h->frames; ++f) {
                const uint8_t* hd = b + 8u + f0 + 8u * (f - 1u);
                empty += hd[2] == 0 && hd[3] == 0 && (hd[4] | (hd[5] << 8)) == f;
            }
            hold_ok = memcmp(b + 8, d.image + src->off + 8, f0) == 0;
        }
        printf(",\"hold\":{\"ok\":%s,\"frames\":%u,\"size\":%u,\"frame0_bytes\":%u,"
               "\"frame0_cmds\":%u,\"empty_frames\":%d,\"image_size\":%u}",
               hold_ok && h ? "true" : "false", h ? h->frames : 0, h ? h->size : 0, f0,
               h ? (unsigned)(c.image[h->off + 10] | (c.image[h->off + 11] << 8)) : 0u, empty,
               c.image ? c.image_size : 0u);
        if (c.image)
            donor_free(&c);
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
        /* Phase 4: the third actor's plan (the strip, and the tall area for
         * blocks taller than it), and where its patched records sample
         * (inside the new block, the kept source row). */
        DonorTexPlan sp;
        const int strip_ok = donor_tex_plan_strip_tall(&t, P3_STRIP_X, P3_STRIP_Y, P3_STRIP_W,
                                                       P3_STRIP_H, P3_TALL_X, P3_TALL_Y,
                                                       P3_TALL_W, P3_TALL_H, &sp);
        printf(",\"strip\":{\"ok\":%s,\"area\":[%d,%d,%d,%d],\"tall\":[%d,%d,%d,%d]",
               strip_ok ? "true" : "false", P3_STRIP_X, P3_STRIP_Y, P3_STRIP_W, P3_STRIP_H,
               P3_TALL_X, P3_TALL_Y, P3_TALL_W, P3_TALL_H);
        if (strip_ok) {
            int outside = 0, wrong_row = 0;
            for (int i = 0; i < t.record_count; ++i) {
                const DonorTexRecord* r = &t.records[i];
                const uint8_t* in = d.image + r->off;
                uint8_t out[12];
                donor_tex_patch_record(&t, &sp, r, in, out);
                const int g = r->group;
                const int ppx = (out[6] & 0xF) * 64, ppy = ((out[6] >> 4) & 1) * 256;
                const int n = r->shift_u3 ? 4 : 3;
                for (int k = 0; k < n; ++k) {
                    const int o = k == 3 ? 10 : 4 * k;
                    const int x = ppx + out[o] / 4, y = ppy + out[o + 1];
                    outside += x < sp.block_x[g] || x >= sp.block_x[g] + t.blocks[g].w ||
                               y < sp.block_y[g] || y >= sp.block_y[g] + sp.block_h[g];
                    wrong_row += y - sp.block_y[g] !=
                                 ((t.page_y + in[o + 1] - t.blocks[g].y) >> sp.vshift[g]);
                }
            }
            printf(",\"texels_outside\":%d,\"wrong_rows\":%d,\"blocks\":[", outside, wrong_row);
            for (int g = 0; g < t.block_count; ++g)
                printf("%s[%u,%u,%u,%u,%u]", g ? "," : "", sp.block_x[g], sp.block_y[g],
                       t.blocks[g].w, sp.block_h[g], sp.vshift[g]);
            printf("],\"cluts\":[");
            for (int g = 0; g < t.clut_count; ++g)
                printf("%s[%u,%u]", g ? "," : "", sp.clut_x[g], sp.clut_y[g]);
            printf("]");
        }
        printf("}");
    }
    printf("}}\n");
    donor_tex_free(&t);
    donor_free(&d);
    free(file);
    return tex_ok && spin ? 0 : 1;
}

int main(int argc, char** argv)
{
    if (argc >= 5 && strcmp(argv[1], "bzz") == 0)
        return cmd_bzz(argv[2], atol(argv[3]), atol(argv[4]));
    if (argc >= 5 && strcmp(argv[1], "daffy") == 0)
        return cmd_daffy(argv[2], atol(argv[3]), atol(argv[4]));
    fprintf(stderr,
            "usage: bbt_disc_probe bzz <image> <lba> <size>\n"
            "       bbt_disc_probe daffy <image> <lba> <size>\n");
    return 2;
}
