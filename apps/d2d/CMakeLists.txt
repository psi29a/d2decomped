# d2d — the game binary skeleton. Wires the dev control channel + screenshot
# pipeline so both are testable end-to-end before phase 5's real renderer.
add_executable(d2d main.cpp)
target_link_libraries(d2d PRIVATE mpq dc6 devctl screenshot)
