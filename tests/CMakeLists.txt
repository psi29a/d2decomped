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
