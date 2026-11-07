/*
 * One override for Actor_Update (0x800161B8), shared by several features.
 *
 * The framework refuses two overrides on one address, and one refused
 * override fails the whole mod plan ("Cannot arm function override"). Two of
 * our features need Actor_Update: the own view (player 2 and the third actor
 * update in their own view, for camera-relative steering) and the third
 * actor (it reads the third player's input record in slot 1). So the
 * override lives in the hidden feature bbt.shared/actor-update (plugin
 * bbt.shared.actor-update, src/mods/actor_update_hub.c), which each of those
 * features requires ([[requirement]] in their manifests), and each feature
 * hooks into it from its activation callback.
 *
 * A hook's pre() decides for one call: nonzero = it changed guest state and
 * its post() must run after the original. Hooks run in slot order before the
 * original and in reverse order after it. Only host function pointers are
 * kept here; they are set at activation and cleared by each plugin's session
 * reset (no mutable state between calls).
 */
#ifndef BBT_ACTOR_UPDATE_HUB_H
#define BBT_ACTOR_UPDATE_HUB_H

#include <stdint.h>

struct CPUState;

enum {
    BBT_ACTOR_HOOK_INPUT = 0,   /* the third actor: input record swap */
    BBT_ACTOR_HOOK_VIEW = 1,    /* the own view: view index for the update */
    BBT_ACTOR_HOOK_COUNT
};

typedef struct BbtActorUpdateHook {
    int (*pre)(struct CPUState* cpu, uint32_t actor, uint32_t* save);
    void (*post)(struct CPUState* cpu, uint32_t actor, uint32_t save);
} BbtActorUpdateHook;

/* NULL clears the slot. */
void bbt_actor_update_hook(int slot, const BbtActorUpdateHook* hook);

/* The third actor (bbt.third-character p3), or 0 when there is none: its
 * plugin is off, not ready, or no clone is spawned. */
uint32_t bbt_p3_actor(void);

/* Nonzero while the third actor is out: dead and waiting for the team's
 * respawn (not updated, not drawn). */
int bbt_p3_out(void);

#endif
