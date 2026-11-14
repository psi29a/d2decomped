// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// How long a player's attack swing is and when it strikes: Fight::set_pmode for an attack mode (FUN_005539b0 files
// the strike and the end of the swing from the start frame FUN_00623b10 returns).
//   `--swing-start`: every class x main-hand weapon x attack mode x IAS x weapon speed (WSM), one line each
//   `D class mode weapon ias wsm rate record frames speed ticks strike`: the swing's length and the tick the strike lands on
//   (counted from the tick the mode is set), as `tools/emu/swing_start.py compare` reads them for game.exe.
//   no argument: the rules (attack_start_frame, frame_tick) and, with the game data, 34 cells (32 of the grid and two unusable weapons) with game.exe's answers
//   (swing_start.py compare printed them): SKIPs that part without D2_MPQ_DIR and the 1.14d patch installer.

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
#include <vector>

using namespace d2d::game;

namespace {

// game.exe's answer (tools/emu/swing_start.py compare, a tick count from the tick the mode is set), the cell as test_swing_start --swing-start
// prints it; the comment: the record, its start frame, and what d2d gave before the start frame and the closed form went in.
struct Frozen { int cls, mode; const char* weapon; int ias, wsm, ticks, strike; };
constexpr Frozen kFrozen[] = {
    { 1, kModeA1, "sst", 0, 0, 16, 9 },   // SOA1STF start 2, d2d before: 18 11
    { 1, kModeA1, "", 0, 0, 15, 8 },   // SOA1HTH start 1, d2d before: 16 9
    { 1, kModeA1, "ssd", 0, 0, 18, 10 },   // SOA11HS start 2, d2d before: 20 12
    { 1, kModeA1, "ssd", 40, 10, 16, 9 },   // SOA11HS start 2, d2d before: 17 11
    { 1, kModeA1, "sbw", 0, 0, 17, 9 },   // SOA1BOW start 0, d2d before: 17 9
    { 1, kModeA1, "lxb", 20, 0, 18, 10 },   // SOA1XBW start 0, d2d before: 18 10
    { 0, kModeA1, "ssd", 0, 0, 14, 8 },   // AMA11HS start 2, d2d before: 16 10
    { 0, kModeA1, "spr", 0, 0, 16, 9 },   // AMA12HT start 2, d2d before: 18 11
    { 0, kModeA1, "spr", 30, -10, 12, 7 },   // AMA12HT start 2, d2d before: 14 9
    { 0, kModeA1, "", 0, 0, 12, 7 },   // AMA1HTH start 1, d2d before: 13 8
    { 0, kModeA1, "sbw", 0, 0, 14, 6 },   // AMA1BOW start 0, d2d before: 14 6
    { 0, kModeA1, "gsd", 100, 20, 14, 8 },   // AMA12HS start 2, d2d before: 15 9
    { 3, kModeA1, "ssd", 0, 0, 15, 7 },   // PAA11HS start 0, d2d before: 15 7
    { 3, kModeA1, "ssd", 20, 0, 13, 6 },   // PAA11HS start 0, d2d before: 13 7
    { 3, kModeA1, "ssd", 40, 0, 12, 6 },   // PAA11HS start 0, d2d before: 12 6
    { 3, kModeA1, "ssd", 60, 0, 11, 6 },   // PAA11HS start 0, d2d before: 11 6
    { 3, kModeA1, "gsd", 60, 10, 14, 7 },   // PAA12HS start 0, d2d before: 14 7
    { 3, kModeS1, "ssd", 0, 0, 12, 7 },   // PAS11HS start 0, d2d before: 12 7
    { 3, kModeS1, "ssd", 20, 10, 12, 7 },   // PAS11HS start 0, d2d before: 12 7
    { 3, kModeKK, "", 0, 0, 12, 5 },   // PAKKHTH start 0, d2d before: 12 5
    { 3, kModeKK, "", 40, -10, 9, 4 },   // PAKKHTH start 0, d2d before: 9 4
    { 4, kModeA1, "ssd", 0, 0, 16, 7 },   // BAA11HS start 0, d2d before: 16 7
    { 4, kModeA1, "gsd", 80, 0, 13, 6 },   // BAA12HS start 0, d2d before: 13 6
    { 5, kModeS1, "", 0, 0, 15, 8 },   // DZS1HTH start 0, d2d before: 15 8
    { 5, kModeKK, "", 20, 0, 11, 5 },   // DZKKHTH start 0, d2d before: 11 5
    { 2, kModeA1, "ssd", 0, 0, 19, 9 },   // NEA11HS start 0, d2d before: 19 9
    { 2, kModeA1, "dgr", 150, -20, 11, 6 },   // NEA11HT start 0, d2d before: 11 6
    { 2, kModeA1, "", 0, 10, 17, 9 },   // NEA1HTH start 0, d2d before: 17 10
    { 3, kModeA1, "ssd", 100, 30, 13, 6 },   // PAA11HS start 0, d2d before: 13 7
    { 5, kModeS1, "sbw", 200, 10, 12, 6 },   // DZS1BOW start 0, d2d before: 12 7
    { 6, kModeS1, "dgr", 200, 30, 11, 7 },   // AIS11HT start 0, d2d before: 11 8
    { 6, kModeKK, "sbw", 150, 30, 10, 3 },   // AIKKBOW start 0, d2d before: 10 4
};

struct Cell {
    int ticks = 0, strike = 0;   // the swing's length and the strike's tick, from the tick the mode is set
    int rate = 0;
    std::string record;
    unsigned frames = 0, speed = 0;
    bool record_found = false;
};

// A character of class `cls` wearing `weapon` (a weapons.txt code, "" = barehanded) in the main hand, no shield; `strong`: stats that meet any requirement.
struct Hero {
    Character character;
    explicit Hero(const GameData& data, int cls, const std::string& weapon, bool strong = true) {
        d2d::rules::Rng rng(7);
        auto made = new_character(data, cls, "Headless", false, true, rng);
        character.character_class = cls;
        character.name = "Headless";
        character.header = made.header;
        character.stats = made.stats;
        if (strong)   // every weapon of the grid is usable: the swing is timed by the weapon in hand, which game.exe takes only if usable
            for (const int stat : { d2d::d2s::kStr, d2d::d2s::kDex, d2d::d2s::kLevel }) character.stats.values[std::size_t(stat)] = 255;
        character.items = made.items;
        const d2d::d2s::Item* proto = character.items.empty() ? nullptr : &character.items.front();
        d2d::d2s::Item put = proto ? *proto : d2d::d2s::Item{};
        std::erase_if(character.items, [](const auto& item) { return item.location == 1 && (item.slot == 4 || item.slot == 5); });   // main hand and shield out
        if (!weapon.empty()) { put.code = weapon; put.location = 1; put.slot = 4; character.items.push_back(put); }
        character.panel = panel_stats(data, character.header, character.items, character.stats);
    }
};

}   // namespace

