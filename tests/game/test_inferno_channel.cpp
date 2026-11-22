// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Inferno's channel through the REAL Fight casts and World ticks (skills.md "Inferno's channel"):
// its start (srvstfunc 11 -> FUN_005c8e30, state 0xc off) pays and turns the state on for 20
// frames; its first do (FUN_005c8ca0) makes no flame, only sets the skill entry's +0x18; each do
// after makes one and files the next a tick on (FUN_00553c70(.., 1)), so a flame a tick. A held cast
// while the state is on is taken mid-channel (FUN_0057edd0 with an end event queued): it pays again,
// holds the state 6 more frames and makes a flame at once. The channel ends with the state.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>

using namespace d2d::game;

namespace {

constexpr int kInferno = 41;

struct Rig {
    const GameData& data;
    d2d::rules::Rng rng{ 7 };
    Character character;
    std::unique_ptr<World> world;
    Level open;
    std::uint32_t now = 1000 * kTickMs;

    explicit Rig(const GameData& game_data) : data(game_data) {
        auto made = new_character(data, 1, "Headless", false, true, rng);
        character.character_class = 1;
        character.name = "Headless";
        character.header = made.header;
        character.stats = made.stats;
        character.items = made.items;
        character.panel = panel_stats(data, character.header, character.items, character.stats);
        world = std::make_unique<World>(&data, -1, -1);
        world->enter(character);
        open = *data.level(d2d::rules::level_ids::kBloodMoor);
        std::ranges::fill(open.walk, std::uint16_t(0));
        world->level = &open;
        world->fight.enter(&open);
        world->fight.skill_level = [](int) { return 1; };
        world->fight.skill_base = world->fight.skill_level;
        world->tick({}, now, now - kTickMs);
        now += kTickMs;
        world->actor->character.stats.values[d2d::d2s::kMaxMana] = std::int64_t(100000) << 8;
        world->actor->character.stats.values[d2d::d2s::kMana] = std::int64_t(50000) << 8;
    }
    [[nodiscard]] std::int64_t mana() const { return world->actor->character.stats.values[d2d::d2s::kMana]; }
    [[nodiscard]] int flames() const {
        const auto mine = [](const Missile& missile) { return missile.skill == kInferno; };
        return int(std::ranges::count_if(world->fight.missiles, mine) + std::ranges::count_if(world->fight.pending, mine));   // pending: made this frame
    }
    bool cast() {
        const auto* actor = world->actor;
        return world->fight.cast_missile(kInferno, actor->player.x + 3, actor->player.y, now);
    }
    void step() { now += kTickMs; world->tick({}, now, now - kTickMs); }
    // A flame made on the tick just run (the count of live ones stays level once the first burn out).
    [[nodiscard]] bool flamed() const {
        const auto mine = [&](const Missile& missile) { return missile.skill == kInferno && missile.born == now; };
        return std::ranges::any_of(world->fight.missiles, mine) || std::ranges::any_of(world->fight.pending, mine);
    }
};

}  // namespace

int main() try {
    namespace fs = std::filesystem;
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (D2_MPQ_DIR)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    const auto& inferno = *data->skills.get(kInferno);
    assert(inferno.srvdofunc == d2d::rules::ServerDoFunction::kBreath);
    const std::int64_t cost = d2d::rules::mana_cost(inferno, 1);
    assert(cost > 0);

    // A cast let go at once: one start's cost; the first do no flame, then one a tick till the state's 20 frames end.
    {
        Rig rig(*data);
        const auto start = rig.now;
        const auto before = rig.mana();
        assert(rig.cast());
        assert(before - rig.mana() == cost);                         // paid at the start (srvstfunc 11)
        int first_do = -1;
        for (int tick = 1; tick < 60 && first_do < 0; ++tick) {
            rig.step();
            if (rig.world->actor->pstruck) first_do = tick;
        }
        assert(first_do > 0 && rig.flames() == 0);                  // the first do sets +0x18 only
        rig.step();
        assert(rig.flames() == 1);
        rig.step();
        assert(rig.flames() == 2);
        int last_flame = 0;
        while (rig.world->actor->pmode >= 0 && rig.now < start + 200 * kTickMs) {
            rig.step();
            if (rig.flamed()) last_flame = int((rig.now - start) / kTickMs);
        }
        const int ended = int((rig.now - start) / kTickMs);
        assert(last_flame == 19);                                   // a flame a tick while the state's 20 frames last
        assert(ended > 20 && ended < 60);                           // then the sequence's frames after the do
        std::printf("released: first do on tick %d, the last flame on 19, the cast over on tick %d\n", first_do, ended);
    }
    // Held: a cast every 4 ticks from the first do pays each time, flames at once, and keeps the channel past 20 frames.
    {
        Rig rig(*data);
        const auto start = rig.now;
        assert(rig.cast());
        while (!rig.world->actor->pstruck) rig.step();
        int recasts = 0;
        std::uint32_t last = rig.now;
        for (int tick = 1; tick <= 40; ++tick) {
            rig.step();
            if (tick % 4 == 0) {
                const auto mana_before = rig.mana();
                const int flames_before = rig.flames();
                assert(rig.cast());
                assert(mana_before - rig.mana() == cost && rig.flames() == flames_before + 1);
                assert(rig.world->actor->burning == rig.now + 6 * kTickMs);
                last = rig.now;
                ++recasts;
            }
            assert(rig.world->actor->pmode >= 0);
        }
        std::uint32_t last_flame = 0;
        while (rig.world->actor->pmode >= 0) {
            rig.step();
            if (rig.flamed()) last_flame = rig.now;
        }
        assert(last_flame == last + 5 * kTickMs && last > start + 20 * kTickMs);   // the list 6 frames past the last cast
        std::printf("held: %d casts paid, the channel over 6 frames after the last\n", recasts);
    }
    // A held cast with too little mana is refused (the client's ask): no flame, no cost; the channel runs out.
    {
        Rig rig(*data);
        assert(rig.cast());
        while (!rig.world->actor->pstruck) rig.step();
        rig.step();
        rig.world->actor->character.stats.values[d2d::d2s::kMana] = 0;   // after the tick's regeneration
        const int flames_before = rig.flames();
        assert(!rig.cast() && rig.flames() == flames_before && rig.mana() == 0);
    }
    std::printf("OK: Inferno's channel\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
