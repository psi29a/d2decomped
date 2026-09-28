// Definitions for frontend.hpp: title, character select, cinematics screens.
#include "frontend.hpp"

#include "common.hpp"
#include "scene.hpp"
#include "ui.hpp"
#include "world_view.hpp"

#include <d2s.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace d2d::client {

void render_title(std::vector<std::uint8_t>& framebuffer,
                  const Scene& scene,
                  std::span<const Button> buttons,
                  std::uint32_t elapsed_ms) {
    // Full-screen background — no need to clear; the 4×3 grid tiles fill
    // exactly 800×600 with no gaps.
    blit_dc6_grid(framebuffer, scene.background, scene.pal, 0, 0, scene.bg_tiles_across);

    // Diablo2.dc6 (menu record 0x708e00) is the CLASSIC-D2 static DIABLO II
    // logo — a 320×151 pre-baked title asset. In LoD it stays LOADED but
    // never drawn: the animated D2logo{Black,Fire}{L,R} pieces replace it,
    // and gameselectscreenEXP.dc6 already has "EXPANSION SET / Lord of
    // Destruction" baked into the background. Drawing both stacks two
    // "DIABLO II"s at different anchors — Bret caught the doubled letters.
    // RE: game.exe gates the classic-mode fallback on DAT_007795ec == 0
    // (the "expansion installed" flag; see char-create-table.md). We
    // never load classic-only, so `logo_static` is intentionally unused.
    (void)scene.logo_static;

    // "DIABLO II" animated logo halves — per RE records at 0x708e30 and
    // 0x708e60, both anchored at (400, 120). Each DC6 frame's bottom-left
    // origin (per DCC convention) places itself relative to that anchor.
    // Black silhouettes first, then fire fills on top with the warm-tint
    // hack (PL2 hue-variation colormap remains a follow-up).
    const auto n_logo = scene.logo_bl.frames_per_direction();
    const auto frame_index = (n_logo == 0) ? 0u
        : std::uint32_t((elapsed_ms / kBaseFrameMs) % n_logo);
    constexpr int kLogoAnchorX = 400;
    constexpr int kLogoAnchorY = 120;
    blit_at_anchor    (framebuffer, scene.logo_bl.frame(0, frame_index), scene.pal, kLogoAnchorX, kLogoAnchorY);
    blit_at_anchor    (framebuffer, scene.logo_br.frame(0, frame_index), scene.pal, kLogoAnchorX, kLogoAnchorY);
    blit_additive  (framebuffer, scene.logo_fl.frame(0, frame_index), scene.pal, &scene.sky_pl2, kLogoAnchorX, kLogoAnchorY);
    blit_additive  (framebuffer, scene.logo_fr.frame(0, frame_index), scene.pal, &scene.sky_pl2, kLogoAnchorX, kLogoAnchorY);

    // Buttons — chrome frames from RE'd assets: 2-frame (normal/pressed)
    // for Short/Medium, 4-frame two-piece composite for Wide/Narrow. Hover
    // brightens the label to gold; no chrome-only hover frame exists.
    for (const auto& button : buttons) {
        if (button.chrome) {
            blit_button_chrome(framebuffer, scene.pal, *button.chrome, button.x, button.y,
                               button.hovered && button.pressed);
        }
        if (button.label && *button.label) {
            const int label_width = scene.font.measure(button.label);
            const int line_height = scene.font.line_height();
            const int label_x = button.x + (button.width - label_width) / 2;
            const int label_y = button.y + (button.height - line_height) / 2;
            if (button.hovered) {
                // Gold hover — matches the highlight D2 draws through a
                // PL2 text-colour shift.
                scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, scene.pal, label_x, label_y, button.label,
                                   255, 208, 80);
            } else {
                scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, scene.pal, label_x, label_y, button.label);
            }
        }
    }

    scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, scene.pal, 8, int(kScreenHeight) - 14, "d2d dev build");
}

