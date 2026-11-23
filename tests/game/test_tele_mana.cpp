// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// What a Teleport cast costs, through the REAL Fight cast and World ticks. `--tele-mana CASES`: the cases of
// tools/emu/sk_tele_spot.py (`id tele caster_x caster_y target_x target_y n {x y bits}...`, made by
// `gen N SEED --no-units`), one line each, `id did paid`: whether the unit was put somewhere and the mana the
// cast took (256ths, against a control run with no cast; regeneration leaves under 400 of noise, so read it with a tolerance of 1,024). game.exe's: `python3 tele_mana.py game N SEED`
// (FUN_0056f7f0 with the real do 27: the payment FUN_0056bfe0 is made only when the do put the unit somewhere).
// With no argument: a hop that lands pays its cost, one that is refused pays nothing.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <outdoor.hpp>
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
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace d2d::game;

namespace {

constexpr int kRoom = 40, kGrid = 5, kSide = kRoom * kGrid;
constexpr int kOffset = 40;
constexpr int kHorizon = 30;   // ticks followed after the command

}  // namespace

int main(int argc, char** argv) try {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (!mode.empty() && mode != "--tele-mana") {
        std::printf("SKIP: modes are none and `--tele-mana CASES` (see tools/emu/tele_mana.py)\n");
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

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Headless", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    Level field = *data->level(d2d::rules::level_ids::kBloodMoor);
    field.nearby.clear();
    field.warps.clear();
    field.rooms.clear();
    for (int row = 0; row < kGrid; ++row)
        for (int column = 0; column < kGrid; ++column) {
            d2d::drlg::Outdoor::RoomSeed room;
            room.x = kOffset / 5 + column * 8;
            room.y = kOffset / 5 + row * 8;
            field.rooms.push_back(std::move(room));
        }
    const int width = field.ds1.width() * 5, height = field.ds1.height() * 5;
    assert(width >= kOffset + kSide && height >= kOffset + kSide);
    world.level = &field;
    world.fight.enter(&field);
    world.fight.skill_level = [](int) { return 1; };
    world.fight.skill_base = world.fight.skill_level;
    const int teleport_id = data->skills.by_name.at("Teleport");
    std::uint32_t now = 1000 * kTickMs;
    world.tick({}, now, now - kTickMs);   // a level's first tick fills in its rooms' walk flags
    now += kTickMs;
    auto& mana = world.actor->character.stats.values[d2d::d2s::kMana];

    struct Result { bool did = false; std::int64_t paid = 0; };
    const auto run = [&](int tele, int caster_x, int caster_y, int target_x, int target_y, const std::vector<std::array<int, 3>>& words) {
        field.walk.assign(std::size_t(width) * std::size_t(height), std::uint16_t(1));
        for (int y = 0; y < kSide; ++y)
            for (int x = 0; x < kSide; ++x) field.walk[std::size_t(kOffset + y) * std::size_t(width) + std::size_t(kOffset + x)] = 0;
        for (const auto& word : words)
            if (word[0] >= 0 && word[1] >= 0 && word[0] < kSide && word[1] < kSide)
                field.walk[std::size_t(kOffset + word[1]) * std::size_t(width) + std::size_t(kOffset + word[0])] |= std::uint16_t(word[2]);
        field.teleport = tele;
        const auto trace = [&](bool cast) {
            auto* actor = world.actor;
            actor->character.stats.values[d2d::d2s::kMaxMana] = std::int64_t(100) << 8;       // a small pool: its regeneration (a rate of the maximum) is a few 256ths a tick, the same in both runs
            actor->pmode = -1; actor->swing_skill = 0; actor->attack_mon = -1; actor->attack_skill = 0; actor->pstruck = false;
            actor->regen_acc_life = actor->regen_acc_mana = 0;
            actor->player.x = (float(kOffset + caster_x) + 0.5f) / 5;
            actor->player.y = (float(kOffset + caster_y) + 0.5f) / 5;
            actor->player.path.clear(); actor->player.walking = false;
            actor->reassigned = false;
            now += 100 * kTickMs;
            mana = std::int64_t(50) * 256;
            world.fight.monsters.clear();   // the level's spawns: d2d keeps units in its Crowd, game.exe's synthetic rooms have none
            const float point_x = (float(kOffset + target_x) + 0.5f) / 5, point_y = (float(kOffset + target_y) + 0.5f) / 5;
            actor->events.clear();
            const bool started = !cast || world.fight.cast_missile(teleport_id, point_x, point_y, now);
            assert(started);
            for (int tick = 1; tick <= kHorizon; ++tick) {
                const std::uint32_t when = now + std::uint32_t(tick) * kTickMs;
                world.tick({}, when, when - kTickMs);
                if (std::getenv("TELE_MANA_TRACE")) std::fprintf(stderr, "%s t%d mana %lld mode %d\n", cast ? "cast" : "ctrl", tick, static_cast<long long>(mana), actor->pmode);
            }
            now += std::uint32_t(kHorizon) * kTickMs;
            // FUN_00554ea0 put the unit somewhere (a hop onto its own subtile too): the World's ev::Reassigned
            const bool landed = std::ranges::any_of(actor->events, [](const Event& event) { return std::holds_alternative<ev::Reassigned>(event); });
            return std::pair{ landed, mana };
        };
        const auto control = trace(false);
        const auto cast = trace(true);
        return Result{ cast.first, control.second - cast.second };
    };

    if (mode.empty()) {
        const std::int64_t cost = d2d::rules::mana_cost(*data->skills.get(teleport_id), 1);
        const auto landed = run(1, 100, 100, 130, 100, {});
        assert(landed.did && landed.paid == cost);
        const auto refused = run(1, 100, 100, 160, 100, {});   // the room after the next: not near
        assert(!refused.did);
        assert(refused.paid == 0);
        const auto no_teleport = run(0, 100, 100, 130, 100, {});   // the level's Teleport column 0 (do 27 at 0x5ca39a)
        assert(!no_teleport.did && no_teleport.paid == 0);
        std::printf("test_tele_mana: ok\n");
        return 0;
    }

    if (argc < 3) { std::printf("FAIL: --tele-mana needs a CASES file\n"); return 1; }
    std::ifstream cases(argv[2]);
    if (!cases) { std::printf("FAIL: cannot open %s\n", argv[2]); return 1; }
    std::string line;
    while (std::getline(cases, line)) {
        std::istringstream fields(line);
        int id = 0, tele = 0, caster_x = 0, caster_y = 0, target_x = 0, target_y = 0, count = 0;
        fields >> id >> tele >> caster_x >> caster_y >> target_x >> target_y >> count;
        std::vector<std::array<int, 3>> words(std::size_t(std::max(count, 0)));
        for (auto& word : words) fields >> word[0] >> word[1] >> word[2];
        const auto result = run(tele, caster_x, caster_y, target_x, target_y, words);
        std::printf("%d %d %lld\n", id, result.did ? 1 : 0, static_cast<long long>(result.paid));
    }
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
