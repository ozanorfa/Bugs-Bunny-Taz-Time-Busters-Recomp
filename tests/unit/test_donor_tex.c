/* donor_tex.c: texture analysis and rebuild, the VRAM map, the target
 * pool, the packing plan and the record patch. */
#include <stdlib.h>
#include <string.h>

#include "bouze.h"
#include "check.h"
#include "donor.h"
#include "donor_tex.h"
#include "synth.h"

static uint16_t clut_colour(int k) { return k == 5 ? 0 : (uint16_t)(0x8000u | (0x0100u + (unsigned)k)); }
static uint16_t texel(int x, int y) { return (uint16_t)(0x1000 + (x - 320) + 32 * y); }

typedef struct Fixture {
    SynthLevel l;
    Donor d;
    DonorTex t;
    int ok;
} Fixture;

static void fixture(Fixture* f)
{
    memset(f, 0, sizeof(*f));
    synth_level_build(&f->l);
    f->ok = donor_extract(f->l.file.p, f->l.file.n, SYN_TYPE_DONOR, &f->d) &&
            donor_tex_extract(f->l.file.p, f->l.file.n, &f->d, &f->d.models[0], &f->t);
}

static void fixture_free(Fixture* f)
{
    donor_tex_free(&f->t);
    donor_free(&f->d);
    synth_level_free(&f->l);
}

static void extract_groups_and_records(void)
{
    Fixture f;
    fixture(&f);
    REQUIRE(f.ok);
    const DonorTex* t = &f.t;
    CHECK_EQ(t->tpage, 5);
    CHECK_EQ(t->page_x, 320);
    CHECK_EQ(t->page_y, 0);
    REQUIRE(t->clut_count == 2);
    CHECK_EQ(t->cluts[0], 0x7D00);
    CHECK_EQ(t->cluts[1], 0x7D01);
    /* P0, P1, P2 are textured on page 5; P3 is on page 6; vertex-colour,
     * CLUT-less and untextured records are not counted at all. */
    REQUIRE(t->record_count == 3);
    CHECK_EQ(t->skipped_records, 1);
    const uint32_t m = f.d.models[0].off;
    CHECK_EQ(t->records[0].off, m + 12);
    CHECK_EQ(t->records[0].group, 0);
    CHECK_EQ(t->records[0].shift_u3, 0);
    CHECK_EQ(t->records[1].off, m + 12 + 32);
    CHECK_EQ(t->records[1].group, 1);
    CHECK_EQ(t->records[1].shift_u3, 1);     /* its fourth texel is inside the box */
    CHECK_EQ(t->records[2].off, m + 12 + 32 + 36);
    CHECK_EQ(t->records[2].group, 0);
    CHECK_EQ(t->records[2].shift_u3, 0);     /* (200, 200) is not a texel of the box */
    CHECK_EQ(t->umin, 0);
    CHECK_EQ(t->umax, 95);
    fixture_free(&f);
}

static void extract_rebuilds_pixels(void)
{
    Fixture f;
    fixture(&f);
    REQUIRE(f.ok);
    const DonorTex* t = &f.t;
    REQUIRE(t->block_count == 2);
    const DonorTexBlock* b0 = &t->blocks[0];
    const DonorTexBlock* b1 = &t->blocks[1];
    CHECK(b0->x == 320 && b0->y == 0 && b0->w == 8 && b0->h == 16);
    CHECK(b1->x == 336 && b1->y == 32 && b1->w == 8 && b1->h == 16);
    CHECK_EQ(t->pixel_count, 8 * 16 * 2 + 16 * 2);
    int bad = 0;
    for (int i = 0; i < 2; ++i) {
        const DonorTexBlock* b = &t->blocks[i];
        for (int y = 0; y < b->h; ++y)
            for (int x = 0; x < b->w; ++x)
                bad += t->pixels[b->px + (uint32_t)(y * b->w + x)] != texel(b->x + x, b->y + y);
    }
    CHECK_EQ(bad, 0);
    /* CLUTs as the game leaves them in VRAM: bit 15 on every non-zero
     * colour; only the one row the block carries. */
    int badc = 0;
    for (int g = 0; g < 2; ++g)
        for (int k = 0; k < 16; ++k)
            badc += t->pixels[t->clut_px + 16u * (uint32_t)g + (uint32_t)k] != clut_colour(16 * g + k);
    CHECK_EQ(badc, 0);
    fixture_free(&f);
}

