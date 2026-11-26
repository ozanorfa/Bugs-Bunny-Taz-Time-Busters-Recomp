/*
 * One override for the PsyQ LoadImage core (func_80071110, RECT *a0,
 * data *a1), shared by the features that watch or change texture uploads:
 * today only the Turkish flag (bbt.decor: rewrites one wall texture's
 * source; the Transylvania-enemy test that also mapped uploads was removed
 * on 2026-11-26). The framework refuses two armed overrides on one address,
 * so the override lives in the hidden feature
 * bbt.shared/load-image (plugin bbt.shared.load-image,
 * src/mods/load_image_hub.c), which each of those features requires
 * ([[requirement]]). Each feature sets its hook from its activation
 * callback and clears it in its session reset. Hooks run in slot order
 * before the original upload; only host function pointers are kept here.
 */
#ifndef BBT_LOAD_IMAGE_HUB_H
#define BBT_LOAD_IMAGE_HUB_H

#include <stdint.h>

struct CPUState;

enum {
    BBT_LOAD_IMAGE_HOOK_DECOR = 0,    /* bbt.decor: the flag */
    BBT_LOAD_IMAGE_HOOK_COUNT
};

/* rect and src are the call's guest pointers (a0, a1). */
typedef void (*BbtLoadImageHook)(struct CPUState* cpu, uint32_t rect, uint32_t src);

/* NULL clears the slot. */
void bbt_load_image_hook(int slot, BbtLoadImageHook hook);

#endif
