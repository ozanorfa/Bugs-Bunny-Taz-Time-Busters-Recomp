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
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "func_override.h"
#include "mod_plugins.h"

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

#define LEVEL_LANGUAGE       2u
#define LEVEL_GRANNY         4u
#define LEVEL_CCINTRO        0x53u

#define count(event) psx_mod_counter_add(PLUGIN "." event, 1u)

/* addiu sp,sp,-0x18 / sw ra,0x14(sp) / sw s0,0x10(sp) / lui at,0x8009 */
static const uint32_t kLogosGuard[4] = {
    0x27BDFFE8u, 0xAFBF0014u, 0xAFB00010u, 0x3C018009u,
};
/* lui v1,0x5FDE / ori v1,v1,0xAB97 / sll v0,a0,4 / subu v0,v0,a0 */
static const uint32_t kLevelChangeGuard[4] = {
    0x3C035FDEu, 0x3463AB97u, 0x00041100u, 0x00441023u,
};

/* Options, read once at activation (part of the committed plan). */
static int g_skip_logos = 1;
static int g_skip_intro = 1;
static int g_short_loading = 1;
static int g_cd_speed = 1;

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
    char v[8];
    g_cd_speed = 1;
    if (psx_mod_option_value(PACKAGE, FEATURE, "cd_speed", v, sizeof v))
        g_cd_speed = atoi(v);
    if (g_cd_speed < 1 || g_cd_speed > CD_SPEED_MAX)
        g_cd_speed = 1;
    if (g_cd_speed > 1 && psx_mod_set_disc_speed((uint32_t)g_cd_speed, 0u))
        psx_mod_counter_add(PLUGIN ".cd_speed", (uint32_t)g_cd_speed);
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

PSX_MOD_CONSTRUCTOR(register_bbt_quick_start)
{
    (void)psx_mod_register_activation_plugin(PLUGIN, quick_start_activate);
    (void)psx_mod_register_function_override(PLUGIN ":logos", ADDR_LOGOS, logos_override,
                                             kLogosGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":level-change", ADDR_LEVEL_CHANGE,
                                             level_change_wrap, kLevelChangeGuard, 4,
                                             FO_CREDIT_SELF);
}
