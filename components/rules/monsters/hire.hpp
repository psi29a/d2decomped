// SPDX-License-Identifier: GPL-3.0-or-later
// Hiring a mercenary: the hire list, a merc's offer, its stats, its experience and its resurrection.
// The tables these read are rules.hpp's.
#pragma once

#include "rules.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::rules {

// A mercenary for hire, as FUN_006637f0 rolls it for the hire list.
struct MercOffer {
    int id = 0, level = 0, life = 0, str = 0, dex = 0, cost = 0, def = 0, dmg_min = 0, dmg_max = 0;
    std::uint32_t exp = 0, seed = 0;
    int name = 0;                                       // index from NameFirst
};

// A hire list's entry (FUN_00576070, SUNITNPC.CPP): one per name of the
// seller's (hireling.txt NameFirst..NameLast), its seed a step of the game
// seed; `offered` (+0xc) the ten picked for the list, `hired` (+8) taken.
struct HireEntry { int name = 0; std::uint32_t seed = 0; bool offered = false, hired = false; };

// The list: the entries in name order, each a game seed step, then ten picks of rand(names) on the game seed,
// each moving on past a taken entry (wrapping; back at its start the picking ends).
std::vector<HireEntry> hire_entries(int names, Rng& game);

// The seller's names for act (0-based), difficulty and version.
[[nodiscard]] int hire_names(const Tables& tables, bool expansion, int act, int diff);

// An entry still for hire (FUN_00577010 counts them: none left, the list's rolled anew).
[[nodiscard]] inline bool for_hire(const HireEntry& entry) { return entry.offered && !entry.hired; }

// An entry's offer for act (0-based) and difficulty (0-2), on its own seed {seed, 666}.
std::optional<MercOffer> merc_offer(const Tables& tables, bool expansion, int act, int diff, int clvl, const HireEntry& entry);

// The save's mercenary (hireling Id, experience) as it fights: its level
// is the highest whose experience ((level + 1) * Exp/Lvl * level^2, as
// hiring sets it) it has, its row the LoD band (Version 100) of that Id the
// last at or below that level (FUN_006562f0), its stats as the level-up
// sets them (FUN_00572840, d = level - the row's Level): life HP + HP/Lvl
// x d (40 at least), defence and attack rating + per level x d (0 at
// least), damage + Dmg/Lvl x d / 8 (min 0, max 1 at least: stats 0x17 /
// 0x18), strength and dexterity + per level x d / 8 (10 at least), each of its
// four resistances (stats 0x27 / 0x29 / 0x2b / 0x2d) Resist + Resist/Lvl x
// d / 4 (0 at least).
// Its worn items and the difficulty's resistance penalty go on in the fight
// (Fight::merc_fighter, merc_gear).
struct MercStats { int level = 1, life = 40, def = 0, dmg_min = 1, dmg_max = 2, attack_rating = 0, resist = 0, str = 10, dex = 10; };

MercStats merc_stats(const Tables& tables, int id, std::uint32_t exp);

// The merc's share of its owner's kill.
// `award` is the kill's experience at the merc's level;
std::uint32_t merc_kill_exp(std::uint32_t merc_exp, std::int64_t award, bool merc_blow, int merc_level, int owner_level);

// Hires `o`: pays its cost (carried gold, then the stash) and makes it
// the save's mercenary. False if it can't be paid for.
bool hire(const MercOffer& offer, d2d::d2s::Header& header, d2d::d2s::Stats& stats);

// A dead merc's resurrection: level^2 / 2 * 15, at most 50,000.
int merc_resurrect_cost(int merc_level);

// The NPCs that resurrect a merc (FUN_00579c00; the client's FUN_004b6440
// adds "resurrect", string 0x1507, to their menu while it's dead).
inline constexpr std::array kMercNpcs = { 150, 198, 252, 367, 515 };   // hcIdx: Kashya, Greiz, Asheara, Tyrael (act 4), Qual-Kehk

// Resurrects the save's dead merc at a hire NPC (FUN_00579c00): pays as
// FUN_00576d90 (carried gold, then the stash).
Trade resurrect_merc(int merc_level, d2d::d2s::Header& header, d2d::d2s::Stats& stats);

}  // namespace d2d::rules
