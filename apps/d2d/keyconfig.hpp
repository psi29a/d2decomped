// SPDX-License-Identifier: GPL-3.0-or-later
// game.exe's key bindings (.\UI\CmdTbl.cpp, docs/research/re/controls.md):
// 57 functions, each with two keys (slot 1, the first column, and slot 0),
// as 114 entries; the keys are Windows virtual-key codes, 0x100..0x104 the
// mouse's third, fourth and fifth buttons and its wheel. No SDL in here:
// controls.cpp maps SDL's keys onto these.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace d2d::client::keys {

// The functions (the action table at 0x712698, 12 bytes each).
enum Action : int {
    kCharacter = 0, kInventory, kParty, kMessageLog, kQuestLog, kChat, kHelp, kAutomap, kCenterAutomap, kFadeAutomap,
    kPartyOnAutomap, kNamesOnAutomap, kSkillTree, kSkillSpeedBar, kSkill1, kSkill8 = kSkill1 + 7, kShowBelt, kBelt1,
    kBelt4 = kBelt1 + 3, kSay1, kSay7 = kSay1 + 6, kRun, kToggleRun, kStandStill, kShowItems, kClearScreen,
    kPreviousSkill, kNextSkill, kClearMessages, kScreenShot, kShowPortraits, kSwapWeapons, kToggleMiniMap, kSkill9,
    kSkill16 = kSkill9 + 7, kHireling, kSayRetreat, kGameMenu, kCount, kSeparator = kCount,   // 0x39: a list's gap
};
// Functions with a release handler (the table's +4): Run, Stand Still and
// Show Items are held; Screen Shot fires on the release. The wheel can't
// be one of their keys (FUN_00469420).
[[nodiscard]] inline bool on_release(int action) { return action == kRun || action == kStandStill || action == kShowItems || action == kScreenShot; }

inline constexpr std::uint16_t kNone = 0xffff, kMouse3 = 0x100, kMouse4 = 0x101, kMouse5 = 0x102, kWheelUp = 0x103, kWheelDown = 0x104;
inline constexpr std::uint16_t kPrintScreen = 0x2c;

struct Entry {
    int action = 0;
    std::uint16_t key = kNone;
    int slot = 0;
    bool operator==(const Entry&) const = default;
};
inline constexpr std::size_t kEntries = 0x72;
using Table = std::array<Entry, kEntries>;

// The defaults (0x712220; the MPQ's data\local\CMD\ENG\default.key is
// version 0x24 and refused, FUN_004698d0).
[[nodiscard]] inline Table defaults() {
    // {action, slot 1's key, slot 0's key}, in the table's order.
    static constexpr std::array<std::array<std::uint16_t, 3>, kEntries / 2> kKeys = { {
        { kCharacter, 'A', 'C' }, { kInventory, 'I', 'B' }, { kParty, 'P', kNone }, { kMessageLog, 'M', kNone },
        { kQuestLog, 'Q', kNone }, { kChat, 0x0d, kNone }, { kHelp, 'H', kNone }, { kAutomap, 0x09, kMouse3 },
        { kCenterAutomap, 0x78, kNone }, { kFadeAutomap, 0x79, kNone }, { kPartyOnAutomap, 0x7a, kNone },
        { kNamesOnAutomap, 0x7b, kNone }, { kSkillTree, 'T', kNone }, { kSkillSpeedBar, 'S', kNone },
        { kSkill1, 0x70, kNone }, { kSkill1 + 1, 0x71, kNone }, { kSkill1 + 2, 0x72, kNone }, { kSkill1 + 3, 0x73, kNone },
        { kSkill1 + 4, 0x74, kNone }, { kSkill1 + 5, 0x75, kNone }, { kSkill1 + 6, 0x76, kNone },
        { kShowBelt, 0xc0, kNone }, { kBelt1, '1', kNone }, { kBelt1 + 1, '2', kNone }, { kBelt1 + 2, '3', kNone },
        { kBelt1 + 3, '4', kNone }, { kSay1, 0x60, kNone }, { kSay1 + 1, 0x61, kNone }, { kSay1 + 2, 0x62, kNone },
        { kSay1 + 3, 0x63, kNone }, { kSay1 + 4, 0x64, kNone }, { kSay1 + 5, 0x65, kNone }, { kSay1 + 6, 0x66, kNone },
        { kRun, 0x11, kNone }, { kToggleRun, 'R', kMouse5 }, { kStandStill, 0x10, kNone }, { kShowItems, 0x12, kMouse4 },
        { kClearScreen, 0x20, kNone }, { kPreviousSkill, kWheelUp, kNone }, { kNextSkill, kWheelDown, kNone },
        { kClearMessages, 'N', kNone }, { kScreenShot, kPrintScreen, kNone }, { kShowPortraits, 'Z', kNone },
        { kSwapWeapons, 'W', kNone }, { kToggleMiniMap, 'V', kNone }, { kSkill8, 0x77, kNone },
        { kSkill9, kNone, kNone }, { kSkill9 + 1, kNone, kNone }, { kSkill9 + 2, kNone, kNone }, { kSkill9 + 3, kNone, kNone },
        { kSkill9 + 4, kNone, kNone }, { kSkill9 + 5, kNone, kNone }, { kSkill9 + 6, kNone, kNone }, { kSkill16, kNone, kNone },
        { kHireling, 'O', kNone }, { kSayRetreat, 0x67, kNone }, { kGameMenu, 0x1b, kNone },
    } };
    Table table{};
    for (std::size_t i = 0; i < kKeys.size(); ++i) {
        table[2 * i] = { kKeys[i][0], kKeys[i][1], 1 };
        table[2 * i + 1] = { kKeys[i][0], kKeys[i][2], 0 };
    }
    std::swap(table[2], table[3]);                 // Inventory's slot 0 comes first there
    return table;
}

