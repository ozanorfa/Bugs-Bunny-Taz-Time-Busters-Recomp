/*
 * Quick start — Bugs Bunny & Taz: Time Busters (SLUS-01144).
 *
 * Package bbt.quick-start, feature "quick-start" (plugin bbt.quick-start).
 * Shortens the way from power-on to playing; options pick what is skipped.
 * Everything here changes guest state only, from guest state only, so every
 * netplay peer that selects the same options does the same (content
 * negotiation carries the selection; NOTES.md "Quick start").
 *
 * The boot path (verified in Ghidra and at runtime, NOTES.md):
 *
 *  - Logos. main (0x800122A8) calls FUN_80011A44 once (0x80012330). It
 *    sets up the display (DAT_8008DDBC = 0, FUN_800119D4), then shows the
 *    three LOADING pictures, Bouze ids 0x4A, 0x4B and 0x49 (FUN_8006AEA8 /
 *    FUN_8006AF8C load each one, FUN_80011970 clears VRAM), each for 300
 *    frames during which no button is accepted (FUN_800118F4(5, 0): pad
 *    mask 0), and finally sets the level id 0x80010000 to 2 (LANGUAGE).
 *    main then reads the id again (0x8001233C) and loads that level with
 *    FUN_8006AEA8 / Level_LoadParse itself, so nothing after the logos
 *    depends on them. "skip_logos" replaces FUN_80011A44 with its first and
 *    last steps: the display set-up and the level id 2.
 *
 *  - Intro. NEW GAME -> START GAME fires the title level's exit trigger
 *    (script interpreter FUN_8002A384, 0x8002BC5C: target id from the
 *    trigger, then the exit flag 0x8008DE74 = 1) with level 0x53 (CCINTRO,
 *    a ~6 minute in-engine cutscene); ✕ in the intro fires its own trigger
 *    with level 4 (GRANNY). On every level change main calls FUN_8006B500
 *    exactly once (0x80012830), after the new id is set and before the
 *    level is loaded, with a0 = the file table's word for that id
 *    (0x8008261C + id * 16, the file size; FUN_8006B500 only sizes the
 *    loading bar with it), and reads the id again afterwards (0x80012860,
 *    0x80012870). "skip_intro" is a pre-hook there: a change to 0x53 becomes
 *    a change to 4 (a0 recomputed), so the intro is never loaded.
 *
 *  - Loading screens. Every load is at least as long as the loading screen's
 *    iris (a closing circle): FUN_8006B500 sets its length to
 *    size * 60 / 350000 frames (at most 500; Granwich 410), the iris
 *    shrinks by 0x1000 / length every VSync (FUN_8006B064, from the VSync
 *    callback func_8006B3BC, which clears the loading flag 0x8008DDBC when
 *    it is closed), and Level_LoadParse waits at its end until the flag is
 *    no longer 1 (0x8006AD88). The variables are the loading screen's own
 *    (gp-relative words: 0x8008DC74 length, 0x8008DC78 step, 0x8008DC7C
 *    progress; no other code reads them). "short_loading" lets the
 *    original FUN_8006B500 run, then sets the length to IRIS_FRAMES: a load
 *    then lasts as long as its disc reads, with the iris still shown.
 *
 *  - Disc reads. "cd_speed" (1 = authentic) divides the emulated CD sector
 *    delay through the framework's game-owned loading knob
 *    (psx_mod_set_disc_speed; XA streaming keeps authentic timing, the
 *    runtime applies it after the BIOS and resets it every session). This
 *    changes when the game gets its CD interrupts: values above
 *    CD_SPEED_MAX are refused here (8 hung the game at the title load,
 *    NOTES.md "Quick start").
 *
 *  - Menus (NOTES.md "Straight into the game"). The language screen is
 *    level 2; its choice is the byte 0x80010093 (0 English, 1 French,
 *    3 Spanish: kept by FUN_80011834, clamped by Level_LoadParse); left
 *    alone it goes on to the title (level 0) by itself. main loads the
 *    first level after the logos with FUN_8006AEA8(id) (0x80012348).
 *    "language" (not "ask") is a pre-hook there: level 2 becomes level 0
 *    with that language. The title level's menu is one actor (identity
 *    225) whose behaviour states are the pages: 0x0001 PRESS START,
 *    0x0102 NEW GAME / LOAD GAME / CREDITS, 0x0103 ONE / TWO PLAYERS,
 *    0x0104 START GAME / OPTIONS. A page's script returns the next page's
 *    id and FUN_80020B80(actor, id, work) switches to it (FUN_8002EC70);
 *    TWO PLAYERS runs Mode_Set2Player (0x800510E8) first, ONE PLAYER
 *    Mode_Set1Player (0x80051094); △ on START GAME goes back to 0x0103.
 *    "skip_title": a switch that ends on PRESS START (the level load makes
 *    several) is followed at once by the switch to 0x0102. "auto_players":
 *    a switch to 0x0103 sets the mode the menu would and goes to 0x0104
 *    instead; back from 0x0104 goes to 0x0102. In netplay the players are
 *    the occupied seats (two players with a second seat; the same on every
 *    peer). On one PC the page is skipped only when there is no choice: no
 *    controller for Player 2 (the seat the runtime marks connected, which
 *    is also what makes the game see a pad in port 2, 0x80010094 bit 1,
 *    the game's condition for TWO PLAYERS) means one player; with one, the
 *    game's own page asks (user's choice, 2026-11-09: a Player 2 keyboard
 *    is always "connected"). START GAME
 *    applies the mode again from the players page's cursor, script
 *    variable 0x7F (0x80093950 + i are the game's script variables): left
 *    at 0 it ran Mode_Set1Player (measured). So the skip also leaves the
 *    variables the page leaves when ONE / TWO PLAYERS is confirmed
 *    (measured for both: 0x1B = 60, 0x1C = 0x80, 0x1D = 1, 0x76 = 1, and
 *    0x7F = 1 for TWO).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "func_override.h"
#include "mod_plugins.h"
#include "psx_netplay.h"
#include "sio.h"

#define PACKAGE              "bbt.quick-start"
#define FEATURE              "quick-start"
#define PLUGIN               "bbt.quick-start"

#define ADDR_LEVEL_ID        0x80010000u   /* current / next Bouze level id */
#define ADDR_BOOT_FLAG       0x8008DDBCu   /* byte FUN_80011A44 clears first */
#define ADDR_FILE_SIZES      0x8008261Cu   /* file table +0x08, 16 bytes per id */
#define ADDR_LOGOS           0x80011A44u   /* FUN_80011A44: the logo sequence */
#define ADDR_DISPLAY_SETUP   0x800119D4u   /* FUN_800119D4 */
#define ADDR_LEVEL_CHANGE    0x8006B500u   /* FUN_8006B500: loading bar set-up */

