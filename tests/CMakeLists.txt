# Tests of the Bugs Bunny & Taz: Time Busters recomp project.
#
# A standalone CMake project (it does not need the game build, a BIOS or
# generated code):
#
#   cmake -S tests -B build-tests
#   cmake --build build-tests
#   ctest --test-dir build-tests --output-on-failure            # everything
#   ctest --test-dir build-tests -L unit --output-on-failure    # hermetic only
#
# or simply:  python3 tests/run_tests.py   (see tests/README.md)
#
# Tiers (ctest labels):
#   unit  hermetic: C tests of the mod's host-side logic on synthetic data,
#         and Python tests of the project's tools. No disc needed.
#   disc  offline checks against the player's own disc image (found through
#         game.toml). Skipped (exit 77) when the disc is not there.
#   game  end-to-end: the built game runs headless and is driven through the
#         PSXRecomp debug server. Skipped when the build is not there.
cmake_minimum_required(VERSION 3.20)
project(bbt_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BBT_MODS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/mods)

if(MSVC)
    add_compile_options(/W4 /D_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-function)
endif()

# The mod's host-side logic, compiled exactly as the plugin uses it.
add_library(bbt_mod_logic STATIC
    ${BBT_MODS_DIR}/bouze.c
    ${BBT_MODS_DIR}/donor.c
    ${BBT_MODS_DIR}/donor_tex.c
    ${BBT_MODS_DIR}/clone_res.c
    ${BBT_MODS_DIR}/retarget.c
)
target_include_directories(bbt_mod_logic PUBLIC ${BBT_MODS_DIR})

add_executable(bbt_unit_tests
    unit/unit_main.c
    unit/synth.c
    unit/test_bouze.c
    unit/test_donor.c
    unit/test_donor_tex.c
    unit/test_clone_res.c
    unit/test_retarget.c
)
target_link_libraries(bbt_unit_tests PRIVATE bbt_mod_logic)

# Reads the player's disc and prints facts as JSON for tests/test_disc_*.py.
add_executable(bbt_disc_probe probe/bbt_disc_probe.c)
target_link_libraries(bbt_disc_probe PRIVATE bbt_mod_logic)

enable_testing()
find_package(Python3 3.8 COMPONENTS Interpreter)

add_test(NAME bbt_unit_c COMMAND bbt_unit_tests)
set_tests_properties(bbt_unit_c PROPERTIES LABELS unit)

if(Python3_FOUND)
    set(BBT_RUN ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py)
    add_test(NAME bbt_unit_py
             COMMAND ${BBT_RUN} python-unit --no-build
                     --unit-exe $<TARGET_FILE:bbt_unit_tests>)
    set_tests_properties(bbt_unit_py PROPERTIES LABELS unit)
    add_test(NAME bbt_disc
             COMMAND ${BBT_RUN} disc --no-build --probe $<TARGET_FILE:bbt_disc_probe>)
    set_tests_properties(bbt_disc PROPERTIES LABELS disc SKIP_RETURN_CODE 77)
    add_test(NAME bbt_game COMMAND ${BBT_RUN} game --no-build)
    set_tests_properties(bbt_game PROPERTIES LABELS game SKIP_RETURN_CODE 77 TIMEOUT 3600)
endif()
