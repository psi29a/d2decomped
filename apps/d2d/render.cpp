// The client's drawing and screens: what common, ui, world, items, panels,
// skilltree, store, cursor, ingame, frontend and window declare.
#include "window.hpp"

namespace d2d::app {

// common.hpp
fs::path default_data_dir(std::string_view cfg_data) {
    // Resolution order (first hit wins):
    //   1. --data CLI arg (handled in main; not here)
    //   2. $D2_MPQ_DIR env var — portable, matches the test-suite convention
    //   3. `data = …` in d2d.cfg (user / ./ / global dir, see main)
    //   4. macOS: the launcher's QSettings-persisted path
    //      (~/Library/Preferences/com.d2decomp.D2 Launcher.plist, key
    //      game.dataPath) — same lookup tools/ghidra/import.sh uses
    //   5. Eyeballed default: ~/Workspace/private/diablo2
    if (const char* env = std::getenv("D2_MPQ_DIR"); env && *env)
        return fs::path(env);
    if (!cfg_data.empty()) return fs::path(cfg_data);

#if defined(__APPLE__)
    if (FILE* p = ::popen(
            "defaults read 'com.d2decomp.D2 Launcher' game.dataPath 2>/dev/null",
            "r"); p) {
        char buf[1024];
        std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, p);
        ::pclose(p);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) --n;
        if (n > 0) {
            buf[n] = '\0';
            return fs::path(buf);
        }
    }
#endif

    const char* home = std::getenv("HOME");
    if (!home) return {};
    return fs::path(home) / "Workspace" / "private" / "diablo2";
}

void blit_sprite(std::vector<std::uint8_t>& fb,
                 const d2d::dc6::Frame& f,
                 const d2d::palette::Palette& pal,
                 int dst_x, int dst_y, int shade) {
    for (std::uint32_t y = 0; y < f.height; ++y) {
        const int dy = dst_y + int(y);
        if (dy < 0 || dy >= int(kH)) continue;
        for (std::uint32_t x = 0; x < f.width; ++x) {
            const int dx = dst_x + int(x);
            if (dx < 0 || dx >= int(kW)) continue;
            const auto idx = f.pixels[y * f.width + x];
            if (idx == 0) continue;
            const auto c = pal[idx];
            auto* p = &fb[(std::size_t(dy) * kW + std::size_t(dx)) * 4];
            if (shade == 256) { p[0] = c.r; p[1] = c.g; p[2] = c.b; }
            else {
                p[0] = std::uint8_t(std::min(255, c.r * shade / 256));
                p[1] = std::uint8_t(std::min(255, c.g * shade / 256));
                p[2] = std::uint8_t(std::min(255, c.b * shade / 256));
            }
            p[3] = c.a;
        }
    }
}

void blit_at_anchor(std::vector<std::uint8_t>& fb,
                    const d2d::dc6::Frame& f,
                    const d2d::palette::Palette& pal,
                    int anchor_x, int anchor_y) {
    const int x = anchor_x + f.offset_x;
    const int y = anchor_y + f.offset_y - int(f.height) + 1;
    blit_sprite(fb, f, pal, x, y);
}

void blit_additive(std::vector<std::uint8_t>& fb,
                   const d2d::dc6::Frame& f,
                   const d2d::palette::Palette& pal,
                   const d2d::palette::Pl2* pl2,
                   int anchor_x, int anchor_y) {
    const int dst_x = anchor_x + f.offset_x;
    const int dst_y = anchor_y + f.offset_y - int(f.height) + 1;
    for (std::uint32_t y = 0; y < f.height; ++y) {
        const int py = dst_y + int(y);
        if (py < 0 || py >= int(kH)) continue;
        for (std::uint32_t x = 0; x < f.width; ++x) {
            const int px = dst_x + int(x);
            if (px < 0 || px >= int(kW)) continue;
            const auto idx = f.pixels[y * f.width + x];
            if (idx == 0) continue;
            auto* p = &fb[(std::size_t(py) * kW + std::size_t(px)) * 4];
            if (pl2 && p[0] == 0 && p[1] == 0 && p[2] == 0) {
                // Fast path: black bg => additive result == fg palette entry.
                const auto& out = pl2->base_palette()[idx];
                p[0] = out.r; p[1] = out.g; p[2] = out.b;
            } else {
                const auto c = pal[idx];
                p[0] = std::uint8_t(std::min(255, int(p[0]) + int(c.r)));
                p[1] = std::uint8_t(std::min(255, int(p[1]) + int(c.g)));
                p[2] = std::uint8_t(std::min(255, int(p[2]) + int(c.b)));
            }
        }
    }
}

void paint_test_pattern(std::vector<std::uint8_t>& fb) {
    // Diagonal gradient — a visually distinctive canary when no MPQ loads.
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            auto* p = &fb[(y * kW + x) * 4];
            p[0] = std::uint8_t(x);
            p[1] = std::uint8_t(y);
            p[2] = std::uint8_t((x + y) / 2);
            p[3] = 0xFF;
        }
    }
}

void blit_dc6_grid(std::vector<std::uint8_t>& fb,
                   const d2d::dc6::Sprite& spr,
                   const d2d::palette::Palette& pal,
                   int origin_x, int origin_y,
                   int tiles_across) {
    const auto per_dir = spr.frames_per_direction();
    int cy = origin_y;
    int cx = origin_x;
    int row_h = 0;
    for (int i = 0; i < int(per_dir); ++i) {
        const auto& f = spr.frame(0, i);
        blit_sprite(fb, f, pal, cx, cy);
        cx += int(f.width);
        if (int(f.height) > row_h) row_h = int(f.height);
        if ((i + 1) % tiles_across == 0) {
            cx  = origin_x;
            cy += row_h;
            row_h = 0;
        }
    }
}


// ui.hpp
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


// world.hpp
void blit_dt1_tile(std::vector<std::uint8_t>& fb,
                   const d2d::dt1::Tile& t,
                   const d2d::palette::Palette& pal,
                   int sx, int sy, int alpha_all , const Hole* hole) {
    const int th = std::abs(t.height);
    for (int y = 0; y < th; ++y) {
        const int py = sy + y;
        if (py < 0 || py >= int(kH)) continue;
        const auto* row = t.pixels.data() + std::size_t(y) * t.width;
        for (int x = 0; x < t.width; ++x) {
            const std::uint8_t idx = row[x];
            if (idx == 0) continue;   // transparent
            const int px = sx + x;
            if (px < 0 || px >= int(kW)) continue;
            const auto c = pal[idx];
            auto* p = fb.data() + (std::size_t(py) * kW + px) * 4;
            const int alpha = hole ? hole->alpha(px, py, alpha_all) : alpha_all;
            if (alpha >= 255) { p[0] = c.r; p[1] = c.g; p[2] = c.b; }
            else { p[0] = std::uint8_t((p[0] * (255 - alpha) + c.r * alpha) / 255); p[1] = std::uint8_t((p[1] * (255 - alpha) + c.g * alpha) / 255); p[2] = std::uint8_t((p[2] * (255 - alpha) + c.b * alpha) / 255); }
            p[3] = 0xFF;
        }
    }
}

void blit_dt1_shadow(std::vector<std::uint8_t>& fb, const d2d::dt1::Tile& t,
                     const d2d::palette::Palette& pal, int sx, int sy) {
    const int th = std::abs(t.height);
    for (int y = 0; y < th; ++y) {
        const int py = sy + y;
        if (py < 0 || py >= int(kH)) continue;
        const auto* row = t.pixels.data() + std::size_t(y) * t.width;
        for (int x = 0; x < t.width; ++x) {
            const std::uint8_t idx = row[x];
            const int px = sx + x;
            if (idx == 0 || px < 0 || px >= int(kW)) continue;
            const auto c = pal[idx];
            auto* p = fb.data() + (std::size_t(py) * kW + px) * 4;
            p[0] = std::uint8_t((p[0] * 3 + c.r) / 4); p[1] = std::uint8_t((p[1] * 3 + c.g) / 4); p[2] = std::uint8_t((p[2] * 3 + c.b) / 4);
        }
    }
}

