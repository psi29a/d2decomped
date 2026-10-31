// SPDX-License-Identifier: GPL-3.0-or-later
// The World (world.hpp): the quest hooks every act uses: talks, bosses' deaths, Cain, the
// Cube. The rest is in this directory.
#include "world.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "log.hpp"

#include <cube.hpp>
#include <d2s_items.hpp>
#include <drops.hpp>
#include <level_ids.hpp>
#include <monster_ids.hpp>
#include <monsters.hpp>
#include <object_ids.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <sound_ids.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::game {

// FUN_00463740: whether subtile (x, y) lies in the room holding subtile
// (from_x, from_y) or in one of that room's near list (FUN_00619790).
// ponytail: rooms as gamedata.cpp's room_rects has them (a preset level's
// as 8x8 tiles), the near list as its closeness (FUN_0066bc20: under 6
// tiles apart on both axes); the level's own rooms only. Rooms are agent
// D's (units off a level's rooms).
static bool room_near_holds(const Level& level, int from_x, int from_y, int x, int y) {
    struct Rect { int x, y, width, height; };
    std::vector<Rect> rects;
    for (const auto& room : level.rooms) rects.push_back({ room.x, room.y, room.width, room.height });
    if (rects.empty())
        for (int top = 0; top < level.ds1.height(); top += 8)
            for (int left = 0; left < level.ds1.width(); left += 8) rects.push_back({ left, top, std::min(8, level.ds1.width() - left), std::min(8, level.ds1.height() - top) });
    const auto holds = [](const Rect& rect, int sub_x, int sub_y) {
        return sub_x >= rect.x * 5 && sub_y >= rect.y * 5 && sub_x < (rect.x + rect.width) * 5 && sub_y < (rect.y + rect.height) * 5;
    };
    const auto own = std::ranges::find_if(rects, [&](const Rect& rect) { return holds(rect, from_x, from_y); });
    if (own == rects.end()) return false;
    return std::ranges::any_of(rects, [&](const Rect& rect) {
        const int gap_x = own->x < rect.x ? rect.x - own->width - own->x : own->x - rect.width - rect.x;
        const int gap_y = own->y < rect.y ? rect.y - own->height - own->y : own->y - rect.height - rect.y;
        return gap_x < 6 && gap_y < 6 && holds(rect, x, y);
    });
}

auto World::quest_talk(int hc_idx, bool fresh) -> std::vector<d2d::rules::QuestMsg> {
        if (hosted_quests) return {};
        std::vector<d2d::rules::QuestMsg> out;
        act1_talk(out, hc_idx, fresh);
        act2_talk(out, hc_idx, fresh);
        act3_talk(out, hc_idx);
        act4_talk(out, hc_idx);
        act5_talk(out, hc_idx);
        return out;
    }

auto World::holding_malus() const -> bool {
        return carries("hdm");
    }

// The malus stand (OperateFn 21, FUN_00591ac0): at clvl 8 the malus drops
// from it (FUN_00559a30) and it's used (mode 2); too low, "I can't"
// (FUN_00553380).
// ponytail: the refusal's a log line, not the class's voice; the stand
// starts NU where game.exe's InitFn 15 has it ON with the quest off.
auto World::malus_stand(int npc_index, std::uint32_t now_ms) -> void {
        const auto& object = level->npcs[std::size_t(npc_index)];
        const auto stand = tools.operate(quests(), int(actor->character.stats.get(d2d::d2s::kLevel)));
        if (stand == d2d::rules::ToolsQuest::Stand::refuse) d2d::log::info("I can't use this yet.");
        if (stand == d2d::rules::ToolsQuest::Stand::drop || !tools.active) operated.try_emplace({ level, npc_index }, now_ms);
        if (stand != d2d::rules::ToolsQuest::Stand::drop) return;
        each_player([&](Player&) { tools.mark(quests()); });   // every player's flags for state 4 (FUN_005537d0 -> 0x591340)
        const auto& area_levels = game_data->area_level;
        const int alvl = std::size_t(level->id) < area_levels.size() ? area_levels[std::size_t(level->id)][std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))] : 1;
        loot.put({ .code = "hdm" }, object.x, object.y, alvl, fight.spawning.game, now_ms);
        d2d::log::info("Tools of the Trade: the Horadric Malus");
    }

