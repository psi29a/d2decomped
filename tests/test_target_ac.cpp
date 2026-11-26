// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A player's attack rating and the target's defense as the gear changes them
// (FUN_0057d8a0, SUnitDmg.cpp: ignore target's defense, -n% target defense,
// attack rating against demons and the undead) against stock 1.14d's own, run
// by tools/emu/target_ac.py: d2d::rules::target_defense on the same inputs. No
// game data needed at runtime. With a file argument (target_ac.py's `game`
// output, or its `gen` cases) it prints each case line with d2d's answer
// instead, to diff.
#include <combat.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* kFixture[] = {
#include "target_ac_cases.inc"
};

auto split(const std::string& text, char separator) -> std::vector<std::string> {
    std::vector<std::string> parts;
    std::stringstream stream(text);
    for (std::string part; std::getline(stream, part, separator);) parts.push_back(part);
    return parts;
}

// One case line (`case ignore fraction demon undead type class flags boss_byte
// race_byte attack defense`, maybe with game.exe's `|...` after it): the line
// with d2d's answer. The defender becomes the Target Monster::target makes:
// its flags only for a monster (FUN_0063e9f0, FUN_0063e940, FUN_0063e990,
// FUN_0063ee90 and FUN_005a0180 all ask for unit type 1 first).
auto line_of(const std::string& text) -> std::string {
    const std::string case_text = text.substr(0, text.find('|'));
    const auto fields = split(case_text, '\t');
    if (fields.size() != 12) throw std::runtime_error("malformed case: " + text);
    std::vector<long long> value;
    for (std::size_t at = 1; at < fields.size(); ++at) value.push_back(std::stoll(fields[at]));
    d2d::rules::Fighter fighter;
    fighter.ignore_target_def = int(value[0]);
    fighter.target_def_pct = int(value[1]);
    fighter.demon_tohit = int(value[2]);
    fighter.undead_tohit = int(value[3]);
    d2d::rules::Target target;
    target.unit_type = int(value[4]);
    const bool monster = target.unit_type == 1;
    const long long monster_class = value[5], flags = value[6], boss_byte = value[7], race_byte = value[8];
    target.special = monster && (flags & 10) != 0;
    target.superunique = monster && (flags & 2) != 0;
    target.boss = monster && (boss_byte & 0x40) != 0;
    target.demon = monster && (race_byte & 0x20) != 0;
    target.undead = monster && (race_byte & 0x18) != 0;
    target.hireling = monster && (monster_class == 0x10f || monster_class == 0x152 || monster_class == 0x167 || monster_class == 0x230 || monster_class == 0x231);
    int attack = int(value[9]), defense = int(value[10]);
    d2d::rules::target_defense(fighter, target, attack, defense);
    return case_text + "|" + std::to_string(attack) + " " + std::to_string(defense);
}

void hand_checks() {
    d2d::rules::Fighter fighter;
    d2d::rules::Target target;
    target.armor_class = 1000;
    fighter.ar_base = 500;
    fighter.ar_pct = 100;
    fighter.ignore_target_def = 1;
    assert(d2d::rules::player_to_hit(fighter, target, 0) == std::pair(1000, 0));   // a plain monster: no defense
    target.special = true;                                                           // a champion keeps it
    assert(d2d::rules::player_to_hit(fighter, target, 0) == std::pair(1000, 1000));
    fighter.target_def_pct = 50;                                                     // -50%: the champion loses half
    assert(d2d::rules::player_to_hit(fighter, target, 0).second == 500);
    target.boss = true;                                                              // a boss: half as much
    assert(d2d::rules::player_to_hit(fighter, target, 0).second == 750);
    fighter.demon_tohit = 50;                                                        // before the rating's %
    target.demon = true;
    assert(d2d::rules::player_to_hit(fighter, target, 20).first == 550 * 220 / 100);
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream file(argv[1]);
        if (!file) throw std::runtime_error(std::string("cannot open ") + argv[1]);
        std::string text;
        while (std::getline(file, text)) {
            if (!text.empty() && text.back() == '\r') text.pop_back();
            if (!text.empty()) std::printf("%s\n", line_of(text).c_str());
        }
        if (file.bad()) throw std::runtime_error(std::string("cannot read ") + argv[1]);
        return 0;
    }
    hand_checks();
    int matched = 0;
    for (const char* row : kFixture) {
        const std::string want = row;
        const std::string got = line_of(want);
        if (got != want) {
            std::fprintf(stderr, "game.exe: %s\nd2d:      %s\n", want.c_str(), got.c_str());
            return 1;
        }
        ++matched;
    }
    std::printf("target defense: %d of game.exe's cases match\n", matched);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_target_ac: %s\n", error.what());
    return 1;
}
