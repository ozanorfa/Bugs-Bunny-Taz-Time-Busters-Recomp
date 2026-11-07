/*
 * The shared Actor_Update override (see actor_update_hub.h) -- Bugs Bunny &
 * Taz: Time Busters (SLUS-01144). Package bbt.shared, hidden feature
 * "actor-update", plugin bbt.shared.actor-update; activated implicitly by
 * the features that require it.
 */
#include <stddef.h>
#include <stdint.h>

#include "actor_update_hub.h"
#include "cpu_state.h"
#include "func_override.h"
#include "mod_plugins.h"

#define PLUGIN              "bbt.shared.actor-update"
#define ADDR_ACTOR_UPDATE   0x800161B8u   /* Actor_Update (class 0x0E: Bugs, Taz) */

/* addiu sp,sp,-0x1C8 / sw s4,0x1B0(sp) / move s4,a0 / lui t7,0x1F80 */
static const uint32_t kActorUpdateGuard[4] = {
    0x27BDFE38u, 0xAFB401B0u, 0x0080A021u, 0x3C0F1F80u,
};

static const BbtActorUpdateHook* g_hooks[BBT_ACTOR_HOOK_COUNT];

void bbt_actor_update_hook(int slot, const BbtActorUpdateHook* hook)
{
    if (slot >= 0 && slot < BBT_ACTOR_HOOK_COUNT)
        g_hooks[slot] = hook;
}

static int actor_update_hub(CPUState* cpu)
{
    const uint32_t actor = cpu->gpr[4];
    uint32_t save[BBT_ACTOR_HOOK_COUNT];
    int on[BBT_ACTOR_HOOK_COUNT];
    int any = 0;
    for (int i = 0; i < BBT_ACTOR_HOOK_COUNT; ++i) {
        save[i] = 0;
        on[i] = g_hooks[i] && g_hooks[i]->pre(cpu, actor, &save[i]);
        any |= on[i];
    }
    if (!any)
        return 0;                   /* the original runs as usual */
    func_override_call_original(cpu);
    for (int i = BBT_ACTOR_HOOK_COUNT - 1; i >= 0; --i)
        if (on[i] && g_hooks[i])
            g_hooks[i]->post(cpu, actor, save[i]);
    return 1;
}

PSX_MOD_CONSTRUCTOR(register_bbt_actor_update_hub)
{
    (void)psx_mod_register_function_override(PLUGIN ":actor-update", ADDR_ACTOR_UPDATE,
                                             actor_update_hub, kActorUpdateGuard, 4,
                                             FO_CREDIT_SELF);
}