// Andariel's death hook (FUN_005965a0): the quest's bits; the player's
// kill for it opens Act 2 (the save's progression, FUN_00538680) and
// drops two chipped gems and a standard one (0x7361dc / 0x736444).
// ponytail: loot's rng, not the game's quest rng (game+0x10f4); quests:
// agent A's.
auto World::andariel_died(const Fight::Kill& kill, std::uint32_t now_ms) -> void {
        namespace quest = d2d::rules::quest;
        constexpr int kQuest = d2d::rules::AndyQuest::kQuest;
        const bool first = kill.killer != 0 && andy.killed(quests());   // the killer's (FUN_00596210); a monster's kill none
        if (kill.killer == 0) { andy.state = 4; andy.after_kill = 1; }
        if (first) {
            open_act(1);
            for (const auto* codes : { d2d::rules::AndyQuest::kChipped, d2d::rules::AndyQuest::kChipped, d2d::rules::AndyQuest::kStandard })
                loot.put({ codes[loot.rng.next() % 7] }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms);
        }
        // Every player (FUN_005965a0's passes): the news lists for those in her
        // lair (FUN_00596260); the killer's party in Act 1, neither done nor
        // due, the lists and the killer's share (0x596440 -> FUN_00596320);
        // anyone else neither done nor due bit 14 (0x5963e0); each with bits 1
        // and 13 their act1_complete_andariel (0x596170, unit event 0x21).
        const bool killer_took_part = quest::bit(quests(), kQuest, 13);
        each_player([&](Player& player) { if (level->id == d2d::rules::level_ids::kCatacombsLevel4) andy.in_lair(quests(), player.id); });
        const int killer = actor->id;
        each_player([&](Player& player) {
            auto& bits = quests();
            if (player.id == killer || !killer_took_part || act() != 0 || quest::bit(bits, kQuest, 0) || quest::bit(bits, kQuest, 1)) return;
            andy.cain.add(player.id); andy.akara.add(player.id); andy.kashya.add(player.id);
            quest::set(bits, kQuest, 13); quest::set(bits, kQuest, 1);
            open_act(1);
        });
        each_player([&](Player&) {
            auto& bits = quests();
            if (!quest::bit(bits, kQuest, 0) && !quest::bit(bits, kQuest, 1)) quest::set(bits, kQuest, 14);
            if (quest::bit(bits, kQuest, 1) && quest::bit(bits, kQuest, 13)) class_cue("act1_complete_andariel", now_ms);
        });
        if (!first) return;
        music_cue("music_quest_andariel");
        d2d::log::info("Sisters to the Slaughter: Andariel is dead, return to Warriv");
    }

// Mephisto's death hook (a3q6, FUN_005bc8b0: record slot +0xc0). The
// killer without word 0x16 bit 0, and each other player in the Durance's
// third level without bits 0 and 0xb (FUN_005bc190), gets bits 0xd, 0 and
// 0xb and progression diff x 4 (5 in LoD) + 3 (FUN_005bc140 ->
// FUN_00538680); each that hadn't bit 0xb owes a Mephisto's Soulstone
// (record +0x1c): that many "mss " drop from him (FUN_00559a30,
// Loot::put's drop_spot). The record's state goes to 2;
// the hellgate (InitFn 44 put its id at +4) to mode 1, its event 1
// FrameCnt1 + 1 frames on taking it to mode 2 (FUN_00581490).
auto World::mephisto_died(const Fight::Kill& kill, std::uint32_t now_ms) -> void {
        each_player([&](Player& player) { guardian.killed(player.id); });   // the killer and each player in the level: one level in co-op
        class_cue("act3_complete_guardian", now_ms);   // FUN_005bc8b0: unit event 0x42
        music_cue("music_quest_mephisto");
        bridge_check = now_ms + 24 * kTickMs;
        int owed = 0;
        const int killer = actor->id;
        each_player([&](Player& player) {
            auto& bits = quests();
            const bool had_stone = d2d::rules::quest::bit(bits, 0x16, 0xb);
            if (d2d::rules::quest::bit(bits, 0x16, 0)) return;
            if (player.id != killer && (level->id != d2d::rules::level_ids::kDuranceOfHateLevel3 || had_stone)) return;
            for (const int bit : { 0xd, 0, 0xb }) d2d::rules::quest::set(bits, 0x16, bit);
            open_act(3);
            owed += had_stone ? 0 : 1;
        });
        for (int stone = 0; stone < owed; ++stone) loot.put({ .code = "mss" }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms);
        if (const Level* durance = game_data->level(d2d::rules::level_ids::kDuranceOfHateLevel3)) {
            const auto gate = std::ranges::find(durance->npcs, d2d::rules::object_ids::kHellgate, &Npc::object_id);
            if (gate != durance->npcs.end()) doors[{ durance, int(gate - durance->npcs.begin()) }] = Door{ 1, now_ms };
        }
        d2d::log::info("The Guardian: Mephisto is dead, the hellgate opens");
    }

// The hellgate's event 1 (mode 1 -> 2 at FrameCnt1 + 1 frames) and the
// bridge's event 7 (FUN_005bcac0, every 24 frames): with Mephisto dead and
// a player within 18 of it (FUN_00641530), mode 0 -> 1 (it rises), and at
// the next 1 -> 2, its collision lifted (FUN_00623830: no HasCollision2).
// ponytail: the bridge's checks run while the player's on its level.
auto World::mephisto_bridge(std::uint32_t now_ms) -> void {
        if (!guardian.dead || level->id != d2d::rules::level_ids::kDuranceOfHateLevel3) return;
        for (std::size_t i = 0; i < level->npcs.size(); ++i) {
            const auto& gate = level->npcs[i];
            if (gate.object_id == d2d::rules::object_ids::kHellgate && !doors.contains({ level, int(i) })) doors[{ level, int(i) }] = Door{ 2, now_ms };   // InitFn 44: the record's 2 at a join
            const auto state = doors.find({ level, int(i) });
            if (gate.object_id == d2d::rules::object_ids::kHellgate && state != doors.end() && state->second.mode == 1
                && now_ms - state->second.when >= std::uint32_t(gate.op_frames + 1) * kTickMs)
                state->second = Door{ 2, now_ms };
        }
        if (now_ms < bridge_check) return;
        bridge_check = now_ms + 24 * kTickMs;
        for (std::size_t i = 0; i < level->npcs.size(); ++i) {
            const auto& bridge = level->npcs[i];
            if (bridge.object_id != d2d::rules::object_ids::kMephistoBridge) continue;
            auto& state = doors.try_emplace({ level, int(i) }, Door{ 0, 0 }).first->second;
            const int off_x = int(std::floor(actor->player.x * 5)) - int(std::floor(bridge.x * 5)), off_y = int(std::floor(actor->player.y * 5)) - int(std::floor(bridge.y * 5));
            if (state.mode == 2 || d2d::rules::unit_distance(off_x, off_y, 2, bridge.size_x) >= 18) continue;
            state = Door{ state.mode + 1, now_ms };
            if (state.mode == 2) set_footprint(*level, bridge, false);
            d2d::log::info("The Guardian: the bridge {}", state.mode == 1 ? "rises" : "is up");
        }
    }

