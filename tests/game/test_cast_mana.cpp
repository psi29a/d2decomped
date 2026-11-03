// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// When a Sorceress self cast pays its mana and when its action frame runs, through the REAL
// Fight casts and World ticks. `--cast-mana CASES`: one line per case `id skill lvl mana fcr cut` of
// tools/emu/cast_mana.py (mana in 256ths; cut: -1, or the tick after which a get-hit replaces the cast),
// the answer `id began paid_tick paid fired_tick`: began 1 when the cast began, the tick (0 = the
// command's) the mana stat first fell below what it does with no cast, the total it fell, the tick the
// action frame ran (-1 never). game.exe's answers: `python3 cast_mana.py game CASES`.
// With no argument it checks that a cast without the mana does not begin, that Charged Bolt pays on its
// action frame and not at the command, and that a get-hit before it costs nothing.

#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace d2d::game;

namespace {

constexpr int kHorizon = 30;   // ticks followed after the command

// A Sorceress on an open level whose mana the case sets and whose regeneration the control run takes out.
struct Rig {
    const GameData& data;
    d2d::rules::Rng rng{ 7 };
    Character character;
    std::unique_ptr<World> world;
    Level open;
    std::uint32_t now = 1000 * kTickMs;
    int level = 1;

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
        world->fight.skill_level = [this](int) { return level; };
        world->fight.skill_base = world->fight.skill_level;
        world->tick({}, now, now - kTickMs);   // a level's first tick fills in its rooms' walk flags
        now += kTickMs;
    }

    std::int64_t& mana() { return world->actor->character.stats.values[d2d::d2s::kMana]; }

    // Back to standing, with this mana and no regeneration owed.
    void reset(std::int64_t pool, int fcr) {
        auto* actor = world->actor;
        actor->character.stats.values[d2d::d2s::kMaxMana] = std::int64_t(100000) << 8;   // no clamp: the control run's regeneration is the same
        actor->player_combat.fcr = fcr;
        actor->pmode = -1; actor->swing_skill = 0; actor->attack_mon = -1; actor->attack_skill = 0; actor->pstruck = false;
        actor->regen_acc_life = actor->regen_acc_mana = 0;
        now += 100 * kTickMs;
        mana() = pool;
    }

    bool begin(int id) {
        auto& fight = world->fight;
        const auto* skill = data.skills.get(id);
        if (!skill) return false;
        const float x = world->actor->player.x + 3, y = world->actor->player.y;
        if (self_cast(*skill)) return fight.cast(id, now);
        if (fight.missile_skill(*skill) || Fight::spot_skill(*skill)) return fight.cast_missile(id, x, y, now);
        return false;
    }

    // Ticks 0 (the command's) to kHorizon: the mana after each, the tick the action frame ran, whether a get-hit not ours came.
    struct Trace { bool began = false; std::vector<std::int64_t> mana; int fired = -1; bool stray = false; };
    Trace run(int id, std::int64_t pool, int fcr, int cut, bool cast) {
        reset(pool, fcr);
        Trace trace;
        trace.began = cast ? begin(id) : false;
        trace.mana.push_back(mana());
        for (int tick = 1; tick <= kHorizon; ++tick) {
            if (cut >= 0 && tick == cut + 1) world->fight.set_pmode(kModeGH, now + std::uint32_t(tick - 1) * kTickMs);
            const std::uint32_t when = now + std::uint32_t(tick) * kTickMs;
            world->tick({}, when, when - kTickMs);
            trace.mana.push_back(mana());
            if (cast && trace.fired < 0 && world->actor->pstruck) trace.fired = tick;
            if (cast && world->actor->pmode == kModeGH && !(cut >= 0 && tick > cut)) trace.stray = true;
        }
        now += std::uint32_t(kHorizon) * kTickMs;
        return trace;
    }
};

}  // namespace

int main(int argc, char** argv) try {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (!mode.empty() && !(mode == "--cast-mana" && argc > 2)) {
        std::printf("SKIP: modes are none and `--cast-mana CASES` (see tools/emu/cast_mana.py)\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    Rig rig(*data);
    if (mode.empty()) {
        rig.level = 1;
        const auto none = rig.run(38, 0, 0, -1, true);   // Charged Bolt with no mana does not begin
        assert(!none.began && none.fired < 0);
        const std::int64_t cost = d2d::rules::mana_cost(*data->skills.get(38), 1);
        const auto control = rig.run(38, 100 * 256, 0, -1, false);
        const auto paid = rig.run(38, 100 * 256, 0, -1, true);
        assert(paid.began && paid.fired > 0 && paid.mana[0] == control.mana[0]);   // the command takes nothing
        assert(control.mana[std::size_t(paid.fired - 1)] - paid.mana[std::size_t(paid.fired - 1)] == 0);
        assert(control.mana[std::size_t(paid.fired)] - paid.mana[std::size_t(paid.fired)] == cost);   // the action frame does
        assert(d2d::rules::cast_action_ticks(7, 256, 0) == 7 && d2d::rules::cast_action_ticks(7, 256, 9) == 7 && d2d::rules::cast_action_ticks(7, 256, 200) == 4);
        const auto cut = rig.run(38, 100 * 256, 0, paid.fired - 1, true);
        assert(cut.began && cut.fired < 0 && cut.mana.back() == rig.run(38, 100 * 256, 0, -1, false).mana.back());   // cut before it: free
        std::printf("test_cast_mana: ok\n");
        return 0;
    }
    std::ifstream cases(argv[2]);
    std::string line;
    int strays = 0;
    while (std::getline(cases, line)) {
        std::istringstream fields(line);
        int id = 0, skill = 0, level = 0, fcr = 0, cut = 0;
        std::int64_t pool = 0;
        if (!(fields >> id >> skill >> level >> pool >> fcr >> cut)) continue;
        rig.level = level;
        const auto control = rig.run(skill, pool, fcr, cut, false);
        const auto cast = rig.run(skill, pool, fcr, cut, true);
        int paid_tick = -1;
        for (std::size_t k = 0; k < cast.mana.size() && paid_tick < 0; ++k)
            if (control.mana[k] - cast.mana[k] > 0) paid_tick = int(k);
        const std::int64_t paid = control.mana.back() - cast.mana.back();
        strays += cast.stray ? 1 : 0;
        std::printf("%d %d %d %lld %d\n", id, cast.began ? 1 : 0, cast.began ? paid_tick : -1, static_cast<long long>(cast.began ? paid : 0), cast.began ? cast.fired : -1);
    }
    std::fprintf(stderr, "# get-hits not from the case: %d\n", strays);
    return 0;
} catch (const std::exception& error) {
    std::printf("test_cast_mana: %s\n", error.what());
    return 1;
}
