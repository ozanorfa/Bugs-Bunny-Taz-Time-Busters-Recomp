/*
 * bbt_mod_block — see mod_blocks.h. Process-lifetime handles, like the
 * framework's mod memory itself (PSX_MOD_SESSION_RESOURCES is 0 here).
 */
#include "mod_blocks.h"
#include "mod_plugins.h"

static int g_allocated;
static int g_zero_pending;
static uint32_t g_blocks[BBT_BLOCK_COUNT];

void bbt_mod_blocks_session_reset(void)
{
    g_zero_pending = 1;
}

/* Expansion 1 takes ordinary guest stores only (psx_mod_host_write_ram
 * accepts main RAM), one word at a time; every CPU block is word sized. */
static void zero_cpu_blocks(void)
{
    for (int i = 0; i < BBT_BLOCK_COUNT; ++i) {
        if (i == BBT_BLOCK_VIEW2_ARENA || !g_blocks[i])
            continue;
        const uint32_t bytes = bbt_block_bytes((BbtBlock)i);
        for (uint32_t off = 0; off < bytes; off += 4)
            psx_mod_write_word(g_blocks[i] + off, 0);
    }
    psx_mod_counter_add("bbt.memory.blocks_zeroed", 1);
}

uint32_t bbt_mod_block(BbtBlock b)
{
    if (!g_allocated) {
        const int n = bbt_blocks_allocate(psx_mod_alloc_guest_memory, psx_mod_alloc_gpu_dma_memory,
                                          g_blocks);
        /* Nothing allocated (a call from an output scope): try again on the
         * next request, the layout is still untouched. A partial result
         * stays, so the addresses never move. Fresh blocks are zero. */
        if (n > 0) {
            g_allocated = 1;
            g_zero_pending = 0;
            psx_mod_counter_add("bbt.memory.blocks", (uint32_t)n);
            if (n < BBT_BLOCK_COUNT)
                psx_mod_counter_add("bbt.memory.blocks_missing", (uint32_t)(BBT_BLOCK_COUNT - n));
        }
    } else if (g_zero_pending) {
        g_zero_pending = 0;
        zero_cpu_blocks();
    }
    return (unsigned)b < BBT_BLOCK_COUNT ? g_blocks[b] : 0u;
}
