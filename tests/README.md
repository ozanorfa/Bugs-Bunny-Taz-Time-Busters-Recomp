# Tests

```sh
python3 tests/run_tests.py              # everything that can run here
python3 tests/run_tests.py unit disc    # the fast tiers (~20 s)
python3 tests/run_tests.py game -k trca01
```

Exit status: 0 passed, 1 failed, 77 nothing could run (all skipped). The
runner configures and builds the tests' own CMake project (`tests/`) into
`build-tests/` first (`--no-build` skips that). Output goes to
`build-tests/out/`: game logs, screenshots per stage, the temporary memory
cards and savestates of each run. Git ignores all of it.

The same tiers are registered with CTest:

```sh
cmake -S tests -B build-tests && cmake --build build-tests
ctest --test-dir build-tests -L unit --output-on-failure
```

Standard library only (Python 3.8+, 3.11+ also parses TOML with `tomllib`).
Nothing here is macOS-specific: paths go through `pathlib`, the game runs
through `subprocess`, executables are looked up with `.exe` on Windows and
in per-configuration folders of multi-config generators.

## Tiers

| Tier | Needs | What it covers |
|---|---|---|
| `c-unit` | a C compiler | `src/mods/bouze.c`, `donor.c`, `donor_tex.c`, `clone_res.c` on synthetic data built by `tests/unit/synth.c`: the LZ decoder (literals, matches, long lengths, stopping at the token count), header and record/tag grammar, donor extraction, the synthetic spin and its attach node, Taz's record patch and timing fixes, the scale patch, texture analysis and rebuild (CLUT bit 15, CLUT blocks that declare more rows than they carry), the VRAM map, the target pool, packing (uploaded VRAM only, one texture page per block) and the record patch; the animation picker, the model relocation (as the loader's FUN_8005BB84), the strip packing for the third actor (half-height blocks, records sampling the kept rows), the hold animation, and the third actor's own resource list (with native idle timing) |
| `python-unit` | Python | `tools/bouze/` (decoder, record parser, keyframe parser) against the C code's synthetic level and spin, the ISO 9660 reader on synthetic 2048- and 2352-byte images, disc lookup from `game.toml` / `.cue`, the 3-player `game.toml` writer, `mods/state.toml` writing and restoring |
| `disc` | the player's disc (`game.toml` `[game] disc`) | it is the known dump (size, MD5); file layout; all 63 `.BZZ` files decode identically in C and Python and parse to their end marker; the TRCA01 enemy as the plugin extracts it (model, role table complete, spin on node 23, its 13 texture blocks / CLUTs as verified in VRAM); its and Taz's animations parse exactly; in all 38 levels with Taz the patch touches only his payloads and the texture plan fits (except `AZR01`, BUGS.md #20); the third actor's strip plan fits and every record samples inside its block; its hold animation is the stance's first frame plus empty frames |
| `game` | `build-dev` (2 seats, `PSX_DEBUG_TOOLS=ON`); `build-p3` (`-DBBT_MAX_PLAYERS=5`) for the 3-player test; `build-net` (`-DPSX_NETPLAY=ON -DRNET_ENABLE_ICE=OFF -DBBT_MAX_PLAYERS=5`) for the netplay test; the disc | end to end through the debug server, see below |

### Game tests

Each test module starts the game once, headless, on a free localhost port,
with a fresh memory-card folder (so no save and no savestate of yours is
used or touched), and plays from a cold boot like a person would. The
build's `mods/state.toml` is replaced by the features the test needs and
put back afterwards (also after an interrupted run). The tests refuse to
run against `build-release`, which holds your own mod selection
(`BBT_ALLOW_RELEASE=1` overrides).

- `test_game_stock.py` — every feature off: language screen, title,
  TWO PLAYERS (with pad 2 connected), Granwich with Bugs as P1 and Taz as
  P2, each pad moves only its own character, Taz keeps his own animations,
  no plugin activity.
- `test_game_trca01.py` — Taz as the Transylvania enemy: donor loaded and
  every load path clean (counters); Taz plays only donor animations, Bugs
  is untouched; the enemy's textures are in VRAM exactly where the plugin
  uploaded them (pixel hashes from `{"cmd":"mod_cmd","name":"bbt.trca01"}`)
  and Taz is drawn with them; ✕ ○ □ △ each bring Taz back to the ground
  with his body drawn (the floating / invisible reports); the spin attaches
  the tornado; a level jump to VICA01 relocates the textures again; the
  jump to TRCA01 keeps the enemy's own.
- `test_game_p3.py` — the third player (feature `p3`, `build-p3` with a
  3-player `game.toml`): with the multitap on port 2 the 2-player game
  still works (pad 2 = tap slot A); the plugin's third record follows seat
  2 in the game's bit order once per game frame (driven with
  `{"cmd":"mod_cmd","name":"bbt.p3"}`); a third actor (Taz's copy drawn as
  the enemy) is spawned next to Taz with its own resources while Bugs and
  Taz keep theirs; its textures are in the VRAM strip (pixel hashes from
  `{"cmd":"mod_cmd","name":"bbt.p3actor"}`) and it is drawn with them;
  it plays whole idle loops (no trembling) and still answers at once; seat 2 drives only it; ✕ □ ○ △ and the air spin land it on the floor
  drawn (the spin with a tornado); level jumps replace it; a savestate
  brings it back; random play on all three seats keeps the game running.
  Phase 5: a real hit (2 damage) brings in the third player's own HUD
  panel (sprites 15 / 16) under Bugs' panel, its bar blinks while the hit's
  invincibility runs and ends at 4/6 of 80 pixels, Bugs' panel does not
  move, the panel slides out and the invincibility ends.
