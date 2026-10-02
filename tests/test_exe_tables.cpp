// SPDX-License-Identifier: GPL-3.0-or-later
// Every constant table copied from game.exe 1.14d, checked element-wise
// against the real game.exe (1.14.3.71). The tables stay in the code; this
// only reads the exe at test time. game.exe: $D2_GAME_EXE, else
// $D2_MPQ_DIR/bin/game.exe, else ~/Workspace/private/diablo2/bin/game.exe;
// none (or another version) prints "skip" and passes.
// RULE: every table copied from game.exe gets an entry in kTables below.
#include <install.hpp>

#include <compcode.hpp>
#include <item_text.hpp>
#include <light.hpp>
#include <maze.hpp>
#include <monsters.hpp>
#include <npc_menu.hpp>
#include <npc_talk.hpp>
#include <obj_preset.hpp>
#include <quests.hpp>
#include <room_tiles.hpp>
#include <rules.hpp>
#include <sequences.hpp>
#include <uniques.hpp>

#include "panels.hpp"
#include "speech_sound.hpp"
#include "store.hpp"
#include "ui.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// game.exe in memory, read by virtual address through its PE section headers.
struct Exe {
    std::vector<std::uint8_t> bytes;
    struct Section { std::uint32_t va, size, raw; };
    std::vector<Section> sections;
    std::uint32_t base = 0;

    std::uint32_t raw32(std::size_t at) const {
        return std::uint32_t(bytes.at(at) | bytes.at(at + 1) << 8 | bytes.at(at + 2) << 16 | std::uint32_t(bytes.at(at + 3)) << 24);
    }
    explicit Exe(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), {});
        const std::size_t pe = raw32(0x3c);
        const std::size_t count = bytes.at(pe + 6) | bytes.at(pe + 7) << 8;
        const std::size_t optional = bytes.at(pe + 20) | bytes.at(pe + 21) << 8;
        base = raw32(pe + 24 + 28);
        for (std::size_t i = 0, at = pe + 24 + optional; i < count; ++i, at += 40)
            sections.push_back({ base + raw32(at + 12), raw32(at + 16), raw32(at + 20) });
    }
    // File offset of `va` (the section's raw data only: bss has none).
    std::size_t offset(std::uint32_t va) const {
        for (const auto& section : sections)
            if (va >= section.va && va - section.va < section.size) return section.raw + (va - section.va);
        std::printf("0x%x is in no section's file data\n", va);
        std::exit(1);
    }
    std::uint32_t u32(std::uint32_t va) const { return raw32(offset(va)); }
    int i32(std::uint32_t va) const { return int(u32(va)); }
    int u16(std::uint32_t va) const { const auto at = offset(va); return bytes.at(at) | bytes.at(at + 1) << 8; }
    int u8(std::uint32_t va) const { return bytes.at(offset(va)); }
    int s8(std::uint32_t va) const { return std::int8_t(bytes.at(offset(va))); }
};

const char* g_table = "";
int g_values = 0, g_bad = 0;

using ll = long long;
void eq(std::size_t index, ll ours, ll exe) {
    ++g_values;
    if (ours == exe) return;
    std::printf("MISMATCH %s[%zu]: ours %lld, game.exe %lld\n", g_table, index, ours, exe);
    ++g_bad;
}

// A 3-char item code against the 4 bytes at `va`.
void eq_code(std::size_t index, const char* ours, const Exe& exe, std::uint32_t va) {
    for (std::uint32_t c = 0; c < 3; ++c) eq(index * 3 + c, ours[c], exe.u8(va + c));
}

// Quest talk blocks: 16 {npc, string, flag} then a count, 196 bytes a block.
template <class Blocks>
void quest_blocks(const Exe& exe, std::uint32_t address, const Blocks& blocks) {
    std::size_t index = 0;
    for (const auto& block : blocks) {
        const std::uint32_t at = address + std::uint32_t(196 * (&block - &blocks[0]));
        eq(index++, ll(block.size()), exe.i32(at + 192));
        for (std::uint32_t i = 0; i < block.size(); ++i) {
            eq(index++, block[i].npc, exe.i32(at + 12 * i));
            eq(index++, block[i].string, exe.i32(at + 12 * i + 4));
            eq(index++, block[i].greet, exe.i32(at + 12 * i + 8) != 2);
        }
    }
}

