/*
 * The third player's HUD panel — Bugs Bunny & Taz: Time Busters
 * (SLUS-01144). Phase 5.
 *
 * Feature "p3" of package bbt.third-character, plugin
 * bbt.third-character.p3-hud.
 *
 * How the stock HUD works (Ghidra; NOTES.md "The third player's HUD panel"):
 * - The HUD actor owns 32 sprite nodes, pointers at 0x800929F8[i], created
 *   by FUN_80060B50 when a level's HUD is set up. A node is a textured quad
 *   (xy / uv / clut / tpage like a POLY_FT4 from +8), hidden while +6 bit
 *   15 is set.
 * - FUN_8005FCFC(sprite, images, frame, rect) gives a sprite a frame of the
 *   HUD image table (*0x8008DB70, 16 bytes a frame) and shows it;
 *   FUN_8005FE1C(sprite, pos, slide) moves it to pos + the slide offset.
 *   0x80082240[i] holds each sprite's screen position (s16 x, y; the
 *   screen is 512 x 240 around its centre).
 * - A health panel is FUN_80061150(desc, slide, value, images), called by
 *   the HUD update FUN_80061C6C for Bugs (desc 0x800823F4, sprites 0 bar /
 *   1 face) and Taz (0x80082404, sprites 2 / 3). The 16-byte descriptor:
 *   [0] bar sprite, [1] face sprite, +2 s16 bar width shown (-1 = new),
 *   +4 s16 timer, [6] flags (1: the bar shrinks toward its right end),
 *   [7] [8] bar offset from the face, [9] full value, [10] bar width,
 *   [11] bar frame, +0xC a linked descriptor (0 = none). The slide state
 *   (16 bytes, template at 0x800822E4 + 16 n) moves the panel in from above
 *   when the value changes, keeps it for 60 frames and moves it out.
 * - Sprites 15-21 and 23-25 are never used in a level (no frame in the
 *   start table 0x800823B4, no code reference; the digits use 6-14).
 *
 * The third player's panel uses sprites 15 (bar) and 16 (face) with a
 * descriptor and slide state of its own (guest memory): Taz's face
 * mirrored, Bugs' bar, below Bugs' panel on the left (offline three bands:
 * top centre, there is no room under Bugs). After each HUD update
 * the panel runs through the game's own FUN_80061150 with the third actor's
 * health (0 while it is out), so it slides in and out like the stock ones.
 * Guest state only: the same on every netplay peer.
 *
 * Tests: {"cmd":"mod_cmd","name":"bbt.p3hud","args":"show"}, counters
 * bbt.third-character.p3-hud.*, tests/test_game_ownview3.py.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "actor_update_hub.h"
#include "cpu_state.h"
#include "func_override.h"
#include "mod_plugins.h"
#include "psx_netplay.h"

#define PLUGIN "bbt.third-character.p3-hud"

#define ADDR_HUD_UPDATE   0x80061C6Cu   /* FUN_80061C6C, once a frame */
#define ADDR_HUD_PANEL    0x80061150u   /* FUN_80061150(desc, slide, value, images) */
#define ADDR_SET_IMAGE    0x8005FCFCu   /* FUN_8005FCFC(sprite, images, frame, rect) */
#define ADDR_SPRITES      0x800929F8u   /* 32 sprite node pointers */
#define ADDR_ICONS        0x80092A78u   /* 15 3D icon actors (+4 bit 1 shown) */
#define ADDR_SPRITE_POS   0x80082240u   /* s16 x, y per sprite */
#define ADDR_HUD_READY    0x8008DB68u   /* s16: 1 once the level's HUD is set up */
#define ADDR_HUD_LEVEL    0x8008DB64u   /* level the HUD was set up for (0 title, 0x5D other layout) */
#define ADDR_HUD_MODE     0x80093954u   /* u8: 0x10 frozen (no update unless something shows) */
#define ADDR_HUD_OFF      0x8008DE74u   /* s16: nonzero, no HUD update */
#define ADDR_HUD_IMAGES   0x8008DB70u   /* HUD image table */
#define ADDR_HUD_P1_ID    0x8008DB80u   /* s8: player 1's identity as the HUD last saw it */
#define ADDR_P1           0x8008DD04u
#define ADDR_TAZ_GATE     0x80092960u   /* u16: bit 9 set, Taz's panel is not updated */
#define ADDR_TAZ_SLIDE    0x800822F4u   /* Taz's slide template (from above) */

#define SPRITE_COUNT 32u
#define ICON_COUNT   15u
#define SPR_BAR      15u
#define SPR_FACE     16u
#define IMG_TAZ_FACE 11u                /* 116 x 46 */
#define IMG_BUGS_BAR 13u                /* 80 x 8 */
#define FACE_W       116
#define BAR_W        80
#define FACE_X       (-244)             /* Bugs' face is at -244, -102 .. -57 */
#define FACE_Y       (-55)
#define BAND_FACE_X  (-58)              /* three bands: top centre, between the others */
#define BAND_FACE_Y  (-104)
#define FULL_HEALTH  6

