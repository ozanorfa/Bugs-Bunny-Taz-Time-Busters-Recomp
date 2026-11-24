/*
 * Own view (Phase 6 step N4, in development) — Bugs Bunny & Taz: Time
 * Busters (SLUS-01144).
 *
 * Package bbt.own-view, feature "two-views" (plugin bbt.own-view.two-views):
 * an experiment that turns on the engine's own extra views in a 2-player
 * level: two views (Bugs, Taz), three while the third player's actor
 * (bbt.third-character "p3") is in the level (NOTES.md "N4 research: cameras
 * and views", "Third player's own view").
 *
 * The engine runs every frame once per view (FUN_80011AF0: view count
 * DAT_8008DE00, view 0 follows player 1, view 1 player 2 when the count is
 * above 1; per view its camera record, culling pass, update and draws), but
 * Level_LoadParse always sets the count to 1. Each view links its frame into
 * GPU chains whose heads are words at 0x8009390C / 0x80093B60 / 0x80093C88
 * (and 0x80097204 / 0x80097234 / 0x80097264) + frame * 0x10 + view * 4, for
 * the three frame buffers; the VSync present (FUN_80066388) sends the head of
 * every view of the frame it shows. View 1's heads are 0 in a 1-view game, and
 * a 0 head makes the GPU walk from address 0 into a list that loops (seen at
 * runtime: the game hangs in libgpu's queue, FUN_800716E0). So before the
 * count goes to 2, every view-1 head is set to an empty chain (0x00FFFFFF);
 * the count is raised at the start of a frame (pre-hook on FUN_80011AF0,
 * called from main's game loop 0x800125B0 and pause loop 0x8001273C).
 *
 * The unused multi-view path has a bug in the update walk FUN_800150E0: with
 * a count other than 1, an actor whose identity (+0x34) is 3 (Taz) branches
 * from 0x8001523C to the loop tail 0x80015330 without moving to the next
 * list entry, so the walk never ends. The wrap below lets that walk see a
 * count of 1 (the only use of the count in it), so Taz is updated like every
 * other actor, once per frame (+0x04 bit 3). The VSync present
 * (FUN_80066388, from the VSync callback) can interrupt the walk; it must see
 * the real count, or it sends view 0 only (seen: frames without the bottom
 * half), so it is wrapped too and runs with the real count.
 *
 * Steering: the behaviour script turns the stick into movement with the
 * camera of the current view (0x800964C8.. + DAT_8008DEA0 * 0x2C0), and both
 * players are updated in view 0's walk (they head every view's list; an
 * actor is updated once per frame), so player 2 steered by player 1's camera
 * (measured: "up" moved Taz along camera 0's forward, not camera 1's).
 * The actor a view follows (0x80093C58[v]) is therefore updated with the view
 * index set to that view; the engine's view-aware code (script ops,
 * camera-relative input) then works in that player's view. Actor_Update
 * (0x800161B8) is hooked through the shared override in
 * src/mods/actor_update_hub.c (the third actor needs it too, and the
 * framework refuses two overrides on one address).
 *
 * Each view also needs its own update / draw list buffer: the frame loop
 * builds view v's list at 0x800970FC[v] (FUN_80013030; FUN_800150E0 walks
 * it). Level_LoadParse allocates them in a loop whose bound is the constant
 * 1 (0x800970FC[i] = next; next += DAT_8008DEA8 * 4, i < 1), so view 1's
 * pointer is 0 and its list would be written over RAM address 0 (seen: a jump
 * to 0x159C0000 from the model renderer a few frames later). Views 1 and 2
 * get buffers in mod memory (LIST_BYTES each) at activation.
 *
 * A third view (the third actor): view 2's camera record (0x80096A44) starts
 * as a copy of record 1 with its own view-unit pointer, view 2's actor is set
 * at every frame start, and the EXE's 3-view screen-area entry, which points
 * back at the 2-view table, is replaced by three 72-line bands
 * (env_for_view_wrap). The views' primitives go to an arena of their own: a
 * frame's primitives and the frame the GPU may still read share the game's
 * ring (227,952 bytes), which two or three views (70-110 KB a frame) fill, so
 * the allocators waited for room only the next present frees (three views:
 * forever; two: frames of 5 vblanks). While a frame's views draw, the game's
 * primitive pointer, ring bounds and in-flight marks point into one of three
 * arenas in the GPU-DMA aperture (one per frame buffer; 24-bit list tags
 * reach it), and frame_wrap puts them back (view_step_prehook).
 *
 * In a match the shared frame's models are not drawn at all (nobody sees
 * it): model_emit_wrap. With that the frame fits two vblanks without the
 * cycle scale, which a match therefore turns off (frame_wrap).
 *
 * With more than one view the frame loop also links a 6-word packet into
 * OT bucket 1 of 0x80093CC4 for the last view (its pointer kept at
 * 0x80097294[frame]), which nothing ever fills (0x80097294 has no reader):
 * the GPU ran whatever the primitive buffer held there, and the picture's
 * textures turned to noise. After each frame its 6 words are set to GP0
 * NOPs.
 *
 * Per-view screen areas: the VSync present (FUN_80066388) writes each view's
 * draw environment (E3/E4/E5/E1/E2/E6 from the table for the view count and
 * the display buffer, FUN_80059354) into a packet whose address the frame
 * loop keeps at 0x80096FC4[frame * 4 + view] -- but it stores 0 there, so the
 * words went to RAM 0x4 and both views drew into the full-screen area of the
 * frame's own 9-word env packet (GP0 dump: one E3/E4/E5 per frame). Here
 * each view gets a real 7-word packet: allocated from the game's primitive
 * buffer (0x8009906C, main RAM, so the GPU DMA reaches it), linked at the
 * head of the last bucket of the OT 0x80095CC4 (the view's first chain, the
 * way FUN_80059314 links), and its address put in that slot; the present
 * then fills it for the buffer it shows. Pre-hook on FUN_8006E260, which the
 * frame loop calls once per view (0x80011CC0) after zeroing the slot.
 *
 * One single-player camera per view: Camera_Update (0x8005029C, once per view
 * from the actor pass FUN_80013030, a0 = the actor the view follows) runs
 * the camera director FUN_8005154C. Its 2-player framing is bit 0x100 of the
 * camera record's flags (+0xE0; Mode_Set2Player sets it on record 0 only):
 * the camera aims at the midpoint of player 1 (DAT_8008DD04) and player 2
 * (DAT_8008DD08); when they are too far apart it switches to following
 * player 1 alone (bit 0x80) and, while player 2 is off that screen, sets bit
 * 0x80000000 of player 2's +0x20 (0x80052274), the "do not move this frame"
 * bit: player 2 freezes in every direction (measured: Taz stuck ~3050 units
 * from Bugs). The director also uses player 1, not its own actor, for the
 * line-of-sight test and its "last player 1" field. So each view's call runs
 * with the single-player bits (Mode_Set1Player's: 0x80 set, 0x100 clear),
 * with the view's own actor shown as player 1 and the other as player 2 (the
 * single-player layout: player 2 is the partner), and with its own copy of
 * the globals the camera code keeps between frames (kCameraGlobals, found
 * with a write trace of every camera-code store). View 0 keeps the live
 * globals; the copies of views 1 and 2 live in mod memory (in rollback
 * snapshots) and are swapped in only for their call, so everything outside
 * the camera still sees view 0's values, as in a 1-view game. Camera_Update also clears the
 * partner's "followed" bit (+0x24 bit 0x10000), which every view's own call
 * then set again, re-sending the actor's 0x10000 script event each frame; the
 * partner's bit is put back after the call.
 *
 * Presenting from the main loop. The game presents from its VSync callback
 * (func_800662A0 -> FUN_80066388, at most every 2 vblanks; also from the
 * DrawSync callback FUN_8006623C): the present flips the display to the
 * buffer drawn at the previous present (PutDispEnv 0x8009393C, y = DCD4 *
 * 256) and queues the next finished frame's chains into the other buffer
 * (DrawOTag, libgpu's queue). In netplay each peer shows its own player's
 * view full screen with psx_mod_render_local_view, which must run where "the
 * next flip will show rect and the guest has finished drawing it" and never
 * inside an exception (the callback is one). So while two or three views run, the
 * callback's presents are skipped and frame_wrap presents at each frame
 * start instead, the same way on every peer (guest state decides, so
 * netplay and rollback stay deterministic): wait until the callback's
 * vblank count since the last present reaches the game's 2 (4) as the
 * callback would, DrawSync(0) so the previous frame's draw is complete, draw
 * this peer's own view into that buffer (netplay only), then run the present,
 * which flips to it and queues the frame just built. A callback present is
 * still allowed when the main loop has not presented for PRESENT_FALLBACK
 * vblanks (another loop waiting on presents).
 *
 * The own view (netplay): at each frame start (frame_wrap) the guest RAM,
 * scratchpad and the views' camera banks are copied to a host ring indexed by
 * the frame buffer being built (presentation only: these copies only ever
 * feed the sandboxed local-view draw, whose guest effects are discarded). The
 * flip shows the frame last presented; the copy taken at the start of the
 * frame after it holds that frame's finished state. At the commit the
 * sandbox puts that copy back, sets one view: the one following this peer's
 * own character, or a teammate's while that character is out (seat_view;
 * the game swaps players 1 and 2 when Bugs goes down first). For a view v
 * >= 1 its camera record and globals become view 0's, its actor player 1
 * and player 1 the partner. Then it runs the frame function as a 1-view
 * game with the update walk and Camera_Update skipped (only the draws run:
 * re-running the logic in a pass hit waits on interrupts that never come,
 * e.g. the dialog actor's CD command), and sends its chains to the GPU
 * itself (DMA channel 2, linked list; libgpu's queue needs interrupts, which
 * a pass never takes) after filling the frame's draw environment for the
 * target buffer (FUN_800593B8). VSync, DrawSync and libcd's waits return at
 * once inside the draw, as a safeguard.
 *
 * Sound with three views. The game has one sound mix for every view (the
 * SPU is shared machine state, so in netplay every peer hears the same mix).
 * FUN_80063D24(actor, sound, view) gives a sound its stereo volume: players
 * 1 and 2 (and actor 0) at the master volume DAT_8008DBD4, any other actor by
 * its distance to camera 0's listener (record + 0x2C) or, with more than one
 * view, camera 1's, whichever is nearer (silent beyond 0x1000, or the
 * sound's own range), panned by the camera's direction. It never looks at
 * camera 2, so the third player's own sounds went silent once it was ~0x1000
 * from Bugs' and Taz's cameras (measured: volume 0 / 0 at 4200 units, with
 * camera 2 1000 units away). Most sounds start inside the update walk, which
 * sees a count of 1 (update_walk_wrap), so they were heard from camera 0
 * only, even with two views. sound_volume_wrap gives the function the real
 * count, and with three views hears the third player as players 1 and 2
 * are, and any other actor from camera 2 too: the original runs once as if
 * camera 2 were the only camera (its
 * listener and direction in record 0, a count of 1, view 0), then once as
 * it is; camera 2's result wins only when its louder channel is louder (the
 * pan leaves the nearer side at the distance volume, so the louder channel
 * is the distance volume). The stock call runs last, so the state the
 * function leaves (GTE registers) is the stock one.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "func_override.h"
#include "mod_plugins.h"
#include "psx_netplay.h"
#include "actor_update_hub.h"
#include "mod_blocks.h"

#define PLUGIN              "bbt.own-view.two-views"
#define ADDR_FRAME          0x80011AF0u   /* FUN_80011AF0: one game frame, all views */
#define ADDR_VIEW_COUNT     0x8008DE00u   /* byte */
#define ADDR_MODE_FLAGS     0x800100A4u   /* bit 0x10 = 2-player mode */
#define MODE_TWO_PLAYER     0x10u
#define ADDR_P1_ACTOR       0x8008DD04u
#define ADDR_P2_ACTOR       0x8008DD08u
#define END_OF_CHAIN        0x00FFFFFFu
#define ACTOR_OFF_TYPE      0x08u
#define TYPE_BUGS           0x05000181u
#define TYPE_TAZ            0x05000190u
#define ADDR_VIEW_LISTS     0x800970FCu   /* per-view list buffer pointers */
#define ADDR_LIST_ENTRIES   0x8008DEA8u   /* s16: entries per list */
#define LIST_BYTES          0x1000u
#define ADDR_FRAME_INDEX    0x8008DD28u   /* u16: frame buffer being built, 0..2 */
#define ADDR_EXTRA_PACKETS  0x80097294u   /* [frame]: the unfilled multi-view packet */
#define ADDR_VIEW_STEP      0x8006E260u   /* FUN_8006E260: once per view */
#define ADDR_VIEW_INDEX     0x8008DEA0u   /* byte: view being built */
#define ADDR_PRIM_NEXT      0x8009906Cu   /* next free primitive word */
#define ADDR_OT_FIRST       0x80095CC4u   /* buckets of the view's first chain */
#define OT_FIRST_BUCKETS    0x100u
#define ADDR_ENV_SLOTS      0x80096FC4u   /* [frame * 4 + view]: env packet */
#define ENV_WORDS           6u

