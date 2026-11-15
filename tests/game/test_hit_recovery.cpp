// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A player's block (BL, mode 9) on the server: how long it holds (FUN_00623f50's block branch, then FUN_005539b0's end event),
// and whether a hit puts a unit already in a get-hit or block into one again (FUN_0057edd0 refuses GH and BL from GH and BL).
//   `--block-length`: every class x main-hand weapon x FBR 0..300 x Holy Shield off / on, one line each
//   `D class weapon fbr holy record frames speed ticks`: the ticks Fight::set_pmode(BL) holds, as `tools/emu/block_len.py` reads them.
//   `--restart`: two big hits on a Paladin 5 ticks apart: `R first_end second_end` (ticks from the first hit; game.exe holds the
//   second hit's end at the first's).
//   `--hit-modes`: one big hit on a Paladin standing in each of modes DT..SQ (0..18) with a hold running, then in a knockback
//   slide (19, drawn as GH with `knock` set): `M mode after restarted` (the mode right after the hit, and 1 when the hold's
//   end moved; the cells FUN_0057edd0 answers for GH).
//   `--knock-modes`: one knocking hit (Foe::knocked_from set, 1 point of damage: no get-hit of its own) on a Paladin standing in each of
//   modes DT..SQ (0..18) with a hold running, then in a knockback slide (19): `K mode after knock restarted` (the mode right after, 1 when
//   a knockback slide is set, and 1 when the hold's end moved: the cells FUN_00580a70 -> FUN_0057edd0 answers for new mode 0x13).
//   no argument: the rules (block_rate, block_ticks) and, with the game data (D2_MPQ_DIR), a Paladin's block and the get-hit hold.

