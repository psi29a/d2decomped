// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The order a monster tries a player's pets (FUN_005dd7f0 over FUN_005b1900's
// list): the pet that entered the list last first, and a candidate is taken
// only when strictly nearer, so a tie goes to the newest. rules::search_order
// with rules::search_pick on typed-in distances. Needs no game data.

#include <monsters.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <vector>

using d2d::rules::ListEntry;
using d2d::rules::ListKind;
using d2d::rules::SearchFoe;

namespace {

// Which pet (index into `pets`, each { distance, serial }) the player's search takes, the player far away.
int picked(const std::vector<std::array<int, 2>>& pets) {
    std::vector<ListEntry> entries{ { ListKind::player, 0 } };
    std::vector<SearchFoe> foes{ SearchFoe{ 0x30, false, false, false, false, 0 } };
    for (const auto& pet : pets) {
        entries.push_back({ ListKind::pet, pet[1] });
        foes.push_back(SearchFoe{ pet[0], true, false, false, false, 0 });
    }
    const auto order = d2d::rules::search_order(entries);
    std::vector<SearchFoe> ordered;
    for (const std::size_t slot : order) ordered.push_back(foes[slot]);
    const auto pick = d2d::rules::search_pick(ordered, 0x23, false);
    return pick.target < 0 ? -1 : int(order[std::size_t(pick.target)]) - 1;
}

}  // namespace

int main() try {
    // Two pets the same distance off: the one that entered later (serial 2).
    assert(picked({ { 10, 1 }, { 10, 2 } }) == 1);
    assert(picked({ { 10, 2 }, { 10, 1 } }) == 0);
    // A merc re-entered after the summons (serial 5, d2d lists it first) wins the tie; hired first (serial 1) it loses.
    assert(picked({ { 10, 5 }, { 10, 2 }, { 10, 3 } }) == 0);
    assert(picked({ { 10, 1 }, { 10, 2 }, { 10, 3 } }) == 2);
    // A strictly nearer pet beats a newer one; a farther newest one does not take it.
    assert(picked({ { 6, 1 }, { 10, 9 } }) == 0);
    assert(picked({ { 10, 1 }, { 6, 9 } }) == 1);
    // More than one player entry: d2d's order stays.
    const std::vector<ListEntry> two{ { ListKind::player, 0 }, { ListKind::pet, 1 }, { ListKind::player, 0 }, { ListKind::pet, 2 } };
    assert((d2d::rules::search_order(two) == std::vector<std::size_t>{ 0, 1, 2, 3 }));
    std::printf("OK: a monster tries the pet that entered the list last first\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
