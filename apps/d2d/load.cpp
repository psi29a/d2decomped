// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for load.hpp: the Scene: GameData (gamedata_load.hpp) plus sprites, fonts, palettes, saves.
#include "load.hpp"

#include "common.hpp"
#include "gamemenu.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <compcode.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <dc6.hpp>
#include <dcc.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <log.hpp>
#include <mpq.hpp>
#include <palette.hpp>
#include <rules.hpp>
#include <txt.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::client {

Scene::PlayerAnim load_composite(const d2d::mpq::Stack& mpqs,
                                 const std::vector<d2d::compcode::Entry>& comp,
                                 const std::array<std::vector<std::uint8_t>, 9>& colormaps,
                                 int cls, int mode, const Scene::Appearance& gfx) {
    Scene::PlayerAnim out;
    const char* class_code = kCharCode[cls];
    auto cof = player_cof(mpqs, comp, cls, mode, gfx);      // the COF and timing (gamedata.cpp)
    if (!cof.ok) return out;
    char path[256] = "";
    try {
        out.cof = std::move(cof.cof);
        static_cast<GameData::AnimTiming&>(out) = std::move(cof.timing);
        for (const auto& layer : out.cof.layer_defs()) {
            if (layer.type >= 16) continue;
            const auto component = gfx[layer.type];
            std::string code = (component != 0 && component != 0xff && component < comp.size()) ? comp[component].code : "";
            if (code.empty()) {
                if (layer.type >= 5 && layer.type <= 7) continue;   // empty hand / no shield
                code = "lit";
            }
            std::string weapon_class = layer.weapon_class;
            for (auto* text : { &code, &weapon_class }) for (auto& letter : *text) letter = char(std::toupper(letter));
            std::snprintf(path, sizeof(path), R"(data\global\CHARS\%s\%s\%s%s%s%s%s.dcc)",
                          class_code, kLayerCode[layer.type], class_code, kLayerCode[layer.type], code.c_str(),
                          kModeCode[mode], weapon_class.c_str());
            if (d2d::compcode::Tint tint; d2d::compcode::tint_of(gfx[16 + layer.type], tint) && colormaps[std::size_t(tint.transform)].size() >= 21 * 256)
                out.tint[layer.type] = colormaps[std::size_t(tint.transform)].data() + tint.colour * 256;
            if (auto bytes = mpqs.try_read(path)) out.dcc[layer.type] = std::move(*bytes);
            else if (code != "LIT") {                     // no such piece in this mode (death has only LIT's): the lit one
                std::snprintf(path, sizeof(path), R"(data\global\CHARS\%s\%s\%s%sLIT%s%s.dcc)",
                              class_code, kLayerCode[layer.type], class_code, kLayerCode[layer.type], kModeCode[mode], weapon_class.c_str());
                if (auto piece_bytes = mpqs.try_read(path)) out.dcc[layer.type] = std::move(*piece_bytes);
            }
        }
    } catch (const std::exception& error) {
        d2d::log::warn("{}: {}", path, error.what());
    }
    return out;
}

Scene::PlayerAnim load_npc_composite(const d2d::mpq::Stack& mpqs, const Npc& npc,
                                     const std::string& mode) {
    Scene::PlayerAnim out;
    auto cof = npc_cof(mpqs, npc, mode);                      // the COF and timing (gamedata.cpp)
    if (!cof.ok) return out;
    char path[256] = "";
    try {
        out.cof = std::move(cof.cof);
        static_cast<GameData::AnimTiming&>(out) = std::move(cof.timing);
        for (const auto& layer : out.cof.layer_defs()) {
            if (layer.type >= 16) continue;
            std::string comp = npc.comp[layer.type].empty() ? "lit" : npc.comp[layer.type];
            std::string weapon_class = layer.weapon_class;
            for (auto* text : { &comp, &weapon_class }) for (auto& letter : *text) letter = char(std::toupper(letter));
            std::snprintf(path, sizeof(path), R"(data\global\%s\%s\%s\%s%s%s%s%s.dcc)",
                          npc.root.c_str(), npc.code.c_str(), kLayerCode[layer.type], npc.code.c_str(),
                          kLayerCode[layer.type], comp.c_str(), mode.c_str(), weapon_class.c_str());
            if (auto bytes = mpqs.try_read(path)) { out.dcc[layer.type] = std::move(*bytes); continue; }
            // Some objects' parts are DC6 (the hellgate's): its frames as the DCC's, anchored at their offsets.
            std::strcpy(path + std::strlen(path) - 3, "dc6");
            if (auto bytes = mpqs.try_read(path)) out.decoded[layer.type] = dc6_as_dcc(*bytes);
        }
    } catch (const std::exception& error) {
        d2d::log::warn("{}: {}", path, error.what());
    }
    return out;
}