#define NODE_HIDE    0x8000u            /* node +6 */
#define A_HEALTH     0x3Cu

/* guest block */
#define BLK_SIZE   0x40u
#define B_MAGIC    0x00u
#define B_FACE     0x04u                /* the face sprite's node when set up */
#define B_SHOWN    0x08u                /* 1 once the panel ran in this HUD */
#define B_DESC     0x10u
#define B_SLIDE    0x20u
#define HUD_MAGIC  0x44485033u          /* 'P3HD' */

/* lh a0,0x1E4(gp) / addiu sp,sp,-0x58 / sw ra,0x54(sp) / sw s6,0x50(sp) */
static const uint32_t kHudGuard[4] = {0x878401E4u, 0x27BDFFA8u, 0xAFBF0054u, 0xAFB60050u};

static int g_active;
static uint32_t g_block;

#define count(event, n) psx_mod_counter_add(PLUGIN "." event, (uint32_t)(n))

static uint32_t sprite(uint32_t i)
{
    return psx_mod_read_word(ADDR_SPRITES + i * 4u);
}

static int in_main_ram(uint32_t a)
{
    return a >= 0x80000000u && a < 0x80200000u;
}

static void hide(uint32_t node)
{
    if (in_main_ram(node))
        psx_mod_write_half(node + 6u, (uint16_t)(psx_mod_read_half(node + 6u) | NODE_HIDE));
}

/* The stock update stops early while frozen (mode 0x10) unless a sprite or
 * a 3D icon shows; the panels are updated only past that point. */
static int hud_runs(void)
{
    if (psx_mod_read_half(ADDR_HUD_READY) == 0)
        return 0;
    const uint32_t level = psx_mod_read_word(ADDR_HUD_LEVEL);
    if (level == 0u || level == 0x5Du || psx_mod_read_half(ADDR_HUD_OFF) != 0)
        return 0;
    if (!in_main_ram(sprite(SPR_FACE)) || !in_main_ram(sprite(SPR_BAR)))
        return 0;
    if ((psx_mod_read_byte(ADDR_HUD_MODE) & 0x10u) == 0)
        return 1;
    for (uint32_t i = 0; i < SPRITE_COUNT; ++i) {
        const uint32_t n = sprite(i);
        if (in_main_ram(n) && (psx_mod_read_half(n + 6u) & NODE_HIDE) == 0)
            return 1;
    }
    for (uint32_t i = 0; i < ICON_COUNT; ++i) {
        const uint32_t n = psx_mod_read_word(ADDR_ICONS + i * 4u);
        if (in_main_ram(n) && (psx_mod_read_half(n + 4u) & 2u))
            return 1;
    }
    return 0;
}

/* A new level's HUD: our two sprites are fresh (hidden, no frame). */
static void set_up(CPUState* cpu, uint32_t ra)
{
    const uint32_t images = psx_mod_read_word(ADDR_HUD_IMAGES);
    const uint32_t face = sprite(SPR_FACE);
    (void)psx_mod_call_guest(cpu, ADDR_SET_IMAGE, ra, SPR_FACE, images, IMG_TAZ_FACE, 0);
    /* mirrored: the silhouette on the left, its frame on the right */
    static const uint32_t kU[4] = {0x0Cu, 0x14u, 0x1Cu, 0x24u};
    const uint8_t u0 = psx_mod_read_byte(face + kU[0]);
    const uint8_t u1 = psx_mod_read_byte(face + kU[1]);
    psx_mod_write_byte(face + kU[0], u1);
    psx_mod_write_byte(face + kU[2], u1);
    psx_mod_write_byte(face + kU[1], u0);
    psx_mod_write_byte(face + kU[3], u0);
    hide(face);
    hide(sprite(SPR_BAR));
    psx_mod_write_word(ADDR_SPRITE_POS + SPR_BAR * 4u, 0u);

    /* the descriptor: Taz's, mirrored (bar next to the face, left-anchored) */
    const uint8_t desc[16] = {
        SPR_BAR, SPR_FACE, 0xFF, 0xFF,  /* bar width shown: new */
        0, 0,                           /* timer */
        0,                              /* flags: shrinks toward its left end */
        (uint8_t)(FACE_W - 3 - BAR_W),  /* Taz's bar is 3 from his face's left edge */
        24,                             /* as Taz's */
        FULL_HEALTH, BAR_W, IMG_BUGS_BAR,
        0, 0, 0, 0,                     /* no linked descriptor */
    };
    for (uint32_t i = 0; i < 16u; i += 4)
        psx_mod_write_word(g_block + B_DESC + i, (uint32_t)desc[i] | (uint32_t)desc[i + 1] << 8 |
                                                     (uint32_t)desc[i + 2] << 16 | (uint32_t)desc[i + 3] << 24);
    for (uint32_t i = 0; i < 16u; i += 4)
        psx_mod_write_word(g_block + B_SLIDE + i, psx_mod_read_word(ADDR_TAZ_SLIDE + i));
    psx_mod_write_word(g_block + B_FACE, face);
    psx_mod_write_word(g_block + B_SHOWN, 0);
    psx_mod_write_word(g_block + B_MAGIC, HUD_MAGIC);
    count("setup", 1);
}

