// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A party member's merc gets hit at its own FHR: merc_gear reads any
// player's merc items (stat 99, item_fastergethitrate), not only the acting
// player's, so World::view can give each party merc its get-hit rate.
// No game data: the gear is typed in.

#include <cues.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <loot.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

int main() try {
    GameData game_data;
    const Level* level = nullptr;
    d2d::rules::Rng rng{ 1 };
    Cues cues;
    Loot loot{ &game_data, level, rng, cues };
    Fight fight{ &game_data, level, rng, loot, cues };
    Fight::Hero acting, partner;
    fight.act_as(acting);

    d2d::d2s::Item armor;
    armor.location = d2d::d2s::item_location::kEquipped;
    armor.slot = d2d::d2s::body_location::kTorso;
    armor.props.push_back({ .stat = d2d::d2s::kFasterGetHitRate, .value = 24 });
    partner.character.merc_items.push_back(std::move(armor));

    constexpr auto kFhr = std::size_t(d2d::d2s::kFasterGetHitRate);
    assert(fight.merc_gear().sum[kFhr] == 0);                 // the acting player's merc wears nothing
    assert(fight.merc_gear(partner).sum[kFhr] == 24);         // the partner's does
    assert(d2d::rules::get_hit_rate(256, 24) > d2d::rules::get_hit_rate(256, 0));

    std::puts("OK: a party merc's FHR is its own gear's");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_party_merc_fhr: %s\n", error.what());
    return 1;
}
