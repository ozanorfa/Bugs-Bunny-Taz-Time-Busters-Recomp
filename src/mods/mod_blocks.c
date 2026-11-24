/*
 * Fixed-order guest memory blocks — see mod_blocks.h. No framework calls
 * here: the allocators are passed in (tests/unit/test_mod_blocks.c).
 */
#include "mod_blocks.h"

static const struct {
    uint32_t bytes;
    uint8_t dma;
} kBlocks[BBT_BLOCK_COUNT] = {
    [BBT_BLOCK_VIEW_LISTS] = {BBT_BLOCK_VIEW_LISTS_BYTES, 0},
    [BBT_BLOCK_VIEW_CAMERAS] = {BBT_BLOCK_VIEW_CAMERAS_BYTES, 0},
    [BBT_BLOCK_P3_RECORD] = {BBT_BLOCK_P3_RECORD_BYTES, 0},
    [BBT_BLOCK_P3_ACTOR] = {BBT_BLOCK_P3_ACTOR_BYTES, 0},
    [BBT_BLOCK_P3_HUD] = {BBT_BLOCK_P3_HUD_BYTES, 0},
    [BBT_BLOCK_TRCA01_DONOR] = {BBT_BLOCK_TRCA01_DONOR_BYTES, 0},
    [BBT_BLOCK_VIEW2_ARENA] = {BBT_BLOCK_VIEW2_ARENA_BYTES, 1},
};

uint32_t bbt_block_bytes(BbtBlock b)
{
    return (unsigned)b < BBT_BLOCK_COUNT ? kBlocks[b].bytes : 0u;
}

int bbt_blocks_allocate(BbtBlockAllocFn cpu, BbtBlockAllocFn dma, uint32_t out[BBT_BLOCK_COUNT])
{
    int n = 0;
    for (int i = 0; i < BBT_BLOCK_COUNT; ++i) {
        const BbtBlockAllocFn alloc = kBlocks[i].dma ? dma : cpu;
        out[i] = alloc ? alloc(kBlocks[i].bytes, BBT_BLOCK_ALIGN) : 0u;
        n += out[i] != 0;
    }
    return n;
}