int charselect_max_scroll(int count) {
    return std::max(0, (count + 1) / 2 * 2 - kSlots);
}

std::string char_title(const d2d::d2s::Header& header) {
    const int progression = header.progression;
    const bool lod = header.expansion();
    int tier = lod ? (progression < 5 ? 0 : progression < 10 ? 1 : progression < 15 ? 2 : 3)
                   : (progression < 4 ? 0 : progression < 8 ? 1 : progression < 12 ? 2 : 3);
    if (tier == 0) return {};
    if (header.hardcore()) tier += 3;
    const bool female = header.cls == 0 || header.cls == 1 || header.cls == 6;
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
        const int slot_x = kSlotX[i % 2], slot_y = kSlotY[i / 2];
        if (x >= slot_x && x < slot_x + kSlotW && y >= slot_y && y < slot_y + kSlotH) return i;
    }
    return -1;
}

void render_charselect(std::vector<std::uint8_t>& framebuffer,
                       const Scene& scene,
                       const CharSelectUI& select_ui,
                       std::uint32_t elapsed_ms) {
    const auto& pal = scene.pal;   // char-select shares the Sky palette
    blit_dc6_grid(framebuffer, scene.charselect_bg, pal, 0, 0, scene.bg_tiles_across);

    for (int i = 0; i < kSlots; ++i) {
        const int x = kSlotX[i % 2], y = kSlotY[i / 2];
        // LoD draws ONE frame: the gold charselectbox (record 0x96) moved
        // onto the selected slot while it's on screen (FUN_004390a0). The
        // grey box is classic-only (record 0x97, FUN_0043b080).
        // Two-frame composite: main 256-wide half + 16-wide sliver.
        const int save_index = select_ui.scroll + i;   // save index shown in this slot
        const auto& box = scene.charselect_box;
        if (save_index == select_ui.selected && box.frames_per_direction() >= 2) {
            blit_sprite(framebuffer, box.frame(0, 0), pal, x,       y);
            blit_sprite(framebuffer, box.frame(0, 1), pal, x + 256, y);
        }
        if (save_index >= int(scene.saves.size())) continue;
        // Text, per FUN_004380f0: lines go into the slot's text box (record
        // 0x84+i: 200x92, left margin 76, top margin 3), right of the
        // portrait. [title] name in red (hardcore) or gold, "Level N Class"
        // in white, then "EXPANSION CHARACTER" in green for LoD chars.
        const auto& header = scene.saves[std::size_t(save_index)];
        // Portrait: the character's own composite (FUN_00438ad0 builds it
        // from the save's appearance bytes; FUN_004380f0 parks it at slot
        // x + 30, slot bottom - 13). Town-neutral, or NU for a living
        // LoD hardcore character. Direction 0 (FUN_005051a0(anim, 0)).
        // A dead hardcore one is the ghost: class 8 (female) / 9 (male),
        // both token RH in 0x72e050, and past class 6 the root is monsters:
        // monsters\RH, TN, one TR layer.
        if (header.hardcore() && header.died()) {
            static const Npc ghost = [] { Npc npc; npc.root = "monsters"; npc.code = "RH"; npc.base_w = "HTH"; npc.comp[1] = "lit"; return npc; }();
            draw_composite(framebuffer, scene.npc_anim(ghost, "TN"), pal, 0, elapsed_ms, x + 30, y + kSlotH - 1 - 13);
        } else {
            const bool is_ghost = header.hardcore() && (header.expansion() || header.cls < 5);
            draw_composite(framebuffer, scene.composite(header.cls, is_ghost ? kModeNU : kModeTN, header.look()),
                           pal, 0, elapsed_ms, x + 30, y + kSlotH - 1 - 13);
        }
        const int class_index = kSaveClassToUi[header.cls];
        std::string cls = kClassKey[class_index];
        if (auto found = lookup_string(scene, kClassKey[class_index])) cls = u16_to_latin1(*found);
        std::string level = "Level";
        if (auto found = lookup_string(scene, std::uint16_t(0xfd9))) level = u16_to_latin1(*found);
        const std::string line1 = char_title(header) + header.name;
        const std::string line2 = level + " " + std::to_string(header.level) + " " + cls;
        const int text_x = x + 76, line_height = scene.font.line_height();
        int text_y = y + 3;
        const auto& name_rgb = header.hardcore() ? kTextRed : kTextGold;
        scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, text_x, text_y, line1.c_str(),
                           name_rgb[0], name_rgb[1], name_rgb[2]);
        scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, text_x, text_y += line_height, line2.c_str());
        if (header.expansion()) {
            std::string exp = "EXPANSION CHARACTER";
            if (auto found = lookup_string(scene, std::uint16_t(22731))) exp = u16_to_latin1(*found);
            scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, text_x, text_y += line_height, exp.c_str(),
                               kTextGreen[0], kTextGreen[1], kTextGreen[2]);
        }
    }

    if (const int max = charselect_max_scroll(int(scene.saves.size()));
        max > 0 && scene.charselect_scroll.frames_per_direction() >= 5) {
        const auto& sprite = scene.charselect_scroll;
        blit_sprite(framebuffer, sprite.frame(0, 1), pal, kScrollX, kScrollDownTop);
        if (sprite.frames_per_direction() >= 6)
            for (int offset = 20, bottom = kScrollBottom - 1; offset < kScrollH; offset += 10)
                blit_sprite(framebuffer, sprite.frame(0, 5), pal, kScrollX, rec_top(bottom -= 10, kScrollArrow));
        blit_sprite(framebuffer, sprite.frame(0, 0), pal, kScrollX, kScrollUpTop);
        // Thumb: pos/max in rows (the widget counts rows, we store saves).
        const int thumb_bottom = (kScrollH - 30) * (select_ui.scroll / 2) / (max / 2)
                               - kScrollH + 19 + kScrollBottom;
        blit_sprite(framebuffer, sprite.frame(0, 4), pal, kScrollX, rec_top(thumb_bottom, kScrollArrow));
    }

    if (scene.saves.empty()) {
        // Empty-list placeholder text — centred on the panel.
        constexpr const char* empty1 = "NO CHARACTERS YET";
        constexpr const char* empty2 = "click CREATE NEW CHARACTER to start";
        const int width1 = scene.font.measure(empty1);
        const int width2 = scene.font.measure(empty2);
        scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, int(kScreenWidth)/2 - width1/2, 260,
                           empty1, 255, 208, 80);
        scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, int(kScreenWidth)/2 - width2/2, 280,
                           empty2, 200, 200, 200);
    }

    // Buttons. TallButtonBlank is single-piece 168x60 (frames 0/1 for
    // normal/pressed); the medium OK/EXIT chrome uses blit_button_chrome.
    auto draw_tall = [&](const Button& button, bool disabled = false) {
        if (!button.chrome) return;
        const auto& frame = button.chrome->frame(0, button.hovered && button.pressed ? 1 : 0);
        blit_sprite(framebuffer, frame, pal, button.x, button.y);
        const int line_height = scene.font.line_height();
        const int lines = button.label2 ? 2 : 1;
        int label_y = button.y + (button.height - lines * line_height) / 2;
        for (const char* text : { button.label, button.label2 }) {
            if (!text || !*text) continue;
            const int label_x = button.x + (button.width - scene.font.measure(text)) / 2;
            if (disabled) scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, label_x, label_y, text, 96, 96, 96);
            else          scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, label_x, label_y, text);
            label_y += line_height;
        }
    };
    draw_tall(select_ui.create_btn);
    // Convert-to-expansion only applies to a classic character (ponytail:
    // drawn, never actionable).
    const bool classic_pick = select_ui.selected >= 0 && select_ui.selected < int(scene.saves.size())
                           && !scene.saves[std::size_t(select_ui.selected)].expansion();
    draw_tall(select_ui.convert_btn, !classic_pick);
    draw_tall(select_ui.delete_btn);

    for (const Button* button : {&select_ui.cancel_btn, &select_ui.ok_btn}) {
        if (!button->chrome) continue;
        blit_button_chrome(framebuffer, pal, *button->chrome, button->x, button->y,
                           button->hovered && button->pressed);
        if (button->label && *button->label) {
            const int label_width = scene.font.measure(button->label);
            const int line_height = scene.font.line_height();
            const int label_x = button->x + (button->width - label_width) / 2;
            const int label_y = button->y + (button->height - line_height) / 2;
            // OK greys out until a save is picked (do_switch is gated
            // on that each frame, before this draw).
            if (button == &select_ui.ok_btn && !button->do_switch)
                scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, label_x, label_y, button->label, 96, 96, 96);
            else
                scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, label_x, label_y, button->label);
        }
    }
}