static const uint32_t kChainHeads[] = {
    0x8009390Cu, 0x80093B60u, 0x80093C88u, 0x80097204u, 0x80097234u, 0x80097264u,
};

#define ADDR_UPDATE_WALK    0x800150E0u   /* FUN_800150E0 */
#define ADDR_PRESENT        0x80066388u   /* FUN_80066388: VSync present */
#define ADDR_VSYNC          0x80072144u   /* libetc VSync(mode) */
#define ADDR_DRAWSYNC       0x8006FE40u   /* libgpu DrawSync(mode) */
#define ADDR_SET_FRAME_ENV  0x800593B8u   /* FUN_800593B8(env packet, display buffer) */
#define ADDR_VBLANKS        0x8008DCDCu   /* u16: vblanks since the last present */
#define ADDR_LEVEL          0x80010000u
#define ADDR_SLOW_PRESENTS  0x80093978u   /* byte: != 0 -> every 4 vblanks */
#define ADDR_PRESENTED      0x8008DE58u   /* u16: frame buffer last presented */
#define ADDR_DRAW_BUFFER    0x8008DCD4u   /* u16: display buffer the last present drew into */
#define ADDR_DISPENV        0x8009393Cu   /* DISPENV: disp x, y, w, h (s16) */
#define ADDR_FRAME_ENVS     0x80097654u   /* [frame]: 9-word full-screen env packet */
#define ADDR_EXTRA_CHAINS   0x8008DE08u   /* byte [frame]: second chain set used */
#define ADDR_PRIM_IN_FLIGHT 0x8008DDF0u   /* oldest / newest in-flight frame's primitive start */
#define PRESENT_FALLBACK    12u
#define RAM_BYTES           0x200000u
#define SCRATCH_ADDR        0x1F800000u
#define SCRATCH_BYTES       0x400u
#define GP1                 0x1F801814u
#define DMA2_MADR           0x1F8010A0u
#define DMA2_BCR            0x1F8010A4u
#define DMA2_CHCR           0x1F8010A8u
#define ADDR_FOLLOWED       0x80093C58u   /* [view]: actor the view follows, set at a frame start */

/* addiu sp,sp,-0x28 / li a0,1 / sw ra,0x24(sp) / sw s4,0x20(sp) */
static const uint32_t kPresentGuard[4] = {
    0x27BDFFD8u, 0x24040001u, 0xAFBF0024u, 0xAFB40020u,
};

/* the real view count while the update walk runs with it shown as 1, else 0;
 * set and cleared inside one walk call (no state between frames) */
static uint8_t g_walk_views;

/* addiu sp,sp,-0x30 / sw ra,0x28(sp) / sw s5,0x24(sp) / sw s4,0x20(sp) */
static const uint32_t kUpdateWalkGuard[4] = {
    0x27BDFFD0u, 0xAFBF0028u, 0xAFB50024u, 0xAFB40020u,
};

/* lh v0,0x74(gp) / addiu sp,sp,-0x28 / sw s2,0x18(sp) / move s2,zero */
static const uint32_t kViewStepGuard[4] = {
    0x87820074u, 0x27BDFFD8u, 0xAFB20018u, 0x00009021u,
};

/* addiu sp,sp,-0x48 / lui v0,0x8009 / lw v0,-0x22FC(v0) / lui v1,0x8009 */
static const uint32_t kFrameGuard[4] = {
    0x27BDFFB8u, 0x3C028009u, 0x8C42DD04u, 0x3C038009u,
};

#define ADDR_CAMERA_UPDATE  0x8005029Cu   /* Camera_Update(actor), once per view */
#define ADDR_CAM_RECORDS    0x800964C4u   /* camera record of view v at + v * 0x2C0 */
#define CAM_RECORD_BYTES    0x2C0u
#define CAM_OFF_FLAGS       0xE0u
#define CAM_FRAME_BOTH      0x100u        /* 2-player framing (Mode_Set2Player) */
#define CAM_FOLLOW_ONE      0x80u         /* follow the record's actor only */
#define ACTOR_OFF_FLAGS24   0x24u
#define ACTOR_FOLLOWED      0x10000u      /* set by Camera_Update on its actor */
#define ADDR_HERO_DOWN      0x800100A2u   /* 0x10 / 0x20 Bugs / Taz died, 1 / 2 out */
#define ADDR_BUGS_HERO      0x8008DDD0u   /* the identity-2 hero */
#define ACTOR_OFF_PADS      0x0Cu         /* ETAT1: which pad drives the actor */
#define ACTOR_JOYSTICK1     0x400u
#define ACTOR_JOYSTICK2     0x800u
#define ADDR_CAM_MODE       0x8008DD4Cu   /* bit 1: free look was on */
#define CAM_MODE_FREE_LOOK  0x2u

