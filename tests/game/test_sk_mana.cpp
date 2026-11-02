// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// With no argument: the player's mana check has no minmana floor and the
// payment takes it only from enough mana (FUN_0056c160 / FUN_0056bfe0), through
// the REAL Fight casts: Teleport at level 25 (formula 0, minmana 1) casts with
// half a point and pays nothing, with a whole point pays it. (The row with no
// mana and no lvlmana, which pays nothing, is checked in test_skills.)
// `--sk-mana CASES`: one line per case `id skill lvl mana` of
// tools/emu/sk_mana.py (mana in 256ths), the answer `id check paid` of the
// real Fight::cast / cast_missile / cast_summon / start_swing for that
// skill at that level with that mana: check 1 when the cast began, paid what
// the mana stat lost. `id -1 -1` when no cast path of this build starts the
// skill whatever the mana (a passive, a corpse skill, a weapon the test
// character lacks); game.exe's answers: `python sk_mana.py game CASES`.

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
#include <type_traits>
#include <vector>

using namespace d2d::game;

namespace {

// One character on an open level, the fight's skill level and mana set by the case.
struct Rig {
    const GameData& data;
    d2d::rules::Rng rng{ 7 };
    Character character;
    std::unique_ptr<World> world;
    Level open;
    std::uint32_t now = 1000 * kTickMs;
    int level = 1;

    Rig(const GameData& game_data, int cls) : data(game_data) {
        auto made = new_character(data, cls, "Headless", false, true, rng);
        character.character_class = cls;
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
    }

    auto& mana() { return world->actor->character.stats.values[d2d::d2s::kMana]; }

    // Start the skill by the path d2d takes for it: true when the cast began. The mana stat is the case's, then what is left.
    bool start(int id, int lvl, int mana_256ths) {
        auto& fight = world->fight;
        auto* actor = world->actor;
        level = lvl;
        mana() = static_cast<std::remove_reference_t<decltype(mana())>>(mana_256ths);
        actor->pmode = -1; actor->swing_skill = 0; actor->attack_mon = -1; actor->attack_skill = 0;
        now += 100 * kTickMs;
        const auto* skill = data.skills.get(id);
        if (!skill) return false;
        const float x = actor->player.x + 3, y = actor->player.y;
        if (self_cast(*skill)) return fight.cast(id, now);
        if (fight.missile_skill(*skill) || Fight::spot_skill(*skill)) return fight.cast_missile(id, x, y, now);
        if (fight.summon_skill(*skill)) return !skill->target_corpse && fight.cast_summon(id, x, y, now);
        if (!skill_built(*skill) || skill->attack_no_mana) return false;
        actor->attack_skill = id; actor->attack_mon = 0;
        (void)fight.start_swing(now);
        return actor->swing_skill == id;
    }
};

}  // namespace

int main(int argc, char** argv) try {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (!mode.empty() && mode != "--sk-mana") {
        std::printf("SKIP: modes are none and `--sk-mana CASES` (see tools/emu/sk_mana.py)\n");
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
    constexpr int kAmazon = 0, kSorceress = 1;
    std::array<std::unique_ptr<Rig>, 2> rigs{ std::make_unique<Rig>(*data, kSorceress), std::make_unique<Rig>(*data, kAmazon) };

    if (mode.empty()) {
        const int teleport = data->skills.by_name.at("Teleport");
        auto& rig = *rigs[0];
        // Level 25: 24 - 24 = 0 points, minmana 1. Half a point casts for nothing, a point or more pays the point.
        assert(rig.start(teleport, 25, 128) && rig.mana() == 128);
        assert(rig.start(teleport, 25, 256) && rig.mana() == 0);
        assert(rig.start(teleport, 25, 300) && rig.mana() == 300 - 256);
        assert(rig.start(teleport, 25, 0) && rig.mana() == 0);
        // Level 1 costs 24 points: one short of it is refused, nothing taken.
        assert(!rig.start(teleport, 1, 24 * 256 - 1) && rig.mana() == 24 * 256 - 1);
        assert(rig.start(teleport, 1, 24 * 256) && rig.mana() == 0);
        std::printf("test_sk_mana: ok\n");
        return 0;
    }

    std::ifstream cases(argv[2]);
    std::string line;
    int last_skill = -1, last_lvl = -1;
    Rig* usable = nullptr;
    while (std::getline(cases, line)) {
        std::istringstream fields(line);
        long long case_id = 0, mana = 0;
        int skill_id = 0, lvl = 0;
        fields >> case_id >> skill_id >> lvl >> mana;
        if (skill_id != last_skill || lvl != last_lvl) {            // which test character starts it at all (mana to spare)
            last_skill = skill_id, last_lvl = lvl;
            usable = nullptr;
            for (auto& rig : rigs)
                if (!usable && rig->start(skill_id, lvl, 0x7fffff)) usable = rig.get();
        }
        if (!usable) { std::printf("%lld -1 -1\n", case_id); continue; }
        const int mana_in = int(mana);
        const bool began = usable->start(skill_id, lvl, mana_in);
        std::printf("%lld %d %lld\n", case_id, began ? 1 : 0, began ? static_cast<long long>(mana_in - usable->mana()) : 0LL);
    }
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
