// Menu screens: title, credits, char-select, char-create, cinematics.
#pragma once

#include "ingame.hpp"

namespace d2d::app {


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

// Character-select screen — RE FUN_004359d0 (init) + FUN_0042ef50 (BG draw).
// LoD layout: characterselectscreenEXP as BG, 2 columns × 4 rows of
// character slots; one gold charselectbox (272x93 assembled) marks the pick.
// Four buttons: CREATE / DELETE (TallButtonBlank, record bottom y=528) and
// OK / EXIT (MediumButtonBlank, bottom y=572). Char-create's OK/EXIT sit at
// the same spot but use MediumSelButtonBlank (handle 0x779744), which is
// authored for the fechar palette — on this Sky screen it speckles.
//
// Slots list <user dir>/save/*.d2s; OK enters InGame once one is picked.
// CREATE hops to CharCreate. DELETE is a no-op for now.
struct CharSelectUI {
    Button create_btn{};
    Button convert_btn{};
    Button delete_btn{};
    Button cancel_btn{};
    Button ok_btn{};
    std::string create_label, create_label2;
    std::string convert_label, convert_label2;
    std::string delete_label, delete_label2;
    std::string cancel_label;
    std::string ok_label;
    int selected = -1;           // index into Scene::saves, or -1
    int scroll = 0;              // index of the save in slot 0; always even
    std::uint32_t last_click_ms = 0;   // double-click = OK (FUN_0043a9d0)
};

// Largest valid CharSelectUI::scroll for `n` saves: last row at the bottom.
int charselect_max_scroll(int n) {
    return std::max(0, (n + 1) / 2 * 2 - kSlots);
}

// D2 font colours used on char-select (FUN_004fc9b0 colour arg).
constexpr std::uint8_t kTextRed[3]   = { 255, 77, 77 };    // 1
constexpr std::uint8_t kTextGreen[3] = { 0, 255, 0 };      // 2
constexpr std::uint8_t kTextGold[3]  = { 199, 179, 119 };  // 4

// Name prefix earned by beating difficulties — FUN_005068a0 picks a tier
// from the progression byte, FUN_00505640 the (hard-coded English) word.
// Tier: classic <4/<8/<12/else, LoD <5/<10/<15/else -> 0..3; hardcore
// shifts non-zero tiers by 3. Female: Amazon, Sorceress, Assassin.
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

// Visible slot index under (x, y), or -1. Row-major: slot i = row i/2, col i%2.
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

// Full-screen credits background + scrolling text. The scroll starts with
// the first line off the bottom of the screen and advances upward at ~1 px
// per D2 tick (25 Hz). When the last line clears the top, the scroll loops.
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

// Character-creation screen — the iconic seven-classes-around-a-campfire
// scene, loaded by FUN_004326f0. For MVP we blit each class's nu1 (idle)
// cycle at hardcoded positions matching the D2 layout, plus the fire
// animation in the pit. Selection / hover / class labels are follow-ups.

// Advance the per-class state machine — completes one-shot animations
// (Selecting → Selected, Deselecting → Idle) once they finish.
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

// Hit-test click position against class silhouettes and trigger selection
// transitions. Only one class is Selected/Selecting at a time; picking a
// new one first sends the previous into Deselecting.
// Hardcore-checkbox rect — RE'd char-create master table 0x70b0b0.
constexpr int kHardcoreX = 319, kHardcoreW = 15, kHardcoreH = 16;
constexpr int kHardcoreY = rec_top(560, kHardcoreH);

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

// Store labels alongside the buttons so we own the string memory through
// the frame. Called once at startup; string lookups happen only there.
struct TitleUI {
    std::vector<Button>     buttons;
    std::vector<std::string> labels;   // owns text buffers Button.label points into
};

// Build the main-menu button list. Positions from RE (LoD variant records
// 0x709010..0x7090a0 + shared bottom row 0x708f80..0x708fe0). Labels sourced
// from patchstring.tbl by ID (from the +0x18 field of each menu record) —
// falls back to a plausible English string when the TBL entry is missing.
// The LoD cinematics screen (FUN_00431600 -> FUN_004313d0): frontend
// records 8 (title background), 0x3d (CinematicsSelectionEXP panel,
// bottom-left (237, 505)), 0x3e (the "SELECT CINEMATICS" 0x13fa text box,
// (262, 153) 272x35), 0x3f..0x45 (WideButtons at x 262, bottoms 181..439,
// strings 0x5525..0x552b) and 0x46 (CANCEL 0x13ef, MediumButton at
// (334, 488)). The first `unlocked` entries work; the rest are disabled
// (FUN_004f96f0). Unlocking follows game.exe's "Aux Battle.net" bits.
struct CinematicsUI {
    std::array<Button, 7> entry{};
    std::array<std::string, 7> labels;
    Button cancel{};
    std::string cancel_label, heading;
    int unlocked = 1;
};
// The videos behind the seven entries, in order (handlers 0x434320..).
constexpr std::array<const char*, 7> kCinematicVideo = {
    R"(data\local\video\ENG\d2intro640x292.bik)",   R"(data\local\video\ENG\Act02start640x292.bik)",
    R"(data\local\video\ENG\Act03start640x292.bik)", R"(data\local\video\ENG\Act04start640x292.bik)",
    R"(data\local\video\ENG\Act04end640x292.bik)",   R"(data\local\video\ENG\D2x_Intro_640x292.bik)",
    R"(data\local\video\ENG\D2x_Out_640x292.bik)" };

// How many entries are unlocked, from what has been seen: game.exe's
// chain (FUN_00431600) over its registry bits — 0x01 LoD ending: 7,
// 0x80 LoD intro: 6, 0x10 act 4 end: 5, 0x08 act 4: 4, 0x40 act 3: 3,
// 0x04 act 2: 2, else 1. We keep the same facts in cinematics_seen.
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

}  // namespace d2d::app
