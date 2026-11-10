/*
 * Decorations — Bugs Bunny & Taz: Time Busters (SLUS-01144).
 *
 * Feature "turkish-flag" of package bbt.decor (plugin bbt.decor): a Turkish
 * flag hangs from the arched windows of Granny's house in Granwich, the way
 * flags are hung from windows on national holidays (NOTES.md "Turkish
 * flag"). Both side walls use the same window texture, so the house shows
 * four flags, two on each side.
 *
 * The window is part of one 64x64 4-bit wall texture that Granwich uploads
 * to VRAM (256,448) 16x64 words, with its 16-colour CLUT at (128,475)
 * (LEVEL1/GRANNY.BZZ chunk 1, CLUT at 0xCF9B0, texels at 0xCF9DC). This
 * plugin wraps the PsyQ LoadImage core (func_80071110, RECT *a0, data *a1)
 * and, when one of those two uploads carries exactly the stock bytes
 * (FNV-1a 64 of the source), rewrites the source in guest RAM before the
 * upload:
 *   - CLUT: the two least used colours, 10 and 13 (42 and 69 of the 4096
 *     texels), become the flag's red and white;
 *   - texels: those two colours are drawn with their nearest neighbours
 *     (10 -> 9, 13 -> 14), then the 30x20 flag below is painted at texel
 *     (17,32), over the lower panes and the sill.
 * The flag is our own pixel art at the game's resolution, so every
 * renderer shows it, and in a match every peer makes the same change in
 * the shared simulation from the same disc (no host state, nothing kept
 * between calls). Nothing from the disc is stored here: only hashes.
 *
 * The hook is shared with the Transylvania-enemy test feature through
 * bbt.shared/load-image, so both can be on.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cpu_state.h"
#include "load_image_hub.h"
#include "mod_plugins.h"

#define PLUGIN "bbt.decor"

#define WALL_X 256
#define WALL_Y 448
#define WALL_W 16                      /* VRAM words: 64 texels of 4 bits */
#define WALL_H 64
#define WALL_BYTES (WALL_W * 2 * WALL_H)
#define CLUT_X 128
#define CLUT_Y 475
#define WALL_FNV 0xACC8A04410331D5Cull
#define CLUT_FNV 0x9FBD073BA3E89981ull

#define RED_INDEX   10u
#define WHITE_INDEX 13u
#define RED_WORD    0x0C3Cu            /* (227,10,23) in 5:5:5 */
#define WHITE_WORD  0x7FFFu
#define FLAG_X 17
#define FLAG_Y 32
#define FLAG_W 30
#define FLAG_H 20

/* '#' white (crescent and star), '.' red. Proportions of the Turkish Flag
 * Law, sampled at 6x6 for the crescent; the star is drawn by hand with one
 * point toward the hoist. */
static const char* const kFlag[FLAG_H] = {
    "..............................",
    "..............................",
    "..............................",
    "..............................",
    "..............................",
    "........#####.................",
    ".......###....................",
    "......###.....................",
    "......##.........#............",
    "......##........####..........",
    "......##.......####...........",
    "......##........####..........",
    "......###........#............",
    ".......###....................",
    "........#####.................",
    "..............................",
    "..............................",
    "..............................",
    "..............................",
    "..............................",
};

static uint64_t fnv1a64(const uint8_t* p, uint32_t n)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (uint32_t i = 0; i < n; ++i)
        h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

static int in_main_ram(uint32_t a)
{
    return a >= 0x80000000u && a < 0x80200000u;
}

static void read_bytes(uint32_t addr, uint8_t* out, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        out[i] = psx_mod_read_byte(addr + i);
}

