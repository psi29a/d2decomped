// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Energy Shield absorbs on the unresisted record field, before the resistance pass (FUN_005ca6c0 in state event 0xb,
// raised on each record before FUN_0057c1e0 reduces it), and drains the mana as game.exe does: per field,
// a = MulDiv(field, calc1, 100) capped at MulDiv(mana, 16, calc2), mana -= MulDiv(a, calc2, 16), in 256ths.
// With no game data the rules::energy_shield_blows checks run; with it the REAL Fight::absorb runs a grid of
// single-element blows against the same arithmetic written out here.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monsters.hpp>
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
#include <span>
#include <utility>

using namespace d2d::game;
namespace rules = d2d::rules;

namespace {

// game.exe's arithmetic for one single-element record, written out: the shield on the raw field, then the resistance.
// element 0 physical (resistance = damage reduction %), 1 magic (no percent resistance on a monster blow here), 2 fire,
// 3 lightning, 4 cold, 5 poison (no field in the shield's table).
struct Expect { std::int64_t life = 0, mana = 0; };
Expect expected(int element, int res, int whole, int pct, int per16, std::int64_t mana) {
    std::int64_t field = std::int64_t(whole) * 256;
    if (element != 5 && pct > 0) {
        const std::int64_t part = std::max<std::int64_t>(std::min(field * pct / 100, mana * 16 / per16), 0);
        mana = std::max<std::int64_t>(mana - part * per16 / 16, 0);
        field -= part;
    }
    if (element == 5) return { 0, mana };
    if (element == 1) return { field, mana };
    return { element == 0 ? rules::reduce_fine(int(field), res, 0) : rules::resisted_fine(int(field), res), mana };
}

rules::StruckBlow fire_blow(int roll, int res, int mdr = 0) {
    rules::Fighter defender;
    defender.res[0] = res;
    defender.mdr = mdr;
    rules::MonStats attacker;
    attacker.level = 1;
    for (auto& element : attacker.elements) element.type = -1;
    attacker.elements[0] = { 0, 100, roll, roll, 25, "A1" };
    rules::Rng rng(1);
    const auto taken = rules::monster_blow(defender, 1, rules::Motion::running, attacker, false, rng, false, true, true);   // a record made without the roll
    rules::StruckBlow blow{ taken.damage, 0, taken.elemental > 0, taken.physical, taken.elemental, false, taken.frac, taken.raw, 0, 0 };
    blow.parts[0] = { taken.element_raw[0], taken.element_dealt[0], res };
    blow.mdr = mdr;
    return blow;
}

void rules_checks() {
    // The first differing case of the differential (fire, resistance -100, 632 raw): the shield takes its share of the raw 632 and the
    // vulnerability then doubles what is left, instead of taking it from the doubled 1264. calc1 = 40 % here, calc2 = 32.
    {
        auto blow = fire_blow(632, -100);
        assert(blow.damage == 1264);
        rules::EnergyShield shield{ 40, 32, 103179 };   // mana 403.0 points
        rules::energy_shield_blows(std::span(&blow, 1), shield);
        const std::int64_t cap = 103179LL * 16 / 32;                       // 51,589 256ths of damage
        const std::int64_t part = std::min<std::int64_t>(632LL * 256 * 40 / 100, cap);
        assert(shield.absorbed == part);
        assert(blow.damage * 256 + blow.frac == (632 * 256 - part) * 2);   // the rest, doubled (res -100)
        assert(shield.mana == 103179 - part * 32 / 16);
    }
    {   // zero shield level (calc1 0): nothing taken, the blow and the mana untouched
        auto blow = fire_blow(100, 50);
        const auto before = blow;
        rules::EnergyShield shield{ 0, 32, 5000 };
        rules::energy_shield_blows(std::span(&blow, 1), shield);
        assert(shield.absorbed == 0 && shield.mana == 5000 && blow.damage == before.damage && blow.frac == before.frac);
    }
    {   // mana 0: nothing taken
        auto blow = fire_blow(100, 0);
        rules::EnergyShield shield{ 40, 32, 0 };
        rules::energy_shield_blows(std::span(&blow, 1), shield);
        assert(shield.absorbed == 0 && shield.mana == 0 && blow.damage == 100);
    }
    {   // immune: the shield still pays mana for the raw field (the resistance comes after), and the blow stays at 0
        auto blow = fire_blow(100, 100);
        assert(blow.damage == 0);
        rules::EnergyShield shield{ 40, 32, 100 * 256 };
        rules::energy_shield_blows(std::span(&blow, 1), shield);
        assert(shield.absorbed == 100 * 256 * 40 / 100 && blow.damage == 0 && blow.frac == 0 && shield.mana < 100 * 256);
    }
    {   // the flat magic reduction comes off BEFORE the resistance (FUN_0057bf80): 100 fire, a 20 % shield, mdr 10, 50 %: (80 - 10) * 0.5
        // (game.exe, run natively: 100 fire at 50 % with mdr 10 and no shield deals 45, not 40)
        auto blow = fire_blow(100, 50, 10);
        rules::EnergyShield shield{ 20, 1, 1'000'000 };
        rules::energy_shield_blows(std::span(&blow, 1), shield);
        assert(blow.damage * 256 + blow.frac == 35 * 256 && blow.elemental == 35);
    }
    {   // a field the shield leaves under the mdr stays negative and is summed into the record (game.exe, run natively; it was clamped at 0 per
        // field): a 50 % shield on 10 fire and 100 cold, mdr 6: fire 5 - 6 = -1, cold 50 - 6 = 44, the record deals 43
        auto blow = fire_blow(10, 0, 6);
        blow.parts[2] = { 100, 94, 0 };   // 100 cold, dealt 94
        blow.damage += 94; blow.elemental += 94;
        rules::EnergyShield shield{ 50, 1, 10'000'000 };
        rules::energy_shield_blows(std::span(&blow, 1), shield);
        assert(blow.damage * 256 + blow.frac == 43 * 256);   // (-1 + 44) points: the negative fire field takes damage off the cold one
        const auto control = [&] {   // zero-flat control: the same blow with mdr 0 has no negative field
            auto plain = fire_blow(10, 0, 0);
            plain.parts[2] = { 100, 100, 0 };
            plain.damage += 100; plain.elemental += 100;
            rules::EnergyShield same{ 50, 1, 10'000'000 };
            rules::energy_shield_blows(std::span(&plain, 1), same);
            return plain.damage * 256 + plain.frac;
        }();
        assert(control == 55 * 256);   // 5 + 50
    }
    {   // physical: 215 under a 95 % shield leaves 10.75, flat 15 makes -4.25; with 39 lightning and 10 cold it is summed (the native probe: 925/256)
        rules::StruckBlow blow{ 0, 0, false, 0, 0 };
        blow.raw = 215 * 256; blow.pct = 0; blow.flat = 15;
        blow.physical = 200; blow.damage = 200;
        blow.parts[0] = { 39, 33, 0 };    // fire stands for lightning here: the type does not change the arithmetic
        blow.parts[2] = { 10, 4, 0 };
        blow.damage += 37; blow.elemental = 37; blow.marked = true;
        blow.mdr = 6;
        rules::EnergyShield shield{ 95, 32, 119494 };
        rules::energy_shield_blows(std::span(&blow, 1), shield);
        // mana lasts for the physical field (104,576) and 7,459 of the 39 fire (field 9984); nothing is left for the cold:
        // physical 2752 - 3840 = -1088, fire 2525 - 1536 = 989, cold 2560 - 1536 = 1024 -> 925
        assert(blow.damage * 256 + blow.frac == 925 && shield.mana == 0);
    }
    {   // a field taken to nothing loses its element mark; poison keeps it
        auto blow = fire_blow(10, 0);
        rules::EnergyShield shield{ 95, 1, 1'000'000 };
        rules::energy_shield_blows(std::span(&blow, 1), shield);
        assert(blow.damage == 0 && blow.frac == 128 && blow.elemental == 0 && blow.marked == false);
    }
    {   // mana runs out across two records in landing order: the second gets what is left
        std::array<rules::StruckBlow, 2> blows{ fire_blow(100, 0), fire_blow(100, 0) };
        rules::EnergyShield shield{ 50, 16, 60 * 256 };   // 16 per16: one mana point per point absorbed
        rules::energy_shield_blows(blows, shield);
        assert(blows[0].damage == 50 && blows[1].damage == 90 && shield.mana == 0);
    }
    {   // damage no field accounts for is taken as it stands
        rules::StruckBlow loose{ 20, 0, true, 0, 20 };
        rules::EnergyShield shield{ 50, 16, 1'000'000 };
        rules::energy_shield_blows(std::span(&loose, 1), shield);
        assert(loose.damage == 10 && shield.absorbed == 10 * 256);
    }
    std::puts("OK: Energy Shield on the raw field, then the resistance (rules)");
}

}   // namespace

