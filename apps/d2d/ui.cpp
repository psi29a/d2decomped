// Definitions for ui.hpp: buttons, NPC menus and speech.
#include "ui.hpp"
#include "common.hpp"
#include "scene.hpp"

namespace d2d::client {

void blit_button_chrome(std::vector<std::uint8_t>& fb,
                        const d2d::palette::Palette& pal,
                        const d2d::dc6::Sprite& chrome,
                        int x, int y, bool pressed) {
    const auto n = chrome.frames_per_direction();
    if (n == 0) return;
    if (n == 2) {
        const auto& fr = chrome.frame(0, pressed ? 1 : 0);
        blit_sprite(fb, fr, pal, x, y);
        return;
    }
    // 4-frame split. First half goes at x; second half right after it.
    const auto base = pressed ? 2u : 0u;
    if (n > base) {
        const auto& left = chrome.frame(0, base);
        blit_sprite(fb, left, pal, x, y);
        if (n > base + 1) {
            const auto& right = chrome.frame(0, base + 1);
            blit_sprite(fb, right, pal, x + int(left.width), y);
        }
    }
}

NpcMenuState open_npc_menu(const Scene& s, const Level& L, int npc, int screen_x, int screen_y, int clvl , int unidentified ,
                           bool respec) {
    NpcMenuState m;
    const auto& n = L.npcs[std::size_t(npc)];
    const auto it = std::ranges::find_if(kNpcMenus, [&](const NpcMenu& e) { return e.hc_idx == n.hc_idx; });
    if (it == kNpcMenus.end()) return m;
    m.npc = npc;
    m.lines.push_back({ n.name, 21, 0, 0, true });
    auto entries = it->entries;
    if (n.hc_idx == 150 && clvl > 7) entries = { 0xd35, 0xd45 };
    if (n.hc_idx == 148 && respec) entries[2] = 0x2ba0;
    for (const auto id : entries)
        if (id && !(id == 0xfb4 && unidentified == 0))
            m.lines.push_back({ string_id(s, id), 15, 0, 0, false,
                                id == 0xd35 ? NpcMenuState::kTalk
                                : id == 0xd44 || id == 0xd06 ? NpcMenuState::kTrade
                                : id == 0xd46 ? NpcMenuState::kGamble
                                : id == 0xd45 ? NpcMenuState::kHire
                                : id == 0xfb4 ? NpcMenuState::kIdentify
                                : id == 0x2ba0 ? NpcMenuState::kRespec : NpcMenuState::kClose });
    m.lines.push_back({ string_id(s, 0x102e), 15 });
    layout_npc_menu(s, m, screen_x, screen_y);
    return m;
}

NpcMenuState open_respec_menu(const Scene& s, int npc, int screen_x, int screen_y) {
    NpcMenuState m;
    m.npc = npc;
    m.lines.push_back({ string_id(s, 0x2ba0), 21, 0, 0, true });
    m.lines.push_back({ string_id(s, 0xd49), 15, 0, 0, false, NpcMenuState::kRespecOk });
    m.lines.push_back({ string_id(s, 0xd48), 15 });
    layout_npc_menu(s, m, screen_x, screen_y);
    return m;
}

NpcMenuState open_talk_menu(const Scene& s, const Level& L, int npc, int screen_x, int screen_y,
                            const std::vector<d2d::rules::QuestMsg>& quest) {
    NpcMenuState m;
    const auto& n = L.npcs[std::size_t(npc)];
    const auto t = std::ranges::find_if(kNpcTalk, [&](const NpcTalk& e) { return e.hc_idx == n.hc_idx; });
    m.npc = npc;
    m.lines.push_back({ string_id(s, 0xd35), 21, 0, 0, true });
    if (t != kNpcTalk.end() && !t->topics.empty()) {
        if (!t->no_intro) m.lines.push_back({ string_id(s, 0xd47), 15, 0, 0, false, NpcMenuState::kIntro });
        m.lines.push_back({ string_id(s, 0xd43), 15, 0, 0, false, NpcMenuState::kGossip });
    }
    for (const auto& q : quest)
        if (!q.greet && q.string >= 64 && q.string <= 80)
            m.lines.push_back({ string_id(s, 3714), 15, 0, 0, false, NpcMenuState::kQuest, q.string });
    m.lines.push_back({ string_id(s, 0xd48), 15 });
    layout_npc_menu(s, m, screen_x, screen_y);
    return m;
}

void layout_npc_menu(const Scene& s, NpcMenuState& m, int screen_x, int screen_y) {
    int tw = 0;
    for (auto& l : m.lines) { l.width = s.font.measure(l.text); tw = std::max(tw, l.width); m.h += l.height; }
    m.w = tw + 20;
    m.h += 15;
    for (auto& l : m.lines) l.x = l.width < m.w ? (m.w - l.width + 1) / 2 + 1 : 0;
    const int cx = screen_x, cy = std::max(screen_y - 150, 20);
    m.x = cx - m.w / 2;
    m.y = cy - m.h / 3;
    if (m.x + m.w > int(kW) - 10) m.x = int(kW) - m.w;
    if (m.y + m.h > int(kH) - 0x3a) m.y = int(kH) - m.h - 0x30;
    if (m.x < 11) m.x = 10;
    if (m.y < 11) m.y = 10;
}

int talk_topic(const NpcTalk& t, bool intro, int cls, d2d::rules::Rng& rng,
               const std::function<bool(int quest)>& quest_done) {
    if (intro) return t.topics.size() > 1 && int(t.topics[1].cls) == cls ? 1 : 0;
    const int n = int(t.topics.size());
    for (int tries = 10; tries > 0 && n > 0; --tries) {
        const int i = rng(n);
        if (i < 2) continue;
        const auto& tp = t.topics[std::size_t(i)];
        if (tp.cls != 7 && int(tp.cls) != cls) continue;
        if (tp.quest_gated && quest_done(int(tp.quest)) != (tp.quest_state != 0)) continue;
        return i;
    }
    return std::min(2, n - 1);
}

Speech start_speech(const Scene& s, int npc, std::uint16_t string, std::uint32_t ms) {
    Speech sp;
    sp.npc = npc;
    sp.start_ms = ms;
    sp.string = string;
    const std::string text = string_id(s, string);
    std::size_t a = 0;
    bool first = true;
    while (a <= text.size()) {
        const auto nl = text.find('\n', a);
        const std::string line = text.substr(a, nl == std::string::npos ? std::string::npos : nl - a);
        if (first) {
            first = false;
            const bool num = !line.empty() && std::ranges::all_of(line, [](char c) { return c >= '0' && c <= '9'; });
            sp.rate = num ? std::atoi(line.c_str()) : 8;
            if (!num) sp.lines.push_back(line);
        } else {
            sp.lines.push_back(line);
        }
        if (nl == std::string::npos) break;
        a = nl + 1;
    }
    return sp;
}

void draw_speech(std::vector<std::uint8_t>& fb, const Scene& s, const Speech& sp, std::uint32_t ms) {
    if (sp.npc < 0) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int bx = (int(kW) - 0x145) / 2, top = 12;
    for (int y = std::max(0, top - 5); y < top - 5 + 0x7a; ++y)
        for (int x = bx; x < bx + 0x145; ++x) {
            auto* p = &fb[(std::size_t(y) * kW + std::size_t(x)) * 4];
            p[0] = std::uint8_t(p[0] / 2); p[1] = std::uint8_t(p[1] / 2); p[2] = std::uint8_t(p[2] / 2);
        }
    const auto& f = s.font_formal11.line_height() > 0 ? s.font_formal11 : s.font;
    const int cell = f.sheet().frames_per_direction() > 0 ? int(f.sheet().frame(0, 0).height) : 16;
    const int off = sp.offset_px(ms);
    for (std::size_t i = 0; i < sp.lines.size(); ++i) {
        const int base = top + 0x70 - off + int(i) * 18;
        if (base < top - 5 || base - cell > top - 5 + 0x7a) continue;
        f.draw_tinted(fb, kW, kH, pal, bx + 16, base - cell + 1, sp.lines[i], 255, 255, 255,
                      top - 5, top - 5 + 0x7a);
    }
}

void draw_npc_menu(std::vector<std::uint8_t>& fb, const Scene& s, const NpcMenuState& m,
                   int mx, int my, std::uint32_t ms) {
    if (m.npc < 0) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    for (int y = std::max(0, m.y); y < std::min(int(kH), m.y + m.h); ++y)
        for (int x = std::max(0, m.x); x < std::min(int(kW), m.x + m.w); ++x) {
            auto* p = &fb[(std::size_t(y) * kW + std::size_t(x)) * 4];
            p[0] = std::uint8_t(p[0] / 2); p[1] = std::uint8_t(p[1] / 2); p[2] = std::uint8_t(p[2] / 2);
        }
    const int hover = m.line_at(mx, my);
    const int cell = s.font.sheet().frames_per_direction() > 0 ? int(s.font.sheet().frame(0, 0).height) : 16;
    int base = m.y;
    for (std::size_t i = 0; i < m.lines.size(); ++i) {
        const auto& l = m.lines[i];
        base += l.height;
        const int x = m.x + l.x;
        if (l.header) s.font.draw_tinted(fb, kW, kH, pal, x, base - cell + 1, l.text, 199, 179, 119);
        else          s.font.draw(fb, kW, kH, pal, x, base - cell + 1, l.text);
        if (int(i) == hover && s.focus16.frames_per_direction() > 0) {
            const auto& f = s.focus16.frame(0, (ms / 40) % std::min<std::uint32_t>(7, s.focus16.frames_per_direction()));
            blit_sprite(fb, f, pal, x - 24, base + 4 - int(f.height) + 1);
            blit_sprite(fb, f, pal, x + l.width + 2, base + 4 - int(f.height) + 1);
        }
    }
}

bool update_button(Button& b, const Mouse& m, Screen& current_screen,
                   std::atomic<bool>& quit) {
    b.hovered = m.x >= b.x && m.x < b.x + b.w
             && m.y >= b.y && m.y < b.y + b.h;
    if (b.hovered && m.press_this_frame) {
        b.pressed = true;
        if (g_on_button_press) g_on_button_press();
    }
    if (!m.down)                          b.pressed = false;
    if (b.hovered && m.release_this_frame) {
        if (b.do_switch) { current_screen = b.goto_screen; return true; }
        if (b.quit)      { quit = true; return true; }
    }
    return false;
}

std::vector<std::string> parse_credits_utf16(std::span<const std::byte> b) {
    std::vector<std::string> out;
    std::size_t i = 0;
    // Skip BOM (FF FE) if present.
    if (b.size() >= 2 && std::uint8_t(b[0]) == 0xFF
                      && std::uint8_t(b[1]) == 0xFE) i = 2;
    std::string cur;
    while (i + 1 < b.size()) {
        const auto lo = std::uint8_t(b[i]);
        const auto hi = std::uint8_t(b[i + 1]);
        i += 2;
        if (hi == 0 && lo == '\r') continue;         // ignore CR
        if (hi == 0 && lo == '\n') { out.push_back(std::move(cur)); cur.clear(); continue; }
        // Latin-1 subset: keep low byte, drop chars we can't render.
        if (hi == 0 && lo >= 32) cur.push_back(char(lo));
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

}  // namespace d2d::client
