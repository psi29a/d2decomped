// The game server's side (docs/design/multiplayer.md): the world a game
// runs — levels, the player's unit, the merc, NPCs, monsters, missiles,
// ground items, objects, the rolls — stepped by commands from the client
// (protocol.hpp). What the client needs to hear back goes out as Events.
// ponytail: one player; the character (cc) is shared with the client
// until the CharacterStore, and item moves, stat / skill points and the
// store still edit it client-side; the rng is shared too.
#pragma once

#include "ai.hpp"
#include "character.hpp"
#include "character_store.hpp"
#include "cues.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "inventory.hpp"
#include "loot.hpp"
#include "protocol.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <light.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <sequences.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::game {

// What the server tells the client (the S -> C side, network.md).
namespace ev {
// The player's level changed (`from`): a crossing or warp (`keep_map`:
// the automap stays open) or a respawn.
struct LevelChanged { const Level* from = nullptr; bool keep_map = false; };
// Arrived at an object or NPC that opens a panel (game.exe's 0x58 "open
// UI" and the NPC interaction).
// A talk carries the NPC's quest messages for the player (quests.hpp).
struct OpenUI { enum Kind { stash, waypoint, talk, trade, hire } kind = stash; int npc = -1; std::vector<d2d::rules::QuestMsg> quest; };
}  // namespace ev
using Event = std::variant<ev::LevelChanged, ev::OpenUI>;

// What a client is told after each tick (the S -> C side, network.md):
// everything it draws and clicks on. In-process these are copies; a TCP
// transport sends them as bytes.
// ponytail: monsters go whole (the fields a client needs aren't picked yet);
// the character (cc), the store and the item in hand are still shared with
// the World; levels are pointers into the shared GameData (the same on every
// machine, from the map seed).
struct View {
    const Level* level = nullptr;
    UnitState player;
    bool running = false, dead = false;
    int pmode = -1;                        // Fight::pmode: A1, GH, BL, DT, DD ... (-1 none)
    float prate = 1.f;
    std::vector<d2d::rules::SeqFrame> seq;   // an SQ skill's frames while it plays
    std::uint32_t seq_frame_ms = 40;
    bool seq_loop = false;
    GameData::Appearance gfx{};               // what the character wears
    struct Merc { UnitState unit; const Npc* npc = nullptr; std::string_view mode; };
    std::optional<Merc> merc;
    struct Pet { Npc npc; UnitState unit; std::string_view mode; };
    std::vector<Pet> pets;                 // the player's summons on this level
    std::vector<Monster> monsters;         // the level's
    struct Shot { const GameData::MissileInfo* info = nullptr; float x = 0, y = 0; int dir = 0; std::uint32_t born = 0; };
    std::vector<Shot> missiles;
    int attack = -1, attack_skill = 0;     // the monster the player's attacking (unit id), with what
    std::vector<Loot::GroundItem> ground;  // the level's floor
    struct Fire { float x, y; const Npc* npc; };
    std::vector<Fire> fires;
    // The player's town portal where they stand (which 0: where it was cast,
    // 1: its twin in town), leading to level `to`.
    struct Portal { float x = 0, y = 0; int destination = 0; std::uint32_t born = 0; int which = 0; };
    std::vector<Portal> portals;
    // The player's corpses where they stand: their look when they fell.
    struct Corpse { float x = 0, y = 0; int dir = 0; GameData::Appearance gfx{}; int which = 0; };
    std::vector<Corpse> corpses;
    // The level's NPCs as they patrol, then each Level::nearby level's in
    // that order (its npcs.size() each, its own cells).
    std::vector<UnitState> npc_states;
    std::vector<std::pair<int, int>> boost;   // the shrine boost's stats while it lasts
    int aura = 0;                          // the aura that's on
    int gold_lost = 0;                     // goldlost (175) of the last death: the death screen's line
    std::vector<int> buffs;                // skills whose state is on the player (their aurastate: Frozen Armor, Shout ...)
    d2d::rules::Day day;                   // the time of day (lighting, day/night sounds)
    bool den_cleared = false;              // the Den of Evil cleared in this game (its quest state, S→C 0x02)
    int light_bonus = 0;                   // item_lightradius (stat 89) from what's worn: the player's light grows by it
    int den_state = 1, den_log = 0, den_left = 0;   // the Den quest's record: state, log state, monsters left (the quest log)
    // The player's own character (what only its owner is told): header,
    // stats, items with their unit ids, the item in hand; the open store's
    // stock and the hire list.
    bool has_character = false;
    d2d::d2s::Header header;
    d2d::d2s::Stats stats;
    std::vector<d2d::d2s::Item> items;
    std::optional<d2d::d2s::Item> held;
    std::optional<Store> store;
    std::vector<d2d::rules::MercOffer> hire_offers;
    std::vector<Cues::Cue> sounds;         // the world's sounds cued since the last View
    std::vector<Event> events;             // what the World said since the last View
    // A monster by unit id: its index in `monsters`, or -1.
    [[nodiscard]] int monster(int id) const {
        const auto found = std::ranges::find(monsters, id, &Monster::id);
        return id < 0 || found == monsters.end() ? -1 : int(found - monsters.begin());
    }
};

