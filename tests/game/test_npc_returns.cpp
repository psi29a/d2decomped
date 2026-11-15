// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The town NPCs' return line (rules::NpcReturns): a town marked in the player's
// congratulations (FUN_00544fa0) is named at the next act change (S->C 0x91,
// FUN_00545100), and its NPC greets with its return line (kind 2) at its next
// menu, which clears its bit (C->S 0x4d, FUN_005724c0). Already greeted this
// stay in town (+0x15 clear), the name is taken but nothing plays and the bit
// stays (FUN_004b66b0, 0x4b68f3..0x4b690e). A shared crossing sets each player's act.
// Needs the data; SKIPs without it.

#include <character.hpp>
#include <character_store.hpp>
#include <cues.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <protocol.hpp>
#include <rules.hpp>
#include <sound_ids.hpp>
#include <town_npcs.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <vector>

using namespace d2d::game;
namespace fs = std::filesystem;
using d2d::rules::NpcReturns;

int main() try {
    // The table alone: 0x732738's bits, the towns' lists, Act 4 none, 521 (Tyrael in Harrogath) bit 0.
    std::uint64_t bits = 0;
    NpcReturns::mark(bits, 0);
    assert(bits == 0x7e && NpcReturns::due(bits, 0) == (std::vector<int>{ 148, 154, 150, 147, 155, 265 }));
    assert(NpcReturns::due(bits, 1).empty() && NpcReturns::town(3).empty() && NpcReturns::bit_of(521) == 0);
    NpcReturns::heard(bits, 147);
    assert(bits == 0x7c);

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (D2_MPQ_DIR)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && !data->town.walk.empty());
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng(7);
    auto made = new_character(*data, 1, "Headless", false, true, rng);
    Character character;
    character.character_class = 1;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    auto* actor = world.actor;
    auto& town = data->town;
    town.walk.assign(std::size_t(town.ds1.width() * 5) * std::size_t(town.ds1.height() * 5), std::uint16_t(0));

    constexpr int kGheed = 147;
    const auto gheed = std::ranges::find(town.npcs, kGheed, &Npc::hc_idx);
    assert(gheed != town.npcs.end());
    const int npc = int(gheed - town.npcs.begin());
    const auto* greeting = d2d::rules::sound_ids::greeting_of(kGheed);
    assert(greeting && greeting->back > 0);
    std::uint32_t now = 1000 * kTickMs;
    // Talk to him from beside his start (he may have strolled); the sounds his menu cued.
    const auto talk = [&] {
        actor->player.x = gheed->x + 1; actor->player.y = gheed->y;
        world.cues.due.clear();
        world.apply(cmd::Interact{ npc }, now);
        for (int tick = 0; tick < 200 && actor->interact_npc >= 0; ++tick) { world.tick({}, now + kTickMs, now); now += kTickMs; }
        assert(actor->interact_npc == -1);
        std::vector<int> sounds;
        for (const auto& cue : world.cues.due) if (cue.unit == unit_key(kSoundOnMonster, -1 - npc)) sounds.push_back(cue.sound);
        return sounds;
    };
    const auto is_return = [&](int sound) { return sound >= greeting->back && sound < greeting->back + std::max(data->sounds[std::size_t(greeting->back)].group, 1); };

    NpcReturns::mark(world.congrats(), 0);                           // Warriv's first trip
    world.owe_returns(town);                                         // back in the camp: 0x91 names its six
    assert(actor->returning.size() == 6);
    auto heard = talk();
    assert(heard.size() == 1 && is_return(heard[0]));
    assert(!std::ranges::contains(actor->returning, kGheed) && (world.congrats() >> NpcReturns::bit_of(kGheed) & 1) == 0);
    heard = talk();                                                  // greeted this stay: nothing
    assert(heard.empty());

    NpcReturns::mark(world.congrats(), 0);
    world.owe_returns(town);
    heard = talk();                                                  // named, but +0x15 is clear: no line, the bit stays
    assert(heard.empty() && !std::ranges::contains(actor->returning, kGheed) && (world.congrats() >> NpcReturns::bit_of(kGheed) & 1) == 1);

    // A shared crossing: the party goes with the mover (bring_party), so the partner's save takes
    // the new act too, and Lut Gholein's names come to it as to the mover.
    auto partner = character;
    partner.name = "Partner";
    auto& second = world.join(partner);
    NpcReturns::mark(second.character.header.congrats[0], 1);
    world.act_as(*world.players.front());
    const Level* lut_gholein = data->level(d2d::rules::level_ids::kLutGholein);
    assert(lut_gholein && lut_gholein->start.first >= 0 && world.actor != &second);
    world.arrive(lut_gholein, lut_gholein->start.first, lut_gholein->start.second, "a test");
    assert((second.character.header.difficulty[0] & 7) == 1 && second.returning.size() == 11);

    std::printf("test_npc_returns: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_npc_returns: %s\n", error.what());
    return 1;
}
