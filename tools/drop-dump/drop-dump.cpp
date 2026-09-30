// drop-dump <mpq dir> — our drop rolls in the text form tools/emu/drops.py
// prints game.exe's. Reads jobs from stdin, "seed<TAB>class<TAB>level<TAB>
// ilvl<TAB>players<TAB>mf" a line; prints "seed class@level>moved iilvl
// pplayers mmf: code:quality[*mul]... -> seed low after".
// drop-dump <mpq dir> tables — class names from stdin, each as "name:
// entry:prob ...".
#include <drops.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: drop-dump <mpq dir> [tables] < jobs\n"); return 2; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    const auto data = d2d::game::load_game_data(argv[1], patch ? d2d::game::fs::path(patch) : d2d::game::fs::path{}, 0x1234);
    if (!data) return 1;
    const auto& rules = data->rules;
    const bool tables = argc > 2 && std::string(argv[2]) == "tables";
    for (std::string line; std::getline(std::cin, line);) {
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
