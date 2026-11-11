// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// test-includes: apps/d2d
// FUN_004aa3a0: ListRow -1 is absent, and a scroll skill (record +8 & the
// mask at 0x6ce278) is listed only when its carried count is above 0.
// FUN_0055c110: a scroll whose quantity stat is 0 adds nothing. A book
// (Book of Identify, Book of Townportal) whose quantity stat reads 0
// adds 1. SkillBar::listed calls picker_lists; SkillBar::usable adds the
// level and the left list's leftskill bit.

#include "skill_list.hpp"

#include <d2s_items.hpp>
#include <skills.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

namespace {

d2d::rules::Skill skill_row(const char* name, int list_row) {
    d2d::rules::Skill skill;
    skill.name = name;
    skill.desc = name;
    skill.list_row = list_row;
    return skill;
}

d2d::d2s::Item carried(const char* code, int quantity, int where) {
    d2d::d2s::Item item;
    item.code = code;
    item.quantity = quantity;
    item.location = where;
    return item;
}

}  // namespace

int main() try {
    using d2d::client::picker_lists;
    using d2d::client::scroll_count;
    using d2d::d2s::item_location::kBelt;
    using d2d::d2s::item_location::kCursor;
    using d2d::d2s::item_location::kEquipped;
    using d2d::d2s::item_location::kGround;
    using d2d::d2s::item_location::kStored;
    const std::vector<d2d::d2s::Item> none;
    const auto attack = skill_row("Attack", 0);
    const auto kick = skill_row("Kick", -1);
    const auto thrown = skill_row("Throw", 0);
    const auto unsummon = skill_row("Unsummon", 0);
    const auto left_throw = skill_row("Left Hand Throw", 0);
    const auto left_swing = skill_row("Left Hand Swing", -1);
    const auto bolt = skill_row("Fire Bolt", 1);
    const auto scroll = skill_row("Scroll of Townportal", 4);
    const auto book = skill_row("Book of Identify", 4);
    auto blank = attack;
    blank.desc.clear();

    assert(picker_lists(attack, none));
    assert(picker_lists(thrown, none));
    assert(picker_lists(unsummon, none));
    assert(picker_lists(left_throw, none));
    assert(picker_lists(bolt, none));
    assert(!picker_lists(kick, none));
    assert(!picker_lists(left_swing, none));
    assert(!picker_lists(blank, none));
    assert(!picker_lists(scroll, none));
    assert(!picker_lists(book, none));

    const std::vector<d2d::d2s::Item> held = {
        carried("tsc", 1, kStored),
        carried("isc", 0, kStored),
        carried("ibk", 0, kStored),
        carried("tbk", 2, kGround),
    };
    assert(picker_lists(scroll, held));
    assert(scroll_count(scroll, held) == 1);
    assert(!picker_lists(skill_row("Scroll of Identify", 4), held));
    assert(scroll_count(skill_row("Scroll of Identify", 4), held) == 0);
    assert(picker_lists(book, held));
    assert(scroll_count(book, held) == 1);
    assert(!picker_lists(skill_row("Book of Townportal", 4), held));
    assert(scroll_count(skill_row("Book of Townportal", 4), held) == 0);

    const std::vector<d2d::d2s::Item> belt = { carried("ibk", 3, kBelt) };
    assert(picker_lists(book, belt));
    assert(scroll_count(book, belt) == 3);

    const auto portal_book = skill_row("Book of Townportal", 4);
    const std::vector<d2d::d2s::Item> empty_tomes = {
        carried("tbk", 0, kBelt),
        carried("tbk", 0, kEquipped),
        carried("tbk", 0, kGround),
        carried("ibk", 0, kCursor),
        carried("ibk", 4, kStored),
    };
    assert(picker_lists(portal_book, empty_tomes));
    assert(scroll_count(portal_book, empty_tomes) == 2);
    assert(scroll_count(book, empty_tomes) == 5);
    assert(scroll_count(book, { carried("ibk", -1, kStored) }) == 0);
    assert(!picker_lists(book, { carried("ibk", -1, kStored) }));
    return 0;
} catch (const std::exception& error) {
    std::printf("test_skill_picker: %s\n", error.what());
    return 1;
}
