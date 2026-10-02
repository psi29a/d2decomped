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
// from line to line. Low / normal / superior take a 6th field, the
// difficulty, and print "seed code iilvl qquality ddiff: seeds unit own qty
// N dur N/N def N | qquality sub N flags (0x800 socketed, 0x400000
// ethereal) sock N pic N auto N [stat:param=value,...] -> game, unit, own
// seed lows after".
// drop-dump <mpq dir> objects — containers opened, "objseed<TAB>unitseed<TAB>
// op<TAB>class<TAB>level<TAB>difficulty<TAB>locked<TAB>sparkle" a line, off
// object seed {objseed, 666} and unit seed {unitseed, 666} (World::operate):
// "objseed class Llevel ddiff [L][S] opop: code:quality[*mul]... | extra...
// [shut] -> object seed low, unit seed low".
// drop-dump <mpq dir> stands — "seed<TAB>weapon<TAB>ilvl" a line: the stand's
// base off room seed {seed, 666}, then its unit and own seeds off that, the
// quality off its own: "seed w|a iilvl: code:quality -> room seed low after
// the pick".
#include <drops.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <shrines.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: drop-dump <mpq dir> [tables|items|objects|stands] < jobs\n"); return 2; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    const auto data = d2d::game::load_game_data(argv[1], patch ? d2d::game::fs::path(patch) : d2d::game::fs::path{}, 0x1234);
    if (!data) return 1;
    const auto& rules = data->rules;
    const bool tables = argc > 2 && std::string(argv[2]) == "tables";
    const bool items = argc > 2 && std::string(argv[2]) == "items";
    const bool objects = argc > 2 && std::string(argv[2]) == "objects";
    const bool stands = argc > 2 && std::string(argv[2]) == "stands";
    std::vector<bool> found_uniques;
    for (std::string line; std::getline(std::cin, line);) {
        std::vector<int> job;
        if (objects || stands) {
            std::istringstream fields(line);
            for (std::string field; std::getline(fields, field, '\t');) job.push_back(int(std::stoul(field)));
        }
        if (objects) {
            const auto [seed, unit_low, op, id, lid, diff, locked, sparkle] = std::array<int, 8>{ job[0], job[1], job[2], job[3], job[4], job[5], job[6], job[7] };
            const auto& area = data->area_level;
            auto alvl = [&](int level) { return area[std::size_t(level)][std::size_t(diff)]; };
            const auto [low, high] = d2d::rules::kChestLevels[0];
            const auto tc = d2d::rules::chest_tc(0, diff, alvl(lid), alvl(low), alvl(high));
            d2d::rules::Rng object_seed{ std::uint32_t(seed) }, unit{ std::uint32_t(unit_low) };
            std::vector<d2d::rules::Drop> drops;
            const auto opened = d2d::rules::open_container(op, id, locked, sparkle, object_seed,
                                                           [&](int forced) { return d2d::rules::chest_round(rules, tc, unit, drops, forced); });
            std::printf("%08x %d L%d d%d %s%s op%d:", unsigned(seed), id, lid, diff, locked ? "L" : "", sparkle ? "S" : "", op);
            for (const auto& drop : drops) std::printf(drop.mul ? " %s:%d*%d" : " %s:%d", drop.code.c_str(), drop.quality, drop.mul);
            std::printf(" |");
            for (const auto& code : opened.extra) std::printf(" %s", code.c_str());
            std::printf("%s -> %08x %08x\n", opened.opened ? "" : " shut", object_seed.low, unit.low);
            continue;
        }
        if (stands) {
            d2d::rules::Rng room{ std::uint32_t(job[0]) };
            const auto code = d2d::rules::stand_item(rules, job[1] != 0, job[2], room);
            const auto after = room.low;
            d2d::rules::Rng unit{ room.next() }, own{ room.next() };
            const int quality = code.empty() ? 0 : d2d::rules::stand_quality(rules, code, job[2], own);
            std::printf("%08x %s i%d: %s:%d -> %08x\n", unsigned(job[0]), job[1] ? "w" : "a", job[2], code.c_str(), quality, after);
            continue;
        }
        if (items) {
            std::istringstream fields(line);
            std::string seed, code, ilvl, quality, bovine, diff;
            for (auto* field : { &seed, &code, &ilvl, &quality, &bovine, &diff }) std::getline(fields, *field, '\t');
            const int level = std::stoi(ilvl), wanted = std::stoi(quality);
            d2d::rules::Rng game{ std::uint32_t(std::stoul(seed)) };
            d2d::rules::Rng unit{ game.next() }, own{ game.next() };
            const auto unit_low = unit.low, own_low = own.low;
            d2d::d2s::Item item;
            int gold = 0;
            if (code == "gld") gold = d2d::rules::gold_amount(level, 0, unit);
            else item = d2d::rules::generate_item(rules, code, level, wanted, own, &unit, &found_uniques, bovine == "1", diff.empty() ? 0 : std::stoi(diff));
            if (wanted <= 3) {
                std::map<std::pair<int, int>, int> props;      // the item's one modifier list, (stat, param) summed
                for (const auto& prop : item.props) props[{ prop.stat, prop.param }] += prop.value;
                std::string list;
                for (const auto& [key, value] : props)
                    if (value) list += (list.empty() ? "" : ",") + std::to_string(key.first) + ":" + std::to_string(key.second) + "=" + std::to_string(value);
                const int flags = (item.socketed ? 0x800 : 0) | (item.ethereal ? 0x400000 : 0);
                std::printf("%08x %s i%d q%d d%s: seeds %08x %08x qty %d dur %d/%d def %d | q%d sub %d flags %x sock %d pic %d auto %d [%s] -> %08x %08x %08x\n",
                            unsigned(std::stoul(seed)), code.c_str(), level, wanted, diff.c_str(), unit_low, own_low, std::max(item.quantity, 0), item.durability,
                            item.max_durability, std::max(item.defense, 0), item.quality, item.qsub, flags, item.sockets, std::max(item.picture, 0),
                            item.class_affix < 0 ? 0 : item.class_affix, list.c_str(), game.low, unit.low, own.low);
                continue;
            }
            const int pick = wanted == 7 && item.quality == 7 ? item.unique_id : wanted == 5 && item.quality == 5 ? item.set_id : -1;
            std::string made;                                  // a unique / set that took: its flags, mods and own seed after
            if ((wanted == 5 || wanted == 7) && item.quality == wanted) {
                std::map<std::pair<int, int>, int> props;
                for (const auto& prop : item.props) props[{ prop.stat, prop.param }] += prop.value;
                for (const auto& [key, value] : props)
                    if (value) made += (made.empty() ? "" : ",") + std::to_string(key.first) + ":" + std::to_string(key.second) + "=" + std::to_string(value);
                std::string sets;                              // a set's bonus lists, " i[...]" each
                std::size_t at = 0, list = 0;
                for (int bit = 0; bit < 5; ++bit) {
                    if (!(item.set_lists >> bit & 1) || list >= item.set_list_sizes.size()) continue;
                    std::map<std::pair<int, int>, int> bonus;
                    for (std::size_t i = 0; i < item.set_list_sizes[list]; ++i) bonus[{ item.set_props[at + i].stat, item.set_props[at + i].param }] += item.set_props[at + i].value;
                    at += item.set_list_sizes[list++];
                    std::string one;
                    for (const auto& [key, value] : bonus)
                        if (value) one += (one.empty() ? "" : ",") + std::to_string(key.first) + ":" + std::to_string(key.second) + "=" + std::to_string(value);
                    sets += " " + std::to_string(bit) + "[" + one + "]";
                }
                char tail[64];
                std::snprintf(tail, sizeof tail, " %08x", own.low);
                made = std::string(" flags ") + (item.ethereal ? "400000" : "0") + " [" + made + "]" + sets + tail;
            }
            std::printf("%08x %s i%d q%d%s: seeds %08x %08x gold %d qty %d dur %d/%d def %d pick %d%s -> %08x\n", unsigned(std::stoul(seed)), code.c_str(), level, wanted,
                        bovine == "1" ? " cow" : "", unit_low, own_low, gold, std::max(item.quantity, 0), item.durability, item.max_durability, std::max(item.defense, 0), pick,
                        made.c_str(), game.low);
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
