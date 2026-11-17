// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// How long a player's attack sequence is (Jab, Impale, Double Swing, Double Throw, Frenzy, Fists of Fire, Claws of Thunder,
// Blades of Ice, Dragon Claw): Fight::start_sequence ticks the skill's steps at the attack rate. Double Throw is in the grid, but in play it
// goes through cast_missile (mode SC) and never reaches start_sequence: the grid does not cover that path (ponytail note in cast_missile).
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

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace d2d::game;

namespace {

// game.exe's answers (tools/emu/seq_rate.py compare printed the same ticks): class, skill, weapon code, IAS, WSM, skill speed, steps, AnimData speed,
// the sequence's ticks; the comment: what d2d gave before the sequence took 30 points off its rate.
struct Frozen { int cls; const char* skill; const char* weapon; int ias, wsm, sias, steps, speed, ticks; };
constexpr Frozen kFrozen[] = {
    { 6, "Blades of Ice", "ktr", 80, 30, 40, 12, 208, 12 },   // parent 10
    { 6, "Blades of Ice", "", 80, 10, 0, 12, 256, 12 },   // parent 9
    { 6, "Blades of Ice", "", 200, 30, 40, 12, 256, 8 },   // parent 7
    { 6, "Blades of Ice", "ktr", 200, -30, 40, 12, 208, 9 },   // parent 9
    { 6, "Claws of Thunder", "", 0, 10, 20, 12, 256, 16 },   // parent 11
    { 6, "Claws of Thunder", "ktr", 20, 10, 20, 12, 208, 16 },   // parent 12
    { 6, "Claws of Thunder", "", 80, 0, -50, 12, 256, 18 },   // parent 13
    { 6, "Claws of Thunder", "", 100, -30, 40, 12, 256, 7 },   // parent 7
    { 4, "Double Swing", "ssd", 80, 30, 40, 17, 256, 14 },   // parent 11
    { 4, "Double Swing", "ssd", 20, -10, 40, 17, 256, 13 },   // parent 11
    { 4, "Double Swing", "spr", 10, -10, -50, 17, 256, 44 },   // parent 25
    { 4, "Double Swing", "lxb", 150, 0, 40, 17, 256, 10 },   // parent 10
    { 4, "Double Throw", "dgr", 80, 30, 20, 12, 256, 12 },   // parent 9
    { 4, "Double Throw", "spr", 20, -30, 40, 12, 256, 8 },   // parent 7
    { 4, "Double Throw", "sbw", 0, 0, -50, 12, 256, 61 },   // parent 24
    { 4, "Double Throw", "ssd", 200, -30, 20, 12, 256, 7 },   // parent 7
    { 6, "Dragon Claw", "ktr", 60, 0, 40, 12, 208, 10 },   // parent 9
    { 6, "Dragon Claw", "", 80, 0, -50, 12, 256, 18 },   // parent 13
    { 6, "Dragon Claw", "", 0, -10, 20, 12, 256, 12 },   // parent 10
    { 6, "Dragon Claw", "", 200, -10, 40, 12, 256, 7 },   // parent 7
    { 6, "Fists of Fire", "ktr", 10, -10, 0, 12, 208, 17 },   // parent 13
    { 6, "Fists of Fire", "", 100, 0, 0, 12, 256, 10 },   // parent 8
    { 6, "Fists of Fire", "", 40, 10, -50, 12, 256, 31 },   // parent 18
    { 6, "Fists of Fire", "ktr", 150, -30, 0, 12, 208, 9 },   // parent 9
    { 4, "Frenzy", "ssd", 100, -10, 20, 17, 256, 12 },   // parent 10
    { 4, "Frenzy", "", 100, 10, 0, 17, 256, 15 },   // parent 12
    { 4, "Frenzy", "ssd", 60, -30, 0, 17, 256, 13 },   // parent 11
    { 4, "Frenzy", "lxb", 80, -30, 40, 17, 256, 10 },   // parent 10
    { 0, "Impale", "", 80, 10, 40, 13, 256, 9 },   // parent 8
    { 0, "Impale", "", 100, -30, -50, 13, 256, 13 },   // parent 10
    { 0, "Impale", "", 0, -30, -50, 13, 256, 26 },   // parent 17
    { 0, "Impale", "", 30, -30, 40, 13, 256, 8 },   // parent 8
    { 0, "Jab", "dgr", 10, 30, 40, 18, 256, 21 },   // parent 16
    { 0, "Jab", "dgr", 20, -10, -50, 18, 256, 39 },   // parent 24
    { 0, "Jab", "", 200, 10, -50, 13, 256, 16 },   // parent 12
    { 0, "Jab", "", 200, -10, 40, 13, 256, 8 },   // parent 8
};

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
    if (!grid) {
        // The 30 points (FUN_00623f50 0x624290..0x62429b) for a UseAttackRate sequence that is not a cast (seqtrans SC: FUN_006216e0), and what they do to a length.
        using d2d::rules::sequence_sias;
        assert(sequence_sias(true, "A1") == -30 && sequence_sias(true, "SQ") == -30 && sequence_sias(true, "SC") == 0 && sequence_sias(false, "A1") == 0);
        assert(d2d::rules::attack_ticks(13, 256, 0, 0, sequence_sias(true, "A1")) == 19 && d2d::rules::attack_ticks(13, 256, 0, 0) == 13);   // Jab with bare hands
        assert(d2d::rules::attack_ticks(17, 256, 0, 0, sequence_sias(true, "A1")) == 25 && d2d::rules::attack_ticks(17, 256, 0, 0) == 17);   // Frenzy
        assert(d2d::rules::attack_speed(256, 0, 0, sequence_sias(true, "A1")) == 179 && d2d::rules::attack_speed(256, 0, 0, -85) == 38);        // the floor of 15 percent
    }
    // The checks above need no data and passed: without the data set only the data-backed part is skipped, and the run still passes (tests/README.md);
    // `--seq-rate` has nothing to print without it, so it reports the skip.
    const auto skip = [&](const char* why) { std::printf("SKIP: %s\n", why); return grid ? D2D_TEST_SKIPPED : 0; };
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir) return skip("D2_MPQ_DIR");
    const fs::path data_dir = mpq_dir;
    // A file of the data folder by name in any case, as the loader looks for it.
    const auto has_file = [&](std::string_view wanted) {
        return fs::is_directory(data_dir) && std::ranges::any_of(fs::directory_iterator(data_dir), [&](const fs::directory_entry& entry) {
            std::string name = entry.path().filename().string();
            std::ranges::transform(name, name.begin(), [](unsigned char letter) { return char(std::tolower(letter)); });
            return name == wanted;
        });
    };
    // Missing data, or no 1.14d patch (the loader fails on CD-era tables) is a skip; a data set that is there and does not load is a failure.
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!has_file("d2data.mpq")) return skip("no d2data.mpq");
    if (patch && *patch && !fs::exists(patch)) {   // a configured patch that is not there is a broken fixture
        std::fprintf(stderr, "test_seq_rate: D2_PATCH_INSTALLER %s does not exist\n", patch);
        return 1;
    }
    if (!(patch && *patch) && !has_file("patch_d2.mpq") && !has_file("lodpatch_114d.exe")) return skip("no 1.14d patch data");
    auto data = load_game_data(data_dir, patch && *patch ? fs::path(patch) : fs::path{}, 0x1234);
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
    int checked = 0;
    for (const auto& frozen : kFrozen) {
        Hero hero(*data, frozen.cls, frozen.weapon);
        World world(&*data, -1, -1);
        world.enter(hero.character);
        world.actor->character.appearance = data->look_of(hero.character.items);
        world.fight.update_fighters(1000 * kTickMs);
        std::uint32_t now = 1000 * kTickMs;
        const auto found = data->skills.by_name.find(frozen.skill);
        assert(found != data->skills.by_name.end());
        const auto cell = one(world, int(found->second), frozen.ias, frozen.wsm, frozen.sias, now);
        if (!cell.found || cell.steps != frozen.steps || cell.speed != frozen.speed || cell.ticks != frozen.ticks)
            std::printf("FAIL: class %d %s '%s' ias %d wsm %d sias %d: %d steps at %d lasts %d ticks, game.exe's %d at %d lasts %d\n", frozen.cls, frozen.skill, frozen.weapon, frozen.ias,
                        frozen.wsm, frozen.sias, cell.steps, cell.speed, cell.ticks, frozen.steps, frozen.speed, frozen.ticks);
        assert(cell.found && cell.steps == frozen.steps && cell.speed == frozen.speed && cell.ticks == frozen.ticks);
        ++checked;
    }
    std::printf("test_seq_rate: %d cells match game.exe\n", checked);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_seq_rate: %s\n", error.what());
    return 1;
}
