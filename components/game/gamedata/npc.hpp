// SPDX-License-Identifier: GPL-3.0-or-later
// A unit's collision (UnitShape) and a level's unit as placed (Npc): what a
// monster, an NPC and an object are made from.
#pragma once

#include <rules.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace d2d::game {

// A unit's collision (FUN_006484e0): its pattern from MonStats2 SizeX
// (DAT_006eb3dc 0, 1, 1, 2; over 3: 1), 1 → 3 and 2 → 4 for a MonStats
// npc or inTown that isn't interact (the merc, the cow), or a player's pet
// (unit +0xc4 bit 31, FUN_0056d8d0; FUN_0063e860). Stamped (FUN_0064ea90):
// 0 nothing, 1 / 3 its footprint bit (0x1000, 3 / 4 0x2000) on its
// subtile, 2 / 4 on the plus. Tested (FUN_0064d910): 0 its subtile, 1 / 3
// the plus, 2 / 4 the 3x3 box. `mask` (path +0x50) is what blocks it: a player 0x1c09, a
// monster 0x3c01, opendoors 0x3401, flying 0x1804. Players, by default.
struct UnitShape {
    std::uint8_t pattern = 1, size = 2;
    std::uint16_t mask = 0x1c09;
    [[nodiscard]] std::uint16_t bit() const { return pattern >= 3 ? 0x2000 : 0x1000; }
    [[nodiscard]] bool box() const { return pattern == 2 || pattern == 4; }
    [[nodiscard]] bool stamps() const { return pattern >= 1 && pattern <= 4; }
    // A pet's: 1 → 3, 2 → 4 (FUN_00648580 with bit 31 set).
    [[nodiscard]] UnitShape owned() const {
        UnitShape out = *this;
        if (pattern == 1 || pattern == 2) out.pattern = std::uint8_t(pattern + 2);
        return out;
    }
    static UnitShape monster(int size, bool town_unit, bool flying, bool open_doors) {
        const int pattern = size == 0 ? 0 : size == 3 ? 2 : 1;
        const UnitShape shape{ std::uint8_t(pattern), std::uint8_t(size), std::uint16_t(flying ? 0x1804 : open_doors ? 0x3401 : 0x3c01) };
        return town_unit ? shape.owned() : shape;
    }
};

// The town AIs (MonAI 32 Npc FUN_005e7130, 41 Towner FUN_005e7540, 42
// Vendor FUN_005e9e00; town-npcs.md), 81 JarJar, 58 Navi (Flavie).
enum class TownAi { none, npc, towner, vendor, jarjar, navi };