namespace {

void load_ui_sprites(Scene& scene, const d2d::mpq::Stack& mpqs) {
    auto txt = [&](const char* name) {
        auto bytes = mpqs.try_read(std::string(R"(data\global\excel\)") + name + ".txt");
        return bytes ? d2d::txt::Table(*bytes) : d2d::txt::Table{};
    };
    auto keys = [&](const char* name, const char* col, bool all) {
        std::vector<std::string> values;
        if (auto bytes = mpqs.try_read(std::string(R"(data\global\excel\)") + name + ".txt")) {
            const d2d::txt::Table table(*bytes, all);
            for (std::size_t row = 0; row < table.size(); ++row) values.emplace_back(table.get(row, col));
        }
        return values;
    };
    auto sprite = [&](std::string_view path, auto& into) {
        if (auto bytes = mpqs.try_read(path)) into = d2d::dc6::Sprite(*bytes);
    };
    const auto types = txt("ItemTypes");
    if (auto bytes = mpqs.try_read(R"(data\global\monsters\RandTransforms.dat)"))
        scene.rand_transforms.assign(reinterpret_cast<const std::uint8_t*>(bytes->data()), reinterpret_cast<const std::uint8_t*>(bytes->data()) + bytes->size());
    sprite(R"(data\global\ui\PANEL\buysell.dc6)", scene.store_panel);
    sprite(R"(data\global\ui\PANEL\buyselltabs.dc6)", scene.store_tabs);
    sprite(R"(data\global\ui\PANEL\buysellbtn.dc6)", scene.store_buttons);
    sprite(R"(data\global\ui\PANEL\goldcoinbtn.dc6)", scene.gold_coin);
    sprite(R"(data\global\ui\PANEL\gemsocket.dc6)", scene.gem_socket);
    sprite(R"(data\global\ui\MENU\dialogbackground.dc6)", scene.gold_dialog);
    sprite(R"(data\global\ui\MENU\spinner.dc6)", scene.spinner);
    for (std::size_t row = 0; row < types.size(); ++row) {
        const std::string code(types.get(row, "Code"));
        auto& graphics = scene.type_invgfx[code];
        for (int i = 0; i < 6; ++i) graphics[std::size_t(i)] = std::string(types.get(row, "InvGfx" + std::to_string(i + 1)));
    }
    if (auto bytes = mpqs.try_read(R"(data\global\ui\PANEL\ctrlpnl_popbelt.dc6)")) scene.popbelt = d2d::dc6::Sprite(*bytes);
    if (auto bytes = mpqs.try_read(R"(data\global\ui\CURSOR\focus16.dc6)")) scene.focus16 = d2d::dc6::Sprite(*bytes);
    {
        static constexpr std::string_view kLevelTypeName[] = {
            "None", "1 Town", "1 Wilderness", "1 Cave", "1 Crypt", "1 Monestary", "1 Courtyard", "1 Barracks",
            "1 Jail", "1 Cathedral", "1 Catacombs", "1 Tristram", "2 Town", "2 Sewer", "2 Harem", "2 Basement",
            "2 Desert", "2 Tomb", "2 Lair", "2 Arcane", "3 Town", "3 Jungle", "3 Kurast", "3 Spider", "3 Dungeon",
            "3 Sewer", "4 Town", "4 Mesa", "4 Lava", "5 Town", "5 Siege", "5 Barricade", "5 Temple", "5 Ice",
            "5 Baal", "5 Lava" };
        static constexpr std::string_view kOrientName[] = {
            "fl", "wl", "wr", "wtlr", "wtll", "wtr", "wbl", "wbr", "wld", "wrd", "wle", "wre", "co", "sh", "tr",
            "rf", "ld", "rd", "fd", "fi" };
        const auto automap_table = txt("AutoMap");
        auto idx = [](auto& names, std::string_view name) {
            for (std::size_t i = 0; i < std::size(names); ++i) if (names[i] == name) return int(i);
            return -1;
        };
        auto num = [&](std::size_t row_index, std::string_view column) {
            const auto value = automap_table.get(row_index, column);
            return value.empty() ? -1 : std::atoi(std::string(value).c_str());
        };
        for (std::size_t row = 0; row < automap_table.size(); ++row) {
            Scene::AutomapRule rule;
            rule.level_type = idx(kLevelTypeName, automap_table.get(row, "LevelName"));
            rule.orientation = idx(kOrientName, automap_table.get(row, "TileName"));
            if (rule.level_type < 0 || rule.orientation < 0) continue;
            rule.main = num(row, "Style");
            rule.sub0 = num(row, "StartSequence");
            rule.sub1 = num(row, "EndSequence");
            for (const char* column : { "Cel1", "Cel2", "Cel3", "Cel4" })
                if (const int cel = num(row, column); cel >= 0) rule.cels.push_back(cel);
            if (!rule.cels.empty()) scene.automap_rules.push_back(std::move(rule));
        }
        // By level type and orientation, each's rows in the table's order (automap_cel's first match).
        std::ranges::stable_sort(scene.automap_rules, {}, [](const Scene::AutomapRule& rule) { return std::pair(rule.level_type, rule.orientation); });
        if (auto bytes = mpqs.try_read(R"(data\global\ui\AutoMap\MaxiMap.dc6)")) scene.automap_cels = d2d::dc6::Sprite(*bytes);
        if (auto bytes = mpqs.try_read(R"(data\global\ui\AutoMap\MaxiMapS.dc6)")) scene.automap_cels_small = d2d::dc6::Sprite(*bytes);
    }
    sprite(R"(data\global\ui\menu\waygatebackground.dc6)", scene.wp_bg);
    sprite(R"(data\global\ui\menu\waygateicons.dc6)", scene.wp_icons);
    sprite(R"(data\global\ui\menu\waygatetabs.dc6)", scene.wp_tabs[0]);
    sprite(R"(data\global\ui\menu\expwaygatetabs.dc6)", scene.wp_tabs[1]);
    if (auto bytes = mpqs.try_read(R"(data\local\FONT\LATIN\fontformal11.tbl)"))
        if (auto sheet_bytes = mpqs.try_read(R"(data\local\FONT\LATIN\fontformal11.dc6)"))
            scene.font_formal11 = d2d::font::Font(*bytes, d2d::dc6::Sprite(*sheet_bytes));
    if (auto bytes = mpqs.try_read(R"(data\local\FONT\LATIN\fontingamechat.tbl)"))
        if (auto sheet_bytes = mpqs.try_read(R"(data\local\FONT\LATIN\fontingamechat.dc6)"))
            scene.font_chat = d2d::font::Font(*bytes, d2d::dc6::Sprite(*sheet_bytes));
    if (auto bytes = mpqs.try_read(R"(data\local\FONT\LATIN\font30.tbl)"))
        if (auto sheet_bytes = mpqs.try_read(R"(data\local\FONT\LATIN\font30.dc6)"))
            scene.font30 = d2d::font::Font(*bytes, d2d::dc6::Sprite(*sheet_bytes));
    sprite(R"(data\local\UI\ENG\youdiedhardcore.dc6)", scene.you_died);
    sprite(R"(data\local\UI\ENG\youdiedinst.dc6)", scene.you_died_inst);
    for (const auto name : game_menu_images())
        if (!scene.menu_text.contains(std::string(name)))
            if (auto bytes = mpqs.try_read(std::string(R"(data\local\UI\ENG\)") + std::string(name) + ".dc6"))
                scene.menu_text.emplace(std::string(name), d2d::dc6::Sprite(*bytes));
    sprite(R"(data\global\ui\CURSOR\pentspin.dc6)", scene.pentspin);
    sprite(R"(data\global\ui\menu\boxpieces.dc6)", scene.boxpieces);
    sprite(R"(data\global\ui\menu\textslid.dc6)", scene.textslid);
    sprite(R"(data\global\ui\Menu\800helpborder.dc6)", scene.help_border);
    sprite(R"(data\global\ui\PANEL\NPCInv.dc6)", scene.npc_inv);
    sprite(R"(data\global\ui\Menu\party.dc6)", scene.party_panel);
    sprite(R"(data\global\ui\Menu\partybuttons.dc6)", scene.party_buttons);
    sprite(R"(data\global\ui\Menu\partyboxes.dc6)", scene.party_boxes);
    sprite(R"(data\global\ui\Menu\partybar.dc6)", scene.party_bar);
    sprite(R"(data\global\ui\PANEL\inv_armor.dc6)", scene.inv_armor);
    sprite(R"(data\global\ui\PANEL\inv_helm_glove.dc6)", scene.inv_helm_glove);
    sprite(R"(data\global\ui\PANEL\inv_weapons.dc6)", scene.inv_weapons);
    sprite(R"(data\global\ui\Menu\helpwhitebullet.dc6)", scene.help_white_bullet);
    sprite(R"(data\global\ui\Menu\helpyellowbullet.dc6)", scene.help_yellow_bullet);
    sprite(R"(data\global\ui\Widgets\OptBar.dc6)", scene.opt_bar);
    sprite(R"(data\global\ui\Widgets\OptBarC.dc6)", scene.opt_bar_c);
    sprite(R"(data\global\ui\Widgets\OptSkull.dc6)", scene.opt_skull);
    sprite(R"(data\global\ui\PANEL\menubutton.dc6)", scene.menu_button);
    sprite(R"(data\global\ui\PANEL\minipanel_s.dc6)", scene.minipanel);
    sprite(R"(data\global\ui\PANEL\minipanelbtn.dc6)", scene.minipanel_btn);
    scene.unique_inv = keys("UniqueItems", "invfile", false);
    scene.set_inv    = keys("SetItems", "invfile", false);
    auto font = [&](const char* name, auto& into) {
        if (auto bytes = mpqs.try_read(std::string(R"(data\local\FONT\LATIN\)") + name + ".tbl"))
            if (auto sheet_bytes = mpqs.try_read(std::string(R"(data\local\FONT\LATIN\)") + name + ".dc6"))
                into = d2d::font::Font(*bytes, d2d::dc6::Sprite(*sheet_bytes));
    };
    font("font8", scene.font_small);
    font("font6", scene.font_tiny);
    sprite(R"(data\global\ui\PANEL\800ctrlpnl7.dc6)", scene.ctrl_panel);
    sprite(R"(data\global\ui\PANEL\hlthmana.dc6)", scene.globes);
    sprite(R"(data\global\ui\PANEL\overlap.dc6)", scene.globe_glass);
    sprite(R"(data\global\ui\PANEL\runbutton.dc6)", scene.run_button);
    if (auto bytes = mpqs.try_read(R"(data\global\ui\PANEL\invchar6.dc6)"))
        scene.inv_panel = d2d::dc6::Sprite(*bytes);
    sprite(R"(data\global\ui\PANEL\invchar6Tab.dc6)", scene.inv_tab);
    sprite(R"(data\global\ui\PANEL\800BorderFrame.dc6)", scene.border_frame);
    {
        const auto levels = txt("Levels");
        std::unordered_map<int, int> act_of;                  // Id -> Act (rows aren't Ids: LoD's "Expansion" row)
        for (std::size_t row = 0; row < levels.size(); ++row)
            act_of[std::atoi(std::string(levels.get(row, "Id")).c_str())] = std::atoi(std::string(levels.get(row, "Act")).c_str());
        auto entry = [&](int level, const std::string& file) {
            if (file.empty() || file == "0") return std::string();
            const std::string act = level >= 0x6d ? "EXPANSION" : "ACT" + std::to_string(act_of[level] + 1);
            std::string path = R"(data\local\UI\ENG\)" + act + "\\" + file + ".dc6";
            if (scene.entry_art.contains(path)) return path;
            auto bytes = mpqs.try_read(path);
            if (!bytes) return std::string();
            scene.entry_art.emplace(path, d2d::dc6::Sprite(*bytes));
            return path;
        };
        for (std::size_t row = 0; row < levels.size(); ++row) {
            const std::string id(levels.get(row, "Id"));
            if (id.empty()) continue;
            const int level = std::atoi(id.c_str());
            if (auto path = entry(level, std::string(levels.get(row, "EntryFile"))); !path.empty()) scene.level_entry[level] = std::move(path);
        }
        // 0x70f860: {level, quest, file}: the art while that quest's open in this game (FUN_004b92e0).
        for (const auto& [level, file] : { std::pair{ 8, "A1Q1" }, std::pair{ 20, "A1Q5" } })
            if (auto path = entry(level, file); !path.empty()) scene.level_entry_quest[level] = std::move(path);
    }
    sprite(R"(data\global\ui\PANEL\inv_belt.dc6)", scene.inv_belt);
    sprite(R"(data\global\ui\PANEL\inv_boots.dc6)", scene.inv_boots);
    sprite(R"(data\global\ui\PANEL\inv_ring_amulet.dc6)", scene.inv_ring_amulet);
    sprite(R"(data\global\ui\PANEL\level.dc6)", scene.level_button);
    sprite(R"(data\global\ui\PANEL\levelsocket.dc6)", scene.level_socket);
    sprite(R"(data\global\ui\PANEL\skillpoints.dc6)", scene.points_box);
    sprite(R"(data\global\ui\PANEL\bank.dc6)", scene.stash_panel[0]);
    sprite(R"(data\global\ui\PANEL\TradeStash.dc6)", scene.stash_panel[1]);
    sprite(R"(data\global\ui\PANEL\trade.dc6)", scene.trade_panel);
    sprite(R"(data\global\ui\PANEL\tradebtn.dc6)", scene.trade_button);
    static constexpr const char* kClassIcon[7] = { "Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin" };
    for (std::size_t class_index = 0; class_index < 7; ++class_index)
        if (auto bytes = mpqs.try_read(std::string(R"(data\global\ui\Hireables\)") + kClassIcon[class_index] + "Icon.dc6"))
            scene.class_icons[class_index] = d2d::dc6::Sprite(*bytes);
    if (auto bytes = mpqs.try_read(R"(data\global\ui\PANEL\supertransmogrifier.dc6)"))
        scene.cube_panel = d2d::dc6::Sprite(*bytes);
    if (auto bytes = mpqs.try_read(R"(data\global\ui\PANEL\miniconvert.dc6)"))
        scene.transmute_button = d2d::dc6::Sprite(*bytes);
    static constexpr const char* kTree[7] = { "a", "s", "n", "p", "b", "d", "i" };
    static constexpr const char* kIcons[7] = { "Am", "So", "Ne", "Pa", "Ba", "Dr", "As" };
    for (std::size_t class_index = 0; class_index < 7; ++class_index) {
        if (auto bytes = mpqs.try_read(std::string(R"(data\global\ui\SPELLS\skltree_)") + kTree[class_index] + "_back.dc6"))
            scene.skill_tree_bg[class_index] = d2d::dc6::Sprite(*bytes);
        if (auto bytes = mpqs.try_read(std::string(R"(data\global\ui\SPELLS\)") + kIcons[class_index] + "Skillicon.dc6"))
            scene.skill_icons[class_index] = d2d::dc6::Sprite(*bytes);
    }
    if (auto bytes = mpqs.try_read(R"(data\global\ui\SPELLS\Skillicon.dc6)")) scene.generic_skill_icons = d2d::dc6::Sprite(*bytes);
    sprite(R"(data\global\ui\menu\questbackground.dc6)", scene.quest_bg);
    sprite(R"(data\global\ui\menu\expquesttabs.dc6)", scene.quest_tabs);
    sprite(R"(data\global\ui\menu\questtabs.dc6)", scene.quest_tabs_classic);
    sprite(R"(data\global\ui\menu\questsockets.dc6)", scene.quest_sockets);
    sprite(R"(data\global\ui\menu\questlast.dc6)", scene.quest_last);
    sprite(R"(data\global\ui\menu\questdone.dc6)", scene.quest_done);
    for (std::size_t i = 0; i < scene.quest_icons.size(); ++i)    // the names at 0x6da2c8: a1q1..a1q6, a2q1.., a4q1..3, a5q1..
        if (auto bytes = mpqs.try_read(std::format(R"(data\global\ui\menu\a{}q{}.dc6)", i < 6 ? 1 : i < 12 ? 2 : i < 18 ? 3 : i < 21 ? 4 : 5,
                                               i < 18 ? i % 6 + 1 : i < 21 ? i - 17 : i - 20)))
            scene.quest_icons[i] = d2d::dc6::Sprite(*bytes);
}

void load_monster_sprites(Scene& scene, const d2d::mpq::Stack& mpqs) {
    std::vector<std::pair<d2d::dcc::Sprite*, std::vector<std::byte>>> cels;
    for (const auto& [name, missile] : scene.missiles)
        if (auto bytes = mpqs.try_read(R"(data\global\missiles\)" + missile.cel_file + ".dcc")) cels.emplace_back(&scene.missile_cels[name], std::move(*bytes));
    if (auto bytes = mpqs.try_read(R"(data\global\overlays\NPCSpeechBalloon.dcc)")) cels.emplace_back(&scene.npc_alert, std::move(*bytes));
    for (std::size_t i = 0; i < 4; ++i) {              // the rain's splashes and the mud's bubbles (FUN_00472890)
        if (auto bytes = mpqs.try_read(R"(data\global\UncompOverlays\Rain)" + std::to_string(i + 1) + ".dc6")) scene.rain_splash[i] = d2d::dc6::Sprite(*bytes);
        if (auto bytes = mpqs.try_read(R"(data\global\UncompOverlays\bubble)" + std::to_string(i + 1) + ".dc6")) scene.mud_bubble[i] = d2d::dc6::Sprite(*bytes);
    }
    // Their DCCs decode on every core (the reads above stay on this thread:
    // StormLib handles aren't shared).
    {
        std::atomic<std::size_t> next = 0;
        std::vector<std::jthread> pool(std::max(1u, std::thread::hardware_concurrency()));
        for (auto& worker : pool) worker = std::jthread([&] {
            for (std::size_t i; (i = next++) < cels.size();)
                try { *cels[i].first = d2d::dcc::Sprite(cels[i].second); }
                catch (const std::exception& error) { d2d::log::warn("missile cel: {}", error.what()); }
        });
    }
}

}  // namespace