void render_credits(std::vector<std::uint8_t>& framebuffer,
                    const Scene& scene,
                    std::uint32_t elapsed_ms) {
    // creditsbckgexpand.dc6 has the same 4×3 sub-frame grid as the title
    // background; both add up to exactly 800×600.
    blit_dc6_grid(framebuffer, scene.credits_bg, scene.pal, 0, 0, scene.bg_tiles_across);

    if (scene.credits.empty()) {
        scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, scene.pal, 300, 300, "(no credits.txt found)");
    } else {
        // Line pitch: font16 line-height + 4 px spacing.
        const int pitch = scene.font.line_height() + 6;
        const int total_h = int(scene.credits.size()) * pitch;
        // scroll_y = distance the first line has moved above the bottom.
        // 40 ms per tick = D2 base; advance one px per tick.
        const int scroll = int(elapsed_ms / kBaseFrameMs);
        // total scroll cycle: total_h + kH (start at bottom, end past top).
        const int cycle = total_h + int(kScreenHeight);
        const int off   = scroll % (cycle > 0 ? cycle : 1);
        // Base y for line 0.
        int y = int(kScreenHeight) - off;
        // Gold-ish tint for section headers, approximating D2's PL2
        // hue-shift. Regular lines draw untinted (255,255,255 = pass-through).
        constexpr std::uint8_t kHdrR = 255, kHdrG = 208, kHdrB = 80;
        for (const auto& line : scene.credits) {
            if (y > int(kScreenHeight))          { y += pitch; continue; }
            if (y + pitch < 0)        { y += pitch; continue; }
            const bool header = !line.empty() && line.front() == '*';
            std::string_view text = line;
            if (header) text.remove_prefix(1);
            if (text.empty())         { y += pitch; continue; }
            const int line_width = scene.font.measure(text);
            const int x = int(kScreenWidth) / 2 - line_width / 2;
            if (header) {
                scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, scene.pal, x, y, text,
                                   kHdrR, kHdrG, kHdrB);
            } else {
                scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, scene.pal, x, y, text);
            }
            y += pitch;
        }
    }

    // Small hint at the bottom-left so anyone can find their way back.
    scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, scene.pal, 8, int(kScreenHeight) - 14,
                "d2d dev build — click or Esc to return");
}

