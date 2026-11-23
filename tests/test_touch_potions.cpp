// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// Bounded touch HUD potion aggregation (apps/d2d/input/touch/potions.hpp).
#include "input/touch/potions.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>

using d2d::client::touch::PotionEntry;
using d2d::client::touch::PotionPlace;
using d2d::client::touch::summarize_touch_potions;

int main() try {
    {
        const std::array entries{
            PotionEntry{ "hp1", PotionPlace::Belt, 0 },
            PotionEntry{ "hp5", PotionPlace::Belt, 4 }, // deep stock doesn't change the consumable at slot 0
            PotionEntry{ "mp3", PotionPlace::Belt, 1 },
            PotionEntry{ "mp5", PotionPlace::Belt, 5 },
            PotionEntry{ "hp4", PotionPlace::Inventory, 0 },
            PotionEntry{ "rvl", PotionPlace::Belt, 2 },
            PotionEntry{ "rvs", PotionPlace::Belt, 3 },
        };
        const auto summary = summarize_touch_potions(entries);
        assert(summary.healing == 2 && summary.mana == 2);
        assert(summary.strongest_healing_slot == 0 && summary.strongest_mana_slot == 1);
    }
    {
        const std::array entries{
            PotionEntry{ "hp4", PotionPlace::Belt, 2 },
            PotionEntry{ "mp4", PotionPlace::Belt, 1 },
            PotionEntry{ "hp5", PotionPlace::Belt, 6 }, // deeper row is stock, not currently consumed
            PotionEntry{ "mp5", PotionPlace::Belt, 5 }, // below mp4; cannot be consumed first
            PotionEntry{ "hp5", PotionPlace::Inventory, 0 },
        };
        const auto summary = summarize_touch_potions(entries);
        assert(summary.healing == 2 && summary.mana == 2);
        assert(summary.strongest_healing_slot == 2 && summary.strongest_mana_slot == 1);
    }
    {
        const std::array entries{
            PotionEntry{ "hp5", PotionPlace::Inventory, 0 },
            PotionEntry{ "rvl", PotionPlace::Belt, 0 },
            PotionEntry{ "rvs", PotionPlace::Belt, 4 },
            PotionEntry{ "hp1", PotionPlace::Belt, 16 }, // invalid box
        };
        const auto summary = summarize_touch_potions(entries);
        assert(summary.healing == 0 && summary.mana == 0 && !summary.strongest_healing_slot && !summary.strongest_mana_slot);
    }
    {
        const std::array entries{
            PotionEntry{ "hp5", PotionPlace::Belt, 3 },
            PotionEntry{ "hp2", PotionPlace::Belt, 0 },
            PotionEntry{ "hp5", PotionPlace::Belt, 2 },
            PotionEntry{ "mp4", PotionPlace::Belt, 1 },
        };
        const auto summary = summarize_touch_potions(entries);
        assert(summary.healing == 3 && summary.mana == 1);
        assert(summary.strongest_healing_slot == 2 && summary.strongest_mana_slot == 1);
    }
    {
        const std::array<PotionEntry, 0> entries{};
        const auto summary = summarize_touch_potions(entries);
        assert(summary.healing == 0 && summary.mana == 0 && !summary.strongest_healing_slot && !summary.strongest_mana_slot);
    }
    {
        const std::array entries{
            PotionEntry{ "rvl", PotionPlace::Belt, 0 }, // non-healing first match blocks hp5 in the same box
            PotionEntry{ "hp5", PotionPlace::Belt, 0 },
            PotionEntry{ "mp3", PotionPlace::Belt, 1 },
            PotionEntry{ "hp4", PotionPlace::Belt, 4 }, // deeper row does not occupy slot 0's drinkable box
        };
        const auto summary = summarize_touch_potions(entries);
        assert(summary.healing == 1 && summary.mana == 1);
        assert(!summary.strongest_healing_slot && summary.strongest_mana_slot == 1);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_touch_potions: %s\n", error.what());
    return 1;
}
