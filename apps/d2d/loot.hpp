// Loot: what kills drop (treasure classes, components/rules/drops.hpp) lying
// on each level's floor, drawn with each item's flippy, and picking it
// up into the purse or the inventory.
#pragma once

#include "window.hpp"

namespace {

struct Loot {
    const Scene* scene;
    const Level* const& level;             // Town's: where drops land
    CharCreateUI& cc;
    UnitState& player;
    d2d::rules::Rng& rng;
    Cues& cues;
    // An item on the ground (gold: code "gld", `gold` coins).
    struct GroundItem {
        d2d::d2s::Item item;
        int gold = 0;
        float x = 0, y = 0;
        std::uint32_t ms = 0;                // when it dropped: the flippy plays from here
        std::string label;
        std::array<std::uint8_t, 3> rgb{ 255, 255, 255 };
    };
    std::vector<GroundItem> ground;        // ground_level's
    const Level* ground_level = nullptr;
    std::unordered_map<const Level*, std::vector<GroundItem>> kept;   // other levels' floors
    // The player went to `to`: what lies on its floor, the last level's kept.
    void enter(const Level* to) {
        if (to == ground_level) return;
        if (ground_level) kept[ground_level] = std::move(ground);
        ground = std::move(kept[to]);
        ground_level = to;
    }

    // A kill's loot (MonStats TreasureClass1 for the difficulty) round
    // where it fell. Magic and better come unidentified.
    // ponytail: D2 spreads drops by its own pattern (not traced); here each
    // goes to the nearest free spot within half a cell.
    void drop(const Monster& m, std::uint32_t ms) {
        const int diff = cc.header.active_difficulty();
        // Champions drop from TreasureClass2, uniques from 3, superuniques
        // from their SuperUniques TC (for the difficulty).
        const auto d = std::size_t(std::clamp(diff, 0, 2));
        const auto& t = scene->monsters.types[std::size_t(m.type)];
        const std::string& tc = m.super >= 0 && std::size_t(m.super) < scene->superuniques.size() && !scene->superuniques[std::size_t(m.super)].tc[d].empty()
                                    ? scene->superuniques[std::size_t(m.super)].tc[d]
                              : m.boss == d2d::rules::Boss::champion && !t.tc_champion[d].empty() ? t.tc_champion[d]
                              : m.boss == d2d::rules::Boss::unique && !t.tc_unique[d].empty()     ? t.tc_unique[d]
                                                                                                   : t.diff[d].tc;
        std::vector<d2d::rules::Drop> drops;
        d2d::rules::roll_drops(scene->rules, tc, m.st.level, rng, drops);
        for (const auto& d : drops) put(d, m.u.x, m.u.y, m.st.level, ms);
    }
    // One drop round (x, y).
    void put(const d2d::rules::Drop& d, float x, float y, int ilvl, std::uint32_t ms) {
        {
            GroundItem g;
            std::tie(g.x, g.y) = level->nearest_free(x + float(rng(11) - 5) / 10, y + float(rng(11) - 5) / 10);
            g.ms = ms;
            if (d.code == "gld") {
                g.item.code = "gld";
                g.gold = d.gold;
                g.label = std::to_string(d.gold) + " Gold";
            } else {
                g.item = d2d::rules::generate_item(scene->rules, d.code, ilvl, d.quality, rng);
                g.item.identified = d.quality <= 3;
                const auto lines = item_lines(*scene, g.item, int(cc.stats.get(d2d::d2s::kLevel)));
                if (!lines.empty()) { g.label = lines[0].text; g.rgb = lines[0].rgb; }
            }
            if (!scene->flippy(g.item.code)) return;
            cues.cue("item_flippy", ms, g.x, g.y);
            if (const auto info = scene->rules.item_info.find(g.item.code); info != scene->rules.item_info.end())
                cues.cue(info->second.drop_sound, ms + std::uint32_t(info->second.drop_frame) * 40, g.x, g.y);
            ground.push_back(std::move(g));
        }
    }

    // Picking up: gold into the purse (up to 10000 per character level),
    // an item into the first inventory spot it fits.
    // ponytail: potions don't go to the belt first; no "no room" sound.
    void take(std::size_t i) {
        using namespace d2d::d2s;
        auto& g = ground[i];
        if (g.item.code == "gld") {
            const auto cap = cc.stats.get(kLevel) * 10000, room = std::max<std::int64_t>(cap - cc.stats.get(kGold), 0);
            const auto n = std::min<std::int64_t>(g.gold, room);
            if (n <= 0) return;
            cc.stats.v[kGold] += n;
            cues.cue("item_gold", 0, player.x, player.y);
            if ((g.gold -= int(n)) > 0) { g.label = std::to_string(g.gold) + " Gold"; return; }
        } else {
            const auto& lay = scene->inv_layout[std::size_t(kUiToSaveClass[std::max(cc.selected, 0)])];
            std::vector<const Item*> inv;
            for (const auto& x : cc.items) if (x.location == 0 && x.panel == 1) inv.push_back(&x);
            const auto [w, h] = d2d::rules::item_size(scene->rules, g.item.code);
            const auto [x, y] = d2d::rules::free_spot(scene->rules, inv, lay.cols ? lay.cols : 10, lay.rows ? lay.rows : 4, w, h);
            if (x < 0) { d2d::log::info("no room for {}", g.label); return; }
            g.item.location = 0; g.item.panel = 1; g.item.column = x; g.item.row = y;
            cc.items.push_back(std::move(g.item));
            cues.cue("item_pickup", 0, player.x, player.y);
        }
        ground.erase(ground.begin() + std::ptrdiff_t(i));
    }

    // The ground items near (px, py) as units the world draws by depth
    // (npc -1000 - i: hoverable, labelled in the item's colour).
    void units(float px, float py, std::vector<Unit>& out) const {
        for (std::size_t i = 0; i < ground.size(); ++i) {
            const auto& g = ground[i];
            if (std::abs(g.x - px) >= 14 || std::abs(g.y - py) >= 14) continue;
            Unit u{ g.x, g.y, nullptr, 0, &g.label, g.ms, -1000 - int(i) };
            u.sprite = scene->flippy(g.item.code);
            u.rgb = g.rgb;
            out.push_back(u);
        }
    }
};

}  // namespace