static void extract_rejects(void)
{
    SynthLevel l;
    synth_level_build(&l);
    Donor d;
    REQUIRE(donor_extract(l.file.p, l.file.n, SYN_TYPE_DONOR, &d));
    DonorTex t;
    CHECK(!donor_tex_extract(l.file.p, l.file.n, &d, NULL, &t));
    /* The bystander's page-8 model has no TIM behind it: still analysed,
     * but a level file without a texture chunk cannot be rebuilt. */
    const SynthChunk no_tex[2] = {{1, l.stream.p, l.stream.n}, {4, l.blobs.p, l.blobs.n}};
    Buf f2;
    buf_init(&f2);
    synth_bzz(no_tex, 2, &f2);
    CHECK(!donor_tex_extract(f2.p, f2.n, &d, &d.models[0], &t));
    CHECK(t.pixels == NULL);
    /* A model blob whose header points outside it. */
    DonorBlob bogus = d.models[0];
    bogus.size = 8;
    CHECK(!donor_tex_extract(l.file.p, l.file.n, &d, &bogus, &t));
    buf_free(&f2);
    donor_free(&d);
    synth_level_free(&l);
}

static void vram_map(void)
{
    VramMap* m = (VramMap*)calloc(1, sizeof(VramMap));
    REQUIRE(m != NULL);
    CHECK(vram_map_is_free(m, 0, 0, 1024, 512));
    vram_map_mark(m, -5, -5, 10, 10);          /* clipped to (0..4, 0..4) */
    CHECK(!vram_map_is_free(m, 4, 4, 1, 1));
    CHECK(vram_map_is_free(m, 5, 0, 1, 5));
    CHECK(vram_map_is_free(m, 0, 5, 5, 1));
    vram_map_mark(m, 1020, 510, 10, 10);       /* clipped at the right and bottom */
    CHECK(!vram_map_is_free(m, 1023, 511, 1, 1));
    CHECK(vram_map_is_free(m, 1019, 511, 1, 1));
    CHECK(!vram_map_is_free(m, 1020, 0, 8, 1));  /* out of range is never free */
    CHECK(!vram_map_is_free(m, 0, 0, 0, 1));
    CHECK(!vram_map_is_free(m, -1, 0, 1, 1));
    vram_map_clear(m);
    CHECK(vram_map_is_free(m, 0, 0, 1024, 512));
    free(m);
}

static const uint8_t* target_model(const SynthLevel* l, uint32_t* size)
{
    *size = l->size[20];
    return l->blobs.p + l->off[20];
}

static void pool_from_target_model(void)
{
    SynthLevel l;
    synth_level_build(&l);
    uint32_t size;
    const uint8_t* m = target_model(&l, &size);
    DonorTexPool pool;
    REQUIRE(donor_tex_pool_from_model(m, size, &pool));
    REQUIRE(pool.rect_count == 1);
    CHECK_EQ(pool.x[0], 448);
    CHECK_EQ(pool.y[0], 0);
    CHECK_EQ(pool.w[0], 16);
    CHECK_EQ(pool.h[0], 64);
    REQUIRE(pool.clut_count == 1);
    CHECK_EQ(pool.clut_x[0], 32);
    CHECK_EQ(pool.clut_y[0], 481);
    /* No textured record / a header pointing outside the blob. */
    static const SynthPrim kFlat[] = {{0x30, 0, 28, 0, 0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}}}};
    Buf b;
    buf_init(&b);
    synth_model(kFlat, 1, &b);
    CHECK(!donor_tex_pool_from_model(b.p, (uint32_t)b.n, &pool));
    CHECK(!donor_tex_pool_from_model(m, 11, &pool));
    buf_free(&b);
    synth_level_free(&l);
}

static int inside(int x, int y, int w, int h, int rx, int ry, int rw, int rh)
{
    return x >= rx && y >= ry && x + w <= rx + rw && y + h <= ry + rh;
}

