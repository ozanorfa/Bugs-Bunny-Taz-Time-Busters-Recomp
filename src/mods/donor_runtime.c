/*
 * Donor loader — see donor_runtime.h. donor.c reads Daffy's model and
 * animations from his level file, donor_add_spin appends the synthetic
 * spin, donor_tex.c rebuilds his texels and CLUTs from the level's TIM
 * list. (Written for the Transylvania enemy in Phase 3; that donor was
 * removed on 2026-11-26.)
 */
#include "donor_runtime.h"

#include <stdlib.h>
#include <string.h>

#include "mod_plugins.h"
#include "daffy_profile.h"
#include "p3_profile.h"

static BbtDonorRuntime g_daffy;

const BbtDonorRuntime* bbt_daffy_runtime(void)
{
    return &g_daffy;
}

/* One character from one level file: by actor type, or by resource ids
 * when `ids` is given. */
static int load(BbtDonorRuntime* rt, const char* name, uint32_t actor_type,
                const uint16_t* ids, int id_count, uint16_t model_sub)
{
    if (rt->state != 0)
        return rt->state;
    uint32_t size = 0;
    if (!psx_mod_read_disc_file(name, NULL, 0, &size) || size == 0)
        return 0;                  /* no disc mounted yet: try again later */
    uint8_t* file = (uint8_t*)malloc(size);
    if (!file)
        return 0;
    uint32_t got = 0;
    int ok = psx_mod_read_disc_file(name, file, size, &got) && got == size &&
             (ids ? donor_extract_ids(file, size, ids, id_count, &rt->donor)
                  : donor_extract(file, size, actor_type, &rt->donor));
    if (ok) {
        /* Without the synthetic spin, the spin falls back to a default
         * pose (no tornado): reported, not fatal. */
        rt->spin_node = donor_add_spin(&rt->donor, DONOR_SUB_SPIN, DONOR_SPIN_FRAMES);
        /* Textures: without them the donor keeps its own level's texture
         * positions (right only there): reported, not fatal. */
        const DonorBlob* model = NULL;
        for (int i = 0; i < rt->donor.model_count; ++i)
            if (rt->donor.models[i].sub == model_sub)
                model = &rt->donor.models[i];
        rt->tex_ready = model && donor_tex_extract(file, size, &rt->donor, model, &rt->tex);
        rt->state = 1;
    } else {
        donor_free(&rt->donor);
        rt->state = -1;
    }
    free(file);
    return rt->state;
}

int bbt_daffy_runtime_load(void)
{
    return load(&g_daffy, DAFFY_FILE, 0, kDaffyResIds, DAFFY_RES_COUNT, DAFFY_MODEL_SUB);
}

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
