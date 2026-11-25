// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A classic (version 0) item's mods go through game.exe's own table
// (FUN_0065fe10 -> 0x745b58), not the Properties.txt funcs: the max never
// rolls, res-all and dmg% draw each stat, sock is the param, indestruct
// zeroes the durability, a do-nothing row does nothing. Hand-made tables
// always; with the 1.14d data, tools/emu/classic_mods.py's cases
// (classic_mods_cases.inc). With a case file as argv[1] it prints a line
// per case, as `classic_mods.py game` does.
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <item_gen.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::rules;
using d2d::d2s::Item;

namespace {
struct Case { std::uint32_t seed; const char* code; int ilvl, defence, dur; const char* prop; const char* param; int min, max, kind; const char* line; };
constexpr Case kCases[] = {
#include "classic_mods_cases.inc"
};

// The case's item (version 0) with the mod applied, as classic_mods.py prints it.
std::string run(const Tables& tables, std::uint32_t seed, const std::string& code, int ilvl, int defence, int dur,
                const std::string& prop, const std::string& param, int min, int max, int kind) {
    Item item;
    item.code = code;
    item.version = 0;
    item.ilvl = ilvl;
    item.defense = defence;
    item.durability = item.max_durability = dur;
    const auto info = tables.item_info.find(code);
    const auto base = tables.item_base.find(code);
    const auto type = info != tables.item_info.end() ? tables.types.find(info->second.type) : tables.types.end();
    const ModItem target{ &item, base != tables.item_base.end() ? &base->second : nullptr, info != tables.item_info.end() ? info->second.kind : 0,
                          type != tables.types.end() && type->second.throwable, kind == 1 };
    Rng rng(seed);
    apply_mod(tables, { prop, param, min, max }, item.props, rng, &target);
    std::map<std::pair<int, int>, int> sums;
    for (const auto& prop_value : item.props) sums[{ prop_value.stat, prop_value.param }] += prop_value.value;
    std::string props;
    for (const auto& [key, value] : sums) {
        if (value == 0) continue;
        if (!props.empty()) props += ',';
        props += std::to_string(key.first) + ':' + std::to_string(key.second) + '=' + std::to_string(value);
    }
    char text[512];
    std::snprintf(text, sizeof text, "%08x %s i%d %s(%s) %d..%d k%d: [%s] def %d dur %d/%d sock %d f%d -> %08x", seed, code.c_str(), ilvl,
                  prop.c_str(), param.c_str(), min, max, kind, props.c_str(), item.defense, item.durability, item.max_durability,
                  item.sockets, item.socketed ? 1 : 0, rng.low);
    return text;
}

std::vector<d2d::d2s::ItemProp> mods(const Tables& tables, Item& item, const ItemBase& base, int kind, const Mod& mod, std::uint32_t seed) {
    const ModItem target{ &item, &base, kind, false, false };
    Rng rng(seed);
    item.props.clear();
    apply_mod(tables, mod, item.props, rng, &target);
    return item.props;
}
}  // namespace

