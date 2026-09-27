// Frontend widgets: screen enum, buttons, mouse, NPC menus + speech, char-create state.
#pragma once

#include "scene.hpp"

namespace {

// --- Screen state machine + mouse routing ---------------------------------

enum class Screen { Title, Credits, CharSelect, CharCreate, InGame, Video, Cinematics };

// Per-class animation state on the char-create screen. Matches D2's flow:
// classes idle in place (nu1); on click the "just clicked" class walks
// forward one time (fw) and then stands in the selected pose (nu3 loop);
// clicking a different class or Cancel walks the current selected back
// (bw once) before returning to idle.
enum class ClassState : std::uint8_t { Idle, Selecting, Selected, Deselecting };

struct ClassUI {
    ClassState    state = ClassState::Idle;
    std::uint32_t state_start_ms = 0;
};

// Class placement records — sourced from RE'd char-create menu table at
// 0x70aed0..0x70b470 (each entry is one kind=3 record from the master
// table). (x, y, w, h) is the bounding-box rect the D2 drawer uses to
// position the class sprite and to hit-test clicks. Sprite renders so
// its logical origin (feet-centre) lands at (x + w/2, y + h), which
// combined with each frame's own DC6 offset positions the actual pixels.
// Order matches Scene::class_anims (BA, NE, PA, AM, SO, DZ, AS — our
// left-to-right visual order). Individual records source addresses:
//   AM 0x70b050  NE 0x70afc0  AS 0x70b440  BA 0x70af30
//   PA 0x70b020  SO 0x70aff0  DZ 0x70b470
struct ClassPos { int x, y, w, h; };
constexpr ClassPos kClassPos[7] = {
    {400, 330, 88, 184},   // Barbarian
    {217, 360, 88, 184},   // Necromancer
    {521, 339, 88, 184},   // Paladin
    {100, 337, 88, 184},   // Amazon
    {626, 353, 88, 184},   // Sorceress
    {720, 370, 88, 184},   // Druid
    {232, 364, 88, 184},   // Assassin
};
// Fallbacks used when string.tbl can't resolve a class-name key. Order
// matches Scene::class_anims (BA, NE, PA, AM, SO, DZ, AS).
constexpr const char* kClassKey[7] = {
    "Barbarian", "Necromancer", "Paladin", "Amazon",
    "Sorceress", "Druid",       "Assassin",
};

// .d2s class id (AM SO NE PA BA DZ AS) -> our visual-order index
// (BA NE PA AM SO DZ AS, see kClassKey).
constexpr int kSaveClassToUi[7] = { 3, 4, 1, 2, 0, 5, 6 };
constexpr int kUiToSaveClass[7] = { 4, 2, 3, 0, 1, 5, 6 };

// D2's frontend records store (x, y, w, h) with y = the BOTTOM row
// (bottom-left anchor, like its DC6 blits): the full-screen BG record is
// (0, 599, 800, 600). Our blits and hit tests are top-left, so convert.
constexpr int rec_top(int y_bottom, int h) { return y_bottom - h + 1; }

// Char-select slot grid, 2 columns x 4 rows — RE'd from the LoD
// char-select init FUN_0043ae30 / drawer FUN_004380f0: 8 visible slots
// (DAT_0070cc0c), column x alternates 0x25/0x135, row bottom y =
// 0xb2 + 0x5d*row — so tops at 86..365, filling the panel's 4x93 interior.
// Each 272x93 slot is a 200x92 text box (records 0x84..0x8b) + a 72x93
// portrait cell on its right (0x8c..0x93). See docs/research/re/char-select.md.
constexpr int kSlotW = 272, kSlotH = 93;
constexpr int kSlotX[2] = { 37, 309 };
constexpr int kSlotY[4] = { rec_top(178, kSlotH), rec_top(271, kSlotH),
                            rec_top(364, kSlotH), rec_top(457, kSlotH) };
constexpr int kSlots = 8;
// Scrollbar: record 0xa7 is a text box with flag 4 ("has scrollbar"),
// whose child scrollbar (kind 5, FUN_005084f0) takes its geometry from
// {585, 457, 363} at 0x708d00: x, bottom y, height. Art is
// joingamescrollbars.dc6 (f0/f2 up, f1/f3 down normal/pressed, f4 thumb,
// f5 track; 12x14 each). Shown only when saves > 8; one step = one row =
// 2 saves (callback 0x439df0). Draw math from FUN_00508370, bottom-left
// anchored like every D2 cel: down arrow at bottom y-1, track tiles every
// 10 px above it, up arrow at bottom (y-h)+9, thumb at bottom
// (h-30)*pos/max - h + 19 + y.
constexpr int kScrollX = 585, kScrollBottom = 457, kScrollH = 363, kScrollArrow = 14;
constexpr int kScrollUpTop   = rec_top(kScrollBottom - kScrollH + 9, kScrollArrow);  // 90
constexpr int kScrollDownTop = rec_top(kScrollBottom - 1, kScrollArrow);             // 443

struct Button {
    int x{}, y{}, w{}, h{};
    const char*             label   = nullptr;
    const d2d::dc6::Sprite* chrome  = nullptr;
    // Action: set BOTH goto_screen (screen switch) OR quit (exit). Neither
    // means "no-op for now" — used for the Battle.net / Multiplayer buttons.
    Screen                  goto_screen = Screen::Title;
    bool                    do_switch   = false;
    bool                    quit        = false;
    // Transient per-frame state, updated from mouse events.
    bool                    hovered = false;
    bool                    pressed = false;
    // Second label line (char-select's tall buttons: FUN_00500bf0 adds
    // one under the record's own label). Last so positional inits hold.
    const char*             label2  = nullptr;
};

struct Mouse {
    int  x = 0, y = 0;
    bool down = false;                 // current button state
    bool press_this_frame = false;     // rising edge
    bool release_this_frame = false;   // falling edge
    bool rpress_this_frame = false;    // right button, rising edge
    int  wheel = 0;                    // wheel notches this frame, +up
};

// Composite the chrome for one button. D2's chrome DC6s come in two shapes
// (verified against the actual assets):
//   * 2 frames — single-piece, full width. f=0 normal, f=1 pressed.
//     (ShortButtonBlank 135×25, MediumButtonBlank 128×35.)
//   * 4 frames — two-piece: `wide + sliver`, drawn side-by-side. f=0 is
//     the 256×h left piece, f=1 is the 16×h right sliver. f=2 + f=3 are
//     the pressed variants of the same split.
//     (WideButtonBlank / WideButtonBlank02 / NarrowButtonBlank.)
// There is NO hover-only frame — D2 signals hover by brightening the LABEL
// instead. Callers pick the label tint separately.
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

// Update hover/pressed state and, on a mouse-up over a hovered+pressed
// button, invoke the action. Returns true if any action was taken so the
// caller can early-out.
// Is item type `t` (or an Equiv ancestor) `want`?
bool type_is(const GameData& s, const std::string& t, std::string_view want) {
    return d2d::rules::type_is(s.rules, t, want);
}

// What a filled socket adds to `parent`: a jewel's own properties, or the
// gem/rune's gems.txt bonus for the parent's kind (weapon, shield, else
// helm/armour).
std::vector<d2d::d2s::ItemProp> socket_props(const GameData& s, const d2d::d2s::Item& parent,
                                             const d2d::d2s::Item& filled) {
    auto out = filled.props;
    const auto g = s.gem_props.find(filled.code);
    const auto info = s.rules.item_info.find(parent.code);
    if (g == s.gem_props.end() || info == s.rules.item_info.end()) return out;
    const int k = info->second.kind == 2 ? 0 : type_is(s, info->second.type, "shld") ? 2 : 1;
    const auto& add = g->second[std::size_t(k)];
    out.insert(out.end(), add.begin(), add.end());
    return out;
}

// The char panel's computed values (stat 30 next level, 31 defence,
// resistances 39/43/41/45), as FUN_004a7d00 shows them. From the save's
// base stats and gear.
// ponytail: equipped slots 1..10 (the primary weapon set), socket
// bonuses and charms, the passives with no weapon type (Iron Skin's
// defense %, Natural Resistance); no set bonuses or auras.
struct PanelStats {
    std::int64_t next = -1, defense = 0;
    std::array<std::int64_t, 4> res{};           // fire, cold, lightning, poison
};

PanelStats panel_stats(const GameData& s, const d2d::d2s::Header& h,
                       const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Stats& st,
                       const std::vector<d2d::rules::PassiveStat>* passives = nullptr) {
    PanelStats p;
    const auto lvl = st.get(d2d::d2s::kLevel);
    if (lvl >= 0 && std::size_t(lvl) + 1 < s.exp_next.size()) p.next = s.exp_next[std::size_t(lvl)];
    std::array<std::int64_t, 64> sum{};
    auto add = [&](const std::vector<d2d::d2s::ItemProp>& props) {
        for (const auto& pr : props) if (pr.stat >= 0 && pr.stat < 64) sum[std::size_t(pr.stat)] += pr.value;
    };
    std::int64_t item_def = 0, per_level = 0;
    for (const auto& it : items) {
        const bool worn = it.location == 1 && it.slot >= 1 && it.slot <= 10;
        const bool charm = it.location == 0 && it.panel == 1
                        && (it.code == "cm1" || it.code == "cm2" || it.code == "cm3");
        if (!worn && !charm) continue;
        add(it.props);
        for (const auto& j : it.socketed_items) add(socket_props(s, it, j));
        std::int64_t ed = 0;
        for (const auto& pr : it.props) {
            if (pr.stat == 16) ed += pr.value;            // item_armor_percent: this item's base
            if (pr.stat == 214) per_level += pr.value;    // item_armor_perlevel, 1/8 per level
        }
        if (it.defense > 0) item_def += it.defense * (100 + ed) / 100;
    }
    std::int64_t skill_def = 0;                           // 171 skill_armor_percent
    if (passives) for (const auto& ps : *passives) {
        if (!ps.itype.empty()) continue;
        if (ps.stat >= 0 && ps.stat < 64) sum[std::size_t(ps.stat)] += ps.value;
        if (ps.stat == 171) skill_def += ps.value;
    }
    p.defense = item_def + sum[31] + per_level * lvl / 8 + st.get(d2d::d2s::kDex) / 4;
    p.defense += p.defense * skill_def / 100;
    const int diff = h.active_difficulty();
    const std::int64_t penalty = h.expansion() ? s.resist_penalty[std::size_t(diff)]
                                               : std::array<std::int64_t, 3>{ 0, -20, -50 }[std::size_t(diff)];
    constexpr int kRes[4] = { 39, 43, 41, 45 };
    for (int i = 0; i < 4; ++i) {
        const auto cap = std::min<std::int64_t>(75 + sum[std::size_t(kRes[i] + 1)], 95);
        p.res[std::size_t(i)] = std::clamp<std::int64_t>(sum[std::size_t(kRes[i])] + penalty, -100, cap);
    }
    return p;
}

// An open NPC menu, as game.exe builds and lays it out (FUN_004b4830,
// FUN_004b85f0, FUN_004b8410; docs/research/re/npc-menu.md): a gold
// font16 header with the NPC's name (21 px line), the npc_menu.hpp
// entries and "cancel" (string 0x102e), 15 px lines, white. Box: widest
// line + 20 by the line heights + 15, placed at (cx - w/2, cy - h/3) where
// (cx, cy) is the NPC's feet on screen raised 150 px (FUN_004b1c80), then
// kept inside the screen.
struct NpcMenuState {
    int npc = -1;                            // Level::npcs index, -1 = closed
    // What choosing a line does. ponytail: trade/hire/gamble/... just close.
    enum Action { kClose, kTalk, kIntro, kGossip, kTrade, kGamble, kHire, kIdentify, kHireOffer };
    struct Line { std::string text; int height = 15, width = 0, x = 0; bool header = false; Action action = kClose; int arg = -1; };
    std::vector<Line> lines;
    int x = 0, y = 0, w = 0, h = 0;
    // Index of the selectable line under (mx, my), or -1.
    [[nodiscard]] int line_at(int mx, int my) const {
        if (npc < 0 || mx < x || mx >= x + w) return -1;
        int base = y;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            base += lines[i].height;
            if (!lines[i].header && my > base - lines[i].height && my <= base) return int(i);
        }
        return -1;
    }
};