// The game's step: game.exe's frame, 1000 / 25 ms (FUN_0052fc20,
// docs/research/re/network.md).
constexpr std::uint32_t kTickMs = 40;

struct World {
    const GameData* game_data = nullptr;
    Character character;                       // the character: the World's own (the client's is a copy of the View's)
    const Level* level = nullptr;          // where the player is: the town, the Blood Moor, the Den of Evil
    UnitState player;                      // DS1 cells (x.5 = a cell centre)
    std::optional<UnitState> merc;          // the save's mercenary, following
    const Npc* merc_npc = nullptr;
    std::vector<UnitState> npc_states;     // the level's NPCs as they patrol
    // Other levels' NPCs, kept while the player's away; the neighbours'
    // keep patrolling (game.exe runs every unit in the rooms in play).
    std::unordered_map<const Level*, std::vector<UnitState>> other_npcs;
    d2d::rules::Rng rng{ 0x2545f491u };    // rolls (the client's stock, talk topics, gambles, merc offers too)
    Cues  cues{ game_data };                   // world sounds due later
    Loot  loot{ game_data, level, character, player, rng, cues };                       // on the ground (loot.hpp)
    Fight fight{ game_data, level, character, player, merc, merc_npc, rng, loot, cues };  // the fight (fight.hpp)
    float target_x = 0, target_y = 0;      // where the player is walking to
    bool  running = false;                 // run / walk (game.exe's 0x53 / 0x54)
    int   take_warp = -1;                  // the warp of `level` the player is walking to
    int   interact_npc = -1;               // the object / NPC being walked to
    int   pick_item = -1;                  // the ground item being walked to (its unit id)
    std::vector<int> not_there;            // levels walked toward that aren't built (logged once)
    std::map<std::pair<const Level*, int>, std::uint32_t> operated;   // shrines / chests used: when
    struct Fire { const Level* level; const Npc* npc; float x, y; };
    std::vector<Fire> fires;               // chest traps 5 / 7 left these burning
    // The player's town portal: [0] where it was cast, [1] its twin in town
    // (FUN_0056d130 / FUN_0056cf40); a new one closes the old pair.
    struct Portal { const Level* level = nullptr; float x = 0, y = 0; std::uint32_t born = 0; };
    std::array<Portal, 2> portal{};
    int take_portal = -1;                  // the portal (0 / 1) the player is walking to
    // The player's corpses (FUN_0057f700): where they fell, what they wore
    // and had in hand, 75% of the experience the death took; at most 16.
    struct Corpse { const Level* level = nullptr; float x = 0, y = 0; int dir = 0; std::vector<d2d::d2s::Item> items;
                    std::int64_t exp = 0; GameData::Appearance gfx{}; };
    std::vector<Corpse> corpses;
    int take_corpse = -1;                  // the corpse the player is walking to
    int last_pmode = -1;                   // the player's mode at the last tick (death's stages)
    std::int64_t exp_lost = 0;             // what the last death took
    std::uint32_t now = 0;                 // the tick's time
    std::uint32_t arrived_at = 0;          // when the player last changed level (pcdata+0x160)
    std::array<int, 3> talking{ -1, -1, -1 };   // NPCs the client has a menu, speech or store open with (they stand)
    std::vector<Event> events;             // for the client, since it last looked
    const CharacterStore* characters = nullptr;   // where the character is saved
    std::optional<d2d::d2s::Item> held;    // the item in the player's hand (the cursor)
    Store store;                           // the NPC window open (npc < 0: none): its stock
    std::vector<d2d::rules::MercOffer> hire_offers;   // Kashya's list while it's open
    int next_item_id = 1;                  // the next item's unit id
    d2d::rules::DenQuest den;              // this game's Den of Evil
    d2d::rules::AndyQuest andy;            // and Sisters to the Slaughter
    int den_left = -1;                     // its monsters alive when last counted
    std::uint32_t den_log_at = 0;          // when the log moves to "Return to Akara", 0 none
    // The quest flags of the difficulty played.
    d2d::rules::QuestBits& quests();
    // Every item of the character has a unit id (new ones get theirs).
    void item_ids();

