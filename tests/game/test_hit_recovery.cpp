// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A player's block (BL, mode 9) on the server: how long it holds (FUN_00623f50's block branch, then FUN_005539b0's end event).
//   `--block-length`: every class x main-hand weapon x FBR 0..300 x Holy Shield off / on, one line each
//   `D class weapon fbr holy record frames speed ticks`: the ticks Fight::set_pmode(BL) holds, as `tools/emu/block_len.py` reads them.

#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

using namespace d2d::game;

namespace {

// A character of class `cls` wearing `weapon` (a weapons.txt code, "" = barehanded) in the main hand, no shield.
Character make_hero(const GameData& data, int cls, const std::string& weapon) {
    d2d::rules::Rng rng(7);
    auto made = new_character(data, cls, "Headless", false, true, rng);
    Character character;
    character.character_class = cls;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    for (const int stat : { d2d::d2s::kStr, d2d::d2s::kDex }) character.stats.values[std::size_t(stat)] = 255;
    character.items = made.items;
    const d2d::d2s::Item* proto = character.items.empty() ? nullptr : &character.items.front();
    d2d::d2s::Item put = proto ? *proto : d2d::d2s::Item{};
    std::erase_if(character.items, [](const auto& item) { return item.location == 1 && (item.slot == 4 || item.slot == 5); });
    if (!weapon.empty()) { put.code = weapon; put.location = 1; put.slot = 4; character.items.push_back(put); }
    character.panel = panel_stats(data, character.header, character.items, character.stats);
    return character;
}

}   // namespace

int main(int argc, char** argv) try {
    const bool grid = argc > 1 && std::strcmp(argv[1], "--block-length") == 0;
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir) { std::printf("SKIP: D2_MPQ_DIR\n"); return D2D_TEST_SKIPPED; }
    const fs::path data_dir = mpq_dir;
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->patched);

    int holy_skill = -1;   // the skill whose aurastate is Holy Shield's state 0x65
    for (int id = 0; id < 400 && holy_skill < 0; ++id)
        if (const auto* skill = data->skills.get(id); skill && skill->aurastate == "holyshield") holy_skill = id;
    assert(holy_skill >= 0);

    // none, sword (1hs), dagger (1ht), great sword (2hs), spear (2ht), staff (stf), bow, crossbow (xbw), claws (ht1)
    const std::vector<std::string> weapons = { "", "ssd", "dgr", "gsd", "spr", "sst", "sbw", "lxb", "ktr" };
    // The ticks the block holds with `fbr` and Holy Shield on or off, through the real Fight::set_pmode; the record is the one the class and weapon pick.
    const auto block = [&](World& world, int fbr, bool holy, std::uint32_t& now, std::string& record, unsigned& frames, unsigned& speed) {
        auto& actor = *world.actor;
        now += 1000;
        actor.player_combat.fbr = fbr;
        actor.self_states.clear();
        if (holy) actor.self_states.push_back({ holy_skill, 1, now + 100000 });
        const auto& anim = world.fight.player_anim(kModeBL);
        record = anim.name; frames = anim.frames; speed = anim.speed ? anim.speed : 256;
        if (!anim.frames) return -1;
        world.fight.set_pmode(kModeBL, now);
        return int((actor.pmode_until - now) / 40);
    };
    if (grid) {
        for (int cls = 0; cls < 7; ++cls)
            for (const auto& weapon : weapons) {
                if (weapon == "ktr" && cls != d2d::d2s::kAssassin) continue;   // claws are the Assassin's
                auto hero = make_hero(*data, cls, weapon);
                World world(&*data, -1, -1);
                world.enter(hero);
                world.actor->character.appearance = data->look_of(hero.items);
                world.fight.update_fighters(1000 * kTickMs);
                std::uint32_t now = 1000 * kTickMs;
                for (const int holy : { 0, 1 })
                    for (int fbr = 0; fbr <= 300; ++fbr) {
                        std::string record; unsigned frames = 0, speed = 0;
                        const int ticks = block(world, fbr, holy, now, record, frames, speed);
                        if (ticks < 0) continue;
                        std::printf("D %d %s %d %d %s %u %u %d\n", cls, weapon.empty() ? "none" : weapon.c_str(), fbr, holy, record.c_str(), frames, speed, ticks);
                    }
            }
        return 0;
    }
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "test_hit_recovery: %s\n", e.what());
    return 1;
}
