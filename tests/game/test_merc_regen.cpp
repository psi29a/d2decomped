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
// Without it, the rule on a typed-in merc: stat 0x4a = its level-up life in
// 256ths / 2000 (FUN_00572840); its total with its items' Replenish Life is
// added once a frame by its event 3 (FUN_005a6920), filed by its first hurt,
// poison or healing potion (FUN_0057c6c0, FUN_0057ac50, FUN_005be3f0): the
// event ends at its max life or a total of 0, a negative total kills it out
// of town and stops under one point in town; a level-up sets the value again;
// a merc that joins has no event.

#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <loot.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
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

// Worn Replenish Life on the merc: an armour with stat 74 `hpregen`.
void wear_regen(Fight::Hero& hero, int hpregen) {
    hero.character.merc_items.clear();
    d2d::d2s::Item armor;
    armor.location = d2d::d2s::item_location::kEquipped;
    armor.slot = d2d::d2s::body_location::kTorso;
    armor.props.push_back({ .stat = d2d::d2s::kHitPointRegeneration, .value = hpregen });
    hero.character.merc_items.push_back(std::move(armor));
}

// One point of damage to the merc, as a monster's hit lands (hurt_side).
void hurt(Rig& rig) {
    std::array<Foe, 2> side{};
    side[1].damage = 1;
    rig.fight.hurt_side(std::span(side), rig.now);
}

// The rule on a merc typed in, no game data (merc_level_stats finds no
// Hireling.txt row and makes the default merc: life 40).
int regen_checks() {
    const GameData game_data;
    Rig rig(game_data);
    auto& hero = rig.hero;
    auto& fight = rig.fight;
    hero.merc = Monster{};                                              // a merc that joins: no event
    fight.merc_joins();
    assert(hero.merc_st.life == 40 && hero.merc->stats.regen == 0);
    assert(rig.run(20 * 256, "NU", 50) == 20 * 256);
    hero.merc_st.life = 1000;
    hero.merc->hit_points = 900;
    const int start = hero.merc->life_fine();
    assert(rig.run(start, "NU", 50) == start);                          // never hurt: no event, no regeneration
    hurt(rig);
    assert(hero.merc->stats.regen == 1000 * 256 / 2000);                // 128 256ths a frame
    assert(rig.run(899 * 256, "NU", 1) == 899 * 256 + 128);
    assert(rig.run(899 * 256, "NU", 10) == 899 * 256 + 1280);
    assert(rig.run(899 * 256, "GH", 1) == 899 * 256 + 128);             // any mode
    assert(rig.run(1000 * 256 + 300, "NU", 0) == 1000 * 256 + 300);     // no frame, nothing
    assert(rig.run(1000 * 256 - 100, "NU", 3) == 1000 * 256);           // at most its max life, and the event ends
    assert(hero.merc->stats.regen == 0);
    assert(rig.run(900 * 256, "NU", 5) == 900 * 256);                   // ended: nothing until the next hurt
    hurt(rig);
    assert(rig.run(1000 * 256 + 300, "NU", 1) == 1000 * 256 && hero.merc->stats.regen == 0);   // over it: down to it, ended
    hurt(rig);
    hero.merc->mode = "NU";
    fight.apply_regen(rig.now + 20, rig.now);                           // within one frame: nothing
    assert(hero.merc->life_fine() == 999 * 256);
    fight.apply_regen(rig.now + 40, rig.now + 20);                      // the next frame starts: one
    assert(hero.merc->life_fine() == 999 * 256 + 128);
    wear_regen(hero, 10);                                               // its items' Replenish Life joins it
    assert(rig.run(500 * 256, "NU", 3) == 500 * 256 + 3 * 138);
    wear_regen(hero, -128);                                             // a total of 0: nothing, and the event ends
    assert(rig.run(500 * 256, "NU", 3) == 500 * 256 && hero.merc->stats.regen == 0);
    hero.character.merc_items.clear();
    hurt(rig);
    wear_regen(hero, -200);                                             // a negative total drains
    assert(rig.run(500 * 256, "NU", 2) == 500 * 256 - 2 * 72);
    assert(rig.run(0x100 + 50, "NU", 3, true) == 0x100 + 50 - 72);      // in town, not under one point
    assert(hero.merc->hit_points == 1 && hero.merc->stats.regen == 128);
    assert(rig.run(0x100 + 50, "NU", 4) == 0x100 + 50 - 4 * 72);        // out of town it goes on
    assert(rig.run(0x100 + 50, "NU", 5) == 0 && hero.merc->hit_points == 0 && hero.merc->mode == "DT");   // and kills
    hero.merc = Monster{};                                              // worn life doesn't count: the level-up's base stat 7
    hero.merc->hit_points = 900;
    hero.character.merc_items.clear();
    d2d::d2s::Item helm;
    helm.location = d2d::d2s::item_location::kEquipped;
    helm.slot = d2d::d2s::body_location::kHead;
    helm.props.push_back({ .stat = d2d::d2s::kMaxLife, .value = 1000 });
    hero.character.merc_items.push_back(std::move(helm));
    hurt(rig);
    assert(fight.merc_max_life() == 2000 && hero.merc->stats.regen == 128);
    hero.character.merc_items.clear();
    fight.merc_level_stats();                                           // a level-up sets it again: life 40
    assert(hero.merc_st.life == 40 && hero.merc->stats.regen == 40 * 256 / 2000);
    std::puts("OK: a hurt merc regains its level-up life / 2000 and its Replenish Life each frame, to its max");

    hero.merc = Monster{};                                              // a new merc: poisoned, not hit
    hero.merc_st.life = 1000;
    hero.merc->hit_points = 1000;
    std::array<Foe, 2> side{};
    side[1].poison = 256; side[1].poison_ticks = 25;
    fight.hurt_side(std::span(side), rig.now);
    assert(hero.merc->stats.regen == 128);
    hero.merc = Monster{};                                              // a hurt whose total is 0 files nothing
    hero.merc->hit_points = 900;
    wear_regen(hero, -128);
    hurt(rig);
    assert(hero.merc->hit_points == 899 && hero.merc->stats.regen == 0);
    hero.character.merc_items.clear();
    hero.merc->hit_points = 1;                                          // killed by the hurt: no event
    hero.merc->life_frac = 0;
    hurt(rig);
    assert(hero.merc->hit_points == 0 && hero.merc->stats.regen == 0);
    std::puts("OK: poison files the event too; a killing hurt or a total of 0 doesn't");
    return 0;
}

// A healing potion the merc drinks files the event (FUN_005be3f0): needs Misc.txt's potions.
int potion_check(const GameData& game_data) {
    d2d::rules::Rng rng(7);
    constexpr int kPaladin = 3;
    auto made = new_character(game_data, kPaladin, "Headless", false, true, rng);
    Character character;
    character.character_class = kPaladin;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(game_data, character.header, character.items, character.stats);
    World world(&game_data, -1, -1);
    world.enter(character);
    auto& actor = *world.actor;
    actor.merc = Monster{};
    actor.merc_st.life = 1000;
    actor.merc->hit_points = 500;
    world.fight.merc_drinks("hp1", 3000);
    assert(actor.merc->stats.regen == 128);
    std::puts("OK: a healing potion starts the merc's regeneration");
    return 0;
}

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
    if (const int failed = regen_checks()) return failed;
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: the potion check needs D2_MPQ_DIR"); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::puts("FAIL: load_game_data failed"); return 1; }
    return potion_check(*data);
} catch (const std::exception& error) {
    std::printf("test_merc_regen: %s\n", error.what());
    return 1;
}