Character character_of_save(const Scene& scene, std::size_t index) {
    Character character;
    const auto& header = scene.saves[index];
    character.character_class = header.cls;
    character.name = header.name;
    character.appearance = header.look();
    character.items = index < scene.save_items.size() ? scene.save_items[index] : std::vector<d2d::d2s::Item>{};
    character.stats = index < scene.save_stats.size() ? scene.save_stats[index] : d2d::d2s::Stats{};
    character.corpse = index < scene.save_corpses.size() ? scene.save_corpses[index] : std::vector<d2d::d2s::Item>{};
    character.merc_items = index < scene.save_mercs.size() ? scene.save_mercs[index] : std::vector<d2d::d2s::Item>{};
    character.panel = panel_stats(scene, header, character.items, character.stats);
    character.expansion = header.expansion();
    character.header = header;
    return character;
}

Character made_character(const Scene& scene, int save_cls, const std::string& name, bool hardcore, bool expansion, d2d::rules::Rng& rng) {
    auto made = new_character(scene, save_cls, name, hardcore, expansion, rng);
    Character character;
    character.character_class = save_cls;
    character.name = name;
    character.expansion = expansion;
    character.header = std::move(made.header);
    character.stats = made.stats;
    character.items = std::move(made.items);
    character.panel = panel_stats(scene, character.header, character.items, character.stats);
    return character;
}