void blit_dt1_tile_lit(std::vector<std::uint8_t>& fb, const d2d::dt1::Tile& t, const Lighting& light,
                       int sx, int sy, int gx, int gy, int top_x, int top_y, bool floor, int alpha_all , const Hole* hole) {
    // The subtile corners round the cell: a tile's pixels reach up to 6
    // subtiles before its top corner (tall floors) and 8 past.
    constexpr int kP = 16, kO = 6;
    std::array<float, kP * kP> c{};
    int lo = 255, hi = 0;
    for (int j = 0; j < kP; ++j)
        for (int i = 0; i < kP; ++i) {
            const int v = light.grid.at(gx * 5 - kO + i, gy * 5 - kO + j);
            c[std::size_t(j * kP + i)] = float(v);
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
    const auto& pals = *light.pal;
    if (lo >> 3 == hi >> 3) { blit_dt1_tile(fb, t, pals[std::size_t(lo >> 3)], sx, sy, alpha_all, hole); return; }
    // Screen offset from the top corner → subtiles into the cell: dx - dy =
    // x / 80 cells, dx + dy = y / 40.
    auto level = [&](int ox, int oy) {
        const float p = float(ox) / (kIsoW / 2), q = float(oy) / (kIsoH / 2);
        const float a = std::clamp((q + p) * 2.5f + kO, 0.f, kP - 1.001f), b = std::clamp((q - p) * 2.5f + kO, 0.f, kP - 1.001f);
        const int i = int(a), j = int(b);
        const float fa = a - float(i), fb2 = b - float(j);
        const float* e = &c[std::size_t(j * kP + i)];
        return std::size_t(int((e[0] + (e[1] - e[0]) * fa) * (1 - fb2) + (e[kP] + (e[kP + 1] - e[kP]) * fa) * fb2) >> 3);
    };
    const int th = std::abs(t.height);
    for (int y = 0; y < th; ++y) {
        const int py = sy + y;
        if (py < 0 || py >= int(kH)) continue;
        const auto* row = t.pixels.data() + std::size_t(y) * t.width;
        for (int x = 0; x < t.width; ++x) {
            const std::uint8_t idx = row[x];
            if (idx == 0) continue;
            const int px = sx + x;
            if (px < 0 || px >= int(kW)) continue;
            const auto lvl = level(px - top_x, floor ? py - top_y : kIsoH / 2);
            const auto col = pals[lvl][idx];
            auto* p = fb.data() + (std::size_t(py) * kW + px) * 4;
            const int alpha = hole ? hole->alpha(px, py, alpha_all) : alpha_all;
            if (alpha >= 255) { p[0] = col.r; p[1] = col.g; p[2] = col.b; }
            else { p[0] = std::uint8_t((p[0] * (255 - alpha) + col.r * alpha) / 255); p[1] = std::uint8_t((p[1] * (255 - alpha) + col.g * alpha) / 255); p[2] = std::uint8_t((p[2] * (255 - alpha) + col.b * alpha) / 255); }
            p[3] = 0xFF;
        }
    }
}

const d2d::dc6::Frame* flippy_frame(const d2d::dc6::Sprite& s, std::uint32_t elapsed) {
    if (s.directions() == 0 || s.frames_per_direction() == 0) return nullptr;
    return &s.frame(0, std::min<std::uint32_t>(elapsed / 40, s.frames_per_direction() - 1));
}

std::array<int, 4> composite_bounds(const Scene::PlayerAnim& p, int dir_want,
                                    std::uint32_t elapsed_ms, int ax, int ay) {
    std::array<int, 4> r{ INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN };
    const auto dirs = p.cof.directions(), fpd = p.cof.frames_per_direction();
    if (dirs == 0 || fpd == 0) return r;
    const auto dir = cof_direction(dir_want, dirs);
    // 25 ticks/s; each tick advances speed/256 frames.
    const auto ms_per_frame = p.ms_per_frame();
    const auto frame = std::uint8_t((elapsed_ms / ms_per_frame) % fpd);
    for (std::size_t t = 0; t < p.dcc.size(); ++t) {
        const auto& spr = p.layer(t);
        if (dir >= spr.directions() || frame >= spr.frames_per_direction()) continue;
        const auto& f = spr.frame(dir, frame);
        r = { std::min(r[0], ax + f.box_left), std::min(r[1], ay + f.box_top),
              std::max(r[2], ax + f.box_right), std::max(r[3], ay + f.box_bottom) };
    }
    return r;
}

void render_world(std::vector<std::uint8_t>& fb,
                  const Scene& s,
                  const Level& L,
                  float cam_x, float cam_y,
                  std::uint32_t elapsed_ms ,
                  std::span<const Unit> units ,
                  int mouse_x , int mouse_y ,
                  std::pair<const Unit*, std::array<int, 4>>* hovered ,
                  const Lighting* light , d2d::rules::Rain* rain ,
                  std::vector<std::pair<const Unit*, std::array<int, 4>>>* items) {   // each ground item drawn, its box
    const auto& m = L.ds1;
    if (m.width() == 0 || m.height() == 0) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int cx0 = int(kW) / 2;
    const int cy0 = int(kH) / 2;
    const int mw  = m.width();
    const int base_x = int(std::floor(cam_x)), base_y = int(std::floor(cam_y));
    // Screen position of cell (gx, gy)'s top diamond corner. The camera
    // point (cam_x, cam_y) — continuous, in cells — lands at (kW/2,
    // kH/2 + kIsoH/2), i.e. a cell centre when the camera sits on one.
    auto iso = [&](int gx, int gy) {
        const float dx = float(gx) - cam_x, dy = float(gy) - cam_y;
        return std::pair{ cx0 + int(std::lround((dx - dy) * (kIsoW / 2))),
                          cy0 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2))) };
    };
    // Screen position of a continuous world point (a unit's feet).
    auto iso_point = [&](float x, float y) {
        const float dx = x - cam_x, dy = y - cam_y;
        return std::pair{ cx0 + int(std::lround((dx - dy) * (kIsoW / 2))),
                          cy0 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2))) };
    };

    // Iso footprint for the 800x600 window: each screen cell is 160x80.
    // A ±12 grid-cell window around the camera covers > 2× screen area,
    // leaving room for tall walls (up to 128+px) to reach in from cells
    // that are off-screen at their base.
    constexpr int kR = 12;

    // Blit a single tile at cell (gx, gy)'s iso position, honouring the
    // 80-tall-diamond-at-bottom convention shared by floor/wall pixel
    // buffers.
    // `layer`: 0 wall, 1 floor, 2 shadow (the pass, not the DT1's own type field).
    auto blit_cell = [&](int gx, int gy, const d2d::dt1::Tile& t, int layer, int alpha = 255) {
        const auto [iso_x, iso_y] = iso(gx, gy);
        const int th = std::abs(t.height);
        const int sx = iso_x - t.width / 2;
        const int sy = iso_y - (th - kIsoH);
        if (layer == 2) blit_dt1_shadow(fb, t, pal, sx, sy);
        else if (light) blit_dt1_tile_lit(fb, t, *light, sx, sy, gx, gy, iso_x, iso_y, layer == 1, alpha);
        else blit_dt1_tile(fb, t, pal, sx, sy, alpha);
    };
    // Walls in front of the player see-through (FUN_004dd060 /
    // FUN_004dd180): a wall 1..3 cells past the player's cell in x
    // (orientations 1 4 5 7 8 10 12) or in y (2 3 6 7 9 11 12) fades to
    // alpha 0x80 over 500 ms, and back to 0xff once it isn't. Roofs (15)
    // and lower walls (16..19) never fade.
    // ponytail: the blend is linear in RGB, not the driver's alpha table;
    // game.exe's room-based mode (DAT_0072a968) isn't built.
    struct Fade { int from = 255, to = 255; std::uint32_t at = 0; };
    static std::unordered_map<std::uint64_t, Fade> fades;
    const int pcx = int(std::floor(cam_x)), pcy = int(std::floor(cam_y));
    const auto [hole_x, hole_y] = iso_point(cam_x, cam_y);
    const Hole hole{ hole_x, hole_y - 40, 70 };           // round the player's body
    auto wall_alpha = [&](const void* lv, int off, int type, int k, int gx, int gy) {
        static constexpr std::uint16_t kX = 1 << 1 | 1 << 4 | 1 << 5 | 1 << 7 | 1 << 8 | 1 << 10 | 1 << 12;
        static constexpr std::uint16_t kY = 1 << 2 | 1 << 3 | 1 << 6 | 1 << 7 | 1 << 9 | 1 << 11 | 1 << 12;
        const bool see = type < 16 && ((gx > pcx && gx < pcx + 4 && (kX >> type & 1)) || (gy > pcy && gy < pcy + 4 && (kY >> type & 1)));
        const int want = see ? 0x80 : 0xff;
        const auto key = std::uint64_t(reinterpret_cast<std::uintptr_t>(lv)) * 1000003u ^ (std::uint64_t(off) << 12 | std::uint64_t(type) << 6 | std::uint64_t(k));
        auto it = fades.find(key);
        if (it == fades.end()) { if (!see) return 255; it = fades.emplace(key, Fade{ 255, 255, elapsed_ms }).first; }
        auto& f = it->second;
        const auto now_a = [&] {                // 0x7f every 500 ms, from where it was
            const int step = int(std::min<std::uint32_t>(elapsed_ms - f.at, 1000)) * 0x7f / 500;
            return f.to > f.from ? std::min(f.to, f.from + step) : std::max(f.to, f.from - step);
        };
        if (f.to != want) { f.from = now_a(); f.to = want; f.at = elapsed_ms; }
        const int a = now_a();
        if (a >= 255 && !see) fades.erase(it);
        return a;
    };

    auto find_tile = [&](const Level& lv, int style, int seq, int type)
        -> const d2d::dt1::Tile* {
        const auto it = lv.tile_lookup.find(tile_key(style, seq, type));
        return it == lv.tile_lookup.end() ? nullptr : it->second;
    };
    // Cell (gx, gy) of this level or, past its edge, of the level next to
    // it in the act (Level::near): D2 draws the neighbour's rooms too.
    auto at = [&](int gx, int gy) -> std::pair<const Level*, std::size_t> {
        auto on = [](const Level& lv, int x, int y) {
            return x >= 0 && y >= 0 && x < lv.ds1.width() && y < lv.ds1.height();
        };
        if (on(L, gx, gy)) return { &L, std::size_t(gy) * std::size_t(mw) + std::size_t(gx) };
        for (const auto& n : L.nearby)
            if (on(*n.level, gx - n.dx, gy - n.dy))
                return { n.level, std::size_t(gy - n.dy) * std::size_t(n.level->ds1.width()) + std::size_t(gx - n.dx) };
        return { nullptr, 0 };
    };

    const auto& upal_splash = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    // Row-major sweep so back rows render first. dy increases downward
    // in screen space, so we iterate low→high dy for back-to-front.
    for (int dy = -kR; dy <= kR; ++dy) {
        for (int dx = -kR; dx <= kR; ++dx) {
            const int gx = base_x + dx;
            const int gy = base_y + dy;
            const auto [lv, off] = at(gx, gy);
            if (!lv) continue;
            const auto& cm = lv->ds1;
            // A floor whose DT1 material flags have 2 may splash in the rain
            // (FUN_004de410, as the tile's drawn).
            auto splash = [&](const d2d::dt1::Tile& t) {
                if (rain && (t.material_flags & 2)) { const auto [x, y] = iso(gx, gy); rain->floor(rain->rng, x, y); }
            };
            if (!lv->picks.empty()) {                   // the tiles game.exe picked: floors, then shadows
                for (const int layer : { 1, 2 })
                    for (const auto& p : lv->picks[off])
                        if (p.layer == layer) { blit_cell(gx, gy, *p.tile, layer); if (layer == 1) splash(*p.tile); }
                continue;
            }

            // Floor (single layer typical). Type 0 in the floor stream
            // is the "no floor here" marker (dropped by the game); we
            // still need to look up type=0 for actual floors from DT1s.
            for (const auto& fl : cm.floors()) {
                const auto& c = fl.cells[off];
                // A floor is there when prop1 bit 2 says so (FUN_0066e9b0);
                // (0, 0, 0) with it is the grass tile, without it nothing.
                if (c.hidden || !(c.prop1 & 2)) continue;
                if (auto* t = find_tile(*lv, c.style, c.sequence, /*type=*/0)) {
                    blit_cell(gx, gy, *t, 1);
                    splash(*t);
                }
            }

            // Shadow layer: blended over the floor (blit_dt1_shadow).
            for (const auto& sh : cm.shadows()) {
                const auto& c = sh.cells[off];
                if (c.hidden) continue;
                if (c.style == 0 && c.sequence == 0 && c.wall_type == 0) continue;
                if (auto* t = find_tile(*lv, c.style, c.sequence, /*type=*/13))
                    blit_cell(gx, gy, *t, 2);
            }
        }
    }

    // The rain's splashes on the floor (FUN_00473c00 → FUN_00473a70: draw
    // mode 3, additive).
    if (rain)
        for (const auto& sp : rain->splashes)
            if (const auto fr = s.rain_splash[std::size_t(sp.kind)].frames(); sp.frame < int(fr.size()))
                blit_additive(fb, fr[std::size_t(sp.frame)], upal_splash, nullptr, sp.x, sp.y);
    // Units' shadows, on the ground under the walls and units (the floor
    // pass FUN_004df510 → FUN_004dc7b0).
    {
        static std::vector<std::uint16_t> mask(std::size_t(kW) * kH, 0);
        static std::uint16_t id = 0;
        for (const auto& u : units) {
            if (!u.anim || !u.shadow) continue;
            const auto [ax, ay] = iso_point(u.x, u.y);
            if (ax < -200 || ax > int(kW) + 200 || ay < -100 || ay > int(kH) + 300) continue;
            if (++id == 0) { std::ranges::fill(mask, std::uint16_t{ 0 }); id = 1; }
            shadow_composite(fb, *u.anim, u.dir, std::uint32_t(float(elapsed_ms - u.mode_ms) * u.rate), ax, ay, mask, id);
        }
    }
    set_phase(MainPhase::IngameWalls);
    // Walls / trees / roofs — same row-major sweep, per-cell one-pass
    // draw. All non-floor orientation types share the same iso
    // positioning; the DT1 tile's own y_shift + per-block y encode the
    // vertical layout, so height-varying elements (columns, trees, roofs)
    // land correctly relative to the cell iso anchor without special
    // per-type math here. Roofs (type 15) get a small extra vertical
    // hoist from the DS1 orientation dword's upper 24 bits when
    // present — for MVP we use the DT1's per-tile roof_height instead.
    //
    // Walls go back to front by iso depth (gx + gy, one diagonal at a
    // time), and each unit is drawn once its own cell's diagonal is done:
    // a tent north of the player stays behind them, one south of them
    // covers them. Units on the same diagonal go in screen-y order.
    // game.exe buckets tiles by cell too (FUN_004dd7c0: FUN_00643340 gives
    // the tile's cell of the screen grid, each cell a slot of lists).
    // ponytail: how it interleaves units with a cell's walls isn't traced.
    std::vector<const Unit*> order;
    for (const auto& u : units) if (u.anim || u.sprite || u.missile) order.push_back(&u);
    auto diag_of = [&](const Unit* u) {
        return (int(std::floor(u->x)) - base_x) + (int(std::floor(u->y)) - base_y);
    };
    std::ranges::sort(order, {}, [](const Unit* u) { return u->x + u->y; });
    std::size_t next_unit = 0;
    const auto& upal0 = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    auto draw_units_through = [&](int diag) {
        for (; next_unit < order.size() && diag_of(order[next_unit]) <= diag; ++next_unit) {
            const Unit& u = *order[next_unit];
            const auto [ax, ay] = iso_point(u.x, u.y);
            // The unit under the cursor (FUN_00467a10) at twice its light,
            // 0x40..0xff (FUN_00471ec0).
            // ponytail: objects also switch to draw mode 7 there, untraced.
            const auto& lpal = !light ? upal0
                             : (*light->pal)[std::size_t((u.highlight ? std::clamp(light->unit_at(u.x, u.y) * 2, 0x40, 0xff) : light->unit_at(u.x, u.y)) >> 3)];
            // A state's colour shift wins over the unit's own colour
            // (a monster's palshift / RandTransforms, an item's colormap).
            const auto* umap = u.shift ? u.shift : u.cmap;
            const auto spal = umap ? Scene::mapped(lpal, umap) : d2d::palette::Palette{};
            const auto& upal = umap ? spal : lpal;
            // Its overlays: PreDraw ones behind it, the rest in front
            // (FUN_00470390: AnimRate x 16 / 256 frames a tick).
            auto draw_overs = [&](bool pre) {
                for (const auto& ov : u.overs) {
                    if (ov.o->predraw != pre) continue;
                    const auto* spr = s.overlay_sprite(*ov.o);
                    if (!spr || elapsed_ms < ov.start) continue;
                    const auto n = std::uint32_t(std::min(ov.o->frames, int(spr->frames_per_direction())));
                    auto f = (elapsed_ms - ov.start) * std::uint32_t(std::max(ov.o->rate, 1)) / 640;
                    if (ov.once && f >= n) continue;
                    f %= n;
                    blit_dcc_frame(fb, spr->frame(std::uint8_t(std::uint32_t(u.dir) % spr->directions()), std::int32_t(f)), upal0,
                                   ax + ov.o->x, ay + ov.o->dy(u.overlay_class), draw_mode(ov.o->trans));
                }
            };
            draw_overs(true);
            if (ax < -200 || ax > int(kW) + 200 || ay < -100 || ay > int(kH) + 300) continue;
            std::array<int, 4> b{};
            if (u.missile) {
                const auto cel = s.missile_cels.find(u.missile->name);
                if (cel == s.missile_cels.end()) continue;
                const auto& spr = cel->second;
                const std::uint32_t dirs = spr.directions(), fpd = std::uint32_t(spr.frames_per_direction());
                if (dirs == 0 || fpd == 0) continue;
                const auto frame = (elapsed_ms - u.mode_ms) * std::uint32_t(u.missile->anim_speed) / (40u * 16u)
                                   % std::min<std::uint32_t>(std::uint32_t(u.missile->anim_len), fpd);
                blit_dcc_frame(fb, spr.frame(std::uint8_t(std::uint32_t(u.dir) % dirs), std::uint8_t(frame)), u.missile->trans ? upal0 : upal, ax, ay, u.missile->trans);
                continue;
            }
            if (u.sprite) {
                const auto* f = flippy_frame(*u.sprite, elapsed_ms - u.mode_ms);
                if (!f) continue;
                blit_at_anchor(fb, *f, upal, ax, ay);
                b = { ax + f->offset_x, ay + f->offset_y - int(f->height) + 1,
                      ax + f->offset_x + int(f->width), ay + f->offset_y + 1 };
                if (items) items->push_back({ &u, b });
            } else {
                const auto el = std::uint32_t(float(elapsed_ms - u.mode_ms) * u.rate);
                draw_composite(fb, *u.anim, upal, u.dir, el, ax, ay);
                if (hovered && u.name && !u.name->empty()) b = composite_bounds(*u.anim, u.dir, el, ax, ay);
            }
            draw_overs(false);
            // Its overlay (npcalert: Xoffset -5, Yoffset -7, the NPCs'
            // OverlayHeight row 0; Trans 3, draw mode 3 additive), 16
            // frames at AnimRate 9.
            // ponytail: AnimRate read as frames a second; LoopWaitTime
            // (7000) not applied.
            if (u.overlay && u.overlay->directions() && u.overlay->frames_per_direction())
                blit_dcc_frame(fb, u.overlay->frame(0, std::uint8_t(elapsed_ms / 111 % u.overlay->frames_per_direction())), upal0, ax - 5, ay - 7, 1);
            // Last drawn unit under the cursor = the frontmost one.
            if (hovered && u.name && !u.name->empty()) {
                if (mouse_x >= b[0] && mouse_x < b[2] && mouse_y >= b[1] && mouse_y < b[3])
                    *hovered = { &u, b };
            }
        }
    };
    for (int diag = -2 * kR; diag <= 2 * kR; ++diag) {
        draw_units_through(diag - 1);
        for (int dx = std::max(-kR, diag - kR); dx <= std::min(kR, diag + kR); ++dx) {
            const int dy = diag - dx;
            const int gx = base_x + dx;
            const int gy = base_y + dy;
            const auto [lv, off] = at(gx, gy);
            if (!lv) continue;
            const auto& cm = lv->ds1;
            auto draw_wall = [&](int type, const d2d::dt1::Tile& t, int k) {
                if (type != 15) { blit_cell(gx, gy, t, 0, wall_alpha(lv, int(off), type, k, gx, gy)); return; }
                // Roof — hoist by the DT1's own roof_height.
                auto [iso_x, iso_y] = iso(gx, gy);
                iso_y -= t.roof_height;
                // A roof is flat: lit like a floor, where each pixel lies on
                // the roof's plane (its top corner at the hoisted iso_y).
                const Hole* h = g_roof_cutout ? &hole : nullptr;
                if (light) blit_dt1_tile_lit(fb, t, *light, iso_x - t.width / 2, iso_y - (std::abs(t.height) - kIsoH), gx, gy, iso_x, iso_y, true, 255, h);
                else blit_dt1_tile(fb, t, pal, iso_x - t.width / 2, iso_y - (std::abs(t.height) - kIsoH), 255, h);
            };
            if (!lv->picks.empty()) {
                int k = 0;
                for (const auto& p : lv->picks[off])
                    if (p.layer == 0 && p.orient != 13) draw_wall(p.orient, *p.tile, k++);
                continue;
            }
            int k = 0;
            for (const auto& wl : cm.walls()) {
                const auto& c = wl.cells[off];
                ++k;
                if (c.hidden) continue;
                const int type = c.wall_type;
                if (type == 0) continue;         // floor marker in wall stream
                if (type == 13) continue;        // shadow (drawn above)
                if (auto* t = find_tile(*lv, c.style, c.sequence, type)) draw_wall(type, *t, k);
            }
        }
    }
    draw_units_through(1 << 20);
}

void blit_dcc_frame(std::vector<std::uint8_t>& fb,
                    const d2d::dcc::Frame& f,
                    const d2d::palette::Palette& pal,
                    int anchor_x, int anchor_y, int trans) {
    const int dst_x = anchor_x + f.box_left;
    const int dst_y = anchor_y + f.box_top;
    for (std::int32_t y = 0; y < f.height; ++y) {
        const int py = dst_y + y;
        if (py < 0 || py >= int(kH)) continue;
        const auto* row = f.pixels.data() + std::size_t(y) * f.width;
        for (std::int32_t x = 0; x < f.width; ++x) {
            const auto idx = row[x];
            if (idx == 0) continue;
            const int px = dst_x + x;
            if (px < 0 || px >= int(kW)) continue;
            const auto c = pal[idx];
            auto* p = fb.data() + (std::size_t(py) * kW + px) * 4;
            if (trans == 1) { p[0] = std::uint8_t(std::min(255, p[0] + c.r)); p[1] = std::uint8_t(std::min(255, p[1] + c.g)); p[2] = std::uint8_t(std::min(255, p[2] + c.b)); }
            else if (trans == 2) { p[0] = std::uint8_t(p[0] * c.r / 255); p[1] = std::uint8_t(p[1] * c.g / 255); p[2] = std::uint8_t(p[2] * c.b / 255); }
            else if (trans >= 3 && trans <= 5) {                       // a quarter, half, three quarters of the layer
                const int a = trans - 2;
                p[0] = std::uint8_t((p[0] * (4 - a) + c.r * a) / 4); p[1] = std::uint8_t((p[1] * (4 - a) + c.g * a) / 4); p[2] = std::uint8_t((p[2] * (4 - a) + c.b * a) / 4);
            }
            else { p[0] = c.r; p[1] = c.g; p[2] = c.b; }
            p[3] = 0xFF;
        }
    }
}

