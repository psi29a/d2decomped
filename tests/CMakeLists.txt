# All tests. Each `test_*.cpp` becomes its own executable + ctest entry.
# ponytail: no framework, no fixtures. asserts + int main.

function(d2d_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ${ARGN})
    # Tests use plain asserts; NDEBUG would strip them and make the test a no-op
    # under Release / RelWithDebInfo. Keep asserts unconditionally on.
    target_compile_options(${name} PRIVATE -UNDEBUG)
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
d2d_add_test(test_d2s d2s)
d2d_add_test(test_userdir userdir)
d2d_add_test(test_compcode mpq compcode)

# End-to-end: headless d2d driven over devctl (SDL dummy driver).
add_test(NAME smoke_d2d
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/smoke_d2d.py $<TARGET_FILE:d2d>)