/* addiu sp,sp,-0x148 / sw s2,0x128(sp) / move s2,a0 / sw ra,0x140(sp) */
static const uint32_t kCameraUpdateGuard[4] = {
    0x27BDFEB8u, 0xAFB20128u, 0x00809021u, 0xAFBF0140u,
};

/* Globals the camera code (Camera_Update, the director) stores and reads
 * again in a later frame: free-look angles (gp+0x188/0x18A), counters
 * (gp+0x46/0x170/0x190/0x194), the director's last target and "last player
 * 1" (gp+0x48, gp+0x1AC), the followed actor's last position (0x800927C8).
 * gp = 0x8008D984. Plus bit 1 of ADDR_CAM_MODE. */
static const struct { uint32_t addr, bytes; } kCameraGlobals[] = {
    {0x8008D9CAu, 2}, {0x8008D9CCu, 4}, {0x8008DAF4u, 2}, {0x8008DB0Cu, 4},
    {0x8008DB14u, 2}, {0x8008DB18u, 2}, {0x8008DB30u, 4}, {0x800927C8u, 12},
};
#define CAMERA_BANK_BYTES   36u           /* the spans (32 bytes) + the mode bit word */

#define MAX_VIEWS           3u            /* players 1, 2 and the third actor */
#define ADDR_ENV_FOR_VIEW   0x80059354u   /* FUN_80059354(dst, buffer, view, count) */

/* Guest buffers for views 1.. (mod memory, allocated once): their update
 * lists and their copies of the camera globals. */
static uint32_t g_view_lists;          /* list of view v at + (v - 1) * LIST_BYTES */
static uint32_t g_view_cameras;        /* bank of view v at + (v - 1) * CAMERA_BANK_BYTES */

/* The views' primitives (see view_step_prehook): one arena per frame buffer in
 * the GPU-DMA aperture, which 24-bit linked-list tags reach. */
#define VIEW2_ARENA_BYTES   0x40000u
#define ADDR_PRIM_RING_LO   0x8008DE4Cu   /* primitive ring start (Level_LoadParse) */
#define ADDR_PRIM_RING_HI   0x8008DE50u   /* and end */
#define ADDR_VIEW_PRIM_START 0x800971F8u  /* [frame]: primitive pointer at the last view's draws */
static uint32_t g_view2_arena;
/* the main ring while view 2 draws into its arena: set and restored inside
 * one frame_wrap call (no state between frames) */
static struct { uint32_t prim, lo, hi, in0, in1, start, frame; int active; } g_ring;

static int arena_span(uint32_t a, uint32_t bytes);
static uint32_t g_emits_skipped;       /* model draws skipped since the last frame start (counter only) */
static uint32_t view_list(uint32_t v) { return g_view_lists + (v - 1u) * LIST_BYTES; }
static uint32_t view_bank(uint32_t v) { return g_view_cameras + (v - 1u) * CAMERA_BANK_BYTES; }

/* 1 while frame_wrap runs the present; the present clears it on entry, so a
 * callback present that interrupts it is still skipped */
static int g_main_present;

/* Frame-start copies for the own view (host, presentation only). */
typedef struct FrameCopy {
    uint8_t* ram;                      /* RAM_BYTES, allocated on first use */
    uint8_t scratch[SCRATCH_BYTES];
    uint8_t camera[(MAX_VIEWS - 1u) * CAMERA_BANK_BYTES]; /* the camera banks of views 1.. */
    uint32_t sp;                       /* the main loop's sp at the frame call */
    uint32_t generation;               /* 0 = empty */
} FrameCopy;
static FrameCopy g_copies[3];
static uint32_t g_generation = 1;      /* bumped when two views stop */

static int in_main_ram(uint32_t a)
{
    return a >= 0x80000000u && a < 0x80200000u;
}

/* Option "offline" (default "shared"): on one PC the game keeps its own
 * shared screen; "split" gives every player a band of it. In netplay every
 * PC shows its own player either way. */
static int g_offline_split;

static const BbtActorUpdateHook kViewHook;
static void two_views_activate(void)
{
    char v[16];
    g_offline_split = psx_mod_option_value("bbt.own-view", "two-views", "offline", v, sizeof v) &&
                      strcmp(v, "split") == 0;
    bbt_actor_update_hook(BBT_ACTOR_HOOK_VIEW, &kViewHook);
    /* fixed addresses whatever was played before (BUGS #56, mod_blocks.h) */
    g_view_lists = bbt_mod_block(BBT_BLOCK_VIEW_LISTS);
    g_view_cameras = bbt_mod_block(BBT_BLOCK_VIEW_CAMERAS);
    g_view2_arena = bbt_mod_block(BBT_BLOCK_VIEW2_ARENA);
}
BBT_BLOCK_SIZE_IS(view_lists, (MAX_VIEWS - 1u) * LIST_BYTES, BBT_BLOCK_VIEW_LISTS_BYTES);
BBT_BLOCK_SIZE_IS(view_cameras, (MAX_VIEWS - 1u) * CAMERA_BANK_BYTES, BBT_BLOCK_VIEW_CAMERAS_BYTES);
BBT_BLOCK_SIZE_IS(view2_arena, 3u * VIEW2_ARENA_BYTES, BBT_BLOCK_VIEW2_ARENA_BYTES);

/* Copy the live camera globals to a bank (guest address), or back. */
static void camera_globals_save(uint32_t bank)
{
    uint32_t off = 0;
    for (unsigned i = 0; i < sizeof kCameraGlobals / sizeof kCameraGlobals[0]; ++i)
        for (uint32_t k = 0; k < kCameraGlobals[i].bytes; ++k, ++off)
            psx_mod_write_byte(bank + off, psx_mod_read_byte(kCameraGlobals[i].addr + k));
    psx_mod_write_word(bank + off, psx_mod_read_word(ADDR_CAM_MODE) & CAM_MODE_FREE_LOOK);
}

static void camera_globals_load(uint32_t bank)
{
    uint32_t off = 0;
    for (unsigned i = 0; i < sizeof kCameraGlobals / sizeof kCameraGlobals[0]; ++i)
        for (uint32_t k = 0; k < kCameraGlobals[i].bytes; ++k, ++off)
            psx_mod_write_byte(kCameraGlobals[i].addr + k, psx_mod_read_byte(bank + off));
    const uint32_t mode = psx_mod_read_word(ADDR_CAM_MODE) & ~CAM_MODE_FREE_LOOK;
    psx_mod_write_word(ADDR_CAM_MODE, mode | (psx_mod_read_word(bank + off) & CAM_MODE_FREE_LOOK));
}

static void raise_view_count(void);

static uint16_t next_buffer(uint16_t f)
{
    return f >= 2u ? 0u : (uint16_t)(f + 1u);
}

/* --- the own view (netplay, presentation only) -------------------------- */

typedef struct LocalJob {
    uint32_t frame;      /* frame buffer (0..2) the flip will show */
    uint32_t buffer;     /* display buffer (0/1) holding it */
    uint32_t view;       /* this peer's view */
    uint32_t ra;
} LocalJob;

/* The runtime's main RAM (the render-pass helpers use it too,
 * render_pass_replay.hpp): 2 MB copied with memcpy instead of 512K word
 * calls, ~6.5 ms less per frame. Inside a pass every RAM store bypasses
 * code-page tracking anyway, and the pass restores all of RAM afterwards. */
extern uint8_t* memory_get_ram_ptr(void);

static void capture_frame_start(const CPUState* cpu)
{
    const uint32_t frame = psx_mod_read_half(ADDR_FRAME_INDEX);
    if (frame > 2u)
        return;
    FrameCopy* c = &g_copies[frame];
    if (!c->ram && !(c->ram = (uint8_t*)malloc(RAM_BYTES)))
        return;
    const uint8_t* ram = memory_get_ram_ptr();
    if (ram) {
        memcpy(c->ram, ram, RAM_BYTES);
    } else {
        for (uint32_t a = 0; a < RAM_BYTES; a += 4u) {
            const uint32_t w = psx_mod_read_word(0x80000000u + a);
            c->ram[a] = (uint8_t)w; c->ram[a + 1] = (uint8_t)(w >> 8);
            c->ram[a + 2] = (uint8_t)(w >> 16); c->ram[a + 3] = (uint8_t)(w >> 24);
        }
    }
    for (uint32_t a = 0; a < SCRATCH_BYTES; ++a)
        c->scratch[a] = psx_mod_read_byte(SCRATCH_ADDR + a);
    for (uint32_t a = 0; a < sizeof c->camera; ++a)
        c->camera[a] = psx_mod_read_byte(g_view_cameras + a);
    c->sp = cpu->gpr[29];
    c->generation = g_generation;
}

/* GPU DMA of one chain (the head word is the chain's first tag): in a pass the
 * linked list completes at once. */
