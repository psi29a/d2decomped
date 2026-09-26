// The skill bar: the left and right skill buttons on the control panel
// (panel.cpp FUN_00496cf0: x 117 and screen width - 165, bottom on the
// screen's bottom edge), the picker a click on one opens, and the skill
// hotkeys (F1-F8). The chosen skills and hotkeys start from the save
// (header +0x38..0x87); a character's level in a skill is its points plus
// item bonuses (components/rules/skills.hpp).
#pragma once

#include "fight.hpp"

namespace {

struct SkillBar {
    const Scene* scene;
    CharCreateUI& cc;
    std::vector<d2d::d2s::ItemProp> extra;   // stats on top of the gear (the skill shrine's +all skills)
    int left = 0, right = 0;               // Skills.txt ids (0: Attack)
    int picking = 0;                       // the picker open: 1 left, 2 right, 0 none

    static constexpr int kLeftX = 117, kRightX = int(kW) - 165, kIcon = 48;

    [[nodiscard]] int cls() const { return int(kUiToSaveClass[std::max(cc.selected, 0)]); }
    // Points in a skill: a class skill's from the save's skill bytes (in
    // Skills.txt order), Attack and a tome's skill (with the tome carried) 1.
    // ponytail: other general skills (Throw, Unsummon, the left-hand
    // swings) and item charges aren't offered yet.
    [[nodiscard]] int base_level(int id) const {
        const auto& ids = scene->skills.class_ids[std::size_t(cls())];
        if (const auto it = std::ranges::find(ids, id); it != ids.end()) return cc.stats.skills[std::size_t(it - ids.begin())];
        if (id == 0) return 1;
        const auto* s = scene->skills.get(id);
        if (!s) return 0;
        const char* tome = s->name == "Book of Townportal" ? "tbk" : s->name == "Book of Identify" ? "ibk" : nullptr;
        return tome && std::ranges::any_of(cc.items, [&](const d2d::d2s::Item& it) { return it.code == tome && it.location == 0; }) ? 1 : 0;
    }
    // The level with item bonuses (worn items and what's socketed in them).
    // ponytail: charms and set bonuses don't count yet.
    [[nodiscard]] int level(int id) const {
        const auto* s = scene->skills.get(id);
        const int base = base_level(id);
        if (!s || s->cls.empty()) return base;
        std::vector<d2d::d2s::ItemProp> props = extra;
        for (const auto& it : cc.items) {
            if (it.location != 1 || it.slot < 1 || it.slot > 10) continue;
            props.insert(props.end(), it.props.begin(), it.props.end());
            for (const auto& j : it.socketed_items) {
                const auto sp = socket_props(*scene, it, j);
                props.insert(props.end(), sp.begin(), sp.end());
            }
        }
        const int bonus = d2d::rules::item_skill_bonus(*s, cls(), props);
        return base > 0 ? base + bonus : 0;          // bonuses only raise skills that have points
    }
    // Can go on the button: known (a level), not passive, and on the left
    // only when Skills.txt's leftskill allows it.
    [[nodiscard]] bool usable(int id, bool on_left) const {
        const auto* s = scene->skills.get(id);
        return s && !s->passive && (!on_left || s->leftskill) && level(id) > 0;
    }

    // A new game: the save's left and right skills (Attack when they're no
    // longer usable, like panel.cpp's reset when the level drops below 1).
    void new_game() {
        left = int(cc.header.left_skill);
        right = int(cc.header.right_skill);
        if (!usable(left, true)) left = 0;
        if (!usable(right, false)) right = 0;
        picking = 0;
    }

    // The picker's rows, bottom up: the general skills, then each of the
    // class's tabs (SkillDesc page 1..3) that has one to offer.
    // ponytail: game.exe's picker layout isn't traced; this is D2's by eye —
    // rows 48 px apart above the button, the right one growing leftwards.
    [[nodiscard]] std::vector<std::vector<int>> rows(bool on_left) const {
        std::vector<std::vector<int>> out(4);
        for (const auto& s : scene->skills.rows)
            if (s.id >= 0 && usable(s.id, on_left)) out[s.cls.empty() ? 0 : std::size_t(std::clamp(s.page, 1, 3))].push_back(s.id);
        std::erase_if(out, [](const auto& r) { return r.empty(); });
        return out;
    }
    // Where a picker icon's bottom-left corner is (row 0 sits on the
    // buttons' top edge), or {-1, -1}.
    [[nodiscard]] std::pair<int, int> picker_at(const std::vector<std::vector<int>>& rs, bool on_left, int row, int col) const {
        if (row < 0 || std::size_t(row) >= rs.size()) return { -1, -1 };
        const int x = on_left ? kLeftX + col * kIcon : kRightX - col * kIcon;
        return { x, int(kH) - kIcon * (row + 1) };
    }
    // The skill under (mx, my) in the open picker, or -1.
    [[nodiscard]] int picked(int mx, int my) const {
        if (!picking) return -1;
        const bool on_left = picking == 1;
        const auto rs = rows(on_left);
        for (std::size_t r = 0; r < rs.size(); ++r)
            for (std::size_t c = 0; c < rs[r].size(); ++c) {
                const auto [x, y] = picker_at(rs, on_left, int(r), int(c));
                if (mx >= x && mx < x + kIcon && my > y - kIcon && my <= y) return rs[r][c];
            }
        return -1;
    }
    [[nodiscard]] static bool on_button(int mx, int my, int x) { return mx >= x && mx < x + kIcon && my > int(kH) - kIcon; }