#define ADDR_IRIS_LENGTH     0x8008DC74u   /* frames */
#define ADDR_IRIS_STEP       0x8008DC78u   /* 0x1000 / length */
#define IRIS_FRAMES          20u

#define CD_SPEED_MAX         4

#define LEVEL_TITLE          0u
#define LEVEL_LANGUAGE       2u
#define LEVEL_GRANNY         4u
#define LEVEL_CCINTRO        0x53u

#define ADDR_LEVEL_LOAD      0x8006AEA8u   /* FUN_8006AEA8(id): main's first level load */
#define ADDR_LANGUAGE        0x80010093u   /* byte: 0 English, 1 French, 3 Spanish */
#define ADDR_BEHAVIOUR       0x80020B80u   /* FUN_80020B80(actor, event, work): state switch */
#define ADDR_MODE_2P         0x800510E8u   /* Mode_Set2Player */
#define ADDR_MODE_1P         0x80051094u   /* Mode_Set1Player */
#define A_IDENTITY           0x34u
#define A_STATES             0x10Cu        /* list of behaviour states: +0 next, +4 u16 id */
#define A_STATE              0x178u        /* the current state's node */
#define MENU_IDENTITY        225u          /* the title level's menu actor */
#define ADDR_SCRIPT_VARS     0x80093950u   /* the game's script variables, one byte each */
#define VAR_PLAYERS_CURSOR   0x7Fu         /* 1 = TWO PLAYERS (START GAME re-applies the mode from it) */
#define PAGE_TITLE           0x0001u       /* PRESS START */
#define PAGE_MAIN            0x0102u       /* NEW GAME / LOAD GAME / CREDITS */
#define PAGE_PLAYERS         0x0103u       /* ONE PLAYER / TWO PLAYERS */
#define PAGE_START           0x0104u       /* START GAME / OPTIONS */

#define count(event) psx_mod_counter_add(PLUGIN "." event, 1u)

