// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A sequence a new command cuts: FUN_0057edd0 lets a new mode start over SQ (0x12) when
// FUN_0057ed90 finds the skill's seqinput, and the client sends one only from that frame on
// (FUN_00464600 -> FUN_004645b0, rules::press_goes, tools/emu/press_gate.py): Blade Fury
// (seqinput 12) is cut by a walk at its frame 12, not at 11; Jab (none) never.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <protocol.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string_view>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir) { std::printf("SKIP: D2_MPQ_DIR (the game-data check)\n"); return D2D_TEST_SKIPPED; }
    const fs::path data_dir = mpq_dir;
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (the game-data check)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (data && !data->patched) { std::printf("SKIP: no 1.14d patch data (the game-data check)\n"); return D2D_TEST_SKIPPED; }
    assert(data && data->patched);

    // The frame `frame` of a fresh sequence of `name` for class `cls`, then a walk: whether it cut the sequence.
    const auto cut_at = [&](int cls, std::string_view name, int frame) {
        d2d::rules::Rng rng(7);
        auto made = new_character(*data, cls, "Cutter", false, true, rng);
        Character character;
        character.character_class = cls;
        character.name = "Cutter";
        character.header = made.header;
        character.stats = made.stats;
        World world(&*data, -1, -1);
        world.enter(character);
        world.actor->character.appearance = data->look_of(world.actor->character.items);
        constexpr std::uint32_t kStart = 1000 * kTickMs;
        world.fight.update_fighters(kStart);
        const int skill = data->skills.by_name.at(std::string(name));
        world.actor->swing_skill = skill;
        world.fight.set_pmode(kModeA1, kStart);
        world.fight.start_sequence(kStart);
        assert(!world.actor->seq.empty() && int(world.actor->seq.size()) > frame);
        const std::uint32_t now = kStart + world.actor->seq_frame_ms * std::uint32_t(frame);
        world.apply(cmd::Move{ world.actor->player.x + 3, world.actor->player.y, true }, now);
        return world.actor->pmode < 0 && world.actor->seq.empty() && world.actor->player.walking;
    };
    assert(data->skills.get(data->skills.by_name.at("Blade Fury"))->seqinput == 12);
    assert(!cut_at(d2d::d2s::kAssassin, "Blade Fury", 11));
    assert(cut_at(d2d::d2s::kAssassin, "Blade Fury", 12));
    std::puts("OK: Blade Fury's sequence is cut by a walk from its frame 12 (seqinput), not before");
    assert(data->skills.get(data->skills.by_name.at("Jab"))->seqinput == 0);
    assert(!cut_at(d2d::d2s::kAmazon, "Jab", 12));
    std::puts("OK: Jab (no seqinput) is never cut");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_sequence_cut: %s\n", error.what());
    return 1;
}
