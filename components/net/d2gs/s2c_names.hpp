// SPDX-License-Identifier: GPL-3.0-or-later
// What each S->C id is, as far as docs/research/re/net-packets.md and
// net-join.md have it: for the net log. An empty name is an id nobody has
// looked at yet.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace d2d::net::d2gs {

inline auto s2c_name(std::uint8_t id) -> std::string_view {
    static constexpr auto kNames = [] {
        std::array<std::string_view, 256> names{};
        names[0x00] = "game loading";      names[0x01] = "game flags";      names[0x02] = "load successful";
        names[0x03] = "load act";          names[0x04] = "load complete";   names[0x05] = "unload complete";
        names[0x06] = "game exit";         names[0x07] = "map reveal";      names[0x08] = "map hide";
        names[0x09] = "assign warp";       names[0x0a] = "remove unit";     names[0x0c] = "hit";
        names[0x0d] = "stop";              names[0x0e] = "object state";    names[0x0f] = "move to x, y";
        names[0x10] = "move to unit";      names[0x15] = "reassign player"; names[0x18] = "life, mana, position";
        names[0x19] = "gold +=";           names[0x1a] = "experience += (byte)"; names[0x1b] = "experience += (word)";
        names[0x1c] = "experience =";      names[0x9e] = "unit stat (byte)"; names[0x9f] = "unit stat (word)";
        names[0xa0] = "unit stat (dword)"; names[0xa1] = "unit stat += (byte)"; names[0xa2] = "unit stat += (word)";
        names[0x1d] = "stat (byte)";       names[0x1e] = "stat (word)";     names[0x1f] = "stat (dword)";
        names[0x20] = "stat of any unit";  names[0x21] = "skill level";     names[0x22] = "item skill";
        names[0x23] = "set skill";         names[0x26] = "chat / overhead"; names[0x27] = "NPC info";
        names[0x28] = "quest info";        names[0x29] = "game quest info"; names[0x2a] = "NPC transaction";
        names[0x2c] = "unit sound";        names[0x3e] = "item stat";       names[0x3f] = "stackable used";
        names[0x40] = "item flags";        names[0x42] = "clear cursor";    names[0x4c] = "skill on a unit";
        names[0x4d] = "skill at x, y";     names[0x4e] = "hirelings";       names[0x4f] = "hirelings";
        names[0x50] = "hirelings";         names[0x51] = "assign object";   names[0x52] = "player quest info";
        names[0x53] = "act data";          names[0x57] = "monster enchants"; names[0x58] = "open UI";
        names[0x0b] = "unit handshake";    names[0x69] = "monster act at x, y"; names[0x6a] = "monster act on a unit";
        names[0x6b] = "monster act at x, y (where it is)"; names[0x6c] = "monster act on a unit (where it is)";
        names[0x47] = "check equipment";   names[0x48] = "check equipment"; names[0x5d] = "quest log news";
        names[0x5e] = "quests open in game"; names[0x5f] = "portal levels visited"; names[0x7c] = "item: end stat list";
        names[0x7e] = "load act COFs";
        names[0x65] = "player kills";      names[0x59] = "assign player";     names[0x5a] = "event message";   names[0x5b] = "player joins";
        names[0x5c] = "player leaves";     names[0x60] = "town portal state"; names[0x63] = "waypoints";
        names[0x67] = "move to x, y";      names[0x68] = "move to unit";    names[0x6d] = "stop";
        names[0x73] = "missile";           names[0x75] = "party roster";    names[0x76] = "player in proximity";
        names[0x77] = "button action";     names[0x7b] = "hotkey";          names[0x7f] = "party member";
        names[0x81] = "assign merc";       names[0x82] = "portal owner";    names[0x89] = "unique event";
        names[0x8a] = "NPC interaction";   names[0x8b] = "relationship";    names[0x8c] = "player relation";
        names[0x8d] = "assign party";      names[0x8e] = "corpse";          names[0x8f] = "pong";
        names[0x94] = "skill list";        names[0x95] = "life, mana, position"; names[0x96] = "walk verify";
        names[0x97] = "weapon switch";     names[0x9c] = "item in the world"; names[0x9d] = "item owned by a unit";
        names[0xa7] = "delayed state";     names[0xa8] = "set state";       names[0xa9] = "end state";
        names[0xaa] = "add unit's states"; names[0xab] = "heal";            names[0xac] = "assign monster";
        names[0xaf] = "connection info";   names[0xb0] = "connection close"; names[0xb3] = "save chunk";
        names[0xb4] = "join refused";
        return names;
    }();
    return kNames[id];
}

} // namespace d2d::net::d2gs