static void send_chain(uint32_t head)
{
    psx_mod_write_word(GP1, 0x04000002u);                 /* DMA direction CPU -> GP0 */
    psx_mod_write_word(DMA2_MADR, head & 0x00FFFFFFu);
    psx_mod_write_word(DMA2_BCR, 0);
    psx_mod_write_word(DMA2_CHCR, 0x01000401u);           /* linked list, start */
}

static int draw_local_view(CPUState* cpu, void* user, uint32_t alpha)
{
    (void)alpha;
    const LocalJob* job = (const LocalJob*)user;
    const FrameCopy* c = &g_copies[job->frame];
    /* the state at the start of the frame the flip will show */
    uint8_t* ram = memory_get_ram_ptr();
    if (ram)
        memcpy(ram, c->ram, RAM_BYTES);
    else if (!psx_mod_host_write_ram(0x80000000u, c->ram, RAM_BYTES))
        return 0;
    for (uint32_t a = 0; a < SCRATCH_BYTES; ++a)
        psx_mod_write_byte(SCRATCH_ADDR + a, c->scratch[a]);
    for (uint32_t a = 0; a < sizeof c->camera; ++a)
        psx_mod_write_byte(g_view_cameras + a, c->camera[a]);
    /* one view, following this peer's player: its camera record and globals
     * become view 0's, its actor player 1 and player 1 the partner */
    if (job->view >= 1) {
        const uint32_t p1 = psx_mod_read_word(ADDR_P1_ACTOR);
        const uint32_t own = psx_mod_read_word(ADDR_FOLLOWED + 4u * job->view);
        if (!in_main_ram(own) || !in_main_ram(p1))
            return 0;
        for (uint32_t off = 4; off < CAM_RECORD_BYTES; off += 4)
            psx_mod_write_word(ADDR_CAM_RECORDS + off,
                               psx_mod_read_word(ADDR_CAM_RECORDS + job->view * CAM_RECORD_BYTES + off));
        camera_globals_load(view_bank(job->view));
        psx_mod_write_word(ADDR_P1_ACTOR, own);
        psx_mod_write_word(ADDR_P2_ACTOR, p1);
    }
    psx_mod_write_byte(ADDR_VIEW_COUNT, 1);
    /* Nothing is in flight for the GPU inside the draw (the shared frame's
     * draw is complete): the primitive allocators (FUN_8005A718, FUN_8005B798,
     * FUN_8005D480) wait while the write pointer is within 0x1000 bytes of the
     * oldest in-flight frame's primitives (0x8008DDF0) until a present has
     * moved it, which never happens in a pass (time stands still). */
    const uint32_t prim = psx_mod_read_word(ADDR_PRIM_NEXT);
    psx_mod_write_word(ADDR_PRIM_IN_FLIGHT, prim);
    psx_mod_write_word(ADDR_PRIM_IN_FLIGHT + 4u, prim);
    /* the frame again, as a 1-view game */
    cpu->gpr[29] = c->sp;
    (void)psx_mod_call_guest(cpu, ADDR_FRAME, job->ra, 0, 0, 0, 0);
    if (psx_mod_read_half(ADDR_FRAME_INDEX) != job->frame)
        return 0;
    /* the frame's draw environment for the target buffer, then its chains */
    (void)psx_mod_call_guest(cpu, ADDR_SET_FRAME_ENV, job->ra,
                             psx_mod_read_word(ADDR_FRAME_ENVS + job->frame * 4u), job->buffer, 0, 0);
    send_chain(0x8009390Cu + job->frame * 0x10u);
    send_chain(0x80093B60u + job->frame * 0x10u);
    send_chain(0x80093C88u + job->frame * 0x10u);
    if (psx_mod_read_byte(ADDR_EXTRA_CHAINS + job->frame)) {
        send_chain(0x80097264u + job->frame * 0x10u);
        send_chain(0x80097234u + job->frame * 0x10u);
        send_chain(0x80097204u + job->frame * 0x10u);
    }
    return 1;
}

static uint32_t copy_word(const FrameCopy* c, uint32_t addr)
{
    uint32_t v;
    memcpy(&v, c->ram + (addr & (RAM_BYTES - 4u)), sizeof v);
    return v;
}

/* Out: dead and waiting for the team's respawn, not in the world. A hero's
 * death step marks Bugs (0x8008DDD0) with 0x800100A2 bit 0 and any other
 * hero with bit 1 (FUN_80022510); the third actor has its own flag. */
static int out_of_play(const FrameCopy* c, uint32_t a)
{
    if (!in_main_ram(a))
        return 1;
    if (a == bbt_p3_actor())
        return bbt_p3_out();
    const uint8_t down = c->ram[ADDR_HERO_DOWN & (RAM_BYTES - 1u)];
    return a == copy_word(c, ADDR_BUGS_HERO) ? (down & 1u) != 0 : (down & 2u) != 0;
}

/* The view that shows a seat's own character, in a frame copy. Views 0 and
 * 1 follow players 1 and 2 (FUN_80011AF0), and the game swaps those when
 * player 1's hero is out (FUN_80022510 toggles 0x800100A4 bit 2; measured:
 * Bugs down -> player 1 = Taz), so seat 0 / 1 is the hero driven by pad 1 /
 * pad 2 (+0x0C Joystick1 / Joystick2), seat 2 the third actor. While a
 * seat's character is out, the seat watches a teammate still in play (the
 * user's choice; the stock one-screen camera moves to the survivor too). */
static uint32_t seat_view(const FrameCopy* c, int port, uint32_t count)
{
    uint32_t own = 0;
    if (port == 0 || port == 1) {
        const uint32_t pad_bit = port == 0 ? ACTOR_JOYSTICK1 : ACTOR_JOYSTICK2;
        for (uint32_t k = 0; k < 2u; ++k) {
            const uint32_t a = copy_word(c, k == 0 ? ADDR_P1_ACTOR : ADDR_P2_ACTOR);
            if (in_main_ram(a) && (copy_word(c, a + ACTOR_OFF_PADS) & pad_bit))
                own = a;
        }
    } else if (port == 2) {
        own = bbt_p3_actor();
    }
    uint32_t view = port > 0 && (uint32_t)port < count ? (uint32_t)port : 0u;
    for (uint32_t v = 0; own && v < count && v < MAX_VIEWS; ++v)
        if (copy_word(c, ADDR_FOLLOWED + 4u * v) == own) {
            view = v;
            break;
        }
    if (own && out_of_play(c, own)) {
        for (uint32_t v = 0; v < count && v < MAX_VIEWS; ++v)
            if (v != view && !out_of_play(c, copy_word(c, ADDR_FOLLOWED + 4u * v))) {
                psx_mod_counter_add(PLUGIN ".local_view_teammate", 1u);
                return v;
            }
    }
    return view;
}

/* Before the flip to the buffer drawn at the last present: draw this peer's
 * own view there. */
static void commit_local_view(CPUState* cpu)
{
    if (!psx_netplay_active() || !g_view_cameras)
        return;
    const uint32_t status = psx_mod_render_local_view_status();
    if (status != PSX_MOD_RENDER_PASS_READY) {
        psx_mod_counter_add(status == PSX_MOD_RENDER_PASS_SESSION ? PLUGIN ".local_session"
                            : status == PSX_MOD_RENDER_PASS_BUSY ? PLUGIN ".local_busy"
                            : PLUGIN ".local_unavailable", 1u);
        return;
    }
    LocalJob job;
    /* The flip shows the frame last presented (DE58). The copy taken at the
     * start of the frame after it holds that frame's finished state: logic
     * done, cameras moved. The draw runs only the frame's draws on it. */
    job.frame = next_buffer(psx_mod_read_half(ADDR_PRESENTED));
    job.buffer = psx_mod_read_half(ADDR_DRAW_BUFFER) & 1u;
    const int port = psx_netplay_local_port();
    job.ra = cpu->gpr[31];
    /* no copy yet, or one from before this peer's view existed (the first
     * frame after the view count rose): the shared frame shows this once */
    if (job.frame > 2u || !g_copies[job.frame].ram || g_copies[job.frame].generation != g_generation) {
        psx_mod_counter_add(PLUGIN ".local_no_copy", 1u);
        return;
    }
    const uint32_t copy_views = g_copies[job.frame].ram[ADDR_VIEW_COUNT & 0x1FFFFFu];
    job.view = seat_view(&g_copies[job.frame], port, copy_views);
    if (port > 0 && (uint32_t)port >= copy_views) {
        psx_mod_counter_add(PLUGIN ".local_no_copy", 1u);
        return;
    }
    if (job.view != (port > 0 ? (uint32_t)port : 0u))
        psx_mod_counter_add(PLUGIN ".local_view_swapped", 1u);
    PSXModRenderPass rect;
    rect.struct_size = sizeof rect;
    rect.alpha_q16 = 0;
    rect.x = psx_mod_read_half(ADDR_DISPENV);
    rect.y = (uint16_t)(job.buffer << 8);
    rect.w = psx_mod_read_half(ADDR_DISPENV + 4u);
    rect.h = psx_mod_read_half(ADDR_DISPENV + 6u);
    const int ok = psx_mod_render_local_view(cpu, &rect, draw_local_view, &job);
    psx_mod_counter_add(!ok ? PLUGIN ".local_refused"
                        : job.view == 2 ? PLUGIN ".local_view_2"
                        : job.view == 1 ? PLUGIN ".local_view_1" : PLUGIN ".local_view_0", 1u);
}