    // At --start-cam-x/y, else the town start (Level::start), else the
    // map's middle; then the nearest free spot so we never start inside a
    // tent.
    World(const GameData* loaded_data, int start_x, int start_y) : game_data(loaded_data), level(loaded_data ? &loaded_data->town : nullptr) {
        const bool have_world = level && !level->dt1s.empty();
        player.x = (start_x >= 0 ? float(start_x) : have_world ? float(level->ds1.width() / 2) : 0.f) + 0.5f;
        player.y = (start_y >= 0 ? float(start_y) : have_world ? float(level->ds1.height() / 2) : 0.f) + 0.5f;
        if (have_world && start_x < 0 && start_y < 0 && level->start.first >= 0)
            std::tie(player.x, player.y) = level->start;
        if (have_world) std::tie(player.x, player.y) = level->nearest_free(player.x, player.y);
        if (game_data) npc_states = npc_start(*level);
        if (game_data) fight.new_game(0);
        target_x = player.x; target_y = player.y;
        player.dir = 4;                    // south, facing the viewer
        // The character's skill levels for the fight, the skill shrine's
        // +all skills (item_allskills) while its boost lasts.
        fight.skill_base = [this](int id) { return skill_base_level(*game_data, character, id); };
        fight.skill_level = [this](int id) {
            std::vector<d2d::d2s::ItemProp> extra;
            if (now < fight.boost.until)
                for (const auto& [stat, value] : fight.boost.stats) if (stat == 127) extra.push_back({ .stat = 127, .value = value });
            return skill_level(*game_data, character, id, extra);
        };
    }
    World(const World&) = delete;

    // What the client is told (View).
    // A monster's colour as the client picks it (FUN_00466360): its
    // TransLvl + 2 (8 and up: 2); a unique's (and superunique's) roll from
    // its seed, rand(30) + 9, unless noUniqueShift; MonStats2 Utrans for the
    // difficulty; a superunique's own Utrans; 30 and up fall back to 2.
    // ponytail: the roll is on the unit id, not the unit's seed (the same
    // odds, not the same colour as game.exe's for a given monster); Utrans
    // 0xff's pick (FUN_004791b0) is taken as 1.
    std::int64_t gold_lost = 0;            // goldlost (175), as the last death set it
    [[nodiscard]] int monster_colour(const Monster& monster) const;
    [[nodiscard]] View view() const;

    // The character to the CharacterStore: the save's header with what the
    // game changed (level, when last played, the gear's look), its stats and
    // items. "" when it's written, else why not.
    std::string save();

    // Operating a shrine (FUN_00583c70: its Shrines.txt effect) or a chest
    // (FUN_00585f60 / FUN_00585b90: it opens, its act's chest treasure class
    // drops at the area level).
    // ponytail: magic shrines (16..22) other than gem and warping only
    // log; D2's operate range is 2 cells here.
    void operate(int npc_index, std::uint32_t now_ms, int force = -1);

    // A chest's trap (the table at 0x732cec, docs/research/re/objects.md
    // "Trap monsters"). The trap monster acts once and is gone, so its
    // shot goes straight from the chest at the player: missile level 1 / 4
    // / 8 by difficulty; a row with a Skill (chainlightning) carries that
    // skill's damage, the others their own columns. 5 / 7 leave two fires
    // (no damage traced); 8 raises the level's undead.
    // ponytail: the AI's range check (aip1) is skipped — the player opening
    // the chest is always close; chainlightning doesn't hop; trapfirebolt's
    // fireexplode isn't spawned.
    void spring_trap(int trap, float x, float y, int alvl, std::uint32_t now_ms);

    // A fresh game for the character: the Blood Moor's monsters at its
    // difficulty, no loot about.
    // A player enters with their character (a save loaded, or made): the
    // World takes its copy, the merc comes along, a fresh game; the Act 1
    // quest-gated NPCs (Cain) are there once their quest is done.
    void enter(const Character& entering);

    void new_game();

    // The character's merc (cc.header) next to the player, if alive.
    void spawn_merc();

    // Back in camp after dying: at the town start with full life. Monsters
    // stay as they are.
    // ponytail: D2 leaves a corpse holding the gear and takes gold; not yet.
    void respawn(std::uint32_t now_ms);
    // The player came to `level` from `from`: `from`'s NPC states are kept,
    // `level`'s taken back (made at their start the first time).
    void swap_npcs(const Level* from);

