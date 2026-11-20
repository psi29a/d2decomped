// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A merc's share of its owner's kill, as tools/emu/merc_levelup.py runs
// game.exe's: `--merc-levelup` reads its cases (`hid level into mexp mlvl
// plvl blow addexp gear kind value`) and prints `hid level exp mexp mlvl plvl
// blow addexp gear life base exp_after level_after life_after`. Each merc is
// made by Fight::merc_level_stats (Hireling.txt Id `hid`, `into` thousandths
// of the way from `level` to the next), with `gear` max life and `addexp`
// percent more experience on a worn item and its life (256ths) from
// `kind` / `value`; its owner at character level `plvl` kills a monster of
// experience `mexp` at level `mlvl`, the merc's blow or not (`blow`), through
// Fight::merc_share: base is its level-up life (256ths) before, then its
// experience, level and life (256ths) after. Needs the game data.
// Without it, the rule on a typed-in Hireling.txt row: a kill that raises
// the merc's level sets its life to its new level-up life (FUN_00572840:
// stat 6 := stat 7), its items' max life left out, lower too; one that
// doesn't leaves its life alone.

#include <ai.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <loot.hpp>
#include <rules.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

struct Rig {
    const GameData& game_data;
    Level wild;
    const Level* level = &wild;
    d2d::rules::Rng rng{ 1 };
    Cues cues;
    Loot loot{ &game_data, level, rng, cues };
    Fight fight{ &game_data, level, rng, loot, cues };
    Fight::Hero hero;

    explicit Rig(const GameData& data) : game_data(data) {
        wild.id = 2;                                                    // the Blood Moor
        fight.act_as(hero);
    }

    // The first expansion row of Hireling.txt Id `hid`, or none.
    [[nodiscard]] const d2d::rules::Hireling* first_row(int hid) const {
        for (const auto& hireling : game_data.rules.hirelings)
            if (hireling.version == 100 && hireling.id == hid) return &hireling;
        return nullptr;
    }

    // A merc of Id `hid` with experience `exp`, `gear` max life and `addexp`
    // percent more experience on its armour, its owner at `owner_level`.
    void make(int hid, std::uint32_t exp, int gear, int addexp, int owner_level) {
        auto& header = hero.character.header;
        header.merc_seed = 1;
        header.merc_type = std::uint16_t(hid);
        header.merc_exp = exp;
        hero.character.stats.values[d2d::d2s::kLevel] = owner_level;
        hero.character.merc_items.clear();
        if (gear != 0 || addexp != 0) {
            d2d::d2s::Item armor;
            armor.location = d2d::d2s::item_location::kEquipped;
            armor.slot = d2d::d2s::body_location::kTorso;
            if (gear != 0) armor.props.push_back({ .stat = d2d::d2s::kMaxLife, .value = gear });
            if (addexp != 0) armor.props.push_back({ .stat = d2d::d2s::kAddExperience, .value = addexp });
            hero.character.merc_items.push_back(std::move(armor));
        }
        hero.merc = Monster{};
        fight.merc_level_stats();
    }
};

// (L + 1) x ExpPerLvl x L^2, 32 bits, as merc_stats and FUN_00663790.
std::uint32_t exp_at(std::int64_t level, std::int64_t per_level) {
    return std::uint32_t((level + 1) * per_level * level * level);
}

