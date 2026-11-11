// SPDX-License-Identifier: GPL-3.0-or-later
// A unit's footprint height in subtiles — game.exe 1.14d, UNITS/Units.cpp
// (docs/research/re/units.md "Type data").
#pragma once

#include <cstdint>
#include <optional>

namespace d2d::rules {

// The unit's first dword.
enum class UnitType : int { kPlayer = 0, kMonster = 1, kObject = 2, kMissile = 3, kItem = 4, kTile = 5 };

// FUN_006205a0(unit), stdcall: by the unit's type, a player 2, an item 1;
// a monster its MonStats2 SizeY (byte +9, signed), an object its
// objects.txt SizeY (+0xd4), a missile its Missiles.txt Size (byte +0x18a,
// unsigned). `record` is that column, or nullopt when a monster's or a
// missile's class has no row (FUN_00451fe0 / FUN_0046ace0 answer 0): then
// 0, as for a tile or any other type. (An object's record is read without
// a check: game.exe has no "no row" for it.) (A null unit asserts, id 0x74b.) Its width twin is
// FUN_00620510 (SizeX: byte +8, +0xd0, the same byte +0x18a).
constexpr int unit_size_y(int type, std::optional<int> record) {
    switch (type) {
    case int(UnitType::kPlayer): return 2;
    case int(UnitType::kMonster): return record ? int(std::int8_t(*record)) : 0;
    case int(UnitType::kObject): return record ? *record : 0;
    case int(UnitType::kMissile): return record ? int(std::uint8_t(*record)) : 0;
    case int(UnitType::kItem): return 1;
    default: return 0;
    }
}

}  // namespace d2d::rules
