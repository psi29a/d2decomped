// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: net
// net::pick_own_address, the TCP/IP screen's address (FUN_0040df60, tcpip-menu.md):
// a host name that maps to loopback (Android's "localhost", Debian's 127.0.1.1)
// gives way to the UDP route's address (deviations.md improvement 22).
#include <tcp.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

namespace {
constexpr auto ipv4(std::uint32_t first, std::uint32_t second, std::uint32_t third, std::uint32_t fourth) -> std::uint32_t {
    return first << 24 | second << 16 | third << 8 | fourth;
}
} // namespace

int main() try {
    using d2d::net::pick_own_address;
    const auto lan = ipv4(192, 168, 1, 20);
    const auto wifi = ipv4(192, 168, 1, 37);
    const auto loopback = ipv4(127, 0, 0, 1);
    const auto debian = ipv4(127, 0, 1, 1);

    assert(pick_own_address(lan, 0) == lan);          // Windows: the name's LAN address, as game.exe
    assert(pick_own_address(lan, wifi) == lan);       // the name wins when it isn't loopback
    assert(pick_own_address(0, wifi) == wifi);        // no name address: the route, as game.exe
    assert(pick_own_address(0, 0) == 0);              // nothing: "0.0.0.0", as game.exe
    assert(pick_own_address(loopback, wifi) == wifi); // Android on Wi-Fi: the route, not loopback
    assert(pick_own_address(debian, wifi) == wifi);   // Debian's 127.0.1.1 too
    assert(pick_own_address(loopback, 0) == loopback);// no network: loopback, HOST and JOIN dark
    assert(pick_own_address(debian, 0) == loopback);  // shown as 127.0.0.1, which the screen checks for

    std::printf("OK: a loopback host name gives way to the route's address\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_own_address: %s\n", error.what());
    return 1;
}