- `test_game_quickstart.py` — Quick start (`bbt.quick-start`, defaults):
  no logo levels and the language screen at once; START GAME reaches
  Granwich without the intro ever running; afterwards both pads move their
  character, the HUD has set itself up, no cinema bars, the pause screen
  opens and closes; a level jump takes less than half the stock 470 frames
  at the default 2x CD speed. With skip_logos off the logos are shown.
- `test_game_ownview.py` — the engine's own two views (experiment
  `bbt.own-view` "two-views", with Quick start): Granwich runs with the
  view count 2, view 0 follows Bugs and view 1 Taz, each pad moves only its
  own character, and every frame that draws the world sets the top-half and
  the bottom-half draw area (GP0 dump, `gpu_frame_dump`). Each view has a
  single-player camera: Taz walks more than 6000 units from Bugs (stock 2P
  freezes him at about 3050), each camera looks at its own player, and each
  pad's "up" turns its character to its own camera's forward direction.
  While two views run, the game presents from the main loop (frame wrap),
  not from its VSync callback, and still makes a frame every 2 vblanks
  while both players run (game.toml `[timing]` cycle scale gated on the
  view count).
- `test_game_ownview3.py` — three views (build-p3, 3-player game.toml, the
  third player "p3" + "Two views" + Quick start): the view count becomes 3,
  the views follow Bugs, Taz and the third actor with single-player
  cameras, seat 2 turns the third actor to camera 2's forward, the game
  keeps ≥ 0.44 frames per vblank with everyone running, the shared
  Actor_Update feature is activated implicitly, and the third actor's own
  sounds (sound slot table 0x80093274) keep the master volume when it is
  farther than 0x1000 from Bugs' and Taz's cameras. A hit brings in its HUD
  panel at the top centre of each 72-line band (the same check as in
  `test_game_p3.py`, `check_p3_panel`; screenshot `4b_hud_panel.png`); its
  invincibility then ends, so the next test can kill it. Its death (a hit put in
  RAM) leaves Taz and the lives alone and takes it out (not listed); after
  Bugs and Taz both go down and the team respawns (a life lost) it is back
  next to Taz with full health and moves again. A second run
  (EnemiesSeeTheThirdPlayer) jumps to TRCA01: a Transylvania enemy (model
  635) ignores the third player while the codes 4-7 rule is off (debug
  command `ref-third off`) and chases and hits it once it is on.
- `test_game_net.py` — netplay with our mods (`build-net`): two headless
  peers on 127.0.0.1 in a rollback match (`--netplay`, `PSX_NET_MODE=rollback`),
  each from a folder of its own under `build-tests/out/net-*/` (a link to
  the executable, its own `mods/`), so the build's `mods/state.toml` is not
  used. Both on "Taz becomes the Transylvania enemy": the host plays into
  Granwich, the mod is active on both peers, and the machine digests both
  logged every 32 ticks agree. One peer with the mod and one without: the
  match never starts ("LAN content differs between peers"). Both also on
  Quick start: it acts alike on both peers and the digests agree. Both on
  Quick start and "Two views", each in a hidden OpenGL window
  (`NetMatch(window=True)`, GL scale capped at 2x with `PSX_GL_MAX_DIM`):
  each peer draws its own player's view full screen
  (`psx_mod_render_local_view`, counters `local_view_0` / `local_view_1`),
  none refused, the authoritative frame (`screenshot`) still split and the
  presented one (`screenshot_hires`) not (row brightness, `bbt/png.py`),
  digests equal. Three peers (host as relay, `NetMatch` with three feature
  sets) on the third character and Quick start with a 3-player game.toml:
  the third actor spawns on every peer, the third peer's own pad (seat 2)
  drives it the same way everywhere, digests equal. Three peers on the
  third character + "Two views" in hidden windows: each peer draws its own
  seat's view (`local_view_0/1/2`), no watchdog, no split shown, digests
  equal. Two peers on "Two views" after Bugs is down (a hit put in RAM on
  both, so no digest check): Taz is player 1, the guest draws Taz (view 0)
  and the host, whose Bugs is out, his teammate Taz (`local_view_teammate`).
  About 4.5 min in all.
- `test_game_lanroom.py` — a LAN room through the real launcher (`build-net`;
  on macOS it keeps the display awake with `caffeinate`, the launchers stall
  while it sleeps):
  two launchers driven by recomp-ui's script hook (`LNG_SCRIPT`, hidden
  windows), host Host Lobby -> Create Lobby, guest Join Direct
  127.0.0.1:7777, host PLAY. The host selected the Phase 3 mod and Quick
  start, the guest nothing: both run the host's mods, the digests agree,
  and the guest's own selection is unchanged. Click positions are for the
  1100x750 launcher layout; when the launcher's layout changes, update them
  from the screenshots in `build-tests/out/lanroom/`. About 75 s.

A game test takes 2–4 minutes (headless runs at ~74 frames/s; a cold boot to
Granwich is ~5,000 frames). Screenshots of every stage are in
`build-tests/out/<module>/` for a look when something fails.

## Rules for new tests

- No game data in the repository: tests may read the player's disc at run
  time, and may only compare sizes, counts, positions and hashes. Test
  output stays under `build-tests/`.
- Expected values come from verified facts (NOTES.md); say where in a
  comment.
- A test must be able to fail. The C unit tests were checked by running
  them against deliberately broken copies of the mod code (each mutation
  must make a test fail).
- Do not drive a host controller from a test; third-player input goes
  through the seat state netplay synchronizes (see `third_player.c`).