void advance_char_states(CharCreateUI& create_ui,
                         const Scene& scene,
                         std::uint32_t elapsed_ms) {
    for (std::size_t i = 0; i < 7; ++i) {
        auto& class_ui = create_ui.classes[i];
        if (class_ui.state == ClassState::Selecting) {
            const auto& forward = scene.class_anims[i][2];
            const auto frames = forward.frames_per_direction();
            const auto elapsed = elapsed_ms - class_ui.state_start_ms;
            if (frames == 0 || elapsed / kBaseFrameMs >= frames) {
                class_ui.state = ClassState::Selected;
                class_ui.state_start_ms = elapsed_ms;
            }
        } else if (class_ui.state == ClassState::Deselecting) {
            const auto& backward = scene.class_anims[i][4];
            const auto frames = backward.frames_per_direction();
            const auto elapsed = elapsed_ms - class_ui.state_start_ms;
            if (frames == 0 || elapsed / kBaseFrameMs >= frames) {
                class_ui.state = ClassState::Idle;
                class_ui.state_start_ms = elapsed_ms;
            }
        }
    }
}

void handle_charcreate_click(CharCreateUI& create_ui,
                             const Mouse& mouse,
                             std::uint32_t elapsed_ms) {
    if (!mouse.release_this_frame) return;
    // Hardcore checkbox toggle — the click zone is the RE'd hitbox at
    // 0x70b080 (339, 561, 100, 32) unioned with the box chrome itself, so
    // clicking either the box OR its label toggles.
    const int hcRx = kHardcoreX, hcRw = kHardcoreW + 5 + 100;   // box + gap + label
    const int hcRy = rec_top(561, 32), hcRh = 32;              // 0x70b080's rows
    if (mouse.x >= hcRx && mouse.x < hcRx + hcRw &&
        mouse.y >= hcRy && mouse.y < hcRy + hcRh) {
        create_ui.hardcore = !create_ui.hardcore;
        return;
    }
    // Class hitboxes ARE the (x, y, w, h) rects from the RE'd records — 88×184
    // per class, position varies. D2 uses the same rects for hover + click
    // detection AND for sprite placement anchor.
    for (int i = 0; i < 7; ++i) {
        const auto position = kClassPos[i];
        if (mouse.x >= position.x && mouse.x < position.x + position.width && mouse.y >= position.y && mouse.y < position.y + position.height) {
            if (create_ui.selected == i) return;   // clicked selected class → no-op
            // Deselect old.
            if (create_ui.selected >= 0) {
                auto& prev = create_ui.classes[create_ui.selected];
                prev.state = ClassState::Deselecting;
                prev.state_start_ms = elapsed_ms;
            }
            auto& cur = create_ui.classes[i];
            cur.state = ClassState::Selecting;
            cur.state_start_ms = elapsed_ms;
            create_ui.selected = i;
            return;
        }
    }
}

