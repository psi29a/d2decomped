// SPDX-License-Identifier: GPL-3.0-or-later
// The game runs on its own: components/game needs nothing from the client
// (apps/d2d: SDL, sound, sprites, fonts), so a standalone server links
// just this library. With the game's MPQs (D2_MPQ_DIR, as test_outdoor)
// it loads GameData and plays a new character for a second.
#include <character.hpp>
#include <character_store.hpp>
#include <drops.hpp>
#include <gamedata_load.hpp>
#include <item_text.hpp>
#include <monsters.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

using namespace d2d::game;

int main() {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");

    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());
    // A server's GameData keeps the tiles' walk flags, not their pixels.
    for (const auto& archive : data->town.dt1s)
        for (const auto& tile : archive.tiles()) assert(tile.pixels.empty());

    // Every level's monster region, as game.exe makes them at game start
    // (tools/emu regions.py 0x1234 prints the same lines): seed 0x1234, normal.
    std::string regions;
    d2d::rules::Rng region_seed{ d2d::rules::Rng{ 0x1234 }.next() };
    for (std::size_t id = 1; id < data->level_mon.size(); ++id) {
        const auto region = d2d::rules::monster_region(data->monsters, data->level_mon[id], 0, region_seed);
        if (region.types.empty()) continue;
        regions += std::to_string(id);
        for (std::size_t i = 0; i < region.types.size(); ++i) {
            regions += " " + std::to_string(region.types[i].first) + ":";
            for (std::size_t set = 0; set < region.components[i].size(); ++set) {
                if (set) regions += "/";
                for (const auto layer : region.components[i][set]) { char hex[3]; std::snprintf(hex, sizeof hex, "%02x", layer); regions += hex; }
            }
        }
        regions += "\n";
    }
    std::uint64_t hash = 0xcbf29ce484222325;                      // FNV-1a of game.exe's lines
    for (const char letter : regions) hash = (hash ^ std::uint8_t(letter)) * 0x100000001b3;
    if (patch) {                                                  // the 1.14d tables (the CD's differ)
        if (hash != 0x69b556c850c6632eull) std::printf("%s", regions.c_str());
        assert(hash == 0x69b556c850c6632eull);
    }

    // Act 1's drops as game.exe rolls them (tools/emu drops.py act1 prints
    // the same lines): its classes sorted, the jobs of drops.py jobs().
    std::vector<std::string> act1;
    for (const auto& [name, treasure] : data->rules.treasure)
        if (name.starts_with("Act 1 ") || name.starts_with("Andariel") || name.starts_with("Countess") || name.starts_with("Cow")) act1.push_back(name);
    std::sort(act1.begin(), act1.end());
    std::string drops;
    for (std::uint32_t job = 1; job <= 3000; ++job) {
        const std::string& name = act1[job % act1.size()];
        const int level = int(job * 37 % 100), ilvl = int(1 + job * 13 % 99), players = int(1 + job % 8);
        const int magic_find = std::array{ 0, 0, 0, 50, 110, 250, 600, -100 }[job / 8 % 8];
        d2d::rules::Rng seed{ job * 0x9E3779B1u };
        const auto moved = d2d::rules::tc_upgrade(data->rules, name, level);
        std::vector<d2d::rules::Drop> dropped;
        d2d::rules::roll_drops(data->rules, moved, ilvl, seed, dropped, players, magic_find);
        char text[128];
        std::snprintf(text, sizeof text, "%08x %s@%d>%s i%d p%d m%d:", job * 0x9E3779B1u, name.c_str(), level, moved.c_str(), ilvl, players, magic_find);
        drops += text;
        for (const auto& drop : dropped) drops += " " + drop.code + ":" + std::to_string(drop.quality) + (drop.mul ? "*" + std::to_string(drop.mul) : "");
        std::snprintf(text, sizeof text, " -> %08x\n", seed.low);
        drops += text;
    }
    std::uint64_t drops_hash = 0xcbf29ce484222325;
    for (const char letter : drops) drops_hash = (drops_hash ^ std::uint8_t(letter)) * 0x100000001b3;
    if (patch) {
        if (drops_hash != 0x86f78a5cf5373648ull) std::printf("%s%016llx\n", drops.c_str(), (unsigned long long)drops_hash);
        assert(drops_hash == 0x86f78a5cf5373648ull);
    }

    d2d::rules::Rng rng(7);
    constexpr int kBarbarian = 4;
    auto made = new_character(*data, kBarbarian, "Headless", false, true, rng);
    Character character;
    character.character_class = kBarbarian;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    World world(&*data, -1, -1);
    world.enter(character);
    for (std::uint32_t tick = 1; tick <= 25; ++tick) world.tick({}, tick * kTickMs, (tick - 1) * kTickMs);
    const auto view = world.view();
    assert(view.level && view.level->id == 1 && !view.dead);
    assert(!view.level->unit_blocked(view.player.x, view.player.y));
    std::printf("OK: %s in level %d at (%.1f, %.1f)\n", character.name.c_str(), view.level->id, view.player.x, view.player.y);

    // d2d's autoloot (deviations.md #5): gold under the player goes into the purse.
    const auto purse = world.character.stats.get(d2d::d2s::kGold);
    world.loot.put({ .code = "gld", .gold = 7 }, world.player.x, world.player.y, 1, world.fight.spawning.game, 26 * kTickMs);
    std::tie(world.loot.ground.back().x, world.loot.ground.back().y) = std::pair{ world.player.x, world.player.y };
    world.tick({}, 26 * kTickMs, 25 * kTickMs);
    assert(world.character.stats.get(d2d::d2s::kGold) == purse + 7 && world.loot.ground.empty());
    {   // gold find (FUN_005589a0): the coins times (100 + stat 79) / 100
        auto seed = world.fight.spawning.game;
        d2d::rules::Rng unit{ seed.next() };
        const int coins = d2d::rules::gold_amount(10, 0, unit);
        world.loot.put({ .code = "gld" }, world.player.x, world.player.y, 10, world.fight.spawning.game, 27 * kTickMs, false, 50);
        assert(!world.loot.ground.empty() && world.loot.ground.back().gold == coins * 150 / 100);
        world.loot.ground.pop_back();
    }

    // Gear: a set piece's bonus list 0 is on with two of its set worn;
    // charms in the inventory count, ones in the stash don't.
    {
        const auto& sets = data->rules.sets;
        std::size_t first = 0, second = 1;
        for (; first < sets.size(); ++first) {
            for (second = first + 1; second < sets.size() && sets[second].set != sets[first].set; ++second) {}
            if (second < sets.size()) break;
        }
        assert(second < sets.size());
        auto piece = [](std::size_t set_id, int slot) {
            d2d::d2s::Item worn;
            worn.quality = 5; worn.set_id = int(set_id); worn.location = 1; worn.slot = slot;
            return worn;
        };
        std::vector<d2d::d2s::Item> gear{ piece(first, 1), piece(second, 3), {} };
        gear[0].set_props = { { 0, 0, 15 } }; gear[0].set_lists = 1; gear[0].set_list_sizes = { 1 };
        gear[2].code = "cm1"; gear[2].panel = 1; gear[2].props = { { 80, 0, 20 } };
        auto total = [&](int stat) {
            int sum = 0;
            for (const auto& prop : gear_props(*data, gear)) if (prop.stat == stat) sum += prop.value;
            return sum;
        };
        assert(total(0) == 15 && total(80) == 20);
        assert(wearer(*data, 0, gear, character.stats).str == int(character.stats.get(d2d::d2s::kStr)) + 15);
        gear[1].slot = 0; gear[2].panel = 5;                      // one piece worn, the charm stashed
        assert(total(0) == 0 && total(80) == 0);
    }

    // A healer's / well's cure (FUN_00578d30 / FUN_00585720): poison and the
    // curses go, chill stays; a thawing potion's takes the chill.
    auto& fight = world.fight;
    const auto poisoned = [&] { return std::ranges::any_of(fight.regen, &Fight::Regen::poison); };
    fight.regen.push_back({ -1.0, 0, 1000 * kTickMs, true });
    fight.amplified[0] = fight.chilled = 1000 * kTickMs;
    assert(fight.cure(26 * kTickMs) && !poisoned() && fight.amplified[0] == 0 && fight.chill_rate(26 * kTickMs) == -50);
    assert(!fight.cure(26 * kTickMs));
    fight.regen.push_back({ -1.0, 0, 1000 * kTickMs, true });
    fight.potion("yps", 26 * kTickMs);
    fight.potion("wms", 26 * kTickMs);
    assert(!poisoned() && fight.chill_rate(26 * kTickMs) == 0);

    // Tristram Cain (FUN_00593290 -> FUN_005e7880): the Gibbet opened, he
    // comes out, walks off, opens his portal, walks back in: camp Cain due.
    // (The 1.14d tables: the CD's leave Tristram's presets unplaced.)
    if (!patch) return 0;
    const auto* tristram = data->level(d2d::rules::CainQuest::kTristram);
    d2d::game::populate_level(*data, world.fight.spawning, *tristram);   // its rooms up: the Gibbet made
    const auto gibbet = std::ranges::find(tristram->npcs, 10, &Npc::operate_fn);
    const auto cain_npc = std::ranges::find(tristram->npcs, d2d::rules::monster_ids::kCain, &Npc::hc_idx);
    assert(gibbet != tristram->npcs.end() && cain_npc != tristram->npcs.end());
    const auto cain_index = std::size_t(cain_npc - tristram->npcs.begin());
    const auto* camp = world.level;
    world.level = tristram;
    std::tie(world.player.x, world.player.y) = tristram->nearest_free(gibbet->x + 2, gibbet->y + 2);
    world.swap_npcs(camp);
    assert(world.npc_states[cain_index].hidden);
    world.operate(int(gibbet - tristram->npcs.begin()), 26 * kTickMs);
    assert(world.cain_walk.npc == int(cain_index) && !world.cain.camp_due);
    float cain_moved = 0;
    for (std::uint32_t tick = 27; tick < 27 + 750 && !world.cain.camp_due; ++tick) {
        world.tick({}, tick * kTickMs, (tick - 1) * kTickMs);
        const auto& cain_state = world.npc_states[cain_index];
        if (!cain_state.hidden) cain_moved = std::max(cain_moved, std::hypot(cain_state.x - cain_npc->x, cain_state.y - cain_npc->y));
    }
    assert(world.cain.camp_due && world.cain_walk.npc < 0 && world.npc_states[cain_index].hidden && cain_moved > 0.3f);
    // His portal (object 189, FUN_005943b0) at the subtile he first stood
    // on (FUN_005944b0: the Gibbet's x + 3, y + 3), drawn as a town portal.
    const int spot_x = int(std::floor(cain_npc->x * 5)), spot_y = int(std::floor(cain_npc->y * 5));
    assert(spot_x == int(std::floor(gibbet->x * 5)) + 3 && spot_y == int(std::floor(gibbet->y * 5)) + 3);
    assert(world.cain_portal.level == tristram && world.cain_portal.x == (float(spot_x) + 0.5f) / 5 && world.cain_portal.y == (float(spot_y) + 0.5f) / 5);
    const auto cain_view = world.view();
    assert(std::ranges::count_if(cain_view.portals, [&](const auto& shown) { return shown.which == 4 && shown.x == world.cain_portal.x && shown.y == world.cain_portal.y; }) == 1);
    std::printf("OK: Tristram Cain walked %.2f cells and took his portal at subtile (%d, %d)\n", cain_moved, spot_x, spot_y);
    // A spot no room near his holds (FUN_00463740 null): it moves on 3 and
    // no portal opens; he steps in at once, his own spot the target.
    world.cain_portal = {};
    world.npc_states[cain_index].hidden = false;
    world.cain_walk = { .npc = int(cain_index), .stage = 1, .tries = 6, .x = spot_x, .y = spot_y, .spot_x = spot_x + 400, .spot_y = spot_y + 400 };
    for (std::uint32_t tick = 800; tick < 800 + 60 && world.cain_walk.npc >= 0; ++tick) world.tick({}, tick * kTickMs, (tick - 1) * kTickMs);
    assert(world.cain_walk.npc < 0 && world.cain_walk.stage == 3 && world.cain_walk.spot_x == spot_x + 403 && world.cain_walk.spot_y == spot_y + 403 && !world.cain_portal.level);
    std::printf("OK: a spot with no room moves on 3 and opens no portal\n");

    // Hover text (FUN_0048dd90), top line first, for a level 1 Amazon:
    // a plain short sword and cap.
    if (patch) {
        const d2d::rules::Wearer amazon{ 0, 20, 25, 1 };
        auto text = [&](const d2d::d2s::Item& item, const d2d::rules::Wearer* wearer) {
            std::string joined;
            for (const auto& line : item_lines(*data, item, 1, wearer)) joined += line.text + "|";
            return joined;
        };
        d2d::d2s::Item sword{ .code = "ssd" };
        sword.identified = true; sword.quality = 2; sword.durability = sword.max_durability = 24;
        d2d::d2s::Item cap{ .code = "cap" };
        cap.identified = true; cap.quality = 2; cap.defense = 3; cap.durability = cap.max_durability = 12;
        assert(text(sword, &amazon) == "Short Sword|One-Hand Damage: 2 to 7|Durability: 24 of 24|Sword Class - Fast Attack Speed|");
        assert(text(sword, nullptr) == "Short Sword|One-Hand Damage: 2 to 7|Durability: 24 of 24|");   // no player: no speed line
        assert(text(cap, &amazon) == "Cap|Defense: 3|Durability: 12 of 12|");
        // +50% defense: the number blue after "Defense: ".
        cap.props.push_back({ .stat = 16, .value = 50 });
        const auto cap_lines = item_lines(*data, cap, 1, &amazon);
        assert(cap_lines[1].text == "Defense: 4" && cap_lines[1].split == 9 && cap_lines[1].tail == kTxtBlue);
        std::printf("OK: hover text of a short sword and a cap\n");
    }

    // A Blood Moor shrine (FUN_00583c70): stamina (row 14) fills stamina,
    // its message overhead, its sound; a resist (row 8) ends it (one
    // shrine state: stamina filled again, FUN_00583a40) for 3600 frames.
    const auto* moor = data->level(2);
    d2d::game::populate_level(*data, world.fight.spawning, *moor);
    const auto shrine = std::ranges::find(moor->npcs, 2, &Npc::operate_fn);
    assert(shrine != moor->npcs.end());
    const auto shrine_index = int(shrine - moor->npcs.begin());
    world.level = moor;
    world.swap_npcs(tristram);
    auto& stats = world.character.stats.values;
    stats[d2d::d2s::kStamina] = 0;
    world.cues.due.clear();
    world.operate(shrine_index, 900 * kTickMs, 14);
    assert(stats[d2d::d2s::kStamina] == stats[d2d::d2s::kMaxStamina] && world.shrine_code(world.fight.boost.shrine) == 14);
    assert(std::ranges::count(world.cues.due, data->sound_index.at("shrine_recharge"), &Cues::Cue::sound) == 1);
    world.tick({}, 901 * kTickMs, 900 * kTickMs);
    assert(world.npc_states[std::size_t(shrine_index)].says == 3683 + shrine->shrine && world.view().boost_code == 14);
    const int fire = world.fight.player_combat.res[0];
    stats[d2d::d2s::kStamina] = 0;
    world.operate(shrine_index, 902 * kTickMs, 8);
    world.tick({}, 903 * kTickMs, 902 * kTickMs);
    assert(stats[d2d::d2s::kStamina] == stats[d2d::d2s::kMaxStamina] && world.view().boost_code == 8);
    assert(world.fight.player_combat.res[0] == std::min(fire + 75, int(world.character.panel.res_cap[0])) && world.fight.boost.until == (902 + 3600) * kTickMs);
    world.tick({}, (902 + 3600) * kTickMs, (901 + 3600) * kTickMs);
    assert(world.fight.boost.shrine == 0 && world.fight.player_combat.res[0] == fire);
    std::printf("OK: shrines: stamina filled, one state at a time, resist fire for 3600 frames\n");

    // A chest's trap fires at its event 4, 35 frames after it opened
    // (FUN_00582510 -> FUN_005817a0); in act 1 a lightning trap (1) is a
    // firebolt (2).
    const auto missiles_named = [&](std::string_view name) {
        return std::ranges::count_if(world.fight.missiles, [&](const auto& missile) { return missile.info && missile.info->name == name; });
    };
    world.fight.missiles.clear();
    world.arm_trap(1, world.player.x + 1, world.player.y, 1, 4600 * kTickMs);
    world.tick({}, (4600 + 34) * kTickMs, (4600 + 33) * kTickMs);
    assert(world.fight.missiles.empty() && world.traps.size() == 1);
    world.tick({}, (4600 + 35) * kTickMs, (4600 + 34) * kTickMs);
    assert(world.traps.empty() && missiles_named("trapfirebolt") == 1 && missiles_named("chainlightning") == 0);
    std::printf("OK: a chest's trap springs 35 frames on, lightning a firebolt in act 1\n");

    // The Moldy Tome (FUN_00594e70): its message 127 for the player
    // (S->C 0x27); heard back (cmd::QuestMessage), the Forgotten Tower starts.
    const auto* tower_level = data->level(d2d::rules::level_ids::kStonyField);
    d2d::game::populate_level(*data, world.fight.spawning, *tower_level);
    const auto tome = std::ranges::find(tower_level->npcs, d2d::rules::operate_fn::kMoldyTome, &Npc::operate_fn);
    assert(tome != tower_level->npcs.end());
    const int tome_index = int(tome - tower_level->npcs.begin());
    world.level = tower_level;
    world.swap_npcs(moor);
    world.events.clear();
    world.operate(tome_index, 4700 * kTickMs);
    assert(world.tower.state == 0 && world.events.size() == 1);
    const auto* told = std::get_if<d2d::game::ev::Speech>(&world.events[0]);
    assert(told && told->npc == tome_index && told->string == d2d::rules::TowerQuest::kTome);
    world.apply(d2d::game::cmd::QuestMessage{ tome_index, d2d::rules::TowerQuest::kTome }, 4701 * kTickMs);
    assert(world.tower.state == 2);
    std::printf("OK: the Moldy Tome's message 127, heard, starts the Forgotten Tower\n");
    return 0;
}