void load_saves(Scene& scene, const fs::path& dir) {
    struct Entry { d2d::d2s::Header header; std::vector<d2d::d2s::Item> items; d2d::d2s::Stats stats; std::vector<d2d::d2s::Item> corpse, merc; };
    std::vector<Entry> out;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(dir, error)) {
        if (entry.path().extension() != ".d2s") continue;
        std::ifstream file(entry.path(), std::ios::binary);
        std::vector<char> raw{std::istreambuf_iterator<char>(file), {}};
        const auto bytes = std::as_bytes(std::span(raw));
        try {
            Entry save_entry{ d2d::d2s::parse_header(bytes), {}, {} };
            if (scene.item_tables) {
                try {
                    save_entry.stats = d2d::d2s::parse_stats(bytes, *scene.item_tables);
                    save_entry.items = d2d::d2s::parse_items(bytes, *scene.item_tables);
                    save_entry.corpse = d2d::d2s::parse_corpse(bytes, *scene.item_tables).items;
                    save_entry.merc = d2d::d2s::parse_merc_items(bytes, *scene.item_tables);
                }
                catch (const std::exception& parse_error) {
                    d2d::log::warn("{} items: {}", entry.path().string(), parse_error.what());
                }
            }
            out.push_back(std::move(save_entry));
        } catch (const std::exception& parse_error) {
            d2d::log::warn("{}: {}", entry.path().string(), parse_error.what());
        }
    }
    // Newest first — LoD inserts each character by last-played time,
    // descending (FUN_00438ad0), and preselects slot 0.
    // Ties (e.g. synthetic saves with no timestamp) fall back to name so
    // the order doesn't depend on directory iteration.
    std::ranges::sort(out, [](const Entry& first, const Entry& second) {
        return std::tie(second.header.last_played, first.header.name)
             < std::tie(first.header.last_played, second.header.name);
    });
    scene.saves.clear(); scene.save_items.clear(); scene.save_stats.clear(); scene.save_corpses.clear(); scene.save_mercs.clear();
    for (auto& entry : out) {
        scene.saves.push_back(std::move(entry.header));
        scene.save_items.push_back(std::move(entry.items));
        scene.save_corpses.push_back(std::move(entry.corpse));
        scene.save_mercs.push_back(std::move(entry.merc));
        scene.save_stats.push_back(entry.stats);
    }
    d2d::log::info("Characters: {} in {}", scene.saves.size(), dir.string());
}