void draw_composite(std::vector<std::uint8_t>& fb, const Scene::PlayerAnim& p,
                    const d2d::palette::Palette& pal, int dir_want,
                    std::uint32_t elapsed_ms, int anchor_x, int anchor_y) {
    composite_frames(p, dir_want, elapsed_ms, [&](const d2d::dcc::Frame& f, const d2d::cof::Layer* l) {
        blit_dcc_frame(fb, f, pal, anchor_x, anchor_y, layer_trans(l));
    });
}

void shadow_composite(std::vector<std::uint8_t>& fb, const Scene::PlayerAnim& p, int dir_want, std::uint32_t elapsed_ms,
                      int anchor_x, int anchor_y, std::vector<std::uint16_t>& mask, std::uint16_t id) {
    composite_frames(p, dir_want, elapsed_ms, [&](const d2d::dcc::Frame& f, const d2d::cof::Layer* l) {
        if (l && !l->shadow) return;                               // the COF says this layer casts none
        const int bottom = f.box_top + f.height - 1;              // the frame's bottom row, from the anchor
        const int x0 = anchor_x + f.box_left + bottom / 2, y0 = anchor_y + bottom / 2;
        for (std::int32_t r = 0; r < f.height; r += 2) {          // rows up from the bottom
            const int py = y0 - r / 2;
            if (py < 0 || py >= int(kH)) continue;
            const auto* row = f.pixels.data() + std::size_t(f.height - 1 - r) * f.width;
            for (std::int32_t x = 0; x < f.width; ++x) {
                const int px = x0 + x - r / 2;
                if (row[x] == 0 || px < 0 || px >= int(kW)) continue;
                auto& m = mask[std::size_t(py) * kW + std::size_t(px)];
                if (m == id) continue;
                m = id;
                auto* q = fb.data() + (std::size_t(py) * kW + std::size_t(px)) * 4;
                q[0] = std::uint8_t(q[0] / 4); q[1] = std::uint8_t(q[1] / 4); q[2] = std::uint8_t(q[2] / 4);
            }
        }
    });
}


// items.hpp
void draw_hover_text(std::vector<std::uint8_t>& fb, const Scene& s, const std::vector<TextLine>& lines,
                     int x0, int x1, int top, int bottom) {
    if (lines.empty()) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int lh = 16;                                   // font16 cell height
    int w = 0;
    for (const auto& l : lines) w = std::max(w, s.font.measure(l.text));
    const int h = lh * int(lines.size());
    int bx = std::clamp((x0 + x1) / 2 - w / 2 - 2, 0, std::max(0, int(kW) - w - 4));
    int by = bottom - h - 2;
    if (by < 0) by = std::min(top, int(kH) - h - 4);
    for (int y = std::max(0, by); y < std::min(int(kH), by + h + 4); ++y)
        for (int x = bx; x < std::min(int(kW), bx + w + 4); ++x) {
            auto* p = &fb[(std::size_t(y) * kW + std::size_t(x)) * 4];
            p[0] = std::uint8_t(p[0] / 4); p[1] = std::uint8_t(p[1] / 4); p[2] = std::uint8_t(p[2] / 4);
        }
    int y = by + 2;
    for (const auto& l : lines) {
        const int lw = s.font.measure(l.text);
        s.font.draw_tinted(fb, kW, kH, pal, bx + 2 + (w - lw) / 2, y, l.text, l.rgb[0], l.rgb[1], l.rgb[2]);
        y += lh;
    }
}


// panels.hpp
void draw_inventory(std::vector<std::uint8_t>& fb, const Scene& s, const Scene::InvLayout& L,
                    const std::vector<d2d::d2s::Item>& items, int mx , int my , int clvl ,
                    const std::function<std::string(const d2d::d2s::Item&)>* price) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    // invchar6.dc6 holds two 2x2 panels (256+64 wide, 256+176 tall);
    // frames 4..7 are the inventory, 0..3 the character-stats page.
    if (s.inv_panel.frames_per_direction() >= 8) {
        const auto& f0 = s.inv_panel.frame(0, 4);
        blit_sprite(fb, f0, pal, L.panel_x, L.panel_y);
        blit_sprite(fb, s.inv_panel.frame(0, 5), pal, L.panel_x + int(f0.width), L.panel_y);
        blit_sprite(fb, s.inv_panel.frame(0, 6), pal, L.panel_x, L.panel_y + int(f0.height));
        blit_sprite(fb, s.inv_panel.frame(0, 7), pal, L.panel_x + int(f0.width), L.panel_y + int(f0.height));
    }
    auto draw_in = [&](const d2d::d2s::Item& it, int x, int y, int w, int h) {
        const auto* spr = s.item_sprite(it);
        if (!spr || spr->frames_per_direction() == 0) return;
        const auto& f = spr->frame(0, 0);
        blit_sprite(fb, f, s.item_pal(it, pal), x + (w - int(f.width)) / 2, y + (h - int(f.height)) / 2);
    };
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hover_box{};
    for (const auto& it : items) {
        std::array<int, 4> r{};
        if (it.location == 0 && it.panel == 1) {
            const auto info = s.rules.item_info.find(it.code);
            const int iw = info != s.rules.item_info.end() ? info->second.w : 1;
            const int ih = info != s.rules.item_info.end() ? info->second.h : 1;
            r = { L.grid_x + it.column * L.box_w, L.grid_y + it.row * L.box_h, iw * L.box_w, ih * L.box_h };
        } else if (it.location == 1 && it.slot >= 1 && it.slot <= 10) {
            r = L.slots[std::size_t(it.slot)];
        }
        if (r[2] <= 0) continue;
        draw_in(it, r[0], r[1], r[2], r[3]);
        if (mx >= r[0] && mx < r[0] + r[2] && my >= r[1] && my < r[1] + r[3]) { hover = &it; hover_box = r; }
    }
    if (hover) {
        auto lines = item_lines(s, *hover, clvl);
        if (price && *price) lines.push_back({ (*price)(*hover), kTxtWhite });
        draw_hover_text(fb, s, lines, hover_box[0], hover_box[0] + hover_box[2],
                        hover_box[1] + hover_box[3], hover_box[1]);
    }
}

int stat_button_at(int mx, int my) {
    for (int i = 0; i < 4; ++i) {
        const auto& b = kStatButtons[i];
        const int x = mx - kCharPanelX, y = my - kCharPanelY;
        if (x > b.x && x < b.x + 40 && y > b.y - 22 && y < b.y) return i;
    }
    return -1;
}

void draw_char_panel(std::vector<std::uint8_t>& fb, const Scene& s, const d2d::d2s::Stats& st,
                     const PanelStats& ps,
                     std::string_view name, int class_idx, int pressed_button) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int px = kCharPanelX, py = kCharPanelY;
    if (s.inv_panel.frames_per_direction() >= 8) {
        const auto& f0 = s.inv_panel.frame(0, 0);
        blit_sprite(fb, f0, pal, px, py);
        blit_sprite(fb, s.inv_panel.frame(0, 1), pal, px + int(f0.width), py);
        blit_sprite(fb, s.inv_panel.frame(0, 2), pal, px, py + int(f0.height));
        blit_sprite(fb, s.inv_panel.frame(0, 3), pal, px + int(f0.width), py + int(f0.height));
    }
    // As FUN_004a7d00 draws it: text centred in [x0, x1] as
    // x0 + (x1 - x0 + 1 - w) / 2 (left-aligned when it doesn't fit), y the
    // baseline. Labels in font6, a "Fire\nResistance" pair at y-4 / y+4;
    // values in font16, dropping to font8 when > 999 or too wide (life/
    // mana/stamina); name font16/font8/font6 by length, class font16.
    // Colour (local_8 in FUN_004a7d00): strength .. max stamina blue when
    // their total is over the character's own, red when under (life, mana,
    // stamina themselves stay white); a resistance gold at its cap, red
    // below 0.
    // ponytail: the states' part (Battle Orders' life, a curse's resistances
    // blue / red, defense) isn't coloured: the panel sees items and passives.
    auto pick = [&](const d2d::font::Font& f) -> const d2d::font::Font& {
        return f.line_height() > 0 ? f : s.font;
    };
    const auto& f16 = s.font;
    const auto& f8 = pick(s.font_small);
    const auto& f6 = pick(s.font_tiny);
    auto text = [&](const d2d::font::Font& f, int x0, int x1, int y, const std::string& t, int colour = 0) {
        const int w = f.measure(t);
        const int x = w < x1 - x0 + 1 ? x0 + (x1 - x0 + 1 - w) / 2 : x0;
        // Glyph cells blit bottom-anchored at y, like any DC6 (font6 cells
        // are 11 tall with the baseline on row 8).
        const int ty = py + y - int(f.sheet().frame(0, 0).height) + 1;
        static constexpr std::array<std::array<std::uint8_t, 3>, 5> kRgb{ { { 255, 255, 255 }, { 255, 77, 77 }, { 255, 255, 255 }, { 105, 105, 255 }, { 199, 179, 119 } } };
        if (colour == 0) f.draw(fb, kW, kH, pal, px + x, ty, t);
        else f.draw_tinted(fb, kW, kH, pal, px + x, ty, t, kRgb[std::size_t(colour)][0], kRgb[std::size_t(colour)][1], kRgb[std::size_t(colour)][2]);
    };
    for (const auto& t : kCharLabels) {
        const auto v = lookup_string(s, std::uint16_t(t.id));
        if (!v) continue;
        const auto txt = u16_to_latin1(*v);
        if (const auto nl = txt.find('\n'); nl != txt.npos) {
            text(f6, t.x0, t.x1, t.y - 4, txt.substr(0, nl));
            text(f6, t.x0, t.x1, t.y + 4, txt.substr(nl + 1));
        } else {
            text(f6, t.x0, t.x1, t.y, txt);
        }
    }
    for (const auto& t : kCharValues) {
        const bool fixed = t.id >= 6 && t.id <= 11;       // life/mana/stamina, 8.8
        std::int64_t v = fixed ? st.fixed(t.id) : st.get(t.id);
        int colour = 0;                                   // 1 red, 3 blue, 4 gold
        if (t.id < 12 && t.id != 6 && t.id != 8 && t.id != 10) {   // the maxima already hold theirs (Fight::item_max)
            const auto b = ps.bonus[std::size_t(t.id)];
            if (t.id < 4) v += b;
            colour = b > 0 ? 3 : b < 0 ? 1 : 0;
        }
        auto res = [&](int k) { v = ps.res[std::size_t(k)]; colour = v >= ps.res_cap[std::size_t(k)] ? 4 : v < 0 ? 1 : 0; };
        switch (t.id) {
            case 30: v = ps.next; break;
            case 31: v = ps.defense; break;
            case 39: res(0); break;
            case 43: res(1); break;
            case 41: res(2); break;
            case 45: res(3); break;
            default: break;
        }
        if (t.id == 30 && v < 0) continue;                // max level: blank
        const auto txt = std::to_string(v);
        const bool small_font = (fixed || t.id == 31) && (v > 999 || f16.measure(txt) >= t.x1 - t.x0);
        text(small_font ? f8 : f16, t.x0, t.x1, t.y, txt, colour);
    }
    std::string cls = class_idx >= 0 && class_idx < 7 ? kClassKey[class_idx] : "";
    if (auto v = lookup_string(s, cls)) cls = u16_to_latin1(*v);
    const auto& fname = name.size() + 1 <= 11 ? f16 : name.size() + 1 < 14 ? f8 : f6;
    text(fname, 13, 13 + 0xa1 - 0xd - 1, 25, std::string(name));
    text(f16, 0xc1, 0x137 - 1, 25, cls);
    // Unspent stat points (FUN_004a7d00): the skillpoints box at (3, 364)
    // with "Stat Points" / "Remaining" (0xfeb, 0xfec) in font6 centred in
    // 11..88 at baselines 355 / 363 and the count in font16 in 92..127 at
    // 360; each stat's button, levelsocket at (x+5, y+5) under level
    // (frame 1 while pressed) at (x+8, y+1). DC6s anchor bottom-left.
    if (const auto pts = st.get(d2d::d2s::kStatPts); pts > 0) {
        auto dc6 = [&](const d2d::dc6::Sprite& spr, int frame, int x, int y) {
            if (frame < 0 || spr.frames_per_direction() <= std::uint32_t(frame)) return;
            const auto& f = spr.frame(0, std::uint32_t(frame));
            blit_sprite(fb, f, pal, px + x, py + y - int(f.height) + 1);
        };
        dc6(s.points_box, 0, 3, 364);
        for (auto [id, y] : { std::pair{ 0xfeb, 355 }, { 0xfec, 363 } })
            if (auto v = lookup_string(s, std::uint16_t(id))) text(f6, 11, 0x59 - 1, y, u16_to_latin1(*v));
        text(f16, 0x5c, 0x80 - 1, 360, std::to_string(pts));
        for (int i = 0; i < 4; ++i) {
            const auto& b = kStatButtons[i];
            dc6(s.level_socket, 0, b.x + 5, b.y + 5);
            dc6(s.level_button, i == pressed_button ? 1 : 0, b.x + 8, b.y + 1);
        }
    }
}

void draw_hud(std::vector<std::uint8_t>& fb, const Scene& s, const d2d::d2s::Stats& st) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int W = int(kW), H = int(kH);
    auto at_bottom = [&](const d2d::dc6::Sprite& spr, int frame, int x, int bottom) {
        if (frame >= int(spr.frames_per_direction())) return;
        const auto& f = spr.frame(0, std::uint32_t(frame));
        blit_sprite(fb, f, pal, x, bottom - int(f.height));
    };
    if (s.ctrl_panel.frames_per_direction() >= 6) {
        const int xs[6] = { 0, W / 2 - 0xeb, W / 2 - 0x6b, W / 2 + 0x15, W / 2 + 0x95, W - 0x75 };
        for (int i = 0; i < 6; ++i) at_bottom(s.ctrl_panel, i, xs[i], H);
    }
    // Globe fill: only the bottom `rows` rows of the 80x80 cel.
    auto fill = [&](int frame, int x, std::int64_t cur, std::int64_t max) {
        if (max <= 0 || frame >= int(s.globes.frames_per_direction())) return;
        const auto& f = s.globes.frame(0, std::uint32_t(frame));
        const int rows = int(std::clamp<std::int64_t>(cur * 80 / max, 0, 80));
        const int top = H - 13 - int(f.height);
        for (int y = int(f.height) - rows; y < int(f.height); ++y)
            for (int x0 = 0; x0 < int(f.width); ++x0) {
                const auto idx = f.pixels[std::size_t(y) * f.width + std::size_t(x0)];
                const int px = x + x0, py = top + y;
                if (!idx || px < 0 || py < 0 || px >= W || py >= H) continue;
                const auto c = pal[idx];
                auto* d = fb.data() + (std::size_t(py) * kW + std::size_t(px)) * 4;
                d[0] = c.r; d[1] = c.g; d[2] = c.b;
            }
    };
    fill(0, 29, st.fixed(d2d::d2s::kLife), st.fixed(d2d::d2s::kMaxLife));
    fill(1, W - 0x6f, st.fixed(d2d::d2s::kMana), st.fixed(d2d::d2s::kMaxMana));
    at_bottom(s.globe_glass, 0, 28, H - 5);
    at_bottom(s.globe_glass, 1, W - 0x6e, H - 9);
}

std::array<int, 4> grid_rect(const Scene& s, const Scene::InvLayout& L, const d2d::d2s::Item& it) {
    const auto info = s.rules.item_info.find(it.code);
    return { L.grid_x + it.column * L.box_w, L.grid_y + it.row * L.box_h,
             (info != s.rules.item_info.end() ? info->second.w : 1) * L.box_w,
             (info != s.rules.item_info.end() ? info->second.h : 1) * L.box_h };
}