/* addiu sp,sp,-0x18 / sw ra,0x14(sp) / sw s0,0x10(sp) / lui at,0x8009 */
static const uint32_t kLogosGuard[4] = {
    0x27BDFFE8u, 0xAFBF0014u, 0xAFB00010u, 0x3C018009u,
};
/* lui v1,0x5FDE / ori v1,v1,0xAB97 / sll v0,a0,4 / subu v0,v0,a0 */
static const uint32_t kLevelChangeGuard[4] = {
    0x3C035FDEu, 0x3463AB97u, 0x00041100u, 0x00441023u,
};
/* addiu sp,sp,-0x20 / sw s0,0x18(sp) / move s0,a0 / sw ra,0x1C(sp) */
static const uint32_t kLevelLoadGuard[4] = {
    0x27BDFFE0u, 0xAFB00018u, 0x00808021u, 0xAFBF001Cu,
};
/* addiu sp,sp,-0x30 / sw s4,0x20(sp) / move s4,a0 / sw s0,0x10(sp) */
static const uint32_t kBehaviourGuard[4] = {
    0x27BDFFD0u, 0xAFB40020u, 0x0080A021u, 0xAFB00010u,
};

/* Options, read once at activation (part of the committed plan). */
static int g_skip_logos = 1;
static int g_skip_intro = 1;
static int g_short_loading = 1;
static int g_cd_speed = 1;
static int g_language = 0;           /* -1: show the language screen */
static int g_skip_title = 1;
static int g_auto_players = 1;

static int read_bool_option(const char* option, int fallback)
{
    char v[16];
    if (!psx_mod_option_value(PACKAGE, FEATURE, option, v, sizeof v))
        return fallback;
    return strcmp(v, "true") == 0 || strcmp(v, "1") == 0;
}

static void quick_start_activate(void)
{
    g_skip_logos = read_bool_option("skip_logos", 1);
    g_skip_intro = read_bool_option("skip_intro", 1);
    g_short_loading = read_bool_option("short_loading", 1);
    char v[16];
    g_cd_speed = 1;
    if (psx_mod_option_value(PACKAGE, FEATURE, "cd_speed", v, sizeof v))
        g_cd_speed = atoi(v);
    if (g_cd_speed < 1 || g_cd_speed > CD_SPEED_MAX)
        g_cd_speed = 1;
    if (g_cd_speed > 1 && psx_mod_set_disc_speed((uint32_t)g_cd_speed, 0u))
        psx_mod_counter_add(PLUGIN ".cd_speed", (uint32_t)g_cd_speed);
    g_language = 0;
    if (psx_mod_option_value(PACKAGE, FEATURE, "language", v, sizeof v))
        g_language = strcmp(v, "ask") == 0 ? -1 : strcmp(v, "french") == 0 ? 1 : strcmp(v, "spanish") == 0 ? 3 : 0;
    g_skip_title = read_bool_option("skip_title", 1);
    g_auto_players = read_bool_option("auto_players", 1);
}

/* Replaces FUN_80011A44 (logos) when skip_logos is on. */
static int logos_override(CPUState* cpu)
{
    if (!g_skip_logos)
        return 0;                             /* the original shows the logos */
    psx_mod_write_byte(ADDR_BOOT_FLAG, 0);
    (void)psx_mod_call_guest(cpu, ADDR_DISPLAY_SETUP, cpu->gpr[31], 0, 0, 0, 0);
    psx_mod_write_word(ADDR_LEVEL_ID, LEVEL_LANGUAGE);
    count("logos_skipped");
    return 1;                                 /* resume in main after the call */
}

/* Wrap of FUN_8006B500: a level change to the intro goes to Granwich; the
 * loading screen's iris closes in IRIS_FRAMES. */
static int level_change_wrap(CPUState* cpu)
{
    if (!g_skip_intro && !g_short_loading)
        return 0;                             /* the original, unchanged */
    if (g_skip_intro && psx_mod_read_word(ADDR_LEVEL_ID) == LEVEL_CCINTRO) {
        psx_mod_write_word(ADDR_LEVEL_ID, LEVEL_GRANNY);
        cpu->gpr[4] = psx_mod_read_word(ADDR_FILE_SIZES + LEVEL_GRANNY * 16u);
        count("intro_skipped");
    }
    func_override_call_original(cpu);
    if (g_short_loading) {
        psx_mod_write_word(ADDR_IRIS_LENGTH, IRIS_FRAMES);
        psx_mod_write_word(ADDR_IRIS_STEP, 0x1000u / IRIS_FRAMES);
        count("loading_shortened");
    }
    return 1;
}

/* Pre-hook of FUN_8006AEA8(id): the language screen becomes the title,
 * in the chosen language. */
