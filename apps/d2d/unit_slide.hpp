// SPDX-License-Identifier: GPL-3.0-or-later
// Units drawn sliding between the World's 25 Hz ticks (deviations.md #13):
// where each was at the tick before, and the View moved to the blend while
// it's drawn.
#pragma once

#include "game_api.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::client {

// The View's units at the tick before. A unit with a stable id (a monster,
// another player, the merc, a town NPC by its place in the level's list) is
// found by kind and id; one without (a pet, a missile) by the nearest of
// its kind (a missile: of its birth tick), each taken once, so one dying
// doesn't move the others' starts.
struct SlideFrom {
    struct Loose { int kind; std::uint32_t tag; float x, y; };
    std::unordered_map<std::uint64_t, std::pair<float, float>> keyed;
    std::vector<Loose> loose;
    void clear() { keyed.clear(); loose.clear(); }
};

enum SlideKind : int { kSlideMonster = 1, kSlideOther, kSlideMerc, kSlidePet, kSlidePartyPet, kSlideMissile, kSlideNpc };

// Each unit of a View that slides: its kind, its id (keyed) or tag (loose),
// and how far it may move in a tick before it counts as a jump.
template <class Visit>
void for_each_slide(View& view, Visit&& visit) {
    for (auto& monster : view.monsters) visit(kSlideMonster, std::uint32_t(monster.id), true, monster.unit.x, monster.unit.y, 2.f);
    for (auto& other : view.others) visit(kSlideOther, other.id, true, other.unit.x, other.unit.y, 2.f);
    if (view.merc) visit(kSlideMerc, 0u, true, view.merc->unit.x, view.merc->unit.y, 2.f);
    for (std::size_t i = 0; i < view.npc_states.size(); ++i) visit(kSlideNpc, std::uint32_t(i), true, view.npc_states[i].x, view.npc_states[i].y, 2.f);
    for (auto& pet : view.pets) visit(kSlidePet, 0u, false, pet.unit.x, pet.unit.y, 2.f);
    for (auto& pet : view.party_pets) visit(kSlidePartyPet, 0u, false, pet.unit.x, pet.unit.y, 2.f);
    for (auto& shot : view.missiles) visit(kSlideMissile, shot.born, false, shot.x, shot.y, 4.f);   // a missile flies further a tick
}

// Where the View has its units now: the next slide's start.
inline void remember_slides(View& view, SlideFrom& from) {
    from.clear();
    for_each_slide(view, [&](int kind, std::uint32_t id, bool keyed, float x, float y, float) {
        if (keyed) from.keyed[std::uint64_t(kind) << 32 | id] = { x, y };
        else from.loose.push_back({ kind, id, x, y });
    });
}

// A unit moved to its blend, and where to put it back.
struct Slid { float* x; float* y; float at_x, at_y; };

// Moves each unit `from` knows to `blend` (0 its last place, 1 its new
// one); a jump past its reach, or a unit `from` doesn't know, stays put.
inline std::vector<Slid> slide_units(View& view, const SlideFrom& from, float blend) {
    std::vector<Slid> slid;
    std::vector<bool> taken(from.loose.size());
    std::pair<float, float> loose_start;
    for_each_slide(view, [&](int kind, std::uint32_t id, bool keyed, float& x, float& y, float reach) {
        const std::pair<float, float>* start = nullptr;
        if (keyed) {
            const auto found = from.keyed.find(std::uint64_t(kind) << 32 | id);
            if (found != from.keyed.end() && std::hypot(x - found->second.first, y - found->second.second) <= reach) start = &found->second;
        } else {
            std::size_t best = taken.size();
            float best_apart = std::numeric_limits<float>::max();
            for (std::size_t i = 0; i < from.loose.size(); ++i) {
                const auto& was = from.loose[i];
                if (taken[i] || was.kind != kind || was.tag != id) continue;
                if (const float apart = std::hypot(x - was.x, y - was.y); apart <= reach && apart < best_apart) { best = i; best_apart = apart; }
            }
            if (best == taken.size()) return;
            taken[best] = true;
            loose_start = { from.loose[best].x, from.loose[best].y };
            start = &loose_start;
        }
        if (!start) return;
        slid.push_back({ &x, &y, x, y });
        x = start->first + (x - start->first) * blend;
        y = start->second + (y - start->second) * blend;
    });
    return slid;
}

inline void unslide(const std::vector<Slid>& slid) {
    for (const auto& unit_at : slid) { *unit_at.x = unit_at.at_x; *unit_at.y = unit_at.at_y; }
}

// Puts the View's units back where the tick has them when it goes out of
// scope: after everything the frame draws or lights from them.
struct SlidGuard {
    std::vector<Slid> slid;
    SlidGuard() = default;
    explicit SlidGuard(std::vector<Slid> moved) : slid(std::move(moved)) {}
    SlidGuard(const SlidGuard&) = delete;
    SlidGuard& operator=(const SlidGuard&) = delete;
    ~SlidGuard() { unslide(slid); }
};

}  // namespace d2d::client
