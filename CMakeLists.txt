cmake_minimum_required(VERSION 3.20)
set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH
    "Path to psxrecomp")
if(NOT EXISTS "${PSXRECOMP_ROOT}/runtime/runtime.cmake")
    message(FATAL_ERROR
        "PSXRecomp runtime is missing: ${PSXRECOMP_ROOT}/runtime/runtime.cmake\n"
        "This project expects the psxrecomp framework checkout at ${PSXRECOMP_ROOT}.\n"
        "Run: git submodule update --init --recursive\n"
        "If psxrecomp/ is not a submodule, set -DPSXRECOMP_ROOT=<path-to-psxrecomp>.")
endif()

project(BugsBunnyTazRecomp LANGUAGES C CXX)

# --- Opt-in product surfaces (OFF by default in the framework) -------------
# Scaffold prompts / flags fill these. Without recomp-ui, PSX_RECOMP_UI is OFF.
# The wizard (WIZARD_CMAKE_BLOCK / ENABLE_SETUP_WIZARD) is the first-run disc
# picker. This project does not commit generated/ (it is derived from the
# disc): the players' ZIP generates and builds on each player's PC, and a
# ready-built package is compiled from the packaging PC's own generated/ C.
# recomp-ui submodule present (PSX_RECOMP_UI defaults ON).
# Netplay (LAN / Direct IP) is a configure-time choice, not this file's:
# configure a separate build with -DPSX_NETPLAY=ON -DRNET_ENABLE_ICE=OFF
# (build-net; ICE is only for online lobbies), so the other builds stay
# without it. option(PSX_NETPLAY) in runtime.cmake keeps that cache value.
# Full netplay UI — enable after adding recomp-ui + testing:
# if(EXISTS "${PSXRECOMP_ROOT}/lib/recomp-net/CMakeLists.txt")
#     set(PSX_NETPLAY ON CACHE BOOL "…" FORCE)
# endif()
set(PSX_SETUP_WIZARD ON CACHE BOOL
    "Advertise first-run setup wizard + Generate & rebuild in recomp-ui" FORCE)
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

# This project's own mod catalog. The FRAMEWORK stages it -- together with its
# builtin psx.* packages -- into <exe-dir>/mods/bundled, and refuses to
# configure if a catalog exists here but is not declared. Shared Internal
# Resolution is included even with a builtin allowlist, enabled at 4x on fresh
# profiles, with 1x..32x choices; do not supply a title-local copy of that id.
# Never copy the catalog by hand: five titles did, and framework 4cc04be3's rename of the staged
# directory broke all five silently (bead beads-eio.3.101).
set(PSX_PRELOADED_MODS "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded")
if(NOT IS_DIRECTORY "${PSX_PRELOADED_MODS}")
    set(PSX_PRELOADED_MODS "NONE")
endif()

# Controller seats compiled in. 2 = the game's two ports. Phase 4 (third
# player) builds with 5: one SCPH-1070 multitap plus the opposite port's pad
# (PSX_MAX_PLAYERS >= 5 is what enables the runtime's multitap and seat 3).
set(BBT_MAX_PLAYERS 2 CACHE STRING "Controller seats compiled into the runtime")

# The config the exe opens without --game (double-click, launcher). A build
# with the multitap seats opens the 3-player config, so its launcher offers
# three players and its LAN rooms three seats (a room's seat limit is the
# config's `players`), and the launcher's multitap-analog choice goes into
# the staged copy next to the exe, not into the repository's file. The
# release package ships game_3p.toml as its game.toml, so packaging sets
# -DBBT_DEFAULT_GAME_CONFIG=game.toml (packaging/release.toml).
set(BBT_DEFAULT_GAME_CONFIG "" CACHE STRING
    "Config opened without --game (empty: game_3p.toml with >= 5 seats, else game.toml)")
if(BBT_DEFAULT_GAME_CONFIG)
    set(_BBT_DEFAULT_CONFIG "${BBT_DEFAULT_GAME_CONFIG}")
elseif(BBT_MAX_PLAYERS GREATER_EQUAL 5)
    set(_BBT_DEFAULT_CONFIG "game_3p.toml")
else()
    set(_BBT_DEFAULT_CONFIG "game.toml")
endif()

psxrecomp_add_game_runtime(psx-runtime
    PRELOADED_MODS_DIR "${PSX_PRELOADED_MODS}"
    # ENABLE_NETPLAY_IF_PRESENT
    # NETPLAY_LOBBY_URL "ws://netplay.retcomm.net:8765"
    ENABLE_SETUP_WIZARD
    WINDOW_TITLE "BugsBunnyTaz Recompiled"
    DEFAULT_GAME_CONFIG_PATH "${_BBT_DEFAULT_CONFIG}"
    MAX_PLAYERS ${BBT_MAX_PLAYERS}
    # LAUNCHER_BOXART "${CMAKE_CURRENT_SOURCE_DIR}/launcher_assets/img/boxart.tga"
    APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/psxrecomp.ico"
    # Game C from psxrecomp_cli.py generate (generated/ is NOT committed in
    # this project: it is derived from the disc, each machine generates it).
    GEN_MARKER "generated/SLUS_011.44_dispatch.c"
    GEN_FULL_GLOB "generated/SLUS_011.44_full_*.c"
    CODEGEN_SETUP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/codegen_setup.c"
)

# Trusted mod plugins compiled into the game (selected by the manifests in
# mods/preloaded/packages; inactive unless a package feature enables them).
target_sources(psx-runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/donor.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/donor_tex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/third_player.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/quick_start.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/decor.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/own_view.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/actor_update_hub.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/load_image_hub.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/third_actor.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/third_hud.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/retarget.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/daffy_moves.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/clone_res.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/donor_runtime.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/bouze.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/mod_blocks.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/mod_blocks_runtime.c")

# Stage runtime configs next to the exe for direct build/ launches. Only copy
# files that exist — never fail POST_BUILD when game_options.toml is absent.
foreach(_PSX_RUNTIME_CONFIG game.toml game_3p.toml game_options.toml)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}")
        add_custom_command(TARGET psx-runtime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}"
                "$<TARGET_FILE_DIR:psx-runtime>/${_PSX_RUNTIME_CONFIG}"
            COMMENT "Staging ${_PSX_RUNTIME_CONFIG}"
            VERBATIM)
    endif()
endforeach()