/* The present, from the main loop (see the header): the same on every peer. */
static void main_loop_present(CPUState* cpu)
{
    const uint32_t ra = cpu->gpr[31];
    const uint16_t need = (psx_mod_read_word(ADDR_LEVEL) != 0 && psx_mod_read_byte(ADDR_SLOW_PRESENTS))
                          ? 4u : 2u;
    for (unsigned i = 0; i < 8u && psx_mod_read_half(ADDR_VBLANKS) < need; ++i)
        (void)psx_mod_call_guest(cpu, ADDR_VSYNC, ra, 0, 0, 0, 0);
    (void)psx_mod_call_guest(cpu, ADDR_DRAWSYNC, ra, 0, 0, 0, 0);   /* the last present's draw */
    const int fresh = next_buffer(psx_mod_read_half(ADDR_PRESENTED)) != psx_mod_read_half(ADDR_FRAME_INDEX);
    if (fresh)
        commit_local_view(cpu);
    /* the frame-start copy, while libgpu's queue is empty: a pass takes no
     * interrupts, so a queued draw would never finish inside the rerun and a
     * DrawSync there would spin until the watchdog */
    if (psx_netplay_active())
        capture_frame_start(cpu);
    if (!fresh)
        return;                                     /* nothing new (a callback presented it) */
    g_main_present = 1;
    (void)psx_mod_call_guest(cpu, ADDR_PRESENT, ra, 0, 0, 0, 0);
    g_main_present = 0;
    psx_mod_counter_add(PLUGIN ".main_presents", 1u);
}

/* Wrap of FUN_80011AF0: raise the count at a frame start and present from
 * here while two views run; after the frame, neutralise the multi-view packet
 * nothing fills. Inside the own-view draw (a 1-view rerun) only the original
 * runs. */
static int frame_wrap(CPUState* cpu)
{
    if (psx_mod_local_view_scope()) {
        func_override_call_original(cpu);
        return 1;
    }
    raise_view_count();
    /* The cycle scale (game.toml [timing]) is for one PC's split screen,
     * where every view is drawn. A match draws none of them in the shared
     * frame (model_emit_wrap), so its frame fits two vblanks without the
     * scale, and at 1 the game's waits cost the host a quarter of the
     * instructions. The gate state is in savestates and rollback snapshots. */
    psx_mod_set_guest_cycle_scale_gate(psx_netplay_active() ? 0 : 1);
    if (g_emits_skipped) {
        psx_mod_counter_add(PLUGIN ".shared_emits_skipped", g_emits_skipped);
        g_emits_skipped = 0;
    }
    if (psx_mod_read_byte(ADDR_VIEW_COUNT) > 1) {
        main_loop_present(cpu);
    } else if (g_copies[0].generation == g_generation || g_copies[1].generation == g_generation ||
               g_copies[2].generation == g_generation) {
        ++g_generation;                             /* two views stopped: old copies are stale */
    }
    func_override_call_original(cpu);
    if (g_ring.active) {
        /* back to the main ring; the present protects the frame from view
         * 1's start, the last view drawn there */
        psx_mod_write_word(ADDR_PRIM_NEXT, g_ring.prim);
        psx_mod_write_word(ADDR_PRIM_RING_LO, g_ring.lo);
        psx_mod_write_word(ADDR_PRIM_RING_HI, g_ring.hi);
        psx_mod_write_word(ADDR_PRIM_IN_FLIGHT, g_ring.in0);
        psx_mod_write_word(ADDR_PRIM_IN_FLIGHT + 4u, g_ring.in1);
        psx_mod_write_word(ADDR_VIEW_PRIM_START + g_ring.frame * 4u, g_ring.start);
        g_ring.active = 0;
    }
    if (psx_mod_read_byte(ADDR_VIEW_COUNT) > 1) {
        const uint32_t frame = psx_mod_read_half(ADDR_FRAME_INDEX);
        const uint32_t pkt = frame < 3 ? psx_mod_read_word(ADDR_EXTRA_PACKETS + frame * 4u) : 0;
        /* the views draw into the frame's arena, so this packet is there */
        if (in_main_ram(pkt) || arena_span(pkt, 28u)) {
            for (uint32_t k = 1; k <= 6; ++k)
                psx_mod_write_word(pkt + 4u * k, 0);
        }
    }
    return 1;
}

/* A gameplay level with Bugs and Taz registered as the two players (menus
 * and the title have other actors, or none, in these slots). */
static int two_player_level(void)
{
    if ((psx_mod_read_byte(ADDR_MODE_FLAGS) & MODE_TWO_PLAYER) == 0)
        return 0;
    const uint32_t p1 = psx_mod_read_word(ADDR_P1_ACTOR), p2 = psx_mod_read_word(ADDR_P2_ACTOR);
    return in_main_ram(p1) && in_main_ram(p2) &&
           psx_mod_read_word(p1 + ACTOR_OFF_TYPE) == TYPE_BUGS &&
           psx_mod_read_word(p2 + ACTOR_OFF_TYPE) == TYPE_TAZ;
}

/* View v (>= 1) before the count includes it: its list buffer, empty GPU
 * chains in every frame buffer, its camera globals from view 0's; view 2
 * also gets camera record 1's state (Taz's camera: the third actor spawns
 * next to him) with its own view-unit pointer. */
static void prepare_view(uint32_t v)
{
    psx_mod_write_word(ADDR_VIEW_LISTS + 4u * v, view_list(v));
    for (unsigned h = 0; h < sizeof kChainHeads / sizeof kChainHeads[0]; ++h)
        for (uint32_t frame = 0; frame < 3; ++frame)
            psx_mod_write_word(kChainHeads[h] + frame * 0x10u + 4u * v, END_OF_CHAIN);
    /* a frame built before the change has no env packet for this view: the
     * present then writes through a 0 slot (RAM 0x4, as stock 1-view play
     * does), never through an old packet address */
    for (uint32_t frame = 0; frame < 3; ++frame)
        psx_mod_write_word(ADDR_ENV_SLOTS + (frame * 4u + v) * 4u, 0);
    camera_globals_save(view_bank(v));
    if (v >= 2) {
        const uint32_t to = ADDR_CAM_RECORDS + v * CAM_RECORD_BYTES;
        for (uint32_t off = 4; off < CAM_RECORD_BYTES; off += 4)
            psx_mod_write_word(to + off, psx_mod_read_word(ADDR_CAM_RECORDS + CAM_RECORD_BYTES + off));
        psx_mod_write_word(to, to + 4u);
    }
}

/* At a frame start: two views in a 2-player level, three while the third
 * actor (bbt.third-character p3) is in it; view 2 follows it. */
static void raise_view_count(void)
{
    const uint8_t now = psx_mod_read_byte(ADDR_VIEW_COUNT);
    if (now < 1 || now > MAX_VIEWS || !two_player_level())
        return;
    if (!psx_netplay_active() && !g_offline_split)
        return;                     /* one PC: the game's own shared screen */
    const uint32_t third = bbt_p3_actor();
    const uint8_t want = third ? 3u : 2u;
    /* the views' actors, as the frame loop sets them (it sets view 1's only
     * once the count is above 1, after the frame-start copy) */
    psx_mod_write_word(ADDR_FOLLOWED + 4u, psx_mod_read_word(ADDR_P2_ACTOR));
    if (want == 3u)
        psx_mod_write_word(ADDR_FOLLOWED + 8u, third);
    if (now == want)
        return;
    const int16_t entries = (int16_t)psx_mod_read_half(ADDR_LIST_ENTRIES);
    if (!g_view_lists || !g_view_cameras || entries <= 0 || (uint32_t)entries * 4u > LIST_BYTES) {
        psx_mod_counter_add(PLUGIN ".no_list", 1u);
        return;
    }
    for (uint32_t v = now; v < want; ++v)
        prepare_view(v);
    psx_mod_write_byte(ADDR_VIEW_COUNT, want);
    psx_mod_counter_add(want == 3u ? PLUGIN ".views_3" : PLUGIN ".views_set", 1u);
}