void draw_storage(std::vector<std::uint8_t>& fb, const Scene& s, const std::vector<d2d::d2s::Item>& items,
                  const d2d::dc6::Sprite& art, const Scene::InvLayout& L, int panel,
                  int mx, int my, int clvl) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    if (art.frames_per_direction() >= 4) {
        const auto& f0 = art.frame(0, 0);
        blit_sprite(fb, f0, pal, kCharPanelX, kCharPanelY);
        blit_sprite(fb, art.frame(0, 1), pal, kCharPanelX + int(f0.width), kCharPanelY);
        blit_sprite(fb, art.frame(0, 2), pal, kCharPanelX, kCharPanelY + int(f0.height));
        blit_sprite(fb, art.frame(0, 3), pal, kCharPanelX + int(f0.width), kCharPanelY + int(f0.height));
    }
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hb{};
    for (const auto& it : items) {
        if (it.location != 0 || it.panel != panel) continue;
        const auto [x, y, w, h] = grid_rect(s, L, it);
        if (const auto* spr = s.item_sprite(it); spr && spr->frames_per_direction() > 0) {
            const auto& f = spr->frame(0, 0);
            blit_sprite(fb, f, s.item_pal(it, pal), x + (w - int(f.width)) / 2, y + (h - int(f.height)) / 2);
        }
        if (mx >= x && mx < x + w && my >= y && my < y + h) { hover = &it; hb = { x, y, w, h }; }
    }
    if (hover) draw_hover_text(fb, s, item_lines(s, *hover, clvl), hb[0], hb[0] + hb[2], hb[1] + hb[3], hb[1]);
}

void draw_belt(std::vector<std::uint8_t>& fb, const Scene& s, const std::vector<d2d::d2s::Item>& items,
               int mx, int my, int clvl, bool popup) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const auto& B = s.belts[std::size_t(belt_index(s, items))];
    const int rows = B.boxes / 4;
    if (popup && s.popbelt.frames_per_direction() > 0) {
        const auto& f = s.popbelt.frame(0, 0);
        for (int i = 0; i + 1 < rows; ++i)
            blit_sprite(fb, f, pal, int(kW) / 2 + 21, int(kH) - 41 - 32 * i - int(f.height) + 1);
    }
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hb{};
    for (const auto& it : items) {
        if (it.location != 2 || it.column < 0 || it.column >= B.boxes || (!popup && it.column > 3)) continue;
        const auto& b = B.box[std::size_t(it.column)];
        if (b[1] <= b[0]) continue;
        if (const auto* spr = s.item_sprite(it); spr && spr->frames_per_direction() > 0) {
            const auto& f = spr->frame(0, 0);
            blit_sprite(fb, f, s.item_pal(it, pal), b[0] + (b[1] - b[0] + 1 - int(f.width)) / 2,
                        b[2] + (b[3] - b[2] + 1 - int(f.height)) / 2);
        }
        if (mx >= b[0] && mx <= b[1] && my >= b[2] && my <= b[3]) { hover = &it; hb = b; }
    }
    if (hover) draw_hover_text(fb, s, item_lines(s, *hover, clvl), hb[0], hb[1] + 1, hb[3] + 1, hb[2]);
}

int automap_cel(const Scene& s, const Level& L, int orientation, int main, int sub, std::uint32_t hash) {
    for (const auto& r : s.automap_rules) {
        if (r.level_type != L.type || r.orientation != orientation) continue;
        if (r.main >= 0 && r.main != main) continue;
        if (r.sub0 >= 0 && (sub < r.sub0 || sub > r.sub1)) continue;
        return r.cels[hash % r.cels.size()];
    }
    return -1;
}

void automap_reveal(const Scene& s, const Level& level, Automap& am, float px, float py) {
    const auto& m = level.ds1;
    const int w = int(m.width()), h = int(m.height());
    if (w == 0) return;
    auto& seen = am.revealed[level.id];
    if (seen.size() != std::size_t(w * h)) seen.assign(std::size_t(w * h), 0);
    const int cx = int(std::floor(px)), cy = int(std::floor(py)), R = 12;
    for (int ty = std::max(0, cy - R); ty < std::min(h, cy + R); ++ty)
        for (int tx = std::max(0, cx - R); tx < std::min(w, cx + R); ++tx) {
            auto& done = seen[std::size_t(ty * w + tx)];
            if (done) continue;
            done = 1;
            const int wx = tx + level.world_x, wy = ty + level.world_y;     // act tiles
            const std::uint32_t hash = std::uint32_t(wx * 73856093) ^ std::uint32_t(wy * 19349663);
            const int ax = (wx - wy) * 80 / 10, ay = (wx + wy) * 40 / 10;
            for (const auto& L : m.floors()) {
                const auto& t = L.cells[std::size_t(ty * w + tx)];
                if (t.hidden || !(t.prop1 & 2)) continue;
                if (const int c = automap_cel(s, level, 0, t.style, t.sequence, hash); c >= 0) am.cells.push_back({ c, ax, ay });
            }
            for (const auto& L : m.walls()) {
                const auto& t = L.cells[std::size_t(ty * w + tx)];
                if (t.hidden || t.wall_type == 0) continue;
                if (const int c = automap_cel(s, level, t.wall_type, t.style, t.sequence, hash); c >= 0)
                    am.cells.push_back({ c, ax, ay + (t.wall_type > 15 ? 24 : 0) });
            }
        }
}

void draw_automap(std::vector<std::uint8_t>& fb, const Scene& s, const Automap& am, float px, float py) {
    if (!am.open || s.automap_cels.frames_per_direction() == 0) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int scroll_x = int(std::lround((px - py) * 80 / 10)) - int(kW) / 2 + 40;
    const int scroll_y = int(std::lround((px + py) * 40 / 10)) - int(kH) / 2 + 15;
    for (const auto& c : am.cells) {
        if (c.cel < 0 || std::uint32_t(c.cel) >= s.automap_cels.frames_per_direction()) continue;
        const auto& f = s.automap_cels.frame(0, std::uint32_t(c.cel));
        const int x = c.x - scroll_x, y = c.y - scroll_y;
        if (x < -32 || x > int(kW) + 32 || y < -64 || y > int(kH) + 64) continue;
        blit_sprite(fb, f, pal, x, y - int(f.height) + 1);
    }
    // Your own mark (FUN_0045a860 -> FUN_0045a7f0): the 13-point shape at
    // 0x6d6638 doubled, at (unit px / div - scroll + 8, py / div - scroll
    // - 8), in the palette colour nearest FUN_004fb180(0, 0, 0xff) — the
    // palette is BGR, so red (party green, other players blue).
    static constexpr int kMark[13][2] = { {0,-1},{2,-2},{4,-1},{2,0},{4,1},{2,2},{0,1},{-2,2},{-4,1},{-2,0},{-4,-1},{-2,-2},{0,-1} };
    std::uint8_t mr = 255, mg = 0, mb = 0;
    {
        int best = 1 << 30;
        for (std::size_t i = 0; i < 256 && i < pal.entries().size(); ++i) {
            const auto c = pal[std::uint8_t(i)];
            const int d = (c.r - 255) * (c.r - 255) + c.g * c.g + c.b * c.b;
            if (d < best) { best = d; mr = c.r; mg = c.g; mb = c.b; }
        }
    }
    const int ux = int(std::lround((px - py) * 80 / 10)) - scroll_x + 8;
    const int uy = int(std::lround((px + py) * 40 / 10)) - scroll_y - 8;
    auto plot = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= int(kW) || y >= int(kH)) return;
        auto* p = &fb[(std::size_t(y) * kW + std::size_t(x)) * 4];
        p[0] = mr; p[1] = mg; p[2] = mb;
    };
    for (int i = 0; i + 1 < 13; ++i) {                   // Bresenham, like a D2GFX line
        int x0 = ux + kMark[i][0] * 2, y0 = uy + kMark[i][1] * 2;
        const int x1 = ux + kMark[i + 1][0] * 2, y1 = uy + kMark[i + 1][1] * 2;
        const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        for (int err = dx + dy;;) {
            plot(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

bool waypoint_act_open(const d2d::d2s::Header& h, int act) {
    static constexpr int kQuest[5] = { -1, 7, 15, 23, 26 };
    return act == 0 || h.quest_flag(h.active_difficulty(), kQuest[act], 0);
}

int waypoint_row_at(const Scene& s, const WaypointUI& ui, const d2d::d2s::Header& h, int mx, int my) {
    const auto& rows = s.waypoint_levels[std::size_t(ui.tab)];
    for (std::size_t i = 0; i < rows.size() && i < 9; ++i) {
        if (!h.waypoint(h.active_difficulty(), rows[i].wp)) continue;
        const int x = kCharPanelX + 17, y = 60 + kWpHitTop[i];
        if (mx > x && mx < x + 280 && my > y && my < y + 30) return int(i);
    }
    return -1;
}

int waypoint_tab_at(const d2d::d2s::Header& h, bool expansion, int mx, int my) {
    const int x = mx - kCharPanelX, y = my - 60;
    if (y > 30 || x < 0 || x > 320) return -1;
    const int t = std::min(x / (expansion ? 64 : 80), expansion ? 4 : 3);
    return waypoint_act_open(h, t) ? t : -1;
}

void draw_waypoints(std::vector<std::uint8_t>& fb, const Scene& s, const WaypointUI& ui,
                    const d2d::d2s::Header& h, bool expansion, int current_level, int mx, int my) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    if (s.wp_bg.frames_per_direction() >= 4) {
        const auto& f0 = s.wp_bg.frame(0, 0);
        blit_sprite(fb, f0, pal, kCharPanelX, kCharPanelY);
        blit_sprite(fb, s.wp_bg.frame(0, 1), pal, kCharPanelX + int(f0.width), kCharPanelY);
        blit_sprite(fb, s.wp_bg.frame(0, 2), pal, kCharPanelX, kCharPanelY + int(f0.height));
        blit_sprite(fb, s.wp_bg.frame(0, 3), pal, kCharPanelX + int(f0.width), kCharPanelY + int(f0.height));
    }
    static constexpr int kTabX[2][5] = { { 3, 81, 159, 237, 0 }, { 3, 67, 129, 191, 253 } };
    const auto& tabs = s.wp_tabs[expansion ? 1 : 0];
    for (int i = 0; i < (expansion ? 5 : 4); ++i) {
        if (i != ui.tab && !waypoint_act_open(h, i)) continue;
        const auto fi = std::uint32_t(i * 2 + (i == ui.tab ? 0 : 1));
        if (fi >= tabs.frames_per_direction()) continue;
        const auto& f = tabs.frame(0, fi);
        blit_sprite(fb, f, pal, kCharPanelX + kTabX[expansion ? 1 : 0][i], 60 + 34 - int(f.height) + 1);
    }
    const int cell = s.font.sheet().frames_per_direction() > 0 ? int(s.font.sheet().frame(0, 0).height) : 16;
    auto text = [&](int x, int baseline, const std::string& t, std::array<std::uint8_t, 3> c) {
        s.font.draw_tinted(fb, kW, kH, pal, x, baseline - cell + 1, t, c[0], c[1], c[2]);
    };
    const int diff = h.active_difficulty();
    const auto& rows = s.waypoint_levels[std::size_t(ui.tab)];
    const int hover = waypoint_row_at(s, ui, h, mx, my);
    bool others = false;
    for (const auto& a : s.waypoint_levels)
        for (const auto& r : a) others |= r.level != current_level && h.waypoint(diff, r.wp);
    for (std::size_t i = 0; i < rows.size() && i < 9; ++i) {
        const bool active = h.waypoint(diff, rows[i].wp), here = rows[i].level == current_level;
        const int fi = here ? 0 : active ? 3 + (hover == int(i)) : -1;
        if (fi >= 0 && std::uint32_t(fi) < s.wp_icons.frames_per_direction()) {
            const auto& f = s.wp_icons.frame(0, std::uint32_t(fi));
            blit_sprite(fb, f, pal, kCharPanelX + 17, 60 + kWpIconBottom[i] - int(f.height) + 1);
        }
        const auto name = lookup_string(s, std::string_view(rows[i].name));
        text(kCharPanelX + 80, 60 + kWpTextBase[i], name ? u16_to_latin1(*name) : rows[i].name,
             !active ? kTxtGrey : here || hover == int(i) ? kTxtBlue : kTxtWhite);
    }
    const auto title = string_id(s, others ? 0xf96 : 0xf97);
    text(kCharPanelX + 160 - s.font.measure(title) / 2, 108, title, kTxtWhite);
    if (std::uint32_t(11) < s.store_buttons.frames_per_direction()) {
        const auto& f = s.store_buttons.frame(0, ui.cancel_down ? 11 : 10);
        blit_sprite(fb, f, pal, kCharPanelX + 0x111, 477 - int(f.height) + 1);
    }
    if (mx - (kCharPanelX + 0x111) >= 0 && mx - (kCharPanelX + 0x111) < 0x24 && my - 0x183 - 60 >= 0 && my - 0x183 - 60 < 0x22) {
        const auto c = string_id(s, 0x1022);
        const int w = s.font.measure(c);
        draw_hover_text(fb, s, { { c, kTxtWhite } }, kCharPanelX + 0x126 - w / 2, kCharPanelX + 0x126 + w / 2, 0x183 + 60, 0x172 + 60);
    }
}

bool draw_quest_log(std::vector<std::uint8_t>& fb, const Scene& s, QuestLog& q, const d2d::rules::QuestBits& f,
                    const QuestState& st, std::uint32_t ms) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int px = kCharPanelX, py = kCharPanelY;
    bool sound = false;
    auto bottom = [&](const d2d::dc6::Sprite& sp, std::uint32_t frame, int x, int y) {   // DC6s draw up from their bottom-left
        if (frame >= sp.frames_per_direction()) return;
        const auto& fr = sp.frame(0, frame);
        blit_sprite(fb, fr, pal, px + x, py + y - int(fr.height));
    };
    bottom(s.quest_bg, 0, 0, 256); bottom(s.quest_bg, 1, 256, 256);
    bottom(s.quest_bg, 2, 0, 432); bottom(s.quest_bg, 3, 256, 432);
    for (int a = 0; a < 5; ++a) bottom(s.quest_tabs, std::uint32_t(a * 2 + (a == q.act ? 0 : 1)), kQuestTabX[std::size_t(a)], 33);
    const QuestEntry* sel = nullptr;
    for (const auto& e : kQuestLog) {
        if (e.act != q.act) continue;
        const auto [x, y] = kQuestSlot[std::size_t(e.slot)];
        const bool on = e.slot == q.slot;
        if (on) sel = &e;
        int frame = quest_icon_frame(f, e.quest, on);
        const auto k = std::size_t(e.quest);
        if (frame == 24 && k < q.seen.size() && !q.seen[k] && !d2d::rules::qbit(f, e.quest, 12)) {   // the done animation
            if (!q.frame_ms[k]) q.frame_ms[k] = ms;
            if (ms - q.frame_ms[k] > 100) {
                q.frame_ms[k] = ms;
                if (++q.frame[k] == 1) sound = true;
            }
            if (q.frame[k] > 24) { q.frame[k] = 24; q.seen[k] = true; }
            frame = q.frame[k];
        }
        bottom(s.quest_icons[std::size_t(e.icon)], std::uint32_t(frame), x, y);
        bottom(s.quest_sockets, on ? 1u : 0u, x - 4, y + 5);
    }
    if (std::uint32_t(11) < s.store_buttons.frames_per_direction()) bottom(s.store_buttons, q.close_down ? 11u : 10u, 0x116, 422);
    bottom(s.quest_last, q.last_down ? 1u : 0u, 0xe2, 422);
    if (!sel) return sound;
    auto centred = [&](const std::string& t, int y) { s.font.draw(fb, kW, kH, pal, px + (320 - s.font.measure(t)) / 2, py + y - s.font.line_height(), t); };
    centred(string_id(s, std::uint16_t(sel->name)), 248);
    if (const auto qt = quest_text(f, sel->quest, st); qt.string) {   // word-wrapped to 270 px (FUN_00502970(0x10e))
        std::string text = string_id(s, std::uint16_t(qt.string)), row;
        if (qt.count >= 0) text += std::to_string(qt.count);
        int y = 270;
        std::size_t a = 0;
        while (a < text.size()) {
            const auto b = std::min(text.find(' ', a), text.size());
            const std::string word = text.substr(a, b - a);
            if (!row.empty() && s.font.measure(row + " " + word) > 270) { centred(row, y); y += 20; row.clear(); }
            row += (row.empty() ? "" : " ") + word;
            a = b + 1;
        }
        if (!row.empty()) centred(row, y);
    }
    return sound;
}


// skilltree.hpp
std::pair<int, int> skill_icon_at(const d2d::rules::ClassSkill& sk) {
    static constexpr int kCol[4] = { 0, 0x131, 0xec, 0xa7 };
    static constexpr int kRow[7] = { 0, 0x1a2, 0x15e, 0x11a, 0xd6, 0x91, 0x4d };
    if (sk.col < 1 || sk.col > 3 || sk.row < 1 || sk.row > 6) return { -1000, -1000 };
    return { kTreeR - kCol[sk.col], kTreeB - kRow[sk.row] };
}

int skill_at(const Scene& s, int cls, int tab, int mx, int my) {
    const auto& list = s.rules.class_skills[std::size_t(cls)];
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].page != tab) continue;
        const auto [x, y] = skill_icon_at(list[i]);
        if (mx > x && mx < x + 0x30 && my > y - 0x30 && my < y) return int(i);
    }
    return -1;
}

