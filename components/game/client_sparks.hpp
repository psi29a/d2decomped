// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Client do 8 (FUN_004d38d0, table 0x72a398 + 4 x 8), the lightning rows'
// (lightningbolt, chainlightning, the sentries', the wisps', mephlight...).
// The bolt itself shows little or nothing (lightningbolt's CelFile is null):
// what's seen is the trail it lays. Each client frame, from age InitSteps
// (+0x135, FUN_0064a3b0) on and while path flag 8 is up (FUN_006505c0), it
// makes CltParam1 (+0x58, at least 1) client missiles of row CltSubMissile1
// (+0x1e: lightninghit) where it is (FUN_0045adf0 / FUN_0045ae20;
// FUN_004cd540, type 1). Path flag 8: the last step entered a new subtile
// (the step FUN_00650150 sets it when it crossed one, 0x65029e; the move
// FUN_00650840 clears it first, 0x650855), so nothing is laid before the
// bolt's first step, at the caster. The callback FUN_004cc870 sets each up:
// motion z rand(CltParam2) - CltParam2 / 2 (the child row's +0x5c,
// lightninghit's 16 "Z Range"; 0 when that's under 1; FUN_004da1d0), the
// parent's direction (FUN_006487f0 -> FUN_006488a0) and its frame
// rand(AnimLen) (+0x178) << 8 into +0x44. It lives its row's Range in frames
// (FUN_004d30c0 counts it down). FUN_0045c3e0 is rules::Rng's roll.

#include "world.hpp"

#include <rules.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace d2d::game {

struct ClientSpark {
    View::Shot shot;            // born: its first frame's start, so its frame rand(AnimLen) is up at once
    std::uint32_t dies = 0;     // its Range spent
};

// The client's trails. ponytail: one client rng for all; game.exe rolls the
// z from the parent unit's seed (+0x20) and the frame from the child's,
// whose first seed (FUN_004cd540 -> FUN_00466730) isn't traced.
struct SparkTrail {
    std::vector<ClientSpark> sparks;
    d2d::rules::Rng rng{ 0x1234 };
    // Each client do 8 missile's subtile at the last client frame: path flag
    // 8 is a change of it. The client has no missile ids, so a missile is its
    // row and birth, the nearest of those if there are several.
    struct Seen { const GameData::MissileInfo* info = nullptr; std::uint32_t born = 0; int subtile_x = 0, subtile_y = 0; };
    std::vector<Seen> seen;

    // A new level or game: nothing laid there carries over.
    void clear() { sparks.clear(); seen.clear(); }

    // One client frame at `now_ms`: the spent sparks go, then each client do
    // 8 missile that stepped into a new subtile lays its own.
    void tick(std::span<const View::Shot> missiles, std::uint32_t now_ms) {
        std::erase_if(sparks, [&](const ClientSpark& spark) { return now_ms >= spark.dies; });
        std::vector<Seen> now_seen;
        const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
        for (const auto& missile : missiles) {
            const auto* info = missile.info;
            if (!info || info->clt_do != 8 || !info->clt_sub_info) continue;
            const Seen here{ info, missile.born, subtile(missile.x), subtile(missile.y) };
            now_seen.push_back(here);
            const Seen* before = nullptr;
            for (const auto& was : seen)
                if (was.info == info && was.born == missile.born
                    && (!before || std::abs(was.subtile_x - here.subtile_x) + std::abs(was.subtile_y - here.subtile_y)
                                       < std::abs(before->subtile_x - here.subtile_x) + std::abs(before->subtile_y - here.subtile_y))) before = &was;
            const bool crossed = before && (before->subtile_x != here.subtile_x || before->subtile_y != here.subtile_y);
            if (crossed && int(now_ms - missile.born) / 40 >= info->init_steps) lay(missile, now_ms);
        }
        seen = std::move(now_seen);
    }

private:
    void lay(const View::Shot& missile, std::uint32_t now_ms) {
        const auto& sub = *missile.info->clt_sub_info;
        const int z_range = sub.clt_params[1];
        const std::uint32_t ms_per_frame = 40u * 256u / (std::uint32_t(sub.anim_speed) * 16u);   // AnimSpeed in 16ths of a frame a tick
        for (int made = 0; made < std::max(missile.info->clt_params[0], 1); ++made) {
            const int z_offset = z_range > 0 ? rng(z_range) - z_range / 2 : 0;
            const int frame = rng(sub.anim_len);
            View::Shot shot{ &sub, missile.x, missile.y, missile.dir, now_ms - std::uint32_t(frame) * ms_per_frame };
            shot.motion_z = z_offset;
            sparks.push_back({ shot, now_ms + std::uint32_t(std::max(sub.range, 1)) * 40u });
        }
    }
};

}  // namespace d2d::game