static int camera_update_wrap(CPUState* cpu)
{
    if (psx_mod_local_view_scope())
        return 1;                   /* the own-view draw: the camera already moved */
    if (psx_mod_read_byte(ADDR_VIEW_COUNT) <= 1 || !g_view_cameras)
        return 0;
    const uint32_t view = psx_mod_read_byte(ADDR_VIEW_INDEX);
    if (view >= MAX_VIEWS)
        return 0;
    const uint32_t actor = cpu->gpr[4];
    const uint32_t p1 = psx_mod_read_word(ADDR_P1_ACTOR), p2 = psx_mod_read_word(ADDR_P2_ACTOR);
    const uint32_t rec = ADDR_CAM_RECORDS + view * CAM_RECORD_BYTES;
    const uint32_t flags = psx_mod_read_word(rec + CAM_OFF_FLAGS);
    psx_mod_write_word(rec + CAM_OFF_FLAGS, (flags & ~CAM_FRAME_BOTH) | CAM_FOLLOW_ONE);

    /* a view's own actor is player 1 for its camera, player 1 its partner */
    const int swap = view >= 1 && actor != 0 && actor != p1 && in_main_ram(p1);
    const uint32_t partner = actor == p1 ? p2 : (in_main_ram(actor) ? p1 : 0);
    const uint32_t partner_followed =
        in_main_ram(partner) ? psx_mod_read_word(partner + ACTOR_OFF_FLAGS24) & ACTOR_FOLLOWED : 0;
    uint8_t view0_globals[CAMERA_BANK_BYTES];
    if (view >= 1) {
        /* keep view 0's live values on the host for the length of the call */
        uint32_t off = 0;
        for (unsigned i = 0; i < sizeof kCameraGlobals / sizeof kCameraGlobals[0]; ++i)
            for (uint32_t k = 0; k < kCameraGlobals[i].bytes; ++k, ++off)
                view0_globals[off] = psx_mod_read_byte(kCameraGlobals[i].addr + k);
        view0_globals[off] = (uint8_t)(psx_mod_read_word(ADDR_CAM_MODE) & CAM_MODE_FREE_LOOK);
        camera_globals_load(view_bank(view));
    }
    if (swap) {
        psx_mod_write_word(ADDR_P1_ACTOR, actor);
        psx_mod_write_word(ADDR_P2_ACTOR, p1);
    }
    func_override_call_original(cpu);
    if (swap) {
        psx_mod_write_word(ADDR_P1_ACTOR, p1);
        psx_mod_write_word(ADDR_P2_ACTOR, p2);
    }
    if (view >= 1) {
        camera_globals_save(view_bank(view));
        uint32_t off = 0;
        for (unsigned i = 0; i < sizeof kCameraGlobals / sizeof kCameraGlobals[0]; ++i)
            for (uint32_t k = 0; k < kCameraGlobals[i].bytes; ++k, ++off)
                psx_mod_write_byte(kCameraGlobals[i].addr + k, view0_globals[off]);
        const uint32_t mode = psx_mod_read_word(ADDR_CAM_MODE) & ~CAM_MODE_FREE_LOOK;
        psx_mod_write_word(ADDR_CAM_MODE, mode | view0_globals[off]);
    }
    if (in_main_ram(partner)) {
        const uint32_t f = psx_mod_read_word(partner + ACTOR_OFF_FLAGS24) & ~ACTOR_FOLLOWED;
        psx_mod_write_word(partner + ACTOR_OFF_FLAGS24, f | partner_followed);
    }
    return 1;
}

static int arena_span(uint32_t a, uint32_t bytes)
{
    return g_view2_arena && a >= g_view2_arena && a + bytes <= g_view2_arena + 3u * VIEW2_ARENA_BYTES;
}

static int view_step_prehook(CPUState* cpu)
{
    (void)cpu;
    if (psx_mod_read_byte(ADDR_VIEW_COUNT) <= 1)
        return 0;
    const uint32_t frame = psx_mod_read_half(ADDR_FRAME_INDEX);
    const uint32_t view = psx_mod_read_byte(ADDR_VIEW_INDEX);
    if (frame >= 3 || view >= 4)
        return 0;
    if (g_view2_arena && !g_ring.active) {
        /* The frame's first view step. A frame's primitives and the frame
         * the GPU may still be reading share the game's ring (227,952
         * bytes). With two or three views (70-110 KB a frame) it is nearly
         * full, and the allocators (FUN_8005A718 / B798 / D480) then wait at
         * their entry: while the write pointer is within 0x1000 bytes of the
         * oldest in-flight frame (0x8008DDF0) and fewer than 5 vblanks have
         * passed since the last present (0x8008DCDC; 0x8005B798..7EC), they
         * spin. A present moves the mark, and with our views the present is
         * at the frame start, so a frame that met the wait took 5 vblanks
         * instead of 2 (measured in a 3-peer match: 0.21-0.38 frames per
         * vblank instead of 0.5, and a third of the host's time in that
         * loop). So every view of a frame draws into the frame buffer's own
         * arena in the GPU-DMA aperture (256 KB, 24-bit list tags reach it;
         * it is written again three frames later, long after the GPU read
         * it): the game's pointer, ring bounds and in-flight marks point
         * there until the frame ends (frame_wrap puts them back), and the
         * allocators never wait. The main ring keeps its pointer; the
         * own-view rerun (one view) draws there. */
        g_ring.prim = psx_mod_read_word(ADDR_PRIM_NEXT);
        g_ring.lo = psx_mod_read_word(ADDR_PRIM_RING_LO);
        g_ring.hi = psx_mod_read_word(ADDR_PRIM_RING_HI);
        g_ring.in0 = psx_mod_read_word(ADDR_PRIM_IN_FLIGHT);
        g_ring.in1 = psx_mod_read_word(ADDR_PRIM_IN_FLIGHT + 4u);
        g_ring.start = psx_mod_read_word(ADDR_VIEW_PRIM_START + frame * 4u);
        g_ring.frame = frame;
        g_ring.active = 1;
        const uint32_t lo = g_view2_arena + frame * VIEW2_ARENA_BYTES, hi = lo + VIEW2_ARENA_BYTES;
        psx_mod_write_word(ADDR_PRIM_NEXT, lo);
        psx_mod_write_word(ADDR_PRIM_RING_LO, lo);
        psx_mod_write_word(ADDR_PRIM_RING_HI, hi);
        psx_mod_write_word(ADDR_PRIM_IN_FLIGHT, hi);
        psx_mod_write_word(ADDR_PRIM_IN_FLIGHT + 4u, hi);
    }
    const uint32_t prim = psx_mod_read_word(ADDR_PRIM_NEXT);
    if ((prim & 3u) || !(in_main_ram(prim) || arena_span(prim, 4u * (ENV_WORDS + 1u))))
        return 0;
    /* link at the head of the bucket, as FUN_80059314 does */
    const uint32_t bucket = ADDR_OT_FIRST + (OT_FIRST_BUCKETS - 1u) * 8u;
    const uint32_t head = psx_mod_read_word(bucket);
    psx_mod_write_word(bucket, prim);
    if (head == 0)
        psx_mod_write_word(bucket + 4u, prim);
    psx_mod_write_word(prim, (ENV_WORDS << 24) | (head & 0x00FFFFFFu));
    for (uint32_t k = 1; k <= ENV_WORDS; ++k)
        psx_mod_write_word(prim + 4u * k, 0);           /* NOPs until the present fills it */
    psx_mod_write_word(ADDR_PRIM_NEXT, prim + 4u * (ENV_WORDS + 1u));
    psx_mod_write_word(ADDR_ENV_SLOTS + (frame * 4u + view) * 4u, prim);
    return 0;
}

/* A match shows every PC its own view, drawn by the own-view rerun; the
 * frame the game itself builds, with every view in a band, is never shown
 * (only for a frame whose own view could not be drawn). Its models are
 * therefore not turned into primitives there: the two emitters return at
 * once. Everything else the draw walk FUN_8005EEDC does still runs, once per
 * view as before: the poses (FUN_80058474), the fades, the camera record's
 * bits 0x100 / 0x200 that Camera_Update reads (FUN_8005DE54), the shadow
 * list, the static models' own counters (FUN_8005A7A0). The emitters only
 * write the scratchpad draw context, a draw stamp (0x80081A2C) and the
 * primitives (Ghidra).
 *   FUN_8005ACAC(actor, nodes, ctx): an animated model; returns nothing.
 *   FUN_80059440(nodes, prim, ctx) -> the primitive pointer after the mesh.
 * Measured (3 peers on one M1, Release build, 2x GL cap): with the models
 * drawn 28-34 ticks a second; without them and with the cycle scale off
 * 60 a second, the game at its 30 frames (NOTES.md "Own view at full
 * speed"). Guest state decides (a match, more than one view, not the
 * rerun), so every peer and every rollback replay does the same. One PC's
 * split screen shows the bands and draws everything. */
