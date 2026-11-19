// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Blood Golem's life steal, as tools/emu/blood_golem.py runs game.exe's
// FUN_005c6530: `--blood-golem` reads its cases (`target drain align phys
// tlife owner olife omax glife gmax lvl`, 256ths) and prints each with
// `| owner_life golem_life`: what d2d's Blood Golem leaves them at (it has no
// life steal: as they were).

#include <cstdio>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

int blood_golem_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        long long target = 0, drain = 0, align = 0, phys = 0, target_life = 0, has_owner = 0, owner_life = 0, owner_max = 0, golem_life = 0, golem_max = 0, level = 0;
        if (!(fields >> target >> drain >> align >> phys >> target_life >> has_owner >> owner_life >> owner_max >> golem_life >> golem_max >> level)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        std::printf("%s | %lld %lld\n", line.c_str(), owner_life, golem_life);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--blood-golem") return blood_golem_cases();
    std::puts("test_blood_golem: --blood-golem reads tools/emu/blood_golem.py's cases");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_blood_golem: %s\n", error.what());
    return 1;
}
