/*
 * The guest memory of every bbt.* plugin, at fixed addresses (BUGS #56).
 *
 * The framework's mod memory only grows for the whole process
 * (psx_mod_alloc_guest_memory / psx_mod_alloc_gpu_dma_memory, no session
 * reset for this title). When each plugin took its block on its own first
 * activation, the addresses depended on what had been played since the game
 * was opened: a PC whose first session had other features on placed the
 * blocks elsewhere, and those addresses go into main RAM (the third actor's
 * resource list, the own view's lists). A state load between PCs also needs
 * the same used size on both (memory.c psx_mod_memory_snapshot_validate).
 *
 * So the first request from any plugin allocates every block, in the order
 * of BbtBlock, whatever is enabled; later requests return the same
 * addresses. The CPU blocks keep the order the default features got them in
 * up to 0.6.0 (own view, third player, third actor, HUD), so a session with
 * the defaults uses the same addresses as before. Nothing else in this
 * title allocates mod memory (the framework's own allocators are called
 * only by title code).
 */
#ifndef BBT_MOD_BLOCKS_H
#define BBT_MOD_BLOCKS_H

#include <stdint.h>

typedef enum BbtBlock {
    BBT_BLOCK_VIEW_LISTS,      /* own_view.c: players 2 and 3's draw lists */
    BBT_BLOCK_VIEW_CAMERAS,    /* own_view.c: their camera banks */
    BBT_BLOCK_P3_RECORD,       /* third_player.c: the third player's input record */
    BBT_BLOCK_P3_ACTOR,        /* third_actor.c: the third actor's state, resources, image */
    BBT_BLOCK_P3_HUD,          /* third_hud.c: the third player's panel */
    BBT_BLOCK_TRCA01_DONOR,    /* third_character.c: the Transylvania enemy's donor */
    BBT_BLOCK_VIEW2_ARENA,     /* own_view.c: GPU DMA aperture, the other views' primitives */
    BBT_BLOCK_COUNT
} BbtBlock;

#define BBT_BLOCK_VIEW_LISTS_BYTES   0x2000u    /* 2 x LIST_BYTES */
#define BBT_BLOCK_VIEW_CAMERAS_BYTES 72u        /* 2 x CAMERA_BANK_BYTES */
#define BBT_BLOCK_P3_RECORD_BYTES    0x40u
#define BBT_BLOCK_P3_ACTOR_BYTES     0x20000u
#define BBT_BLOCK_P3_HUD_BYTES       0x40u
#define BBT_BLOCK_TRCA01_DONOR_BYTES 0x20000u
#define BBT_BLOCK_VIEW2_ARENA_BYTES  0xC0000u   /* 3 x VIEW2_ARENA_BYTES */
#define BBT_BLOCK_ALIGN              16u

/* A compile-time check that a plugin's own size matches its block. */
#define BBT_BLOCK_SIZE_IS(name, expr, bytes) \
    typedef char bbt_block_size_##name[(expr) == (bytes) ? 1 : -1]

typedef uint32_t (*BbtBlockAllocFn)(uint32_t size, uint32_t alignment);

/* Pure part (mod_blocks.c, unit tested): every block in order, the CPU
 * blocks from cpu(), the GPU DMA arena from dma(). out[] gets 0 for a block
 * that could not be allocated. Returns the number allocated. */
int bbt_blocks_allocate(BbtBlockAllocFn cpu, BbtBlockAllocFn dma, uint32_t out[BBT_BLOCK_COUNT]);
uint32_t bbt_block_bytes(BbtBlock b);

/* The plugins' entry (mod_blocks_runtime.c): the block's guest address,
 * allocating all of them on the first call. 0 if it could not be. Call it
 * from activation (host setup), not from a draw callback. */
uint32_t bbt_mod_block(BbtBlock b);

/* From the plugins' session-reset callbacks (they run before every plan,
 * also plans without that plugin; host state only). The first
 * bbt_mod_block() of the next activation then zeroes every CPU block, so a
 * PC back in a LAN room for a rematch holds the same bytes as one that
 * just started: the blocks outlive the session, and e.g. the third actor
 * keeps its state while its magic word is set (the Fighting Force session
 * measured the same, 2026-11-10). The GPU DMA arena is not zeroed: the own
 * view rewrites its primitives every frame. */
void bbt_mod_blocks_session_reset(void);

#endif
