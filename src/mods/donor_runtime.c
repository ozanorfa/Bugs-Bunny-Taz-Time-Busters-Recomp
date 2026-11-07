/*
 * Shared donor loader — see donor_runtime.h. Moved out of
 * third_character.c (Phase 3, M2-M4) so the Phase 4 third actor uses the
 * same extraction: donor.c reads the model and animations of the TRCA01
 * enemy, donor_add_spin appends the synthetic spin, donor_tex.c rebuilds
 * its texels and CLUTs from the level's TIM list.
 */
#include "donor_runtime.h"

#include <stdlib.h>
#include <string.h>

#include "mod_plugins.h"
#include "trca01_profile.h"

static BbtDonorRuntime g_rt;

const BbtDonorRuntime* bbt_donor_runtime(void)
{
    return &g_rt;
}

int bbt_donor_runtime_load(void)
{
    if (g_rt.state != 0)
        return g_rt.state;
    uint32_t size = 0;
    if (!psx_mod_read_disc_file(DONOR_FILE, NULL, 0, &size) || size == 0)
        return 0;                  /* no disc mounted yet: try again later */
    uint8_t* file = (uint8_t*)malloc(size);
    if (!file)
        return 0;
    uint32_t got = 0;
    int ok = psx_mod_read_disc_file(DONOR_FILE, file, size, &got) && got == size &&
             donor_extract(file, size, DONOR_ACTOR_TYPE, &g_rt.donor);
    if (ok) {
        /* Without the synthetic spin, the spin falls back to a default
         * pose (no tornado): reported, not fatal. */
        g_rt.spin_node = donor_add_spin(&g_rt.donor, DONOR_SUB_SPIN, DONOR_SPIN_FRAMES);
        /* Textures: without them the donor keeps TRCA01's texture
         * positions (right only in TRCA01): reported, not fatal. */
        const DonorBlob* model = NULL;
        for (int i = 0; i < g_rt.donor.model_count; ++i)
            if (g_rt.donor.models[i].sub == TAZ_MODEL_SUB)
                model = &g_rt.donor.models[i];
        g_rt.tex_ready = model && donor_tex_extract(file, size, &g_rt.donor, model, &g_rt.tex);
        g_rt.state = 1;
    } else {
        donor_free(&g_rt.donor);
        g_rt.state = -1;
    }
    free(file);
    return g_rt.state;
}

/* Upload staging: pixels go to guest RAM below the stack pointer (unused
 * at that moment), are uploaded synchronously and the bytes restored. */
#define STAGE_GAP 0x100u
#define STAGE_MAX 0x800u

static int in_main_ram(uint32_t p)
{
    return p >= 0x80000000u && p < 0x80200000u;
}

int bbt_upload_rect(uint32_t sp, int x, int y, int w, int h, const uint16_t* px)
{
    const uint32_t n = (uint32_t)(w * h);
    const uint32_t bytes = (n * 2u + 3u) & ~3u;
    if (n == 0 || bytes > STAGE_MAX || sp < 0x80000000u + STAGE_GAP + bytes)
        return 0;
    const uint32_t base = (sp - STAGE_GAP - bytes) & ~3u;
    if (!in_main_ram(base) || !in_main_ram(base + bytes - 1u))
        return 0;
    uint32_t saved[STAGE_MAX / 4u];
    for (uint32_t i = 0; i < bytes / 4u; ++i) {
        saved[i] = psx_mod_read_word(base + 4u * i);
        const uint32_t lo = px[2u * i];
        const uint32_t hi = 2u * i + 1u < n ? px[2u * i + 1u] : 0u;
        psx_mod_write_word(base + 4u * i, lo | (hi << 16));
    }
    const int ok = psx_mod_psyq_load_image((uint32_t)x, (uint32_t)y, (uint32_t)w,
                                           (uint32_t)h, base);
    for (uint32_t i = 0; i < bytes / 4u; ++i)
        psx_mod_write_word(base + 4u * i, saved[i]);
    return ok;
}

uint32_t bbt_fnv1a_pixels(const uint16_t* px, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; ++i) {
        h = (h ^ (uint8_t)px[i]) * 16777619u;
        h = (h ^ (uint8_t)(px[i] >> 8)) * 16777619u;
    }
    return h;
}
