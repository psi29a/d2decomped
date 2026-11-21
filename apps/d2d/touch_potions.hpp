// SPDX-License-Identifier: GPL-3.0-or-later
// Pure, bounded potion summary for the touch HUD.
#pragma once

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace d2d::client::touch {

enum class PotionKind { None, Healing, Mana };
enum class PotionPlace { Belt, Inventory, Other };

struct PotionEntry {
    std::string_view code;
    PotionPlace place = PotionPlace::Other;
    int box = -1; // Native Item::column: belt boxes 0..15, four columns at 0..3.
};

struct PotionSummary {
    int healing = 0;
    int mana = 0;
    std::optional<int> strongest_healing_slot;
    std::optional<int> strongest_mana_slot;
};

[[nodiscard]] inline PotionKind touch_potion_kind(std::string_view code) {
    if (code == "hp1" || code == "hp2" || code == "hp3" || code == "hp4" || code == "hp5") return PotionKind::Healing;
    if (code == "mp1" || code == "mp2" || code == "mp3" || code == "mp4" || code == "mp5") return PotionKind::Mana;
    return PotionKind::None; // Includes rejuv, antidote, thawing and stamina potions.
}

[[nodiscard]] inline int touch_potion_tier(std::string_view code) {
    if (code.size() == 3 && (code[0] == 'h' || code[0] == 'm') && code[1] == 'p' && code[2] >= '1' && code[2] <= '5') return code[2] - '0';
    return 0;
}

/**
 * Summarize potion stock from belt entries only. The consumable in each column
 * is the first item in input order whose native Item::column equals that
 * slot, matching rules::drink_belt's exact column comparison. Deeper rows
 * (boxes 4..15) count as stock but are not consumed until moved upward.
 * Ties within each potion kind resolve to the lowest slot (0..3).
 */
[[nodiscard]] inline PotionSummary summarize_touch_potions(std::span<const PotionEntry> entries) {
    PotionSummary result;
    std::array<std::string_view, 4> top_code{};
    std::array<bool, 4> has_top{};
    std::array<bool, 16> occupied{};
    for (const auto& entry : entries) {
        if (entry.place != PotionPlace::Belt || entry.box < 0 || entry.box >= 16) continue;
        if (occupied[std::size_t(entry.box)]) continue;
        occupied[std::size_t(entry.box)] = true;
        const auto kind = touch_potion_kind(entry.code);
        if (kind == PotionKind::Healing) ++result.healing;
        if (kind == PotionKind::Mana) ++result.mana;
        if (entry.box < 4 && !has_top[std::size_t(entry.box)]) {
            has_top[std::size_t(entry.box)] = true;
            top_code[std::size_t(entry.box)] = entry.code;
        }
    }
    int best_healing_tier = 0, best_mana_tier = 0;
    for (int slot = 0; slot < 4; ++slot) {
        const auto code = top_code[std::size_t(slot)];
        const int tier = touch_potion_tier(code);
        if (touch_potion_kind(code) == PotionKind::Healing && tier > best_healing_tier) {
            best_healing_tier = tier;
            result.strongest_healing_slot = slot;
        } else if (touch_potion_kind(code) == PotionKind::Mana && tier > best_mana_tier) {
            best_mana_tier = tier;
            result.strongest_mana_slot = slot;
        }
    }
    return result;
}

} // namespace d2d::client::touch