void render_charcreate(std::vector<std::uint8_t>& framebuffer,
                       const Scene& scene,
                       const CharCreateUI& create_ui,
                       std::uint32_t elapsed_ms) {
    // fechar palette per FUN_00435580 RE.
    const auto& pal = scene.charselect_pal;

    blit_dc6_grid(framebuffer, scene.charcreate_bg, pal, 0, 0, scene.bg_tiles_across);

    // Campfire — RE record 0x70ae70: (x=345, y=470, w=110, h=127).
    // D2's drawer treats (x, y) as sprite origin (feet-of-flame); anchor
    // for BOTTOM-LEFT DC6 convention = (x + w/2, y + h). A shadow layer
    // exists at (345, 454) — same handle, 16 px higher — draw both.
    const auto fire_frames = scene.fire.frames_per_direction();
    if (fire_frames > 0) {
        const auto fire_frame = std::uint32_t(((elapsed_ms + 7) / kBaseFrameMs) % fire_frames);
        // Shadow first, then main flame on top.
        blit_additive(framebuffer, scene.fire.frame(0, fire_frame), pal, &scene.fechar_pl2, 345 + 55, 454 + 127);
        blit_additive(framebuffer, scene.fire.frame(0, fire_frame), pal, &scene.fechar_pl2, 345 + 55, 470 + 127);
    }

    // Per-class draw: pick anim + frame based on state, place at the RE'd
    // rect's centre-bottom (matches D2's per-class positioning).
    for (std::size_t i = 0; i < 7; ++i) {
        const auto& class_ui = create_ui.classes[i];
        const auto position = kClassPos[i];
        int anim = 0;
        std::uint32_t elapsed_for_frame = elapsed_ms + std::uint32_t(i) * 7;
        bool one_shot = false;
        switch (class_ui.state) {
        case ClassState::Idle:        anim = 0; break;
        case ClassState::Selecting:   anim = 2; one_shot = true; break;
        case ClassState::Selected:    anim = 3; break;
        case ClassState::Deselecting: anim = 4; one_shot = true; break;
        }
        const auto& spr = scene.class_anims[i][anim];
        const auto frames = spr.frames_per_direction();
        if (frames == 0) continue;
        std::uint32_t frame_index;
        if (one_shot) {
            const auto elapsed = elapsed_ms - class_ui.state_start_ms;
            frame_index = std::min<std::uint32_t>(elapsed / kBaseFrameMs, frames - 1);
        } else {
            frame_index = std::uint32_t((elapsed_for_frame / kBaseFrameMs) % frames);
        }
        // Anchor is the box's centre-bottom point; each frame's own DC6
        // offset places the actual pixels relative to that anchor.
        blit_at_anchor(framebuffer, spr.frame(0, frame_index), pal, position.x + position.width / 2, position.y + position.height);
    }

    // Selected class name — big warm-gold caption above the panel area.
    // TBL-sourced ("Amazon" / "Sorceress" / etc. all live under those exact
    // keys in string.tbl per a probe of the file).
    std::string caption;
    if (create_ui.selected >= 0) {
        if (auto found = lookup_string(scene, kClassKey[create_ui.selected]))
            caption = u16_to_latin1(*found);
        else
            caption = kClassKey[create_ui.selected];
    } else {
        caption = "SELECT HERO CLASS";
    }
    {
        const int width = scene.font.measure(caption);
        scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, int(kScreenWidth)/2 - width/2, 22, caption,
                           255, 208, 80);
    }

    // Name-entry field — per RE record 0x70b290: (319, 519, 169, 26)
    // with textbox.dc6 chrome. Draw the chrome, then the typed name
    // centered inside. Prompt "Character Name" shows above the box.
    if (scene.textbox.frames_per_direction() > 0) {
        // Above-box prompt (string.tbl id 0x1405 = "Character Name"; a
        // TBL scan of the 0x1380..0x1500 id band confirmed this).
        std::string np_owned;
        const char* nprompt = "CHARACTER NAME";
        if (auto found = lookup_string(scene, std::uint16_t(0x1405))) {
            np_owned = u16_to_latin1(*found);
            if (!np_owned.empty()) nprompt = np_owned.c_str();
        }
        const int npw = scene.font.measure(nprompt);
        constexpr int kNameTop = rec_top(519, 26);
        scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, 319 + (169 - npw)/2, kNameTop - 14,
                           nprompt, 200, 200, 200);
        blit_sprite(framebuffer, scene.textbox.frame(0, 0), pal, 319, kNameTop);
        // Typed name over the box.
        const int name_width = scene.font.measure(create_ui.name);
        const int nlh = scene.font.line_height();
        scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal,
                           319 + (169 - name_width) / 2,
                           kNameTop + (26 - nlh) / 2,
                           create_ui.name, 255, 208, 80);
        // Simple blinking cursor after the last char (D2 uses a blinking
        // underline; we use a solid "|" for now).
        if (((elapsed_ms / 500) & 1) == 0) {
            scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal,
                               319 + (169 - name_width) / 2 + name_width,
                               kNameTop + (26 - nlh) / 2,
                               "|", 255, 208, 80);
        }
    }

    // Hardcore checkbox — chrome from clickbox.dc6 (frame 0 unchecked,
    // 1 checked). Label rendered to the LEFT of the box in the pale
    // grey D2 uses for inactive-but-toggleable text; goes bright gold
    // when checked.
    if (scene.clickbox.frames_per_direction() >= 2 && !create_ui.hardcore_label.empty()) {
        const auto& frame = scene.clickbox.frame(0, create_ui.hardcore ? 1 : 0);
        blit_sprite(framebuffer, frame, pal, kHardcoreX, kHardcoreY);
        // Label rendered right of the box — RE'd hitbox 0x70b080 sits at
        // x=339 (20px right of the box's x=319), covering the label's
        // click zone.
        const int line_height = scene.font.line_height();
        const int label_x = kHardcoreX + kHardcoreW + 5;
        const int label_y = kHardcoreY + (kHardcoreH - line_height) / 2;
        if (create_ui.hardcore)
            scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, label_x, label_y,
                               create_ui.hardcore_label, 255, 208, 80);
        else
            scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, label_x, label_y,
                               create_ui.hardcore_label, 180, 180, 180);
    }

    // OK / EXIT buttons at the bottom — MediumSelButtonBlank chrome.
    // OK renders muted grey when disabled (no class picked yet or empty name).
    for (const Button* button : {&create_ui.cancel_btn, &create_ui.ok_btn}) {
        const bool disabled = (button == &create_ui.ok_btn) && !button->do_switch;
        if (!button->chrome) continue;
        blit_button_chrome(framebuffer, pal, *button->chrome, button->x, button->y,
                           button->hovered && button->pressed);
        if (button->label && *button->label) {
            const int label_width = scene.font.measure(button->label);
            const int line_height = scene.font.line_height();
            const int label_x = button->x + (button->width - label_width) / 2;
            const int label_y = button->y + (button->height - line_height) / 2;
            if (disabled)
                scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, label_x, label_y, button->label, 96, 96, 96);
            else if (button->hovered)
                scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, label_x, label_y, button->label, 255,208,80);
            else
                scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, label_x, label_y, button->label);
        }
    }

    scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, 8, int(kScreenHeight) - 14,
                "d2d dev build — pick class, type name, hit OK");
}