// The key of (action, slot), or kNone (FUN_00469aa0).
[[nodiscard]] inline std::uint16_t key_of(const Table& table, int action, int slot) {
    for (const auto& entry : table) if (entry.action == action && entry.slot == slot) return entry.key;
    return kNone;
}
// The functions `key` sets off.
[[nodiscard]] inline std::vector<int> actions_of(const Table& table, std::uint16_t key) {
    std::vector<int> out;
    if (key != kNone) for (const auto& entry : table) if (entry.key == key) out.push_back(entry.action);
    return out;
}
[[nodiscard]] inline bool bound(const Table& table, int action, std::uint16_t key) {
    return key != kNone && std::ranges::any_of(table, [&](const Entry& entry) { return entry.action == action && entry.key == key; });
}

// Whether a key can be given at all (FUN_00469ae0): the mouse's own
// buttons, Escape, the arrows, Insert's neighbours, the Windows keys and
// a few more can't.
[[nodiscard]] inline bool assignable(std::uint16_t key) {
    if (key >= 0x100) return key <= kWheelDown;
    switch (key) {
        case 1: case 2: case 3: case 4: case 0x15: case 0x17: case 0x18: case 0x19: case 0x1b: case 0x1c: case 0x1d:
        case 0x1e: case 0x1f: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2b: case 0x5b: case 0x5c:
        case 0x5d: case 0x90:
            return false;
        default: break;
    }
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) return true;
    return key == 8 || key == 9 || key == 0xc || key == 0xd || (key >= 0x10 && key <= 0x14) || (key >= 0x20 && key <= 0x24)
        || key == 0x2a || (key >= 0x2c && key <= 0x2f) || (key >= 0x60 && key <= 0x87) || key == 0x91 || (key >= 0xba && key <= 0xc0)
        || (key >= 0xdb && key <= 0xde);
}

// Messages (string ids) an assignment can give.
inline constexpr std::uint16_t kCannotWheel = 0xf8a, kCannotKey = 0xf8b;

// Give (action, slot) `key` (FUN_00469c20): the key leaves whatever had
// it. False (with `message` 0 or a string id) if it can't; then the key
// stays where it was.
inline bool assign(Table& table, int action, int slot, std::uint16_t key, std::uint16_t& message) {
    message = 0;
    if (action >= kGameMenu) return false;
    if (!assignable(key)) { message = kCannotKey; return false; }
    if ((key == kWheelUp || key == kWheelDown) && on_release(action)) { message = kCannotWheel; return false; }
    if (key == kPrintScreen && action != kScreenShot) { message = kCannotKey; return false; }   // only Screen Shot (its press handler is null)
    std::size_t taken = kEntries;
    for (std::size_t i = 0; i < kEntries; ++i)
        if (table[i].key == key) { table[i].key = kNone; taken = i; break; }
    for (auto& entry : table)
        if (entry.action == action && entry.slot == slot) { entry.key = key; return true; }
    if (taken < kEntries) table[taken].key = key;
    return false;
}
// The Delete key's clear (FUN_00469d70).
inline void clear(Table& table, int action, int slot) {
    for (auto& entry : table) if (entry.action == action && entry.slot == slot) entry.key = kNone;
}

