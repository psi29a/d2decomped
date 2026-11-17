// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A merc's own life regeneration, as tools/emu/merc_regen.py runs game.exe's:
// `--merc-regen` reads its cases (`hid level gear hpregen town kind value
// frames`) and prints `hid level gear hpregen town life frames regen
// life_after filed dead`. Each merc
// is made by Fight::merc_level_stats (Hireling.txt Id `hid`, the experience
// of `level`), hurt one point through Fight::hurt_side (game.exe files its
// regeneration event on a hurt), then run through Fight::apply_regen once a
// frame, in a town level or not: regen is the life it gains in a frame from
// one point, life_after its life (256ths) after `frames` frames from `life`,
// with `gear` max life and `hpregen` Replenish Life (stat 74) on a worn item,
// filed 1 while its regeneration still runs (merc stats.regen set), dead 1 if
// it died. Needs the game data.

#include <ai.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <loot.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

struct Rig {
    const GameData& game_data;
    Level town, wild;
    const Level* level = &wild;
    d2d::rules::Rng rng{ 1 };
    Cues cues;
    Loot loot{ &game_data, level, rng, cues };
    Fight fight{ &game_data, level, rng, loot, cues };
    Fight::Hero hero;
    std::uint32_t now = 4000;

    explicit Rig(const GameData& data) : game_data(data) {
        town.id = 1;                                                    // the Rogue Encampment
        wild.id = 2;                                                    // the Blood Moor
        fight.act_as(hero);
    }

    // A merc of Hireling.txt Id `hid` at `merc_level` (the experience that
    // makes it: (L + 1) x ExpPerLvl x L^2), `gear` max life and `hpregen`
    // Replenish Life on its armour, at full life, then hurt one point. False
    // if d2d makes another level.
    bool make(int hid, int merc_level, int gear, int hpregen) {
        const d2d::rules::Hireling* first = nullptr;
        for (const auto& hireling : game_data.rules.hirelings)
            if (hireling.version == 100 && hireling.id == hid) { first = &hireling; break; }
        if (!first) return false;
        auto& header = hero.character.header;
        header.merc_seed = 1;
        header.merc_type = std::uint16_t(hid);
        header.merc_exp = std::uint32_t(std::int64_t(merc_level + 1) * first->exp_per_level * merc_level * merc_level);
        hero.character.merc_items.clear();
        if (gear != 0 || hpregen != 0) {
            d2d::d2s::Item armor;
            armor.location = d2d::d2s::item_location::kEquipped;
            armor.slot = d2d::d2s::body_location::kTorso;
            if (gear != 0) armor.props.push_back({ .stat = d2d::d2s::kMaxLife, .value = gear });
            if (hpregen != 0) armor.props.push_back({ .stat = d2d::d2s::kHitPointRegeneration, .value = hpregen });
            hero.character.merc_items.push_back(std::move(armor));
        }
        hero.merc = Monster{};
        fight.merc_level_stats();
        hero.merc->hit_points = fight.merc_max_life();
        std::array<Foe, 2> side{};
        side[1].damage = 1;
        fight.hurt_side(std::span(side), now);
        // A hurt files nothing at a total of 0 (FUN_0057c6c0); a poison files it at any
        // (FUN_0057ac50): one is put, then taken off again, so every case starts filed.
        side = {};
        side[1].poison = 1; side[1].poison_ticks = 1;
        fight.hurt_side(std::span(side), now);
        hero.merc_poison_until = 0; hero.merc_poison_acc = 0; hero.merc_poison_rate = 0;
        return hero.merc_st.level == merc_level;
    }
    // Its life (256ths) after `frames` frames from `life` in `mode`, in a town or not.
    int run(int life, std::string_view mode, int frames, bool in_town = false) {
        level = in_town ? &town : &wild;
        hero.merc->mode = mode;
        hero.merc->set_life_fine(life);
        for (int frame = 0; frame < frames; ++frame, now += 40) fight.apply_regen(now + 40, now);
        return hero.merc->life_fine();
    }
};

int merc_regen_cases(const GameData& game_data) {
    Rig rig(game_data);
    std::string line;
    while (std::getline(std::cin, line)) {
        int hid = 0, merc_level = 0, gear = 0, hpregen = 0, town = 0, kind = 0, value = 0, frames = 0;
        if (line.empty()) continue;
        if (std::sscanf(line.c_str(), "%d %d %d %d %d %d %d %d", &hid, &merc_level, &gear, &hpregen, &town, &kind, &value, &frames) != 8) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        if (!rig.make(hid, merc_level, 0, 0)) { std::fprintf(stderr, "Id %d level %d: no such merc in d2d\n", hid, merc_level); return 1; }
        const int regen = rig.run(0x100, "NU", 1) - 0x100;
        rig.make(hid, merc_level, gear, hpregen);
        const int max_life = rig.fight.merc_max_life() * 256;
        const int life = kind == 0 ? value : kind == 1 ? std::max(max_life - value, 0x100) : max_life + value;
        const int after = rig.run(life, "NU", frames, town != 0);
        const int filed = rig.hero.merc->stats.regen > 0 ? 1 : 0;
        const int dead = rig.hero.merc->hit_points <= 0 ? 1 : 0;
        std::printf("%d %d %d %d %d %d %d %d %d %d %d\n", hid, merc_level, gear, hpregen, town, life, frames, regen, after, filed, dead);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--merc-regen") {
        const char* mpq_dir = std::getenv("D2_MPQ_DIR");
        if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::fprintf(stderr, "--merc-regen needs D2_MPQ_DIR\n"); return 1; }
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
        if (!data || !data->patched) { std::fprintf(stderr, "--merc-regen needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return 1; }
        return merc_regen_cases(*data);
    }
    std::puts("SKIP: run with --merc-regen < cases (tools/emu/merc_regen.py)");
    return D2D_TEST_SKIPPED;
} catch (const std::exception& error) {
    std::printf("test_merc_regen: %s\n", error.what());
    return 1;
}
