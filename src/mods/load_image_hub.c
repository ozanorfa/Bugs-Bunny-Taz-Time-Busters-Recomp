/* The shared LoadImage-core override (see load_image_hub.h). */
#include <stddef.h>
#include <stdint.h>

#include "cpu_state.h"
#include "func_override.h"
#include "load_image_hub.h"
#include "mod_plugins.h"

#define PLUGIN          "bbt.shared.load-image"
#define ADDR_LOAD_IMAGE 0x80071110u   /* PsyQ LoadImage core (RECT *a0, data *a1) */

/* addiu sp,sp,-0x30 / sw s1,0x14(sp) / move s1,a0 / sw s2,0x18(sp) */
static const uint32_t kLoadImageGuard[4] = {
    0x27BDFFD0u, 0xAFB10014u, 0x00808821u, 0xAFB20018u,
};

static BbtLoadImageHook g_hooks[BBT_LOAD_IMAGE_HOOK_COUNT];

void bbt_load_image_hook(int slot, BbtLoadImageHook hook)
{
    if (slot >= 0 && slot < BBT_LOAD_IMAGE_HOOK_COUNT)
        g_hooks[slot] = hook;
}

static int load_image_hub(CPUState* cpu)
{
    const uint32_t rect = cpu->gpr[4], src = cpu->gpr[5];
    int any = 0;
    for (int i = 0; i < BBT_LOAD_IMAGE_HOOK_COUNT; ++i)
        if (g_hooks[i]) {
            g_hooks[i](cpu, rect, src);
            any = 1;
        }
    if (!any)
        return 0;                   /* the original runs as usual */
    func_override_call_original(cpu);
    return 1;
}

PSX_MOD_CONSTRUCTOR(register_bbt_load_image_hub)
{
    (void)psx_mod_register_function_override(PLUGIN ":load-image", ADDR_LOAD_IMAGE,
                                             load_image_hub, kLoadImageGuard, 4,
                                             FO_CREDIT_SELF);
}