std::optional<Scene> load_scene(const fs::path& data_dir, const fs::path& patch_installer) {
    auto data = game::open_game_data(data_dir, patch_installer, /*tile_pixels=*/true);
    if (!data) return std::nullopt;
    const auto start_ms = d2d::log::ms();
    try {
        Scene scene{ std::move(*data) };
        const auto& mpqs = scene.mpqs;
        // Prefer the LoD title asset (fenced rogue camp at night). Classic
        // TitleScreen is only 4×3 sub-frames; LoD is the same layout.
        auto title = mpqs.try_read(R"(data\global\ui\FrontEnd\gameselectscreenEXP.dc6)");
        if (!title) title = mpqs.try_read(R"(data\global\ui\FrontEnd\TitleScreen.DC6)");
        if (!title) throw std::runtime_error("no title screen asset");

        // Sky = title/credits (game.exe hardcodes palette\sky\pal.pl2 in
        // 5 sites of the menu loader — docs/research/re/frontend-menu-table.md).
        scene.pal = d2d::palette::Palette(mpqs.read(
                                R"(data\global\palette\Sky\pal.dat)"));
        // fechar = "Front End CHARacter", the char-select/creation palette.
        // game.exe's FUN_00435580 (char-select init) loads it right after
        // the char-select asset loader (FUN_004326f0). Firelit warm tones
        // — night camp scene lit by the campfire the classes stand around.
        scene.charselect_pal = d2d::palette::Palette(mpqs.read(
                                R"(data\global\palette\fechar\pal.dat)"));
        scene.sky_pl2 = d2d::palette::Pl2(mpqs.read(
                                R"(data\global\palette\Sky\Pal.PL2)"));
        scene.fechar_pl2 = d2d::palette::Pl2(mpqs.read(
                                R"(data\global\palette\fechar\Pal.PL2)"));
        scene.background = d2d::dc6::Sprite(*title);
        scene.logo_static = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\Diablo2.dc6)"));
        scene.logo_bl = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoBlackLeft.DC6)"));
        scene.logo_br = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoBlackRight.DC6)"));
        scene.logo_fl = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoFireLeft.DC6)"));
        scene.logo_fr = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoFireRight.DC6)"));
        scene.btn_wide = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\WideButtonBlank.dc6)"));
        scene.btn_wide2 = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\WideButtonBlank02.dc6)"));
        scene.btn_narrow = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\NarrowButtonBlank.dc6)"));
        scene.btn_short = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\ShortButtonBlank.dc6)"));
        scene.credits_bg = [&] {
                // creditsbckgexpand.dc6 (LoD) → creditsbckg.dc6 (classic).
                auto bytes = mpqs.try_read(R"(data\global\ui\CharSelect\creditsbckgexpand.dc6)");
                if (!bytes) bytes = mpqs.read(R"(data\global\ui\CharSelect\creditsbckg.dc6)");
                return d2d::dc6::Sprite(*bytes);
            }();
        scene.charcreate_bg = [&] {
                auto bytes = mpqs.try_read(R"(data\global\ui\FrontEnd\charactercreationscreenEXP.dc6)");
                if (!bytes) bytes = mpqs.read(R"(data\global\ui\FrontEnd\CharacterCreate.dc6)");
                return d2d::dc6::Sprite(*bytes);
            }();
        scene.fire = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\fire.DC6)"));
        scene.medium_button = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\MediumButtonBlank.dc6)"));
        scene.medium_sel_button = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\MediumSelButtonBlank.dc6)"));
        scene.textbox = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\textbox.dc6)"));
        scene.clickbox = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\clickbox.dc6)"));
        if (auto grey = mpqs.try_read(R"(data\global\ui\FrontEnd\joingameclickboxgrey.dc6)")) scene.clickbox_grey = d2d::dc6::Sprite(*grey);
        scene.charselect_bg = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\characterselectscreenEXP.dc6)"));
        scene.charselect_box = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\charselectbox.dc6)"));
        scene.charselect_scroll = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\joingamescrollbars.dc6)"));
        scene.tall_button = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\TallButtonBlank.dc6)"));
        scene.tcpip_bg = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\tcpipbckg.dc6)"));
        scene.ip_box = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\IPAddressBox.dc6)"));
        scene.popup_ok_cancel = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\PopUpOKCancel2.dc6)"));
        scene.cancel_button = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\CancelButtonBlank.dc6)"));
        scene.difficulty_levels = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\DifficultyLevels.dc6)"));
        scene.popup_ok = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\PopUpOK.dc6)"));
        auto font = [&](const char* name) {
            return d2d::font::Font(mpqs.read(std::string(R"(data\local\FONT\LATIN\)") + name + ".tbl"),
                                   d2d::dc6::Sprite(mpqs.read(std::string(R"(data\local\FONT\LATIN\)") + name + ".dc6")));
        };
        scene.font42 = font("font42");
        scene.font_formal12 = font("fontformal12");
        scene.font24 = font("font24");
        scene.cursor = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CURSOR\ohand.dc6)"));
        scene.cursor_buysell = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CURSOR\buysell.dc6)"));
        scene.class_anims = [&] {
                // Anim files per class, in order {nu1, nu2, fw, nu3, bw}.
                // Class prefix pairs from FUN_004326f0's loader.
                struct C { const char* dir; const char* prefix; };
                constexpr C classes[7] = {
                    {"barbarian",   "ba"},
                    {"necromancer", "ne"},
                    {"paladin",     "pa"},
                    {"amazon",      "am"},
                    {"sorceress",   "so"},
                    {"druid",       "dz"},
                    {"assassin",    "as"},
                };
                constexpr const char* suffix[5] = {"nu1", "nu2", "fw", "nu3", "bw"};
                std::array<std::array<d2d::dc6::Sprite, 5>, 7> out;
                for (std::size_t class_index = 0; class_index < 7; ++class_index) {
                    for (std::size_t state_index = 0; state_index < 5; ++state_index) {
                        char path[256];
                        std::snprintf(path, sizeof(path),
                            R"(data\global\ui\FrontEnd\%s\%s%s.dc6)",
                            classes[class_index].dir, classes[class_index].prefix, suffix[state_index]);
                        out[class_index][state_index] = d2d::dc6::Sprite(mpqs.read(path));
                    }
                }
                return out;
            }();
        scene.font = d2d::font::Font(
                             mpqs.read(R"(data\local\FONT\LATIN\font16.tbl)"),
                             d2d::dc6::Sprite(mpqs.read(R"(data\local\FONT\LATIN\font16.dc6)")));
        scene.credits = [&] {
                auto bytes = mpqs.try_read(R"(data\local\UI\ENG\ExpansionCredits.txt)");
                if (!bytes) bytes = mpqs.try_read(R"(data\local\ui\eng\Credits.txt)");
                return bytes ? parse_credits_utf16(*bytes) : std::vector<std::string>{};
            }();
        // Rogue-camp world data — separate call so a DS1/DT1 miss doesn't
        // nuke the whole scene; the InGame screen falls back to the credits
        // placeholder when world is empty.
        if (auto bytes = mpqs.try_read(R"(data\global\ui\FrontEnd\CinematicsSelectionEXP.dc6)"))
            scene.cinematics_panel = d2d::dc6::Sprite(*bytes);
        d2d::log::info("Menus loaded in {} ms.", d2d::log::ms() - start_ms);
        return scene;
    } catch (const std::exception& error) {
        d2d::log::error("load_scene: {}", error.what());
        return std::nullopt;
    }
}

