// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's death COF (FUN_0064f5b0, docs/research/re/compcode.md "The COF
// name: FUN_0064f5b0, ..."): DT and DD name hth unless MonStats2 compositeDeath, every
// other mode BaseW; objects and composite deaths keep BaseW. MonStats2's
// column reaches the Npc, and a joined client gets it on the wire.
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <protocol.hpp>
#include <replication.hpp>
#include <txt.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

using namespace d2d::game;

namespace {

// A txt table from its text, as the MPQ's bytes.
d2d::txt::Table table(std::string_view text) {
    return d2d::txt::Table(std::as_bytes(std::span(text.data(), text.size())));
}

}  // namespace

// With the 1.14d data: each monster the rule changes gets a death COF through
// npc_timing (GM, FE and FK had none before); false without the data.
bool real_death_cofs() {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) return false;
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) return false;
    int checked = 0;
    for (const Npc& npc : data->mon_npc) {
        const bool changed = (npc.code == "GM" && npc.base_w == "2hs") || (npc.code == "FE" && npc.base_w == "1hs") ||
                             (npc.code == "FK" && npc.base_w == "1hs");
        if (!changed || npc.composite_death) continue;
        assert(data->npc_timing(npc, "DT").directions > 0);
        ++checked;
    }
    assert(checked > 0);
    return true;
}

// The rule on hand-built Npcs, then monster_npc on two MonStats rows, then the wire.
int main() try {
    Npc goatman;
    goatman.root = "monsters";
    goatman.base_w = "2hs";
    assert(cof_weapon_class(goatman, "DT") == "hth");
    assert(cof_weapon_class(goatman, "DD") == "hth");
    assert(cof_weapon_class(goatman, "NU") == "2hs");
    assert(cof_weapon_class(goatman, "A1") == "2hs");
    Npc composite = goatman;
    composite.composite_death = true;
    assert(cof_weapon_class(composite, "DT") == "2hs");
    assert(cof_weapon_class(composite, "DD") == "2hs");
    Npc object = goatman;
    object.root = "objects";
    assert(cof_weapon_class(object, "DT") == "2hs");

    const GameData game_data;
    const auto monstats = table("Id\tCode\tMonStatsEx\ngoatman1\tGM\tgoatman1\nskeleton1\tSK\tskeleton1\n");
    const auto monstats2 = table("Id\tBaseW\tcompositeDeath\ngoatman1\t2hs\t0\nskeleton1\t1hs\t1\n");
    const auto rows = id_rows(monstats2);
    const Npc built_goatman = monster_npc(game_data, monstats, monstats2, rows, 0);
    const Npc built_skeleton = monster_npc(game_data, monstats, monstats2, rows, 1);
    assert(!built_goatman.composite_death && cof_weapon_class(built_goatman, "DT") == "hth");
    assert(built_skeleton.composite_death && cof_weapon_class(built_skeleton, "DT") == "1hs");

    wire::Out out;
    out.u8(0);   // In starts after the command id byte
    wire::npc(out, built_skeleton);
    wire::In input{ out.bytes };
    const Npc received = wire::npc(input);
    assert(input.ok && received.composite_death && received.base_w == "1hs");

    const bool data_checked = real_death_cofs();
    std::printf("OK: a monster's DT and DD COF is hth unless compositeDeath%s\n",
                data_checked ? "; GM, FE and FK death COFs load" : " (no 1.14d data: the COF check skipped)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_cof_weapon_class: %s\n", error.what());
    return 1;
}