/* The flag over the stock wall texels (exposed for the unit tests). */
void bbt_decor_paint_flag(uint8_t* texels)
{
    for (uint32_t i = 0; i < WALL_BYTES; ++i) {
        uint8_t lo = texels[i] & 15u, hi = texels[i] >> 4;
        if (lo == RED_INDEX) lo = 9u; else if (lo == WHITE_INDEX) lo = 14u;
        if (hi == RED_INDEX) hi = 9u; else if (hi == WHITE_INDEX) hi = 14u;
        texels[i] = (uint8_t)(lo | (hi << 4));
    }
    for (int y = 0; y < FLAG_H; ++y) {
        for (int x = 0; x < FLAG_W; ++x) {
            const uint8_t c = (uint8_t)(kFlag[y][x] == '#' ? WHITE_INDEX : RED_INDEX);
            const int tx = FLAG_X + x;
            uint8_t* b = &texels[(FLAG_Y + y) * (WALL_W * 2) + tx / 2];
            *b = (tx & 1) ? (uint8_t)((*b & 0x0Fu) | (c << 4)) : (uint8_t)((*b & 0xF0u) | c);
        }
    }
}

/* The flag's two colours in the stock CLUT (16 little-endian words); the
 * game uploads it with the semi-transparency bit set, which is kept. */
void bbt_decor_paint_clut(uint8_t* clut)
{
    const uint16_t red = (uint16_t)(RED_WORD | ((clut[RED_INDEX * 2 + 1] << 8) & 0x8000u));
    const uint16_t white = (uint16_t)(WHITE_WORD | ((clut[WHITE_INDEX * 2 + 1] << 8) & 0x8000u));
    clut[RED_INDEX * 2] = (uint8_t)red;
    clut[RED_INDEX * 2 + 1] = (uint8_t)(red >> 8);
    clut[WHITE_INDEX * 2] = (uint8_t)white;
    clut[WHITE_INDEX * 2 + 1] = (uint8_t)(white >> 8);
}

/* FNV-1a 64 of the CLUT's colours without the semi-transparency bit. */
static uint64_t clut_fnv(const uint8_t* clut)
{
    uint8_t c[32];
    for (int i = 0; i < 32; ++i)
        c[i] = (i & 1) ? (uint8_t)(clut[i] & 0x7Fu) : clut[i];
    return fnv1a64(c, sizeof c);
}

static void load_image_hook(struct CPUState* cpu, uint32_t rect, uint32_t src)
{
    (void)cpu;
    if (in_main_ram(rect) && in_main_ram(rect + 7u)) {
        const int x = (int16_t)psx_mod_read_half(rect), y = (int16_t)psx_mod_read_half(rect + 2u);
        const int w = (int16_t)psx_mod_read_half(rect + 4u), h = (int16_t)psx_mod_read_half(rect + 6u);
        if (x == WALL_X && y == WALL_Y && w == WALL_W && h == WALL_H &&
            in_main_ram(src) && in_main_ram(src + WALL_BYTES - 1u)) {
            static uint8_t texels[WALL_BYTES];
            read_bytes(src, texels, WALL_BYTES);
            if (fnv1a64(texels, WALL_BYTES) == WALL_FNV) {
                bbt_decor_paint_flag(texels);
                (void)psx_mod_host_write_ram(src, texels, WALL_BYTES);
                psx_mod_counter_add(PLUGIN ".flag_texels", 1u);
            }
        } else if (w > 0 && h > 0 && x <= CLUT_X && CLUT_X + 16 <= x + w &&
                   y <= CLUT_Y && CLUT_Y < y + h) {
            /* the CLUT row may come in a larger upload */
            const uint32_t at = src + (uint32_t)(((CLUT_Y - y) * w + (CLUT_X - x)) * 2);
            if (in_main_ram(src) && in_main_ram(at) && in_main_ram(at + 31u)) {
                uint8_t clut[32];
                read_bytes(at, clut, sizeof clut);
                if (clut_fnv(clut) == CLUT_FNV) {
                    bbt_decor_paint_clut(clut);
                    (void)psx_mod_host_write_ram(at, clut, sizeof clut);
                    psx_mod_counter_add(PLUGIN ".flag_clut", 1u);
                }
            }
        }
    }
}

static void decor_activate(void) { bbt_load_image_hook(BBT_LOAD_IMAGE_HOOK_DECOR, load_image_hook); }
static void decor_session_reset(void) { bbt_load_image_hook(BBT_LOAD_IMAGE_HOOK_DECOR, NULL); }

PSX_MOD_CONSTRUCTOR(register_bbt_decor)
{
    (void)psx_mod_register_activation_plugin(PLUGIN, decor_activate);
    (void)psx_mod_register_session_reset_plugin(PLUGIN, decor_session_reset);
}