std::string string_id(const GameData& s, std::uint16_t id) {
    const auto v = lookup_string(s, id);
    return v ? u16_to_latin1(*v) : std::string{};
}

void layout_npc_menu(const Scene& s, NpcMenuState& m, int screen_x, int screen_y);

// clvl and the unidentified item count adjust the table like game.exe:
// Kashya gains "hire" above level 7 (FUN_004b66b0 -> FUN_004b6410
// patches her record to talk, hire); "identify items" only shows when
// something needs it (FUN_004b4830).
NpcMenuState open_npc_menu(const Scene& s, const Level& L, int npc, int screen_x, int screen_y, int clvl = 1, int unidentified = 0) {
    NpcMenuState m;
    const auto& n = L.npcs[std::size_t(npc)];
    const auto it = std::ranges::find_if(kNpcMenus, [&](const NpcMenu& e) { return e.hc_idx == n.hc_idx; });
    if (it == kNpcMenus.end()) return m;
    m.npc = npc;
    m.lines.push_back({ n.name, 21, 0, 0, true });
    auto entries = it->entries;
    if (n.hc_idx == 150 && clvl > 7) entries = { 0xd35, 0xd45 };
    for (const auto id : entries)
        if (id && !(id == 0xfb4 && unidentified == 0))
            m.lines.push_back({ string_id(s, id), 15, 0, 0, false,
                                id == 0xd35 ? NpcMenuState::kTalk
                                : id == 0xd44 || id == 0xd06 ? NpcMenuState::kTrade
                                : id == 0xd46 ? NpcMenuState::kGamble
                                : id == 0xd45 ? NpcMenuState::kHire
                                : id == 0xfb4 ? NpcMenuState::kIdentify : NpcMenuState::kClose });
    m.lines.push_back({ string_id(s, 0x102e), 15 });
    layout_npc_menu(s, m, screen_x, screen_y);
    return m;
}

