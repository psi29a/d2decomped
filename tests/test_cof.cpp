// SPDX-License-Identifier: GPL-3.0-or-later
// Parse two real COF files (already used as byte-length canary in test_mpq).
#include <cof.hpp>
#include <mpq.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    const char* env = std::getenv("D2_MPQ_DIR");
    const fs::path dir = env ? fs::path(env)
        : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "")
            / "Workspace/private/diablo2";
    const auto d2char = dir / "d2char.mpq";
    if (!fs::exists(d2char)) {
        std::printf("SKIP: %s not found\n", d2char.string().c_str());
        return 0;
    }

    d2d::mpq::Archive archive(d2char);

    // Necromancer walk-left animation, hand-to-hand weapon class.
    {
        const auto raw = archive.read(R"(data\global\CHARS\NE\COF\NEWL1HT.COF)");
        d2d::cof::Cof cof(raw);
        std::printf("NEWL1HT: layers=%u frames=%u dirs=%u speed=%u\n",
                    unsigned(cof.layers()), unsigned(cof.frames_per_direction()),
                    unsigned(cof.directions()), unsigned(cof.speed()));
        assert(cof.layers() > 0);
        assert(cof.frames_per_direction() > 0);
        assert(cof.directions() > 0);
        assert(cof.layer_defs().size() == cof.layers());
        assert(cof.events().size() == cof.frames_per_direction());

        // Every layer defines a weapon class (usually "hth" for walk).
        for (const auto& layer : cof.layer_defs()) {
            assert(!layer.weapon_class.empty());
            assert(layer.weapon_class.size() <= 3);
        }

        // Priority lookup for (dir 0, frame 0) returns `layers` bytes.
        const auto pri = cof.priority(0, 0);
        assert(pri.size() == cof.layers());
    }

    // The priority rows go round the compass, DCC directions don't: a
    // Barbarian in town holding a hand axe (RH) draws it behind his torso
    // facing north (DCC 6) and in front of it facing the viewer (DCC 4).
    {
        const auto raw = archive.read(R"(data\global\CHARS\BA\COF\BATN1HS.COF)");
        d2d::cof::Cof cof(raw);
        assert(cof.directions() == 16);
        auto before = [&](std::size_t direction, std::uint8_t first, std::uint8_t second) {
            const auto row = cof.priority(d2d::cof::Cof::priority_row(direction, cof.directions()), 0);
            return std::ranges::find(row, first) < std::ranges::find(row, second);
        };
        constexpr std::uint8_t kTorso = 1, kRightHand = 5;
        assert(before(6, kRightHand, kTorso));
        assert(before(4, kTorso, kRightHand));
        std::array<bool, 16> seen{};
        for (std::size_t direction = 0; direction < 16; ++direction) seen[d2d::cof::Cof::priority_row(direction, 16)] = true;
        assert(std::ranges::all_of(seen, [](bool hit) { return hit; }));
    }

    // Barbarian throw with crossbow — different weapon class.
    {
        const auto raw = archive.read(R"(data\global\CHARS\BA\COF\BATNXBW.COF)");
        d2d::cof::Cof cof(raw);
        assert(cof.directions() == 8 || cof.directions() == 16);
        assert(cof.layers() > 0);
    }

    std::printf("OK\n");
    return 0;
}
