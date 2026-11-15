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
//   `--interrupt`: one big hit on a Paladin whose used skill is every Skills.txt row d2d parsed, in each swing or cast mode with its
//   hold queued, with no state and with state 0x36 (a Leap or Whirlwind under way): `I skill mode own state after kicks seq smove`
//   (own 1 when the mode is the one the swing table plays that skill in; kicks, seq, smove 0 when the swing's repeats, sequence and
//   movement are gone), and the barrel kick (swing_skill 0 in KK, what World::operate leaves): `B kick_id after`;
//   `tools/emu/interrupt_swing.py game` and `compare` check them against FUN_0057eec0.
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
#include <sequences.hpp>
#include <world.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <span>
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
    if (!weapon.empty()) { put.code = weapon; put.location = 1; put.slot = 4; character.items.push_back(std::move(put)); }
    character.panel = panel_stats(data, character.header, character.items, character.stats);
    return character;
}

}   // namespace

int main(int argc, char** argv) try {
    const bool grid = argc > 1 && std::strcmp(argv[1], "--block-length") == 0;
    const bool restart = argc > 1 && std::strcmp(argv[1], "--restart") == 0;
    const bool interrupt = argc > 1 && std::strcmp(argv[1], "--interrupt") == 0;
    if (!grid && !restart && !interrupt) {
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
    if (interrupt) {
        // One big hit on a Paladin whose used skill is every Skills.txt row d2d parsed, in each swing or cast mode with its hold queued: `I skill mode own state after kicks seq smove`, with and without state 0x36.
        static const d2d::rules::SeqFrame one_frame{};
        const auto kick = data->skills.by_name.find("Kick");
        assert(kick != data->skills.by_name.end());
        // One big hit on a Paladin whose swing_skill is `id`, in `mode` with its hold queued; `fly` puts a Leap or Whirlwind under way (state 0x36).
        const auto hit = [&](int id, int mode, bool fly, std::uint32_t pmode_until = 0) {
            auto hero = make_hero(*data, d2d::d2s::kPaladin, "ssd");
            auto world = std::make_unique<World>(&*data, -1, -1);
            world->enter(hero);
            world->fight.update_fighters(1000 * kTickMs);
            auto& actor = *world->actor;
            std::array<Foe, 2> side{};
            side[0].hits = 1;
            side[0].damage = 20;
            side[0].blows.push_back(d2d::rules::StruckBlow{ .damage = 20, .landed = true });
            const std::uint32_t start_ms = 1000 * kTickMs;
            actor.swing_skill = id;
            actor.pmode = mode;
            actor.pmode_until = pmode_until ? pmode_until : start_ms + 10 * 40;
            actor.kicks_left = 3;
            actor.seq = std::span<const d2d::rules::SeqFrame>(&one_frame, 1);
            actor.smove.active = true;
            actor.smove.fly = fly;
            world->fight.hurt_side(side, start_ms + 40);
            return world;
        };
        for (int id = 0; id < 400; ++id) {
            const auto* row = data->skills.get(id);
            if (!row) continue;
            // The mode d2d plays the skill in: its own swing for a melee row (swing_mode), SC for a cast row, A1 for the rest (a throw, a bolt).
            const int own_mode = row->anim == "SC" ? kModeSC : row->anim == "KK" ? kModeKK : row->anim == "S1" ? kModeS1 : row->anim == "S3" ? kModeS3 : row->anim == "S4" ? kModeS4 : kModeA1;
            for (const int mode : { kModeA1, kModeA2, kModeSC, 11, kModeKK, kModeS1, kModeS3, kModeS4 })
                for (const int state : { 0, 1 }) {
                    const auto world = hit(id, mode, state == 1);
                    const auto& actor = *world->actor;
                    std::printf("I %d %d %d %d %d %d %d %d\n", id, mode, mode == own_mode ? 1 : 0, state, actor.pmode, actor.kicks_left, actor.seq.empty() ? 0 : 1, actor.smove.active ? 1 : 0);
                }
        }
        // The barrel kick (World::operate, objects.cpp: swing_skill 0, pmode KK): game.exe starts it with the unit's skill 1, Kick.
        {
            const auto world = hit(0, kModeKK, false);
            std::printf("B %d %d\n", kick->second, world->actor->pmode);
        }
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
    // A hit interrupts a swing whose skill's `interrupt` is set and leaves Zeal and Smite alone (FUN_0057eec0 through FUN_005809d0): Attack and Sacrifice
    // go to a get-hit and lose the repeats, the sequence and the movement of the swing.
    {
        const auto id_of = [&](const char* name) { for (int id = 0; id < 400; ++id) if (const auto* row = data->skills.get(id); row && row->name == name) return id; return -1; };
        for (const auto& [name, interrupted] : { std::pair{ "Attack", true }, { "Sacrifice", true }, { "Zeal", false }, { "Smite", false } }) {
            const int id = id_of(name);
            assert(id >= 0);
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
            actor.swing_skill = id;
            actor.pmode = world.fight.swing_mode();
            actor.pmode_until = start_ms + 10 * 40;
            actor.kicks_left = 3;
            world.fight.hurt_side(side, start_ms + 40);
            assert((actor.pmode == kModeGH) == interrupted);
            assert((actor.kicks_left == 0) == interrupted);
        }
    }
    // State 0x36 (a Leap Attack in flight) and the barrel kick refuse the hit though their skills' `interrupt` is set or the swing has no skill id
    // (FUN_0057eec0: the state is its first test, 0x57eec7; the barrel kicks with the unit's skill 1, Kick, whose `interrupt` is blank).
    {
        const auto id_of = [&](const char* name) { return data->skills.by_name.at(name); };
        const auto struck = [&](int id, int mode, bool flying) {
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
            actor.swing_skill = id;
            actor.pmode = mode;
            actor.pmode_until = start_ms + 10 * 40;
            actor.smove.active = true;
            actor.smove.fly = flying;
            world.fight.hurt_side(side, start_ms + 40);
            return std::pair{ actor.pmode, actor.smove.active };
        };
        const int leap_attack = id_of("Leap Attack");
        assert(data->skills.get(leap_attack)->interrupt);                      // it is the state alone that protects it
        assert(struck(leap_attack, kModeA1, true) == std::pair(int(kModeA1), true));   // in flight: the swing and its movement go on
        assert(struck(leap_attack, kModeA1, false).first == kModeGH);          // the same swing on the ground (its landing) is interrupted
        assert(struck(id_of("Attack"), kModeA1, true) == std::pair(int(kModeA1), true));   // the state, not the skill: Attack under way refuses too
        assert(struck(0, kModeKK, false).first == kModeKK);                    // the barrel kick: swing_skill 0, KK (World::operate)
        assert(struck(id_of("Kick"), kModeKK, false).first == kModeKK);
    }
    std::printf("test_hit_recovery: block length and the get-hit hold match game.exe's\n");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "test_hit_recovery: %s\n", e.what());
    return 1;
}
