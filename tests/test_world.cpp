// The World links on its own: components/world needs nothing from the
// client (apps/d2d: SDL, sound, sprites, fonts). A standalone server would
// link just this library.
#include "server.hpp"

#include <cstdio>

using namespace d2d::app;

int main() {
    // Taking these pulls world.cpp and gamedata.cpp into the link.
    volatile auto tick = &World::tick;
    volatile auto view = &World::view;
    volatile auto nearby = &want_nearby;
    volatile auto spawns = &level_spawns;
    (void)tick; (void)view; (void)nearby; (void)spawns;
    std::printf("OK\n");
    return 0;
}