// A character's <name>.key (0x476 bytes: version 0x25, then the table,
// FUN_00469780) and its check when a game starts (FUN_0046aae0): every
// function is there and no key is on two of them.
inline constexpr std::uint16_t kVersion = 0x25;
[[nodiscard]] inline std::vector<std::uint8_t> write_table(const Table& table) {
    std::vector<std::uint8_t> out;
    const auto put = [&](std::uint32_t value, int bytes) { for (int i = 0; i < bytes; ++i) out.push_back(std::uint8_t(value >> (8 * i))); };
    for (const auto& entry : table) { put(std::uint32_t(entry.action), 4); put(entry.key, 2); put(std::uint32_t(entry.slot), 4); }
    return out;
}
[[nodiscard]] inline std::optional<Table> read_table(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != kEntries * 10) return std::nullopt;
    const auto get = [&](std::size_t offset, int count) { std::uint32_t value = 0; for (int i = 0; i < count; ++i) value |= std::uint32_t(bytes[offset + std::size_t(i)]) << (8 * i); return value; };
    Table table{};
    for (std::size_t i = 0; i < kEntries; ++i) table[i] = { int(get(10 * i, 4)), std::uint16_t(get(10 * i + 4, 2)), int(get(10 * i + 6, 4)) };
    return table;
}
[[nodiscard]] inline bool valid(const Table& table) {
    for (int action = 0; action < kCount; ++action)
        if (std::ranges::none_of(table, [&](const Entry& entry) { return entry.action == action; })) return false;
    for (std::size_t i = 0; i < kEntries; ++i)
        for (std::size_t j = 0; j < kEntries; ++j)
            if (i != j && table[i].key != kNone && table[i].key == table[j].key) return false;
    return true;
}
[[nodiscard]] inline std::vector<std::uint8_t> write_character(const Table& table) {
    std::vector<std::uint8_t> out = { std::uint8_t(kVersion), 0 };
    const auto body = write_table(table);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
[[nodiscard]] inline std::optional<Table> read_character(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 2 + kEntries * 10 || (bytes[0] | bytes[1] << 8) != kVersion) return std::nullopt;
    auto table = read_table(bytes.subspan(2));
    return table && valid(*table) ? table : std::nullopt;
}
// The save folder's default.key ("WS", 0x25, 0x47a, the table): the last
// table accepted, the start for a character with no .key of its own.
[[nodiscard]] inline std::vector<std::uint8_t> write_default(const Table& table) {
    std::vector<std::uint8_t> out = { 'W', 'S', std::uint8_t(kVersion), 0, 0x7a, 0x04 };
    const auto body = write_table(table);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
[[nodiscard]] inline std::optional<Table> read_default(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 6 + kEntries * 10 || bytes[0] != 'W' || bytes[1] != 'S' || (bytes[2] | bytes[3] << 8) != kVersion
        || (bytes[4] | bytes[5] << 8) != 0x47a) return std::nullopt;
    return read_table(bytes.subspan(6, kEntries * 10));
}

// A key's name: a string id (0xeb2: unassigned), or 0 when the name is
// the key's own character (FUN_00469de0).
[[nodiscard]] inline std::uint16_t name_id(std::uint16_t key) {
    if (key == kNone) return 0xeb2;
    if (key >= 0x100 && key <= kWheelDown) return std::uint16_t(0xeb6 + (key - 0x100));
    switch (key) {
        case 1: return 0xeb3; case 2: return 0xeb4; case 3: return 0xeb5; case 4: return 0xeb6;
        case 8: return 0xece; case 9: return 0xecf; case 0xc: return 0xed0; case 0xd: return 0xed1;
        case 0x15: return 0xebb; case 0x17: return 0xebc; case 0x18: return 0xebd; case 0x19: return 0xebe;
        case 0x2b: return 0xec9; case 0x90: return 0xecd; case 0x91: return 0xf09; case 0xc0: return 0xf10;
        default: break;
    }
    if (key >= 0x10 && key <= 0x14) return std::uint16_t(0xed2 + (key - 0x10));
    if (key >= 0x1b && key <= 0x1f) return std::uint16_t(0xebf + (key - 0x1b));
    if (key >= 0x20 && key <= 0x24) return std::uint16_t(0xed7 + (key - 0x20));
    if (key >= 0x25 && key <= 0x29) return std::uint16_t(0xec4 + (key - 0x25));
    if (key == 0x2a) return 0xedc;
    if (key >= 0x2c && key <= 0x2f) return std::uint16_t(0xedd + (key - 0x2c));
    if (key >= 0x5b && key <= 0x5d) return std::uint16_t(0xeca + (key - 0x5b));
    if (key >= 0x60 && key <= 0x87) return std::uint16_t(0xee1 + (key - 0x60));
    if (key >= 0xba && key <= 0xbf) return std::uint16_t(0xf0a + (key - 0xba));
    if (key >= 0xdb && key <= 0xde) return std::uint16_t(0xf11 + (key - 0xdb));
    return 0;
}

