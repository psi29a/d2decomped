// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--freeze-resume CASES`: when a frozen monster thinks again, the cases of
// tools/emu/freeze_resume.py (game.exe's FUN_0057b230 / 0x57b170 /
// FUN_005a7f80). A Zombie (it swings at every think with its foe in melee)
// carrying the case row's MonStats aidel is frozen through the REAL
// Fight::strike (a missile's freeze), then monster_update runs a tick at a
// time. Prints `id first second`: the frames from the freeze to the first
// swing, and to the first from the second freeze's frame on (`-` none).

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <montypes.hpp>
#include <rules.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace d2d::game;

int main(int argc, char** argv) try {
    if (argc < 3 || std::string_view(argv[1]) != "--freeze-resume") {
        std::printf("SKIP: an oracle mode, run `test_freeze_resume --freeze-resume CASES` (see tools/emu/freeze_resume.py)\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");

    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1);
    if (!data->patched) { std::printf("SKIP: the oracle is 1.14d's game.exe: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
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
    World world(&*data, -1, -1);
    world.enter(character);

    const auto zombie = data->monsters.by_id.find("zombie1");
    const auto spike = data->skills.by_name.find("Glacial Spike");
    assert(zombie != data->monsters.by_id.end() && spike != data->skills.by_name.end());
    auto& body = const_cast<d2d::rules::MonType&>(data->monsters.types[zombie->second]);
    const auto real_diff = body.diff;
    const bool real_boss_column = body.boss_column;
    GameData::MissileInfo spike_info;
    spike_info.name = "freezeresume";
    spike_info.etype = 2;
    spike_info.to_hit = false;
    Level here{};
    here.id = d2d::rules::level_ids::kBloodMoor;
    const Crowd crowd;
    std::vector<Missile> shots;
    constexpr std::uint32_t kFreezeFrame = 2000;
    constexpr int kHorizon = 1200;
    std::ifstream case_file(argv[2]);
    std::string line, row;
    while (std::getline(case_file, line)) {
        std::istringstream fields(line);
        int id = 0, difficulty = 0, kind = 0, length = 0, pending = 0, prior = 0, second_at = 0, second_length = 0;
        if (!(fields >> id >> difficulty >> row >> kind >> length >> pending >> prior >> second_at >> second_length)) continue;
        const auto source = data->monsters.by_id.find(row);
        if (source == data->monsters.by_id.end()) { std::printf("%d NOROW\n", id); continue; }
        body.diff[std::size_t(difficulty)].aidel = data->monsters.types[source->second].diff[std::size_t(difficulty)].aidel;
        body.boss_column = kind == 5;
        d2d::rules::Rng spawn_rng{ std::uint32_t(0x5eed0000u + std::uint32_t(id)) };
        Monster monster = make_monster(*data, int(zombie->second), 10.f, 10.f, spawn_rng, difficulty);
        monster.boss = kind >= 1 && kind <= 4 ? d2d::rules::Boss(kind) : d2d::rules::Boss::none;
        monster.stats.hit_points = monster.hit_points = 1000000;
        monster.life_frac = 0;
        monster.aware = true;
        const std::uint32_t now = kFreezeFrame * kTickMs;
        monster.next_act = now + std::uint32_t(pending) * kTickMs;
        monster.stun_until = prior > 0 ? now + std::uint32_t(prior) * kTickMs : 0;
        world.fight.game_difficulty = difficulty;
        world.fight.monsters.clear();
        world.fight.add_monster(monster);
        Missile missile;
        missile.info = &spike_info;
        missile.skill = int(spike->second);
        missile.fixed = 0;
        missile.friendly = true;
        world.fight.strike(missile, 0, now, length);
        std::array<Foe, 1> foes{};
        foes[0].x = 10.4f;
        foes[0].y = 10.f;
        d2d::rules::Rng think_rng{ std::uint32_t(0x7e570000u + std::uint32_t(id)) };
        int first = -1, second = -1;
        auto& struck = world.fight.monsters[0];
        for (int tick = 1; tick <= kHorizon && (first < 0 || (second_at > 0 && second < 0)); ++tick) {
            const std::uint32_t tick_ms = now + std::uint32_t(tick) * kTickMs;
            if (tick == second_at) world.fight.strike(missile, 0, tick_ms, second_length);
            const std::string_view before = struck.mode;
            foes[0].alive = true;
            monster_update(*data, here, struck, foes, think_rng, tick_ms, 0.04f, crowd, shots, world.fight.monsters);
            const bool swung = struck.mode != before && (struck.mode == "A1" || struck.mode == "A2");
            if (swung && first < 0) first = tick;
            if (swung && second_at > 0 && tick >= second_at && second < 0) second = tick;
        }
        const auto out = [](int frame) { return frame < 0 ? std::string("-") : std::to_string(frame); };
        std::printf("%d %s %s\n", id, out(first).c_str(), second_at > 0 ? out(second).c_str() : "-");
        body.diff = real_diff;
        body.boss_column = real_boss_column;
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
