// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--next-hit SEED N`, the d2d side of tools/emu/next_hit.py. Skips (rc 77)
// without that argument or without the game data (D2_MPQ_DIR).

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace d2d::game;

int main(int argc, char** argv) try {
    if (argc < 4 || std::string_view(argv[1]) != "--next-hit") { std::printf("SKIP: needs --next-hit SEED N\n"); return D2D_TEST_SKIPPED; }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");

    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Headless", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    // `--next-hit SEED N`: one Nova or Frost Nova cast (the 64-missile ring,
    // NextHit rows) on 1..6 standing monsters of sizes 1..3, through the
    // REAL Fight::fire and Fight::world (missile flight, the frame's unit
    // loop, Fight::skill_missile_hits). Each missile deals a fixed 10 points
    // (Missile::fixed) to monsters with no resistance and a million life, so
    // a monster's life lost over a frame / 10 is its hits that frame. Lines:
    // `C<case> <skill> <n> <size>@<dx>,<dy> ...` (subtiles from the caster),
    // `F<case> <frame> <missile>:<monster>.<monster> ...` the units in the
    // subtiles each live missile entered that frame (rules::missile_touches,
    // monster order), `H<case> <frame> <monster>x<hits> ...` and
    // `T<case> <hits per monster>`. tools/emu/next_hit.py replays the F
    // lines through game.exe's FUN_005adf10 and prints its H and T lines.
    World world(&*data, -1, -1);
    world.enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world.level = &open;
    world.fight.enter(&open);
    // A caster's subtile with rooms all round (missile flight needs them).
    int at_x = -1, at_y = -1;
    for (int y = 20; y < 400 && at_x < 0; ++y)
        for (int x = 20; x < 400 && at_x < 0; ++x)
            if (std::ranges::all_of(std::array<std::pair<int, int>, 5>{ { { 0, 0 }, { 8, 0 }, { -8, 0 }, { 0, 8 }, { 0, -8 } } },
                                    [&](auto off) { return !missile_rooms(*data, open, float(x + off.first) + 0.5f, float(y + off.second) + 0.5f).empty(); }))
                at_x = x * 5 + 2, at_y = y * 5 + 2;
    assert(at_x >= 0);
    world.actor->player.x = (float(at_x) + 0.5f) / 5;
    world.actor->player.y = (float(at_y) + 0.5f) / 5;
    // One MonStats row per size (MonStats2 SizeX), hostile kinds.
    std::array<int, 4> kind{ -1, -1, -1, -1 };
    for (const char* name : { "flyingscimitar", "maggotegg1", "fallen1", "zombie1", "bighead1", "brute2", "wendigo1" })
        if (const auto found = data->monsters.by_id.find(name); found != data->monsters.by_id.end())
            if (const int size = data->monsters.types[found->second].size; size >= 1 && size <= 3 && kind[std::size_t(size)] < 0) kind[std::size_t(size)] = int(found->second);
    for (std::size_t row = 0; row < data->monsters.types.size(); ++row)
        if (const int size = data->monsters.types[row].size; size >= 1 && size <= 3 && kind[std::size_t(size)] < 0) kind[std::size_t(size)] = int(row);
    for (int size = 1; size <= 3; ++size) {
        if (kind[std::size_t(size)] < 0) { std::printf("SKIP: no size %d row\n", size); return D2D_TEST_SKIPPED; }
        std::fprintf(stderr, "size %d: MonStats row %d %s\n", size, kind[std::size_t(size)], data->monsters.types[std::size_t(kind[std::size_t(size)])].id.c_str());
    }
    const int nova = data->skills.by_name.at("Nova"), frost_nova = data->skills.by_name.at("Frost Nova");
    const Crowd crowd;
    std::uint32_t now = 1000 * kTickMs;
    struct Placed { int size, dx, dy; };
    const auto run = [&](const std::string& tag, int skill_id, int lvl, const std::vector<Placed>& placed) {
        const auto& skill = *data->skills.get(skill_id);
        std::printf("C%s %s %d %zu", tag.c_str(), skill_id == nova ? "nova" : "frostnova", lvl, placed.size());
        for (const auto& one : placed) std::printf(" %d@%d,%d", one.size, one.dx, one.dy);
        std::printf("\n");
        world.fight.monsters.clear();
        world.fight.missiles.clear();
        world.fight.pending.clear();
        d2d::rules::Rng spawn_rng{ 5 };
        for (const auto& one : placed) {
            Monster monster = make_monster(*data, kind[std::size_t(one.size)], (float(at_x + one.dx) + 0.5f) / 5, (float(at_y + one.dy) + 0.5f) / 5, spawn_rng, 0);
            monster.stats.hit_points = monster.hit_points = 1000000;
            monster.life_frac = 0;
            monster.next_act = ~0u;
            world.fight.add_monster(monster);
        }
        for (std::size_t i = 0; i < world.fight.monsters.size(); ++i) {
            auto& monster = world.fight.monsters[i];
            monster.boss_res = {};
            const auto res = world.fight.target_of(i).res;
            for (std::size_t k = 0; k < monster.boss_res.size() && k < res.size(); ++k) monster.boss_res[k] = -res[k];
        }
        Fight::Caster caster;
        caster.x = world.actor->player.x; caster.y = world.actor->player.y; caster.level = lvl;
        caster.to_x = caster.x + 1; caster.to_y = caster.y;
        world.fight.fire(skill, now, caster);
        for (auto& missile : world.fight.pending) missile.fixed = 10 << 8;
        std::vector<int> total(placed.size(), 0);
        for (int frame = 1; frame <= 40 && !(world.fight.missiles.empty() && world.fight.pending.empty() && frame > 1); ++frame) {
            now += kTickMs;
            std::vector<int> life_before;
            for (auto& monster : world.fight.monsters) { monster.next_act = ~0u; life_before.push_back(monster.hit_points); }
            world.fight.world(true, now, 0.04f, crowd);
            std::string contacts;
            for (const auto& missile : world.fight.missiles) {
                if (missile.skill != skill_id) continue;
                std::string touched;
                for (std::size_t i = 0; i < world.fight.monsters.size(); ++i) {
                    const auto& monster = world.fight.monsters[i];
                    if (monster.targetable() && missile_struck(open, missile, monster.unit.x, monster.unit.y, data->monsters.types[std::size_t(monster.type)].size))
                        touched += (touched.empty() ? "" : ".") + std::to_string(i);
                }
                if (!touched.empty()) contacts += " " + std::to_string(missile.sound) + ":" + touched;
            }
            if (!contacts.empty()) std::printf("F%s %d%s\n", tag.c_str(), frame, contacts.c_str());
            std::string hits;
            for (std::size_t i = 0; i < world.fight.monsters.size(); ++i) {
                const int lost = life_before[i] - world.fight.monsters[i].hit_points;
                if (lost <= 0) continue;
                total[i] += lost / 10;
                hits += " " + std::to_string(i) + "x" + std::to_string(lost / 10) + (lost % 10 ? "?" : "");
            }
            if (!hits.empty()) std::printf("H%s %d%s\n", tag.c_str(), frame, hits.c_str());
        }
        std::printf("T%s", tag.c_str());
        for (const int hits : total) std::printf(" %d", hits);
        std::printf("\n");
        now += 100 * kTickMs;
    };
    std::uint32_t state = std::uint32_t(std::atoi(argv[2]));
    if (state == 0) state = 1;
    const auto next = [&state] { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; };
    int id = 0;
    // Boundaries: one monster of each size at 0..14 subtiles, three headings.
    for (const int skill_id : { nova, frost_nova })
        for (int size = 1; size <= 3; ++size)
            for (const int distance : { 0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 14 })
                for (const int heading : { 0, 1, 2 })
                    run("B" + std::to_string(id++), skill_id, 1, { { size, distance, heading == 0 ? 0 : heading == 1 ? distance / 2 : distance } });
    // Random packs: 1..6 monsters within 12 subtiles, skill level 1..20.
    for (int i = 0; i < std::atoi(argv[3]); ++i) {
        const int skill_id = next() % 2 ? nova : frost_nova;
        const int lvl = 1 + int(next() % 20);
        const int count = 1 + int(next() % 6);
        std::vector<Placed> placed;
        for (int k = 0; k < count; ++k) {
            const int size = 1 + int(next() % 3);
            const int dx = int(next() % 25) - 12, dy = int(next() % 25) - 12;
            placed.push_back({ size, dx, dy });
        }
        run("R" + std::to_string(i), skill_id, lvl, placed);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
