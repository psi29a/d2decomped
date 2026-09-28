// Scene: GameData (gamedata.hpp) plus what the client draws and plays.
#pragma once

#include "common.hpp"
#include "gamedata.hpp"

namespace d2d::app {
// All the client draws and plays: GameData, plus the pixels, fonts and
// palettes. The frontend's come from FUN_0042e6d0 (main-menu loader) —
// see docs/research/re/frontend-menu-table.md.
struct Scene : GameData {
    d2d::palette::Palette pal;                // Sky — title/credits palette
    d2d::palette::Palette charselect_pal;     // fechar — char-select/create palette
    d2d::palette::Pl2     sky_pl2;            // Sky PL2 (title logo additive)
    d2d::palette::Pl2     fechar_pl2;         // fechar PL2 (campfire additive)
    d2d::dc6::Sprite      bg;                 // TitleScreen or gameselectscreenEXP
    d2d::dc6::Sprite      logo_static;        // Diablo2.dc6 — 320×151, classic only
    d2d::dc6::Sprite      logo_bl, logo_br;   // D2logoBlack{Left,Right} — silhouettes
    d2d::dc6::Sprite      logo_fl, logo_fr;   // D2logoFire{Left,Right} — animated fire
    d2d::dc6::Sprite      btn_wide;           // WideButtonBlank
    d2d::dc6::Sprite      btn_wide2;          // WideButtonBlank02
    d2d::dc6::Sprite      btn_narrow;         // NarrowButtonBlank
    d2d::dc6::Sprite      btn_short;          // ShortButtonBlank
    d2d::dc6::Sprite      credits_bg;         // creditsbckgexpand.dc6 (or classic fallback)
    // Character-creation screen (loaded by FUN_004326f0). SP button hops here.
    d2d::dc6::Sprite      charcreate_bg;      // charactercreationscreenEXP.dc6
    d2d::dc6::Sprite      fire;               // fire.DC6 — campfire between the classes
    d2d::dc6::Sprite      medium_button;      // MediumButtonBlank.dc6 — char-select OK/EXIT (handle 0x77973c, Sky palette)
    d2d::dc6::Sprite      cinematics_panel;   // FrontEnd\CinematicsSelectionEXP.dc6 (2x2, 326x427)
    d2d::dc6::Sprite      medium_sel_button;  // MediumSelButtonBlank.dc6 — char-create OK/EXIT chrome (per FUN_004326f0)
    d2d::dc6::Sprite      textbox;            // textbox.dc6 — name-entry chrome (single 169×26 frame)
    d2d::dc6::Sprite      clickbox;           // clickbox.dc6 — Hardcore checkbox chrome (2 frames × 15×16, unchecked/checked)
    // Character-select screen assets (RE FUN_004359d0, handle 0x00779734).
    // Slot chrome is 2-frame 256+16 wide × 93 tall (matches WideButton
    // composite pattern). BG is 12-frame 4×3 grid of ≤256×256 tiles.
    d2d::dc6::Sprite      charselect_bg;      // characterselectscreenEXP.dc6
    d2d::dc6::Sprite      charselect_box;     // charselectbox.dc6 (filled slot)
    d2d::dc6::Sprite      charselect_scroll;  // FrontEnd\joingamescrollbars.dc6
    d2d::dc6::Sprite      tall_button;        // TallButtonBlank.dc6 (168×60) — CREATE / DELETE
    d2d::dc6::Sprite      cursor;             // CURSOR\ohand.dc6 — D2's gauntlet, 8 frames
    // Character composites (in-game player, char-select portraits). The
    // COF names the body-part layers and their per-frame draw order; each
    // layer is its own DCC, indexed by COF composite type (0 HD, 1 TR,
    // 2 LG, 3 RA, 4 LA, 5 RH, 6 LH, 7 SH, 8.. S1..S8). What each layer
    // wears comes from 16 appearance bytes — a .d2s header's, or a new
    // character's starting gear — through `comp` (components/compcode).
    struct PlayerAnim : AnimTiming {
        d2d::cof::Cof                     cof;
        // Each layer's DCC file, decoded when it's first drawn: the World
        // reads only the COF and timings (a mode's length, its hit frame).
        mutable std::array<std::vector<std::byte>, 16> dcc;
        mutable std::array<d2d::dcc::Sprite, 16>       decoded;
        std::array<const std::uint8_t*, 16>            tint{};   // each layer's colormap (256), null: none
        [[nodiscard]] const d2d::dcc::Sprite& layer(std::size_t t) const {
            if (!dcc[t].empty()) {
                try { decoded[t] = d2d::dcc::Sprite(dcc[t]); if (tint[t]) decoded[t].remap(tint[t]); }
                catch (const std::exception& e) { d2d::log::warn("{} layer {}: {}", name, t, e.what()); }
                dcc[t] = {};
            }
            return decoded[t];
        }
    };
    // Loaded on first use and kept — decoding every composite up front
    // doubled startup. `mutable` so the const Scene renderers can fill it.
    mutable std::map<std::array<std::uint8_t, 34>, PlayerAnim> composites;
    // Class animations — 7 classes × 5 states, per the RE'd class table at
    // 0x00708a00. State order matches D2's suffix scheme: nu1, nu2, fw,
    // nu3, bw. Class order (rows in the table): assassin, druid, amazon,
    // necromancer, barbarian, sorceress, paladin — but we store them in
    // our left-to-right visual order (barb/necro/pally/ama/sorc/druid/assn)
    // to match Scene::class positions.
    std::array<std::array<d2d::dc6::Sprite, 5>, 7> class_anims;
    d2d::font::Font       font;
    d2d::font::Font       font_small;         // font8 — long panel values
    d2d::font::Font       font_tiny;          // font6 — panel labels
    // Credits.txt / ExpansionCredits.txt parsed to plain Latin-1 lines.
    // A '*' prefix on a line marks a section header in D2's format.
    std::vector<std::string> credits;
    // Character saves from <user dir>/save/*.d2s, most recently played first,
    // with each save's items (empty if they couldn't be parsed).
    std::vector<d2d::d2s::Header> saves;
    std::vector<std::vector<d2d::d2s::Item>> save_items;
    std::vector<d2d::d2s::Stats> save_stats;
    std::vector<std::vector<d2d::d2s::Item>> save_corpses;   // each save's corpse list (its items), empty: none
    mutable std::unordered_map<std::string, std::optional<d2d::dc6::Sprite>> item_sprites;
    mutable std::unordered_map<std::string, std::optional<d2d::dc6::Sprite>> flippy_sprites;
    const d2d::dc6::Sprite* flippy(const std::string& code) const;   // an item code's ground animation
    // An item's inventory graphic: the unique's/set item's own invfile,
    // else the picture variant (ItemTypes InvGfx<n>), else the base's.
    const d2d::dc6::Sprite* item_sprite(const d2d::d2s::Item& it) const;
    // The item's colormap (256 entries), or null (FUN_0062c100): inv, the
    // inventory's (InvTrans, invtransform); else the character's and the
    // ground's (Transform, chrtransform).
    [[nodiscard]] const std::uint8_t* item_map(const d2d::d2s::Item& it, bool inv) const {
        const auto p = item_pieces.find(it.code);
        if (p == item_pieces.end()) return nullptr;
        const int t = inv ? p->second.inv_transform : p->second.transform;
        if (!d2d::compcode::tints_with(t) || colormaps[std::size_t(t)].size() < 21 * 256) return nullptr;
        const int c = item_colours.of(it.quality, it.unique_id, it.set_id, it.prefix, it.suffix, it.affixes, it.class_affix,
                                      it.socketed && !it.socketed_items.empty() ? it.socketed_items[0].code : std::string{}, inv);
        return c < 0 || c >= 21 ? nullptr : colormaps[std::size_t(t)].data() + c * 256;
    }
    // pal through a colormap (null: pal itself).
    [[nodiscard]] static d2d::palette::Palette mapped(const d2d::palette::Palette& base, const std::uint8_t* map) {
        if (!map) return base;
        std::array<d2d::palette::Rgba, 256> e;
        for (std::size_t i = 0; i < 256; ++i) e[i] = base[i == 0 ? 0 : map[i]];
        return d2d::palette::Palette(e);
    }
    [[nodiscard]] d2d::palette::Palette item_pal(const d2d::d2s::Item& it, const d2d::palette::Palette& base) const {
        return mapped(base, item_map(it, true));
    }
    std::vector<std::string> unique_inv, set_inv;             // invfile, rows as item_names
    d2d::dc6::Sprite popbelt;                          // PANEL\ctrlpnl_popbelt
    std::unordered_map<std::string, std::array<std::string, 6>> type_invgfx;
    std::array<d2d::dc6::Sprite, 2> stash_panel;
    d2d::dc6::Sprite cube_panel;
    d2d::dc6::Sprite inv_panel;                       // PANEL\invchar6.dc6
    // Char panel stat buttons (docs/research/re/char-panel.md): PANEL\level
    // (frame 1 pressed) on PANEL\levelsocket, the PANEL\skillpoints box.
    d2d::dc6::Sprite level_button, level_socket, points_box;
    // Skill tree (docs/research/re/skill-tree.md): SPELLS\skltree_<c>_back
    // (frames 0..3 the panel, 4t..4t+3 tab t on top) and <Cl>Skillicon.
    std::array<d2d::dc6::Sprite, 7> skill_tree_bg, skill_icons;   // by d2s class
    d2d::dc6::Sprite generic_skill_icons;              // SPELLS\Skillicon: Attack and the other non-class skills
    // The quest log's art (FUN_004a23d0 / FUN_004a3220): questbackground,
    // expquesttabs, questsockets, and each quest's icon (a1q1 .. a5q6).
    d2d::dc6::Sprite quest_last;                        // ui\menu\questlast: replay the quest's message
    d2d::dc6::Sprite quest_bg, quest_tabs, quest_sockets;
    std::array<d2d::dc6::Sprite, 27> quest_icons;
    d2d::dc6::Sprite ctrl_panel, globes, globe_glass; // 800ctrlpnl7 / hlthmana / overlap
    int                   bg_tiles_across{4};