int skill_tab_at(int mx, int my) {
    if (mx < kTreeR - 0x58 || mx > kTreeR) return 0;
    if (my > kTreeB - 0x174 && my < kTreeB - 0x109) return 3;
    if (my > kTreeB - 0x108 && my < kTreeB - 0x9d) return 2;
    if (my > kTreeB - 0x9c && my < kTreeB - 0x31) return 1;
    return 0;
}

void draw_skill_tree(std::vector<std::uint8_t>& fb, const Scene& s, int cls, int tab,
                     const std::array<std::uint8_t, 30>& lv, const d2d::d2s::Stats& st, int pressed, int mx, int my) {
    if (cls < 0 || cls > 6) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    auto dc6 = [&](const d2d::dc6::Sprite& spr, int frame, int x, int y, int shade = 256) {
        if (frame < 0 || spr.frames_per_direction() <= std::uint32_t(frame)) return;
        const auto& f = spr.frame(0, std::uint32_t(frame));
        blit_sprite(fb, f, pal, x, y - int(f.height) + 1, shade);
    };
    const auto& bg = s.skill_tree_bg[std::size_t(cls)];
    for (const int base : { 0, 4 * tab }) {
        dc6(bg, base + 0, kTreeR - 0x140, kTreeB - 0xe0);
        dc6(bg, base + 1, kTreeR - 0x40, kTreeB - 0xe0);
        dc6(bg, base + 2, kTreeR - 0x140, kTreeB - 0x30);
        dc6(bg, base + 3, kTreeR - 0x40, kTreeB - 0x30);
    }
    auto centred = [&](const d2d::font::Font& f, int x0, int x1, int y, const std::string& t) {
        const int w = f.measure(t);
        const int x = w < x1 - x0 + 1 ? x0 + (x1 - x0 + 1 - w) / 2 : x0;
        f.draw(fb, kW, kH, pal, x, y - int(f.sheet().frame(0, 0).height) + 1, t);
    };
    for (const auto& list : { std::vector<TreeLabel>(std::begin(kTreeHeader), std::end(kTreeHeader)),
                              kTreeTabs[std::size_t(cls)] })
        for (const auto& l : list)
            if (auto v = lookup_string(s, l.id)) centred(s.font, kTreeR - 0x5a, kTreeR, l.y, u16_to_latin1(*v));
    const auto pts = st.get(d2d::d2s::kSkillPts);
    centred(s.font, kTreeR - 0x41, kTreeR - 0x19, 140, std::to_string(pts));
    const auto& list = s.rules.class_skills[std::size_t(cls)];
    const int clvl = int(st.get(d2d::d2s::kLevel));
    int hover = -1;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& sk = list[i];
        if (sk.page != tab) continue;
        const auto [x, y] = skill_icon_at(sk);
        const bool live = pts > 0 ? d2d::rules::can_learn(s.rules, cls, int(i), lv, clvl) : lv[i] > 0;
        dc6(s.skill_icons[std::size_t(cls)], sk.icon + (int(i) == pressed ? 1 : 0), x, y, live ? 256 : 128);
        if (lv[i] > 0) {
            const auto& f = lv[i] > 9 ? (s.font_small.line_height() > 0 ? s.font_small : s.font) : s.font;
            f.draw(fb, kW, kH, pal, x + 0x30 - (lv[i] > 9 ? 4 : 0), y + 0xc - int(f.sheet().frame(0, 0).height) + 1,
                   std::to_string(lv[i]));
        }
        if (mx > x && mx < x + 0x30 && my > y - 0x30 && my < y) hover = int(i);
    }
    if (hover >= 0 && !list[std::size_t(hover)].name.empty()) {
        const auto [x, y] = skill_icon_at(list[std::size_t(hover)]);
        if (auto v = lookup_string(s, list[std::size_t(hover)].name))
            draw_hover_text(fb, s, { { u16_to_latin1(*v), kTxtWhite } }, x, x + 0x30, y, y - 0x30);
    }
}


// store.hpp
NpcMenuState open_hire_menu(const Scene& s, int npc, const std::vector<d2d::rules::MercOffer>& offers,
                            std::int64_t gold) {
    NpcMenuState m;
    m.npc = npc;
    std::string head = string_id(s, 0xd24);
    if (const auto p = head.find("%d"); p != head.npos) head.replace(p, 2, std::to_string(gold));
    m.lines.push_back({ head, 21, 0, 0, true });
    auto label = [&](std::uint16_t id) { return string_id(s, id) + ": "; };
    for (std::size_t i = 0; i < offers.size(); ++i) {
        const auto& o = offers[i];
        const auto merc = s.mercs.find(o.id);
        const auto name = merc != s.mercs.end() ? merc_name(s, merc->second, o.name) : std::string("?");
        m.lines.push_back({ name + " - " + label(0xd28) + std::to_string(o.level) + "  " + label(0xd26) + std::to_string(o.life)
                                + "  " + label(0xd27) + std::to_string(o.def) + "  " + label(0xd29) + std::to_string(o.cost),
                            0x23, 0, 0, false, NpcMenuState::kHireOffer, int(i) });
    }
    m.lines.push_back({ string_id(s, 0xd48), 0x23 });
    m.w = 0x1ea; m.h = 0x15e;
    m.x = (int(kW) - m.w) / 2; m.y = (int(kH) - m.h) / 2;
    for (auto& l : m.lines) {
        l.width = s.font.measure(l.text);
        l.x = std::max(4, (m.w - l.width) / 2);
    }
    return m;
}

int store_item_at(const Scene& s, const Store& st, int mx, int my) {
    const auto& tab = st.tabs[std::size_t(st.tab)];
    for (std::size_t i = 0; i < tab.size(); ++i) {
        const auto [w, h] = d2d::rules::item_size(s.rules, tab[i].code);
        const int x = 96 + tab[i].column * 29, y = 123 + tab[i].row * 29;
        if (mx >= x && mx < x + w * 29 && my >= y && my < y + h * 29) return int(i);
    }
    return -1;
}

std::array<int, 4> store_button_frames(const Store& st) {
    const bool repair = st.npc >= 0 && d2d::rules::is_repair_vendor(st.hc_idx);
    return { 2, 4, repair ? 6 : 0, repair ? 18 : 10 };
}

void draw_store(std::vector<std::uint8_t>& fb, const Scene& s, const Store& st, int mx, int my, int clvl) {
    if (st.npc < 0) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    if (s.store_panel.frames_per_direction() >= 4) {
        const auto& f0 = s.store_panel.frame(0, 0);
        blit_sprite(fb, f0, pal, kCharPanelX, kCharPanelY);
        blit_sprite(fb, s.store_panel.frame(0, 1), pal, kCharPanelX + int(f0.width), kCharPanelY);
        blit_sprite(fb, s.store_panel.frame(0, 2), pal, kCharPanelX, kCharPanelY + int(f0.height));
        blit_sprite(fb, s.store_panel.frame(0, 3), pal, kCharPanelX + int(f0.width), kCharPanelY + int(f0.height));
    }
    static constexpr int kTabLabelX[4] = { 42, 121, 201, 281 };
    static constexpr std::uint16_t kTabString[4] = { 0xfc4, 0xfc5, 0xfc5, 0xfc7 };
    for (int i = 0; i < 4; ++i) {
        const bool active = i == st.tab;
        if (s.store_tabs.frames_per_direction() >= 8) {
            const auto& f = s.store_tabs.frame(0, std::uint32_t(active ? i : i + 4));
            blit_sprite(fb, f, pal, kCharPanelX + 80 * i, 90 - int(f.height) + 1);
        }
        const std::string label = string_id(s, kTabString[i]);
        const int w = s.font.measure(label);
        const int cell = s.font.sheet().frames_per_direction() > 0 ? int(s.font.sheet().frame(0, 0).height) : 16;
        const int x = kCharPanelX + kTabLabelX[i] - w / 2, y = 79 - cell + 1;
        if (active) s.font.draw_tinted(fb, kW, kH, pal, x, y, label, 199, 179, 119);
        else        s.font.draw(fb, kW, kH, pal, x, y, label);
    }
    const auto frames = store_button_frames(st);
    static constexpr int kBtnX[4] = { 116, 169, 221, 273 };
    for (int i = 0; i < 4; ++i)
        if (std::uint32_t(frames[std::size_t(i)] + 1) < s.store_buttons.frames_per_direction()) {
            const bool down = st.pressed[std::size_t(i)] || (i < 2 && st.mode == i + 1);
            const auto& f = s.store_buttons.frame(0, std::uint32_t(frames[std::size_t(i)] + (down ? 1 : 0)));
            blit_sprite(fb, f, pal, kCharPanelX - 1 + kBtnX[i], 476 - int(f.height) + 1);
        }
    // Stock, Monster2 grid.
    Scene::InvLayout L;
    L.grid_x = 96; L.grid_y = 123; L.box_w = L.box_h = 29;
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hb{};
    for (const auto& it : st.tabs[std::size_t(st.tab)]) {
        const auto [x, y, w, h] = grid_rect(s, L, it);
        if (const auto* spr = s.item_sprite(it); spr && spr->frames_per_direction() > 0) {
            const auto& f = spr->frame(0, 0);
            blit_sprite(fb, f, s.item_pal(it, pal), x + (w - int(f.width)) / 2, y + (h - int(f.height)) / 2);
        }
        if (mx >= x && mx < x + w && my >= y && my < y + h) { hover = &it; hb = { x, y, w, h }; }
    }
    if (hover) {
        auto lines = item_lines(s, *hover, clvl);
        // "Cost: " (0xd01) + the vendor's price, as the store hover shows it (FUN_004b2ad0).
        // At the gamble screen, the gamble price (FUN_00629370).
        const int price = st.gamble ? d2d::rules::gamble_price(s.rules, hover->code, clvl)
                                    : d2d::rules::item_price(s.rules, *hover, st.npc_id, false, st.header);
        lines.push_back({ string_id(s, 0xd01) + std::to_string(price), kTxtWhite });
        draw_hover_text(fb, s, lines, hb[0], hb[0] + hb[2], hb[1] + hb[3], hb[1]);
    }
}

void draw_gold(std::vector<std::uint8_t>& fb, const Scene& s, const d2d::d2s::Stats& st, bool store) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int cell = s.font.sheet().frames_per_direction() > 0 ? int(s.font.sheet().frame(0, 0).height) : 16;
    auto text = [&](int x, int baseline, const std::string& t) { s.font.draw(fb, kW, kH, pal, x, baseline - cell + 1, t); };
    if (!store) {
        if (s.gold_coin.frames_per_direction() > 0) {
            const auto& f = s.gold_coin.frame(0, 0);
            blit_sprite(fb, f, pal, 484, 469 - int(f.height) + 1);
        }
        text(508, 468, std::to_string(st.get(d2d::d2s::kGold)));
        return;
    }
    text(101, 434, string_id(s, 0xcf3));
    const auto n = std::to_string(st.get(d2d::d2s::kGoldBank));
    text(278 - s.font.measure(n), 434, n);
}


// cursor.hpp
CursorClick item_cursor_command(const Scene& s, const std::vector<d2d::d2s::Item>& items, const std::optional<d2d::d2s::Item>& held,
                                int save_cls, const OpenPanels& open, int mx, int my) {
    const auto& t = s.rules;
    auto inside = [&](int x, int y, int w, int h) { return mx >= x && mx < x + w && my >= y && my < y + h; };
    // Grids: the inventory (panel 1), and the stash (5) or cube (4) on the left.
    struct Grid { const Scene::InvLayout* L; int panel; };
    std::vector<Grid> grids;
    const auto& inv = s.inv_layout[std::size_t(save_cls)];
    if (open.inv) grids.push_back({ &inv, 1 });
    if (open.cube) grids.push_back({ &s.cube_layout, 4 });
    else if (open.stash) grids.push_back({ &s.stash_layout[open.expansion ? 1 : 0], 5 });
    for (const auto& [L, panel] : grids) {
        if (!inside(L->grid_x, L->grid_y, L->cols * L->box_w, L->rows * L->box_h)) continue;
        if (held) {
            // The held item is drawn centred on the cursor: its top-left
            // cell is the one under the cursor, shifted back half its size.
            const auto [w, h] = d2d::rules::item_size(t, held->code);
            const int col = int(std::floor((float(mx - L->grid_x) - float((w - 1) * L->box_w) / 2.f) / float(L->box_w)));
            const int row = int(std::floor((float(my - L->grid_y) - float((h - 1) * L->box_h) / 2.f) / float(L->box_h)));
            return { true, cmd::ToGrid{ panel, col, row } };
        }
        for (const auto& it : items) {
            if (it.location != 0 || it.panel != panel) continue;
            const auto r = grid_rect(s, *L, it);
            if (inside(r[0], r[1], r[2], r[3])) return { true, cmd::ToCursor{ it.id } };
        }
        return { true, {} };
    }
    if (open.inv)
        for (int slot = 1; slot <= 10; ++slot) {
            const auto& r = inv.slots[std::size_t(slot)];
            if (r[2] <= 0 || !inside(r[0], r[1], r[2], r[3])) continue;
            if (held) return { true, cmd::ToBody{ slot } };
            for (const auto& it : items)
                if (it.location == 1 && it.slot == slot) return { true, cmd::ToCursor{ it.id } };
            return { true, {} };
        }
    // Belt boxes: row 1 on the HUD strip, the rest with the popup open.
    const auto& B = s.belts[std::size_t(belt_index(s, items))];
    for (int b = 0; b < B.boxes && b < int(B.box.size()); ++b) {
        if (b > 3 && !open.belt_popup) break;
        const auto& r = B.box[std::size_t(b)];
        if (r[1] <= r[0] || mx < r[0] || mx > r[1] || my < r[2] || my > r[3]) continue;
        if (held) return { true, cmd::ToBelt{ b } };
        for (const auto& it : items)
            if (it.location == 2 && it.column == b) return { true, cmd::ToCursor{ it.id } };
        return {};                                         // empty box: the strip toggles the popup
    }
    return {};
}

void draw_held(std::vector<std::uint8_t>& fb, const Scene& s, const d2d::d2s::Item& it, int mx, int my) {
    const auto* spr = s.item_sprite(it);
    if (!spr || spr->frames_per_direction() == 0) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const auto& f = spr->frame(0, 0);
    blit_sprite(fb, f, s.item_pal(it, pal), mx - int(f.width) / 2, my - int(f.height) / 2);
}