// Blood Raven's death hook (FUN_00590ec0): each player near the killer
// (FUN_00590c40: in its room or one next to it) takes part, then their
// party in Act 1 (LAB_00590e70 -> FUN_00590d60), the rest heard of it
// (LAB_00590dd0: bit 14); each with bit 13 says act1_complete_burial
// (LAB_00590e30, voice 0x22).
auto World::blood_raven_died(std::uint32_t now_ms) -> void {
        if (!burial.open) return;
        const float killer_x = actor->player.x, killer_y = actor->player.y;
        const auto by_kill = [&](const Player& player) { return level->id == d2d::rules::BurialQuest::kBurial && near_room(killer_x, killer_y, player.player.x, player.player.y); };
        kill_credit(d2d::rules::BurialQuest::kQuest, 0, { 13, 1 }, by_kill, [&](Player&) { burial.killed(quests(), true); }, [&](Player&) { burial.killed(quests(), false); });
        bool any = false;
        each_player([&](Player&) { if (d2d::rules::quest::bit(quests(), d2d::rules::BurialQuest::kQuest, 13)) { class_cue("act1_complete_burial", now_ms); any = true; } });
        if (!any) return;
        music_cue("music_quest_bloodraven");
        d2d::log::info("Sisters' Burial Grounds: Blood Raven is dead, return to Kashya");
    }

// The Cube's Transmute (FUN_005665f0 / FUN_00565ab0, rules/cube.hpp): the
// first recipe the Cube's items answer; what it makes; every item in the
// Cube gone (FUN_00564f30); the new ones into the Cube (FUN_00560200:
// rules::free_spot, a player's best fit), else at the feet. The Horadric
// Staff made: its quest's bit 11 (FUN_0059e5c0).
// ponytail: the transmute's sound.
auto World::transmute(std::uint32_t now_ms) -> void {
        using d2d::d2s::Item;
        std::vector<const Item*> cube;
        for (const auto& item : actor->character.items)
            if (item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kCube) cube.push_back(&item);
        d2d::rules::CubeMatch match;
        const int diff = actor->character.header.active_difficulty();
        const int row = d2d::rules::cube_recipe(game_data->rules, game_data->cube_recipes, cube, diff, actor->character.expansion, actor->character.header.cls, match);
        if (row < 0) { d2d::log::info("transmute: nothing ({} items)", cube.size()); return; }
        cues.cue(d2d::rules::sound_ids::kCursorConvertReady, now_ms, actor->player.x, actor->player.y);
        const auto& recipe = game_data->cube_recipes[std::size_t(row)];
        // A portal (output kind 1..3, FUN_00565ab0's table 0x6e11c8): made, the
        // inputs go; refused, they stay (rules::CainQuest::cow_portal_opens).
        if (const int kind = recipe.outputs[0].kind; kind >= d2d::rules::cube_out::kCowPortal && kind <= d2d::rules::cube_out::kFinale) {
            const Level* farm = game_data->level(d2d::rules::level_ids::kMooMooFarm);
            if (kind != d2d::rules::cube_out::kCowPortal || !farm || farm->ds1.width() == 0
                || !d2d::rules::CainQuest::cow_portal_opens(quests(), cow_portal_opened, actor->character.expansion, level->id)) {
                d2d::log::info("transmute: {} refused", recipe.description);
                return;
            }
            // FUN_00594140: a spot by the player (FUN_00545340: size 3, mask
            // 0x400, 100 rings, Level::free_area), then FUN_0056d130's own
            // (portal_spot) from it; none: refused. Its twin at the farm's
            // portal spot (FUN_0056cf40).
            const auto by_player = level->free_area(int(std::floor(actor->player.x * 5)), int(std::floor(actor->player.y * 5)), 3, 0x400, 100);
            const auto opened_at = by_player ? portal_spot(*level, (float(by_player->first) + 0.5f) / 5, (float(by_player->second) + 0.5f) / 5) : std::nullopt;
            if (!opened_at) { d2d::log::info("transmute: {} refused (no room)", recipe.description); return; }
            cow_portal_opened = true;
            const auto [spot_x, spot_y] = *opened_at;
            portal[kCowPortal] = { level, spot_x, spot_y, now_ms };
            const auto [to_x, to_y] = farm->portal_spot.first >= 0 ? farm->nearest_free(farm->portal_spot.first, farm->portal_spot.second)
                                                                  : farm->nearest_free(float(farm->ds1.width()) / 2, float(farm->ds1.height()) / 2);
            portal[kCowPortal + 1] = { farm, to_x, to_y, now_ms };
            std::erase_if(actor->character.items, [](const Item& item) { return item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kCube; });
            cues.cue("object_townportal", now_ms, spot_x, spot_y);
            d2d::log::info("transmute: the Cow Portal opens");
            return;
        }
        auto made = d2d::rules::cube_make(game_data->rules, recipe, match, cube, int(actor->character.stats.get(d2d::d2s::kLevel)), actor->character.expansion, diff,
                                          fight.spawning.game, &loot.found_uniques);
        if (!made.made) { d2d::log::info("transmute: {} made nothing", recipe.description); return; }
        cues.cue(d2d::rules::sound_ids::kCursorConvertItem, now_ms, actor->player.x, actor->player.y);
        std::erase_if(actor->character.items, [](const Item& item) { return item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kCube; });
        const auto& layout = game_data->cube_layout;
        for (auto& item : made.items) {
            if (item.code == "hst") {             // FUN_0059e5c0 -> FUN_0059b660
                music_cue("music_quest_horadric");
                d2d::rules::quest::set(quests(), d2d::rules::HoradricStaffQuest::kQuest, 11);
                staff.staff_made = true;
                arcane.staff_made(quests());
            }
            std::vector<const Item*> placed;
            for (const auto& other : actor->character.items)
                if (other.location == d2d::d2s::item_location::kStored && other.panel == d2d::d2s::item_panel::kCube) placed.push_back(&other);
            const auto [width, height] = d2d::rules::item_size(game_data->rules, item.code);
            std::optional<Item> held_item = std::move(item);
            held_item->id = -1;
            if (const auto [column, cell_row] = d2d::rules::free_spot(game_data->rules, placed, layout.cols, layout.rows, width, height, true);
                column < 0 || !d2d::rules::put_in_grid(game_data->rules, actor->character.items, held_item, d2d::d2s::item_panel::kCube, layout.cols, layout.rows, column, cell_row))
                loot.place(std::move(*held_item), actor->player.x, actor->player.y, now_ms);
        }
        d2d::log::info("transmute: {} ({} made)", recipe.description, made.items.size());
    }

