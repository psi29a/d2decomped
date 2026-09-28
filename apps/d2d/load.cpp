// Definitions for load.hpp: the Scene: GameData (gamedata_load.hpp) plus sprites, fonts, palettes, saves.
#include "load.hpp"
#include "common.hpp"
#include "scene.hpp"
#include "ui.hpp"

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
            if (auto bytes = mpqs.try_read(path)) out.dcc[layer.type] = std::move(*bytes);
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
    const auto types = txt("ItemTypes");
    if (auto bytes = mpqs.try_read(R"(data\global\monsters\RandTransforms.dat)"))
        scene.rand_transforms.assign(reinterpret_cast<const std::uint8_t*>(bytes->data()), reinterpret_cast<const std::uint8_t*>(bytes->data()) + bytes->size());
    for (auto [path, into] : { std::pair{ R"(data\global\ui\PANEL\buysell.dc6)", &scene.store_panel },
                               { R"(data\global\ui\PANEL\buyselltabs.dc6)", &scene.store_tabs },
                               { R"(data\global\ui\PANEL\buysellbtn.dc6)", &scene.store_buttons },
                               { R"(data\global\ui\PANEL\goldcoinbtn.dc6)", &scene.gold_coin } })
        if (auto bytes = mpqs.try_read(path)) *into = d2d::dc6::Sprite(*bytes);
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
        if (auto bytes = mpqs.try_read(R"(data\global\ui\AutoMap\MaxiMap.dc6)")) scene.automap_cels = d2d::dc6::Sprite(*bytes);
    }
    for (auto [path, into] : { std::pair{ R"(data\global\ui\menu\waygatebackground.dc6)", &scene.wp_bg },
                               { R"(data\global\ui\menu\waygateicons.dc6)", &scene.wp_icons },
                               { R"(data\global\ui\menu\waygatetabs.dc6)", &scene.wp_tabs[0] },
                               { R"(data\global\ui\menu\expwaygatetabs.dc6)", &scene.wp_tabs[1] } })
        if (auto bytes = mpqs.try_read(path)) *into = d2d::dc6::Sprite(*bytes);
    if (auto bytes = mpqs.try_read(R"(data\local\FONT\LATIN\fontformal11.tbl)"))
        if (auto sheet_bytes = mpqs.try_read(R"(data\local\FONT\LATIN\fontformal11.dc6)"))
            scene.font_formal11 = d2d::font::Font(*bytes, d2d::dc6::Sprite(*sheet_bytes));
    if (auto bytes = mpqs.try_read(R"(data\local\FONT\LATIN\font30.tbl)"))
        if (auto sheet_bytes = mpqs.try_read(R"(data\local\FONT\LATIN\font30.dc6)"))
            scene.font30 = d2d::font::Font(*bytes, d2d::dc6::Sprite(*sheet_bytes));
    for (auto [spr, name] : { std::pair{ &scene.you_died, "youdiedhardcore" }, { &scene.you_died_inst, "youdiedinst" } })
        if (auto bytes = mpqs.try_read(std::string(R"(data\local\UI\ENG\)") + name + ".dc6")) *spr = d2d::dc6::Sprite(*bytes);
    scene.unique_inv = keys("UniqueItems", "invfile", false);
    scene.set_inv    = keys("SetItems", "invfile", false);
    for (auto [name, into] : { std::pair{ "font8", &scene.font_small }, { "font6", &scene.font_tiny } })
        if (auto bytes = mpqs.try_read(std::string(R"(data\local\FONT\LATIN\)") + name + ".tbl"))
            if (auto sheet_bytes = mpqs.try_read(std::string(R"(data\local\FONT\LATIN\)") + name + ".dc6"))
                *into = d2d::font::Font(*bytes, d2d::dc6::Sprite(*sheet_bytes));
    for (auto [path, into] : { std::pair{ R"(data\global\ui\PANEL\800ctrlpnl7.dc6)", &scene.ctrl_panel },
                               { R"(data\global\ui\PANEL\hlthmana.dc6)", &scene.globes },
                               { R"(data\global\ui\PANEL\overlap.dc6)", &scene.globe_glass } })
        if (auto bytes = mpqs.try_read(path)) *into = d2d::dc6::Sprite(*bytes);
    if (auto bytes = mpqs.try_read(R"(data\global\ui\PANEL\invchar6.dc6)"))
        scene.inv_panel = d2d::dc6::Sprite(*bytes);
    for (auto [path, into] : { std::pair{ R"(data\global\ui\PANEL\level.dc6)", &scene.level_button },
                               { R"(data\global\ui\PANEL\levelsocket.dc6)", &scene.level_socket },
                               { R"(data\global\ui\PANEL\skillpoints.dc6)", &scene.points_box } })
        if (auto bytes = mpqs.try_read(path)) *into = d2d::dc6::Sprite(*bytes);
    for (auto [path, page] : { std::pair{ R"(data\global\ui\PANEL\bank.dc6)", 0 },
                            { R"(data\global\ui\PANEL\TradeStash.dc6)", 1 } })
        if (auto bytes = mpqs.try_read(path)) scene.stash_panel[std::size_t(page)] = d2d::dc6::Sprite(*bytes);
    if (auto bytes = mpqs.try_read(R"(data\global\ui\PANEL\supertransmogrifier.dc6)"))
        scene.cube_panel = d2d::dc6::Sprite(*bytes);
    static constexpr const char* kTree[7] = { "a", "s", "n", "p", "b", "d", "i" };
    static constexpr const char* kIcons[7] = { "Am", "So", "Ne", "Pa", "Ba", "Dr", "As" };
    for (std::size_t class_index = 0; class_index < 7; ++class_index) {
        if (auto bytes = mpqs.try_read(std::string(R"(data\global\ui\SPELLS\skltree_)") + kTree[class_index] + "_back.dc6"))
            scene.skill_tree_bg[class_index] = d2d::dc6::Sprite(*bytes);
        if (auto bytes = mpqs.try_read(std::string(R"(data\global\ui\SPELLS\)") + kIcons[class_index] + "Skillicon.dc6"))
            scene.skill_icons[class_index] = d2d::dc6::Sprite(*bytes);
    }
    if (auto bytes = mpqs.try_read(R"(data\global\ui\SPELLS\Skillicon.dc6)")) scene.generic_skill_icons = d2d::dc6::Sprite(*bytes);
    for (auto [sprite, file] : { std::pair{ &scene.quest_bg, "questbackground" }, { &scene.quest_tabs, "expquesttabs" },
                                 { &scene.quest_sockets, "questsockets" }, { &scene.quest_last, "questlast" } })
        if (auto bytes = mpqs.try_read(std::string(R"(data\global\ui\menu\)") + file + ".dc6")) *sprite = d2d::dc6::Sprite(*bytes);
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
    for (std::size_t i = 0; i < 4; ++i)                // the rain's splashes (FUN_00472890)
        if (auto bytes = mpqs.try_read(R"(data\global\UncompOverlays\Rain)" + std::to_string(i + 1) + ".dc6")) scene.rain_splash[i] = d2d::dc6::Sprite(*bytes);
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

