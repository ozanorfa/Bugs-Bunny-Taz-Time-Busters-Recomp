/*
 * Third character mod — Bugs Bunny & Taz: Time Busters (SLUS-01144).
 *
 * Phase 3 of the project: in 2-player mode, Taz is replaced by another
 * in-game character. This file is a trusted static plugin (see
 * psxrecomp/psxmod.md "When you need C or C++"); the package manifest in
 * mods/preloaded/packages/bbt.third-character/ only selects it by id and
 * ships no game data. Anything taken from the game is read from the
 * player's own disc at runtime.
 *
 * Milestone M1 ("swap-test" feature): proves the swap path end to end with
 * data that is already loaded. After every successful level load, each Taz
 * actor gets Bugs' resource list (model + animations), so Taz is drawn with
 * Bugs' model while keeping Taz's behaviour script and controller.
 *
 * Addresses come from the Phase 2 mapping (NOTES.md):
 *   Level_LoadParse  0x80068174  level stream parser, wrapped here
 *   actor lists      0x8008DD44 (definition actors), 0x8008DDCC (placed)
 *   actor +0x00 next, +0x08 type word, +0x98 resource node list
 *   (+0x170 / +0x174 hold the same list head)
 *   2-player flag    0x800100A4 bit 0x10
 * The model instance (+0x9C) is built only when the actor becomes active,
 * after loading, so changing the list right after the load takes effect.
 *
 * Plain C; no host state (everything lives in guest RAM), so savestates and
 * future netplay see a pure function of guest state.
 */
#include <stdint.h>

#include "cpu_state.h"
#include "func_override.h"
#include "mod_plugins.h"

#define ADDR_LEVEL_LOAD_PARSE 0x80068174u
#define ADDR_ACTOR_LIST_DEFS  0x8008DD44u
#define ADDR_ACTOR_LIST_PLACED 0x8008DDCCu
#define ADDR_MODE_FLAGS       0x800100A4u
#define MODE_FLAG_TWO_PLAYER  0x10u

#define ACTOR_OFF_NEXT   0x000u
#define ACTOR_OFF_TYPE   0x008u
#define ACTOR_OFF_RES    0x098u
#define ACTOR_OFF_RES_B  0x170u
#define ACTOR_OFF_RES_C  0x174u

#define TYPE_BUGS 0x05000181u
#define TYPE_TAZ  0x05000190u

#define MAX_ACTORS_PER_LIST 512

/* First four instructions of Level_LoadParse in SLUS_011.44
 * (addiu sp,sp,-0x170 / sw s7,0x164(sp) / move s7,a1 / sw s0,0x148(sp)).
 * On any other code the override declines and the game runs unchanged. */
static const uint32_t kLevelLoadParseGuard[4] = {
    0x27BDFE90u, 0xAFB70164u, 0x00A0B821u, 0xAFB00148u,
};

static int in_main_ram(uint32_t p)
{
    return p >= 0x80000000u && p < 0x80200000u;
}

static uint32_t find_actor_of_type(uint32_t list_head, uint32_t type)
{
    uint32_t p = psx_mod_read_word(list_head);
    for (int n = 0; in_main_ram(p) && n < MAX_ACTORS_PER_LIST; ++n) {
        if (psx_mod_read_word(p + ACTOR_OFF_TYPE) == type)
            return p;
        p = psx_mod_read_word(p + ACTOR_OFF_NEXT);
    }
    return 0;
}

/* Give every Taz actor in one list the resource list of the Bugs actor in
 * the same list. Returns the number of actors changed. */
static int swap_taz_resources_in_list(uint32_t list_head)
{
    const uint32_t bugs = find_actor_of_type(list_head, TYPE_BUGS);
    if (!bugs)
        return 0;
    const uint32_t res   = psx_mod_read_word(bugs + ACTOR_OFF_RES);
    const uint32_t res_b = psx_mod_read_word(bugs + ACTOR_OFF_RES_B);
    const uint32_t res_c = psx_mod_read_word(bugs + ACTOR_OFF_RES_C);
    if (!in_main_ram(res))
        return 0;

    int changed = 0;
    uint32_t p = psx_mod_read_word(list_head);
    for (int n = 0; in_main_ram(p) && n < MAX_ACTORS_PER_LIST; ++n) {
        if (psx_mod_read_word(p + ACTOR_OFF_TYPE) == TYPE_TAZ) {
            psx_mod_write_word(p + ACTOR_OFF_RES, res);
            psx_mod_write_word(p + ACTOR_OFF_RES_B, res_b);
            psx_mod_write_word(p + ACTOR_OFF_RES_C, res_c);
            ++changed;
        }
        p = psx_mod_read_word(p + ACTOR_OFF_NEXT);
    }
    return changed;
}

/* Wrap of Level_LoadParse(id, buffer): run the game's loader unchanged,
 * then adjust the freshly created actors. Level_LoadParse returns 0 on
 * success and 1 when the caller must retry the read. */
static int level_load_parse_wrap(CPUState* cpu)
{
    func_override_call_original(cpu);
    if (cpu->gpr[2] != 0)
        return 1;
    if ((psx_mod_read_byte(ADDR_MODE_FLAGS) & MODE_FLAG_TWO_PLAYER) == 0)
        return 1;
    (void)swap_taz_resources_in_list(ADDR_ACTOR_LIST_DEFS);
    (void)swap_taz_resources_in_list(ADDR_ACTOR_LIST_PLACED);
    return 1;
}

PSX_MOD_CONSTRUCTOR(register_bbt_third_character)
{
    (void)psx_mod_register_function_override(
        "bbt.third-character.swap-test:level-load", ADDR_LEVEL_LOAD_PARSE,
        level_load_parse_wrap, kLevelLoadParseGuard, 4, FO_CREDIT_SELF);
}
