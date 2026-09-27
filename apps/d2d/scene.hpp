// GameData (what the rules read: tables, levels, timings, strings) and
// Scene (GameData plus what the client draws and plays), and the
// string-table lookups.
#pragma once

#include "common.hpp"

namespace {

// Things the DS1 places (its object list): NPCs (type 1, from
// data\global\monsters) and objects (type 2 — torches, fires, the
// waypoint..., from data\global\objects). Both are composites.
struct Npc {
    std::string root;                    // "monsters" or "objects"
    std::string code;                    // <root>\<code>\ ...
    std::string mode;                    // animation mode token (NU, ON...)
    std::string base_w;                  // weapon class ("hth" for objects)
    std::array<std::string, 16> comp;    // per layer, "" = not present
    float x = 0, y = 0;
    int size_x = 0, size_y = 0;          // collision footprint, subtiles
    std::string name;                    // hover label; "" = not selectable
    std::vector<std::pair<float, float>> path;   // DS1 patrol points, cells
    float velocity = 3;                  // MonStats Velocity
    int operate_fn = 0;                  // objects.txt OperateFn (32: the town stash)
    int hc_idx = -1;                     // MonStats hcIdx (NPC menu table key)
    std::string id;                      // MonStats Id (npc.txt key)
    int quest = 0;                       // shown once this Act 1 quest is done (Cain: 4), 0 = always
    int op_frames = 0;                   // objects.txt FrameCnt1 when it has an OP mode (Mode1)
    int shrine = 0;                      // a shrine's Shrines.txt row (roll_shrine)
    int trap = 0;                        // a chest's trap type (roll_chest), 0 none
    bool locked = false;                 // a locked chest: takes a key
};

// One level (Levels.txt row): its DS1, the DT1s it references and a
// (style, seq, type) -> tile lookup for the compositor, the walk grid, the
// units the DS1 places, where players arrive, and its sound environment.
// Empty when the assets aren't found (headless / bad data dir).
struct Level {
    int id = 0;                                        // Levels.txt Id (1: the Rogue Encampment)
    std::string name;                                  // Levels.txt LevelName
    int type = 1;                                      // its LevelType (AutoMap.txt rules)
    int world_x = 0, world_y = 0;                      // act tiles of its (0, 0): where it sits in the act
    int layer = 0;                                     // Levels.txt Layer: levels on one share an automap
    int song = 0, ambience = 0;                        // SoundEnviron Song / Day Ambience (Sounds.txt)
    int night_ambience = 0, day_event = 0, night_event = 0, event_delay = 0;   // its Night Ambience, Day / Night Event, Event Delay (sound ticks)
    d2d::ds1::Map ds1;
    std::vector<d2d::dt1::Archive> dt1s;
    // Keyed by (style, seq, type) — one map covers floors, walls, trees,
    // shadows, roofs; the DT1's `type` field disambiguates orientations
    // that share (style, seq). First matching tile wins across DT1s.
    std::unordered_map<std::uint64_t, const d2d::dt1::Tile*> tile_lookup;
    // The DT1 tile game.exe's room pass gives each cell (drlg
    // level_room_tiles, proven against game.exe): layer 0 wall, 1 floor,
    // 2 shadow, in the order the rooms add them. Filled for generated
    // levels; when present it replaces tile_lookup for drawing and walking.
    struct Pick { std::uint8_t layer, orient; const d2d::dt1::Tile* tile; };
    std::vector<std::vector<Pick>> picks;               // ds1 width x height, or empty
    // Walkability: every floor/wall tile's 5x5 subtile flags OR'd onto
    // its cell, (width*5) x (height*5), row-major. 0x01 blocks walking,
    // 0x08 blocks player walking (DT1 subtile flag bits).
    std::vector<std::uint8_t> walk;
    std::vector<Npc> npcs;                             // what its DS1 places (and Cain)
    // The levels next to it in the act, (dx, dy) = their origin minus
    // ours, in cells. Past this map's edge, collision and
    // the renderer use theirs — the way D2 walks and draws across rooms
    // of neighbouring levels.
    struct Near { const Level* level; int dx, dy; };
    mutable std::vector<Near> nearby;                  // linked as neighbours get built (install_level)
    [[nodiscard]] bool inside(float x, float y) const {
        return x >= 0 && y >= 0 && x < float(ds1.width()) && y < float(ds1.height());
    }
    // `mask` 0x09 walls for walkers; 0x04 the missile barrier (the DT1
    // subtile bit missiles stop on; walk-only 0x01 cells, like the Fallen
    // camp's, let them by).
    [[nodiscard]] bool blocked(float x, float y, std::uint8_t mask = 0x09) const {
        if (!inside(x, y))
            for (const auto& n : nearby)
                if (n.level->inside(x - float(n.dx), y - float(n.dy))) return n.level->blocked_here(x - float(n.dx), y - float(n.dy), mask);
        return blocked_here(x, y, mask);
    }
    [[nodiscard]] bool blocked_here(float x, float y, std::uint8_t mask = 0x09) const {
        const int w = ds1.width() * 5, h = ds1.height() * 5;
        const int sx = int(std::floor(x * 5)), sy = int(std::floor(y * 5));
        if (sx < 0 || sy < 0 || sx >= w || sy >= h || walk.empty()) return true;
        return walk[std::size_t(sy) * std::size_t(w) + std::size_t(sx)] & mask;
    }
    // Can a small unit (the player, the merc, NPCs) stand at (x, y)? Its
    // collision pattern is a plus: the subtile and its four neighbours,
    // any of them walls (0x09) blocks — FUN_0064d100, collision pattern
    // 1 (FUN_0064d870). Off the map counts as blocked, like the game's
    // 0x27 outside a room.
    // Units don't stamp themselves in; moving ones block each other through
    // the Crowd (ai.hpp).
    [[nodiscard]] bool unit_blocked(float x, float y) const {
        return blocked(x, y) || blocked(x - 0.2f, y) || blocked(x + 0.2f, y)
            || blocked(x, y - 0.2f) || blocked(x, y + 0.2f);
    }
    // The nearest spot a unit can stand, searching outward a subtile per
    // ring (FUN_0064dea0 does it per room for spawns).
    [[nodiscard]] std::pair<float, float> nearest_free(float x, float y) const {
        if (!unit_blocked(x, y)) return { x, y };
        for (int r = 1; r < 200; ++r)
            for (int i = -r; i <= r; ++i)
                for (auto [ox, oy] : { std::pair{ i, -r }, { i, r }, { -r, i }, { r, i } })
                    if (!unit_blocked(x + float(ox) * 0.2f, y + float(oy) * 0.2f))
                        return { x + float(ox) * 0.2f, y + float(oy) * 0.2f };
        return { x, y };
    }
    std::pair<float, float> start{ -1.f, -1.f };            // cells: where a player joining arrives; see load_world
    // Its warps (the units its hidden warp tiles make): cell, the level
    // it leads to (Levels.txt Vis), and where someone arriving through it
    // stands (its LvlWarp ExitWalk, subtiles from the cell).
    struct Warp { float x, y; int to; float exit_x, exit_y; };
    std::vector<Warp> warps;
    // Its rooms' preset units (drlg level_room_tiles, proven against
    // game.exe): level-relative subtiles; load_npcs / load_monsters make
    // them objects, NPCs and monsters.
    std::vector<d2d::drlg::Unit> units;
    // Monsters: its Levels.txt columns, the rooms the generator made and
    // what populating them spawned (subtiles, level-relative).
    d2d::rules::LevelMon mon;
    std::vector<d2d::drlg::Outdoor::RoomSeed> rooms;
    // What populating it spawns at a difficulty, made the first time it's
    // played at that difficulty (level_spawns), and its monster region's
    // MonStats rows (trap 8).
    mutable std::array<std::optional<std::vector<d2d::rules::Spawn>>, 3> spawns;
    mutable std::array<std::vector<int>, 3> region;
};

// What the game's rules read: the tables, the levels, animation timings,
// string tables, the MPQs. The World (server.hpp) sees only this, so a
// standalone server loads no graphics beyond the levels' tiles.
// ponytail: a Level still carries its DT1s' pixels next to its walk grid.
struct GameData {
    // A mode's timing, all the World needs of an animation: its COF name
    // ("AITW1HS"), animdata.d2's rate (256 = a frame per tick), frames per
    // direction and event frame (the hit), the COF's own rate and frames
    // for modes animdata lacks, and its directions (0: no such mode).
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
    const AnimTiming& npc_timing(const Npc& n, std::string_view mode) const;
    const AnimTiming& composite_timing(int d2s_class, int mode, const std::array<std::uint8_t, 16>& gfx) const;
    // data\global\animdata.d2 by COF name: the game's rate source (COFs of
    // walk/run modes store 0), frames per direction and the event frame.
    struct AnimInfo { std::uint32_t speed = 0, frames = 0; int action = -1; };
    std::unordered_map<std::string, AnimInfo> anim_data;
    using Appearance = std::array<std::uint8_t, 16>;
    std::vector<d2d::compcode::Entry> comp;         // appearance byte -> component
    std::array<Appearance, 7>         starting_gear{};  // per d2s class, CharStats.txt
    // Kept open for lazy loads after startup.
    d2d::mpq::Stack mpqs;
    // True when 1.14d patch data is layered in. Without it patchstring.tbl
    // is the CD's (826 entries), whose IDs don't match what 1.14d code asks
    // for (10832 is "CREATE NEW" in 1.14d, "Bonus to Attack Rating" on CD).
    bool patched = false;
    std::vector<std::int64_t> exp_next;          // experience.txt: exp for level+1, by level
    std::array<std::int64_t, 3> resist_penalty{ 0, -40, -100 };   // DifficultyLevels.txt
    // Items: parse tables (needs 1.14d ItemStatCost.txt), per-code
    // inventory graphic + size, and the 800x600 inventory panel/layouts.
    std::optional<d2d::d2s::ItemTables> item_tables;
    // gems.txt socket bonuses by gem/rune code, per slot kind (weapon,
    // helm/armour, shield), already resolved through Properties.txt.
    std::unordered_map<std::string, std::array<std::vector<d2d::d2s::ItemProp>, 3>> gem_props;
    // String keys for item names, indexed the way the save's IDs are (see
    // item_lines): uniques/sets by row without separators, magic affixes by
    // raw row, rare names by raw row, runewords by RunewordN rank.
    struct ItemNames {
        std::vector<std::string> unique, set, prefix, suffix, rare_pre, rare_suf, runeword;
    } item_names;
    // ItemStatCost.txt description columns, by stat ID, and what the skill
    // descfuncs need: skill name keys by skill ID, CharStats strings by class.
    struct StatDesc {
        int prio = 0, func = 0, val = 0, op = 0, op_param = 0, dgrp = 0, dgrp_func = 0, dgrp_val = 0;
        std::string pos, neg, str2, dgrp_pos, dgrp_neg, dgrp_str2;
    };
    std::vector<StatDesc> stat_desc;
    std::vector<std::string> skill_name;         // string key
    std::vector<int>         skill_class;        // CharStats row, -1 none
    struct ClassStrs { std::string all_skills, tab[3], only; };
    std::array<ClassStrs, 7> class_strs;
    d2d::rules::Tables rules;                    // item/vendor/price tables (components/rules)
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
    // Horadric Cube: inventory.txt "Transmogrify Box2" (3x4), art
    // PANEL\supertransmogrifier (FUN_0048a4b0), same left-panel spot
    // (panel 0xe, 0x48eeca).
    InvLayout cube_layout{};
    std::array<d2d::rules::ClassGains, 7> class_gains{};   // by d2s class (CharStats)
    // A new character's start (CharStats.txt): str, dex, int (energy), vit,
    // stamina, hpadd; item1..10 with their loc and count; StartSkill.
    struct ClassStart {
        int str = 0, dex = 0, ene = 0, vit = 0, stamina = 0, hpadd = 0;
        struct Item { std::string code, loc; int count = 0; };
        std::vector<Item> items;
        std::string start_skill;
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
    Level town;                                        // the Rogue Encampment, built at start
    // Every other level: built from the map seed the first time it's
    // wanted (the same on every machine), then kept. `level` builds it or
    // waits for its build; `want_level` starts one on the builder thread
    // (the levels next to the player's); `poll_levels` takes in finished
    // builds and links outdoor neighbours (Level::nearby). load.hpp.
    mutable std::map<int, std::unique_ptr<Level>> levels;
    const Level* level(int id) const;
    void want_level(int id) const;
    void poll_levels() const;
    // SuperUniques.txt (without its Expansion row): name, MonStats row of
    // its Class, minions.
    struct SuperUnique { std::string name; int type = -1, min_grp = 0, max_grp = 0; std::vector<int> mods; std::array<std::string, 3> tc; };
    std::vector<SuperUnique> superuniques;
    d2d::rules::UMods umods;                           // MonUMod.txt: champion / unique mods and constants
    std::array<std::vector<std::string>, 3> unique_names;   // UniquePrefix / Suffix / Appellation, resolved
    std::array<std::string, 2> unique_formats;         // strings 0x6b9 ("%0 %1"), 0x6ba ("%0 %1 %2")
    std::vector<d2d::rules::ShrineRow> shrines;        // Shrines.txt
    std::vector<std::array<int, 4>> area_level;        // Levels.txt MonLvl1Ex..3Ex, then classic MonLvl1, by Id
    std::uint32_t map_seed = 3;                        // act layout + levels (3: townE1)
    std::vector<d2d::drlg::Placed> act1_layout;        // where act 1's levels sit (act tiles)
    // Mercenary units by hireling.txt Id (the save's merc type): the
    // monster, and the first name key (merc01, merca201, MercX101, ...).
    struct Merc { Npc npc; std::string name_first; };
    std::unordered_map<int, Merc> mercs;
    // Waypoints (docs/research/re/waypoint.md): Levels.txt rows with a
    // Waypoint index, per act in index order; art ui\menu\waygate*.
    struct WaypointLevel { int wp = 0, level = 0; std::string name; };
    std::array<std::vector<WaypointLevel>, 5> waypoint_levels;
    // Sounds.txt by Index: file (under data\global\sfx or, for speech,
    // data\local\sfx) and volume 0..255.
    struct Sound { std::string file; int volume = 255; bool loop = false, music = false; int fade_in = 0, fade_out = 0;
                   int group = 0;                      // Group Size: variants at the following indices
                   std::array<int, 3> block{ -1, -1, -1 }; };   // a song's Block 1..3: cue points, in sample frames
    std::unordered_map<std::string, int> sound_index;   // Sounds.txt Sound -> Index
    // MonSounds.txt by Id (MonStats MonSound): per attack mode (A1, A2) the
    // attack and weapon sounds, their delays in ticks and the attack sound's
    // chance; the get-hit and death sounds and delays.
    struct MonSound {
        std::array<int, 2> attack{}, weapon{}, att_del{}, wea_del{}, att_prb{ 100, 100 };
        int hit = 0, death = 0, hit_del = 0, death_del = 0;
    };
    std::unordered_map<std::string, MonSound> mon_sounds;
    std::vector<Sound> sounds;
    fs::path data_dir;                                  // the MPQs' folder
    // CharStats WalkVelocity / RunVelocity by d2s class. Running adds
    // run*100/walk - 100 to velocitypercent (FUN_00620e80): +50%.
    std::array<int, 7> walk_velocity{ 6, 6, 6, 6, 6, 6, 6 }, run_velocity{ 9, 9, 9, 9, 9, 9, 9 };
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
    struct MissileInfo { std::string name; int vel = 0, range = 0, src_damage = 0, min = 0, max = 0, anim_speed = 16, anim_len = 1;
                         std::string skill; int lev_range = 0, hit_func = 0, hit_par1 = 0;
                         bool to_hit = false, collide_kill = true, pierce = false;
                         // Its own element (a row with no Skill: FUN_0064b100 / 0064b1d0 /
                         // 0064b2a0 by level): Skill::etype order, frze as cold.
                         int etype = -1, emin = 0, emax = 0, hitshift = 8, elen = 0;
                         std::array<int, 5> emin_lev{}, emax_lev{}; std::array<int, 3> elen_lev{};
                         // Its server functions' inputs: pSrvDoFunc (table 0x73c768), Param1..,
                         // sHitPar2, the rows it spawns (SubMissile1, HitSubMissile1).
                         int srv_do = 0, param1 = 0, param2 = 0, hit_par2 = 0;
                         bool next_hit = false; int next_delay = 0;   // NextHit: it strikes a unit again NextDelay frames on
                         std::string sub, hit_sub;
                         int trans = 0;   // Trans (record +0x18d): 1 additive, 2 multiply (FUN_004720xx: draw modes 3 / 4)
                       };
    std::unordered_map<std::string, MissileInfo> missiles;
    std::array<Npc, 2> trap_fires;                         // objects 162 / 160, ON (a chest's traps 5 and 7)
    std::unordered_map<std::string, std::string> thrown;   // a throwing weapon's code: its Missiles.txt row (weapons.txt missiletype)
    std::vector<std::size_t> mon_bin;                  // game.exe's MonStats unit ids -> rows (no Expansion row)
    std::vector<bool> mon_is_npc;                      // by MonStats row: MonStats npc
    // The level builder: its own MPQ handles, the DRLG tables and the
    // tables a build reads. Last, so it's torn down first: a build in
    // flight reads the rest of the Scene.
    struct LevelBuilder;
    std::shared_ptr<LevelBuilder> builder;
};

// All the client draws and plays: GameData, plus the pixels, fonts and
// palettes. The frontend's come from FUN_0042e6d0 (main-menu loader) —
// see docs/research/re/frontend-menu-table.md.
struct Scene : GameData {
    d2d::palette::Palette pal;                // Sky — title/credits palette
    d2d::palette::Palette charselect_pal;     // fechar — char-select/create palette
    d2d::palette::Pl2     sky_pl2;            // Sky PL2 (title logo additive)
    d2d::palette::Pl2     fechar_pl2;         // fechar PL2 (campfire additive)
    d2d::dc6::Sprite      bg;                 // TitleScreen or gameselectscreenEXP
    d2d::dc6::Sprite      logo_static;        // Diablo2.dc6 — 320×151, classic only
    d2d::dc6::Sprite      logo_bl, logo_br;   // D2logoBlack{Left,Right} — silhouettes
    d2d::dc6::Sprite      logo_fl, logo_fr;   // D2logoFire{Left,Right} — animated fire
    d2d::dc6::Sprite      btn_wide;           // WideButtonBlank
    d2d::dc6::Sprite      btn_wide2;          // WideButtonBlank02
    d2d::dc6::Sprite      btn_narrow;         // NarrowButtonBlank
    d2d::dc6::Sprite      btn_short;          // ShortButtonBlank
    d2d::dc6::Sprite      credits_bg;         // creditsbckgexpand.dc6 (or classic fallback)
    // Character-creation screen (loaded by FUN_004326f0). SP button hops here.
    d2d::dc6::Sprite      charcreate_bg;      // charactercreationscreenEXP.dc6
    d2d::dc6::Sprite      fire;               // fire.DC6 — campfire between the classes
    d2d::dc6::Sprite      medium_button;      // MediumButtonBlank.dc6 — char-select OK/EXIT (handle 0x77973c, Sky palette)
    d2d::dc6::Sprite      cinematics_panel;   // FrontEnd\CinematicsSelectionEXP.dc6 (2x2, 326x427)
    d2d::dc6::Sprite      medium_sel_button;  // MediumSelButtonBlank.dc6 — char-create OK/EXIT chrome (per FUN_004326f0)
    d2d::dc6::Sprite      textbox;            // textbox.dc6 — name-entry chrome (single 169×26 frame)
    d2d::dc6::Sprite      clickbox;           // clickbox.dc6 — Hardcore checkbox chrome (2 frames × 15×16, unchecked/checked)
    // Character-select screen assets (RE FUN_004359d0, handle 0x00779734).
    // Slot chrome is 2-frame 256+16 wide × 93 tall (matches WideButton
    // composite pattern). BG is 12-frame 4×3 grid of ≤256×256 tiles.
    d2d::dc6::Sprite      charselect_bg;      // characterselectscreenEXP.dc6
    d2d::dc6::Sprite      charselect_box;     // charselectbox.dc6 (filled slot)
    d2d::dc6::Sprite      charselect_scroll;  // FrontEnd\joingamescrollbars.dc6
    d2d::dc6::Sprite      tall_button;        // TallButtonBlank.dc6 (168×60) — CREATE / DELETE
    d2d::dc6::Sprite      cursor;             // CURSOR\ohand.dc6 — D2's gauntlet, 8 frames
    // Character composites (in-game player, char-select portraits). The
    // COF names the body-part layers and their per-frame draw order; each
    // layer is its own DCC, indexed by COF composite type (0 HD, 1 TR,
    // 2 LG, 3 RA, 4 LA, 5 RH, 6 LH, 7 SH, 8.. S1..S8). What each layer
    // wears comes from 16 appearance bytes — a .d2s header's, or a new
    // character's starting gear — through `comp` (components/compcode).
    struct PlayerAnim : AnimTiming {
        d2d::cof::Cof                     cof;
        // Each layer's DCC file, decoded when it's first drawn: the World
        // reads only the COF and timings (a mode's length, its hit frame).
        mutable std::array<std::vector<std::byte>, 16> dcc;
        mutable std::array<d2d::dcc::Sprite, 16>       decoded;
        [[nodiscard]] const d2d::dcc::Sprite& layer(std::size_t t) const {
            if (!dcc[t].empty()) {
                try { decoded[t] = d2d::dcc::Sprite(dcc[t]); } catch (const std::exception& e) { d2d::log::warn("{} layer {}: {}", name, t, e.what()); }
                dcc[t] = {};
            }
            return decoded[t];
        }
    };
    // Loaded on first use and kept — decoding every composite up front
    // doubled startup. `mutable` so the const Scene renderers can fill it.
    mutable std::map<std::array<std::uint8_t, 18>, PlayerAnim> composites;
    // Class animations — 7 classes × 5 states, per the RE'd class table at
    // 0x00708a00. State order matches D2's suffix scheme: nu1, nu2, fw,
    // nu3, bw. Class order (rows in the table): assassin, druid, amazon,
    // necromancer, barbarian, sorceress, paladin — but we store them in
    // our left-to-right visual order (barb/necro/pally/ama/sorc/druid/assn)
    // to match Scene::class positions.
    std::array<std::array<d2d::dc6::Sprite, 5>, 7> class_anims;
    d2d::font::Font       font;
    d2d::font::Font       font_small;         // font8 — long panel values
    d2d::font::Font       font_tiny;          // font6 — panel labels
    // Credits.txt / ExpansionCredits.txt parsed to plain Latin-1 lines.
    // A '*' prefix on a line marks a section header in D2's format.
    std::vector<std::string> credits;
    // Character saves from <user dir>/save/*.d2s, most recently played first,
    // with each save's items (empty if they couldn't be parsed).
    std::vector<d2d::d2s::Header> saves;
    std::vector<std::vector<d2d::d2s::Item>> save_items;
    std::vector<d2d::d2s::Stats> save_stats;
    mutable std::unordered_map<std::string, std::optional<d2d::dc6::Sprite>> item_sprites;
    mutable std::unordered_map<std::string, std::optional<d2d::dc6::Sprite>> flippy_sprites;
    const d2d::dc6::Sprite* flippy(const std::string& code) const;   // an item code's ground animation
    // An item's inventory graphic: the unique's/set item's own invfile,
    // else the picture variant (ItemTypes InvGfx<n>), else the base's.
    const d2d::dc6::Sprite* item_sprite(const d2d::d2s::Item& it) const;
    std::vector<std::string> unique_inv, set_inv;             // invfile, rows as item_names
    d2d::dc6::Sprite popbelt;                          // PANEL\ctrlpnl_popbelt
    std::unordered_map<std::string, std::array<std::string, 6>> type_invgfx;
    std::array<d2d::dc6::Sprite, 2> stash_panel;
    d2d::dc6::Sprite cube_panel;
    d2d::dc6::Sprite inv_panel;                       // PANEL\invchar6.dc6
    // Char panel stat buttons (docs/research/re/char-panel.md): PANEL\level
    // (frame 1 pressed) on PANEL\levelsocket, the PANEL\skillpoints box.
    d2d::dc6::Sprite level_button, level_socket, points_box;
    // Skill tree (docs/research/re/skill-tree.md): SPELLS\skltree_<c>_back
    // (frames 0..3 the panel, 4t..4t+3 tab t on top) and <Cl>Skillicon.
    std::array<d2d::dc6::Sprite, 7> skill_tree_bg, skill_icons;   // by d2s class
    d2d::dc6::Sprite generic_skill_icons;              // SPELLS\Skillicon: Attack and the other non-class skills
    d2d::dc6::Sprite ctrl_panel, globes, globe_glass; // 800ctrlpnl7 / hlthmana / overlap
    int                   bg_tiles_across{4};

