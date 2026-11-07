// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A stationary player, not attacking. Fallen1's real swing is rolled, then
// one blow under the get-hit band and one over it go through Fight::hurt_side.
// A one-point hit stays idle and still queues the struck sounds. Half of
// max life enters get-hit and queues those sounds once.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <sound_ids.hpp>
#include <sound_mix.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

using namespace d2d::game;

namespace {

const char* mode_name(int mode) {
    if (mode < 0) return "idle";
    if (mode == kModeGH) return "GH";
    if (mode == kModeBL) return "BL";
    if (mode == kModeA1) return "A1";
    if (mode == kModeNU) return "NU";
    return "other";
}

std::string sound_name(const GameData& data, int id) {
    for (const auto& [name, index] : data.sound_index)
        if (index == id) return name;
    return {};
}

void stand(World::Player& hero) {
    hero.pmode = -1;
    hero.player.walking = false;
    hero.running = false;
    hero.owner_mode = 1;
}

void report_cues(const GameData& data, const Cues& cues, std::string_view label) {
    std::printf("  cues %s: %zu\n", std::string(label).c_str(), cues.due.size());
    for (const auto& cue : cues.due) {
        const bool row = cue.sound > 0 && std::size_t(cue.sound) < data.sounds.size();
        const auto& sound = row ? data.sounds[std::size_t(cue.sound)] : GameData::Sound{};
        const auto [screen_x, screen_y2] = d2d::rules::screen_offset(0.f, 0.f);
        const auto mix = d2d::rules::sound_mix_pixels(row ? sound.volume : 0, row ? sound.falloff : 0, screen_x, screen_y2);
        std::printf("    id %d name %s file %s hush %u mix_audible %d mix_volume %d\n", cue.sound, sound_name(data, cue.sound).c_str(),
                    row ? sound.file.c_str() : "", cue.hush, int(mix.audible), mix.volume);
    }
    std::printf("  playback: not observed (no audio device opened)\n");
}

}  // namespace

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) {
        std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str());
        return D2D_TEST_SKIPPED;
    }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) {
        std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n");
        return D2D_TEST_SKIPPED;
    }

    d2d::rules::Rng made{ 7 };
    auto made_character = new_character(*data, 0, "Still", false, true, made);
    Character character;
    character.character_class = 0;
    character.name = "Still";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    auto& hero = *world.actor;
    stand(hero);
    const std::uint32_t now_ms = 1000;
    world.fight.update_fighters(now_ms);
    auto foe = world.fight.player_foe(now_ms);
    assert(!foe.running && !foe.moving && hero.pmode < 0);

    const auto fallen_row = data->monsters.by_id.find("fallen1");
    assert(fallen_row != data->monsters.by_id.end());
    d2d::rules::Rng spawn{ 3 };
    Monster fallen = make_monster(*data, fallen_row->second, hero.player.x + 1.f, hero.player.y, spawn, 0);
    const int hit_class = data->monsters.types[std::size_t(fallen.type)].hit_class;
    const int max_life = int(hero.character.stats.fixed(d2d::d2s::kMaxLife));
    const int chance = d2d::rules::hit_chance(fallen.stats.to_hit, foe.fighter.defense + foe.fighter.def_melee, fallen.stats.level, foe.level);
    std::printf("stationary amazon pmode %s walking %d owner_mode %d\n", mode_name(hero.pmode), int(hero.player.walking), hero.owner_mode);
    std::printf("max_life %d defense %d block %d\n", max_life, foe.fighter.defense + foe.fighter.def_melee, foe.fighter.block);
    std::printf("fallen1 to_hit %d level %d damage %d-%d hit_class %d chance %d\n", fallen.stats.to_hit, fallen.stats.level, fallen.stats.a1_min,
                fallen.stats.a1_max, hit_class, chance);

    d2d::rules::Rng rng{ 1 };
    int miss = 0, blocked = 0, dodged = 0, hit = 0, eligible = 0;
    d2d::rules::Taken first_hit;
    bool have_hit = false;
    for (int n = 0; n < 1000; ++n) {
        const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, d2d::rules::Motion::standing, fallen.stats, false, rng);
        if (taken.blocked) ++blocked;
        else if (taken.dodged) ++dodged;
        else if (!taken.hit) ++miss;
        else {
            ++hit;
            if (!have_hit) { first_hit = taken; have_hit = true; }
            d2d::rules::Rng gate{ 1 };
            if (taken.damage > 0 && d2d::rules::get_hit(hero.character.stats.get(d2d::d2s::kMaxLife), std::int64_t(taken.damage) << 8, hit_class, false, gate))
                ++eligible;
        }
    }
    std::printf("standing rolls 1000 seed 1: miss %d block %d dodge %d hit %d get_hit_eligible %d\n", miss, blocked, dodged, hit, eligible);
    assert(miss + blocked + dodged + hit == 1000);

    const auto life0 = hero.character.stats.values[d2d::d2s::kLife];
    if (have_hit) {
        world.cues.due.clear();
        stand(hero);
        auto side = std::array{ world.fight.player_foe(now_ms), world.fight.merc_foe(now_ms) };
        side[0].take(first_hit);
        side[0].hit_class = hit_class;
        world.fight.hurt_side(side, now_ms);
        const auto lost = int((life0 - hero.character.stats.values[d2d::d2s::kLife]) >> 8);
        std::printf("first natural hit damage %d life_lost %d pmode %s\n", first_hit.damage, lost, mode_name(hero.pmode));
        report_cues(*data, world.cues, "natural");
        hero.character.stats.values[d2d::d2s::kLife] = life0;
        stand(hero);
        world.cues.due.clear();
    }

    const auto apply = [&](int whole, std::string_view label) {
        world.cues.due.clear();
        stand(hero);
        hero.character.stats.values[d2d::d2s::kLife] = life0;
        d2d::rules::MonStats fixed = fallen.stats;
        fixed.a1_min = fixed.a1_max = whole;
        d2d::rules::Rng once{ 1 };
        const auto taken = d2d::rules::monster_blow(foe.fighter, foe.level, d2d::rules::Motion::standing, fixed, false, once, false, true, true);
        assert(taken.hit);
        auto side = std::array{ world.fight.player_foe(now_ms), world.fight.merc_foe(now_ms) };
        side[0].take(taken);
        side[0].hit_class = hit_class;
        d2d::rules::Rng gate{ 1 };
        const bool big = taken.damage > 0 && d2d::rules::get_hit(hero.character.stats.get(d2d::d2s::kMaxLife), std::int64_t(taken.damage) << 8, hit_class, false, gate);
        world.fight.hurt_side(side, now_ms);
        const auto lost = int((life0 - hero.character.stats.values[d2d::d2s::kLife]) >> 8);
        std::printf("%s forced damage %d (rolled %d) life_lost %d get_hit %d pmode %s\n", std::string(label).c_str(), whole, taken.damage, lost, int(big),
                    mode_name(hero.pmode));
        report_cues(*data, world.cues, label);
        return std::tuple{ lost, big, hero.pmode };
    };

    const auto [small_lost, small_big, small_mode] = apply(1, "below");
    const int voice = d2d::rules::sound_ids::kPlayerVoice[std::size_t(hero.character.character_class)].hit;
    const int impact = d2d::rules::sound_ids::impact(hit_class, false).impact;
    // One point stays idle, and still asks for the struck sounds (client command 0x13).
    assert(small_lost > 0 && !small_big && small_mode != kModeGH);
    assert(std::ranges::count_if(world.cues.due, [&](const Cues::Cue& cue) { return cue.sound == impact; }) == 1);
    assert(std::ranges::count_if(world.cues.due, [&](const Cues::Cue& cue) { return cue.sound == voice && cue.hush == Cues::kHush; }) == 1);

    const int half = std::max(max_life / 2, 1);
    const auto [large_lost, large_big, large_mode] = apply(half, "above");
    assert(large_lost > 0 && large_big && large_mode == kModeGH);
    assert(std::ranges::count_if(world.cues.due, [&](const Cues::Cue& cue) { return cue.sound == impact; }) == 1);
    assert(std::ranges::count_if(world.cues.due, [&](const Cues::Cue& cue) { return cue.sound == voice && cue.hush == Cues::kHush; }) == 1);
    assert(impact > 0 && std::size_t(impact) < data->sounds.size() && !data->sounds[std::size_t(impact)].file.empty());
    assert(voice > 0 && std::size_t(voice) < data->sounds.size() && !data->sounds[std::size_t(voice)].file.empty());
    std::printf("incoming_hit: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_incoming_hit: %s\n", error.what());
    return 1;
}
