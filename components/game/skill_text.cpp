// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for skill_text.hpp.
#include "skill_text.hpp"

#include "gamedata.hpp"
#include "item_text.hpp"

#include <skills.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace d2d::game {

std::vector<TextLine> skill_tree_tip(const GameData& game_data, const d2d::rules::CalcEnv& env, int skill_id, int lvl, bool maxed,
                                            bool item_granted, bool learned) {
    using namespace skill_text;
    const auto* skill = game_data.skills.get(skill_id);
    if (!skill) return {};
    Builder out{ game_data, {}, {} };
    const auto line_env = with_missiles(game_data, env);
    const auto block = [&](int from, int end, int step, int at_level) {
        bool any = false;
        for (int index = from; index != end; index += step)
            any = desc_line(out, game_data.skills, *skill, line_env, at_level, skill->desc_lines[std::size_t(index)], 0) || any;
        return any;
    };
    if (block(16, 9, -1, lvl + 1)) out.newline();
    if (!maxed && block(0, 6, 1, lvl + 1)) { out.id(lvl > 0 ? 0x109d : 0x10ad); out.newline(); out.newline(); }
    if (lvl > 0 && block(0, 6, 1, lvl)) {
        out.id(0x109e); out.add(std::to_string(lvl));
        if (item_granted) out.id(0x1112);
        out.newline(); out.newline();
    }
    if (block(6, 10, 1, lvl + 1)) out.newline();
    out.add(key_text(game_data, skill->str_long)); out.newline();
    out.colour(2); out.add(key_text(game_data, skill->str_name));
    if (item_granted) { out.newline(); out.newline(); out.id(0x1113); }
    if (!learned) { out.newline(); out.newline(); out.id(0x1114); }
    return to_lines(out.text);
}

std::vector<TextLine> skill_button_tip(const GameData& game_data, const d2d::rules::CalcEnv& env, int skill_id, int lvl,
                                              std::vector<TextLine>* corner) {
    using namespace skill_text;
    const auto* skill = game_data.skills.get(skill_id);
    if (!skill) return {};
    Builder out{ game_data, {}, {} };
    const auto line_env = with_missiles(game_data, env);
    bool any = false;
    for (std::size_t index = 0; index < 10; ++index) any = desc_line(out, game_data.skills, *skill, line_env, lvl, skill->desc_lines[index], 0) || any;
    if (any) { out.id(0x109e); out.add(std::to_string(lvl)); out.newline(); out.newline(); }
    out.add(key_text(game_data, skill->str_short)); out.newline();
    out.add(key_text(game_data, skill->str_name));
    if (corner) *corner = out.corner.empty() ? std::vector<TextLine>{} : to_lines(out.corner);
    return to_lines(out.text);
}

}  // namespace d2d::game
