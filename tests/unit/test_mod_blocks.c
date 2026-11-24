/* mod_blocks.c: every plugin's guest memory at a fixed address (BUGS #56). */
#include <string.h>

#include "check.h"
#include "mod_blocks.h"
#include "synth.h"

/* Bump allocators like the framework's (memory.c psx_mod_memory_alloc and
 * psx_mod_gpu_dma_memory_alloc), from their bases. */
static uint32_t g_cpu_used, g_dma_used, g_cpu_calls, g_dma_calls;

static uint32_t bump(uint32_t* used, uint32_t base, uint32_t size, uint32_t alignment)
{
    const uint32_t start = (*used + alignment - 1u) & ~(alignment - 1u);
    *used = start + size;
    return base + start;
}

static uint32_t cpu_alloc(uint32_t size, uint32_t alignment)
{
    ++g_cpu_calls;
    return bump(&g_cpu_used, 0x9F000000u, size, alignment);
}

static uint32_t dma_alloc(uint32_t size, uint32_t alignment)
{
    ++g_dma_calls;
    return bump(&g_dma_used, 0x80800000u, size, alignment);
}

static uint32_t no_alloc(uint32_t size, uint32_t alignment)
{
    (void)size;
    (void)alignment;
    return 0;
}

static void reset(void)
{
    g_cpu_used = g_dma_used = g_cpu_calls = g_dma_calls = 0;
}

static void the_default_layout(void)
{
    /* The addresses measured in a 0.6.0 session with the default features
     * (2026-11-23: own view, third player, third actor, HUD; bbt.p3,
     * bbt.p3actor and bbt.p3hud commands), then the enemy's donor. */
    reset();
    uint32_t a[BBT_BLOCK_COUNT];
    CHECK_EQ(bbt_blocks_allocate(cpu_alloc, dma_alloc, a), BBT_BLOCK_COUNT);
    CHECK_EQ(a[BBT_BLOCK_VIEW_LISTS], 0x9F000000u);
    CHECK_EQ(a[BBT_BLOCK_VIEW_CAMERAS], 0x9F002000u);
    CHECK_EQ(a[BBT_BLOCK_P3_RECORD], 0x9F002050u);
    CHECK_EQ(a[BBT_BLOCK_P3_ACTOR], 0x9F002090u);
    CHECK_EQ(a[BBT_BLOCK_P3_HUD], 0x9F022090u);
    CHECK_EQ(a[BBT_BLOCK_TRCA01_DONOR], 0x9F0220D0u);
    CHECK_EQ(a[BBT_BLOCK_VIEW2_ARENA], 0x80800000u);
    /* the used sizes a state load between PCs compares */
    CHECK_EQ(g_cpu_used, 0x420D0u);
    CHECK_EQ(g_dma_used, BBT_BLOCK_VIEW2_ARENA_BYTES);
    CHECK_EQ(g_cpu_calls, BBT_BLOCK_COUNT - 1);
    CHECK_EQ(g_dma_calls, 1);
}

static void the_sizes_are_the_plugins(void)
{
    CHECK_EQ(bbt_block_bytes(BBT_BLOCK_P3_ACTOR), 0x20000u);
    CHECK_EQ(bbt_block_bytes(BBT_BLOCK_VIEW2_ARENA), 3u * 0x40000u);
    CHECK_EQ(bbt_block_bytes(BBT_BLOCK_COUNT), 0u);
}

static void a_failed_allocator_gives_zero(void)
{
    reset();
    uint32_t a[BBT_BLOCK_COUNT];
    memset(a, 0xAA, sizeof a);
    CHECK_EQ(bbt_blocks_allocate(no_alloc, no_alloc, a), 0);
    for (int i = 0; i < BBT_BLOCK_COUNT; ++i)
        CHECK_EQ(a[i], 0u);
    CHECK_EQ(bbt_blocks_allocate(cpu_alloc, NULL, a), BBT_BLOCK_COUNT - 1);
    CHECK_EQ(a[BBT_BLOCK_VIEW2_ARENA], 0u);
    CHECK_EQ(a[BBT_BLOCK_P3_ACTOR], 0x9F002090u);
}

const TestCase kModBlocksTests[] = {
    {"the_default_layout", the_default_layout},
    {"the_sizes_are_the_plugins", the_sizes_are_the_plugins},
    {"a_failed_allocator_gives_zero", a_failed_allocator_gives_zero},
};
const int kModBlocksTestCount = (int)(sizeof(kModBlocksTests) / sizeof(kModBlocksTests[0]));