#define ADDR_MODEL_EMIT     0x8005ACACu
#define ADDR_MESH_EMIT      0x80059440u
/* addiu sp,sp,-0x18 / sw ra,0x10(sp) / lw a3,0(a2) / nop */
static const uint32_t kModelEmitGuard[4] = {0x27BDFFE8u, 0xAFBF0010u, 0x8CC70000u, 0x00000000u};
/* addiu sp,sp,-0x28 / sw ra,0(sp) / sw s8,4(sp) / sw s7,8(sp) */
static const uint32_t kMeshEmitGuard[4] = {0x27BDFFD8u, 0xAFBF0000u, 0xAFBE0004u, 0xAFB70008u};

static int shared_frame_unseen(void)
{
    return psx_netplay_active() && !psx_mod_local_view_scope() && bbt_view_count() > 1u;
}

static int model_emit_wrap(CPUState* cpu)
{
    (void)cpu;
    if (!shared_frame_unseen())
        return 0;
    ++g_emits_skipped;
    return 1;
}

static int mesh_emit_wrap(CPUState* cpu)
{
    if (!shared_frame_unseen())
        return 0;
    cpu->gpr[2] = cpu->gpr[5];
    ++g_emits_skipped;
    return 1;
}

/* The EXE's 3-view entry of the screen-area table points back at the 2-view
 * table (both display buffers at buffer 0's, view 2 reading past its end):
 * three bands of 72 lines here, laid out like the 1- and 2-view entries
 * (x 512..1023, y 16..231 + 256 * buffer, the drawing offset at the band's
 * centre, the same E1 / E2 / E6). The words go where FUN_80059354 puts its
 * own: dst + 4. */
/* addiu v0,a3,-1 / sll v0,v0,3 / lui a3,0x8006 / addiu a3,a3,0x7420 */
static const uint32_t kEnvForViewGuard[4] = {
    0x24E2FFFFu, 0x000210C0u, 0x3C078006u, 0x24E79420u,
};

static int env_for_view_wrap(CPUState* cpu)
{
    const uint32_t dst = cpu->gpr[4], buffer = cpu->gpr[5] & 1u, view = cpu->gpr[6];
    if (cpu->gpr[7] != 3u || view >= 3u || !in_main_ram(dst))
        return 0;
    /* The drawing offset follows the game's own 2-view table (band top +
     * 106 for 108-line bands): with more than one view the actor pass loads
     * the GTE offset OFY = -64 (FUN_8005EEDC), so the 3D picture is centred
     * at offset - 64, while the HUD's sprites (full-screen layout, panels at
     * y -104) are drawn at the offset itself. Top + 104 puts the 3D centre at
     * band top + 40 (the band's centre + 4) and the HUD's top row at the
     * band's top. (The band centre as the offset put the 3D centre 28 lines
     * above the band and the HUD out of it.) */
    const uint32_t top = 16u + 256u * buffer + 72u * view, bottom = top + 71u, origin = top + 104u;
    const uint32_t words[6] = {
        0xE3000000u | (top << 10) | 512u,
        0xE4000000u | (bottom << 10) | 1023u,
        0xE5000000u | ((origin & 0x7FFu) << 11) | 768u,
        0xE100060Au, 0xE2000000u, 0xE6000000u,
    };
    for (uint32_t k = 0; k < 6; ++k)
        psx_mod_write_word(dst + 4u + 4u * k, words[k]);
    return 1;
}

#define ADDR_SOUND_VOLUME   0x80063D24u   /* FUN_80063D24(actor, sound, view) -> L | R << 8 */
#define ADDR_SOUND_MASTER   0x8008DBD4u   /* byte: master volume of the sound pass */
/* lbu v0,0x250(gp) / addiu sp,sp,-0x88 / sw s4,0x78(sp) / move s4,a1 */
static const uint32_t kSoundVolumeGuard[4] = {
    0x93820250u, 0x27BDFF78u, 0xAFB40078u, 0x00A0A021u,
};
/* The camera words the volume reads: vp x, y, z and vr x, y, z (+0x04..+0x18)
 * for the direction, the listener x, y, z (+0x2C..+0x34) for distance and pan. */
static const uint8_t kSoundCameraWords[9] = {0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x2C, 0x30, 0x34};

static uint32_t louder_channel(uint32_t lr)
{
    const uint32_t l = lr & 0xFFu, r = (lr >> 8) & 0xFFu;
    return l > r ? l : r;
}

static int sound_volume_wrap(CPUState* cpu)
{
    if (psx_mod_local_view_scope())
        return 0;
    /* the update walk shows the game a count of 1 (update_walk_wrap), and
     * most sounds start there: they are heard from every view all the same */
    const uint8_t count = psx_mod_read_byte(ADDR_VIEW_COUNT);
    const uint8_t views = g_walk_views > 1 ? g_walk_views : count;
    if (views <= 1 || views > MAX_VIEWS)
        return 0;
    const uint32_t actor = cpu->gpr[4], sound = cpu->gpr[5], view = cpu->gpr[6];
    if (views == 3u && actor != 0 && actor == psx_mod_read_word(ADDR_FOLLOWED + 8u)) {
        /* the third player, at the master volume like players 1 and 2 */
        const uint32_t master = psx_mod_read_byte(ADDR_SOUND_MASTER);
        cpu->gpr[2] = master | (master << 8);
        psx_mod_counter_add(PLUGIN ".sound_player_3", 1u);
        return 1;
    }
    /* a present from the VSync callback inside a call sees the real count
     * through g_walk_views, as during the update walk */
    const uint8_t walk_views = g_walk_views;
    g_walk_views = views;
    uint32_t from_camera_2 = 0;
    if (views == 3u) {
        /* as if camera 2 were the only camera */
        const uint32_t cam0 = ADDR_CAM_RECORDS, cam2 = ADDR_CAM_RECORDS + 2u * CAM_RECORD_BYTES;
        uint32_t saved[sizeof kSoundCameraWords];
        for (unsigned i = 0; i < sizeof kSoundCameraWords; ++i) {
            saved[i] = psx_mod_read_word(cam0 + kSoundCameraWords[i]);
            psx_mod_write_word(cam0 + kSoundCameraWords[i], psx_mod_read_word(cam2 + kSoundCameraWords[i]));
        }
        psx_mod_write_byte(ADDR_VIEW_COUNT, 1);
        cpu->gpr[6] = 0;
        func_override_call_original(cpu);
        from_camera_2 = cpu->gpr[2] & 0xFFFFu;
        for (unsigned i = 0; i < sizeof kSoundCameraWords; ++i)
            psx_mod_write_word(cam0 + kSoundCameraWords[i], saved[i]);
        cpu->gpr[4] = actor;
        cpu->gpr[5] = sound;
        cpu->gpr[6] = view;
    }
    /* as the game has it with more than one view: cameras 0 and 1 */
    psx_mod_write_byte(ADDR_VIEW_COUNT, views);
    func_override_call_original(cpu);
    psx_mod_write_byte(ADDR_VIEW_COUNT, count);
    g_walk_views = walk_views;
    if (count <= 1)
        psx_mod_counter_add(PLUGIN ".sound_in_walk", 1u);
    if (louder_channel(from_camera_2) > louder_channel(cpu->gpr[2] & 0xFFFFu)) {
        cpu->gpr[2] = from_camera_2;
        psx_mod_counter_add(PLUGIN ".sound_camera_2", 1u);
    }
    return 1;
}

/* A hero that falls out of the world (its y wraps past 32767 while falling
 * fast) is put back at the checkpoint by Actor_Update (0x8001DE5C..), which
 * then calls FUN_800537B4(record) = record +0xBC = 0 on camera record 0
 * only (BUGS #38). With views of their own, that reset view 0 (player 1's
 * camera) whoever fell, and left the faller's own camera alone. The reset
 * now goes to the record of the view that follows the hero that fell
 * (s4 in Actor_Update). Guest state only, the same on every peer. */
#define ADDR_CAMERA_RESET 0x800537B4u   /* FUN_800537B4(record) */
#define RA_FALL_RESET     0x8001DEE4u   /* its return address in Actor_Update */
#define CAM_RESET_FIELD   0xBCu
/* jr ra / sw zero,0xBC(a0) */
static const uint32_t kCameraResetGuard[2] = {0x03E00008u, 0xAC8000BCu};

static int camera_reset_wrap(CPUState* cpu)
{
    if (cpu->gpr[31] != RA_FALL_RESET || cpu->gpr[4] != ADDR_CAM_RECORDS)
        return 0;
    const uint8_t views = bbt_view_count();
    if (views <= 1 || views > MAX_VIEWS)
        return 0;
    const uint32_t hero = cpu->gpr[20];
    for (uint32_t v = 1; v < views; ++v) {
        if (psx_mod_read_word(ADDR_FOLLOWED + 4u * v) == hero) {
            psx_mod_write_word(ADDR_CAM_RECORDS + v * CAM_RECORD_BYTES + CAM_RESET_FIELD, 0);
            psx_mod_counter_add(PLUGIN ".fall_camera_reset", 1u);
            return 1;            /* view 0's camera is not the faller's */
        }
    }
    return 0;
}

