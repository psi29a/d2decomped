// SPDX-License-Identifier: GPL-3.0-or-later
// NPC interaction menus: MonStats hcIdx -> menu entry string IDs (Cancel,
// string 0xeb5, is appended). game.exe 1.14d table at 0x726c48, 48 records
// of 39 bytes {u32 hcIdx, u32 entries incl. Cancel, u16 string[5],
// u32 handler[5], u8}, looked up by FUN_004b2e30 / built by FUN_004b4830.
// Strings: 0xd35 Talk, 0xd44 Trade, 0xd45 Hire, 0xd46 Gamble, 0xd06
// Trade/Repair, 0xd37 Go West, 0xd39 Sail West, 0xfb4 Identify Items.
// Dumped verbatim — see docs/research/re/npc-menu.md.
#pragma once

#include <array>
#include <cstdint>

namespace d2d::game {

struct NpcMenu { int hc_idx; std::array<std::uint16_t, 4> entries; };   // 0 = unused

inline const std::array<NpcMenu, 48> kNpcMenus = {{
    { 148, { 0xd35, 0xd44 } },   // akara
    { 176, { 0xd35 } },   // atma
    { 146, { 0xd35 } },   // cain1
    { 154, { 0xd35, 0xd06 } },   // charsi
    { 177, { 0xd35, 0xd44 } },   // drognan
    { 199, { 0xd35, 0xd44, 0xd46 } },   // elzix
    { 200, { 0xd35 } },   // geglash
    { 147, { 0xd35, 0xd44, 0xd46 } },   // gheed
    { 198, { 0xd35, 0xd45 } },   // greiz
    { 201, { 0xd35 } },   // jerhyn
    { 202, { 0xd35, 0xd44 } },   // lysander
    { 150, { 0xd35 } },   // kashya
    { 178, { 0xd35, 0xd06 } },   // fara
    { 155, { 0xd35 } },   // warriv1
    { 175, { 0xd35, 0xd37 } },   // warriv2
    { 210, { 0xd35 } },   // meshif1
    { 244, { 0xd35, 0xfb4 } },   // cain2
    { 265, { 0xd35, 0xfb4 } },   // cain5
    { 245, { 0xd35, 0xfb4 } },   // cain3
    { 246, { 0xd35, 0xfb4 } },   // cain4
    { 251, { 0xd35 } },   // tyrael1
    { 367, { 0xd35 } },   // tyrael2
    { 257, { 0xd06 } },   // halbu
    { 405, { 0xd44, 0xd46 } },   // jamella
    { 252, { 0xd35, 0xd45, 0xd44 } },   // asheara
    { 253, { 0xd35, 0xd06 } },   // hratli
    { 254, { 0xd35, 0xd44, 0xd46 } },   // alkor
    { 255, { 0xd35, 0xd44 } },   // ormus
    { 406, { 0xd35 } },   // izualghost
    { 257, { 0xd35 } },   // halbu
    { 264, { 0xd35, 0xd39 } },   // meshif2
    { 297, { 0xd35 } },   // natalya
    { 266, { 0xd35 } },   // navi
    { 331, { 0xd35 } },   // act2guard2
    { 408, { 0xd35 } },   // malachai
    { 378, { 0xd35 } },   // act2guard5
    { 377, { 0xd35 } },   // act2guard4
    { 521, { 0xd35 } },   // tyrael3
    { 520, { 0xd35, 0xfb4 } },   // cain6
    { 511, { 0xd35, 0xd06 } },   // larzuk
    { 513, { 0xd35, 0xd44 } },   // malah
    { 512, { 0xd35, 0xd44, 0xd46 } },   // drehya
    { 514, { 0xd35, 0xd46 } },   // nihlathak
    { 515, { 0xd35 } },   // qual-kehk
    { 527, { 0xd35 } },   // drehyaiced
    { 537, { 0xd35 } },   // ancientstatue1
    { 538, { 0xd35 } },   // ancientstatue2
    { 539, { 0xd35 } },   // ancientstatue3
}};

}  // namespace d2d::game
