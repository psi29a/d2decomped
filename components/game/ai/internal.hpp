// SPDX-License-Identifier: GPL-3.0-or-later
// What ai.cpp's think and the per-AI inputs (ai/act1.cpp .. act5.cpp)
// share: no part of the game's API. Each act's file sets the ThinkIn
// fields its thinks (rules/thinks/actN.hpp) read beyond the ones every
// think gets, a member function each of a struct built on ThinkPrep, a
// row each in that act's table by MonStats AI name.
#pragma once

#include "ai.hpp"
#include "gamedata.hpp"

#include <monsters.hpp>
#include <montypes.hpp>
#include <rules.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::game {

inline int subtile(float cells) { return int(std::floor(cells * 5)); }
// Diablo's Bone Prison round (0x73d4f0): boneprison1..4 at the target's
// diagonals, (1, 1) first.
inline constexpr std::array<std::pair<int, int>, 4> kPrison{ { { 1, 1 }, { 1, -1 }, { -1, -1 }, { -1, 1 } } };
// FUN_005fd350: a nest's young can come out by (x, y) (ai.cpp).
bool nest_spot_free(const GameData& game_data, const Level& level, const Crowd& crowd, int type, int x, int y, bool flag);
// A Skills.txt row by name, -1 none.
int skill_id(const GameData& game_data, const std::string& name);
// Impregnate's test (FUN_005d2140, ai.cpp).
bool impregnable(const GameData& game_data, const Monster& host);

// A think's state as its inputs are set: the monster, its target (or the
// stand-in when it thinks without one), and the finds its act may be at
// (think reads them back: the kin it walks to, the hurt one or corpse it
// casts on, the imp it mounts, where its young are laid, the wisps it
// dances with, the second target).
struct ThinkPrep {
    const GameData& game_data;
    const Level& level;
    Monster& monster;
    const d2d::rules::MonType& type_info;
    std::span<Foe> foes;
    std::span<Monster> pack;
    const Crowd& crowd;
    Spawning::LevelState* rooms_in_play;
    std::uint32_t now_ms;
    d2d::rules::ThinkIn& input;
    UnitState& unit;
    int x, y;                                  // its subtile
    Foe* target;
    bool targeted;                             // false: the stand-in (search types 2 / 5)
    int target_x, target_y;
    int frame;
    std::size_t self;                          // its index in pack
    bool cried_at;                             // a minion under Cry Help (think: AI command 1)
    Foe*& second;
    int& corpse;
    int& kin;
    int& aid;
    int& imp_found;
    int& lay_x;
    int& lay_y;
    std::vector<int>& wisp_finds;

    // The eligible pack units in FUN_005dd0b0 mode 1's order (FUN_005dce60,
    // FUN_0065ac70): near rooms in order, each room's newest first.
    template <class Eligible>
    std::vector<int> near_order(Eligible eligible) const {
        std::vector<int> rooms;
        for (const auto& [room_level, room] : near_rooms(game_data, level, unit.x, unit.y))
            if (room_level == &level) rooms.push_back(room);
        std::vector<d2d::rules::RoomUnit> units;
        units.reserve(pack.size());
        for (const auto& other : pack) units.push_back({ room_of(level, other.unit.x, other.unit.y), eligible(other) });
        return d2d::rules::room_order(units, rooms);
    }
    template <class Eligible>
    int first_near(Eligible eligible) const {
        const auto order = near_order(eligible);
        return order.empty() ? -1 : order.front();
    }
    [[nodiscard]] bool wall805(int at_x, int at_y) const { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x805); }
    // FUN_005fd470, a skill usable, for the thinks that ask it (not in town,
    // FUN_0061ab00: a think here never is). Resurrect / Resurrect2's
    // TgtPlaceCheck (Skills +8 bit 0): the corpse's spot open to its pattern
    // under 0x3c01 (FUN_0064d910).
    void corpse_place() {
        if (corpse < 0) return;
        const auto& body = pack[std::size_t(corpse)].unit;
        UnitShape place = body.shape;
        place.mask = 0x3c01;
        input.usable = !level.unit_blocked(body.x, body.y, place) && !crowd.at(body.x, body.y, &body);
    }
    // Charge (do 67): a clear line to the target (FUN_00645910, 0xc01).
    void charge_line() {
        const auto wallc01 = [&](int at_x, int at_y) { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0xc01); };
        input.usable = !d2d::rules::sight_blocked(x, y, 0, target_x, target_y, 0, wallc01);
    }
    // Baal's Vile Effigy and his tentacles (FUN_005ef820) live while he
    // does (FUN_00552fd0: their master, FUN_005541b0); he's the one Baal,
    // and the only caster of Festering Appendages (the clone's act 9 swings).
    void baal_rider() {
        input.rider = std::ranges::any_of(pack, [&](const Monster& other) { return other.alive() && game_data.monsters.types[std::size_t(other.type)].ai_name == "BaalCrab"; });
    }
};

// A row of an act's table: the AI's name, its inputs.
using PrepFn = void (*)(const ThinkPrep& prep);
struct PrepRow {
    std::string_view ai_name;
    PrepFn prep;
};

// An act's inputs, member `act_prep` of its struct, as a PrepFn.
template <class Act, void (Act::*act_prep)()>
void prep_as(const ThinkPrep& prep) {
    Act act{ prep };
    (act.*act_prep)();
}

// Each act's rows (ai/actN.cpp).
extern const std::span<const PrepRow> kAct1Preps, kAct2Preps, kAct3Preps, kAct4Preps, kAct5Preps;

}  // namespace d2d::game