// The way down to Duriel (FUN_0059d870): object 100 13 subtiles left of the
// orifice and 3 down (FUN_00555230), in the true tomb.
auto World::durielportal_open(std::uint32_t now_ms) -> void {
        const Level* tomb = game_data->level(tombs.true_tomb);
        if (!tomb) return;
        const auto orifice = std::ranges::find(tomb->npcs, d2d::rules::operate_fn::kOrifice, &Npc::operate_fn);
        if (orifice == tomb->npcs.end()) return;
        portal[6] = { tomb, orifice->x - 13.f / 5, orifice->y + 3.f / 5, now_ms };
        d2d::log::info("The Seven Tombs: the way down to Duriel opens");
    }

// The way to the Canyon of the Magi (FUN_0056d130: object 60 to level 46):
// by the player (FUN_00545340's search: the nearest free spot), its twin at
// the Canyon's portal spot.
auto World::canyon_portal(std::uint32_t now_ms) -> void {
        const Level* canyon = game_data->level(d2d::rules::ArcaneQuest::kCanyon);
        const auto [spot_x, spot_y] = level->nearest_free(actor->player.x, actor->player.y + 0.6f);
        portal[4] = { level, spot_x, spot_y, now_ms };
        if (canyon && canyon->ds1.width() > 0) {
            const auto [to_x, to_y] = canyon->portal_spot.first >= 0 ? canyon->nearest_free(canyon->portal_spot.first, canyon->portal_spot.second)
                                                                    : canyon->nearest_free(float(canyon->ds1.width()) / 2, float(canyon->ds1.height()) / 2);
            portal[5] = { canyon, to_x, to_y, now_ms };
        }
        cues.cue("object_townportal", now_ms, spot_x, spot_y);
        d2d::log::info("The Arcane Sanctuary: the way to the Canyon of the Magi");
    }

// Radament's death hook (FUN_00599020): each player near the killer
// (FUN_00598e20: in its room or one next to it) takes part (13, 1, 5),
// then their party in Act 2 (FUN_00598d40), the rest heard of it
// (LAB_00598dc0); a book each for those due one without one drops at him
// (LAB_00598fa0, FUN_00559a30).
auto World::radament_died(const Fight::Kill& kill, std::uint32_t now_ms) -> void {
        if (!radament.open) return;
        const float killer_x = actor->player.x, killer_y = actor->player.y;
        int books = 0;
        bool any = false;
        const auto by_kill = [&](const Player& player) { return level->id == d2d::rules::RadamentQuest::kLair && near_room(killer_x, killer_y, player.player.x, player.player.y); };
        const auto credit = [&](bool took_part) {
            const auto result = radament.killed(quests(), game_bits, took_part, carries("ass"));
            books += result.books;
            if (result.took_part) { class_cue("act2_complete_radament", now_ms); any = true; }
        };
        kill_credit(d2d::rules::RadamentQuest::kQuest, 1, { 13, 1, 5 }, by_kill, [&](Player&) { credit(true); }, [&](Player&) { credit(false); });
        for (int i = 0; i < books; ++i) loot.put({ .code = "ass" }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms);
        if (any) music_cue("music_quest_radament");
        if (radament.state == 4) d2d::log::info("Radament's Lair: Radament is dead, return to Atma");
    }

auto World::near_room(float from_x, float from_y, float x, float y) const -> bool {
        const auto subtile = [](float cell) { return int(std::floor(cell * 5)); };
        return room_near_holds(*level, subtile(from_x), subtile(from_y), subtile(x), subtile(y));
    }

auto World::hire_list(int list_act) -> std::vector<d2d::rules::HireEntry>& {
        auto& entries = hire_lists[std::size_t(list_act)];
        if (std::ranges::none_of(entries, d2d::rules::for_hire))      // FUN_00577010 -> FUN_00576070
            entries = d2d::rules::hire_entries(d2d::rules::hire_names(game_data->rules, actor->character.expansion, list_act, actor->character.header.active_difficulty()),
                                               fight.spawning.proxy);
        return entries;
    }

