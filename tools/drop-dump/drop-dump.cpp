// drop-dump <mpq dir> — our drop rolls in the text form tools/emu/drops.py
// prints game.exe's. Reads jobs from stdin, "seed<TAB>class<TAB>level<TAB>
// ilvl<TAB>players<TAB>mf" a line; prints "seed class@level>moved iilvl
// pplayers mmf: code:quality[*mul]... -> seed low after".
// drop-dump <mpq dir> tables — class names from stdin, each as "name:
// entry:prob ...".
// drop-dump <mpq dir> items — made items, "seed<TAB>code<TAB>ilvl<TAB>
// quality<TAB>bovine" a line, off a game seed {seed, 666} (Loot::put):
// "seed code iilvl qquality[ cow]: seeds unit own gold N qty N dur N/N def
// N pick N -> game seed low after". The one-per-game uniques carry on
// from line to line.
#include <drops.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: drop-dump <mpq dir> [tables|items] < jobs\n"); return 2; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    const auto data = d2d::game::load_game_data(argv[1], patch ? d2d::game::fs::path(patch) : d2d::game::fs::path{}, 0x1234);
    if (!data) return 1;
    const auto& rules = data->rules;
    const bool tables = argc > 2 && std::string(argv[2]) == "tables";
    const bool items = argc > 2 && std::string(argv[2]) == "items";
    std::vector<bool> found_uniques;
    for (std::string line; std::getline(std::cin, line);) {
        if (items) {
            std::istringstream fields(line);
            std::string seed, code, ilvl, quality, bovine;
            for (auto* field : { &seed, &code, &ilvl, &quality, &bovine }) std::getline(fields, *field, '\t');
            const int level = std::stoi(ilvl), wanted = std::stoi(quality);
            d2d::rules::Rng game{ std::uint32_t(std::stoul(seed)) };
            d2d::rules::Rng unit{ game.next() }, own{ game.next() };
            const auto unit_low = unit.low, own_low = own.low;
            d2d::d2s::Item item;
            int gold = 0;
            if (code == "gld") gold = d2d::rules::gold_amount(level, 0, unit);
            else item = d2d::rules::generate_item(rules, code, level, wanted, own, &unit, &found_uniques, bovine == "1");
            const int pick = wanted == 7 && item.quality == 7 ? item.unique_id : wanted == 5 && item.quality == 5 ? item.set_id : -1;
            std::printf("%08x %s i%d q%d%s: seeds %08x %08x gold %d qty %d dur %d/%d def %d pick %d -> %08x\n", unsigned(std::stoul(seed)), code.c_str(), level, wanted,
                        bovine == "1" ? " cow" : "", unit_low, own_low, gold, std::max(item.quantity, 0), item.durability, item.max_durability, std::max(item.defense, 0), pick, game.low);
            continue;
        }
        if (tables) {
            std::string out = line + ":";
            if (const auto found = rules.treasure.find(line); found != rules.treasure.end())
                for (const auto& [name, chance] : found->second.items) out += " " + name + ":" + std::to_string(chance);
            std::printf("%s\n", out.c_str());
            continue;
        }
        std::istringstream fields(line);
        std::string seed, name, level, ilvl, players, magic_find;
        for (auto* field : { &seed, &name, &level, &ilvl, &players, &magic_find }) std::getline(fields, *field, '\t');
        d2d::rules::Rng rng{ std::uint32_t(std::stoul(seed)) };
        const auto moved = d2d::rules::tc_upgrade(rules, name, std::stoi(level));
        std::vector<d2d::rules::Drop> drops;
        d2d::rules::roll_drops(rules, moved, std::stoi(ilvl), rng, drops, std::stoi(players), std::stoi(magic_find));
        std::printf("%08x %s@%s>%s i%s p%s m%s:", unsigned(std::stoul(seed)), name.c_str(), level.c_str(), moved.c_str(), ilvl.c_str(), players.c_str(), magic_find.c_str());
        for (const auto& drop : drops) std::printf(drop.mul ? " %s:%d*%d" : " %s:%d", drop.code.c_str(), drop.quality, drop.mul);
        std::printf(" -> %08x\n", rng.low);
    }
}