static int hud_wrap(CPUState* cpu)
{
    if (!g_active || !g_block)
        return 0;
    const uint32_t ra = cpu->gpr[31];
    const int was_ready = psx_mod_read_half(ADDR_HUD_READY) != 0;
    func_override_call_original(cpu);
    if (!hud_runs())
        return 1;
    if (!was_ready || psx_mod_read_word(g_block + B_MAGIC) != HUD_MAGIC ||
        psx_mod_read_word(g_block + B_FACE) != sprite(SPR_FACE))
        set_up(cpu, ra);

    const uint32_t third = bbt_p3_actor();
    if (!third) {
        if (psx_mod_read_word(g_block + B_SHOWN)) {
            hide(sprite(SPR_FACE));
            hide(sprite(SPR_BAR));
            psx_mod_write_word(g_block + B_SHOWN, 0);
        }
        return 1;
    }
    /* the same gate as Taz's panel; FUN_80061150 also re-reads player 1's
     * identity, which the stock panels have already done this frame */
    if (psx_mod_read_half(ADDR_TAZ_GATE) & 0x200u)
        return 1;
    const uint32_t p1 = psx_mod_read_word(ADDR_P1);
    if (!in_main_ram(p1) || (int8_t)psx_mod_read_byte(ADDR_HUD_P1_ID) != (int)psx_mod_read_half(p1 + 0x34u))
        return 1;
    /* Where the panel goes. A full screen (one view, or a netplay peer's own
     * view, which is drawn full screen from the shared frame): under Bugs'
     * panel. Offline three bands (72 lines, the HUD's top row at the band's
     * top, see own_view.c): no room under Bugs, so the top centre. Netplay
     * is the same on every peer of a match. */
    const int bands = bbt_view_count() == 3u && !psx_netplay_active();
    psx_mod_write_word(ADDR_SPRITE_POS + SPR_FACE * 4u,
                       (uint32_t)(uint16_t)(bands ? BAND_FACE_X : FACE_X) |
                       (uint32_t)(uint16_t)(bands ? BAND_FACE_Y : FACE_Y) << 16);
    int health = bbt_p3_out() ? 0 : (int16_t)psx_mod_read_half(third + A_HEALTH);
    if (health < 0)
        health = 0;
    if (health > FULL_HEALTH)
        health = FULL_HEALTH;
    (void)psx_mod_call_guest(cpu, ADDR_HUD_PANEL, ra, g_block + B_DESC, g_block + B_SLIDE,
                             (uint32_t)health, psx_mod_read_word(ADDR_HUD_IMAGES));
    /* the hit flash, as Taz's panel's: while the hit counter runs and the
     * face shows, the bar blinks with count bit 2 */
    const uint8_t flash = bbt_p3_hit_flash();
    const uint32_t face = sprite(SPR_FACE), bar = sprite(SPR_BAR);
    if ((flash & 0x80u) && (flash & 0x3Cu) < 0x3Cu && !(psx_mod_read_half(face + 6u) & NODE_HIDE)) {
        if (flash & 4u)
            psx_mod_write_half(bar + 6u, (uint16_t)(psx_mod_read_half(bar + 6u) & ~NODE_HIDE));
        else
            hide(bar);
    }
    psx_mod_write_word(g_block + B_SHOWN, 1);
    count("update", 1);
    return 1;
}

static void activate(void)
{
    g_active = 1;
    if (!g_block)
        g_block = psx_mod_alloc_guest_memory(BLK_SIZE, 16);
}

static void session_reset(void)
{
    g_active = 0;
}

static int p3hud_command(const char* args, char* reply, int cap)
{
    (void)args;
    snprintf(reply, (size_t)cap,
             "{\"ok\":true,\"active\":%d,\"block\":\"%08X\",\"bar\":%u,\"face\":%u,"
             "\"face_x\":%d,\"face_y\":%d}",
             g_active, g_block, SPR_BAR, SPR_FACE, FACE_X, FACE_Y);
    return 1;
}

PSX_MOD_CONSTRUCTOR(register_bbt_third_hud)
{
    (void)psx_mod_register_activation_plugin(PLUGIN, activate);
    (void)psx_mod_register_function_override(PLUGIN ":hud", ADDR_HUD_UPDATE, hud_wrap,
                                             kHudGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_session_reset_plugin(PLUGIN, session_reset);
    (void)psx_mod_register_command("bbt.p3hud", p3hud_command);
}