uint8_t bbt_view_count(void)
{
    return g_walk_views > 1 ? g_walk_views : psx_mod_read_byte(ADDR_VIEW_COUNT);
}

static int update_walk_wrap(CPUState* cpu)
{
    if (psx_mod_local_view_scope())
        return 1;                   /* the own-view draw: the actors already moved */
    const uint8_t views = psx_mod_read_byte(ADDR_VIEW_COUNT);
    if (views <= 1)
        return 0;
    g_walk_views = views;
    psx_mod_write_byte(ADDR_VIEW_COUNT, 1);
    func_override_call_original(cpu);
    psx_mod_write_byte(ADDR_VIEW_COUNT, views);
    g_walk_views = 0;
    return 1;
}

/* Actor_Update through the shared hub (actor_update_hub.h): the actor a
 * view follows is updated in that view, so the behaviour script turns its
 * stick into movement with that view's camera. */
static int view_pre(CPUState* cpu, uint32_t actor, uint32_t* save)
{
    (void)cpu;
    const uint8_t views = g_walk_views > 1 ? g_walk_views : psx_mod_read_byte(ADDR_VIEW_COUNT);
    if (views <= 1 || actor == 0)
        return 0;
    const uint8_t now = psx_mod_read_byte(ADDR_VIEW_INDEX);
    for (uint32_t v = 1; v < views && v < 4u; ++v) {
        if (psx_mod_read_word(ADDR_FOLLOWED + 4u * v) == actor) {
            if (v == now)
                return 0;
            *save = now;
            psx_mod_write_byte(ADDR_VIEW_INDEX, (uint8_t)v);
            return 1;
        }
    }
    return 0;
}

static void view_post(CPUState* cpu, uint32_t actor, uint32_t save)
{
    (void)cpu;
    (void)actor;
    psx_mod_write_byte(ADDR_VIEW_INDEX, (uint8_t)save);
}

static const BbtActorUpdateHook kViewHook = {view_pre, view_post};

static void two_views_session_reset(void)
{
    bbt_actor_update_hook(BBT_ACTOR_HOOK_VIEW, NULL);
    bbt_mod_blocks_session_reset();
}

/* lui v0,0x8008 / lw v0,0x31FC(v0) / lui v1,0x8008 / lw v1,0x3200(v1) */
static const uint32_t kVSyncGuard[4] = {
    0x3C028008u, 0x8C4231FCu, 0x3C038008u, 0x8C633200u,
};
/* lui v0,0x8008 / lbu v0,0x3096(v0) / addiu sp,sp,-0x18 / sw s0,0x10(sp) */
static const uint32_t kDrawSyncGuard[4] = {
    0x3C028008u, 0x90423096u, 0x27BDFFE8u, 0xAFB00010u,
};

/* Inside the own-view draw no interrupt is taken and guest time stands still,
 * so VSync and DrawSync would wait until the watchdog: they return at once
 * there (a pass finishes GPU DMA synchronously). */
static int vsync_in_view(CPUState* cpu)
{
    if (!psx_mod_local_view_scope())
        return 0;
    psx_mod_counter_add(PLUGIN ".view_vsync", 1u);
    cpu->gpr[2] = 0;
    return 1;
}

static int drawsync_in_view(CPUState* cpu)
{
    if (!psx_mod_local_view_scope())
        return 0;
    psx_mod_counter_add(PLUGIN ".view_drawsync", 1u);
    cpu->gpr[2] = 0;
    return 1;
}

/* libcd's internal waits (named by their timeout messages): CD_cw writes a
 * command and waits for the CD interrupt to set the sync status, CD_sync and
 * CD_ready wait for those statuses, CD_datasync for the CD DMA. Inside the
 * own-view draw no interrupt comes (the pass drops CD stores), so a rerun of
 * a frame that issues a CD command (the Granwich dialog actor, type 0x310,
 * when a line advances: seen on all three peers on the same frame) spun to
 * the watchdog. There they return what the finished command would. */
#define ADDR_CD_SYNC        0x8007B838u   /* CD_sync(mode, result) -> status */
#define ADDR_CD_READY       0x8007BAB8u   /* CD_ready(mode, result) -> status */
#define ADDR_CD_CW          0x8007BD80u   /* CD_cw(cmd, param, result, async) -> 0 */
#define ADDR_CD_DATASYNC    0x8007C5B8u   /* CD_datasync(mode) -> 0 */
#define CDL_COMPLETE        2u
static const uint32_t kCdSyncGuard[4] = {0x27BDFFC0u, 0xAFB60030u, 0x0080B021u, 0xAFB70034u};
static const uint32_t kCdReadyGuard[4] = {0x27BDFFC0u, 0xAFB70034u, 0x0080B821u, 0xAFB30024u};
static const uint32_t kCdCwGuard[4] = {0x3C028009u, 0x8C42A344u, 0x27BDFFC8u, 0xAFB00018u};
static const uint32_t kCdDatasyncGuard[4] = {0x27BDFFD0u, 0xAFB20020u, 0x00809021u, 0x2404FFFFu};

static int cd_return_in_view(CPUState* cpu, uint32_t v0)
{
    if (!psx_mod_local_view_scope())
        return 0;
    psx_mod_counter_add(PLUGIN ".view_cd", 1u);
    cpu->gpr[2] = v0;
    return 1;
}
static int cd_sync_in_view(CPUState* cpu) { return cd_return_in_view(cpu, CDL_COMPLETE); }
static int cd_ready_in_view(CPUState* cpu) { return cd_return_in_view(cpu, 0); }
static int cd_cw_in_view(CPUState* cpu) { return cd_return_in_view(cpu, 0); }
static int cd_datasync_in_view(CPUState* cpu) { return cd_return_in_view(cpu, 0); }

static int present_wrap(CPUState* cpu)
{
    const uint8_t views = g_walk_views > 1 ? g_walk_views : psx_mod_read_byte(ADDR_VIEW_COUNT);
    if (views > 1) {
        const int from_main_loop = g_main_present;
        g_main_present = 0;
        if (!from_main_loop && psx_mod_read_half(ADDR_VBLANKS) < PRESENT_FALLBACK)
            return 1;                    /* frame_wrap presents (see the header) */
        if (!from_main_loop)
            psx_mod_counter_add(PLUGIN ".callback_presents", 1u);
    }
    if (g_walk_views <= 1)
        return 0;
    psx_mod_write_byte(ADDR_VIEW_COUNT, g_walk_views);
    func_override_call_original(cpu);
    psx_mod_write_byte(ADDR_VIEW_COUNT, 1);
    return 1;
}

PSX_MOD_CONSTRUCTOR(register_bbt_own_view)
{
    (void)psx_mod_register_activation_plugin(PLUGIN, two_views_activate);
    (void)psx_mod_register_function_override(PLUGIN ":update-walk", ADDR_UPDATE_WALK,
                                             update_walk_wrap, kUpdateWalkGuard, 4,
                                             FO_CREDIT_SELF);
    (void)psx_mod_register_session_reset_plugin(PLUGIN, two_views_session_reset);
    (void)psx_mod_register_function_override(PLUGIN ":camera", ADDR_CAMERA_UPDATE,
                                             camera_update_wrap, kCameraUpdateGuard, 4,
                                             FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":view-cd-sync", ADDR_CD_SYNC, cd_sync_in_view,
                                             kCdSyncGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":view-cd-ready", ADDR_CD_READY, cd_ready_in_view,
                                             kCdReadyGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":view-cd-cw", ADDR_CD_CW, cd_cw_in_view,
                                             kCdCwGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":view-cd-datasync", ADDR_CD_DATASYNC,
                                             cd_datasync_in_view, kCdDatasyncGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":view-vsync", ADDR_VSYNC, vsync_in_view,
                                             kVSyncGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":view-drawsync", ADDR_DRAWSYNC,
                                             drawsync_in_view, kDrawSyncGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":env-3-views", ADDR_ENV_FOR_VIEW,
                                             env_for_view_wrap, kEnvForViewGuard, 4,
                                             FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":fall-camera", ADDR_CAMERA_RESET,
                                             camera_reset_wrap, kCameraResetGuard, 2,
                                             FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":sound-3-views", ADDR_SOUND_VOLUME,
                                             sound_volume_wrap, kSoundVolumeGuard, 4,
                                             FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":present", ADDR_PRESENT, present_wrap,
                                             kPresentGuard, 4, FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":view-env", ADDR_VIEW_STEP,
                                             view_step_prehook, kViewStepGuard, 4,
                                             FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":shared-model", ADDR_MODEL_EMIT,
                                             model_emit_wrap, kModelEmitGuard, 4,
                                             FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":shared-mesh", ADDR_MESH_EMIT,
                                             mesh_emit_wrap, kMeshEmitGuard, 4,
                                             FO_CREDIT_SELF);
    (void)psx_mod_register_function_override(PLUGIN ":frame", ADDR_FRAME, frame_wrap,
                                             kFrameGuard, 4, FO_CREDIT_SELF);
}