void finish_scene(Scene& scene, const fs::path& save_dir, std::uint32_t map_seed,
                  std::uint32_t (*save_seed)(const d2d::d2s::Header&)) {
    const auto start_ms = d2d::log::ms();
    try {
        auto mpqs = scene.mpqs.reopen();        // StormLib handles stay on their thread
        game::load_game_tables(scene, mpqs);
        load_saves(scene, save_dir);            // their items need the tables
        // The newest save is the likely pick: its map, and the act it was left in.
        int act = 0;
        if (!scene.saves.empty()) {
            map_seed = save_seed(scene.saves.front());
            act = scene.saves.front().active_act();
        }
        game::load_game_world(scene, mpqs, map_seed);
        // A later act's town starts building on the builder thread while the
        // menus run (as the camp's neighbours do, deviations.md improvement 8).
        if (act > 0 && std::size_t(act) < d2d::rules::level_ids::kActTowns.size()) {
            scene.want_level(d2d::rules::level_ids::kActTowns[std::size_t(act)]);
            d2d::log::info("  Newest save is in act {}; its town builds while the menus run", act + 1);
        }
        load_ui_sprites(scene, mpqs);
        load_monster_sprites(scene, mpqs);
        load_act_palettes(scene, mpqs, 0);
        want_nearby(scene, scene.town);         // the town's neighbours build while the menus run (deviations.md improvement 8)
        d2d::log::info("Scene loaded in {} ms.", d2d::log::ms() - start_ms);
    } catch (const std::exception& error) {
        d2d::log::error("finish_scene: {}", error.what());
    }
}