// ingame.hpp
void draw_rain(std::vector<std::uint8_t>& fb, const d2d::rules::Rain& rain) {
    const int bottom = int(kH) - 47;
    for (const auto& d : rain.drops) {
        int dx = 0, dy = 0;
        if (!d.landed) {
            dx = int(d2d::rules::cos512(rain.wind) * float(d.len));
            dy = int(d2d::rules::sin512(rain.wind) * float(d.len));
            if (const int room = d.bottom - d.y; room < dy) { dx = dy ? room * dx / dy : 0; dy = room; }
        }
        const auto rgb = d2d::rules::rain_rgb(d.kind, d.tone);
        const bool half = d.kind == 0;
        const int n = std::max({ std::abs(dx), std::abs(dy), 1 });
        for (int k = 0; k <= n; ++k) {
            const int x = d.x + dx * k / n, y = d.y + dy * k / n;
            if (x < 0 || y < 0 || x >= int(kW) || y >= bottom) continue;
            auto* p = fb.data() + (std::size_t(y) * kW + std::size_t(x)) * 4;
            for (int c = 0; c < 3; ++c) p[c] = half ? std::uint8_t((p[c] + rgb[std::size_t(c)]) / 2) : rgb[std::size_t(c)];
        }
    }
}

void render_ingame(std::vector<std::uint8_t>& fb,
                   const Scene& s,
                   const Level& L,
                   int class_idx,
                   const Scene::Appearance& gfx,
                   std::string_view name,
                   bool hardcore,
                   float cam_x,
                   float cam_y,
                   int player_mode,
                   int player_dir,
                   std::uint32_t elapsed_ms,
                   int mouse_x , int mouse_y ,
                   std::span<const UnitState> npcs ,
                   const std::vector<d2d::d2s::Item>* inventory ,
                   const d2d::d2s::Stats* char_stats ,
                   const d2d::d2s::Stats* hud_stats ,
                   const PanelStats* panel ,
                   std::uint32_t player_mode_ms ,
                   const std::vector<d2d::d2s::Item>* belt ,
                   int* hovered_npc ,
                   const std::vector<d2d::d2s::Item>* stash , bool stash_expansion ,
                   bool belt_popup , bool cube_open ,
                   const NpcMenuState* npc_menu , const Speech* speech ,
                   const Automap* automap , const Store* store ,
                   int stat_pressed ,
                   const Npc* merc , const UnitState* merc_state ,
                   const std::string* merc_label ,
                   std::span<const Unit> extra_units , float player_rate ,
                   const Lighting* light , d2d::rules::Rain* rain , bool player_visible ,
                   const Unit* player_look ,     // its states' colour shift and overlays
                   bool show_items) {             // Alt held: every ground item's name
    // Prefer the real tile-composited world when townE1.ds1 loaded; fall
    // back to the credits DC6 placeholder when it didn't (headless CI, a
    // stripped MPQ dir, etc.). Palette follows the render path: ACT1 for
    // the tiles, Sky for the credits DC6 which was authored against it.
    if (!L.dt1s.empty()) {
        set_phase(MainPhase::IngameClear);
        // Clear to black — tiles don't cover every subtile so an
        // uninitialized fb would leak the previous frame's contents.
        std::fill(fb.begin(), fb.end(), std::uint8_t{0});
        for (std::size_t i = 3; i < fb.size(); i += 4) fb[i] = 0xFF;
        set_phase(MainPhase::IngameFloor);   // render_world does floor+shadow+walls internally
        // Player: the camera follows them, so their feet sit on the camera
        // point (kW/2, kH/2 + kIsoH/2); render_world slots them into the
        // wall pass by depth. Wears the loaded save's gear, or the
        // class's starting gear.
        std::vector<Unit> units;
        units.reserve(L.npcs.size() + 1);
        if (player_visible && class_idx >= 0 && class_idx < 7)
            units.push_back({ cam_x, cam_y, &s.composite(kUiToSaveClass[class_idx], player_mode, gfx),
                              player_dir, nullptr, player_mode_ms });
        if (!units.empty()) {
            units.back().rate = player_rate;
            units.back().overlay_class = 1;                 // FUN_006223a0: a player's Height2
            if (player_look) { units.back().shift = player_look->shift; units.back().overs = player_look->overs; }
        }
        // NPCs and objects, at their live position when they patrol.
        for (std::size_t i = 0; i < L.npcs.size(); ++i) {
            const auto& n = L.npcs[i];
            const UnitState* st = i < npcs.size() ? &npcs[i] : nullptr;
            if (st && st->hidden) continue;
            const float x = st ? st->x : n.x, y = st ? st->y : n.y;
            if (std::abs(x - cam_x) >= 14 || std::abs(y - cam_y) >= 14) continue;
            const auto& anim = s.npc_anim(n, st && st->walking ? std::string_view("WL") : st && !st->mode.empty() ? st->mode : std::string_view(n.mode));
            static const std::string none;
            units.push_back({ x, y, &anim, st ? st->dir : 0, st && !st->mode.empty() && n.root == "objects" ? &none : &n.name, st ? st->mode_ms : 0, int(i) });
            if (st && st->alert) units.back().overlay = &s.npc_alert;
            units.back().shadow = n.root != "objects";
        }
        // The neighbour levels' objects and NPCs (torches by the camp's
        // gate, Flavie), as they stand: D2 draws the rooms round the player
        // whichever level they're in. Not clickable from here (npc -3).
        // ponytail: NPCs at their start, where npc_start puts them on
        // crossing (the World steps only the player's level); quest-gated
        // ones (Cain) left out.
        static const std::string no_name;
        for (const auto& nb : L.nearby)
            for (const auto& n : nb.level->npcs) {
                if (n.quest) continue;
                const float x = n.x + float(nb.dx), y = n.y + float(nb.dy);
                if (std::abs(x - cam_x) >= 14 || std::abs(y - cam_y) >= 14) continue;
                units.push_back({ x, y, &s.npc_anim(n, n.mode), 0, &no_name, 0, -3 });
                units.back().shadow = n.root != "objects";
            }
        if (merc && merc_state)                    // npc -2: not an NPC-menu unit
            units.push_back({ merc_state->x, merc_state->y,
                              &s.npc_anim(*merc, merc_state->walking ? std::string_view("WL") : std::string_view("NU")),
                              merc_state->dir, merc_label, merc_state->mode_ms, -2 });
        units.insert(units.end(), extra_units.begin(), extra_units.end());
        if (hovered_npc && *hovered_npc != -1)            // last frame's: brighter (render_world)
            for (auto& u : units) if (u.npc == *hovered_npc && u.name) u.highlight = true;
        std::pair<const Unit*, std::array<int, 4>> hovered{ nullptr, {} };
        std::vector<std::pair<const Unit*, std::array<int, 4>>> items;
        render_world(fb, s, L, cam_x, cam_y, elapsed_ms, units, mouse_x, mouse_y, &hovered, light, rain, show_items ? &items : nullptr);
        if (rain) draw_rain(fb, *rain);
        if (hovered_npc) *hovered_npc = hovered.first ? hovered.first->npc : -1;
        // Alt ("Show Items"): each ground item's name in a dark box over it,
        // nudged up clear of the ones already placed; the label under the
        // mouse is the item it points at (a click picks it up).
        // ponytail: game.exe's label layout isn't traced (box padding, the
        // stacking order, the hovered label's own colour).
        if (show_items) {
            const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
            std::ranges::sort(items, {}, [](const auto& e) { return -e.second[3]; });   // nearest the bottom first
            std::vector<std::array<int, 4>> placed;
            const int lh = s.font.line_height();
            for (const auto& [u, b] : items) {
                if (!u->name || u->name->empty()) continue;
                const int w = s.font.measure(*u->name) + 8;
                std::array<int, 4> r{ (b[0] + b[2]) / 2 - w / 2, b[1] - lh - 4, 0, 0 };
                r[2] = r[0] + w; r[3] = r[1] + lh + 2;
                for (bool moved = true; moved;) {
                    moved = false;
                    for (const auto& p : placed)
                        if (r[0] < p[2] && p[0] < r[2] && r[1] < p[3] && p[1] < r[3]) { const int dy = r[3] - p[1]; r[1] -= dy; r[3] -= dy; moved = true; }
                }
                placed.push_back(r);
                for (int y = std::max(r[1], 0); y < std::min(r[3], int(kH)); ++y)
                    for (int x = std::max(r[0], 0); x < std::min(r[2], int(kW)); ++x) {
                        auto* p = &fb[(std::size_t(y) * kW + std::size_t(x)) * 4];
                        p[0] = std::uint8_t(p[0] / 4); p[1] = std::uint8_t(p[1] / 4); p[2] = std::uint8_t(p[2] / 4);
                    }
                s.font.draw_tinted(fb, kW, kH, pal, r[0] + 4, r[1] + 1, *u->name, u->rgb[0], u->rgb[1], u->rgb[2]);
                if (mouse_x >= r[0] && mouse_x < r[2] && mouse_y >= r[1] && mouse_y < r[3]) {
                    if (hovered_npc) *hovered_npc = u->npc;
                    hovered = { nullptr, {} };              // the label names it: no second name
                }
            }
        }
        // Name over whatever the cursor points at, centred above it.
        if (hovered.first && (hovered.first->npc > -10 || hovered.first->npc <= -1000)) {   // monsters: their bar at the top
            const auto& nm = *hovered.first->name;
            const auto& b  = hovered.second;
            const auto& c  = hovered.first->rgb;
            const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
            s.font.draw_tinted(fb, kW, kH, pal, (b[0] + b[2]) / 2 - s.font.measure(nm) / 2,
                               b[1] - s.font.line_height() - 2, nm, c[0], c[1], c[2]);
        }
        if (inventory && class_idx >= 0 && class_idx < 7)
        {
            // With a store open, your items show what the vendor pays ("Sell value: ", 0xd03).
            std::function<std::string(const d2d::d2s::Item&)> sell_price;
            if (store && store->npc >= 0)
                sell_price = [&](const d2d::d2s::Item& it) {
                    return string_id(s, 0xd03) + std::to_string(d2d::rules::item_price(s.rules, it, store->npc_id, true, store->header));
                };
            draw_inventory(fb, s, s.inv_layout[std::size_t(kUiToSaveClass[class_idx])], *inventory,
                           mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1, &sell_price);
            if (hud_stats) draw_gold(fb, s, *hud_stats, false);
        }
        if (char_stats) draw_char_panel(fb, s, *char_stats, panel ? *panel : PanelStats{}, name, class_idx, stat_pressed);
        if (store && store->npc >= 0)
        {
            draw_store(fb, s, *store, mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1);
            if (hud_stats) draw_gold(fb, s, *hud_stats, true);
        }
        if (stash) {
            const int e = stash_expansion ? 1 : 0;
            if (cube_open)
                draw_storage(fb, s, *stash, s.cube_panel, s.cube_layout, 4, mouse_x, mouse_y,
                             hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1);
            else
                draw_storage(fb, s, *stash, s.stash_panel[std::size_t(e)], s.stash_layout[std::size_t(e)], 5,
                             mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1);
        }
        if (automap) draw_automap(fb, s, *automap, cam_x + float(L.world_x), cam_y + float(L.world_y));
        if (npc_menu) draw_npc_menu(fb, s, *npc_menu, mouse_x, mouse_y, elapsed_ms);
        if (speech) draw_speech(fb, s, *speech, elapsed_ms);
        if (hud_stats) draw_hud(fb, s, *hud_stats);
        if (belt) draw_belt(fb, s, *belt, mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1,
                            belt_popup);
        // Dev overlay: a red dot on every blocked subtile around the camera.
        if (g_debug_collision) {
            const int cx = int(cam_x * 5), cy = int(cam_y * 5);
            for (int sy = cy - 60; sy <= cy + 60; ++sy)
                for (int sx = cx - 60; sx <= cx + 60; ++sx) {
                    const float wx = (float(sx) + 0.5f) / 5, wy = (float(sy) + 0.5f) / 5;
                    if (!L.blocked(wx, wy)) continue;
                    const int px = int(kW) / 2 + int(std::lround(((wx - cam_x) - (wy - cam_y)) * (kIsoW / 2)));
                    const int py = int(kH) / 2 + kIsoH / 2 + int(std::lround(((wx - cam_x) + (wy - cam_y)) * (kIsoH / 2)));
                    for (int oy = -1; oy <= 1; ++oy)
                        for (int ox = -1; ox <= 1; ++ox) {
                            const int x = px + ox, y = py + oy;
                            if (x < 0 || y < 0 || x >= int(kW) || y >= int(kH)) continue;
                            auto* d = fb.data() + (std::size_t(y) * kW + std::size_t(x)) * 4;
                            d[0] = 255; d[1] = 0; d[2] = 0;
                        }
                }
        }
        set_phase(MainPhase::IngameHudText);
    } else {
        std::fill(fb.begin(), fb.end(), std::uint8_t{0});
        for (std::size_t i = 3; i < fb.size(); i += 4) fb[i] = 0xFF;
        blit_dc6_grid(fb, s.credits_bg, s.pal, 0, 0, s.bg_tiles_across);
    }
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    if (speech && speech->npc >= 0) return;               // the dev overlay would cover the speech box
    if (L.id != 1 && !L.dt1s.empty()) return;             // outside camp the top is the monster bar's

    std::string cls = kClassKey[class_idx];
    if (auto v = lookup_string(s, kClassKey[class_idx])) cls = u16_to_latin1(*v);

    constexpr const char* welcome = "WELCOME TO SANCTUARY";
    const int ww = s.font.measure(welcome);
    // Dev HUD at the top edge, clear of the player at screen centre.
    s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - ww/2, 8,
                       welcome, 255, 208, 80);

    // On hardcore, D2 marks the caption with a red " (HC)" suffix — we
    // fudge that with a red tint on the trailing tag.
    const std::string line = name.empty() ? cls : std::string(name) + " the " + cls;
    const int lw = s.font.measure(line);
    s.font.draw(fb, kW, kH, pal, int(kW)/2 - lw/2, 28, line);
    if (hardcore) {
        constexpr const char* tag = " (HARDCORE)";
        const int tw = s.font.measure(tag);
        s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - lw/2 + lw, 28,
                           tag, 220, 60, 60);
        (void)tw;
    }

    if (inventory || char_stats || stash || belt_popup) return;   // the hint would run under a panel
    constexpr const char* hint =
        "d2d dev build — click to walk around the Rogue camp";
    const int hw = s.font.measure(hint);
    s.font.draw(fb, kW, kH, pal, int(kW)/2 - hw/2, int(kH) - 140, hint);   // above the HUD bar
    constexpr const char* esc = "press Esc to return to title";
    const int ew = s.font.measure(esc);
    s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - ew/2, int(kH) - 120,
                       esc, 200, 200, 200);
}