auto World::hire_offers(int list_act) const -> std::vector<d2d::rules::MercOffer> {
        std::vector<d2d::rules::MercOffer> offers;
        const int clvl = int(actor->character.stats.get(d2d::d2s::kLevel));
        for (const auto& entry : hire_lists[std::size_t(list_act)])
            if (d2d::rules::for_hire(entry))
                if (auto offer = d2d::rules::merc_offer(game_data->rules, actor->character.expansion, list_act, actor->character.header.active_difficulty(), clvl, entry)) offers.push_back(*offer);
        return offers;
    }

// Kashya's reward (FUN_00579180): the first offer of her hire list as the
// merc, free — none if there's a merc already (LoD: even a dead one).
// The offer leaves the list (packet 0x50 subtype 2).
// ponytail: an unopened list is rolled here (game.exe: the client asks
// for it as her menu opens: waits for networking).
auto World::kashya_merc() -> void {
        const auto& header = actor->character.header;
        if (header.merc_seed && (actor->character.expansion || !header.merc_dead)) return;
        auto& entries = hire_list(0);
        const auto offers = hire_offers(0);
        const auto first = std::ranges::find_if(entries, d2d::rules::for_hire);
        if (offers.empty() || first == entries.end()) return;
        auto offer = offers.front();
        offer.cost = 0;
        d2d::rules::hire(offer, actor->character.header, actor->character.stats);
        first->hired = true;
        spawn_merc();
        d2d::log::info("Sisters' Burial Grounds: Kashya's reward, a mercenary");
    }

// The Countess's death hook (FUN_00595710): each player in cellar 5 takes
// part (FUN_00594f10: bits 13 and 0, their complete_tower line, unit event
// 0x25), the rest bit 14; then the party of any who took part, anywhere in
// Act 1 and lacking bit 0, bits 13 and 0 (LAB_005953d0 -> LAB_00595370).
// Her drop is her SuperUniques TC's (negative picks: an item and a rune roll).
auto World::countess_died(std::uint32_t now_ms) -> void {
        bool quest_kill = false;
        each_player([&](Player& player) {
            if (!tower.killed(quests(), level->id == d2d::rules::TowerQuest::kCellar, player.id)) return;
            class_cue("act1_complete_tower", now_ms);
            quest_kill = true;
        });
        if (quest_kill && act() == 0)
            each_player([&](Player&) {
                auto& bits = quests();
                if (d2d::rules::quest::bit(bits, d2d::rules::TowerQuest::kQuest, 0)) return;
                d2d::rules::quest::set(bits, d2d::rules::TowerQuest::kQuest, 13);
                d2d::rules::quest::set(bits, d2d::rules::TowerQuest::kQuest, 0);
            });
        // Her treasure (FUN_005954f0, dead and not yet made: d+0x118 / 0x119):
        // a towerchestspawner (missile 332) at each chest of cellar 5 (object
        // 371, InitFn 47 lists up to 8).
        // The invisible owner (monster 326 at her death spot, FUN_005b2f20)
        // only owns the spawners, which d2d plays without one.
        // ponytail: the chests are the level's, not the ones whose rooms came
        // up (room units: agent D's).
        if (tower.dead && !tower.treasure && level->id == d2d::rules::TowerQuest::kCellar) {
            tower.treasure = true;
            for (std::size_t i = 0; i < level->npcs.size() && treasure.size() < 8; ++i)
                if (level->npcs[i].root == "objects" && level->npcs[i].object_id == d2d::rules::object_ids::kLargeChestR) treasure.push_back({ level, int(i), d2d::rules::TowerQuest::kTreasureFrames });
        }
        if (quest_kill) d2d::log::info("The Forgotten Tower: the Countess is dead");
    }

// The towerchestspawner's frame (FUN_005af300; Missiles.txt row 332:
// Range 400, Param1 150, Param2 2, Param3 5). At Range - Param1 left the
// chest opens (FUN_00585e00, mode 0 only): the act's chest class three
// times with quality forced magic (FUN_00585b90, EDX 4), then two each of
// hp / mp by act (+2 past normal: 0x731f4c / 0x731f60, FUN_00558450 /
// FUN_005584c0), then mode 2. From then on every Param2 * 4 frames a gold
// pile at the area level, rand(2r + 1) - r subtiles off (r = Param3).
// The rounds roll off the chest's unit seed (chest_round).
// ponytail: the world's rng stands in for the missile's seed; it runs only while the player's on its level (game.exe: while
// its room's up); the end event (unit event 0x5c at 1 left) isn't sent;
// the missile's lifetime taken as Range (LevRange 1 at level 0).
auto World::tower_treasure(std::uint32_t now_ms) -> void {
        using Tower = d2d::rules::TowerQuest;
        for (auto& spawner : treasure) {
            if (spawner.level != level || spawner.left <= 0) continue;
            const auto& chest = level->npcs[std::size_t(spawner.npc)];
            const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
            const auto& area_levels = game_data->area_level;
            auto alvl = [&](int id) { return std::size_t(id) < area_levels.size() ? area_levels[std::size_t(id)][std::size_t(diff)] : 1; };
            const int here = alvl(level->id);
            if (spawner.left == Tower::kTreasureFrames - Tower::kTreasureOpen && !operated.contains({ level, spawner.npc })) {
                const auto [low, high] = d2d::rules::kChestLevels[0];
                const auto treasure_class = d2d::rules::chest_tc(0, diff, here, alvl(low), alvl(high));
                std::vector<d2d::rules::Drop> drops;
                for (int round = 0; round < 3; ++round) d2d::rules::chest_round(game_data->rules, treasure_class, chest.seed, drops, 4);
                for (const auto* code : { "hp", "hp", "mp", "mp" }) drops.push_back({ std::string(code) + (diff ? "3" : "1") });
                for (const auto& dropped : drops) loot.put(dropped, chest.x, chest.y, here, fight.spawning.game, now_ms);
                operated[{ level, spawner.npc }] = now_ms;
                d2d::log::info("The Forgotten Tower: the Countess's chest opens ({} drops)", drops.size());
            }
            if (spawner.left < Tower::kTreasureFrames - Tower::kTreasureOpen && spawner.left % (Tower::kTreasureEvery * 4) == 0) {
                const int radius = Tower::kTreasureRadius;
                const float off_x = float(rng(2 * radius + 1) - radius) / 5, off_y = float(rng(2 * radius + 1) - radius) / 5;
                const auto [spot_x, spot_y] = level->nearest_free(chest.x + off_x, chest.y + off_y);
                loot.put({ .code = "gld" }, spot_x, spot_y, here, fight.spawning.game, now_ms);
            }
            --spawner.left;
        }
    }