    // A click: on a button, open (or close) its picker; in an open picker,
    // choose; elsewhere, close it. True when the bar took the click.
    bool click(const Mouse& m) {
        if (!m.press_this_frame) return picking != 0;
        if (const int id = picked(m.x, m.y); id >= 0) {
            (picking == 1 ? left : right) = id;
            picking = 0;
            return true;
        }
        const int was = picking;
        picking = 0;
        if (on_button(m.x, m.y, kLeftX)) { picking = was == 1 ? 0 : 1; return true; }
        if (on_button(m.x, m.y, kRightX)) { picking = was == 2 ? 0 : 2; return true; }
        return was != 0;
    }
    // F1..F8: with a picker open, give the hovered skill that key (the
    // left picker marks it for the left button, 0x8000); otherwise choose
    // the skill that key holds.
    void key(SDL_Keycode k, int mx, int my) {
        if (k < SDLK_F1 || k > SDLK_F8) return;
        const auto slot = std::size_t(k - SDLK_F1);
        auto& hk = cc.header.hotkeys;
        if (const int id = picked(mx, my); id >= 0) {
            for (auto& h : hk) if ((h & 0x7fff) == std::uint32_t(id) && ((h & 0x8000) != 0) == (picking == 1)) h = 0xffff;
            hk[slot] = std::uint32_t(id) | (picking == 1 ? 0x8000u : 0u);
            return;
        }
        const auto h = hk[slot];
        if ((h & 0xffff) == 0xffff) return;
        const int id = int(h & 0x7fff);
        const bool on_left = h & 0x8000;
        if (usable(id, on_left)) (on_left ? left : right) = id;
    }

    // The buttons, the open picker (hotkey labels on its icons) and the
    // hovered skill's name.
    void draw(std::vector<std::uint8_t>& fb, int mx, int my) const {
        const auto& pal = scene->act1_pal.entries().empty() ? scene->pal : scene->act1_pal;
        auto icon = [&](int id, int x, int y) {
            const auto* s = scene->skills.get(id);
            if (!s) return;
            int c = -1;
            for (int i = 0; i < 7; ++i) if (s->cls == d2d::rules::kClassCode[std::size_t(i)]) c = i;
            const auto& spr = c >= 0 ? scene->skill_icons[std::size_t(c)] : scene->generic_skill_icons;
            if (s->icon < 0 || spr.frames_per_direction() <= std::uint32_t(s->icon)) return;
            const auto& f = spr.frame(0, std::uint32_t(s->icon));
            blit_sprite(fb, f, pal, x, y - int(f.height) + 1);
        };
        icon(left, kLeftX, int(kH));
        icon(right, kRightX, int(kH));
        if (!picking) return;
        const bool on_left = picking == 1;
        const auto rs = rows(on_left);
        for (std::size_t r = 0; r < rs.size(); ++r)
            for (std::size_t c = 0; c < rs[r].size(); ++c) {
                const auto [x, y] = picker_at(rs, on_left, int(r), int(c));
                icon(rs[r][c], x, y);
                for (std::size_t k = 0; k < 8; ++k) {
                    const auto h = cc.header.hotkeys[k];
                    if ((h & 0xffff) != 0xffff && int(h & 0x7fff) == rs[r][c] && ((h & 0x8000) != 0) == on_left)
                        scene->font_small.draw(fb, kW, kH, pal, x + 2, y - 12, "F" + std::to_string(k + 1));
                }
            }
        if (const int id = picked(mx, my); id >= 0)
            if (const auto* s = scene->skills.get(id)) {
                auto v = lookup_string(*scene, s->str_name);
                const std::string name = v ? u16_to_latin1(*v) : s->name;
                scene->font.draw(fb, kW, kH, pal, std::clamp(mx - scene->font.measure(name) / 2, 0, int(kW) - scene->font.measure(name)),
                                 my - 30, name);
            }
    }
};

}  // namespace
