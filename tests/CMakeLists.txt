# SPDX-License-Identifier: GPL-3.0-or-later
# All tests. Each `test_*.cpp` becomes its own executable + ctest entry.
# ponytail: no framework, no fixtures. asserts + int main.

# Tests use plain asserts; NDEBUG would strip them and make the test a no-op
# under Release / RelWithDebInfo. Keep asserts on: drop NDEBUG from this
# directory's flags (a -UNDEBUG on top trips MSVC's D9025 override warning).
foreach(v CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
    string(REGEX REPLACE "[-/]DNDEBUG" "" ${v} "${${v}}")
endforeach()

# A test with no data to run on (no MPQs, ISO or game.exe) exits 77
# (D2D_TEST_SKIPPED in the C++ tests, sys.exit(77) in the .py tests): ctest
# lists it as skipped, so a green run says which data-backed checks ran.
set(d2d_skip_code 77)

# Every tests/test_*.cpp is a test: adding one needs no line here, so two
# pull requests that each add a test don't both edit this file. A test names
# what it links in its own header, under the SPDX line:
#   // test-links: rules mpq          (targets; none: an empty `test-links:`)
#   // test-sources: apps/d2d/x.cpp   (extra sources, from the repo root)
#   // test-includes: apps/d2d        (extra include dirs, from the repo root)
file(GLOB d2d_test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
set(d2d_cpp_tests "")
foreach(test_source ${d2d_test_sources})
    get_filename_component(name ${test_source} NAME_WE)
    file(STRINGS ${test_source} directives REGEX "^// test-(links|sources|includes):")
    if (NOT directives MATCHES "test-links:")
        message(FATAL_ERROR "${name}.cpp: no `// test-links:` line under the SPDX line (tests/CMakeLists.txt)")
    endif()
    add_executable(${name} ${name}.cpp)
    target_compile_definitions(${name} PRIVATE D2D_TEST_SKIPPED=${d2d_skip_code})
    foreach(directive ${directives})
        string(REGEX MATCH "^// test-([a-z]+):(.*)$" _ "${directive}")
        set(kind ${CMAKE_MATCH_1})
        separate_arguments(values UNIX_COMMAND "${CMAKE_MATCH_2}")
        if (kind STREQUAL "links")
            target_link_libraries(${name} PRIVATE ${values})
        else()
            list(TRANSFORM values PREPEND ${PROJECT_SOURCE_DIR}/)
            if (kind STREQUAL "sources")
                target_sources(${name} PRIVATE ${values})
            else()
                target_include_directories(${name} PRIVATE ${values})
            endif()
        endif()
    endforeach()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE ${d2d_skip_code})
    list(APPEND d2d_cpp_tests ${name})
endforeach()

# End-to-end: headless d2d driven over devctl (SDL dummy driver).
# Python3_EXECUTABLE rather than `python3`: Windows installs it as python.exe.
find_package(Python3 COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
    add_test(NAME smoke_d2d
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/smoke_d2d.py $<TARGET_FILE:d2d>)
    # Local co-op over devctl's virtual pads (docs/design/local-coop.md).
    add_test(NAME coop_d2d
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/coop_d2d.py $<TARGET_FILE:d2d>)
    add_test(NAME coop_lobby_d2d
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/coop_lobby_d2d.py $<TARGET_FILE:d2d>)
    # The pad's A on portals and waypoints.
    add_test(NAME pad_use_d2d
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/pad_use_d2d.py $<TARGET_FILE:d2d>)
    # One per act, each from a new character: `ctest -R quests_act -j3` checks
    # every act's quests in about the time of one. A new act: its number here.
    set(d2d_quest_tests "")
    foreach (act 1 2 3 4 5)
        add_test(NAME quests_act${act}
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/quests_d2d.py $<TARGET_FILE:d2d> ${act})
        list(APPEND d2d_quest_tests quests_act${act})
    endforeach()
    set_tests_properties(smoke_d2d coop_d2d coop_lobby_d2d pad_use_d2d ${d2d_quest_tests}
                         PROPERTIES SKIP_RETURN_CODE ${d2d_skip_code})
endif()

# Optional game-data inputs, handed to the tests that use them (they skip
# those parts when unset): -DD2_PATCH_INSTALLER=.../LODPatch_114d.exe,
# -DD2_SAVES_DIR=<dir of real .d2s saves>, -DD2_GAME_EXE=.../game.exe.
set(D2_PATCH_INSTALLER "$ENV{D2_PATCH_INSTALLER}" CACHE FILEPATH "LODPatch_114d.exe for the 1.14d tables")
set(D2_GAME_EXE "$ENV{D2_GAME_EXE}" CACHE FILEPATH "game.exe 1.14d (1.14.3.71) for test_exe_tables")
set(D2_SAVES_DIR "$ENV{D2_SAVES_DIR}" CACHE PATH "Real .d2s saves for parser checks")
set(d2d_test_env "")
if (D2_PATCH_INSTALLER)
    list(APPEND d2d_test_env "D2_PATCH_INSTALLER=${D2_PATCH_INSTALLER}")
endif()
if (D2_GAME_EXE)
    list(APPEND d2d_test_env "D2_GAME_EXE=${D2_GAME_EXE}")
endif()
if (D2_SAVES_DIR)
    list(APPEND d2d_test_env "D2_SAVES_DIR=${D2_SAVES_DIR}")
endif()
if (d2d_test_env)
    set_tests_properties(${d2d_cpp_tests} PROPERTIES ENVIRONMENT "${d2d_test_env}")
    if (TARGET d2d AND Python3_Interpreter_FOUND)
        set_tests_properties(smoke_d2d coop_d2d coop_lobby_d2d pad_use_d2d ${d2d_quest_tests} PROPERTIES ENVIRONMENT "${d2d_test_env}")
    endif()
endif()