// Tristram Cain's AI (FUN_005e7880), a think each time he stops: the
// first notes where he stands (FUN_005944b0) and heads 3 subtiles on
// (x + 3, y + 3); up to 6 goes while more than 1 off; then the town
// portal (FUN_005943b0: object 189, ECX 2 / EDX 0xbd to FUN_00555230)
// at the noted spot, in the room near his own that holds it
// (FUN_00463740); none does: the spot moves on 3 subtiles on each axis
// and no portal opens. Then back to the portal (FUN_00594450: his own
// spot when there's none) for up to 6 more thinks, stepping in
// (FUN_005944f0: camp Cain due).
// ponytail: the thinks' idles (1, 20 frames) stand in for the AI's own
// tick; the portal stays for the game (FUN_005944f0 doesn't remove it;
// InitFn 61 isn't traced); the Search for Cain: agent A's.
auto World::cain_step(std::uint32_t now_ms, float elapsed) -> void {
        if (cain_walk.npc < 0 || level->id != d2d::rules::CainQuest::kTristram || std::size_t(cain_walk.npc) >= npc_states.size()) return;
        const auto& npc = level->npcs[std::size_t(cain_walk.npc)];
        auto& unit = npc_states[std::size_t(cain_walk.npc)];
        if (unit.walking) {
            unit.walking = walk_on(*level, unit, cells_per_sec(npc.velocity) * elapsed);
            if (unit.walking) return;
            unit.path.clear();
        }
        if (now_ms < cain_walk.next) return;
        const auto cell = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
        const int at_x = int(std::floor(unit.x * 5)), at_y = int(std::floor(unit.y * 5));
        const auto walk = [&](int x, int y) {
            unit.walking = set_off(*level, unit, cell(x), cell(y), false);
        };
        const auto off = [&](int x, int y) { return d2d::rules::ai_distance(x - at_x, y - at_y); };
        if (cain_walk.stage < 0) {
            unit.hidden = false;
            cain_walk.spot_x = at_x; cain_walk.spot_y = at_y;
            cain_walk.x = at_x + 3; cain_walk.y = at_y + 3;
            cain_walk.stage = 1;
            cain_walk.next = now_ms + 40;
            return;
        }
        if (cain_walk.stage < 2) {
            if (off(cain_walk.x, cain_walk.y) > 1 && cain_walk.tries < 6) { ++cain_walk.tries; walk(cain_walk.x, cain_walk.y); return; }
            if (room_near_holds(*level, at_x, at_y, cain_walk.spot_x, cain_walk.spot_y)) {
                cain_portal = { level, cell(cain_walk.spot_x), cell(cain_walk.spot_y), now_ms };
                cues.cue("object_townportal", now_ms, cain_portal.x, cain_portal.y);
                d2d::log::info("Search for Cain: Cain opens a portal at subtile ({}, {})", cain_walk.spot_x, cain_walk.spot_y);
            } else {
                cain_walk.spot_x += 3; cain_walk.spot_y += 3;
            }
            cain_walk.stage = 2;
            cain_walk.next = now_ms + 20 * 40;
            return;
        }
        const bool opened = cain_portal.level == level;
        if (++cain_walk.stage < 8 && opened && off(cain_walk.spot_x, cain_walk.spot_y) != 0) { walk(cain_walk.spot_x, cain_walk.spot_y); return; }
        cain.portal_entered();
        unit.hidden = true;
        cain_walk.npc = -1;
        d2d::log::info("Search for Cain: Cain has gone to the camp");
    }

// FUN_00558110: the cursor's item, then all the player's items.
// The acting player carries `code` (FUN_00558110): the item in hand, a
// quest item (Items +0x12a) only if the game's difficulty is at most its
// stat 0x164; else one in their items, a quest item with questdiffcheck
// (+0x12b) on that condition. A held Cube ("box", no questdiffcheck, no
// stat) so counts only in Normal, as in game.exe.
auto World::carries(std::string_view code) const -> bool {
        const int difficulty = actor->character.header.active_difficulty();
        const auto counts = [&](const d2d::d2s::Item& item, bool held) {
            if (item.code != code) return false;
            const auto found = game_data->rules.item_base.find(item.code);
            const bool quest = found != game_data->rules.item_base.end() && found->second.quest;
            if (!quest || (!held && !found->second.quest_diff)) return true;
            return difficulty <= std::max(d2d::d2s::quest_difficulty_of(item), 0);
        };
        return (actor->held && counts(*actor->held, true)) || std::ranges::any_of(actor->character.items, [&](const auto& item) { return counts(item, false); });
    }