int cinematics_unlocked(const std::string& seen) {
    auto has = [&](const char* key) { return seen.find(key) != std::string::npos; };
    if (has("d2xout")) return 7;
    if (has("d2xintro")) return 6;
    if (has("act4end")) return 5;
    if (has("act4start")) return 4;
    if (has("act3start")) return 3;
    if (has("act2start")) return 2;
    return 1;
}

CinematicsUI cinematics_ui(const Scene& scene, int unlocked) {
    CinematicsUI cinematics;
    cinematics.unlocked = unlocked;
    auto str = [&](std::uint16_t id, const char* fallback) {
        const auto found = lookup_string(scene, id);
        return found ? u16_to_latin1(*found) : std::string(fallback);
    };
    static constexpr int kBottom[7] = { 181, 224, 268, 310, 353, 396, 439 };
    for (int i = 0; i < 7; ++i) {
        cinematics.labels[std::size_t(i)] = str(std::uint16_t(0x5525 + i), "?");
        cinematics.entry[std::size_t(i)] = Button{ 262, rec_top(kBottom[i], 35), 272, 35, nullptr, &scene.btn_wide };
    }
    cinematics.cancel_label = str(0x13ef, "CANCEL");
    cinematics.cancel = Button{ 334, rec_top(488, 35), 128, 35, nullptr, &scene.medium_button,
                        Screen::Title, true };
    cinematics.heading = str(0x13fa, "SELECT CINEMATICS");
    return cinematics;
}