// Things the DS1 places (its object list): NPCs (type 1, from
// data\global\monsters) and objects (type 2 — torches, fires, the
// waypoint..., from data\global\objects). Both are composites.
struct Npc {
    std::string root;                    // "monsters" or "objects"
    std::string code;                    // <root>\<code>\ ...
    std::string mode;                    // animation mode token (NU, ON...)
    std::string base_w;                  // weapon class ("hth" for objects)
    bool composite_death = false;        // MonStats2 compositeDeath: DT and DD keep base_w (cof_weapon_class)
    std::array<std::string, 16> comp;    // per layer, "" = not present
    float x = 0, y = 0;
    int size_x = 0, size_y = 0;          // collision footprint, subtiles
    std::string name;                    // hover label; "" = not selectable
    std::string title;                   // a monster's MonStats NameStr text, selectable or not
    std::vector<std::pair<float, float>> path;   // DS1 patrol points, cells
    std::vector<int> actions;            // each path point's DS1 action (town_npcs.hpp)
    float velocity = 3;                  // MonStats Velocity
    TownAi town_ai = TownAi::none;       // MonStats AI Npc / Towner / Vendor: thinks with rules::npc_think / vendor_think
    std::uint32_t modes = 0;             // MonStats2 mWL / mS1 / mS2 as bits 2 / 8 / 9 (FUN_0046c140)
    int operate_fn = 0;                  // objects.txt OperateFn (32: the town stash)
    std::optional<int> name_offset;      // objects.txt NameOffset: its hover name's height over its feet (FUN_00454f30)
    int automap = 0;                     // objects.txt AutoMap (+0x1bc) / MonStats2 automapCel (+0x118): its automap icon's cel, 0 none (FUN_00458dc0)
    bool interact = false;               // MonStats interact (flags bit 9): a white automap mark (FUN_00459bc0)
    UnitShape shape;                     // an NPC's collision (MonStats / MonStats2)
    int object_id = 0;                   // an object's objects.txt Id
    bool door = false, monster_ok = false;   // objects.txt IsDoor (+0x13a), MonsterOK (+0x16d): monsters open it (FUN_005b0f50)
    // What an object stamps into the collision grid while its mode has
    // collision (FUN_006209d0): a door 0x806 with BlocksVis (+0x13b), else
    // 0x804 with BlockMissile (+0x1b6), else 0x400; another object 0x8000
    // with SubClass (+0x167) & 4, else 0x400 (| 4 with BlockMissile).
    std::uint16_t stamp = 0x400;
    std::uint8_t collision = 0;          // an object's HasCollision0..7, a bit a mode (NU OP ON S1..S5)
    std::uint8_t selectable = 0;         // its Selectable0..7, the same way
    std::uint8_t cycle = 0xff;           // its CycleAnim0..7: loops in that mode, else holds the last frame
    std::array<std::uint8_t, 8> frames{};      // its FrameCnt0..7: a mode's length (FUN_004bcbb0), not its COF's
    std::array<std::uint8_t, 8> loop_from{};   // its Start0..7: the frame a mode starts on and a loop goes back to
    // Its FrameDelta0..7: 256 a frame a tick (unit +0x4c, FUN_00624390);
    // without Sync (+0x175) it varies a little: object_frame_delta.
    std::array<std::uint16_t, 8> frame_delta{};
    bool sync = false;
    int hc_idx = -1;                     // MonStats hcIdx (NPC menu table key)
    std::string id;                      // MonStats Id (npc.txt key)
    int quest = 0;                       // shown once this Act 1 quest is done (Cain: 4), 0 = always
    // Lut Gholein's palace NPCs, shown by rules::palace_spots: 1 Jerhyn by
    // object 121, 2 / 3 Jerhyn in the palace (object 122's x - 10 / x + 15),
    // 4 the gate guard; 0 = none.
    int palace = 0;
    int op_frames = 0;                   // objects.txt FrameCnt1 when it has an OP mode (Mode1)
    bool mode2 = false;                  // objects.txt Mode2: event 1 can transition an operated object to ON
    int shrine = 0;                      // a shrine's Shrines.txt row (roll_shrine)
    int trap = 0;                        // a chest's trap type (roll_chest), 0 none
    bool locked = false;                 // a locked chest: takes a key
    bool sparkle = false;                // a sparkling chest (InitFn 57: unit +0x78 & 1)
    bool env_effect = false;             // objects.txt EnvEffect (+0x139): NU by day, OP at dusk, night and dawn (the client's FUN_004bc5e0)
    int erupts = -1;                     // a fissure's first eruption (event 8), frames from its init (InitFn 58); -1 not one
    // An object's unit seed (+0x20, FUN_00555230; a chest's InitFn re-seeds
    // it, roll_chest): what its rounds (FUN_00585b90) roll on. Its room
    // (Level::rooms index, -1 none): a stand rolls on the room's seed.
    mutable d2d::rules::Rng seed;
    // The client unit's independent animation seed (seed_client_units): its
    // room's client seed stepped once, {low, 666}; {1, 666} with no room.
    // One made by hand (tests) keeps its spot as the seed.
    mutable d2d::rules::Rng client_seed;
    mutable int client_mode = -1;
    mutable std::uint16_t client_frame_delta = 0;
    mutable bool client_seeded = false;
    int room = -1;
    bool group = false;                  // made by an object group (ObjectRooms::groups), after its room's presets
    bool preoperated = false;            // a PreOperate object that starts opened (ON)
    int light = 0;                       // a monster's light radius, subtiles (MonStats2 Light)
    int trans_lvl = 0;                   // MonStats TransLvl: its palshift.dat colour (Fallen 0, Carver 1 ...)
    bool no_unique_shift = false;        // MonStats2 noUniqueShift: a unique keeps its type's colour
    bool dead_col = false;               // MonStats2 deadCol (flag 19): its corpse keeps collision, never shatters
    int height = 0;                      // MonStats2 Height (+0xb): its shattered death's ice (rules::shatter_rows)
    std::array<int, 3> utrans{};         // MonStats2 Utrans by difficulty (0: none)
    int colour = 0;                      // the colour it's drawn in (FUN_00466360; the client's Scene::monster_map), 0/1 none
    int overlay_class = 0;               // which Overlay.txt Height it takes: MonStats2 OverlayHeight - 1 (FUN_006223a0)
    int hit_class = 0;                   // MonStats2 HitClass (+0x14, FUN_00623c20): the swing a weaponsnd / stsounddelay skill start times
    std::array<std::uint8_t, 8> lit{};   // an object's light radius in each mode NU OP ON S1..S5 (objects.txt Lit0..7)
};

}  // namespace d2d::game