// FUN_005466b0(code, ilvl, quality): an item made into the player's
// inventory, left at their feet when there's no room.
auto World::give_item(std::string_view code, std::uint32_t now_ms, int ilvl, int quality) -> void {
        const std::size_t had = loot.ground.size();
        loot.put({ .code = std::string(code), .quality = quality }, actor->player.x, actor->player.y, ilvl, fight.spawning.game, now_ms);
        if (loot.ground.size() > had) loot.take(loot.ground.size() - 1);   // only what was made
    }

auto World::khalim_parts() const -> d2d::rules::KhalimQuest::Parts {
        using K = d2d::rules::KhalimQuest;
        return { carries(K::kEyeCode), carries(K::kBrainCode), carries(K::kHeartCode), carries(K::kFlailCode), carries(K::kWillCode) };
    }

// FUN_00544160: a quest item taken from the player, the cursor's first.
// FUN_00538680: act `act` (0-based) open, the save's progression at
// least difficulty x 4 (5 in LoD) + act.
auto World::open_act(int act) -> void {
        const int diff = actor->character.header.active_difficulty();
        actor->character.header.progression = std::uint8_t(std::max<int>(actor->character.header.progression, diff * (actor->character.header.expansion() ? 5 : 4) + act));
    }

// The player's class says `line` ("act1_complete_den": the sound
// <class>_act1_complete_den).
auto World::game_quest_done(std::size_t quest) const -> bool {
        const bool done[7] = { false, den.state >= 4, burial.state >= 4, tools.game_returned, cain.rescued_flag, tower.dead, andy.log == 0xd };
        return quest < 7 ? done[quest] : d2d::rules::quest::bit(game_bits, int(quest), 13);
    }

// The sound tick (FUN_004dcaa0): 62 ticks after the player's level changed,
// FUN_004cc270 looks it up once; a row's first level this game marks all of
// its levels, then speaks if its quest's to do (FUN_004a4180: open in this
// game, the game's flag 13 and the player's 0, 1 and 14 clear; the Den's
// log short of 5) and the class hasn't spoken for 62 ticks.
// ponytail: "hasn't spoken" is the class's lines (voice_at); game.exe's is
// any sound on the player (+0x7c) and no class line playing (FUN_004b9c20).
auto World::find_voice(std::uint32_t tick_ms) -> void {
        constexpr std::uint32_t kWait = 62 * kTickMs;
        if (level->id != actor->voice_level) { actor->voice_level = level->id; actor->voice_level_at = tick_ms; }
        if (actor->voice_level == actor->voice_checked || tick_ms - actor->voice_level_at < kWait) return;
        actor->voice_checked = actor->voice_level;
        const auto* row = d2d::rules::find_line(actor->voice_level);
        if (!row || std::size_t(actor->voice_level) >= actor->voice_found.size() || actor->voice_found[std::size_t(actor->voice_level)]) return;
        for (const int marked : row->levels)
            if (marked) actor->voice_found.set(std::size_t(marked));
        const auto& bits = actor->character.header.quests[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))];
        const auto quest = std::size_t(row->quest);
        namespace q = d2d::rules::quest;
        if (closed_at_join[quest] || game_quest_done(quest) || q::bit(bits, row->quest, 0) || q::bit(bits, row->quest, 1) || q::bit(bits, row->quest, 14)) return;
        if (row->quest == d2d::rules::DenQuest::kQuest && den.log_state(bits) >= 5) return;
        if (actor->voice_at && tick_ms - actor->voice_at < kWait) return;
        class_cue(row->line, tick_ms);
    }

auto World::class_cue(std::string_view line, std::uint32_t at_ms) -> void {
        static constexpr std::array<const char*, 7> kClass{ "amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin" };
        actor->voice_at = std::max(actor->voice_at, at_ms);
        if (actor->character.header.cls < kClass.size()) cues.cue_on(unit_key(kSoundOnPlayer, actor->id), std::format("{}_{}", kClass[actor->character.header.cls], line), at_ms, actor->player.x, actor->player.y);
    }

auto World::music_cue(std::string_view sound) -> void {
        if (const auto found = game_data->sound_index.find(std::string(sound)); found != game_data->sound_index.end())
            actor->events.push_back(ev::MusicCue{ found->second });
    }

auto World::take_carried(std::string_view code) -> void {
        if (actor->held && actor->held->code == code) { actor->held.reset(); return; }
        if (const auto found = std::ranges::find(actor->character.items, code, &d2d::d2s::Item::code); found != actor->character.items.end()) actor->character.items.erase(found);
    }

