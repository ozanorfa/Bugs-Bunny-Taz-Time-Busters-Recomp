/*
 * Third player — Bugs Bunny & Taz: Time Busters (SLUS-01144). Phase 4.
 *
 * Feature "p3" of package bbt.third-character (plugin bbt.third-character.p3).
 *
 * Milestone P4-M0, input plumbing (NOTES.md "Phase 4 — third player"):
 * the game polls two controller ports. A third player is a third seat of
 * the runtime: with the build's seat count >= 5 and game.toml players >= 3
 * the runtime arms an SCPH-1070 multitap (offline on [controller]
 * multitap_port; netplay forces it for three or more seats). With the tap
 * on port 2 the game still sees P1 on port 1 and P2 on tap slot A (its
 * libpad never asks for multitap mode, so slot A passes through); seat 2
 * (tap slot B) is not polled by the game at all.
 *
 * This plugin reads seat 2's buttons from the runtime's SIO seat state,
 * sio_get_pad_buttons_slot(2) -- the value netplay stages for that seat
 * every tick, the same source the Mega Man X4 co-op mod uses for its extra
 * player -- and never reads a host controller itself. Once per game pad
 * poll (Pad_PollFrame, 0x80065FC8) it turns them into a third input record
 * in mod guest memory (so savestates and rollback carry it), laid out like
 * the game's own input slots (0x80010008 + slot * 0x28):
 *   +0x00 u16 buttons with new presses only for the edge-masked bits
 *             (mask at 0x8008DD40), +0x02 held, +0x04 copy of +0x00,
 *   +0x06 raw held, +0x08 previous +0x02, +0x0A previous +0x06,
 *   +0x0C s8 stick X (Left -128 / Right +127), +0x0D s8 stick Y (Up +127 /
 *             Down -128), digital pad.
 * Button bits are the game's (active high): 0x0001 L2 ... 0x0080 Square,
 * 0x0100 Select ... 0x8000 Left, i.e. the inverted SIO word byte-swapped.
 *
 * Tests: the debug server injects ports 1 and 2 only. The command
 * {"cmd":"mod_cmd","name":"bbt.p3","args":"<hex buttons>|off|show"} sets a
 * test override for seat 2 (SIO active-low word) that this plugin reads
 * instead of the SIO seat, and reports the record.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cpu_state.h"
#include "func_override.h"
#include "mod_plugins.h"
#include "sio.h"

#define PLUGIN_P3            "bbt.third-character.p3"
#define ADDR_PAD_POLL_FRAME  0x80065FC8u
#define ADDR_EDGE_MASK       0x8008DD40u   /* bits reported as new presses only */
#define P3_SEAT              2
#define P3_RECORD_SIZE       0x40u

/* lui v0,0x8001 / lbu v0,0x8A(v0) / addiu sp,sp,-0x28 / sw ra,0x20(sp):
 * the first instructions of Pad_PollFrame in SLUS_011.44. */
static const uint32_t kPadPollFrameGuard[4] = {
    0x3C028001u, 0x9042008Au, 0x27BDFFD8u, 0xAFBF0020u,
};

static uint32_t g_record;              /* guest address of the record, 0 if none */
static volatile int32_t g_test_buttons = -1;   /* -1 = read the SIO seat */

static uint16_t game_bits(uint16_t sio_active_low)
{
    const uint16_t pressed = (uint16_t)~sio_active_low;
    return (uint16_t)((pressed << 8) | (pressed >> 8));
}

static void p3_activate(void)
{
    if (!g_record)
        g_record = psx_mod_alloc_guest_memory(P3_RECORD_SIZE, 16);
}

/* After the game polled its two ports, refresh the third record. */
static int pad_poll_frame_wrap(CPUState* cpu)
{
    func_override_call_original(cpu);
    if (!g_record)
        return 1;
    const int32_t test = g_test_buttons;
    const uint16_t sio = test >= 0 ? (uint16_t)test : sio_get_pad_buttons_slot(P3_SEAT);
    const uint16_t held = game_bits(sio);
    const uint16_t prev = psx_mod_read_half(g_record + 0x02u);
    const uint16_t mask = psx_mod_read_half(ADDR_EDGE_MASK);
    const uint16_t edge = (uint16_t)(held & ~(prev & mask));
    int8_t sx = 0, sy = 0;
    if (held & 0x8000u) sx = -128;
    if (held & 0x2000u) sx = 127;
    if (held & 0x1000u) sy = 127;
    if (held & 0x4000u) sy = -128;
    psx_mod_write_half(g_record + 0x08u, prev);
    psx_mod_write_half(g_record + 0x0Au, psx_mod_read_half(g_record + 0x06u));
    psx_mod_write_half(g_record + 0x00u, edge);
    psx_mod_write_half(g_record + 0x02u, held);
    psx_mod_write_half(g_record + 0x04u, edge);
    psx_mod_write_half(g_record + 0x06u, held);
    psx_mod_write_byte(g_record + 0x0Cu, (uint8_t)sx);
    psx_mod_write_byte(g_record + 0x0Du, (uint8_t)sy);
    psx_mod_counter_add(PLUGIN_P3 ".polls", 1);
    if (edge & ~prev)
        psx_mod_counter_add(PLUGIN_P3 ".presses", 1);
    return 1;
}

/* Debug command (server thread): only sets the host-side test value; the
 * emulation-thread hook above is what reads it. */
static int p3_command(const char* args, char* reply, int cap)
{
    if (args && strncmp(args, "off", 3) == 0) {
        g_test_buttons = -1;
    } else if (args && args[0] && strncmp(args, "show", 4) != 0) {
        unsigned v = 0;
        if (sscanf(args, "%x", &v) != 1 || v > 0xFFFFu) {
            snprintf(reply, (size_t)cap, "{\"ok\":false,\"err\":\"args: <hex>|off|show\"}");
            return 0;
        }
        g_test_buttons = (int32_t)v;
    }
    snprintf(reply, (size_t)cap,
             "{\"ok\":true,\"record\":\"0x%08X\",\"seat\":%d,\"test\":%d,\"sio_seat\":\"0x%04X\"}",
             g_record, P3_SEAT, (int)g_test_buttons, sio_get_pad_buttons_slot(P3_SEAT));
    return 1;
}

PSX_MOD_CONSTRUCTOR(register_bbt_third_player)
{
    (void)psx_mod_register_activation_plugin(PLUGIN_P3, p3_activate);
    (void)psx_mod_register_function_override(
        PLUGIN_P3 ":pad-poll", ADDR_PAD_POLL_FRAME,
        pad_poll_frame_wrap, kPadPollFrameGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_command("bbt.p3", p3_command);
}
