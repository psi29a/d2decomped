// SPDX-License-Identifier: GPL-3.0-or-later
// d2d's side of tools/emu/pets_merc.py: reads its case lines, runs d2d's
// own rules code (components/rules/monsters.hpp: search_pick, search_sight,
// sight_choice) and prints the lines game.exe's side prints.
//   S: ai.cpp search_target's evil-monster branch (ai.cpp, the `picks` loop):
//      a dead pet entry scores 0x7fffffff, then rules::search_pick over the
//      foes. `before` keeps fight/tick.cpp's order (player, merc, summons
//      oldest first); `after` runs the foes through the real
//      rules::search_order with each pet's serial (when it entered the list,
//      FUN_005b1900) from the case line. The harness tags slot 0 player and
//      the rest pet; ai.cpp's own tagging is covered by
//      tests/game/test_pet_search_order.cpp.
//   M: merc.cpp's search (rules::search_sight, then sight_choice with the
//      case's path answer).
// Build, from tools/emu: g++ -O2 -std=c++23 -I../../components $(find ../../components -name '*.hpp' -printf '-I%h\n' | sort -u) pets_merc_harness.cpp ../../build/components/rules/librules.a -o pm_h
#include <monsters.hpp>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
    const bool after = argc > 1 && std::string(argv[1]) == "after";
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string kind;
        long long index = 0;
        in >> kind >> index;
        if (kind == "S") {
            int best = 0, sight = 0, count = 0;
            in >> best >> sight >> count;
            std::vector<int> ids, serials;
            std::vector<d2d::rules::SearchFoe> foes;
            for (int k = 0; k < count; ++k) {
                int id = 0, distance = 0, pet = 0, away = 0, dead = 0, blocked = 0, serial = 0;
                in >> id >> distance >> pet >> away >> dead >> blocked >> serial;
                serials.push_back(serial);
                d2d::rules::SearchFoe foe{ distance, pet != 0, away != 0, dead != 0, false, 0 };
                if (foe.pet && foe.dead) foe.distance = 0x7fffffff;    // ai.cpp:558
                foe.blocked = sight && (foe.list || foe.distance < best) && blocked;   // ai.cpp:559 (sight_blocked from the case)
                ids.push_back(id);
                foes.push_back(foe);
            }
            if (after) {                                             // ai.cpp's order: rules::search_order over the foes as ai.cpp tags them
                std::vector<d2d::rules::ListEntry> entries;
                for (std::size_t k = 0; k < foes.size(); ++k) entries.push_back({ k == 0 ? d2d::rules::ListKind::player : d2d::rules::ListKind::pet, serials[k] });
                std::vector<d2d::rules::SearchFoe> ordered;
                std::vector<int> ordered_ids;
                for (const std::size_t slot : d2d::rules::search_order(entries)) { ordered.push_back(foes[slot]); ordered_ids.push_back(ids[slot]); }
                foes = std::move(ordered);
                ids = std::move(ordered_ids);
            }
            const auto pick = d2d::rules::search_pick(foes, best, sight != 0);
            std::printf("S %lld %d %d\n", index, pick.target >= 0 ? ids[std::size_t(pick.target)] : 0, pick.target >= 0 ? pick.best : pick.nearest);
        } else if (kind == "M") {
            int path = 0, count = 0;
            in >> path >> count;
            std::vector<int> ids;
            std::vector<d2d::rules::SightFoe> foes;
            for (int k = 0; k < count; ++k) {
                int id = 0, distance = 0, threat = 0, enemy = 0, blocked = 0;
                in >> id >> distance >> threat >> enemy >> blocked;
                d2d::rules::SightFoe foe{ distance, threat, enemy != 0, false };
                foe.blocked = foe.enemy && foe.distance < 0x31 && blocked;   // merc.cpp:312
                ids.push_back(id);
                foes.push_back(foe);
            }
            int distance = 0x7fffffff;
            const int chosen = d2d::rules::sight_choice(std::span<const d2d::rules::SightFoe>(foes), d2d::rules::search_sight(foes), [&](int) { return path != 0; }, distance);
            std::printf("M %lld %d %d\n", index, chosen >= 0 ? ids[std::size_t(chosen)] : 0, chosen >= 0 ? distance : 0x7fffffff);
        }
    }
}
