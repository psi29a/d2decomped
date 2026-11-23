// SPDX-License-Identifier: GPL-3.0-or-later
// A composite's COF and timing, read from the MPQs: the World's part of a
// composite.
#pragma once

#include "gamedata.hpp"
#include "npc.hpp"

#include <cof.hpp>
#include <compcode.hpp>
#include <mpq.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::game {

// A composite's COF and timing (the World's part of a composite).
struct CofAnim { d2d::cof::Cof cof; GameData::AnimTiming timing; std::string path; bool ok = false; };
CofAnim open_cof(const d2d::mpq::Stack& mpqs, const std::string& path);
CofAnim player_cof(const d2d::mpq::Stack& mpqs, const std::vector<d2d::compcode::Entry>& comp, int cls, int mode,
                   const std::array<std::uint8_t, 32>& gfx);
CofAnim npc_cof(const d2d::mpq::Stack& mpqs, const Npc& npc, std::string_view mode);
// The weapon class in a monster's COF name.
std::string_view cof_weapon_class(const Npc& npc, std::string_view mode);

}  // namespace d2d::game