// frontend.hpp
void render_title(std::vector<std::uint8_t>& fb,
                  const Scene& s,
                  std::span<const Button> buttons,
                  std::uint32_t elapsed_ms) {
    // Full-screen background — no need to clear; the 4×3 grid tiles fill
    // exactly 800×600 with no gaps.
    blit_dc6_grid(fb, s.bg, s.pal, 0, 0, s.bg_tiles_across);

    // Diablo2.dc6 (menu record 0x708e00) is the CLASSIC-D2 static DIABLO II
    // logo — a 320×151 pre-baked title asset. In LoD it stays LOADED but
    // never drawn: the animated D2logo{Black,Fire}{L,R} pieces replace it,
    // and gameselectscreenEXP.dc6 already has "EXPANSION SET / Lord of
    // Destruction" baked into the background. Drawing both stacks two
    // "DIABLO II"s at different anchors — Bret caught the doubled letters.
    // RE: game.exe gates the classic-mode fallback on DAT_007795ec == 0
    // (the "expansion installed" flag; see char-create-table.md). We
    // never load classic-only, so `logo_static` is intentionally unused.
    (void)s.logo_static;

    // "DIABLO II" animated logo halves — per RE records at 0x708e30 and
    // 0x708e60, both anchored at (400, 120). Each DC6 frame's bottom-left
    // origin (per DCC convention) places itself relative to that anchor.
    // Black silhouettes first, then fire fills on top with the warm-tint
    // hack (PL2 hue-variation colormap remains a follow-up).
    const auto n_logo = s.logo_bl.frames_per_direction();
    const auto fi = (n_logo == 0) ? 0u
        : std::uint32_t((elapsed_ms / kBaseFrameMs) % n_logo);
    constexpr int kLogoAnchorX = 400;
    constexpr int kLogoAnchorY = 120;
    blit_at_anchor    (fb, s.logo_bl.frame(0, fi), s.pal, kLogoAnchorX, kLogoAnchorY);
    blit_at_anchor    (fb, s.logo_br.frame(0, fi), s.pal, kLogoAnchorX, kLogoAnchorY);
    blit_additive  (fb, s.logo_fl.frame(0, fi), s.pal, &s.sky_pl2, kLogoAnchorX, kLogoAnchorY);
    blit_additive  (fb, s.logo_fr.frame(0, fi), s.pal, &s.sky_pl2, kLogoAnchorX, kLogoAnchorY);

    // Buttons — chrome frames from RE'd assets: 2-frame (normal/pressed)
    // for Short/Medium, 4-frame two-piece composite for Wide/Narrow. Hover
    // brightens the label to gold; no chrome-only hover frame exists.
    for (const auto& b : buttons) {
        if (b.chrome) {
            blit_button_chrome(fb, s.pal, *b.chrome, b.x, b.y,
                               b.hovered && b.pressed);
        }
        if (b.label && *b.label) {
            const int lw = s.font.measure(b.label);
            const int lh = s.font.line_height();
            const int lx = b.x + (b.w - lw) / 2;
            const int ly = b.y + (b.h - lh) / 2;
            if (b.hovered) {
                // Gold hover — matches the highlight D2 draws through a
                // PL2 text-colour shift.
                s.font.draw_tinted(fb, kW, kH, s.pal, lx, ly, b.label,
                                   255, 208, 80);
            } else {
                s.font.draw(fb, kW, kH, s.pal, lx, ly, b.label);
            }
        }
    }

    s.font.draw(fb, kW, kH, s.pal, 8, int(kH) - 14, "d2d dev build");
}

int charselect_max_scroll(int n) {
    return std::max(0, (n + 1) / 2 * 2 - kSlots);
}

std::string char_title(const d2d::d2s::Header& h) {
    const int p = h.progression;
    const bool lod = h.expansion();
    int tier = lod ? (p < 5 ? 0 : p < 10 ? 1 : p < 15 ? 2 : 3)
                   : (p < 4 ? 0 : p < 8 ? 1 : p < 12 ? 2 : 3);
    if (tier == 0) return {};
    if (h.hardcore()) tier += 3;
    const bool female = h.cls == 0 || h.cls == 1 || h.cls == 6;
    static constexpr const char* kClassic[2][7] = {
        { "", "Sir ",  "Lord ", "Baron ",    "Count ",    "Duke ",    "King "  },
        { "", "Dame ", "Lady ", "Baroness ", "Countess ", "Duchess ", "Queen " },
    };
    static constexpr const char* kLod[2][7] = {
        { "", "Slayer ", "Champion ", "Patriarch ", "Destroyer ", "Conqueror ", "Guardian " },
        { "", "Slayer ", "Champion ", "Matriarch ", "Destroyer ", "Conqueror ", "Guardian " },
    };
    return (lod ? kLod : kClassic)[female][tier];
}

int charselect_slot_at(int x, int y) {
    for (int i = 0; i < kSlots; ++i) {
        const int sx = kSlotX[i % 2], sy = kSlotY[i / 2];
        if (x >= sx && x < sx + kSlotW && y >= sy && y < sy + kSlotH) return i;
    }
    return -1;
}

void render_charselect(std::vector<std::uint8_t>& fb,
                       const Scene& s,
                       const CharSelectUI& ui,
                       std::uint32_t elapsed_ms) {
    const auto& pal = s.pal;   // char-select shares the Sky palette
    blit_dc6_grid(fb, s.charselect_bg, pal, 0, 0, s.bg_tiles_across);

    for (int i = 0; i < kSlots; ++i) {
        const int x = kSlotX[i % 2], y = kSlotY[i / 2];
        // LoD draws ONE frame: the gold charselectbox (record 0x96) moved
        // onto the selected slot while it's on screen (FUN_004390a0). The
        // grey box is classic-only (record 0x97, FUN_0043b080).
        // Two-frame composite: main 256-wide half + 16-wide sliver.
        const int si = ui.scroll + i;   // save index shown in this slot
        const auto& box = s.charselect_box;
        if (si == ui.selected && box.frames_per_direction() >= 2) {
            blit_sprite(fb, box.frame(0, 0), pal, x,       y);
            blit_sprite(fb, box.frame(0, 1), pal, x + 256, y);
        }
        if (si >= int(s.saves.size())) continue;
        // Text, per FUN_004380f0: lines go into the slot's text box (record
        // 0x84+i: 200x92, left margin 76, top margin 3), right of the
        // portrait. [title] name in red (hardcore) or gold, "Level N Class"
        // in white, then "EXPANSION CHARACTER" in green for LoD chars.
        const auto& h = s.saves[std::size_t(si)];
        // Portrait: the character's own composite (FUN_00438ad0 builds it
        // from the save's appearance bytes; FUN_004380f0 parks it at slot
        // x + 30, slot bottom - 13). Town-neutral, or NU for a living
        // LoD hardcore character. Direction 0 (FUN_005051a0(anim, 0)).
        // A dead hardcore one is the ghost: class 8 (female) / 9 (male),
        // both token RH in 0x72e050, and past class 6 the root is monsters:
        // monsters\RH, TN, one TR layer.
        if (h.hardcore() && h.died()) {
            static const Npc ghost = [] { Npc n; n.root = "monsters"; n.code = "RH"; n.base_w = "HTH"; n.comp[1] = "lit"; return n; }();
            draw_composite(fb, s.npc_anim(ghost, "TN"), pal, 0, elapsed_ms, x + 30, y + kSlotH - 1 - 13);
        } else {
            const bool nu = h.hardcore() && (h.expansion() || h.cls < 5);
            draw_composite(fb, s.composite(h.cls, nu ? kModeNU : kModeTN, h.look()),
                           pal, 0, elapsed_ms, x + 30, y + kSlotH - 1 - 13);
        }
        const int ci = kSaveClassToUi[h.cls];
        std::string cls = kClassKey[ci];
        if (auto v = lookup_string(s, kClassKey[ci])) cls = u16_to_latin1(*v);
        std::string level = "Level";
        if (auto v = lookup_string(s, std::uint16_t(0xfd9))) level = u16_to_latin1(*v);
        const std::string line1 = char_title(h) + h.name;
        const std::string line2 = level + " " + std::to_string(h.level) + " " + cls;
        const int tx = x + 76, lh = s.font.line_height();
        int ty = y + 3;
        const auto& name_rgb = h.hardcore() ? kTextRed : kTextGold;
        s.font.draw_tinted(fb, kW, kH, pal, tx, ty, line1.c_str(),
                           name_rgb[0], name_rgb[1], name_rgb[2]);
        s.font.draw(fb, kW, kH, pal, tx, ty += lh, line2.c_str());
        if (h.expansion()) {
            std::string exp = "EXPANSION CHARACTER";
            if (auto v = lookup_string(s, std::uint16_t(22731))) exp = u16_to_latin1(*v);
            s.font.draw_tinted(fb, kW, kH, pal, tx, ty += lh, exp.c_str(),
                               kTextGreen[0], kTextGreen[1], kTextGreen[2]);
        }
    }

    if (const int max = charselect_max_scroll(int(s.saves.size()));
        max > 0 && s.charselect_scroll.frames_per_direction() >= 5) {
        const auto& sb = s.charselect_scroll;
        blit_sprite(fb, sb.frame(0, 1), pal, kScrollX, kScrollDownTop);
        if (sb.frames_per_direction() >= 6)
            for (int n = 20, b = kScrollBottom - 1; n < kScrollH; n += 10)
                blit_sprite(fb, sb.frame(0, 5), pal, kScrollX, rec_top(b -= 10, kScrollArrow));
        blit_sprite(fb, sb.frame(0, 0), pal, kScrollX, kScrollUpTop);
        // Thumb: pos/max in rows (the widget counts rows, we store saves).
        const int thumb_bottom = (kScrollH - 30) * (ui.scroll / 2) / (max / 2)
                               - kScrollH + 19 + kScrollBottom;
        blit_sprite(fb, sb.frame(0, 4), pal, kScrollX, rec_top(thumb_bottom, kScrollArrow));
    }

    if (s.saves.empty()) {
        // Empty-list placeholder text — centred on the panel.
        constexpr const char* empty1 = "NO CHARACTERS YET";
        constexpr const char* empty2 = "click CREATE NEW CHARACTER to start";
        const int w1 = s.font.measure(empty1);
        const int w2 = s.font.measure(empty2);
        s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - w1/2, 260,
                           empty1, 255, 208, 80);
        s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - w2/2, 280,
                           empty2, 200, 200, 200);
    }

    // Buttons. TallButtonBlank is single-piece 168x60 (frames 0/1 for
    // normal/pressed); the medium OK/EXIT chrome uses blit_button_chrome.
    auto draw_tall = [&](const Button& b, bool disabled = false) {
        if (!b.chrome) return;
        const auto& fr = b.chrome->frame(0, b.hovered && b.pressed ? 1 : 0);
        blit_sprite(fb, fr, pal, b.x, b.y);
        const int lh = s.font.line_height();
        const int lines = b.label2 ? 2 : 1;
        int ly = b.y + (b.h - lines * lh) / 2;
        for (const char* t : { b.label, b.label2 }) {
            if (!t || !*t) continue;
            const int lx = b.x + (b.w - s.font.measure(t)) / 2;
            if (disabled) s.font.draw_tinted(fb, kW, kH, pal, lx, ly, t, 96, 96, 96);
            else          s.font.draw(fb, kW, kH, pal, lx, ly, t);
            ly += lh;
        }
    };
    draw_tall(ui.create_btn);
    // Convert-to-expansion only applies to a classic character (ponytail:
    // drawn, never actionable).
    const bool classic_pick = ui.selected >= 0 && ui.selected < int(s.saves.size())
                           && !s.saves[std::size_t(ui.selected)].expansion();
    draw_tall(ui.convert_btn, !classic_pick);
    draw_tall(ui.delete_btn);

    for (const Button* b : {&ui.cancel_btn, &ui.ok_btn}) {
        if (!b->chrome) continue;
        blit_button_chrome(fb, pal, *b->chrome, b->x, b->y,
                           b->hovered && b->pressed);
        if (b->label && *b->label) {
            const int lw = s.font.measure(b->label);
            const int lh = s.font.line_height();
            const int lx = b->x + (b->w - lw) / 2;
            const int ly = b->y + (b->h - lh) / 2;
            // OK greys out until a save is picked (do_switch is gated
            // on that each frame, before this draw).
            if (b == &ui.ok_btn && !b->do_switch)
                s.font.draw_tinted(fb, kW, kH, pal, lx, ly, b->label, 96, 96, 96);
            else
                s.font.draw(fb, kW, kH, pal, lx, ly, b->label);
        }
    }
}

void render_credits(std::vector<std::uint8_t>& fb,
                    const Scene& s,
                    std::uint32_t elapsed_ms) {
    // creditsbckgexpand.dc6 has the same 4×3 sub-frame grid as the title
    // background; both add up to exactly 800×600.
    blit_dc6_grid(fb, s.credits_bg, s.pal, 0, 0, s.bg_tiles_across);

    if (s.credits.empty()) {
        s.font.draw(fb, kW, kH, s.pal, 300, 300, "(no credits.txt found)");
    } else {
        // Line pitch: font16 line-height + 4 px spacing.
        const int pitch = s.font.line_height() + 6;
        const int total_h = int(s.credits.size()) * pitch;
        // scroll_y = distance the first line has moved above the bottom.
        // 40 ms per tick = D2 base; advance one px per tick.
        const int scroll = int(elapsed_ms / kBaseFrameMs);
        // total scroll cycle: total_h + kH (start at bottom, end past top).
        const int cycle = total_h + int(kH);
        const int off   = scroll % (cycle > 0 ? cycle : 1);
        // Base y for line 0.
        int y = int(kH) - off;
        // Gold-ish tint for section headers, approximating D2's PL2
        // hue-shift. Regular lines draw untinted (255,255,255 = pass-through).
        constexpr std::uint8_t kHdrR = 255, kHdrG = 208, kHdrB = 80;
        for (const auto& line : s.credits) {
            if (y > int(kH))          { y += pitch; continue; }
            if (y + pitch < 0)        { y += pitch; continue; }
            const bool header = !line.empty() && line.front() == '*';
            std::string_view text = line;
            if (header) text.remove_prefix(1);
            if (text.empty())         { y += pitch; continue; }
            const int lw = s.font.measure(text);
            const int x = int(kW) / 2 - lw / 2;
            if (header) {
                s.font.draw_tinted(fb, kW, kH, s.pal, x, y, text,
                                   kHdrR, kHdrG, kHdrB);
            } else {
                s.font.draw(fb, kW, kH, s.pal, x, y, text);
            }
            y += pitch;
        }
    }

    // Small hint at the bottom-left so anyone can find their way back.
    s.font.draw(fb, kW, kH, s.pal, 8, int(kH) - 14,
                "d2d dev build — click or Esc to return");
}

void advance_char_states(CharCreateUI& ui,
                         const Scene& s,
                         std::uint32_t elapsed_ms) {
    for (std::size_t i = 0; i < 7; ++i) {
        auto& cu = ui.classes[i];
        if (cu.state == ClassState::Selecting) {
            const auto& fw = s.class_anims[i][2];
            const auto n = fw.frames_per_direction();
            const auto elapsed = elapsed_ms - cu.state_start_ms;
            if (n == 0 || elapsed / kBaseFrameMs >= n) {
                cu.state = ClassState::Selected;
                cu.state_start_ms = elapsed_ms;
            }
        } else if (cu.state == ClassState::Deselecting) {
            const auto& bw = s.class_anims[i][4];
            const auto n = bw.frames_per_direction();
            const auto elapsed = elapsed_ms - cu.state_start_ms;
            if (n == 0 || elapsed / kBaseFrameMs >= n) {
                cu.state = ClassState::Idle;
                cu.state_start_ms = elapsed_ms;
            }
        }
    }
}

void handle_charcreate_click(CharCreateUI& ui,
                             const Mouse& m,
                             std::uint32_t elapsed_ms) {
    if (!m.release_this_frame) return;
    // Hardcore checkbox toggle — the click zone is the RE'd hitbox at
    // 0x70b080 (339, 561, 100, 32) unioned with the box chrome itself, so
    // clicking either the box OR its label toggles.
    const int hcRx = kHardcoreX, hcRw = kHardcoreW + 5 + 100;   // box + gap + label
    const int hcRy = rec_top(561, 32), hcRh = 32;              // 0x70b080's rows
    if (m.x >= hcRx && m.x < hcRx + hcRw &&
        m.y >= hcRy && m.y < hcRy + hcRh) {
        ui.hardcore = !ui.hardcore;
        return;
    }
    // Class hitboxes ARE the (x, y, w, h) rects from the RE'd records — 88×184
    // per class, position varies. D2 uses the same rects for hover + click
    // detection AND for sprite placement anchor.
    for (int i = 0; i < 7; ++i) {
        const auto p = kClassPos[i];
        if (m.x >= p.x && m.x < p.x + p.w && m.y >= p.y && m.y < p.y + p.h) {
            if (ui.selected == i) return;   // clicked selected class → no-op
            // Deselect old.
            if (ui.selected >= 0) {
                auto& prev = ui.classes[ui.selected];
                prev.state = ClassState::Deselecting;
                prev.state_start_ms = elapsed_ms;
            }
            auto& cur = ui.classes[i];
            cur.state = ClassState::Selecting;
            cur.state_start_ms = elapsed_ms;
            ui.selected = i;
            return;
        }
    }
}

