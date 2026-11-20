// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of quest_music.hpp's functions; their notes are in the header.
#include "quest_music.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace d2d::rules {

std::optional<QuestMusic> quest_music_event(int event) {
    for (const auto& row : kQuestMusic)
        if (row.event != 0 && row.event == event) return row;
    return std::nullopt;
}

std::optional<QuestMusic> quest_music_jingle(std::string_view jingle) {
    for (const auto& row : kQuestMusic)
        if (!jingle.empty() && row.jingle == jingle) return row;
    return std::nullopt;
}

std::uint32_t quest_line_due(int event, std::uint32_t now_ms) {
    const auto row = quest_music_event(event);
    return row && row->speaks ? now_ms + std::uint32_t(row->line_at) * 40 : now_ms;
}

}