// The talk submenu (FUN_004b5890): header "talk" (gold), "introduction"
// unless the NPC's talk record says no_intro, "gossip", then "cancel"
// (0xd48). ponytail: no quest topics (FUN_0049f900) or Greiz/Cain extras.
NpcMenuState open_talk_menu(const Scene& s, const Level& L, int npc, int screen_x, int screen_y) {
    NpcMenuState m;
    const auto& n = L.npcs[std::size_t(npc)];
    const auto t = std::ranges::find_if(kNpcTalk, [&](const NpcTalk& e) { return e.hc_idx == n.hc_idx; });
    m.npc = npc;
    m.lines.push_back({ string_id(s, 0xd35), 21, 0, 0, true });
    if (t != kNpcTalk.end() && !t->topics.empty()) {
        if (!t->no_intro) m.lines.push_back({ string_id(s, 0xd47), 15, 0, 0, false, NpcMenuState::kIntro });
        m.lines.push_back({ string_id(s, 0xd43), 15, 0, 0, false, NpcMenuState::kGossip });
    }
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

// An NPC's speech topic for the player's class. Introduction
// (FUN_004b41e0): topic 1 when its class is the player's, else topic 0.
// Gossip (FUN_004b1680): a random topic >= 2 whose class is 7 (any) or the
// player's and, if quest-gated, whose quest state matches — up to 10
// tries, else topic 2. game.exe picks it once per game per NPC; so do we.
// Quest states are the save's flags for the difficulty it was played on.
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

// NPC speech (FUN_004a10e0 / FUN_004a05e0 / the draw at 0x49d5a0): the
// string's line 0 is the scroll rate (8 if not a number), the rest is
// pre-wrapped. A half-dark 325x122 box at ((W-325)/2, 12-5); FontFormal11
// lines at x+16, 18 px apart, entering at the bottom (baseline top+112)
// and rising by (ms/4)*rate/1024 px. Done once the offset passes
// (lines-1)*18 + 112. ponytail: whole lines clipped to the box instead of
// FUN_00501df0's partial-line reveal; no voice.
struct Speech {
    int npc = -1;
    std::vector<std::string> lines;
    int rate = 8;
    std::uint32_t start_ms = 0;
    std::uint16_t string = 0;            // string.tbl id
    int voice = 0;                       // Sounds.txt index, 0 = not started, -1 = none
    [[nodiscard]] int offset_px(std::uint32_t ms) const { return int(std::uint64_t(ms - start_ms) / 4 * std::uint64_t(rate) >> 10); }
    [[nodiscard]] bool done(std::uint32_t ms) const {
        return offset_px(ms) > std::max(int(lines.size()) - 1, 1) * 18 + 0x70;
    }
};

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

// FUN_004b8100: a black box at draw mode 1 (half transparent), each line's
// text with its baseline at the box top + the heights so far + its own;
// the hovered entry gets focus16 (frame = draw count % 7) bottom-anchored
// at (x - 24, baseline + 4) and (x + width + 2, baseline + 4).
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

struct CharCreateUI {
    std::array<ClassUI, 7> classes{};
    int selected = -1;           // index of currently-selected class or -1
    // Gear the in-game character wears: a loaded save's appearance bytes,
    // or unset for a fresh character (starting gear).
    std::optional<std::array<std::uint8_t, 16>> appearance;
    std::vector<d2d::d2s::Item> items;   // a loaded save's items
    d2d::d2s::Stats stats;               // ... and attributes
    PanelStats panel;                    // ... and what the char panel computes
    bool expansion = true;               // the save's expansion flag (stash size)
    d2d::d2s::Header header;             // the loaded save's header (quest flags ...)
    Button ok_btn{};
    Button cancel_btn{};
    // Name entry — SDL text-input feeds this buffer, capped at 15 chars
    // to match D2's char-name limit (per D2's actual character record
    // struct). Left/right arrows and non-printable keys are ignored.
    std::string input_name;
    // Hardcore checkbox — the char-create master-table record at 0x70b0b0
    // (kind=6 button, x=319, y=560, w=15, h=16, handle=DAT_007797c0
    // (clickbox.dc6), on_click=FUN_00430730 which sets bit 0x04 of the
    // character-struct flags word at [0x7795d4]+0x1ef — that's the D2S
    // "Character Status" hardcore bit). Label from patchstring.tbl id
    // 0x1406 ("Hardcore"). See docs/research/re/char-create-table.md for
    // the full 33-record breakdown, including the Ladder (bit 0x40) and
    // Expansion (bit 0x20) checkbox records also present in the table.
    bool hardcore = false;
    // Owned label buffers so Button.label pointers stay live for the
    // frame; sourced from string.tbl by ID.
    std::string ok_label;
    std::string cancel_label;
    std::string hardcore_label;
    // Selected-class name from string.tbl (u16 → Latin-1). Empty when
    // no class is picked yet.
    std::string selected_name;
};

// Set by run_windowed: D2Win buttons (control type 6) play
// sfx\cursor\button.wav when pressed (FUN_00501090).
std::function<void()> g_on_button_press;

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

// Parse D2's UTF-16LE-with-BOM credits.txt into Latin-1 lines. The file's
// section headers use a '*' prefix. Skips empty lines but keeps '*' lines
// as-is (renderer decides whether to style them).
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

}  // namespace