#include <ai.hpp>
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

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
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
    const bool restart = argc > 1 && std::strcmp(argv[1], "--restart") == 0;
    if (!grid && !restart) {
        // The block rate and length (FUN_00623f50's block branch, FUN_005539b0): a Paladin's PABL1HS (3 frames at 256) holds 6 ticks at FBR 0 and 3 at FBR 86 (13 / 32 / 86 are
        // the breakpoints), with Holy Shield's state 0.5 x the rate more: 3 ticks at FBR 0..85 and 2 from 86.
        using d2d::rules::block_rate;
        using d2d::rules::block_ticks;
        assert(block_rate(256, 0, false) == 128 && block_rate(256, 0, true) == 256 && block_rate(1, 0, false) == 1 && block_rate(0, 0, false) == 1);
        for (const auto& [fbr, ticks] : { std::pair{ 0, 6 }, { 12, 6 }, { 13, 5 }, { 31, 5 }, { 32, 4 }, { 85, 4 }, { 86, 3 }, { 300, 3 } })
            assert(block_ticks(3, 256, fbr, false) == ticks);
        for (const auto& [fbr, ticks] : { std::pair{ 0, 3 }, { 85, 3 }, { 86, 2 }, { 300, 2 } })
            assert(block_ticks(3, 256, fbr, true) == ticks);
        assert(block_ticks(0, 256, 0, false) == 2 && block_ticks(5, 256, 0, false) == 10);   // no frames: the end event's floor of 2 ticks
    }
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
    if (std::strcmp(argc > 1 ? argv[1] : "", "--hit-modes") == 0) {
        for (int mode = 0; mode <= 18; ++mode) {
            auto hero = make_hero(*data, d2d::d2s::kPaladin, "ssd");
            World world(&*data, -1, -1);
            world.enter(hero);
            world.fight.update_fighters(1000 * kTickMs);
            auto& actor = *world.actor;
            std::array<Foe, 2> side{};
            side[0].hits = 1;
            side[0].damage = 20;
            side[0].blows.push_back(d2d::rules::StruckBlow{ .damage = 20, .landed = true });
            const std::uint32_t start_ms = 1000 * kTickMs;
            actor.pmode = mode;
            actor.pmode_until = start_ms + 10 * 40;
            world.fight.hurt_side(side, start_ms + 40);
            std::printf("M %d %d %d\n", mode, actor.pmode, actor.pmode_until != start_ms + 10 * 40 ? 1 : 0);
        }
        // Mode 19: game.exe's knockback (0x13), which d2d draws as GH with `knock` set; `restarted` is 1 when the hit began a get-hit
        // (the hold's end moved).
        {
            auto hero = make_hero(*data, d2d::d2s::kPaladin, "ssd");
            World world(&*data, -1, -1);
            world.enter(hero);
            world.fight.update_fighters(1000 * kTickMs);
            auto& actor = *world.actor;
            std::array<Foe, 2> side{};
            side[0].hits = 1;
            side[0].damage = 20;
            side[0].blows.push_back(d2d::rules::StruckBlow{ .damage = 20, .landed = true });
            const std::uint32_t start_ms = 1000 * kTickMs;
            actor.pmode = kModeGH;
            actor.pmode_until = start_ms + 10 * 40;
            actor.knock = std::array{ float(actor.player.x), float(actor.player.y), float(actor.player.x) + 5.f, float(actor.player.y) };
            world.fight.hurt_side(side, start_ms + 40);
            std::printf("M 19 %d %d\n", actor.pmode, actor.pmode_until != start_ms + 10 * 40 ? 1 : 0);
        }
        return 0;
    }
    if (std::strcmp(argc > 1 ? argv[1] : "", "--knock-modes") == 0) {
        // The same cells with a knocking hit (result flag 8, Smite / a monster's knock) of 1 point: only the knockback can start a mode.
        for (int mode = 0; mode <= 19; ++mode) {
            auto hero = make_hero(*data, d2d::d2s::kPaladin, "ssd");
            World world(&*data, -1, -1);
            world.enter(hero);
            world.fight.update_fighters(1000 * kTickMs);
            auto& actor = *world.actor;
            std::array<Foe, 2> side{};
            side[0].hits = 1;
            side[0].damage = 1;
            side[0].blows.push_back(d2d::rules::StruckBlow{ .damage = 1, .landed = true });
            side[0].knocked_from = std::pair{ actor.player.x - 1.f, actor.player.y };
            const std::uint32_t start_ms = 1000 * kTickMs;
            const std::uint32_t hold_end = start_ms + 17 * 40;
            actor.pmode = mode == 19 ? kModeGH : mode;
            actor.pmode_until = hold_end;
            if (mode == 19) actor.knock = std::array{ float(actor.player.x), float(actor.player.y), float(actor.player.x) + 5.f, float(actor.player.y) };
            world.fight.hurt_side(side, start_ms + 40);
            std::printf("K %d %d %d %d\n", mode, actor.pmode, actor.knock ? 1 : 0, actor.pmode_until != hold_end ? 1 : 0);
        }
        return 0;
    }
    // Two big hits on a Paladin 5 ticks apart, through the real Fight::hurt_side; ticks from the first hit to the end of the hold.
    const auto two_hits = [&](int& first_end, int& second_end) {
        auto hero = make_hero(*data, d2d::d2s::kPaladin, "ssd");
        World world(&*data, -1, -1);
        world.enter(hero);
        world.fight.update_fighters(1000 * kTickMs);
        auto& actor = *world.actor;
        const auto hit = [&](std::uint32_t when_ms) {
            std::array<Foe, 2> side{};
            side[0].hits = 1;
            side[0].damage = 20;                                   // over a quarter of a clvl 1 Paladin's life: a get-hit at once
            side[0].blows.push_back(d2d::rules::StruckBlow{ .damage = 20, .landed = true });
            actor.character.stats.values[d2d::d2s::kLife] = actor.character.stats.values[d2d::d2s::kMaxLife];
            world.fight.hurt_side(side, when_ms);
        };
        const std::uint32_t start_ms = 1000 * kTickMs;
        actor.pmode = -1;
        hit(start_ms);
        assert(actor.pmode == kModeGH);
        first_end = int((actor.pmode_until - start_ms) / 40);
        hit(start_ms + 5 * 40);
        second_end = int((actor.pmode_until - start_ms) / 40);
    };
    if (restart) {
        int first = 0, second = 0;
        two_hits(first, second);
        std::printf("R %d %d\n", first, second);
        return 0;
    }
    // A Paladin with a sword: PABL1HS at FBR 0 holds the block 6 ticks, 3 with Holy Shield's state.
    {
        auto hero = make_hero(*data, d2d::d2s::kPaladin, "ssd");
        World world(&*data, -1, -1);
        world.enter(hero);
        world.actor->character.appearance = data->look_of(hero.items);
        world.fight.update_fighters(1000 * kTickMs);
        std::uint32_t now = 1000 * kTickMs;
        std::string record; unsigned frames = 0, speed = 0;
        assert(block(world, 0, false, now, record, frames, speed) == 6 && frames == 3 && speed == 256);
        assert(block(world, 0, true, now, record, frames, speed) == 3);
        assert(block(world, 86, false, now, record, frames, speed) == 3 && block(world, 86, true, now, record, frames, speed) == 2);
    }
    // Two big hits 5 ticks apart: the get-hit is not restarted (FUN_0057edd0 refuses GH from GH): both holds end 10 ticks after the first hit.
    {
        int first = 0, second = 0;
        two_hits(first, second);
        assert(first == 10 && second == 10);
    }
    // A big hit during a knockback slide is a get-hit (game.exe allows mode 4 from mode 0x13: 0x57ee0b) and ends the slide.
    {
        auto hero = make_hero(*data, d2d::d2s::kPaladin, "ssd");
        World world(&*data, -1, -1);
        world.enter(hero);
        world.fight.update_fighters(1000 * kTickMs);
        auto& actor = *world.actor;
        std::array<Foe, 2> side{};
        side[0].hits = 1;
        side[0].damage = 20;
        side[0].blows.push_back(d2d::rules::StruckBlow{ .damage = 20, .landed = true });
        const std::uint32_t start_ms = 1000 * kTickMs;
        actor.pmode = kModeGH;
        actor.pmode_until = start_ms + 40 * 40;
        actor.knock = std::array{ float(actor.player.x), float(actor.player.y), float(actor.player.x) + 5.f, float(actor.player.y) };
        world.fight.hurt_side(side, start_ms + 40);
        assert(actor.pmode == kModeGH && !actor.knock && actor.pmode_until < start_ms + 40 * 40);
    }
    // A knocking hit during a get-hit starts no knockback and moves nothing (FUN_00580a70 -> FUN_0057edd0 refuses new mode 0x13 from GH at 0x580adc).
    {
        auto hero = make_hero(*data, d2d::d2s::kPaladin, "ssd");
        World world(&*data, -1, -1);
        world.enter(hero);
        world.fight.update_fighters(1000 * kTickMs);
        auto& actor = *world.actor;
        std::array<Foe, 2> side{};
        side[0].hits = 1;
        side[0].damage = 1;
        side[0].blows.push_back(d2d::rules::StruckBlow{ .damage = 1, .landed = true });
        side[0].knocked_from = std::pair{ actor.player.x - 1.f, actor.player.y };
        const std::uint32_t start_ms = 1000 * kTickMs;
        actor.pmode = kModeGH;
        actor.pmode_until = start_ms + 17 * 40;
        const auto x = actor.player.x;
        world.fight.hurt_side(side, start_ms + 40);
        assert(actor.pmode == kModeGH && !actor.knock && actor.pmode_until == start_ms + 17 * 40 && actor.player.x == x);
    }
    std::printf("test_hit_recovery: block length and the get-hit hold match game.exe's\n");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "test_hit_recovery: %s\n", e.what());
    return 1;
}