void load_act1_palettes(Scene& scene, const d2d::mpq::Stack& mpqs) {
    if (auto bytes = mpqs.try_read(R"(data\global\palette\ACT1\pal.dat)"))
        scene.act1_pal = d2d::palette::Palette(*bytes);
    // Its 32 light levels: PL2 +0x400, 256 indices a level, level 31 as is and
    // 0 black; the software renderer draws a pixel at light v through level
    // v >> 3 (FUN_004f8050).
    if (auto bytes = mpqs.try_read(R"(data\global\palette\ACT1\Pal.pl2)"); bytes && bytes->size() >= 0x400 + 32 * 256)
        for (std::size_t level = 0; level < 32; ++level) {
            std::array<d2d::palette::Rgba, 256> entries{};
            for (std::size_t i = 0; i < 256; ++i) entries[i] = scene.act1_pal[std::uint8_t((*bytes)[0x400 + level * 256 + i])];
            entries[0].a = 0;
            scene.act1_lit[level] = d2d::palette::Palette(entries);
        }
}

}  // namespace

void load_saves(Scene& scene, const fs::path& dir) {
    struct Entry { d2d::d2s::Header header; std::vector<d2d::d2s::Item> items; d2d::d2s::Stats stats; std::vector<d2d::d2s::Item> corpse; };
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
    scene.saves.clear(); scene.save_items.clear(); scene.save_stats.clear(); scene.save_corpses.clear();
    for (auto& entry : out) {
        scene.saves.push_back(std::move(entry.header));
        scene.save_items.push_back(std::move(entry.items));
        scene.save_corpses.push_back(std::move(entry.corpse));
        scene.save_stats.push_back(entry.stats);
    }
    d2d::log::info("Characters: {} in {}", scene.saves.size(), dir.string());
}

std::optional<Scene> load_scene(const fs::path& data_dir, const fs::path& patch_installer, std::uint32_t map_seed) {
    auto data = game::load_game_data(data_dir, patch_installer, map_seed);
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
        scene.charselect_bg = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\characterselectscreenEXP.dc6)"));
        scene.charselect_box = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\charselectbox.dc6)"));
        scene.charselect_scroll = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\joingamescrollbars.dc6)"));
        scene.tall_button = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\TallButtonBlank.dc6)"));
        scene.cursor = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CURSOR\ohand.dc6)"));
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
        // Frontend button labels (IDs 0x13f2..0x13f7) live in the base
        // string.tbl per probe. patchstring.tbl (826 entries) overrides
        // specific IDs when Blizzard shipped patches; expansionstring.tbl
        // (2788 entries) carries LoD-specific additions. For MVP we use
        // string.tbl directly; when a subsystem needs a patch-shifted
        // entry, load all three and query in order (patch → expansion →
        // base).
        scene.strings = [&] {
                auto bytes = mpqs.try_read(R"(data\local\LNG\ENG\string.tbl)");
                return bytes ? d2d::tbl::Table(*bytes) : d2d::tbl::Table{};
            }();
        scene.patch_strings = [&] {
                auto bytes = mpqs.try_read(R"(data\local\LNG\ENG\patchstring.tbl)");
                return bytes ? d2d::tbl::Table(*bytes) : d2d::tbl::Table{};
            }();
        scene.exp_strings = [&] {
                auto bytes = mpqs.try_read(R"(data\local\LNG\ENG\expansionstring.tbl)");
                return bytes ? d2d::tbl::Table(*bytes) : d2d::tbl::Table{};
            }();
        // Rogue-camp world data — separate call so a DS1/DT1 miss doesn't
        // nuke the whole scene; the InGame screen falls back to the credits
        // placeholder when world is empty.
        if (auto bytes = mpqs.try_read(R"(data\global\ui\FrontEnd\CinematicsSelectionEXP.dc6)"))
            scene.cinematics_panel = d2d::dc6::Sprite(*bytes);
        load_ui_sprites(scene, mpqs);
        load_monster_sprites(scene, mpqs);
        load_act1_palettes(scene, mpqs);
        d2d::log::info("Scene loaded in {} ms.", d2d::log::ms() - start_ms);
        return scene;
    } catch (const std::exception& error) {
        d2d::log::error("load_scene: {}", error.what());
        return std::nullopt;
    }
}

}  // namespace d2d::client