using namespace d2d;

struct ExeTable {
    const char* name;
    std::uint32_t address;
    void (*check)(const Exe& exe, std::uint32_t address);
};

// One entry per table copied from game.exe 1.14d. A newly copied table
// MUST be added here (tools/ghidra/README.md).
const ExeTable kTables[] = {
    // --- apps/d2d ---
    { "client::kCelGroups", 0x711258, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < std::size(client::kCelGroups); ++i)
            for (std::uint32_t k = 0; k < 2; ++k) eq(2 * i + k, client::kCelGroups[i][k], exe.i32(a + 8 * i + 4 * k));
    } },
    { "client::kMark", 0x6d6638, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 13; ++i)
            for (std::uint32_t k = 0; k < 2; ++k) eq(2 * i + k, client::kMark[i][k], exe.i32(a + 8 * i + 4 * k));
    } },
    { "client::kCharLabels", 0x724818, [](const Exe& exe, std::uint32_t a) {   // 18-byte {x0, y, x1, ?, u16 id}
        for (std::uint32_t i = 0; i < std::size(client::kCharLabels); ++i) {
            const auto& label = client::kCharLabels[i];
            const std::uint32_t at = a + 18 * i;
            eq(4 * i, label.left, exe.i32(at)); eq(4 * i + 1, label.y, exe.i32(at + 4));
            eq(4 * i + 2, label.right, exe.i32(at + 8)); eq(4 * i + 3, label.id, exe.u16(at + 12));
        }
    } },
    { "client::kCharValues", 0x724928, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < std::size(client::kCharValues); ++i) {
            const auto& value = client::kCharValues[i];
            const std::uint32_t at = a + 16 * i;
            eq(4 * i, value.left, exe.i32(at)); eq(4 * i + 1, value.y, exe.i32(at + 4));
            eq(4 * i + 2, value.right, exe.i32(at + 8)); eq(4 * i + 3, value.id, exe.i32(at + 12));
        }
    } },
    { "client::kAttackBlock", 0x72d840, [](const Exe& exe, std::uint32_t a) {   // {x0, y, x1}; id unused
        for (std::uint32_t i = 0; i < std::size(client::kAttackBlock); ++i) {
            const auto& block = client::kAttackBlock[i];
            eq(3 * i, block.left, exe.i32(a + 12 * i)); eq(3 * i + 1, block.y, exe.i32(a + 12 * i + 4));
            eq(3 * i + 2, block.right, exe.i32(a + 12 * i + 8));
        }
    } },
    { "client::kStatButtons", 0x724a48, [](const Exe& exe, std::uint32_t a) {   // 14-byte {x, y, pressed, u16 stat}
        for (std::uint32_t i = 0; i < 4; ++i) {
            const auto& button = client::kStatButtons[i];
            eq(3 * i, button.x, exe.i32(a + 14 * i)); eq(3 * i + 1, button.y, exe.i32(a + 14 * i + 4));
            eq(3 * i + 2, button.stat, exe.u16(a + 14 * i + 12));
        }
    } },
    { "client::kWpIconBottom/kWpTextBase/kWpHitTop", 0x7224e8, [](const Exe& exe, std::uint32_t a) {   // 9 rows of 6 ints: columns 1, 3, 5
        for (std::uint32_t i = 0; i < 9; ++i) {
            eq(3 * i, client::kWpIconBottom[i], exe.i32(a + 24 * i + 4));
            eq(3 * i + 1, client::kWpTextBase[i], exe.i32(a + 24 * i + 12));
            eq(3 * i + 2, client::kWpHitTop[i], exe.i32(a + 24 * i + 20));
        }
    } },
    { "client::kQuestLog", 0x723f30, [](const Exe& exe, std::uint32_t a) {   // by quest: {shown, icon, slot, act, name*, ?, quest}
        for (std::size_t i = 0; i < client::kQuestLog.size(); ++i) {
            const auto& entry = client::kQuestLog[i];
            const std::uint32_t at = a + 16 * std::uint32_t(entry.quest);
            eq(6 * i, 1, exe.u8(at) != 0);
            eq(6 * i + 1, entry.icon, exe.u8(at + 1)); eq(6 * i + 2, entry.slot, exe.u8(at + 2));
            eq(6 * i + 3, entry.act, exe.u8(at + 3)); eq(6 * i + 4, entry.name, exe.u16(exe.u32(at + 4)));
            eq(6 * i + 5, entry.quest, exe.i32(at + 12));
        }
    } },
    { "client::kQuestSlot", 0x723ea8, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 6; ++i) {
            eq(2 * i, client::kQuestSlot[i].first, exe.i32(a + 16 * i));
            eq(2 * i + 1, client::kQuestSlot[i].second, exe.i32(a + 16 * i + 4));
        }
    } },
    { "client::kTabLabelX/kTabString", 0x722110, [](const Exe& exe, std::uint32_t a) {   // 18-byte {x, ?, u16 string}
        for (std::uint32_t i = 0; i < 4; ++i) {
            eq(2 * i, client::kTabLabelX[i], exe.i32(a + 18 * i));
            eq(2 * i + 1, client::kTabString[i], exe.u16(a + 18 * i + 8));
        }
    } },
    { "client::kClassPos", 0x70af30, [](const Exe& exe, std::uint32_t) {   // scattered 48-byte records, {x, y, w, h} at +4
        constexpr std::uint32_t kRecord[7] = { 0x70af30, 0x70afc0, 0x70b020, 0x70b050, 0x70aff0, 0x70b470, 0x70b440 };
        for (std::uint32_t i = 0; i < 7; ++i) {
            const auto& pos = client::kClassPos[i];
            eq(4 * i, pos.x, exe.i32(kRecord[i] + 4)); eq(4 * i + 1, pos.y, exe.i32(kRecord[i] + 8));
            eq(4 * i + 2, pos.width, exe.i32(kRecord[i] + 12)); eq(4 * i + 3, pos.height, exe.i32(kRecord[i] + 16));
        }
    } },
    { "client::kSpeechSound", 0x72b0e0, [](const Exe& exe, std::uint32_t a) {   // {u32 sound, u32 string} to a zero sound; ours (string, sound)
        for (std::uint32_t i = 0; i < client::kSpeechSound.size(); ++i) {
            eq(2 * i, client::kSpeechSound[i].first, exe.u32(a + 8 * i + 4));
            eq(2 * i + 1, client::kSpeechSound[i].second, exe.u32(a + 8 * i));
        }
        eq(2 * client::kSpeechSound.size(), 0, exe.u32(a + 8 * std::uint32_t(client::kSpeechSound.size())));   // the end
    } },
    // --- components/game ---
    { "game::kNpcTalk", 0x726850, [](const Exe& exe, std::uint32_t a) {   // 22-byte records; ours skips those without topics
        std::size_t ours = 0, index = 0;
        for (std::uint32_t r = 0; r < std::uint32_t(exe.i32(0x72554c)); ++r) {
            const std::uint32_t at = a + 22 * r;
            const std::uint32_t count = exe.u32(at + 9);
            if (count == 0) continue;
            if (ours >= game::kNpcTalk.size()) { eq(index++, -1, exe.i32(at)); continue; }
            const auto& talk = game::kNpcTalk[ours++];
            eq(index++, talk.hc_idx, exe.i32(at)); eq(index++, talk.act, exe.u8(at + 4));
            eq(index++, talk.no_intro, exe.u8(at + 20)); eq(index++, ll(talk.topics.size()), count);
            for (std::uint32_t t = 0; t < talk.topics.size() && t < count; ++t) {
                const std::uint32_t topic = exe.u32(at + 5) + 15 * t;
                const auto& ours_topic = talk.topics[t];
                eq(index++, ours_topic.string, exe.u16(topic)); eq(index++, ours_topic.quest_gated, exe.u8(topic + 2));
                eq(index++, ours_topic.quest_state, exe.u32(topic + 3)); eq(index++, ours_topic.quest, exe.u32(topic + 7));
                eq(index++, ours_topic.cls, exe.u32(topic + 11));
            }
        }
        eq(index, ll(game::kNpcTalk.size()), ll(ours));
    } },
    { "game::kNpcMenus", 0x726c48, [](const Exe& exe, std::uint32_t a) {   // 39-byte {hc, entries incl. Cancel, u16 string[5], ...}
        for (std::uint32_t i = 0; i < game::kNpcMenus.size(); ++i) {
            const std::uint32_t at = a + 39 * i;
            const auto& menu = game::kNpcMenus[i];
            const std::uint32_t count = exe.u32(at + 4) - 1;
            eq(5 * i, menu.hc_idx, exe.i32(at));
            for (std::uint32_t k = 0; k < 4; ++k) eq(5 * i + 1 + k, menu.entries[k], k < count ? exe.u16(at + 8 + 2 * k) : 0);
        }
    } },
    { "game::kSpeedBand", 0x721f10, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < game::kSpeedBand.size(); ++i) eq(i, game::kSpeedBand[i], exe.i32(a + 4 * i));
    } },
    { "game::kSpeedColumn", 0x722078, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 14; ++i) eq(i, game::kSpeedColumn[i / 2][i % 2], exe.i32(a + 4 * i));
    } },
    // --- components/rules ---
    { "rules::kDay", 0x7443f0, [](const Exe& exe, std::uint32_t a) {   // {start, type, colour}
        for (std::uint32_t i = 0; i < rules::kDay.size(); ++i) {
            eq(2 * i, rules::kDay[i].start, exe.i32(a + 12 * i)); eq(2 * i + 1, rules::kDay[i].type, exe.i32(a + 12 * i + 4));
        }
    } },
    { "rules::DenQuest::kBlocks", 0x7366b0, [](const Exe& exe, std::uint32_t a) { quest_blocks(exe, a, rules::DenQuest::kBlocks); } },
    { "rules::BurialQuest::kBlocks", 0x736ce8, [](const Exe& exe, std::uint32_t a) { quest_blocks(exe, a, rules::BurialQuest::kBlocks); } },
    { "rules::AndyQuest::kBlocks", 0x7382e0, [](const Exe& exe, std::uint32_t a) { quest_blocks(exe, a, rules::AndyQuest::kBlocks); } },
    { "rules::CainQuest::kBlocks", 0x737668, [](const Exe& exe, std::uint32_t a) { quest_blocks(exe, a, rules::CainQuest::kBlocks); } },
    { "rules::TowerQuest::kBlocks", 0x737ed8, [](const Exe& exe, std::uint32_t a) { quest_blocks(exe, a, rules::TowerQuest::kBlocks); } },
    { "rules::ToolsQuest::kBlocks", 0x737198, [](const Exe& exe, std::uint32_t a) { quest_blocks(exe, a, rules::ToolsQuest::kBlocks); } },
    { "rules::kQuestLogRecords", 0x7237a4, [](const Exe& exe, std::uint32_t a) {   // 64-byte stride
        for (std::uint32_t r = 0; r < 6; ++r)
            for (std::uint32_t k = 0; k < 29; ++k) eq(29 * r + k, rules::kQuestLogRecords[r][k], exe.u16(a + 64 * r + 2 * k));
    } },
    { "rules::AndyQuest::kChipped", 0x7361dc, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 7; ++i) eq_code(i, rules::AndyQuest::kChipped[i], exe, a + 4 * i);
    } },
    { "rules::AndyQuest::kStandard", 0x736444, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 7; ++i) eq_code(i, rules::AndyQuest::kStandard[i], exe, a + 4 * i);
    } },
    { "rules::kUModLabel", 0x725188, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < rules::kUModLabel.size(); ++i) eq(i, rules::kUModLabel[i], exe.u16(a + 2 * i));
    } },
    { "rules::kWord", 0x6da488, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < rules::kWord.size(); ++i) {
            eq(2 * i, rules::kWord[i].first, exe.i32(a + 8 * i)); eq(2 * i + 1, rules::kWord[i].second, exe.i32(a + 8 * i + 4));
        }
    } },
    { "rules::kRows (boss aura)", 0x73bf68, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < std::size(rules::kRows); ++i) {
            const auto& row = rules::kRows[i];
            const int ours[5] = { row.min_lvl, row.add, row.mul, row.div, row.skill };
            for (std::uint32_t k = 0; k < 5; ++k) eq(5 * i + k, ours[k], exe.i32(a + 20 * i + 4 * k));
        }
    } },
    { "rules::kNear", 0x6eb180, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 64; ++i) eq(i, rules::kNear[i / 8][i % 8], exe.i32(a + 4 * i));
    } },
    { "rules::kDir", 0x6f1798, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 16; ++i) eq(i, rules::kDir[i / 2][i % 2], exe.i32(a + 4 * i));
    } },
    { "rules::kTry", 0x6f1518, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 75; ++i) eq(i, rules::kTry[i / 3][i % 3], exe.i32(a + 4 * i));
    } },
    { "rules::kRing", 0x6e3188, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 8; ++i) eq(i, rules::kRing[i], exe.u8(a + i));
    } },
    { "rules::kSweep", 0x6e3140, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 72; ++i) eq(i, rules::kSweep[i / 9][i % 9], exe.u8(a + i));
    } },
    { "rules::kGroups", 0x744684, [](const Exe& exe, std::uint32_t) {   // {u32 codes[n], u32 n} for hp, mp, rv
        constexpr std::uint32_t kCodes[3] = { 0x744684, 0x74466c, 0x744660 }, kCount[3] = { 0x744698, 0x744680, 0x744668 };
        std::size_t index = 0;
        for (std::uint32_t g = 0; g < 3; ++g) {
            const auto& group = rules::kGroups[g];
            eq(index++, ll(group.size() / 3), exe.i32(kCount[g]));
            for (std::uint32_t k = 0; k < group.size() / 3; ++k) eq_code(index++, group.data() + 3 * k, exe, kCodes[g] + 4 * k);
        }
    } },
    { "rules::kSeqEntries/kSeqFrames", 0x7483b8, [](const Exe& exe, std::uint32_t a) {   // [seq] -> 14 x {frames*, count, count}; frames 6 bytes
        std::size_t index = 0, found = 0;
        for (const auto& entry : rules::kSeqEntries) {
            const std::uint32_t at = exe.u32(a + 4 * std::uint32_t(entry.seq)) + 12 * std::uint32_t(entry.wclass);
            eq(index++, entry.count, exe.i32(at + 4));
            for (std::uint32_t f = 0; f < entry.count; ++f) {
                const auto& frame = rules::kSeqFrames[entry.first + f];
                const std::uint32_t exe_frame = exe.u32(at) + 6 * f;
                eq(index++, frame.mode, exe.u8(exe_frame + 2)); eq(index++, frame.frame, exe.u8(exe_frame + 3));
                eq(index++, frame.event, exe.u8(exe_frame + 5));
            }
        }
        for (std::uint32_t seq = 0; seq < 24; ++seq)   // every exe sequence is ours
            for (std::uint32_t w = 0; w < 14; ++w)
                if (exe.u32(a + 4 * seq) && exe.i32(exe.u32(a + 4 * seq) + 12 * w + 4) > 0) ++found;
        eq(index, ll(std::size(rules::kSeqEntries)), ll(found));
    } },
    // --- components/compcode, components/drlg ---
    { "compcode::kReservedType", 0x72e1e8, [](const Exe& exe, std::uint32_t a) {   // first int of 12-byte records
        for (std::uint32_t i = 0; i < compcode::kReservedType.size(); ++i) eq(i, compcode::kReservedType[i], exe.i32(a + 12 * i));
    } },
    { "kObjPreset", 0x748ad8, [](const Exe& exe, std::uint32_t a) {   // u32[5][150]
        for (std::uint32_t i = 0; i < 750; ++i) eq(i, kObjPreset[i / 150][i % 150], exe.i32(a + 4 * i));
    } },
    { "drlg::kOrientClass", 0x6ef620, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 20; ++i) eq(i, drlg::room_tiles_detail::kOrientClass[i], exe.i32(a + 4 * i));
    } },
    { "drlg::kOrientMerge", 0x6ef574, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 42; ++i) eq(i, drlg::room_tiles_detail::kOrientMerge[i], exe.i32(a + 4 * i));
    } },
    { "drlg::kDoorLevels", 0x6eefc8, [](const Exe& exe, std::uint32_t a) {   // the first 12 rows of a longer table
        for (std::uint32_t i = 0; i < 12; ++i) {
            const auto& row = drlg::room_tiles_detail::kDoorLevels[i];
            eq(3 * i, row.level, exe.i32(a + 12 * i)); eq(3 * i + 1, row.first, exe.i32(a + 12 * i + 4));
            eq(3 * i + 2, row.last, exe.i32(a + 12 * i + 8));
        }
    } },
    { "drlg::kDoorRows", 0x6ef188, [](const Exe& exe, std::uint32_t a) {   // {style, seq, is9, id, type, dx, dy}; ours no type
        for (std::uint32_t i = 0; i < drlg::room_tiles_detail::kDoorRows.size(); ++i) {
            const auto& row = drlg::room_tiles_detail::kDoorRows[i];
            const int ours[6] = { row.style, row.seq, row.is9, row.id, row.dx, row.dy };
            constexpr std::uint32_t kAt[6] = { 0, 4, 8, 12, 20, 24 };
            for (std::uint32_t k = 0; k < 6; ++k) eq(6 * i + k, ours[k], exe.i32(a + 28 * i + kAt[k]));
        }
    } },
    { "drlg::kDx/kDy", 0x6eee14, [](const Exe& exe, std::uint32_t a) {   // interleaved (dx, dy)
        for (std::uint32_t i = 0; i < 4; ++i) {
            eq(2 * i, drlg::room_tiles_detail::kDx[i], exe.i32(a + 8 * i)); eq(2 * i + 1, drlg::room_tiles_detail::kDy[i], exe.i32(a + 8 * i + 4));
        }
    } },
    { "drlg::kRow", 0x6eeea0, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 20; ++i) eq(i, drlg::room_tiles_detail::kRow[i], exe.i32(a + 4 * i));
    } },
    { "drlg::kMask", 0x6eee24, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 30; ++i) eq(i, drlg::room_tiles_detail::kMask[i / 5][i % 5], exe.i32(a + 4 * i));
    } },
    { "drlg::kAct1Outdoors", 0x6f0750, [](const Exe& exe, std::uint32_t a) {   // {fn*, level, link, -1}
        constexpr std::uint32_t kFn[5] = { 0x6760f0, 0x676150, 0x676650, 0x676450, 0x6768c0 };   // by Place
        for (std::uint32_t i = 0; i < drlg::kAct1Outdoors.size(); ++i) {
            const auto& record = drlg::kAct1Outdoors[i];
            eq(3 * i, kFn[int(record.how)], exe.u32(a + 16 * i));
            eq(3 * i + 1, record.level, exe.i32(a + 16 * i + 4)); eq(3 * i + 2, record.link, exe.i32(a + 16 * i + 8));
        }
    } },
    { "drlg::kAct1Highlands", 0x6f0840, [](const Exe& exe, std::uint32_t a) {
        constexpr std::uint32_t kFn[5] = { 0x6760f0, 0x676150, 0x676650, 0x676450, 0x6768c0 };
        for (std::uint32_t i = 0; i < drlg::kAct1Highlands.size(); ++i) {
            const auto& record = drlg::kAct1Highlands[i];
            eq(3 * i, kFn[int(record.how)], exe.u32(a + 16 * i));
            eq(3 * i + 1, record.level, exe.i32(a + 16 * i + 4)); eq(3 * i + 2, record.link, exe.i32(a + 16 * i + 8));
        }
    } },
    { "drlg::kTownAllowed", 0x6f1158, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 64; ++i) eq(i, drlg::kTownAllowed[i], exe.i32(a + 4 * i));
    } },
    { "drlg::kAct1Flags", 0x6f1258, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < drlg::kAct1Flags.size(); ++i) {
            const auto& rule = drlg::kAct1Flags[i];
            const ll ours[6] = { rule.level, rule.not1, rule.not2, rule.dir, rule.next_dir, rule.flag };
            for (std::uint32_t k = 0; k < 6; ++k) eq(6 * i + k, ours[k], exe.i32(a + 24 * i + 4 * k));
        }
    } },
    { "drlg::kSpecials", 0x6ef8a8, [](const Exe& exe, std::uint32_t a) {   // rows 0..16 here, 17..18 at 0x6f03e8
        for (std::uint32_t r = 0; r < drlg::maze_detail::kSpecials.size(); ++r)
            for (std::uint32_t k = 0; k < 4; ++k) {
                const auto& special = drlg::maze_detail::kSpecials[r][k];
                const std::uint32_t at = (r < 17 ? a + 0x40 * r : 0x6f03e8 + 0x40 * (r - 17)) + 16 * k;
                const int ours[4] = { special.from, special.to_room, special.file, special.dir };
                for (std::uint32_t f = 0; f < 4; ++f) eq(16 * r + 4 * k + f, ours[f], exe.i32(at + 4 * f));
            }
    } },
    { "drlg::kBorder", 0x6f0620, [](const Exe& exe, std::uint32_t a) {   // rows 1..12 (row 0 is never read)
        for (std::uint32_t r = 1; r < drlg::outdoor_detail::kBorder.size(); ++r)
            for (std::uint32_t k = 0; k < 2; ++k) eq(2 * r + k, drlg::outdoor_detail::kBorder[r][k], exe.i32(a + 16 * r + 4 * k));
    } },
    { "drlg::corner_row", 0x6f0fe8, [](const Exe& exe, std::uint32_t a) {   // e -40..40; exe -1 = our 0
        for (int edge = -40; edge <= 40; ++edge) {
            const int value = exe.i32(a + 4 * std::uint32_t(edge + 40));
            eq(std::size_t(edge + 40), drlg::outdoor_detail::corner_row(edge), value < 0 ? 0 : value);
        }
    } },
    { "drlg::kRiver", 0x6f2680, [](const Exe& exe, std::uint32_t a) {   // rows 1..15 (row 0 is never read)
        for (std::uint32_t r = 1; r < drlg::outdoor_detail::kRiver.size(); ++r)
            for (std::uint32_t k = 0; k < 2; ++k) eq(2 * r + k, drlg::outdoor_detail::kRiver[r][k], exe.i32(a + 8 * r + 4 * k));
    } },
    { "drlg::kRoad", 0x6f2700, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 256; ++i) eq(i, drlg::outdoor_detail::kRoad[i], exe.u8(a + i));
    } },
    { "drlg::kDirVal", 0x6f1518, [](const Exe& exe, std::uint32_t a) {   // column 0 of rules::kTry's table
        for (std::uint32_t i = 0; i < 25; ++i) eq(i, drlg::outdoor_detail::kDirVal[i], exe.i32(a + 12 * i));
    } },
    { "drlg::kTurn", 0x6f2840, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 17; ++i) eq(i, drlg::outdoor_detail::kTurn[i], exe.u8(a + i));
    } },
    { "drlg::kStepY", 0x6f2850, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 4; ++i) eq(i, drlg::outdoor_detail::kStepY[i], exe.s8(a + i));
    } },
    { "drlg::kStepX", 0x6f2854, [](const Exe& exe, std::uint32_t a) {
        for (std::uint32_t i = 0; i < 4; ++i) eq(i, drlg::outdoor_detail::kStepX[i], exe.s8(a + i));
    } },
};

fs::path find_exe() {
    if (const char* exe = std::getenv("D2_GAME_EXE"); exe && *exe) return exe;
    if (const char* dir = std::getenv("D2_MPQ_DIR"); dir && *dir) return fs::path(dir) / "bin" / "game.exe";
    if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / "Workspace/private/diablo2/bin/game.exe";
    return {};
}

}  // namespace

int main() {
    const fs::path path = find_exe();
    if (path.empty() || !fs::is_regular_file(path)) { std::printf("skip: no game.exe (set D2_GAME_EXE)\n"); return 0; }
    if (d2d::install::file_version(path) != std::array<std::uint16_t, 4>{ 1, 14, 3, 71 }) {
        std::printf("skip: %s is not 1.14.3.71\n", path.string().c_str());
        return 0;
    }
    const Exe exe(path);
    for (const auto& table : kTables) {
        g_table = table.name;
        const int bad = g_bad;
        g_values = 0;
        table.check(exe, table.address);
        std::printf("%s %s 0x%x (%d values)\n", g_bad == bad ? "ok  " : "FAIL", table.name, table.address, g_values);
    }
    std::printf("%zu tables, %d mismatches\n", std::size(kTables), g_bad);
    return g_bad == 0 ? 0 : 1;
}