    // ACT1 palette — the actual town palette (fechar/sky are frontend-only).
    d2d::palette::Palette                    act1_pal;
    d2d::dc6::Sprite focus16;                          // UI\CURSOR\focus16: menu hover marks
    d2d::font::Font  font_formal11;                    // FontFormal11: NPC speech (font id 8)
    // Automap: AutoMap.txt resolved like FUN_0061fcf0 — LevelName through
    // game.exe's level-type names (0x6e7d50: "None", "1 Town", ...),
    // TileName through its orientation names (0x6e7f90: fl wl wr wtlr
    // wtll wtr wbl wbr wld wrd wle wre co sh tr rf ld rd fd fi), Style =
    // main index, Start/EndSequence = sub-index range, the valid Cel1..4
    // (FUN_0061fff0 picks one at random). Act 1 draws MaxiMap.dc6.
    struct AutomapRule { int level_type = 0, orientation = 0, main = -1, sub0 = -1, sub1 = -1; std::vector<int> cels; };
    d2d::dc6::Sprite store_panel, store_tabs, store_buttons;   // PANEL\buysell, buyselltabs, buysellbtn
    d2d::dc6::Sprite gold_coin;                                   // PANEL\goldcoinbtn
    d2d::dc6::Sprite wp_bg, wp_icons;
    std::array<d2d::dc6::Sprite, 2> wp_tabs;                      // [expansion]: waygatetabs / expwaygatetabs
    d2d::dc6::Sprite automap_cels;                     // UI\AutoMap\MaxiMap.dc6
    std::unordered_map<std::string, d2d::dcc::Sprite> missile_cels;   // a missile's CelFile DCC, by Missiles.txt row name
    mutable std::map<std::string, PlayerAnim> npc_anims;   // by root/code/mode/components
    const PlayerAnim& npc_anim(const Npc& n, std::string_view mode) const;
    const PlayerAnim& composite(int d2s_class, int mode, const Appearance& gfx) const;
    std::vector<AutomapRule> automap_rules;

};

// D2 TBL values are UTF-16; our font is Latin-1. Downcast char by char.
std::string u16_to_latin1(std::u16string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char16_t c : s) {
        // Keep printable Latin-1 (0x20..0xFF) and line breaks (two-line
        // labels like "Fire\nResistance"), drop the rest — D2 UI strings
        // are ASCII with occasional accented chars, all inside Latin-1.
        if ((c >= 0x20 && c <= 0xFF) || c == '\n') out.push_back(char(c));
    }
    return out;
}


// TBL lookup with D2's precedence: patch → expansion → base. First-hit wins,
// matching how the game resolves any string ID/key at runtime.
inline std::optional<std::u16string_view>
lookup_string(const GameData& s, std::string_view key) {
    if (auto v = s.patch_strings.get(key); v && !v->empty()) return v;
    if (auto v = s.exp_strings.get(key);   v && !v->empty()) return v;
    if (auto v = s.strings.get(key);       v && !v->empty()) return v;
    return std::nullopt;
}
inline std::optional<std::u16string_view>
lookup_string(const GameData& s, std::uint16_t id) {
    // Numeric IDs are banked, not layered (RE'd from char-select: 0x58cb =
    // 22731 resolves to expansionstring[2731] "EXPANSION CHARACTER"):
    //   0..9999 string.tbl, 10000..19999 patchstring, 20000+ expansionstring.
    // Trying every table with the raw ID hits the wrong one — string.tbl
    // 2731 is "Bile".
    if (id >= 10000 && id < 20000 && !s.patched) return std::nullopt;
    const auto& t = id >= 20000 ? s.exp_strings : id >= 10000 ? s.patch_strings : s.strings;
    const auto local = std::uint16_t(id >= 20000 ? id - 20000 : id >= 10000 ? id - 10000 : id);
    if (auto v = t.get(local); v && !v->empty()) return v;
    return std::nullopt;
}

}  // namespace