static int overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void plan_and_patch(void)
{
    Fixture f;
    fixture(&f);
    REQUIRE(f.ok);
    uint32_t size;
    const uint8_t* m = target_model(&f.l, &size);
    DonorTexPool pool;
    REQUIRE(donor_tex_pool_from_model(m, size, &pool));
    VramMap* up = (VramMap*)calloc(1, sizeof(VramMap));
    vram_map_mark(up, 448, 0, 16, 64);
    vram_map_mark(up, 32, 481, 16, 1);
    DonorTexPlan plan;
    REQUIRE(donor_tex_plan(&f.t, &pool, up, &plan));

    /* CLUT 0 takes the target's own CLUT row; the others go inside its
     * texel box, 16-aligned. Blocks go inside the box. Nothing overlaps. */
    CHECK(plan.clut_x[0] == 32 && plan.clut_y[0] == 481);
    int rx[4], ry[4], rw[4], rh[4], n = 0;
    for (int g = 0; g < f.t.clut_count; ++g) {
        const DonorTexBlock* b = &f.t.blocks[g];
        CHECK(inside(plan.block_x[g], plan.block_y[g], b->w, b->h, 448, 0, 16, 64));
        CHECK_EQ(plan.tpage[g], (plan.block_x[g] / 64) | ((plan.block_y[g] / 256) << 4));
        /* The shift moves the source block onto the new one. */
        CHECK_EQ(plan.block_x[g] - (plan.block_x[g] & ~63), (b->x - f.t.page_x) + plan.du[g] / 4);
        CHECK_EQ(plan.du[g] % 4, 0);
        CHECK_EQ(plan.block_y[g] - (plan.block_y[g] & ~255), (b->y - f.t.page_y) + plan.dv[g]);
        rx[n] = plan.block_x[g]; ry[n] = plan.block_y[g]; rw[n] = b->w; rh[n] = b->h; ++n;
        if (g > 0) {
            CHECK_EQ(plan.clut_x[g] % 16, 0);
            CHECK(inside(plan.clut_x[g], plan.clut_y[g], 16, 1, 448, 0, 16, 64));
            rx[n] = plan.clut_x[g]; ry[n] = plan.clut_y[g]; rw[n] = 16; rh[n] = 1; ++n;
        }
    }
    int overlaps = 0;
    for (int i = 0; i < n; ++i)
        for (int k = i + 1; k < n; ++k)
            overlaps += overlap(rx[i], ry[i], rw[i], rh[i], rx[k], ry[k], rw[k], rh[k]);
    CHECK_EQ(overlaps, 0);

    /* Patched records sample the new block with the new CLUT; texture
     * page bits other than the page number are kept. */
    int bad = 0;
    for (int i = 0; i < f.t.record_count; ++i) {
        const DonorTexRecord* r = &f.t.records[i];
        const uint8_t* in = f.d.image + r->off;
        uint8_t out[12];
        donor_tex_patch_record(&f.t, &plan, r, in, out);
        const int g = r->group;
        const DonorTexBlock* b = &f.t.blocks[g];
        const uint16_t tp = bouze_u16(out + 6), clut = bouze_u16(out + 2);
        CHECK_EQ(tp & 0x1F, plan.tpage[g]);
        CHECK_EQ(tp & ~0x1F, bouze_u16(in + 6) & ~0x1F);
        CHECK_EQ((clut & 0x3F) * 16, plan.clut_x[g]);
        CHECK_EQ(clut >> 6, plan.clut_y[g]);
        const int px = (tp & 0xF) * 64, py = ((tp >> 4) & 1) * 256;
        const int texels = r->shift_u3 ? 4 : 3;
        for (int k = 0; k < texels; ++k) {
            const int o = k == 3 ? 10 : 4 * k;
            bad += !inside(px + out[o] / 4, py + out[o + 1], 1, 1, plan.block_x[g], plan.block_y[g],
                           b->w, b->h);
        }
        if (!r->shift_u3)
            CHECK(out[10] == in[10] && out[11] == in[11]);
        /* Same texel, same pixel: the source texel at the same spot. */
        const int su = in[0], sv = in[1];
        CHECK_EQ(px + out[0] / 4 - plan.block_x[g], f.t.page_x + su / 4 - b->x);
        CHECK_EQ(py + out[1] - plan.block_y[g], f.t.page_y + sv - b->y);
    }
    CHECK_EQ(bad, 0);
    free(up);
    fixture_free(&f);
}