static int level_load_wrap(CPUState* cpu)
{
    if (g_language < 0 || cpu->gpr[4] != LEVEL_LANGUAGE)
        return 0;
    psx_mod_write_byte(ADDR_LANGUAGE, (uint8_t)g_language);
    psx_mod_write_word(ADDR_LEVEL_ID, LEVEL_TITLE);
    cpu->gpr[4] = LEVEL_TITLE;
    func_override_call_original(cpu);
    count("language_skipped");
    return 1;
}

static int in_ram(uint32_t a)
{
    return a >= 0x80000000u && a < 0x80200000u;
}

static uint16_t page_of(uint32_t actor)
{
    const uint32_t node = psx_mod_read_word(actor + A_STATE);
    return in_ram(node) ? psx_mod_read_half(node + 4u) : 0u;
}

/* The title level's menu actor: level 0, identity 225, with the START GAME
 * page among its states. */
static int title_menu(uint32_t actor)
{
    if (!in_ram(actor) || psx_mod_read_word(ADDR_LEVEL_ID) != LEVEL_TITLE ||
        psx_mod_read_half(actor + A_IDENTITY) != MENU_IDENTITY)
        return 0;
    uint32_t node = psx_mod_read_word(actor + A_STATES);
    for (int i = 0; i < 64 && in_ram(node); ++i, node = psx_mod_read_word(node))
        if (psx_mod_read_half(node + 4u) == PAGE_START)
            return 1;
    return 0;
}

static int second_player(void)
{
    if (psx_netplay_active()) {
        uint32_t seats = psx_netplay_peer_seats();
        const int me = psx_netplay_local_slot();
        if (me >= 0 && me < 32)
            seats |= 1u << me;
        return (seats & ~1u) != 0;           /* a seat other than the host's */
    }
    return sio_get_pad_connected(1);
}

/* Wrap of FUN_80020B80(actor, event, work) for the title menu's pages. */
static int behaviour_wrap(CPUState* cpu)
{
    if ((!g_skip_title && !g_auto_players) || !title_menu(cpu->gpr[4]))
        return 0;
    const uint32_t actor = cpu->gpr[4], event = cpu->gpr[5];
    const uint32_t ra = cpu->gpr[31], work = cpu->gpr[6];
    if (event != PAGE_PLAYERS || !g_auto_players) {
        if (!g_skip_title)
            return 0;
        func_override_call_original(cpu);
        if (page_of(actor) == PAGE_TITLE) {
            (void)psx_mod_call_guest(cpu, ADDR_BEHAVIOUR, ra, actor, PAGE_MAIN, work, 0);
            count("title_skipped");
        }
        return 1;
    }
    if (!psx_netplay_active() && second_player()) {
        /* one PC with a second controller: one or two players is the
         * players' choice, so the game asks (its own page, both ways) */
        if (page_of(actor) != PAGE_START)
            count("players_asked");
        return 0;
    }
    if (page_of(actor) == PAGE_START) {
        cpu->gpr[5] = PAGE_MAIN;             /* back from START GAME */
        count("players_back");
    } else {
        const int two = second_player();
        (void)psx_mod_call_guest(cpu, two ? ADDR_MODE_2P : ADDR_MODE_1P, ra, 0, 0, 0, 0);
        static const uint8_t kConfirmed[][2] = {{0x1Bu, 60u}, {0x1Cu, 0x80u}, {0x1Du, 1u}, {0x76u, 1u}};
        for (unsigned i = 0; i < sizeof kConfirmed / sizeof kConfirmed[0]; ++i)
            psx_mod_write_byte(ADDR_SCRIPT_VARS + kConfirmed[i][0], kConfirmed[i][1]);
        psx_mod_write_byte(ADDR_SCRIPT_VARS + VAR_PLAYERS_CURSOR, two ? 1u : 0u);
        cpu->gpr[4] = actor;
        cpu->gpr[5] = PAGE_START;
        cpu->gpr[6] = work;
        cpu->gpr[31] = ra;
        if (two)
            count("players_two");
        else
            count("players_one");
    }
    func_override_call_original(cpu);
    return 1;
}

PSX_MOD_CONSTRUCTOR(register_bbt_quick_start)
{
    (void)psx_mod_register_function_override(PLUGIN ":language", ADDR_LEVEL_LOAD, level_load_wrap,
                                             kLevelLoadGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":menus", ADDR_BEHAVIOUR, behaviour_wrap,
                                             kBehaviourGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_activation_plugin(PLUGIN, quick_start_activate);
    (void)psx_mod_register_function_override(PLUGIN ":logos", ADDR_LOGOS, logos_override,
                                             kLogosGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":level-change", ADDR_LEVEL_CHANGE,
                                             level_change_wrap, kLevelChangeGuard, 4,
                                             FO_CREDIT_SELF);
}
