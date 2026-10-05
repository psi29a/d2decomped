// SPDX-License-Identifier: GPL-3.0-or-later
// What each C->S id is, as far as docs/research/re/net-packets.md and
// net-join.md have it: for the net logs. An empty name is an id nobody has
// looked at yet.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace d2d::net::d2gs {

inline auto c2s_name(std::uint8_t id) -> std::string_view {
    static constexpr auto kNames = [] {
        std::array<std::string_view, 256> names{};
        names[0x01] = "walk to x, y";       names[0x02] = "walk to a unit";    names[0x03] = "run to x, y";
        names[0x04] = "run to a unit";      names[0x05] = "left skill at x, y"; names[0x06] = "left skill on a unit";
        names[0x07] = "left skill on a unit, no walk"; names[0x08] = "left skill at x, y, held"; names[0x09] = "left skill on a unit, held";
        names[0x0a] = "left skill on a unit, held, no walk"; names[0x0c] = "right skill at x, y"; names[0x0d] = "right skill on a unit";
        names[0x0e] = "right skill on a unit, no walk"; names[0x0f] = "right skill at x, y, held"; names[0x10] = "right skill on a unit, held";
        names[0x11] = "right skill on a unit, held, no walk"; names[0x13] = "interact"; names[0x14] = "chat";
        names[0x15] = "overhead";           names[0x16] = "pick up";           names[0x17] = "drop the cursor's";
        names[0x18] = "cursor -> grid";     names[0x19] = "grid -> cursor";    names[0x1a] = "equip";
        names[0x1c] = "body -> cursor";     names[0x1d] = "swap cursor <-> body"; names[0x1f] = "swap cursor <-> grid";
        names[0x20] = "use item";           names[0x23] = "cursor -> belt";    names[0x24] = "belt -> cursor";
        names[0x26] = "use belt";           names[0x27] = "identify with a scroll"; names[0x2f] = "start NPC chat";
        names[0x30] = "end NPC chat";       names[0x31] = "quest message heard"; names[0x32] = "buy";
        names[0x33] = "sell";               names[0x34] = "Cain identifies";   names[0x35] = "repair";
        names[0x36] = "hire";               names[0x37] = "gamble confirm";    names[0x38] = "NPC action";
        names[0x3a] = "stat point";         names[0x3b] = "skill point";       names[0x3c] = "select skill";
        names[0x3d] = "operate object";     names[0x3f] = "phrase";            names[0x40] = "update quests";
        names[0x41] = "resurrect";          names[0x49] = "waypoint";          names[0x4b] = "unit update";
        names[0x4f] = "click button";       names[0x50] = "drop gold";         names[0x51] = "hotkey";
        names[0x53] = "run";                names[0x54] = "walk";              names[0x58] = "quest state";
        names[0x59] = "unit position";      names[0x5d] = "party";             names[0x5e] = "party";
        names[0x5f] = "where I am";         names[0x66] = "game server link";  names[0x68] = "join";
        names[0x69] = "leave";              names[0x6b] = "load ack";          names[0x6c] = "save chunk";
        names[0x6d] = "ping";
        return names;
    }();
    return kNames[id];
}

} // namespace d2d::net::d2gs
