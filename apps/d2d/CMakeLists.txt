# d2d — the game binary. SDL3 windowing + streaming framebuffer texture;
# devctl + screenshot still wired to the same in-memory buffer.
find_package(SDL3 CONFIG REQUIRED)

add_executable(d2d main.cpp)
target_link_libraries(d2d PRIVATE
    mpq d2s userdir cof dc6 dcc ds1 dt1 font palette tbl devctl screenshot
    SDL3::SDL3 CLI11::CLI11)