// The screen's rows (classic 0x724268, 0x33; expansion 0x724468, 0x3e):
// {function or kSeparator, its name's string id}.
struct Row { int action; std::uint16_t name; };
[[nodiscard]] inline std::span<const Row> rows(bool expansion) {
    static constexpr Row kClassic[] = {
        { 0, 0xf54 }, { 1, 0xf55 }, { 2, 0xf56 }, { 3, 0xf57 }, { 4, 0xf58 }, { 6, 0xf5d }, { kSeparator, 0 },
        { 0xc, 0xf5e }, { 0xd, 0x2a51 }, { 0xe, 0xf60 }, { 0xf, 0xf61 }, { 0x10, 0xf62 }, { 0x11, 0xf63 }, { 0x12, 0xf64 },
        { 0x13, 0xf65 }, { 0x14, 0xf66 }, { 0x15, 0xf67 }, { 0x27, 0xf68 }, { 0x28, 0xf69 }, { kSeparator, 0 },
        { 0x16, 0xf6a }, { 0x17, 0xf6b }, { 0x18, 0xf6c }, { 0x19, 0xf6d }, { 0x1a, 0xf6e }, { kSeparator, 0 },
        { 5, 0xf59 }, { 0x22, 0xf7e }, { 0x23, 0xf7f }, { 0x24, 0xf80 }, { 0x25, 0xf81 }, { 0x2b, 0xf8e }, { kSeparator, 0 },
        { 7, 0xf5a }, { 8, 0xf5b }, { 9, 0xf8f }, { 0xa, 0xf91 }, { 0xb, 0xf90 }, { kSeparator, 0 },
        { 0x1b, 0xf77 }, { 0x1c, 0xf78 }, { 0x1d, 0xf79 }, { 0x1e, 0xf7a }, { 0x1f, 0xf7b }, { 0x20, 0xf7c }, { 0x21, 0xf7d },
        { 0x37, 0x2b4b }, { kSeparator, 0 }, { 0x2a, 0xf83 }, { 0x26, 0xf82 }, { 0x29, 0xf8d },
    };
    static constexpr Row kExpansion[] = {
        { 0, 0xf54 }, { 1, 0xf55 }, { 2, 0xf56 }, { 0x36, 0x58c7 }, { 3, 0xf57 }, { 4, 0xf58 }, { 6, 0xf5d }, { kSeparator, 0 },
        { 0xc, 0xf5e }, { 0xd, 0x2a51 }, { 0xe, 0xf60 }, { 0xf, 0xf61 }, { 0x10, 0xf62 }, { 0x11, 0xf63 }, { 0x12, 0xf64 },
        { 0x13, 0xf65 }, { 0x14, 0xf66 }, { 0x15, 0xf67 }, { 0x2e, 0x58bd }, { 0x2f, 0x58be }, { 0x30, 0x58bf }, { 0x31, 0x58c0 },
        { 0x32, 0x58c1 }, { 0x33, 0x58c2 }, { 0x34, 0x58c3 }, { 0x35, 0x58c4 }, { 0x27, 0xf68 }, { 0x28, 0xf69 }, { kSeparator, 0 },
        { 0x16, 0xf6a }, { 0x17, 0xf6b }, { 0x18, 0xf6c }, { 0x19, 0xf6d }, { 0x1a, 0xf6e }, { 0x2c, 0x58c6 }, { kSeparator, 0 },
        { 5, 0xf59 }, { 0x22, 0xf7e }, { 0x23, 0xf7f }, { 0x24, 0xf80 }, { 0x25, 0xf81 }, { 0x2b, 0xf8e }, { kSeparator, 0 },
        { 7, 0xf5a }, { 8, 0xf5b }, { 9, 0xf8f }, { 0xa, 0xf91 }, { 0xb, 0xf90 }, { 0x2d, 0x58c5 }, { kSeparator, 0 },
        { 0x1b, 0xf77 }, { 0x1c, 0xf78 }, { 0x1d, 0xf79 }, { 0x1e, 0xf7a }, { 0x1f, 0xf7b }, { 0x20, 0xf7c }, { 0x21, 0xf7d },
        { 0x37, 0x2b4b }, { kSeparator, 0 }, { 0x2a, 0xf83 }, { 0x26, 0xf82 }, { 0x29, 0xf8d },
    };
    if (expansion) return kExpansion;
    return kClassic;
}

}  // namespace d2d::client::keys