// The act's palette (palette\ACTn\pal.dat) and its 32 light levels: PL2
// +0x400, 256 indices a level, level 31 as is and 0 black; the software
// renderer draws a pixel at light v through level v >> 3 (FUN_004f8050).
void load_act_palettes(Scene& scene, const d2d::mpq::Stack& mpqs, int act) {
    const auto dir = std::format(R"(data\global\palette\ACT{}\)", std::clamp(act, 0, 4) + 1);
    scene.act_pal = {};                      // a missing file leaves none, never the last act's
    scene.act_lit = {};
    scene.act_text_red = {};
    if (auto bytes = mpqs.try_read(dir + "pal.dat"))
        scene.act_pal = d2d::palette::Palette(*bytes);
    if (scene.act_pal.entries().empty()) { scene.palette_act = act; return; }
    if (auto bytes = mpqs.try_read(dir + "Pal.pl2"); bytes && bytes->size() >= 0x400 + 32 * 256)
        for (std::size_t level = 0; level < 32; ++level) {
            std::array<d2d::palette::Rgba, 256> entries{};
            for (std::size_t i = 0; i < 256; ++i) entries[i] = scene.act_pal[std::uint8_t((*bytes)[0x400 + level * 256 + i])];
            entries[0].a = 0;
            scene.act_lit[level] = d2d::palette::Palette(entries);
        }
    // The PL2 ends in 13 text colours' index shifts (256 each); 1 is red.
    if (auto bytes = mpqs.try_read(dir + "Pal.pl2"); bytes && bytes->size() >= 0x400 + 13 * 256) {
        const std::size_t red_shift = bytes->size() - 13 * 256 + 256;
        std::array<d2d::palette::Rgba, 256> entries{};
        for (std::size_t i = 0; i < 256; ++i) entries[i] = scene.act_pal[std::uint8_t((*bytes)[red_shift + i])];
        entries[0].a = 0;
        scene.act_text_red = d2d::palette::Palette(entries);
    }
    scene.palette_act = act;
}

}  // namespace d2d::client
