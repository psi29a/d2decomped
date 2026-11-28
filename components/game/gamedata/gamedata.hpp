// SPDX-License-Identifier: GPL-3.0-or-later
// GameData: what the rules read (tables, timings, strings) and the World's
// view of them. No pixels, fonts or sound here: Scene (scene.hpp) adds those
// for the client. The levels (level.hpp), their spawning (spawning.hpp) and
// the composites' COFs (cof_anim.hpp) are apart: most files need none of them.
#pragma once

#include "game.hpp"
#include "npc.hpp"

#include <compcode.hpp>
#include <cube.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <mon_equip.hpp>
#include <monsters.hpp>
#include <montypes.hpp>
#include <quests_act5.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <skills.hpp>
#include <tbl.hpp>
#include <uniques.hpp>

#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace d2d::mpq { class Stack; }

namespace d2d::game {

struct ActLayouts;
struct Level;
struct LevelWarp;

// Its act (0 the first), 0 for none.
int level_act(const Level* level);

// What the game's rules read: the tables, the levels, animation timings,
// string tables, the MPQs. The World (world.hpp) sees only this, so a
// standalone server loads no graphics beyond the levels' tiles.
// A Level's DT1s carry pixels only when GameData::tile_pixels (the client).
struct GameData {
    GameData();
    GameData(GameData&&) noexcept;
    GameData(const GameData&) = delete;
    GameData& operator=(const GameData&) = delete;
    GameData& operator=(GameData&&) = delete;
    ~GameData();
    // A mode's timing, all the World needs of an animation: its COF name
    // ("AITW1HS"), animdata.d2's rate (256 = a frame per tick), frames per
    // direction and event frame (the hit), the COF's own rate and frames
    // for modes animdata lacks (game.exe's default record instead:
    // deviations.md approximation 19), and its directions (0: no such mode).
    struct AnimTiming {
        std::string   name;
        std::uint32_t speed = 0, frames = 0;
        int           action = -1;
        std::uint32_t cof_speed = 0, cof_frames = 0, directions = 0;
        // One frame's length: 25 ticks/s, speed/256 frames a tick.
        [[nodiscard]] std::uint32_t ms_per_frame() const {
            return 40u * 256u / std::max<std::uint32_t>(speed ? speed : cof_speed, 1);
        }
        [[nodiscard]] std::uint32_t length_ms() const {
            return ms_per_frame() * std::max<std::uint32_t>(frames ? frames : cof_frames, 1);
        }
        // When the attack lands: its event frame, else halfway.
        [[nodiscard]] std::uint32_t action_ms() const {
            return action >= 0 ? ms_per_frame() * std::uint32_t(action) : length_ms() / 2;
        }
    };
    // An NPC's / monster's mode, a character's mode in its gear: read from
    // the COF alone (load.hpp), kept.
    mutable std::map<std::string, AnimTiming> npc_timings;
    mutable std::map<std::array<std::uint8_t, 18>, AnimTiming> composite_timings;
    const AnimTiming& npc_timing(const Npc& npc, std::string_view mode) const;
    const AnimTiming& composite_timing(int d2s_class, int mode, const std::array<std::uint8_t, 32>& gfx) const;   // tints ignored
    // data\global\animdata.d2 by COF name: the game's rate source (COFs of
    // walk/run modes store 0), frames per direction and the event frame.
    struct AnimInfo { std::uint32_t speed = 0, frames = 0; int action = -1; };
    std::unordered_map<std::string, AnimInfo> anim_data;
    using Appearance = std::array<std::uint8_t, 32>;   // the save's 16 layer graphics, then their 16 tints
    std::vector<d2d::compcode::Entry> comp;         // appearance byte -> component
    std::unordered_map<std::string, d2d::compcode::Piece> item_pieces;
    d2d::compcode::Colours item_colours;
    std::optional<d2d::compcode::Types> item_types;    // ItemTypes' hierarchy (a state's itemtype)
    // Overlay.txt (FUN_00470390; record 0x84): the DCC in
    // data\global\overlays, its frames, drawn before the unit (PreDraw), the
    // offset, the height by the unit's class (Height1..4; class -1: +75),
    // AnimRate (x 16 / 256 frames a tick), its draw mode (Trans, as a COF
    // layer's draw effect), its light (InitRadius / Radius, RGB).
    struct OverlayInfo {
        std::string file;
        int frames = 1, x = 0, y = 0, rate = 16, trans = 3, radius = 0, init_radius = 0;
        bool predraw = false;
        std::array<int, 4> height{};
        [[nodiscard]] int dy(int cls) const { return y + (cls >= 0 && cls < 4 ? height[std::size_t(cls)] : 75); }
    };
    std::unordered_map<std::string, OverlayInfo> overlays;
    std::vector<const OverlayInfo*> overlay_rows;      // by Overlay.txt row (the id a one-shot overlay names); null: no file
    // The Overlay.txt row of `name`, -1 for none or no file.
    [[nodiscard]] int overlay_row(std::string_view name) const {
        if (name.empty()) return -1;
        const auto found = overlays.find(std::string(name));
        if (found == overlays.end()) return -1;
        const auto row = std::ranges::find(overlay_rows, &found->second);
        return row == overlay_rows.end() ? -1 : int(row - overlay_rows.begin());
    }
    // States.txt's look: the colour shift that wins by colorpri (PL2's
    // colour shift tables, +0x53500, colorshift - 1), overlays while it lasts
    // (overlay1..4) and once as it starts (castoverlay); the colour worn
    // items of itemtype take (itemtrans).
    struct StateInfo {
        int pri = 0, shift = -1, item_colour = -1;
        int group = 0;                     // States.txt group (+0x1e): states of one group switch each other off (FUN_0056c740)
        bool curse = false;                // States.txt curse (bit set 11, game +0xf8): its stat list is flagged 0x20, one such at a time (FUN_0056e970)
        bool restrict_form = false;        // States.txt restrict (wolf, bear, delerium): a skill's restrict asks for it (FUN_0063a510, FUN_00644060)
        int row = -1;                      // its States.txt row, the state id
        bool aura = false;                 // States.txt aura (bit 1, list +0xd0, FUN_0063a210): overlay1 / 2 rotate as kind 8 (FUN_004d9920)
        bool plr_stay_death = false, mon_stay_death = false, boss_stay_death = false;   // States.txt plrstaydeath / monstaydeath / bossstaydeath: kept at a death (data tables +0x100 / +0x104 / +0x108, FUN_00639fb0)
        std::string item_type;
        std::array<const OverlayInfo*, 4> over{};
        const OverlayInfo* cast = nullptr;
        int gfx_type = 0, gfx_class = 0;   // gfxtype / gfxclass (+0x2d / +0x2e): 2 looks and moves as that player class (FUN_00645270)
        int on_sound = 0, off_sound = 0;   // onsound / offsound (+0x26 / +0x28): on the unit as it comes on (FUN_004d9b20) / goes (FUN_004d9c30)
        int set_func = 0, rem_func = 0;    // setfunc / remfunc: the client's as it comes on (table 0x72a690) / goes (0x72a710)
        // The char panel's colours (rules::PanelColour bits): armblue, rfblue,
        // rcblue, rlblue, rpblue, attblue, damblue and their reds.
        std::uint8_t panel_blue = 0, panel_red = 0;
    };
    std::unordered_map<std::string, StateInfo> states;
    std::vector<std::string> state_names;              // States.txt's names by row (a host's 0xa8 / 0xa9 state ids)
    std::vector<std::uint8_t> colour_shifts;           // ACT1 PL2 +0x53500: 111 x 256
    // Items\Palette\<transform>.dat by Transform 1..8: 21 colours x 256.
    std::array<std::vector<std::uint8_t>, 9> colormaps;   // item code -> its layer, graphic, armour tiers
    // The look of what's worn (compcode::look). A state on the player with
    // an itemtrans colours worn items of its itemtype (FUN_0062c100's first
    // loop: Enchant's red weapons, Venom Claws' green).
    [[nodiscard]] Appearance look_of(const std::vector<d2d::d2s::Item>& items, std::span<const std::string_view> active_states = {},
                                     d2d::compcode::LookTarget target = d2d::compcode::LookTarget::sprite) const {
        std::vector<d2d::compcode::Worn> worn;
        for (const auto& item : items)
            if (item.location == d2d::d2s::item_location::kEquipped) {
                int colour = item_colours.of(item.quality, item.unique_id, item.set_id, item.prefix, item.suffix, item.affixes, item.class_affix,
                                        item.socketed && !item.socketed_items.empty() ? item.socketed_items[0].code : std::string{});
                if (const auto piece = item_pieces.find(item.code); piece != item_pieces.end() && item_types)
                    for (const auto name : active_states)
                        if (const auto state = states.find(std::string(name)); state != states.end() && state->second.item_colour >= 0) {
                            const int want = item_types->index(state->second.item_type);
                            if (item_types->isa(item_types->index(piece->second.type), want) || item_types->isa(item_types->index(piece->second.type2), want)) {
                                colour = state->second.item_colour;
                                break;
                            }
                        }
                worn.push_back({ item.slot, item.code, colour });
            }
        const auto look = d2d::compcode::look(comp, item_pieces, worn, target), tints = d2d::compcode::tints(item_pieces, worn);
        Appearance appearance;
        std::copy(look.begin(), look.end(), appearance.begin());
        std::copy(tints.begin(), tints.end(), appearance.begin() + 16);
        return appearance;
    }
    std::array<Appearance, 7>         starting_gear{};  // per d2s class, CharStats.txt
    // Kept open for lazy loads after startup. On the heap (mpq_stack owns it), so this header needn't
    // parse mpq.hpp; mpqs is it.
    std::unique_ptr<d2d::mpq::Stack> mpq_stack;
    d2d::mpq::Stack& mpqs;
    // True when 1.14d patch data is layered in. Without it patchstring.tbl
    // is the CD's (826 entries), whose IDs don't match what 1.14d code asks
    // for (10832 is "CREATE NEW" in 1.14d, "Bonus to Attack Rating" on CD).
    bool patched = false;
    // Decode the levels' tile pixels (the client draws them); a headless
    // server keeps only the tiles' headers and walk flags.
    bool tile_pixels = false;
    std::vector<std::int64_t> exp_next;          // experience.txt: exp for level+1, by level
    std::vector<int> exp_ratio;                  // experience.txt ExpRatio, by level (rules::kill_award)
    int exp_ratio_shift = 10, max_level = 99;    // the MaxLvl row's ExpRatio and Amazon
    // PetType.txt rows (Skills.txt pettype names one; "hireable" the merc's):
    // the automap marks it (FUN_00478d90), its portrait (FUN_00494020: icon
    // type 1 one, 2 a count, 3 one of several; the icon by MonStats class,
    // else baseicon, under Hireables\), its name key and life bar.
    struct PetType {
        std::string type;                                // its "pet type"
        bool automap = false, draw_life = false;
        int icon_type = 0;
        std::string name, base_icon;
        std::array<std::pair<int, std::string>, 4> class_icons{};   // mclass1..4 -> micon1..4
    };
    std::vector<PetType> pet_types;                     // in the file's order (the portraits')
    [[nodiscard]] const PetType* pet_type(std::string_view type) const {
        const auto found = std::ranges::find(pet_types, type, &PetType::type);
        return found == pet_types.end() ? nullptr : &*found;
    }
    std::array<std::int64_t, 3> resist_penalty{ 0, -40, -100 };   // DifficultyLevels.txt
    std::array<int, 3> curse_divisor{};          // DifficultyLevels AiCurseDivisor (+0x1c): Confuse / Attract / Dim Vision / Terror's length / it (FUN_005c37a0)
    std::array<int, 3> static_field_min{};       // DifficultyLevels StaticFieldMin (+0x40): no Static Field at or under this % of max life (FUN_005c9800, expansion)
    std::array<int, 3> cold_divisor{};           // DifficultyLevels MonsterColdDivisor (+0x18): a monster's chill length / it
    std::array<int, 3> life_steal_divisor{ 1, 1, 1 };   // DifficultyLevels LifeStealDivisor (+0x28): a player's life steal / it (FUN_0057c420)
    std::array<int, 3> mana_steal_divisor{ 1, 1, 1 };   // DifficultyLevels ManaStealDivisor (+0x2c)
    std::array<int, 3> freeze_divisor{};         // DifficultyLevels MonsterFreezeDivisor (+0x14): a monster's freeze length / it
    std::array<int, 3> hireling_boss_pct{ 100, 100, 100 };   // DifficultyLevels HireableBossDamagePercent (+0x38): a hireling's damage % on a MonStats boss (FUN_0057c060)
    // Items: parse tables (needs 1.14d ItemStatCost.txt), per-code
    // inventory graphic + size, and the 800x600 inventory panel/layouts.
    std::optional<d2d::d2s::ItemTables> item_tables;
    // String keys for item names, indexed the way the save's IDs are (see
    // item_lines): uniques/sets by row without separators, magic affixes by
    // raw row, rare names by raw row, runewords by RunewordN rank.
    struct ItemNames {
        std::vector<std::string> unique, set, prefix, suffix, rare_pre, rare_suf, runeword;
        std::vector<std::string> set_group;   // Sets.txt name by row (record +2), as SetItems rows' `set` count them
    } item_names;
    // What the hover text (item_lines, FUN_0048dd90) reads off the items
    // record by code: wclass (+0xc0), mindam/maxdam (+0xfe/+0xff),
    // 2handmindam/2handmaxdam (+0x102/+0x103), minmisdam/maxmisdam
    // (+0x100/+0x101), nodurability (+0x113), misc.txt spelldesc (+0xb4),
    // spelldescstr (+0xb6), spelldesccalc (+0xa4) and stat1's ID (+0x9e).
    // The level requirements by save ID, indexed like item_names.
    struct ItemDesc {
        std::string wclass, spell_str;
        std::array<int, 6> dam{};                // one-hand, two-hand, throw: min, max
        bool nodurability = false;
        int spell_desc = 0, spell_calc = 0, spell_stat = -1;
    };
    std::unordered_map<std::string, ItemDesc> item_desc;
    std::vector<int> prefix_req, suffix_req, unique_req, set_req, automagic_req;   // automagic_req: AutoMagic levelreq by row (class_affix - 1)
    // MagicPrefix / MagicSuffix / AutoMagic class (+0x67, -1 none) and classlevelreq (+0x68), by row.
    std::vector<std::pair<int, int>> prefix_class_req, suffix_class_req, automagic_class_req;
    // UniqueItems / SetItems dropsound, usesound, dropsfxframe by row (item_sound, flippy_sound).
    struct RowSound { std::string drop, use; int drop_frame = 0; };
    std::vector<RowSound> unique_sounds, set_sounds;
    // ItemStatCost.txt description columns, by stat ID, and what the skill
    // descfuncs need: skill name keys by skill ID, CharStats strings by class.
    struct StatDesc {
        int prio = 0, func = 0, val = 0, operation = 0, op_param = 0, dgrp = 0, dgrp_func = 0, dgrp_val = 0;
        std::string pos, neg, str2, dgrp_pos, dgrp_neg, dgrp_str2;
        int send_bits = 0, send_param = 0;      // Send Bits / Send Param Bits (+8 / +9): a stat list's widths on the wire (0xa8, 0xaa)
    };
    std::vector<StatDesc> stat_desc;
    std::vector<std::string> skill_name;         // string key
    std::vector<int>         skill_class;        // CharStats row, -1 none
    struct ClassStrs { std::string all_skills, tab[3], only; };
    std::array<ClassStrs, 7> class_strs;
    d2d::rules::Tables rules;                    // item/vendor/price tables (components/rules)
    // LvlPrest Outdoors by Def: game.exe flags those presets' rooms 0x80000
    // (FUN_00667ed0), where monsters need no line of sight (FUN_0066ba70).
    std::vector<bool> prest_outdoors;
    // The levels a whole-level preset with LvlPrest AutoMap (row +0x30) makes:
    // FUN_00668100 brings their every room into the automap as they come up.
    std::set<int> prest_automap;
    // belts.txt 800x600 rows ("belt2" .. "uber belt", after the Expansion
    // separator) by armor.txt `belt` index: box count and boxes 1..16
    // {left, right, top, bottom}. Index 2 ("default") when no belt is worn.
    struct Belt { int boxes = 4; std::array<std::array<int, 4>, 16> box{}; };
    std::array<Belt, 7> belts{};
    struct InvLayout {
        int panel_x = 400, panel_y = 60;
        int grid_x = 0, grid_y = 0, box_w = 29, box_h = 29;
        int cols = 0, rows = 0;                     // gridX, gridY
        std::array<std::array<int, 4>, 11> slots{};   // by body slot 1..10: x, y, w, h
    };
    std::array<InvLayout, 7> inv_layout{};            // by d2s class
    // Stash: inventory.txt "Bank Page2" (classic, 6x4) / "Big Bank Page2"
    // (expansion, 6x8) grids; art PANEL\bank / PANEL\TradeStash (game.exe
    // loads the latter only for expansion games, FUN_00489e50), drawn in
    // the left-panel spot like the char panel (0x48f1a4).
    std::array<InvLayout, 2> stash_layout{};          // [expansion]
    // A trade with another player: inventory.txt "Trade Page 1-2" (their
    // offer, top) and "Trade Page 2-2" (ours), 10x4 each; art PANEL\trade
    // (FUN_00489360), the left-panel spot.
    std::array<InvLayout, 2> trade_layout{};          // [0] theirs, [1] ours
    // Horadric Cube: inventory.txt "Transmogrify Box2" (3x4), art
    // PANEL\supertransmogrifier (FUN_0048a4b0), same left-panel spot
    // (panel 0xe, 0x48eeca).
    InvLayout cube_layout{};
    InvLayout merc_layout{};                          // inventory.txt "Hireling2": the Hireling screen's body slots
    std::array<d2d::rules::ClassGains, 7> class_gains{};   // by d2s class (CharStats)
    // A new character's start (CharStats.txt): str, dex, int (energy), vit,
    // stamina, hpadd; item1..10 with their loc and count; StartSkill;
    // Skill 1..10 (record +0xae), the general skills every one of the class
    // has at level 1 (FUN_00647ee0), by Skills.txt name.
    struct ClassStart {
        int str = 0, dex = 0, ene = 0, vit = 0, stamina = 0, hpadd = 0;
        struct Item { std::string code, loc; int count = 0; };
        std::vector<Item> items;
        std::string start_skill;
        std::vector<std::string> skills;
    };
    std::array<ClassStart, 7> class_start{};
    // D2's three-tier string tables. Lookup order per D2's own convention:
    //   patchstring.tbl (826 entries) — patch-shipped overrides, wins
    //   expansionstring.tbl (2788 entries) — LoD additions (Druid/Assassin
    //     class names live here in some builds, but 1.14d put them in
    //     patchstring.tbl — see class-table.md)
    //   string.tbl (5099 entries) — base classic keys
    // Frontend button labels come out via ID lookup (see
    // docs/research/re/frontend-menu-table.md — records at 0x708ec0+ carry
    // TBL ids 0x13f2..0x13f7 in the +0x18 field). Class-name keys are bare
    // ("Barbarian", "Assassin", "Druid", …) — see class-table.md.
    d2d::tbl::Table       strings;         // string.tbl
    d2d::tbl::Table       patch_strings;   // patchstring.tbl (has Druid/Assassin)
    d2d::tbl::Table       exp_strings;     // expansionstring.tbl
    // The Rogue Encampment, built at start. On the heap (town_level owns it), so this header needn't
    // parse level.hpp; town is it.
    std::unique_ptr<Level> town_level;
    Level& town;
    // Every other level: built from the map seed the first time it's
    // wanted (the same on every machine), then kept. `level` builds it or
    // waits for its build; `want_level` starts one on the builder thread
    // (the levels next to the player's); `poll_levels` takes in finished
    // builds and links outdoor neighbours (Level::nearby). load.hpp.
    mutable std::map<int, std::unique_ptr<Level>> levels;
    const Level* level(int id) const;
    // The town of level `id`'s act: the camp, or a later act's town (built
    // on demand like any level; null while its act isn't built).
    [[nodiscard]] const Level* town_of(int id) const { return level(d2d::rules::level_ids::kActTowns[std::size_t(d2d::rules::level_ids::act_of(id))]); }
    void want_level(int id) const;
    void poll_levels() const;
    // SuperUniques.txt (without its Expansion row): name, MonStats row of
    // its Class, minions.
    struct SuperUnique { std::string name; int type = -1, min_grp = 0, max_grp = 0; std::vector<int> mods; std::array<std::string, 3> treasure_classes;
                         std::array<int, 3> utrans{}; bool autopos = false, stacks = false;   // Utrans by difficulty: its colour; AutoPos, Stacks
                         std::string sound; };                                                   // MonSound: its own MonSounds row (rules::boss_sound)
    std::vector<SuperUnique> superuniques;
    std::vector<d2d::rules::CubeRecipe> cube_recipes;    // cubemain.txt, compiled (rules::compile_cube_input / _output)
    d2d::rules::UMods umods;                           // MonUMod.txt: champion / unique mods and constants
    std::array<std::vector<std::string>, 3> unique_names;   // UniquePrefix / Suffix / Appellation, resolved
    std::array<std::string, 2> unique_formats;         // strings 0x6b9 ("%0 %1"), 0x6ba ("%0 %1 %2")
    std::vector<d2d::rules::ShrineRow> shrines;        // Shrines.txt
    std::vector<std::array<int, 4>> area_level;        // Levels.txt MonLvl1Ex..3Ex, then classic MonLvl1, by Id
    std::vector<d2d::rules::LevelMon> level_mon;       // Levels.txt monster columns, by Id
    std::vector<d2d::rules::ObjGroup> obj_groups;      // objgroup.txt rows by Offset (FUN_00552610)
    std::vector<std::uint8_t> obj_subclass;            // objects.txt SubClass by Id (+0x167: 552610's throttle, 0x40 a waypoint)
    std::vector<std::uint8_t> field;                   // expfield.d2: 256 x 256 directions (0-7, 8 the centre) toward (128, 128)
    std::uint32_t map_seed = 3;                        // act layout + levels (3: townE1)
    int difficulty = 0;                                // the levels' (a maze's rooms: LvlMaze Rooms by difficulty)
    // The acts' layouts (level.hpp); layouts is them.
    std::unique_ptr<ActLayouts> act_layouts;
    ActLayouts& layouts;
    // Mercenary units by hireling.txt Id (the save's merc type): the
    // monster, and the first name key (merc01, merca201, MercX101, ...).
    // hire_desc: hireling.txt HireDesc as its hiredesc.txt row (0 None;
    // FUN_00655720 links it to row +0xd2), for the hire list's second line.
    struct Merc { Npc npc; std::string name_first; int hire_desc = 0; };
    // MonSeq.txt: a monster skill's sequence (its MonStats Sk*mode names
    // one), frame by frame: the mode played, its frame, the event (1 a hit).
    struct MonSeqFrame { std::string mode; int frame = 0, event = 0; };
    std::unordered_map<std::string, std::vector<MonSeqFrame>> mon_seqs;
    std::unordered_map<int, Merc> mercs;
    // Waypoints (docs/research/re/waypoint.md): Levels.txt rows with a
    // Waypoint index, per act in index order; art ui\menu\waygate*.
    struct WaypointLevel { int waypoint = 0, level = 0; std::string name; };
    std::array<std::vector<WaypointLevel>, 5> waypoint_levels;
    std::unordered_map<int, std::string> level_names;  // Levels.txt Id -> LevelName (a string key)
    // Sounds.txt by Index: file (under data\global\sfx or, for speech,
    // data\local\sfx) and volume 0..255.
    struct Sound { std::string file; int volume = 255; bool loop = false, music = false; int fade_in = 0, fade_out = 0;
                   int group = 0;                      // Group Size: variants at the following indices
                   std::array<int, 3> block{ -1, -1, -1 };      // a song's Block 1..3: cue points, in sample frames
                   int falloff = 0;                     // Falloff (record +0x47): its near / far range (audio.hpp)
                   bool tracking = false;               // Tracking: follows its unit, its volume and pan redone each tick
                   // How it starts against the playing ones (sound.md "Starting a sound"): Defer Inst /
                   // Stop Inst (record +0x41 / +0x42), Compound (+0x45, ticks, < 0 any), Solo (+0x52),
                   // Priority (+0x4e); group_base (+0x60) the first row of its group, itself when none.
                   bool defer = false, stop_inst = false, solo = false;
                   int compound = 0, priority = 0, group_base = 0; };
    std::unordered_map<std::string, int> sound_index;   // Sounds.txt Sound -> Index
    // How long Sounds.txt row `index` plays, in ms (its WAV under data\global\sfx, else
    // data\local\sfx: rules::wav_ms); 0 when it can't be read. Cached.
    std::uint32_t sound_ms(int index) const;
    mutable std::unordered_map<int, std::uint32_t> sound_lengths;
    // MonSounds.txt by Id (MonStats MonSound): per attack mode (A1, A2) the
    // attack and weapon sounds, their delays in ticks and the attack sound's
    // chance; the get-hit and death sounds and delays.
    struct MonSound {
        std::array<int, 2> attack{}, weapon{}, att_del{}, wea_del{}, att_prb{ 100, 100 };
        int hit = 0, death = 0, hit_del = 0, death_del = 0;
        // Skill1..4 (+0x44..: on S1..S4, FUN_004cb890), Neutral and NeuTime (+0x68 / +0x6c, ticks:
        // FUN_004cb460), Init (+0x70: as it first shows, FUN_004cc380), Footstep, FootstepLayer and
        // FsCnt / FsOff / FsPrb (+0x54..+0x64: FUN_004caf60).
        std::array<int, 4> skill{};
        int neutral = 0, neutral_time = 0, init = 0, taunt = 0, flee = 0;
        int footstep = 0, footstep_layer = 0, fs_count = 0, fs_offset = 0, fs_chance = 100;
    };
    std::unordered_map<std::string, MonSound> mon_sounds;
    std::vector<Sound> sounds;
    fs::path data_dir;                                  // the MPQs' folder
    // CharStats WalkVelocity / RunVelocity by d2s class. Running adds
    // run*100/walk - 100 to velocitypercent (FUN_00620e80): +50%.
    std::array<int, 7> walk_velocity{ 6, 6, 6, 6, 6, 6, 6 }, run_velocity{ 9, 9, 9, 9, 9, 9, 9 };
    std::array<int, 7> run_drain{ 20, 20, 20, 20, 20, 20, 20 };   // CharStats RunDrain (CharStats +0x42)
    d2d::rules::Monsters monsters;                      // MonStats / MonStats2 / MonLvl
    d2d::rules::SkillTables skills;                     // Skills.txt, compiled calcs (skills.hpp)
    std::vector<Npc> mon_npc;                           // by MonStats row: its composite recipe
    // Missiles.txt rows monsters and skills fire: velocity (units like
    // MonStats Velocity), range in ticks (+ LevRange a skill level),
    // SrcDamage (128 = all the attack's damage), its own damage, animation
    // (AnimSpeed/16 frames a tick over AnimLen; its CelFile DCC, 32
    // directions, is in missile_cels); for a skill's: the Skill whose damage it carries
    // (FUN_0064b860), ToHit (rolls the attack rating), CollideKill (spent on
    // its first hit; else flies through), Pierce (may fly on, stat 328 %),
    // pSrvHitFunc (+0x0e, table 0x73c840) and its sHitPar1 (+0x4c).
    struct MissileInfo { std::string name, cel_file; int id = -1; int vel = 0, range = 0, src_damage = 0, min = 0, max = 0, anim_speed = 16, anim_len = 1;
                         int vel_lev = 0, max_vel = 0, accel = 0, activate = 0;   // its flight (rules::MissileFlight)
                         std::string skill; int lev_range = 0, hit_func = 0, hit_par1 = 0;
                         int hit_class = 0;                                     // HitClass, a number (+0x94, FUN_00661b20 type 4): its impact sound
                         int travel_sound = 0, hit_sound = 0, prog_sound = 0;   // TravelSound (+0x12), HitSound (+0x14), ProgSound (+0x34)
                         std::string prog_overlay;                              // ProgOverlay (+0x36): hit function 7's overlay on the unit it heals
                         bool to_hit = false, collide_kill = true, pierce = false;
                         bool half_2h_src = false;                              // Half2HSrc (+4 bit 14): SrcDamage halved from a two-hander (FUN_0064b860)
                         bool return_fire = false;   // ReturnFire (record flag bit 9, +5 & 2): Chilling Armor answers it (FUN_005cab40)
                         bool collide_friend = false;   // CollideFriend (+0x187): it strikes its owner's friends too (FUN_005a8730)
                         int src_miss_dmg = 0;          // SrcMissDmg (+0x12e): a SrcDamage-0 row's share of its parent missile's physical, of 128
                         bool can_slow = false;         // CanSlow (+4 bit 8): its owner's Slow Missiles slows it (FUN_0059fa30)
                         bool client_col = false;       // ClientCol (+0x185): the client tests units along its path (FUN_004d30c0), so each one it meets goes off (FUN_004d2d70)
                         // Its own element (a row with no Skill: FUN_0064b100 / 0064b1d0 /
                         // 0064b2a0 by level): Skill::etype order, frze as cold.
                         int etype = -1, emin = 0, emax = 0, hitshift = 8, elen = 0;
                         std::array<int, 5> emin_lev{}, emax_lev{}; std::array<int, 3> elen_lev{};
                         std::array<int, 5> min_lev{}, max_lev{};   // MinLevDam1..5 / MaxLevDam1..5: the physical part's level brackets (FUN_00644b70 via FUN_0064af20 / FUN_0064aff0)
                         std::string dmg_sym_text; d2d::rules::Calc dmg_sym;     // DmgSymPerCalc (+0xe0), compiled with the skills
                         std::string edmg_sym_text; d2d::rules::Calc edmg_sym;   // EDmgSymPerCalc (+0x118), compiled with the skills
                         std::string hit_calc_text; d2d::rules::Calc hit_calc;   // SHitCalc1 (+0x88): Immolation Arrow's fire length
                         std::string srv_calc_text; d2d::rules::Calc srv_calc;   // SrvCalc1 (+0x80): do 2's, over 0 its trail loops (FUN_005ae400)
                         // Its server functions' inputs: pSrvDoFunc (table 0x73c768), Param1..,
                         // sHitPar2, the rows it spawns (SubMissile1, HitSubMissile1).
                         int clt_do = 0;   // pCltDoFunc (the client's, table 0x72a398 + 4 x n)
                         int clt_hit = 0;  // pCltHitFunc (table 0x72a508 + 4 x n)
                         int srv_do = 0, param1 = 0, param2 = 0, param3 = 0, param4 = 0, hit_par2 = 0;
                         int hit_par3 = 0;      // sHitPar3: hit 2's sub-missiles' sub loops (FUN_005a9d80 -> FUN_005a9370's [0xd])
                         // Param5 (+0x48), CltParam1..5 (+0x58..+0x68), cHitPar1..3 (+0x6c..+0x74):
                         // what misscalc reads of them (FUN_0064b340, skill_text::miss_operand).
                         int param5 = 0; std::array<int, 5> clt_params{}; std::array<int, 3> clt_hit_pars{};
                         // SubLoop, SubStart, SubStop (+0x180..+0x182): made with create flag 8, its Range runs on by
                         // (SubStop - SubStart) x [0xd] when SubLoop (FUN_0059fa30). LoopAnim (+0x137): the client's
                         // SubStart..SubStop wrap needs it with SubLoop (FUN_004d30c0).
                         int sub_loop = 0, sub_start = 0, sub_stop = 0, loop_anim = 0;
                         int damage_rate = 0;   // DamageRate (+0x19c, stat 327 damage_framerate on the missile): a burner's flat DR / MDR x it / 1024 (rules::frame_rate_flat)
                         int dmg_func = 0, dmg_par1 = 0, dmg_par2 = 0;   // pSrvDmgFunc (5: Blessed Hammer's bonus, FUN_005ad2f0), dParam1 (undead %) and dParam2 (demon %)
                         bool next_hit = false; int next_delay = 0;   // NextHit: its hit puts state 0x56 on the unit for NextDelay frames
                         bool last_collide = false;                   // LastCollide (+4 bit 0): it passes the unit it last struck by
                         bool missile_skill = false;                  // MissileSkill (+5 bit 7): its damage is the missile's own skill's, over 0 (Fight::damage_skill)
                         // The one-unit-a-frame, skip-the-last-unit rule (FUN_005ae1f0, FUN_005a8730) for a srvdofunc 1 or 3 row (3: the standing
                         // clouds, FUN_005ae480 -> FUN_005ae1f0; tools/emu/standing_rehit.py) with LastCollide and not NextHit, Pierce rows
                         // too: FUN_005a8730 doesn't test the Pierce flag (+4 bit 2), and every Pierce row has LastCollide.
                         bool last_unit_rule() const { return last_collide && !next_hit && (srv_do == 1 || srv_do == 3); }
                         std::string sub, sub2, hit_sub;
                         std::array<const MissileInfo*, 2> subs{}; const MissileInfo* hit_sub_info = nullptr;   // those rows, loaded
                         // The client's: CltSubMissile1 (+0x1e) and InitSteps (+0x135), the row client do 8 lays
                         // and from what age (FUN_004d38d0; game.exe's column table FUN_00661b20).
                         std::string clt_sub; const MissileInfo* clt_sub_info = nullptr; int init_steps = 0;
                         std::array<std::string, 2> clt_hit_sub_names;
                         std::array<const MissileInfo*, 2> clt_hit_subs{};
                         std::string clt_sub2; const MissileInfo* clt_sub2_info = nullptr;   // CltSubMissile2 (+0x20): client do 26's second
                         // ExplosionMissile (+0x16): played where it hits a unit or a wall,
                         // and at its range's end too when AlwaysExplode (+0x18c) (FUN_004d2d70).
                         std::string explosion_name; const MissileInfo* explosion = nullptr; bool always_explode = false;
                         int light = 0;   // Light: its light radius, subtiles
                         int size = 0;    // Size: its unit collision shape (rules::missile_touches; 0 strikes none)
                         int trans = 0;   // Trans (record +0x18d): 1 additive, 2 multiply (FUN_004720xx: draw modes 3 / 4)
                       };
    std::unordered_map<std::string, MissileInfo> missiles;
    std::array<Npc, 2> trap_fires;                         // objects 162 / 160, ON (a chest's traps 5 and 7)
    Npc afterglow;                                         // object 131 (9b), OP: the "vile dog afterglow" the Dark Wanderer leaves
    Npc town_portal;                                       // object 59 (TP): a town portal's look and light
    Npc red_portal;                                        // object 60 (PP): a permanent (red) portal's (World::portal_object)
    Npc duriel_portal;                                     // object 100 (SJ): the true tomb's way down to Duriel's Lair
    Npc cain_portal;                                       // object 189 (tP: TP's files): Tristram Cain's and the cages' portals
    std::unordered_map<std::string, std::string> thrown;   // a throwing weapon's code: its Missiles.txt row (weapons.txt missiletype)
    std::unordered_map<std::string, std::string> gem_letters;   // gems.txt letter by code (the runes'; gems.txt +0x20)
    std::vector<std::size_t> mon_bin;                  // game.exe's MonStats unit ids -> rows (no Expansion row)
    std::vector<bool> mon_is_npc;                      // by MonStats row: MonStats npc
    // MonEquip.txt (FUN_00659e60), and by MonStats row its first row there
    // (MonStats +0x2a, -1 none): rules::monster_gear's tables.
    std::vector<d2d::rules::MonEquipRow> mon_equip;
    std::vector<int> mon_equip_first;
    // The level builder: its own MPQ handles, the DRLG tables and the
    // tables a build reads. Last, so it's torn down first: a build in
    // flight reads the rest of GameData.
    struct LevelBuilder;
    std::shared_ptr<LevelBuilder> builder;
};

// D2 TBL values are UTF-16; our font is Latin-1.
std::string u16_to_latin1(std::u16string_view text);

// TBL lookup with D2's precedence: patch → expansion → base. First-hit wins,
// matching how the game resolves any string ID/key at runtime.
inline std::optional<std::u16string_view>
lookup_string(const GameData& game_data, std::string_view key) {
    if (auto found = game_data.patch_strings.get(key); found && !found->empty()) return found;
    if (auto found = game_data.exp_strings.get(key);   found && !found->empty()) return found;
    if (auto found = game_data.strings.get(key);       found && !found->empty()) return found;
    return std::nullopt;
}
inline std::optional<std::u16string_view>
lookup_string(const GameData& game_data, std::uint16_t id) {
    // Numeric IDs are banked, not layered (RE'd from char-select: 0x58cb =
    // 22731 resolves to expansionstring[2731] "EXPANSION CHARACTER"):
    //   0..9999 string.tbl, 10000..19999 patchstring, 20000+ expansionstring.
    // Trying every table with the raw ID hits the wrong one — string.tbl
    // 2731 is "Bile".
    if (id >= 10000 && id < 20000 && !game_data.patched) return std::nullopt;
    const auto& table = id >= 20000 ? game_data.exp_strings : id >= 10000 ? game_data.patch_strings : game_data.strings;
    const auto local = std::uint16_t(id >= 20000 ? id - 20000 : id >= 10000 ? id - 10000 : id);
    if (auto found = table.get(local); found && !found->empty()) return found;
    return std::nullopt;
}

inline std::string string_id(const GameData& game_data, std::uint16_t id) {
    const auto found = lookup_string(game_data, id);
    return found ? u16_to_latin1(*found) : std::string{};
}

// Composite tokens: d2s class id -> CHARS folder (Assassin is "AI", its
// dev codename), D2 mode ids we use, and layer names by COF type.
constexpr const char* kCharCode[7] = { "AM", "SO", "NE", "PA", "BA", "DZ", "AI" };
constexpr int kModeDT = 0, kModeNU = 1, kModeWL = 2, kModeRN = 3, kModeGH = 4, kModeTN = 5, kModeTW = 6,
              kModeA1 = 7, kModeA2 = 8, kModeBL = 9, kModeSC = 10, kModeKK = 12, kModeS1 = 13, kModeS3 = 15, kModeS4 = 16, kModeDD = 17;

// Movement speed from a unit's velocity (CharStats Walk/RunVelocity,
// MonStats Velocity): the path velocity is velocity << 8 (scaled by
// velocitypercent, FUN_00462a20), and a unit covers path velocity / 4096
// subtiles per 40 ms tick (arrival time (dist << 16) / ((v >> 8) << 12),
// 0x4c86a3) — velocity / 16 subtiles a tick. Walk 6: 1.875 cells/s.
constexpr float cells_per_sec(float velocity) { return velocity / 16.f * 25.f / 5.f; }
// A walker's path velocity (path +0x7c) as FUN_00623f50 sets it: the
// Velocity << 8 x its walk % / 100, truncated (tools/emu/walk_velocity.py).
// A frame moves it velocity / 4096 subtiles (FUN_006502d0).
constexpr int path_velocity(int velocity, int pct = 100) { return (velocity << 8) * pct / 100; }

// Direction (0..15, D2's DCC order) for a world-space step (dx, dy) in
// cells. Directions are screen-space: project to screen, take the angle
// clockwise from straight down, and map the 16 sectors through D2's
// ordering — the 8 main directions first (0 SW, 1 NW, 2 NE, 3 SE, 4 S,
// 5 W, 6 N, 7 E), then the half-steps (8 between S and SW, ...).
inline constexpr int kDir16FromSector[16] = { 4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15 };
inline constexpr int kDir32FromSector[32] = { 4, 16, 8, 17, 0, 18, 9, 19, 5, 20, 10, 21, 1, 22, 11, 23,
                                              6, 24, 12, 25, 2, 26, 13, 27, 7, 28, 14, 29, 3, 30, 15, 31 };
int direction16(float dx, float dy);
// A composite's direction for a 16-direction facing.
std::uint8_t cof_direction(int dir16, int dirs);
std::uint8_t missile_direction(int dir64, int dirs);
constexpr const char* kModeCode[18] = { "DT", "NU", "WL", "RN", "GH", "TN", "TW", "A1", "A2", "BL", "SC",
                                        "TH", "KK", "S1", "S2", "S3", "S4", "DD" };
constexpr const char* kLayerCode[16] = {
    "HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH",
    "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8",
};

// Loading and building (gamedata.cpp).
[[nodiscard]] std::string ds1_path_to_mpq(std::string_view path);

// MonStats2 layer variants ("lit,med": quoted lists), HDv..S8v.
constexpr const char* kVariant[16] = {
    "HDv", "TRv", "LGv", "RAv", "LAv", "RHv", "LHv", "SHv",
    "S1v", "S2v", "S3v", "S4v", "S5v", "S6v", "S7v", "S8v",
};

// Encode (style, sequence, type) into a single lookup key.
[[nodiscard]] std::uint64_t tile_key(int style, int seq, int type);

// The Blood Moor from the map seed (components/drlg): act 1's layout
// places it against the town, the generator fills it, its tiles come from
// the Act 1 wilderness DT1s (LvlTypes).
// The levels d2d builds so far: Act 1's (the town is GameData::town),
// Act 2's, Kurast Docks (75: Meshif's boat lands there), the jungle
// (76-78), Kurast up to Travincal (79-83) and its dungeons (84-102),
// Act 4 (103-108) and Act 5 (109-136, the uber levels too).
constexpr std::array kBuiltLevels{ 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
                                    56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83,
                                    84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108,
                                    109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130,
                                    131, 132, 133, 134, 135, 136 };

std::vector<std::string> split_variants(std::string_view text);
std::unordered_map<std::string, std::size_t> id_rows(const d2d::txt::Table& table);
int level_light(std::string_view intensity, std::string_view red, std::string_view green, std::string_view blue);
Npc monster_npc(const GameData& game_data, const d2d::txt::Table& monstats, const d2d::txt::Table& ms2,
                const std::unordered_map<std::string, std::size_t>& ms2_rows, std::size_t row);

// A missile row's own elemental damage in 256ths: (EMin + MinELev brackets) plus its EDmgSymPerCalc % of that,
// then << HitShift.
int missile_elem(const GameData& game_data, const GameData::MissileInfo& missile, const d2d::rules::CalcEnv& env, int skill_id, int lvl, bool max);
// Its physical part in 256ths: plus its DmgSymPerCalc % of that, then << HitShift.
int missile_phys(const GameData& game_data, const GameData::MissileInfo& missile, const d2d::rules::CalcEnv& env, int skill_id, int lvl, bool max);
// An object's mode tokens, and the index of one (0 when unknown).
inline constexpr std::array<std::string_view, 8> kObjectModes{ "NU", "OP", "ON", "S1", "S2", "S3", "S4", "S5" };
int mode_index(std::string_view mode);
// A monster's mode tokens (MonMode: DT 0 .. RN 15), and the number of one
// (`unknown` when it isn't one: a MonSeq sequence or Skills.txt monanim xx
// plays SQ, 14).
inline constexpr std::array<std::string_view, 16> kMonsterModes{ "DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN" };
int monster_mode(std::string_view token, int unknown = 14);
// An object's frame rate in a mode (FUN_00624390, 0x624558): FrameDelta as
// written with Sync, else less its 1/16 plus a roll under its 1/8 on the
// unit's seed, kept within 0..0x7fff.
inline int object_frame_delta(int delta, bool sync, d2d::rules::Rng seed) {
    if (sync) return delta;
    return std::clamp(seed(delta >> 3) + delta - (delta >> 4), 0, 0x7fff);
}
// The same for a placed object: its client seed (its spot) steps once on
// entering a mode, and the rate is kept until the mode changes.
inline int object_frame_delta(const Npc& npc, std::size_t mode) {
    if (npc.sync) return npc.frame_delta[mode];
    if (!npc.client_seeded) {
        const auto spot = std::uint32_t(npc.x * 5) << 16 | std::uint32_t(npc.y * 5);   // subtiles: never moves
        npc.client_seed = d2d::rules::Rng{ spot };
        npc.client_seeded = true;
    }
    if (npc.client_mode != int(mode)) {
        npc.client_mode = int(mode);
        const int delta = npc.frame_delta[mode];
        npc.client_frame_delta = std::uint16_t(std::clamp(npc.client_seed(delta >> 3) + delta - (delta >> 4), 0, 0x7fff));
    }
    return npc.client_frame_delta;
}
// The rate object_frame_delta(npc, mode) would give, without drawing: the cached one while the unit is in that
// mode, else the roll its seed makes next.
int object_frame_delta_peek(const Npc& npc, std::size_t mode);
// The game's object seed: {the game seed's second step, 666}.
d2d::rules::Rng object_seed(std::uint32_t map_seed);

// FUN_0064dea0's search (`step` subtiles apart: FUN_0064e7b0 / FUN_0064e810
// pass 1, FUN_0064e7e0 its caller's 4 or 5): (x, y) itself if `clear`, else
// rings out at radius r = 1, 1 + step, 1 + 2 step, ... while r < `limit`
// (param_8: 0x32 from all three wrappers, so 49 rings at step 1, 13 at
// step 4, 10 at step 5), each ring's columns
// x - r and x + r from y - r every `step` (left then right a row), then its
// rows y - r and y + r from x - r + 1 every `step` (top then bottom a
// column); the first clear spot nearest (Manhattan) in the first ring with
// one wins. None: nullopt (the callers' "keep the room" flag then keeps
// (x, y) or fails). The limit by step: tools/emu/corpse_spot.py limit.
template <class Clear>
std::optional<std::pair<int, int>> nearest_spot(int x, int y, Clear clear, int step = 1, int limit = 50) {
    if (clear(x, y)) return std::pair{ x, y };
    int best = -1, best_x = x, best_y = y;
    for (int radius = 1; radius < limit && best < 0; radius += step) {
        auto test = [&](int test_x, int test_y) {
            const int distance = std::abs(test_x - x) + std::abs(test_y - y);
            if (clear(test_x, test_y) && (best < 0 || distance < best)) { best = distance; best_x = test_x; best_y = test_y; }
        };
        for (int ty = y - radius; ty <= y + radius; ty += step) { test(x - radius, ty); test(x + radius, ty); }
        for (int tx = x - radius + 1; tx <= x + radius - 1; tx += step) { test(tx, y - radius); test(tx, y + radius); }
    }
    if (best < 0) return std::nullopt;
    return std::pair{ best_x, best_y };
}

// Where a saved corpse lies (FUN_005394a0 -> FUN_005352c0 -> FUN_0064e7e0(room,
// &xy, 2, 0x9c19, 4)): from subtile (x, y), the act town's start, the nearest
// spot, 4 apart, whose plus (FUN_0064d9b0 case 2) meets no bit of 0x9c19 in
// `flags(x, y)` (0x27 off the rooms; 0x1000 the units' footprints: the player
// standing on the start pushes it off). None: (x, y). Checked against
// tools/emu/corpse_spot.py (test_corpse_spot).
template <class Flags>
std::pair<int, int> corpse_spot(int x, int y, Flags flags) {
    const auto hit = [&](int at_x, int at_y) { return (flags(at_x, at_y) & 0x9c19) != 0; };
    const auto clear = [&](int at_x, int at_y) { return !hit(at_x, at_y) && !hit(at_x - 1, at_y) && !hit(at_x + 1, at_y) && !hit(at_x, at_y - 1) && !hit(at_x, at_y + 1); };
    return nearest_spot(x, y, clear, 4).value_or(std::pair{ x, y });
}

// Where a portal's twin stands (FUN_0056cf40 -> FUN_0064e7e0(room, &xy, 3,
// 0xbe11, 5)): from subtile (x, y), the level's type 11 spawn tile + 3, the
// nearest spot, 5 apart, whose 3x3 square (FUN_0064d9b0 case 3) meets no
// bit of 0xbe11 in `flags(x, y)` (0x27 off the rooms). None: (x, y)
// (0x56d033). Checked against tools/emu/corpse_spot.py's twin mode.
template <class Flags>
std::pair<int, int> twin_spot(int x, int y, Flags flags) {
    const auto clear = [&](int at_x, int at_y) {
        for (int row = -1; row <= 1; ++row)
            for (int column = -1; column <= 1; ++column)
                if (flags(at_x + column, at_y + row) & 0xbe11) return false;
        return true;
    };
    return nearest_spot(x, y, clear, 5).value_or(std::pair{ x, y });
}

// Where a SearchOpenXY skill goes (Skills.txt +6 & 0x80: Fire Wall, Meteor,
// Blizzard, Eruption, Volcano): FUN_004677b0, from FUN_00467880 on the
// client before the skill is sent. From subtile (x, y), the target's or,
// with none, the click's: when its word has bit 1 (FUN_0064cb30; 0x27 off
// the rooms), the nearest subtile without it within 7 (FUN_0064e780(room,
// &xy, 1, 1, 0, 7) -> FUN_0064dea0: size 1, rings of radius 1 to 6). That
// spot: the skill goes there, at the ground (the target dropped). None:
// nullopt, the target and the click as they were. Checked against
// tools/emu/click_free_spot.py (test_search_open_xy).
template <class Flags>
std::optional<std::pair<int, int>> search_open_xy(int x, int y, Flags flags) {
    const auto wall = [&](int at_x, int at_y) { return (flags(at_x, at_y) & 1) != 0; };
    if (!wall(x, y)) return std::nullopt;
    return nearest_spot(x, y, [&](int at_x, int at_y) { return !wall(at_x, at_y); }, 1, 7);
}

// FUN_0064e810 -> nearest_spot with expfield.d2 (`field`): the free
// subtile nearest (x, y) where something `size` across (FUN_0064d9b0: 0 / 1
// the subtile, 2 the plus, 3 the 3 x 3 box) meets no bit of `mask` in
// `flags(x, y)` (FUN_0064ca50: 0x27 off the rooms), and the field's walk
// from it back to (origin_x, origin_y) meets no bit of `walk_mask`
// (FUN_0066a670).
template <class Flags>
std::optional<std::pair<int, int>> field_spot(const std::vector<std::uint8_t>& field, int origin_x, int origin_y, int x, int y, int size, int mask, int walk_mask, Flags flags) {
    auto hit = [&](int at_x, int at_y, int bits) { const int cell_flags = flags(at_x, at_y); return cell_flags == 0x27 || (cell_flags & bits); };
    auto walk = [&](int at_x, int at_y) {
        static constexpr int kStepX[9] = { 0, 1, 1, 1, 0, -1, -1, -1, 0 }, kStepY[9] = { -1, -1, 0, 1, 1, 1, 0, -1, 0 };
        auto dir = [&] { return field[std::size_t((at_y - origin_y + 128) * 256 + at_x - origin_x + 128)]; };
        if (field.empty()) return true;
        if (hit(at_x, at_y, walk_mask)) return false;
        for (;;) {
            const auto step = dir();
            at_x += kStepX[step]; at_y += kStepY[step];
            if (dir() == 8) return true;
            if (hit(at_x, at_y, walk_mask)) return false;
        }
    };
    auto fits = [&](int at_x, int at_y) {
        if (size <= 1) return !hit(at_x, at_y, mask);
        if (size == 2) return !hit(at_x, at_y, mask) && !hit(at_x - 1, at_y, mask) && !hit(at_x + 1, at_y, mask) && !hit(at_x, at_y - 1, mask) && !hit(at_x, at_y + 1, mask);
        if (size > 3) return false;
        for (int row = -1; row <= 1; ++row)
            for (int column = -1; column <= 1; ++column)
                if (hit(at_x + column, at_y + row, mask)) return false;
        return true;
    };
    return nearest_spot(x, y, [&](int test_x, int test_y) { return fits(test_x, test_y) && walk(test_x, test_y); });
}

// Where a drop at subtile (x, y) lands (FUN_00555da0): from (x + 2, y + 3)
// if that's in a room, else (x, y), field_spot's free subtile (size 1, mask
// 0x3e01, walk 0x801). None found: the start, as FUN_0064dea0 keeps it for
// FUN_00555da0's flag (FUN_0055a550 passes 1), when it is in a room; else
// FUN_00555da0 returns 0 and FUN_0055a550 makes no item: nullopt. (Which
// room FUN_00463740 is asked, drops.md "Where a drop lands": `flags` stands
// for it.)
template <class Flags>
std::optional<std::pair<int, int>> drop_place(const std::vector<std::uint8_t>& field, int x, int y, Flags flags) {
    int at_x = x + 2, at_y = y + 3;
    if (flags(at_x, at_y) == 0x27) { at_x = x; at_y = y; }
    if (const auto spot = field_spot(field, x, y, at_x, at_y, 1, 0x3e01, 0x801, flags)) return spot;
    if (flags(at_x, at_y) == 0x27) return std::nullopt;   // the start has no room (FUN_00463740)
    return std::pair{ at_x, at_y };
}
// drop_place's spot, the dropper's own subtile (x, y) when none was made.
template <class Flags>
std::pair<int, int> drop_spot(const std::vector<std::uint8_t>& field, int x, int y, Flags flags) {
    return drop_place(field, x, y, flags).value_or(std::pair{ x, y });
}

}  // namespace d2d::game