int main() try {
    rules_checks();
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::printf("FAIL: load_game_data failed\n"); return 1; }
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    rules::Rng made_rng(7);
    auto made = new_character(*data, 4, "Headless", false, true, made_rng);
    Character character;
    character.character_class = 4;   // not a Sorceress: the shield's sound cue needs no cue table
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    constexpr std::uint32_t kTick = 40;
    for (std::uint32_t tick = 1; tick <= 25; ++tick) world.tick({}, tick * kTick, (tick - 1) * kTick);
    auto& fight = world.fight;
    auto& hero = *world.actor;
    int es_id = -1;
    for (int id = 0; id < 400 && es_id < 0; ++id)
        if (const auto* skill = data->skills.get(id); skill && skill->aurastate == "energyshield") es_id = id;
    assert(es_id >= 0);
    const auto* es_skill = data->skills.get(es_id);
    auto states = hero.self_states;
    const std::uint32_t now = 30 * kTick;

    // One blow of `whole` raw points through the REAL monster_blow, Foe::take and Fight::absorb.
    // element as in `expected`; returns the life lost in whole points and leaves the mana in the hero's stats.
    auto run = [&](int element, int res, int whole, int es_level, std::int64_t mana) {
        rules::MonStats attacker;
        attacker.level = 1;
        for (auto& element_row : attacker.elements) element_row.type = -1;
        Foe foe = fight.player_foe(now);
        foe.fighter.dr_pct = 0; foe.fighter.dr_flat = 0; foe.fighter.mdr = 0;
        for (auto& resistance : foe.fighter.res) resistance = 0;
        if (element == 0) { attacker.a1_min = attacker.a1_max = whole; foe.fighter.dr_pct = res; }
        else {
            const int type = element == 2 ? 0 : element == 3 ? 1 : element == 4 ? 2 : element == 5 ? 3 : 4;
            attacker.elements[0] = { type, 100, whole, whole, 25, "A1" };
            if (type < 4) foe.fighter.res[std::size_t(type)] = res;
        }
        rules::Rng rng(12345);
        foe.take(rules::monster_blow(foe.fighter, 1, rules::Motion::running, attacker, false, rng, false, true, true));   // exact: min == max, no roll
        hero.self_states = states;
        hero.absorb_pool = 0; hero.absorb_skill = -1;
        if (es_level > 0) hero.self_states.push_back({ es_id, es_level, ~0u });
        hero.character.stats.values[d2d::d2s::kMana] = mana;
        hero.self_hurt = 0;
        hero.pmode = -1;
        const int lost = fight.absorb(foe, now);
        return std::pair{ std::int64_t(lost) * 256 + foe.frac, hero.character.stats.values[d2d::d2s::kMana] };
    };

    // The first differing case of the differential: fire, resistance -100, raw 632, level 40, mana 103179/256.
    {
        const int pct = std::clamp(fight.calc(*es_skill, es_skill->calc[0], 40), 0, 95), per16 = std::max(fight.calc(*es_skill, es_skill->calc[1], 40), 1);
        const auto [life, mana] = run(2, -100, 632, 40, 103179);
        const auto want = expected(2, -100, 632, pct, per16, 103179);
        assert(life >> 8 == want.life >> 8 && mana == want.mana);
        assert(life >> 8 == 860 && mana == 1);   // game.exe's answer in the differential: 860 points of life, 1/256 mana left
    }

    // A grid over the six damage types, resistances and the edge cases: shield level 0 (no state), mana 0, immune, vulnerable.
    int cases = 0;
    for (const int element : { 0, 1, 2, 3, 4, 5 })
        for (const int res : { -100, -50, 0, 50, 99, 100 })
            for (const int whole : { 1, 5, 50, 632, 1500 })
                for (const int level : { 0, 1, 18, 40 })
                    for (const std::int64_t mana : { std::int64_t(0), std::int64_t(3 * 256 + 100), std::int64_t(100 * 256), std::int64_t(600 * 256) }) {
                        const int pct = level > 0 ? std::clamp(fight.calc(*es_skill, es_skill->calc[0], level), 0, 95) : 0;
                        const int per16 = level > 0 ? std::max(fight.calc(*es_skill, es_skill->calc[1], level), 1) : 1;
                        const auto [life, mana_left] = run(element, element == 1 ? 0 : res, whole, level, mana);
                        const auto want = expected(element, element == 1 ? 0 : res, whole, pct, per16, mana);
                        if (element == 5) {   // poison is a damage over time, never touched by the shield
                            assert(life == 0 && mana_left == mana);
                            continue;
                        }
                        if ((life >> 8) != (want.life >> 8) || mana_left != want.mana)
                            std::fprintf(stderr, "FAIL: element %d res %d raw %d level %d mana %lld: d2d life %lld mana %lld, game order life %lld mana %lld\n", element, res, whole, level,
                                        static_cast<long long>(mana), static_cast<long long>(life), static_cast<long long>(mana_left), static_cast<long long>(want.life), static_cast<long long>(want.mana));
                        assert((life >> 8) == (want.life >> 8) && mana_left == want.mana);
                        ++cases;
                    }
    // Bone Armor and Energy Shield together: the state cast later takes from the record first (FUN_005c0ad0, 0x5c0ad0, puts
    // its event node at the head of the unit's list; a re-cast, FUN_005c9480 / FUN_005c9c10, re-registers it there).
    {
        int bone_id = -1;
        for (int id = 0; id < 400 && bone_id < 0; ++id)
            if (const auto* skill = data->skills.get(id); skill && skill->aurastate == "bonearmor") bone_id = id;
        assert(bone_id >= 0);
        int es_level = 1;   // the first level that takes at least 40 %
        while (es_level < 60 && fight.calc(*es_skill, es_skill->calc[0], es_level) < 40) ++es_level;
        const int pct = std::clamp(fight.calc(*es_skill, es_skill->calc[0], es_level), 0, 95);
        assert(pct >= 40 && pct < 95);
        auto both = [&](bool shield_last) {
            rules::MonStats attacker;
            attacker.level = 1;
            for (auto& element_row : attacker.elements) element_row.type = -1;
            attacker.a1_min = attacker.a1_max = 100;
            Foe foe = fight.player_foe(now);
            foe.fighter.dr_pct = 0; foe.fighter.dr_flat = 0; foe.fighter.mdr = 0;
            rules::Rng rng(12345);
            foe.take(rules::monster_blow(foe.fighter, 1, rules::Motion::running, attacker, false, rng, false, true, true));
            hero.self_states = states;
            using SelfState = decltype(hero.self_states)::value_type;
            const SelfState shield{ es_id, es_level, ~0u }, bone{ bone_id, 1, ~0u };
            hero.self_states.push_back(shield_last ? bone : shield);
            hero.self_states.push_back(shield_last ? shield : bone);
            hero.absorb_pool = 70; hero.absorb_frac = 0; hero.absorb_skill = bone_id;
            hero.character.stats.values[d2d::d2s::kMana] = 600 * 256;
            hero.self_hurt = 0;
            hero.pmode = -1;
            return std::pair{ fight.absorb(foe, now), hero.absorb_pool };
        };
        // The shield cast last: it takes pct % of the 100 first, the pool (70) all the rest.
        const auto [shield_life, shield_pool] = both(true);
        assert(shield_life == 0 && shield_pool > 0);
        // The armor cast last: its pool takes 70 first, the shield pct % of the 30 left.
        const auto [bone_life, bone_pool] = both(false);
        std::printf("Bone Armor and Energy Shield (%d %%): shield last %d life %d pool, armor last %d life %d pool\n", pct, shield_life, shield_pool, bone_life, bone_pool);
        assert(bone_life > 0 && bone_life < 30 && bone_pool == 0);
    }
    hero.self_states = std::move(states);
    std::printf("OK: Energy Shield through the real Fight::absorb, %d cases, life and mana as game.exe's order\n", cases);
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