    // Leaving the level: past its edge, collision and drawing already
    // use the level next to it in the act (Level::nearby), so the player
    // walks straight on; once they stand outside this map they belong to
    // that level — everything moves by the offset between the two. A
    // click toward a level the layout placed but d2d doesn't build yet
    // (Cold Plains) is logged.
    void cross_level();
    // The Den's monsters after a death (a1q1.cpp FUN_00590260): the last
    // five are counted down in the quest log; the last one clears it, and a
    // player who's earned the reward says so (event 0x23: the class's
    // act1_complete_den, LAB_005900e0).
    // ponytail: counted when the number drops (game.exe: on each death);
    // the quest log isn't drawn, so the count goes to the log.
    void den_count(std::uint32_t now_ms);
    [[nodiscard]] static const char* level_name(const Level& level);

    // Taking a warp (a cave mouth): a click by one walks there; close to
    // it, the player goes to the level it leads to and stands at that
    // level's warp back, at its ExitWalk. Everything with the player
    // (merc, pets) comes along; the automap and monsters are the new level's.
    // ponytail: "close" is 2 cells of the warp's cell, not LvlWarp's
    // Select box; arriving puts the player on the first warp back.
    void use_warp();

    // Into level `to` near (ax, ay): everything with the player (merc, pets)
    // comes along; the automap and monsters are the new level's.
    void arrive(const Level* destination, float arrive_x, float arrive_y, const char* how);
    std::vector<d2d::rules::QuestMsg> quest_talk(int hc_idx);   // every quest's messages from that NPC
    void andariel_died(const Fight::Kill& kill, std::uint32_t now_ms);
    void set_waypoint(int index) { if (index >= 0 && index < 40) character.header.waypoints[std::size_t(character.header.active_difficulty())][std::size_t(index >> 3)] |= std::uint8_t(1 << (index & 7)); }

    // Dying (FUN_00580ec0 → FUN_00535ab0), killed by a monster:
    // - experience (FUN_005359f0): DifficultyLevels DeathExpPenalty % of the
    //   level's span (0 / 5 / 10), never below the level's start + 1;
    // - gold (FUN_005357d0): min(level, 20) % of carried and stashed gold,
    //   in single player at most what leaves level x 500, and only from the
    //   purse; the rest of the purse falls where the player dies
    //   (FUN_00535510), the purse is emptied, goldlost (175) is set.
    // ponytail: the pile isn't split into FUN_0055a090's piles; goldlost
    // isn't kept.
    void death_penalty(std::uint32_t now_ms);
    // The death played out (mode 0x11; FUN_0057fca0 → FUN_0057f700): a
    // corpse where the player lies with what they wore and held, and 75% of
    // the experience lost. They go on without it.
    void make_corpse();
    // Taking one's corpse (FUN_0057fb70): its experience back, each item to
    // its slot when that's free (FUN_00562f30; its requirements met), else to
    // the inventory; what doesn't fit stays on it. It goes once it's empty.
    // ponytail: requirements aren't checked; two-handed / quiver pairing
    // (FUN_0055f2d0) isn't.
    void take_corpse_items(std::size_t corpse_index, std::uint32_t now_ms);

    // Reading a Scroll of Town Portal or a Tome's charge (C->S 0x20 on the
    // item): Skills.txt 219 / 220 cast (srvdofunc 113), not in town (checkfunc
    // 5); the scroll's used up, the tome's quantity goes down.
    void read_portal(std::vector<d2d::d2s::Item>::iterator scroll, std::uint32_t now_ms);
    // Town Portal's action frame: a portal by the player and its twin at the
    // town's portal spot (FUN_0056d130 → FUN_0056cf40, spawn index 11),
    // each at the nearest free spot; the old pair goes.
    // ponytail: "by the player" is the nearest free spot 0.6 cells south;
    // game.exe searches from the caster with collision 0x3e01, size 3.
    void open_portal(std::uint32_t now_ms);
    void open_portal_at(float portal_x, float portal_y, std::uint32_t now_ms);
    // Walking into one: out by the other (OperateFn 15, FUN_00584870).
    void use_portal(std::uint32_t now_ms);

    // NPC deals (protocol.hpp): the windows, buying, selling, repairing,
    // identifying, hiring. True when `c` was one.
    bool deal(const Command& command);

    // A command from the player, checked and applied. A busy player's
    // are dropped (game.exe's dispatcher, FUN_0054d750 / FUN_0057eec0).
    void apply(const Command& command, std::uint32_t now_ms);

    // One step of the game: the player's commands, then the world. The
    // player walks a walk_path to the target (and operates or talks on
    // arrival); NPCs patrol; the merc follows; the monsters and missiles
    // (Fight::world); crossing into the next level or through a warp;
    // potions and regeneration.
    const Level* wanted_near = nullptr;               // whose neighbours were last asked for
    d2d::rules::Day day;
    std::uint32_t day_at = 0;                         // when the day last stepped
    void tick(const std::vector<Command>& cmds, std::uint32_t now_ms, std::uint32_t last_ms);

};

}  // namespace d2d::game