static void plan_needs_uploaded_pool_vram(void)
{
    Fixture f;
    fixture(&f);
    REQUIRE(f.ok);
    uint32_t size;
    const uint8_t* m = target_model(&f.l, &size);
    DonorTexPool pool;
    REQUIRE(donor_tex_pool_from_model(m, size, &pool));
    VramMap* up = (VramMap*)calloc(1, sizeof(VramMap));
    DonorTexPlan plan;
    CHECK(!donor_tex_plan(&f.t, &pool, up, &plan));        /* nothing uploaded */
    vram_map_mark(up, 32, 481, 16, 1);
    CHECK(!donor_tex_plan(&f.t, &pool, up, &plan));        /* CLUT row only */
    vram_map_mark(up, 448, 0, 16, 12);
    CHECK(!donor_tex_plan(&f.t, &pool, up, &plan));        /* too little of the box (16-row blocks) */
    vram_map_mark(up, 448, 0, 16, 64);
    CHECK(donor_tex_plan(&f.t, &pool, up, &plan));
    /* A pool too small for the blocks. */
    DonorTexPool small = pool;
    small.w[0] = 4;
    small.h[0] = 8;
    CHECK(!donor_tex_plan(&f.t, &small, up, &plan));
    /* An empty pool. */
    DonorTexPool none;
    memset(&none, 0, sizeof none);
    CHECK(!donor_tex_plan(&f.t, &none, up, &plan));
    free(up);
    fixture_free(&f);
}

static void plan_keeps_blocks_inside_one_page(void)
{
    /* Two pool boxes side by side across the page 6 / page 7 boundary at
     * x = 448; the narrow one on page 6 is 4 wide. An 8-wide block that
     * starts there would straddle two pages, which one texture page field
     * cannot address. */
    Fixture f;
    fixture(&f);
    REQUIRE(f.ok);
    DonorTexPool pool;
    memset(&pool, 0, sizeof pool);
    pool.x[0] = 444; pool.y[0] = 0; pool.w[0] = 4; pool.h[0] = 64;
    pool.x[1] = 448; pool.y[1] = 0; pool.w[1] = 16; pool.h[1] = 64;
    pool.rect_count = 2;
    pool.clut_x[0] = 32; pool.clut_y[0] = 481;
    pool.clut_x[1] = 48; pool.clut_y[1] = 481;
    pool.clut_count = 2;
    VramMap* up = (VramMap*)calloc(1, sizeof(VramMap));
    vram_map_mark(up, 444, 0, 20, 64);
    vram_map_mark(up, 32, 481, 32, 1);
    DonorTexPlan plan;
    REQUIRE(donor_tex_plan(&f.t, &pool, up, &plan));
    for (int g = 0; g < f.t.block_count; ++g) {
        const int x = plan.block_x[g], w = f.t.blocks[g].w;
        CHECK_EQ(x / 64, (x + w - 1) / 64);
        CHECK(x >= 444 && x + w <= 464);
    }
    free(up);
    fixture_free(&f);
}

const TestCase kDonorTexTests[] = {
    {"extract_groups_and_records", extract_groups_and_records},
    {"extract_rebuilds_pixels", extract_rebuilds_pixels},
    {"extract_rejects", extract_rejects},
    {"vram_map", vram_map},
    {"pool_from_target_model", pool_from_target_model},
    {"plan_and_patch", plan_and_patch},
    {"plan_needs_uploaded_pool_vram", plan_needs_uploaded_pool_vram},
    {"plan_keeps_blocks_inside_one_page", plan_keeps_blocks_inside_one_page},
};
const int kDonorTexTestCount = (int)(sizeof(kDonorTexTests) / sizeof(kDonorTexTests[0]));