int merc_levelup_cases(const GameData& game_data) {
    Rig rig(game_data);
    std::string line;
    while (std::getline(std::cin, line)) {
        int hid = 0, merc_level = 0, into = 0, mexp = 0, mlvl = 0, plvl = 0, blow = 0, addexp = 0, gear = 0, kind = 0, value = 0;
        if (line.empty()) continue;
        if (std::sscanf(line.c_str(), "%d %d %d %d %d %d %d %d %d %d %d", &hid, &merc_level, &into, &mexp, &mlvl, &plvl, &blow, &addexp, &gear, &kind, &value) != 11) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        const auto* first = rig.first_row(hid);
        if (!first) { std::fprintf(stderr, "Id %d: no such merc in d2d\n", hid); return 1; }
        const auto start = exp_at(merc_level, first->exp_per_level), end = exp_at(merc_level + 1, first->exp_per_level);
        const auto exp = std::uint32_t(start + std::uint64_t(end - start) * std::uint64_t(into) / 1000);
        rig.make(hid, exp, gear, addexp, plvl);
        if (rig.hero.merc_st.level != merc_level) { std::fprintf(stderr, "Id %d exp %u: d2d makes level %d, not %d\n", hid, exp, rig.hero.merc_st.level, merc_level); return 1; }
        const int base = rig.hero.merc_st.life * 256;
        const int top = base + gear * 256;
        const int life = kind == 0 ? value : kind == 1 ? std::max(top - value, 0x100) : top + value;
        rig.hero.merc->set_life_fine(life);
        Monster monster;
        monster.stats.exp = mexp;
        monster.stats.level = mlvl;
        rig.fight.merc_blow = blow != 0;
        rig.fight.merc_share(monster);
        rig.fight.merc_blow = false;
        std::printf("%d %d %u %d %d %d %d %d %d %d %d %u %d %d\n", hid, merc_level, exp, mexp, mlvl, plvl, blow, addexp, gear, life, base,
                    rig.hero.character.header.merc_exp, rig.hero.merc_st.level, rig.hero.merc->life_fine());
    }
    return 0;
}

// The rule on a typed-in row: Id 1, level 1, 100 life and 10 a level,
// ExpPerLvl 100 (level 2 at 1,200), an ExpRatio of 1 (1024 >> 10).
int levelup_checks() {
    GameData game_data;
    d2d::rules::Hireling row;
    row.version = 100; row.id = 1; row.level = 1; row.exp_per_level = 100; row.hit_points = 100; row.hp_per_level = 10;
    game_data.rules.hirelings.push_back(std::move(row));
    game_data.exp_ratio.assign(100, 1024);
    Rig rig(game_data);
    auto& hero = rig.hero;
    Monster monster;
    monster.stats.exp = 1000;
    monster.stats.level = 1;
    rig.make(1, 1190, 500, 0, 10);                                      // 10 short of level 2, +500 max life worn
    assert(hero.merc_st.level == 1 && hero.merc_st.life == 100);
    hero.merc->set_life_fine(400 * 256 + 7);
    rig.fight.merc_blow = true;
    rig.fight.merc_share(monster);                                      // the cap: (1,200 - 200) >> 6 = 15, twice that added
    assert(hero.character.header.merc_exp == 1220 && hero.merc_st.level == 2);
    assert(hero.merc->life_fine() == 110 * 256);                        // its new level-up life, not 610 with its gear, nor the 400 it had
    rig.make(1, 200, 500, 0, 10);
    hero.merc->set_life_fine(300 * 256 + 7);
    rig.fight.merc_share(monster);
    assert(hero.character.header.merc_exp == 230 && hero.merc_st.level == 1);
    assert(hero.merc->life_fine() == 300 * 256 + 7);                    // no level-up: its life stays
    rig.fight.merc_blow = false;                                        // its owner's blow: 15 x 86 / 256 = 5, twice that
    rig.make(1, 1199, 0, 0, 10);
    hero.merc->set_life_fine(20 * 256);
    rig.fight.merc_share(monster);
    assert(hero.character.header.merc_exp == 1209 && hero.merc_st.level == 2);
    assert(hero.merc->life_fine() == 110 * 256);                        // the same on its owner's kill
    std::puts("OK: a merc's level-up sets its life to its new level-up life, its gear's left out");
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--merc-levelup") {
        const char* mpq_dir = std::getenv("D2_MPQ_DIR");
        if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::fprintf(stderr, "--merc-levelup needs D2_MPQ_DIR\n"); return 1; }
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
        if (!data || !data->patched) { std::fprintf(stderr, "--merc-levelup needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return 1; }
        return merc_levelup_cases(*data);
    }
    return levelup_checks();
} catch (const std::exception& error) {
    std::printf("test_merc_levelup: %s\n", error.what());
    return 1;
}
