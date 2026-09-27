# All tests. Each `test_*.cpp` becomes its own executable + ctest entry.
# ponytail: no framework, no fixtures. asserts + int main.

# Tests use plain asserts; NDEBUG would strip them and make the test a no-op
# under Release / RelWithDebInfo. Keep asserts on: drop NDEBUG from this
# directory's flags (a -UNDEBUG on top trips MSVC's D9025 override warning).
foreach(v CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
    string(REGEX REPLACE "[-/]DNDEBUG" "" ${v} "${${v}}")
endforeach()

function(d2d_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

d2d_add_test(test_iso9660 iso9660)
d2d_add_test(test_mpq mpq)
d2d_add_test(test_tbl mpq tbl)
d2d_add_test(test_dc6 mpq dc6)
d2d_add_test(test_dt1 mpq dt1)
d2d_add_test(test_ds1 mpq ds1)
d2d_add_test(test_cof mpq cof)
d2d_add_test(test_dcc mpq dcc)
d2d_add_test(test_devctl devctl)
d2d_add_test(test_screenshot screenshot)   # zlib transitive via screenshot
d2d_add_test(test_palette mpq palette)
d2d_add_test(test_font mpq font)
d2d_add_test(test_d2s d2s mpq)
d2d_add_test(test_protocol)
target_include_directories(test_protocol PRIVATE ${CMAKE_SOURCE_DIR}/apps/d2d)
d2d_add_test(test_userdir userdir)
d2d_add_test(test_compcode mpq compcode)
d2d_add_test(test_rules rules)
d2d_add_test(test_monsters rules)
d2d_add_test(test_quests rules)
d2d_add_test(test_light rules)
d2d_add_test(test_combat rules)
d2d_add_test(test_drops rules)
d2d_add_test(test_skills rules)
d2d_add_test(test_drlg drlg)
d2d_add_test(test_outdoor drlg mpq)

# End-to-end: headless d2d driven over devctl (SDL dummy driver).
# Python3_EXECUTABLE rather than `python3`: Windows installs it as python.exe.
find_package(Python3 COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
    add_test(NAME smoke_d2d
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/smoke_d2d.py $<TARGET_FILE:d2d>)
endif()

# Optional game-data inputs, handed to the tests that use them (they skip
# those parts when unset): -DD2_PATCH_INSTALLER=.../LODPatch_114d.exe,
# -DD2_SAVES_DIR=<dir of real .d2s saves>.
set(D2_PATCH_INSTALLER "$ENV{D2_PATCH_INSTALLER}" CACHE FILEPATH "LODPatch_114d.exe for the 1.14d tables")
set(D2_SAVES_DIR "$ENV{D2_SAVES_DIR}" CACHE PATH "Real .d2s saves for parser checks")
set(d2d_test_env "")
if (D2_PATCH_INSTALLER)
    list(APPEND d2d_test_env "D2_PATCH_INSTALLER=${D2_PATCH_INSTALLER}")
endif()
if (D2_SAVES_DIR)
    list(APPEND d2d_test_env "D2_SAVES_DIR=${D2_SAVES_DIR}")
endif()
if (d2d_test_env)
    set_tests_properties(test_d2s test_outdoor PROPERTIES ENVIRONMENT "${d2d_test_env}")
    if (TARGET d2d AND Python3_Interpreter_FOUND)
        set_tests_properties(smoke_d2d PROPERTIES ENVIRONMENT "${d2d_test_env}")
    endif()
endif()