void render_cinematics(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const CinematicsUI& cinematics) {
    blit_dc6_grid(framebuffer, scene.background, scene.pal, 0, 0, scene.bg_tiles_across);
    if (scene.cinematics_panel.frames_per_direction() >= 4) {
        const auto& panel = scene.cinematics_panel.frame(0, 0);
        const int x = 237, y = rec_top(505, 427);
        blit_sprite(framebuffer, panel, scene.pal, x, y);
        blit_sprite(framebuffer, scene.cinematics_panel.frame(0, 1), scene.pal, x + int(panel.width), y);
        blit_sprite(framebuffer, scene.cinematics_panel.frame(0, 2), scene.pal, x, y + int(panel.height));
        blit_sprite(framebuffer, scene.cinematics_panel.frame(0, 3), scene.pal, x + int(panel.width), y + int(panel.height));
    }
    // ponytail: heading in font16 centred in its text box; the text
    // control's own font/colour (FUN_004fc9b0) isn't pinned down.
    {
        const int width = scene.font.measure(cinematics.heading);
        scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, scene.pal, 262 + (272 - width) / 2, rec_top(153, 35) + (35 - scene.font.line_height()) / 2, cinematics.heading);
    }
    // Labels bind here: the UI is returned by value, so pointers into its
    // own strings can't live in the Buttons.
    auto draw = [&](Button button, const std::string& label, bool enabled) {
        button.label = label.c_str();
        if (button.chrome) blit_button_chrome(framebuffer, scene.pal, *button.chrome, button.x, button.y, enabled && button.hovered && button.pressed);
        const int label_width = scene.font.measure(button.label);
        const int label_x = button.x + (button.width - label_width) / 2, label_y = button.y + (button.height - scene.font.line_height()) / 2;
        if (!enabled)       scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, scene.pal, label_x, label_y, button.label, 105, 105, 105);
        else if (button.hovered) scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, scene.pal, label_x, label_y, button.label, 255, 208, 80);
        else                scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, scene.pal, label_x, label_y, button.label);
    };
    for (int i = 0; i < 7; ++i) draw(cinematics.entry[std::size_t(i)], cinematics.labels[std::size_t(i)], i < cinematics.unlocked);
    draw(cinematics.cancel, cinematics.cancel_label, true);
}

