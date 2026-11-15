// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// How long a player's attack sequence is (Jab, Impale, Double Swing, Double Throw, Frenzy, Fists of Fire, Claws of Thunder,
// Blades of Ice, Dragon Claw): Fight::start_sequence ticks the skill's steps at the attack rate.
//   `--seq-rate`: every class x sequence skill x main-hand weapon x IAS x weapon speed (WSM) x skill speed, one line each
//   `D class skill_id skill weapon ias wsm sias steps speed ticks`: the steps the sequence has for that weapon, the AnimData A1 speed it
//   is counted at and the ticks it lasts (-1: the skill moves the unit and the hold is open ended), as
//   `tools/emu/seq_rate.py compare` reads them for game.exe.
//   no argument: the rule (rules::attack_ticks) and, with the game data, the frozen cells game.exe's answers were printed for
//   (seq_rate.py compare printed them): SKIPs that part without D2_MPQ_DIR and the 1.14d patch installer.

#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace d2d::game;

namespace {

struct Cell {
    int steps = 0, speed = 0, ticks = 0;
    bool found = false;
};

// A character of class `cls` wearing `weapon` (a weapons.txt code, "" = barehanded) in the main hand, no shield; stats that meet any requirement.
struct Hero {
    Character character;
    Hero(const GameData& data, int cls, const std::string& weapon) {
        d2d::rules::Rng rng(7);
        auto made = new_character(data, cls, "Headless", false, true, rng);
        character.character_class = cls;
        character.name = "Headless";
        character.header = made.header;
        character.stats = made.stats;
        for (const int stat : { d2d::d2s::kStr, d2d::d2s::kDex, d2d::d2s::kLevel }) character.stats.values[std::size_t(stat)] = 255;
        character.items = made.items;
        const d2d::d2s::Item* proto = character.items.empty() ? nullptr : &character.items.front();
        d2d::d2s::Item put = proto ? *proto : d2d::d2s::Item{};
        std::erase_if(character.items, [](const auto& item) { return item.location == 1 && (item.slot == 4 || item.slot == 5); });   // main hand and shield out
        if (!weapon.empty()) { put.code = weapon; put.location = 1; put.slot = 4; character.items.push_back(std::move(put)); }
        character.panel = panel_stats(data, character.header, character.items, character.stats);
    }
};

}   // namespace

int main(int argc, char** argv) try {
    const bool grid = argc > 1 && std::strcmp(argv[1], "--seq-rate") == 0;
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir) { std::printf("SKIP: D2_MPQ_DIR\n"); return D2D_TEST_SKIPPED; }
    const fs::path data_dir = mpq_dir;
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->patched);

    // none, sword (1hs), dagger (1ht), great sword (2hs), spear (2ht), staff (stf), bow, crossbow (xbw), claws (ht1)
    const std::vector<std::string> weapons = { "", "ssd", "dgr", "gsd", "spr", "sst", "sbw", "lxb", "ktr" };
    // every skill whose anim is SQ and that has steps: the Amazon's, Barbarian's and Assassin's sequences, Lightning's pair (a cast), the
    // moving ones (Whirlwind, Charge, Leap Attack) and the Sorceress's Inferno and Arctic Blast (the oracle names which of them it excludes).
    // Two more player SQ records are not in the grid: Leap (132) and Dragon Flight (275); neither has UseAttackRate, so the term is 0 for them.
    // The grid wears ONE main-hand weapon: dual wield (Frenzy, Double Swing, Double Throw in play) is not compared; game.exe's two-hand average
    // sits before the -30 (0x624290) and is not modelled by the oracle.
    const std::vector<std::string> skills = { "Jab", "Impale", "Double Swing", "Double Throw", "Frenzy", "Fists of Fire", "Claws of Thunder", "Blades of Ice",
                                              "Dragon Claw", "Lightning", "Chain Lightning", "Whirlwind", "Charge", "Leap Attack", "Inferno", "Arctic Blast" };
    const std::array<int, 10> ias_list = { 0, 10, 20, 30, 40, 60, 80, 100, 150, 200 };
    const std::array<int, 5> wsm_list = { -30, -10, 0, 10, 30 };
    const std::array<int, 4> sias_list = { 0, -50, 20, 40 };   // a chill is -50

    const auto one = [&](World& world, int skill_id, int ias, int wsm, int sias, std::uint32_t& now) {
        auto& actor = *world.actor;
        Cell cell;
        now += 1000;
        actor.player_combat.ias = ias; actor.player_combat.wsm = wsm; actor.player_combat.sias = sias;
        actor.swing_skill = skill_id;
        const auto& anim = world.fight.player_anim(kModeA1);
        if (!anim.frames) return cell;
        world.fight.set_pmode(kModeA1, now);
        world.fight.start_sequence(now);
        cell.steps = int(actor.seq.size());
        if (cell.steps == 0) return cell;
        cell.found = true;
        cell.speed = int(anim.speed ? anim.speed : 256);
        cell.ticks = actor.pmode_until == ~0u ? -1 : int((actor.pmode_until - now) / 40);
        return cell;
    };

    if (grid) {
        for (int cls = 0; cls < 7; ++cls)
            for (const auto& weapon : weapons) {
                if (weapon == "ktr" && cls != d2d::d2s::kAssassin) continue;   // claws are the Assassin's: no other class can wear them
                Hero hero(*data, cls, weapon);
                World world(&*data, -1, -1);
                world.enter(hero.character);
                world.actor->character.appearance = data->look_of(hero.character.items);
                world.fight.update_fighters(1000 * kTickMs);
                std::uint32_t now = 1000 * kTickMs;
                for (const auto& name : skills) {
                    const auto found = data->skills.by_name.find(name);
                    assert(found != data->skills.by_name.end());
                    const auto* row = data->skills.get(found->second);
                    assert(row);
                    if (row->cls != d2d::rules::kClassCode[std::size_t(cls)]) continue;   // a skill is played by its own class only
                    std::string tag = name;
                    for (auto& letter : tag) if (letter == ' ') letter = '_';
                    for (const int ias : ias_list)
                        for (const int wsm : wsm_list)
                            for (const int sias : sias_list) {
                                const auto cell = one(world, int(found->second), ias, wsm, sias, now);
                                if (!cell.found) continue;
                                std::printf("D %d %d %s %s %d %d %d %d %d %d\n", cls, int(found->second), tag.c_str(), weapon.empty() ? "none" : weapon.c_str(), ias, wsm, sias, cell.steps, cell.speed, cell.ticks);
                            }
                }
            }
        return 0;
    }
    std::printf("test_seq_rate: no cells to check yet\n");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "test_seq_rate: %s\n", e.what());
    return 1;
}