// The tree (OperateFn 12) drops the scroll by it; the stones (9) light in
// the deciphered scroll's order, the fifth opening the way to Tristram at
// StoneLambda (x + 6, y - 3); the Gibbet (10) frees Cain.
// ponytail: the world's rng stands in for the game's quest rng
// (game+0x10f4); CairnStones' missile and the portal's red look aren't
// drawn. Cain walks out (cain_step); with no room for him the portal
// FUN_0056d130 opens at x + 6, y + 6 is the player's own pair. Quests:
// agent A's.
auto World::cain_operate(int npc_index, std::uint32_t now_ms) -> void {
        using Cain = d2d::rules::CainQuest;
        const auto& object = level->npcs[std::size_t(npc_index)];
        const bool fresh = !operated.contains({ level, npc_index });
        auto& quest_bits = quests();
        if (object.operate_fn == d2d::rules::operate_fn::kInifussTree) {
            if (!cain.tree(quest_bits, carries("bks") || carries("bkd"))) return;
            class_cue("act1_find_tree", now_ms);   // FUN_00593af0: event 0x2d as the first scroll drops
            operated[{ level, npc_index }] = now_ms;
            loot.put({ .code = "bks" }, object.x, object.y, 1, fight.spawning.game, now_ms);
            d2d::log::info("Search for Cain: the Scroll of Inifuss");
            return;
        }
        if (object.operate_fn == d2d::rules::operate_fn::kGibbet) {
            if (!cain.gibbet(quest_bits, fresh)) return;
            class_cue("act1_rescue_cain", now_ms);   // FUN_00593290: unit event 0x30
            party_bits(d2d::rules::CainQuest::kQuest, { 13, 1 });     // the party in Act 1 (FUN_005930b0)
            operated[{ level, npc_index }] = now_ms;
            const auto found = std::ranges::find(level->npcs, d2d::rules::monster_ids::kCain, &Npc::hc_idx);
            const bool spawned = found != level->npcs.end();
            cain.rescued(spawned);
            // Over the players (FUN_00593130 / LAB_005931c0): those in Tristram, then their
            // party, neither done nor due, bits 13 and 1; the rest bit 14.
            kill_credit(d2d::rules::CainQuest::kQuest, 0, { 13, 1 }, [&](const Player&) { return level->id == d2d::rules::CainQuest::kTristram; },
                        [&](Player&) {
                            auto& bits = quests();
                            if (!d2d::rules::quest::bit(bits, d2d::rules::CainQuest::kQuest, 0) && !d2d::rules::quest::bit(bits, d2d::rules::CainQuest::kQuest, 1)) {
                                d2d::rules::quest::set(bits, d2d::rules::CainQuest::kQuest, 13); d2d::rules::quest::set(bits, d2d::rules::CainQuest::kQuest, 1);
                            }
                        },
                        [&](Player&) {
                            auto& bits = quests();
                            if (!d2d::rules::quest::bit(bits, d2d::rules::CainQuest::kQuest, 0) && !d2d::rules::quest::bit(bits, d2d::rules::CainQuest::kQuest, 1))
                                d2d::rules::quest::set(bits, d2d::rules::CainQuest::kQuest, 14);
                        });
            if (spawned) cain_walk = { .npc = int(found - level->npcs.begin()), .next = now_ms + std::uint32_t(object.op_frames) * 40 };
            else open_portal_at(object.x + 6, object.y + 6, now_ms);
            d2d::log::info("Search for Cain: Cain is free");
            return;
        }
        if (!cain.ordered) {
            cain.stone_order(rng.low, rng.high);
            d2d::log::info("Search for Cain: the stones' order {} {} {} {} {}", cain.order[0], cain.order[1], cain.order[2], cain.order[3], cain.order[4]);
        }
        const int voice_throttle = cain.throttle;
        const auto lit = cain.stone(quest_bits, object.object_id, carries("bkd"), fresh);
        if (cain.throttle != voice_throttle && (voice_throttle & 0x3f) == 0
            && !d2d::rules::quest::bit(quest_bits, Cain::kQuest, 3) && !d2d::rules::quest::bit(quest_bits, Cain::kQuest, 4))
            class_cue("act1_find_cairn", now_ms);   // FUN_00593710: event 0x27, every 64th missing-scroll hint
        if (lit == Cain::Stone::none) return;
        operated[{ level, npc_index }] = now_ms;
        if (lit == Cain::Stone::lit) return;
        actor->character.items.erase(std::ranges::find(actor->character.items, std::string_view("bkd"), &d2d::d2s::Item::code));
        party_bits(Cain::kQuest, { 4 });                      // the party in Act 1 (LAB_005936b0)
        const auto lambda = std::ranges::find(level->npcs, 21, &Npc::object_id);
        const auto [spot_x, spot_y] = level->nearest_free(lambda == level->npcs.end() ? object.x : lambda->x + 6, lambda == level->npcs.end() ? object.y : lambda->y - 3);
        portal[2] = { level, spot_x, spot_y, now_ms };
        cues.cue("object_townportal", now_ms, spot_x, spot_y);
        d2d::log::info("Search for Cain: the way to Tristram at ({:.1f}, {:.1f})", spot_x, spot_y);
    }

auto World::den_count(std::uint32_t now_ms) -> void {
        if (level->id != d2d::rules::DenQuest::kDen || fight.mon_level != level) return;
        const int left = int(std::ranges::count_if(fight.monsters, &Monster::alive));
        for (const auto& kill : fight.kills)
            if (kill.killer > 0) den.took_part(kill.killer);   // a monster's death: its killer takes part
        if (den_left >= 0 && left < den_left) {
            const auto kill = den.killed(left);
            if (kill == d2d::rules::DenQuest::Kill::few) d2d::log::info("Den of Evil: {}", left == 1 ? "one monster left" : std::format("monsters remaining: {}", left));
            if (kill == d2d::rules::DenQuest::Kill::cleared) {
                d2d::log::info("Den of Evil: cleared");
                music_cue("music_quest_den");
                den_log_at = now_ms + 8 * kTickMs;     // the log's "Return to Akara" 8 ticks on (LAB_00590230)
                each_player([&](Player& player) {
                    if (d2d::rules::DenQuest::credit(quests(), den.participants.has(player.id))) class_cue("act1_complete_den", now_ms);   // unit event 0x23
                });
            }
        }
        den_left = left;
    }

}  // namespace d2d::game
