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
| `c-unit` | a C compiler | `src/mods/bouze.c`, `donor.c`, `donor_tex.c`, `clone_res.c` on synthetic data built by `tests/unit/synth.c`: the LZ decoder (literals, matches, long lengths, stopping at the token count), header and record/tag grammar, donor extraction, the synthetic spin and its attach node, Taz's record patch and timing fixes, the scale patch, texture analysis and rebuild (CLUT bit 15, CLUT blocks that declare more rows than they carry), the VRAM map, the target pool, packing (uploaded VRAM only, one texture page per block) and the record patch; the animation picker, the model relocation (as the loader's FUN_8005BB84), the strip packing for the third actor (half-height blocks, records sampling the kept rows) and the third actor's own resource list |
| `python-unit` | Python | `tools/bouze/` (decoder, record parser, keyframe parser) against the C code's synthetic level and spin, the ISO 9660 reader on synthetic 2048- and 2352-byte images, disc lookup from `game.toml` / `.cue`, the 3-player `game.toml` writer, `mods/state.toml` writing and restoring |
| `disc` | the player's disc (`game.toml` `[game] disc`) | it is the known dump (size, MD5); file layout; all 63 `.BZZ` files decode identically in C and Python and parse to their end marker; the TRCA01 enemy as the plugin extracts it (model, role table complete, spin on node 23, its 13 texture blocks / CLUTs as verified in VRAM); its and Taz's animations parse exactly; in all 38 levels with Taz the patch touches only his payloads and the texture plan fits (except `AZR01`, BUGS.md #20); the third actor's strip plan fits and every record samples inside its block |
| `game` | `build-dev` (2 seats, `PSX_DEBUG_TOOLS=ON`); `build-p3` (`-DBBT_MAX_PLAYERS=5`) for the 3-player test; the disc | end to end through the debug server, see below |

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
  seat 2 drives only it; ✕ □ ○ △ and the air spin land it on the floor
  drawn (the spin with a tornado); level jumps replace it; a savestate
  brings it back; random play on all three seats keeps the game running.

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