void render_charcreate(std::vector<std::uint8_t>& fb,
                       const Scene& s,
                       const CharCreateUI& ui,
                       std::uint32_t elapsed_ms) {
    // fechar palette per FUN_00435580 RE.
    const auto& pal = s.charselect_pal;

    blit_dc6_grid(fb, s.charcreate_bg, pal, 0, 0, s.bg_tiles_across);

    // Campfire — RE record 0x70ae70: (x=345, y=470, w=110, h=127).
    // D2's drawer treats (x, y) as sprite origin (feet-of-flame); anchor
    // for BOTTOM-LEFT DC6 convention = (x + w/2, y + h). A shadow layer
    // exists at (345, 454) — same handle, 16 px higher — draw both.
    const auto nf = s.fire.frames_per_direction();
    if (nf > 0) {
        const auto ff = std::uint32_t(((elapsed_ms + 7) / kBaseFrameMs) % nf);
        // Shadow first, then main flame on top.
        blit_additive(fb, s.fire.frame(0, ff), pal, &s.fechar_pl2, 345 + 55, 454 + 127);
        blit_additive(fb, s.fire.frame(0, ff), pal, &s.fechar_pl2, 345 + 55, 470 + 127);
    }

    // Per-class draw: pick anim + frame based on state, place at the RE'd
    // rect's centre-bottom (matches D2's per-class positioning).
    for (std::size_t i = 0; i < 7; ++i) {
        const auto& cu = ui.classes[i];
        const auto p = kClassPos[i];
        int anim = 0;
        std::uint32_t elapsed_for_frame = elapsed_ms + std::uint32_t(i) * 7;
        bool one_shot = false;
        switch (cu.state) {
        case ClassState::Idle:        anim = 0; break;
        case ClassState::Selecting:   anim = 2; one_shot = true; break;
        case ClassState::Selected:    anim = 3; break;
        case ClassState::Deselecting: anim = 4; one_shot = true; break;
        }
        const auto& spr = s.class_anims[i][anim];
        const auto n = spr.frames_per_direction();
        if (n == 0) continue;
        std::uint32_t fi;
        if (one_shot) {
            const auto e = elapsed_ms - cu.state_start_ms;
            fi = std::min<std::uint32_t>(e / kBaseFrameMs, n - 1);
        } else {
            fi = std::uint32_t((elapsed_for_frame / kBaseFrameMs) % n);
        }
        // Anchor is the box's centre-bottom point; each frame's own DC6
        // offset places the actual pixels relative to that anchor.
        blit_at_anchor(fb, spr.frame(0, fi), pal, p.x + p.w / 2, p.y + p.h);
    }

    // Selected class name — big warm-gold caption above the panel area.
    // TBL-sourced ("Amazon" / "Sorceress" / etc. all live under those exact
    // keys in string.tbl per a probe of the file).
    std::string caption;
    if (ui.selected >= 0) {
        if (auto v = lookup_string(s, kClassKey[ui.selected]))
            caption = u16_to_latin1(*v);
        else
            caption = kClassKey[ui.selected];
    } else {
        caption = "SELECT HERO CLASS";
    }
    {
        const int w = s.font.measure(caption);
        s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - w/2, 22, caption,
                           255, 208, 80);
    }

    // Name-entry field — per RE record 0x70b290: (319, 519, 169, 26)
    // with textbox.dc6 chrome. Draw the chrome, then the typed name
    // centered inside. Prompt "Character Name" shows above the box.
    if (s.textbox.frames_per_direction() > 0) {
        // Above-box prompt (string.tbl id 0x1405 = "Character Name"; a
        // TBL scan of the 0x1380..0x1500 id band confirmed this).
        std::string np_owned;
        const char* nprompt = "CHARACTER NAME";
        if (auto v = lookup_string(s, std::uint16_t(0x1405))) {
            np_owned = u16_to_latin1(*v);
            if (!np_owned.empty()) nprompt = np_owned.c_str();
        }
        const int npw = s.font.measure(nprompt);
        constexpr int kNameTop = rec_top(519, 26);
        s.font.draw_tinted(fb, kW, kH, pal, 319 + (169 - npw)/2, kNameTop - 14,
                           nprompt, 200, 200, 200);
        blit_sprite(fb, s.textbox.frame(0, 0), pal, 319, kNameTop);
        // Typed name over the box.
        const int nw = s.font.measure(ui.input_name);
        const int nlh = s.font.line_height();
        s.font.draw_tinted(fb, kW, kH, pal,
                           319 + (169 - nw) / 2,
                           kNameTop + (26 - nlh) / 2,
                           ui.input_name, 255, 208, 80);
        // Simple blinking cursor after the last char (D2 uses a blinking
        // underline; we use a solid "|" for now).
        if (((elapsed_ms / 500) & 1) == 0) {
            s.font.draw_tinted(fb, kW, kH, pal,
                               319 + (169 - nw) / 2 + nw,
                               kNameTop + (26 - nlh) / 2,
                               "|", 255, 208, 80);
        }
    }

    // Hardcore checkbox — chrome from clickbox.dc6 (frame 0 unchecked,
    // 1 checked). Label rendered to the LEFT of the box in the pale
    // grey D2 uses for inactive-but-toggleable text; goes bright gold
    // when checked.
    if (s.clickbox.frames_per_direction() >= 2 && !ui.hardcore_label.empty()) {
        const auto& fr = s.clickbox.frame(0, ui.hardcore ? 1 : 0);
        blit_sprite(fb, fr, pal, kHardcoreX, kHardcoreY);
        // Label rendered right of the box — RE'd hitbox 0x70b080 sits at
        // x=339 (20px right of the box's x=319), covering the label's
        // click zone.
        const int lh = s.font.line_height();
        const int lx = kHardcoreX + kHardcoreW + 5;
        const int ly = kHardcoreY + (kHardcoreH - lh) / 2;
        if (ui.hardcore)
            s.font.draw_tinted(fb, kW, kH, pal, lx, ly,
                               ui.hardcore_label, 255, 208, 80);
        else
            s.font.draw_tinted(fb, kW, kH, pal, lx, ly,
                               ui.hardcore_label, 180, 180, 180);
    }

    // OK / EXIT buttons at the bottom — MediumSelButtonBlank chrome.
    // OK renders muted grey when disabled (no class picked yet or empty name).
    for (const Button* b : {&ui.cancel_btn, &ui.ok_btn}) {
        const bool disabled = (b == &ui.ok_btn) && !b->do_switch;
        if (!b->chrome) continue;
        blit_button_chrome(fb, pal, *b->chrome, b->x, b->y,
                           b->hovered && b->pressed);
        if (b->label && *b->label) {
            const int lw = s.font.measure(b->label);
            const int lh = s.font.line_height();
            const int lx = b->x + (b->w - lw) / 2;
            const int ly = b->y + (b->h - lh) / 2;
            if (disabled)
                s.font.draw_tinted(fb, kW, kH, pal, lx, ly, b->label, 96, 96, 96);
            else if (b->hovered)
                s.font.draw_tinted(fb, kW, kH, pal, lx, ly, b->label, 255,208,80);
            else
                s.font.draw(fb, kW, kH, pal, lx, ly, b->label);
        }
    }

    s.font.draw(fb, kW, kH, pal, 8, int(kH) - 14,
                "d2d dev build — pick class, type name, hit OK");
}

int cinematics_unlocked(const std::string& seen) {
    auto has = [&](const char* k) { return seen.find(k) != std::string::npos; };
    if (has("d2xout")) return 7;
    if (has("d2xintro")) return 6;
    if (has("act4end")) return 5;
    if (has("act4start")) return 4;
    if (has("act3start")) return 3;
    if (has("act2start")) return 2;
    return 1;
}

CinematicsUI cinematics_ui(const Scene& s, int unlocked) {
    CinematicsUI ui;
    ui.unlocked = unlocked;
    auto str = [&](std::uint16_t id, const char* fb) {
        const auto v = lookup_string(s, id);
        return v ? u16_to_latin1(*v) : std::string(fb);
    };
    static constexpr int kBottom[7] = { 181, 224, 268, 310, 353, 396, 439 };
    for (int i = 0; i < 7; ++i) {
        ui.labels[std::size_t(i)] = str(std::uint16_t(0x5525 + i), "?");
        ui.entry[std::size_t(i)] = Button{ 262, rec_top(kBottom[i], 35), 272, 35, nullptr, &s.btn_wide };
    }
    ui.cancel_label = str(0x13ef, "CANCEL");
    ui.cancel = Button{ 334, rec_top(488, 35), 128, 35, nullptr, &s.medium_button,
                        Screen::Title, true };
    ui.heading = str(0x13fa, "SELECT CINEMATICS");
    return ui;
}

void render_cinematics(std::vector<std::uint8_t>& fb, const Scene& s, const CinematicsUI& ui) {
    blit_dc6_grid(fb, s.bg, s.pal, 0, 0, s.bg_tiles_across);
    if (s.cinematics_panel.frames_per_direction() >= 4) {
        const auto& f0 = s.cinematics_panel.frame(0, 0);
        const int x = 237, y = rec_top(505, 427);
        blit_sprite(fb, f0, s.pal, x, y);
        blit_sprite(fb, s.cinematics_panel.frame(0, 1), s.pal, x + int(f0.width), y);
        blit_sprite(fb, s.cinematics_panel.frame(0, 2), s.pal, x, y + int(f0.height));
        blit_sprite(fb, s.cinematics_panel.frame(0, 3), s.pal, x + int(f0.width), y + int(f0.height));
    }
    // ponytail: heading in font16 centred in its text box; the text
    // control's own font/colour (FUN_004fc9b0) isn't pinned down.
    {
        const int w = s.font.measure(ui.heading);
        s.font.draw(fb, kW, kH, s.pal, 262 + (272 - w) / 2, rec_top(153, 35) + (35 - s.font.line_height()) / 2, ui.heading);
    }
    // Labels bind here: the UI is returned by value, so pointers into its
    // own strings can't live in the Buttons.
    auto draw = [&](Button b, const std::string& label, bool enabled) {
        b.label = label.c_str();
        if (b.chrome) blit_button_chrome(fb, s.pal, *b.chrome, b.x, b.y, enabled && b.hovered && b.pressed);
        const int lw = s.font.measure(b.label);
        const int lx = b.x + (b.w - lw) / 2, ly = b.y + (b.h - s.font.line_height()) / 2;
        if (!enabled)       s.font.draw_tinted(fb, kW, kH, s.pal, lx, ly, b.label, 105, 105, 105);
        else if (b.hovered) s.font.draw_tinted(fb, kW, kH, s.pal, lx, ly, b.label, 255, 208, 80);
        else                s.font.draw(fb, kW, kH, s.pal, lx, ly, b.label);
    };
    for (int i = 0; i < 7; ++i) draw(ui.entry[std::size_t(i)], ui.labels[std::size_t(i)], i < ui.unlocked);
    draw(ui.cancel, ui.cancel_label, true);
}

TitleUI title_ui(const Scene& s) {
    struct Spec {
        int x, y, w, h;
        std::uint16_t tbl_id;
        const char* fallback;
        const d2d::dc6::Sprite* chrome;
        bool do_switch = false;
        Screen goto_screen = Screen::Title;
        bool quit = false;
    };
    // ID map derived from menu records — see docs/research/re/frontend-menu-table.md.
    const Spec specs[] = {
        {264, 324, 272, 35, 0x13f2, "SINGLE PLAYER",     &s.btn_wide,
            true, Screen::CharSelect, false},
        {264, 366, 272, 35, 0x13f3, "BATTLE.NET",        &s.btn_wide2},
        {264, 391, 272, 25, 0,      "GATEWAY: LOCAL",    &s.btn_narrow},
        {264, 433, 272, 35, 0x13f4, "OTHER MULTIPLAYER", &s.btn_wide},
        {264, 528, 135, 25, 0x13f6, "CREDITS",           &s.btn_short,
            true, Screen::Credits, false},
        {402, 528, 135, 25, 0x13f7, "CINEMATICS",        &s.btn_short,
            true, Screen::Cinematics, false},
        {264, 568, 272, 35, 0x13f5, "EXIT DIABLO II",    &s.btn_wide,
            false, Screen::Title, true},
    };
    TitleUI ui;
    ui.labels.reserve(sizeof(specs) / sizeof(specs[0]));
    ui.buttons.reserve(ui.labels.capacity());
    for (const auto& sp : specs) {
        std::string label;
        if (sp.tbl_id) {
            if (auto v = lookup_string(s, sp.tbl_id)) label = u16_to_latin1(*v);
        }
        if (label.empty() && sp.fallback) label = sp.fallback;
        ui.labels.push_back(std::move(label));
        ui.buttons.push_back(Button{
            sp.x, rec_top(sp.y, sp.h), sp.w, sp.h,
            ui.labels.back().c_str(),
            sp.chrome, sp.goto_screen, sp.do_switch, sp.quit, false, false,
        });
    }
    return ui;
}


// window.hpp
void handle_sdl_events(SDL_Event& ev, Mouse& m, Screen& current_screen,
                       std::string& text_input, bool& text_backspace,
                       std::vector<SDL_Keycode>& keys, std::atomic<bool>& quit) {
    // SDL_EVENT_QUIT fires on app-level termination (Cmd-Q, all windows
    // closed). WINDOW_CLOSE_REQUESTED fires when a specific window's ✕
    // is clicked — SDL3 does NOT auto-promote it to QUIT. Both mean
    // "user wants out" for us since we're single-window.
    if (ev.type == SDL_EVENT_QUIT ||
        ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        quit = true; return;
    }
    // Cmd-Q backup — some window managers eat the SDL_EVENT_QUIT.
    if (ev.type == SDL_EVENT_KEY_DOWN &&
        ev.key.key == SDLK_Q && (ev.key.mod & SDL_KMOD_GUI)) {
        quit = true; return;
    }
    if (ev.type == SDL_EVENT_KEY_DOWN) {
        if (ev.key.key == SDLK_ESCAPE) {
            // Esc pops one layer up:
            //   Title       -> quit
            //   CharCreate  -> CharSelect  (the flow you came from)
            //   InGame      -> closes an open panel, else CharSelect (the
            //                  roster; matches D2) — handled in-game
            //   everything else -> Title
            switch (current_screen) {
                case Screen::Title:      quit = true; break;
                case Screen::CharCreate: current_screen = Screen::CharSelect; break;
                case Screen::InGame:     keys.push_back(ev.key.key); break;
                default:                 current_screen = Screen::Title; break;
            }
        } else if (ev.key.key == SDLK_BACKSPACE) {
            text_backspace = true;
        } else {
            keys.push_back(ev.key.key);   // per-screen key handling
        }
    } else if (ev.type == SDL_EVENT_TEXT_INPUT) {
        // ev.text.text is UTF-8; keep the printable Latin-1 subset.
        for (const char* p = ev.text.text; *p; ++p) {
            const auto c = static_cast<unsigned char>(*p);
            if (c >= 0x20 && c <= 0x7e) text_input.push_back(char(c));
        }
    } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
        m.x = int(ev.motion.x);
        m.y = int(ev.motion.y);
    } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        m.x = int(ev.button.x);
        m.y = int(ev.button.y);
        if (ev.button.button == SDL_BUTTON_LEFT) {
            m.down = true;
            m.press_this_frame = true;
        } else if (ev.button.button == SDL_BUTTON_RIGHT) {
            m.rpress_this_frame = true;
        }
    } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
        m.wheel += ev.wheel.integer_y;
    } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        m.x = int(ev.button.x);
        m.y = int(ev.button.y);
        if (ev.button.button == SDL_BUTTON_LEFT) {
            m.down = false;
            m.release_this_frame = true;
        }
    }
}


}  // namespace d2d::app