int main(int argc, char** argv) try {
    const bool grid = argc > 1 && std::strcmp(argv[1], "--swing-start") == 0;
    if (!grid) {
        // The start frame (FUN_00623b10: table 0x6e8e60 by weapon class and player class) and the tick a frame is reached on.
        using d2d::rules::attack_start_frame;
        assert(attack_start_frame(d2d::d2s::kSorceress, "stf") == 2 && attack_start_frame(d2d::d2s::kAmazon, "2ht") == 2 && attack_start_frame(d2d::d2s::kAmazon, "1ht") == 2);
        assert(attack_start_frame(d2d::d2s::kAmazon, "") == 1 && attack_start_frame(d2d::d2s::kSorceress, "") == 1);
        assert(attack_start_frame(d2d::d2s::kAmazon, "bow") == 0 && attack_start_frame(d2d::d2s::kSorceress, "xbw") == 0 && attack_start_frame(d2d::d2s::kAmazon, "ht1") == 0);
        for (int cls = d2d::d2s::kNecromancer; cls <= d2d::d2s::kAssassin; ++cls)
            for (const char* wclass : { "", "bow", "1hs", "1ht", "stf", "2hs", "2ht", "xbw", "ht1" }) assert(attack_start_frame(cls, wclass) == 0);
        assert(attack_start_frame(d2d::d2s::kAmazon, "zzz") == 1 && attack_start_frame(d2d::d2s::kSorceress, "zzz") == 1 && attack_start_frame(d2d::d2s::kPaladin, "zzz") == 0);   // a key the map lacks: row 0
        assert(attack_start_frame(-1, "1hs") == 0 && attack_start_frame(7, "1hs") == 0);
        assert(d2d::rules::frame_tick(16, 0, 256) == 16 && d2d::rules::frame_tick(16, 2, 256) == 14 && d2d::rules::frame_tick(9, 2, 204) == 9);   // ceil(256 x 7 / 204)
        assert(d2d::rules::frame_tick(1, 2, 256) == 1 && d2d::rules::frame_tick(2, 2, 256) == 1);                                                  // never before the next tick
        assert(d2d::rules::attack_ticks(16, 256, 0, 0) == 16 && d2d::rules::attack_ticks(16, 256, 0, 0, 0, 2) == 14);
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir) { std::printf("SKIP: D2_MPQ_DIR\n"); return D2D_TEST_SKIPPED; }
    const fs::path data_dir = mpq_dir;
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->patched);

    // none, sword (1hs), dagger (1ht), great sword (2hs), spear (2ht), staff (stf), bow, crossbow (xbw), claws (ht1)
    const std::vector<std::string> weapons = { "", "ssd", "dgr", "gsd", "spr", "sst", "sbw", "lxb", "ktr" };
    const std::array<int, 5> modes = { kModeA1, kModeS1, kModeS3, kModeS4, kModeKK };   // the attack modes d2d plays (attack_mode)
    const std::array<int, 10> ias_list = { 0, 10, 20, 30, 40, 60, 80, 100, 150, 200 };
    const std::array<int, 7> wsm_list = { -30, -20, -10, 0, 10, 20, 30 };

    const auto one = [&](World& world, int mode, int ias, int wsm, std::uint32_t& now) {
        auto& actor = *world.actor;
        Cell cell;
        now += 1000;
        actor.player_combat.ias = ias; actor.player_combat.wsm = wsm; actor.player_combat.sias = 0;
        const auto& anim = world.fight.player_anim(mode);
        if (!anim.frames) return cell;
        world.fight.set_pmode(mode, now);
        cell.record_found = true;
        cell.record = anim.name;
        cell.frames = anim.frames;
        cell.speed = anim.speed ? anim.speed : 256;
        cell.rate = d2d::rules::attack_speed(int(cell.speed), ias, wsm, 0);
        cell.ticks = int((actor.pmode_until - now) / 40);
        cell.strike = int((actor.action_ms + 39) / 40);
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
                world.fight.update_fighters(1000 * kTickMs);   // the fighter reads the weapon in hand (its start frame)
                std::uint32_t now = 1000 * kTickMs;
                for (const int mode : modes)
                    for (const int ias : ias_list)
                        for (const int wsm : wsm_list) {
                            const auto cell = one(world, mode, ias, wsm, now);
                            if (!cell.record_found) continue;
                            std::printf("D %d %d %s %d %d %d %s %u %u %d %d\n", cls, mode, weapon.empty() ? "none" : weapon.c_str(), ias, wsm, cell.rate,
                                        cell.record.c_str(), cell.frames, cell.speed, cell.ticks, cell.strike);
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
        const auto cell = one(world, frozen.mode, frozen.ias, frozen.wsm, now);
        if (!cell.record_found || cell.ticks != frozen.ticks || cell.strike != frozen.strike)
            std::printf("FAIL: class %d mode %d weapon '%s' ias %d wsm %d: swing %d strike %d, game.exe's %d and %d\n", frozen.cls, frozen.mode, frozen.weapon, frozen.ias, frozen.wsm,
                        cell.ticks, cell.strike, frozen.ticks, frozen.strike);
        assert(cell.record_found && cell.ticks == frozen.ticks && cell.strike == frozen.strike);
        ++checked;
    }
    // A broken weapon (item flag 0x100) or one short of its requirements (flag 0x4000, set by settle_requirements) is not the weapon in hand
    // (FUN_0063c9b0 -> FUN_0062a4e0): the swing starts as with nothing in hand (row 0: frame 1), on the record the worn weapon picks.
    // game.exe's answers with the class key forced to none (swing_start.py, SOA11HS and SOA12HS, rate 256).
    struct Unusable { int cls; const char* weapon; bool strong; int ticks, strike; };
    for (const Unusable& unusable : { Unusable{ d2d::d2s::kSorceress, "ssd", true, 19, 11 }, Unusable{ d2d::d2s::kSorceress, "gsd", false, 23, 13 } }) {
        Hero hero(*data, unusable.cls, unusable.weapon, unusable.strong);
        for (auto& item : hero.character.items)
            if (unusable.strong && item.location == d2d::d2s::item_location::kEquipped && item.slot == d2d::d2s::body_location::kRightArm) item.flags |= 0x100;   // broken
        World world(&*data, -1, -1);
        world.enter(hero.character);
        world.actor->character.appearance = data->look_of(hero.character.items);
        world.fight.update_fighters(1000 * kTickMs);
        std::uint32_t now = 1000 * kTickMs;
        const auto cell = one(world, kModeA1, 0, 0, now);
        if (cell.ticks != unusable.ticks || cell.strike != unusable.strike)
            std::printf("FAIL: unusable '%s': swing %d strike %d, game.exe's %d and %d\n", unusable.weapon, cell.ticks, cell.strike, unusable.ticks, unusable.strike);
        assert(cell.record_found && cell.ticks == unusable.ticks && cell.strike == unusable.strike);
        ++checked;
    }
    std::printf("test_swing_start: %d cells match game.exe\n", checked);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "test_swing_start: %s\n", e.what());
    return 1;
}
