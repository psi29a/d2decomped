// SPDX-License-Identifier: GPL-3.0-or-later
// What ai.cpp's think and each AI's own parts (ai/act1.cpp .. act5.cpp)
// share: no part of the game's API. Each act's file has a row per AI, as
// game.exe's MonAI table (0x73ca18 + AI x 16: target search, init, think)
// has: its own target pick, a think the game runs itself, the ThinkIn
// fields its rules think (rules/thinks/actN.hpp) reads beyond the ones
// every think gets, and how its skill acts start and aim. Each part is a
// member function of a struct built on ThinkCtx, found by MonStats AI name.
#pragma once

#include "ai.hpp"
#include "gamedata.hpp"

#include <monsters.hpp>
#include <montypes.hpp>
#include <rules.hpp>

#include <algorithm>
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

// A think as each AI's parts see it: a view over ai.cpp think's state (the
// references are its locals, so a part's copy changes them). The monster,
// its target (or the stand-in when it thinks without one), the act its
// rules think chose, and the finds its act may be at (the kin it walks to,
// the hurt one or corpse it casts on, the imp it mounts, where its young
// are laid, the wisps it dances with, the second target).
struct ThinkCtx {
    const GameData& game_data;
    const Level& level;
    Monster& monster;
    const d2d::rules::MonType& type_info;
    const d2d::rules::MonType::Diff& per_difficulty;
    std::span<Foe> foes;
    std::span<Monster> pack;
    const Crowd& crowd;
    Spawning::LevelState* rooms_in_play;
    std::uint32_t now_ms;
    d2d::rules::Rng& rng;                      // the game's (a swing's rolls), not the unit's seed
    bool has_run;                              // an RN mode (else RN falls back to WL)
    int frame;
    std::size_t self;                          // its index in pack
    UnitState& unit;
    d2d::rules::ThinkIn& input;
    const int& x;                              // its subtile
    const int& y;
    Foe*& target;
    int& best;                                 // the search's distance to it
    const bool& targeted;                      // false: the stand-in (search types 2 / 5)
    const int& target_x;
    const int& target_y;
    const float& dx;                           // to the target, cells
    const float& dy;
    const d2d::rules::Think& act;
    Foe*& second;
    int& corpse;
    int& kin;
    int& aid;
    int& imp_found;
    int& lay_x;
    int& lay_y;
    std::vector<int>& wisp_finds;

    // The acts (ai.cpp): stand (FUN_005de080), set off at a foe or to
    // unit.goal, walk to a spot subtiles off, a mode against the target.
    void idle(int frames);
    bool set_off(bool running, const Foe* foe, bool to_unit = false);
    void walk_to(int off_x, int off_y, bool running);
    void use(std::string_view mode, int skill);

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
    [[nodiscard]] bool wallc01(int at_x, int at_y) const { return level.blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0xc01); }

    // The parts several AIs share.
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
        input.usable = !d2d::rules::sight_blocked(x, y, 0, target_x, target_y, 0, [this](int at_x, int at_y) { return wallc01(at_x, at_y); });
    }
    // A nest's init (the crow nests, gargoyle traps, sarcophagi), taken at its first think: the frame.
    void nest() {
        if (monster.ai_state == 0) monster.ai_state = frame;
    }
    // Baal's Vile Effigy and his tentacles (FUN_005ef820) live while he
    // does (FUN_00552fd0: their master, FUN_005541b0); he's the one Baal,
    // and the only caster of Festering Appendages (the clone's act 9 swings).
    void baal_rider() {
        input.rider = std::ranges::any_of(pack, [&](const Monster& other) { return other.alive() && game_data.monsters.types[std::size_t(other.type)].ai_name == "BaalCrab"; });
    }
    // A skill at a point on the map its think chose (the priests' teleport
    // point, the Council's corner, Baal's teleport 25 off, away from the
    // target, the imps' and Nihlathak's off themselves, FUN_005df850).
    void aim_map_point() {
        if (act.x || act.y) { monster.skill_x = act.x; monster.skill_y = act.y; }
    }
    // A skill at a spot on the ground, no unit: the hell meteor's and the
    // turret's (do 28 / 105 read it, FUN_0056d2c0), its own spot when none.
    void aim_ground() {
        if (act.unit != d2d::rules::ThinkUnit::none) return;
        monster.skill_x = act.x || act.y ? act.x : x; monster.skill_y = act.x || act.y ? act.y : y; monster.at_point = true;
    }
    // A skill at its find (the imp's mount, the defiler's host), faced.
    void aim_kin() {
        if (act.unit != d2d::rules::ThinkUnit::kin) return;
        monster.skill_unit = kin;
        if (kin >= 0) unit.dir = direction16(pack[std::size_t(kin)].unit.x - unit.x, pack[std::size_t(kin)].unit.y - unit.y);
    }
};

// A row of an act's table: the AI's name and its parts, each run where its
// think reaches it (ai.cpp think): `search` after the target search; `think`
// in place of the rules think, before the search's miss is handled;
// `think_targeted` the same with a target, after the leash (each says
// whether it acted: always); `prep` the inputs; `skill` as a skill act
// starts (true: it's done); `aim` as it ends. Search types 2 and 5 think without a target: alone (no miss), and
// after the miss.
using PartFn = void (*)(const ThinkCtx& ctx);
using CheckFn = bool (*)(const ThinkCtx& ctx);
struct AiRow {
    std::string_view ai_name;
    bool thinks_alone = false;
    bool thinks_after_miss = false;
    PartFn search = nullptr;
    CheckFn think = nullptr;
    CheckFn think_targeted = nullptr;
    PartFn prep = nullptr;
    CheckFn skill = nullptr;
    PartFn aim = nullptr;
};

// An act's part, member `part` of its struct (ThinkCtx for a shared one), as a PartFn or CheckFn.
template <class Act, auto part>
auto part_as(const ThinkCtx& ctx) {
    Act act{ ctx };
    return (act.*part)();
}

// Each act's rows (ai/actN.cpp).
extern const std::span<const AiRow> kAct1Ais, kAct2Ais, kAct3Ais, kAct4Ais, kAct5Ais;

}  // namespace d2d::game