int main(int argc, char** argv) try {
    {
        Tables tables;
        tables.properties["str"] = { { .func = 1, .stat = 0 } };
        ItemBase sword, armor;                        // a one-hand sword 2-7; an armor of 8-11 defence
        sword.min_1h = 2; sword.max_1h = 7;
        armor.minac = 8; armor.maxac = 11;
        Item classic, expansion;
        classic.version = 0;
        bool expansion_max = false;
        for (std::uint32_t seed = 1; seed <= 200; ++seed) {
            // str 1..2: min + rand(max - min) on a classic item, never 2; the expansion path draws 1..2.
            const auto got = mods(tables, classic, sword, 2, { "str", "", 1, 2 }, seed);
            assert(got.size() == 1 && got[0].stat == 0 && got[0].value == 1);
            const auto other = mods(tables, expansion, sword, 2, { "str", "", 1, 2 }, seed);
            expansion_max = expansion_max || other.at(0).value == 2;
        }
        assert(expansion_max);
        bool apart = false;
        for (std::uint32_t seed = 1; seed <= 50; ++seed) {   // res-all: four draws (stats 39 / 41 / 43 / 45)
            const auto got = mods(tables, classic, sword, 2, { "res-all", "", 1, 100 }, seed);
            assert(got.size() == 4 && got[0].stat == 39 && got[3].stat == 45);
            apart = apart || got[0].value != got[1].value || got[1].value != got[2].value;
        }
        assert(apart);
        {   // dmg%: max% (17) then min% (18), each its own draw; no weapon rule
            const auto got = mods(tables, classic, sword, 2, { "dmg%", "", 5, 5 }, 3);
            assert(got.size() == 2 && got[0].stat == 17 && got[0].value == 5 && got[1].stat == 18 && got[1].value == 5);
        }
        {   // dmg-min on a one-hand sword (not throwable): the one-hand stat only
            const auto got = mods(tables, classic, sword, 2, { "dmg-min", "", -9, -9 }, 3);
            assert(got.size() == 1 && got[0].stat == 21 && got[0].value == -9);   // as drawn: no hold at 1
        }
        {   // sock: flag 0x800, the param capped by the grid and the type's MaxSock (FUN_0062be00), 1 at least;
            // indestruct: durability zeroed; pierce-cold (row 234): nothing
            tables.item_info["qui"] = { .width = 2, .height = 3, .type = "tors" };
            tables.types["tors"] = {};
            tables.types["tors"].max_sock = { 2, 2, 2 };
            ItemBase body = armor;
            body.gem_sockets = 4;
            Item item;
            item.version = 0;
            item.code = "qui";
            item.ilvl = 30;
            item.durability = item.max_durability = 20;
            mods(tables, item, body, 1, { "sock", "1", 0, 0 }, 1);
            assert(item.socketed && item.sockets == 1);
            mods(tables, item, body, 1, { "sock", "9", 0, 0 }, 1);
            assert(item.sockets == 2);
            mods(tables, item, body, 1, { "sock", "0", 0, 0 }, 1);
            assert(item.sockets == 1);
            mods(tables, item, armor, 1, { "indestruct", "", 0, 0 }, 1);
            assert(item.durability == 0 && item.max_durability == 0);
            assert(mods(tables, item, armor, 1, { "pierce-cold", "", 10, 10 }, 1).empty());
        }
        {   // ac% (flag 1): the ac rule on any kind; ac (flag 0) only on a superior
            Item item;
            item.version = 0;
            item.defense = 9;
            mods(tables, item, armor, 1, { "ac", "", 5, 5 }, 1);
            assert(item.defense == 9);
            mods(tables, item, armor, 1, { "ac%", "", 5, 5 }, 1);
            assert(item.defense == 12);
        }
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: the real tables (no d2data.mpq in %s)\n", data_dir.string().c_str()); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = d2d::game::load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::printf("FAIL: load_game_data failed\n"); return 1; }
    if (!data->patched) { std::printf("SKIP: the real tables need the 1.14d data (D2_PATCH_INSTALLER)\n"); return 0; }
    if (argc > 1) {
        std::ifstream input(argv[1]);
        if (!input) { std::fprintf(stderr, "test_classic_mods: cannot open %s\n", argv[1]); return 2; }
        std::string line;
        while (std::getline(input, line)) {
            std::istringstream fields(line);
            std::uint32_t seed = 0;
            std::string code, prop, param;
            int ilvl = 0, defence = 0, dur = 0, min = 0, max = 0, kind = 0;
            if (!(fields >> seed >> code >> ilvl >> defence >> dur >> prop >> param >> min >> max >> kind)) continue;
            if (param == "-") param.clear();
            std::printf("%s\n", run(data->rules, seed, code, ilvl, defence, dur, prop, param, min, max, kind).c_str());
        }
        return 0;
    }
    int wrong = 0;
    for (const auto& one : kCases) {
        const std::string got = run(data->rules, one.seed, one.code, one.ilvl, one.defence, one.dur, one.prop, one.param, one.min, one.max, one.kind);
        if (got != one.line) {
            if (++wrong <= 5) std::printf("ours: %s\ngame: %s\n", got.c_str(), one.line);
        }
    }
    std::printf("%zu cases, %d differ\n", std::size(kCases), wrong);
    return wrong == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