TitleUI title_ui(const Scene& scene) {
    struct Spec {
        int x, y, width, height;
        std::uint16_t tbl_id;
        const char* fallback;
        const d2d::dc6::Sprite* chrome;
        bool do_switch = false;
        Screen goto_screen = Screen::Title;
        bool quit = false;
    };
    // ID map derived from menu records — see docs/research/re/frontend-menu-table.md.
    const Spec specs[] = {
        {264, 324, 272, 35, 0x13f2, "SINGLE PLAYER",     &scene.btn_wide,
            true, Screen::CharSelect, false},
        {264, 366, 272, 35, 0x13f3, "BATTLE.NET",        &scene.btn_wide2},
        {264, 391, 272, 25, 0,      "GATEWAY: LOCAL",    &scene.btn_narrow},
        {264, 433, 272, 35, 0x13f4, "OTHER MULTIPLAYER", &scene.btn_wide},
        {264, 528, 135, 25, 0x13f6, "CREDITS",           &scene.btn_short,
            true, Screen::Credits, false},
        {402, 528, 135, 25, 0x13f7, "CINEMATICS",        &scene.btn_short,
            true, Screen::Cinematics, false},
        {264, 568, 272, 35, 0x13f5, "EXIT DIABLO II",    &scene.btn_wide,
            false, Screen::Title, true},
    };
    TitleUI title;
    title.labels.reserve(sizeof(specs) / sizeof(specs[0]));
    title.buttons.reserve(title.labels.capacity());
    for (const auto& spec : specs) {
        std::string label;
        if (spec.tbl_id) {
            if (auto found = lookup_string(scene, spec.tbl_id)) label = u16_to_latin1(*found);
        }
        if (label.empty() && spec.fallback) label = spec.fallback;
        title.labels.push_back(std::move(label));
        title.buttons.push_back(Button{
            spec.x, rec_top(spec.y, spec.height), spec.width, spec.height,
            title.labels.back().c_str(),
            spec.chrome, spec.goto_screen, spec.do_switch, spec.quit, false, false,
        });
    }
    return title;
}

}  // namespace d2d::client