    // ACT1 palette — the actual town palette (fechar/sky are frontend-only).
    d2d::palette::Palette                    act1_pal;
    std::array<d2d::palette::Palette, 32>    act1_lit;   // act1_pal at each light level (PL2 +0x400)
    d2d::dc6::Sprite focus16;                          // UI\CURSOR\focus16: menu hover marks
    d2d::font::Font  font_formal11;                    // FontFormal11: NPC speech (font id 8)
    d2d::font::Font  font30;                           // Font30 (font id 2): the death screen's lines
    d2d::dc6::Sprite you_died, you_died_inst;          // UI\ENG\youdiedhardcore, youdiedinst (FUN_00453100)
    // Automap: AutoMap.txt resolved like FUN_0061fcf0 — LevelName through
    // game.exe's level-type names (0x6e7d50: "None", "1 Town", ...),
    // TileName through its orientation names (0x6e7f90: fl wl wr wtlr
    // wtll wtr wbl wbr wld wrd wle wre co sh tr rf ld rd fd fi), Style =
    // main index, Start/EndSequence = sub-index range, the valid Cel1..4
    // (FUN_0061fff0 picks one at random). Act 1 draws MaxiMap.dc6.
    struct AutomapRule { int level_type = 0, orientation = 0, main = -1, sub0 = -1, sub1 = -1; std::vector<int> cels; };
    d2d::dc6::Sprite store_panel, store_tabs, store_buttons;   // PANEL\buysell, buyselltabs, buysellbtn
    d2d::dc6::Sprite gold_coin;                                   // PANEL\goldcoinbtn
    d2d::dc6::Sprite wp_bg, wp_icons;
    std::array<d2d::dc6::Sprite, 2> wp_tabs;                      // [expansion]: waygatetabs / expwaygatetabs
    d2d::dc6::Sprite automap_cels;                     // UI\AutoMap\MaxiMap.dc6
    std::unordered_map<std::string, d2d::dcc::Sprite> missile_cels;   // a missile's CelFile DCC, by Missiles.txt row name
    std::array<d2d::dc6::Sprite, 4> rain_splash;       // UncompOverlays\Rain1..4 (Rain3 / 4 splash where drops fall)
    d2d::dcc::Sprite npc_alert;                        // Overlay.txt npcalert: NPCSpeechBalloon.dcc
    mutable std::unordered_map<const OverlayInfo*, d2d::dcc::Sprite> overlay_sprites;   // loaded when first drawn
    // A monster's colour (FUN_00477530): 2..7 its graphic's
    // COF\palshift.dat table, 8..29 RandTransforms.dat's table - 8; else none.
    mutable std::unordered_map<std::string, std::vector<std::uint8_t>> palshifts;   // by monster code, loaded when first drawn
    std::vector<std::uint8_t> rand_transforms;         // Monsters\RandTransforms.dat: 30 x 256
    const std::uint8_t* monster_map(const Npc& n) const;
    const d2d::dcc::Sprite* overlay_sprite(const OverlayInfo& o) const;
    mutable std::map<std::string, PlayerAnim> npc_anims;   // by root/code/mode/components
    const PlayerAnim& npc_anim(const Npc& n, std::string_view mode) const;
    const PlayerAnim& composite(int d2s_class, int mode, const Appearance& gfx) const;
    std::vector<AutomapRule> automap_rules;

};

}  // namespace d2d::app
