// SPDX-License-Identifier: GPL-3.0-or-later
// Act 5's quests (docs/research/re/quests-act5.md): Siege on Harrogath
// (a5q1, Shenk), Rescue on Mount Arreat (a5q2, the prisoners), Prison of
// Ice (a5q3, Anya), Betrayal of Harrogath (a5q4, Nihlathak). One player's
// view of each record; World (world/quests/act5.cpp) runs them at its quest
// hooks. The log (+0xb) moves where the records call FUN_00544300; `news`
// is its flag 1 (the log sent again).
#pragma once

#include "quests.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace d2d::rules {

namespace act5_detail {
// The town's NPCs (MonStats hcIdx): Larzuk, Drehya (Anya), Malah,
// Nihlathak, Qual-Kehk, Cain; frozen Anya (drehyaiced); the prisoners
// (act5pow) and the cages' doors (prisondoor).
inline constexpr int kLarzuk = 0x1ff, kAnya = 0x200, kMalah = 0x201, kNihlathak = 0x202, kQualKehk = 0x203, kCain = 520;
inline constexpr int kFrozenAnya = 0x20f, kPrisoner = 0x216, kPrisonDoor = 0x1b2;
inline constexpr int kHarrogath = 109, kBloodyFoothills = 110, kFrigidHighlands = 111, kArreatPlateau = 112;
inline constexpr int kCrystallinePassage = 113, kFrozenRiver = 114, kNihlathaksTemple = 121, kHallsOfVaught = 124;
// The records' log callbacks (FUN_00586c80 and kin): the log goes to a
// player unless done (bit 0 or 15) without 13 / 14.
bool told(const QuestBits& quest_bits, int quest);
// FUN_0065c3e0: bits 2..11 cleared.
inline void clear_steps(QuestBits& quest_bits, int quest) { for (int bit = 2; bit <= 11; ++bit) quest::set(quest_bits, quest, bit, false); }
struct Entry { int npc, string; bool greet; };
std::vector<QuestMsg> block_for(const std::vector<Entry>& block, int npc);
// Each record's per-player marks of its state: 2 → bit 2, 3 → bit 3 (a5q1: bit 4 with the log at 2).
void mark(QuestBits& quest_bits, int quest, int state, bool four = false);
}  // namespace act5_detail

// a5q0 (FUN_0058ea50, word 0x2a): the town's introductions, kept in the
// player's intro bits (the save's NPC section; FUN_005723c0 tests,
// FUN_00572360 sets; bit by the 0x732738 table). Until heard (FUN_00586b50):
// Anya and Cain block 0; Malah, Nihlathak and Qual-Kehk block 1 to a
// barbarian / assassin / druid, 2 to a sorceress / necromancer / paladin,
// else 0. Hearing one (FUN_0058e990) sets its bit; Malah's opens a5q1.
// Malah's "!" till hers is heard (FUN_00586c50).
struct Act5Opening {
    static constexpr int kMalah = act5_detail::kMalah;
    static inline const std::vector<act5_detail::Entry> kBlocks[3] = {   // 0x732ff8
        { { 512, 20014, true }, { 513, 20037, true }, { 514, 20053, true }, { 515, 20065, true }, { 520, 20003, true } },
        { { 512, 20014, true }, { 513, 20039, true }, { 514, 20054, true }, { 515, 20067, true }, { 520, 20003, true } },
        { { 512, 20014, true }, { 513, 20038, true }, { 514, 20055, true }, { 515, 20066, true }, { 520, 20003, true } },
    };
    // 0x732738's bits: Anya 30, Malah 31, Nihlathak 32, Qual-Kehk 33, Cain 34.
    [[nodiscard]] static int bit_of(int npc) {
        switch (npc) {
        case act5_detail::kAnya: return 30;
        case act5_detail::kMalah: return 31;
        case act5_detail::kNihlathak: return 32;
        case act5_detail::kQualKehk: return 33;
        case act5_detail::kCain: return 34;
        default: return -1;
        }
    }
    [[nodiscard]] static bool heard(std::uint64_t intros, int npc) { return bit_of(npc) >= 0 && (intros >> bit_of(npc) & 1); }
    [[nodiscard]] static std::vector<QuestMsg> talk(std::uint64_t intros, int npc, int player_class);
    // Heard: true when it was Malah's (a5q1 opens).
    static bool said(std::uint64_t& intros, int npc, int string);
    [[nodiscard]] static bool alert(std::uint64_t intros, int npc) { return npc == kMalah && !heard(intros, npc); }
};

// Siege on Harrogath (a5q1.cpp, word 0x23, record 0x1f, FUN_005876e0). State:
// 0 shut, 1 open (Malah's introduction heard, FUN_005723c0(0x201)), 2 Larzuk
// told (20077), 3 the barricades or Harrogath after, 5 rewarded (20090).
// Shenk (superunique 0x2a) dies: those about him get bits 13 and 1, the rest
// 14. Bit 5: Larzuk's reward heard (his socketing then due).
struct SiegeQuest {
    static constexpr int kQuest = 0x23, kLarzuk = act5_detail::kLarzuk, kShenk = 0x2a;
    static constexpr int kTold = 20077, kReward = 20090;
    bool open = true;              // +9
    int state = 0;                 // +0xc
    int log = 0;                   // +0xb
    bool told_heard = false;       // data +8
    bool reward_heard = false;     // data +0x16
    bool news = false;

    // The player joins.
    void join(const QuestBits& quest_bits, bool malah_heard);
    // Malah's introduction heard (a5q0's FUN_0058e990).
    void malah_heard() { if (open && state == 0) state = 1; }
    // Shenk's think, the record open with the log under 2 (FUN_00587900): log 2.
    void shenk_seen() { if (open && log < 2) { log = 2; news = true; } }
    // Larzuk's talk closes: log 1 after 20077, 4 after 20090.
    void talk_closed(int npc);
    // A level change: into the barricades (110..112) at 1 / 2 → 3; out of Harrogath at 2 → 3 (the log 1 if 0).
    void enter(QuestBits& quest_bits, int from, int into);
    // Shenk died: about him (his room or one near it) bits 1 and 13, else 14; the log 3 if under.
    void killed(QuestBits& quest_bits, bool about);
    // Heard: whether the chain (a5q2) opens.
    bool said(QuestBits& quest_bits, int npc, int string);
    // Larzuk's socketing used (FUN_005877c0): done.
    void socketed(QuestBits& quest_bits) { quest::set(quest_bits, kQuest, 0); quest::set(quest_bits, kQuest, 1, false); }
    // Blocks (0x733308 by 0x733928; FUN_00586ff0).
    static inline const std::vector<act5_detail::Entry> kBlocks[5] = {
        { { 511, 20077, true } },
        { { 511, 20078, false }, { 520, 20079, false }, { 512, 20080, false }, { 513, 20081, false }, { 514, 20082, false }, { 515, 20083, false } },
        { { 511, 20084, false }, { 520, 20085, false }, { 512, 20086, false }, { 513, 20087, false }, { 514, 20088, false }, { 515, 20089, false } },
        { { 511, 20090, true }, { 520, 20091, false }, { 512, 20092, false }, { 513, 20093, false }, { 514, 20094, false }, { 515, 20095, false } },
        { { 511, 20090, false }, { 520, 20091, false }, { 512, 20092, false }, { 513, 20093, false }, { 514, 20094, false }, { 515, 20095, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        if (quest::bit(quest_bits, kQuest, 1)) return quest::bit(quest_bits, kQuest, 5) ? std::vector<QuestMsg>{} : act5_detail::block_for(kBlocks[3], npc);
        if (quest::bit(quest_bits, kQuest, 0) || !(state < 4 || quest::bit(quest_bits, kQuest, 13)) || !open || state < 1 || state > 5) return {};
        return act5_detail::block_for(kBlocks[state - 1], npc);
    }
    // The "!".
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const;
    // The log.
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const;
};

// Rescue on Mount Arreat (a5q2.cpp, word 0x24, record 0x20, FUN_00588510).
// State: 0 shut, 1 open (a5q1's chain), 2 Qual-Kehk told (20096) or a cage
// up, 3 the Highlands / Plateau, or out of Harrogath at 2; 5 rewarded
// (20110). Three cages (object 473, InitFn 62: FUN_005886a0) hold five
// prisoners each (act5pow 0x216, the Wussie think FUN_005ee3c0). A cage's
// door (prisondoor 0x1b2) broken frees the living prisoners within 15 of
// it (FUN_00588040); a player by them then has the cage's portal
// (object 189) open (FUN_005888d0), and they walk into it. Five prisoners dead
// fail it (log 12). The portal's opener, with every cage up and every
// prisoner dead or freed: 12 or more freed of the 15, bits 13 and 1, and 5
// / 6 / 7 for 15 / 14 / 12..13; Qual-Kehk pays three runes (two with bit 6,
// one with 7). Log: 1 told, 5 a caged prisoner by the player, 2 a portal
// with some still to free, 3 done, 12 failed, 13 rewarded.
struct RescueQuest {
    static constexpr int kQuest = 0x24, kQualKehk = act5_detail::kQualKehk, kPrisoner = act5_detail::kPrisoner, kDoor = act5_detail::kPrisonDoor;
    static constexpr int kTold = 20096, kReward = 20110, kCages = 3, kPerCage = 5, kFreeReach = 15, kFail = 5;
    // A caged prisoner's cry: S->C 0x8a {1, its id} to the nearest player
    // (0x5ee579), whose client plays wussie_help_me on it (FUN_004b3380,
    // class 0x216; 0x11fb, wussie_cheer_1, only for a unit whose 0xac named
    // an owner, FUN_004ae130 -> FUN_004ae150 from 0x45f3a3: no prisoner).
    static constexpr int kHelpMe = 0x11ff;
    static constexpr std::array<const char*, 3> kRunes{ "r07", "r08", "r09" };   // 0x732fec: Ral, Ort, Tal
    // A cage's portal: the dead door's subtile + 2 on x, as is; cells, the subtile's centre.
    static std::pair<float, float> portal_spot(int door_x, int door_y);
    bool open = true;
    int state = 0, log = 0;
    bool told_heard = false;       // data +0x84
    bool closed_hook = true;       // record +0xa8 (LAB_00587a30), cleared as a level change moves the log from 0
    struct Cage {
        bool up = false;           // data +0x86 + n
        int x = 0, y = 0;          // data +0x8c + n * 8: the cage's spot, subtiles
        std::array<int, kPerCage> ids{ -1, -1, -1, -1, -1 };   // data +0xb0 + n * 20: its prisoners' unit ids
        int gone = 0;              // data +0x114 + n * 4: its prisoners dead or through the portal
        bool emptied = false;      // data +0x108 + n: five gone
        bool portal = false;       // data +0x10e + n: its portal opened (+0xf8 + n: made)
    };
    std::array<Cage, kCages> cages{};
    int made = 0;                  // data +0xa4: prisoners made
    int dead = 0;                  // data +0xa8: prisoners killed caged
    int freed = 0;                 // data +0xac: freed by their door
    std::vector<int> freed_ids;    // data +0x120, count +0x15c
    bool news = false;

    // A later player joins (+0xd8, LAB_00588450): their bit 4 cleared.
    static void join_more(QuestBits& quest_bits) { quest::set(quest_bits, kQuest, 4, false); }
    // The player joins: bit 4 cleared first.
    void join(QuestBits& quest_bits);
    // a5q1's chain (FUN_005883e0): 0 → 1.
    void opened() { if (open && state == 0) state = 1; }
    // A cage comes up at (x, y) subtiles: state 2 if under, the log 1 at 0 (under five dead); a slot of its own
    // (none when three are up or one's at that spot).
    int cage(int x, int y);
    void prisoner_made(int slot, int which, int id) { cages[std::size_t(slot)].ids[std::size_t(which)] = id; ++made; }
    // FUN_00587fc0: the cage holding the prisoner, -1 none.
    [[nodiscard]] int cage_of(int id) const {
        for (int slot = 0; slot < kCages; ++slot)
            if (std::ranges::contains(cages[std::size_t(slot)].ids, id)) return slot;
        return -1;
    }
    // A level change: into the Highlands / Plateau, or out of Harrogath at 2.
    void enter(QuestBits& quest_bits, int from, int into);
    // A door broke: these living prisoners within 15 of it freed.
    void door_broken(QuestBits& quest_bits, const std::vector<int>& near_ids);
    // A prisoner died: one freed counts gone from its cage; one caged is dead (five fail it), and gone too.
    void prisoner_died(QuestBits& quest_bits, int id);
    // A freed prisoner went through its cage's portal (FUN_00588880).
    void went(int slot) {
        if (slot < 0) return;
        if (++cages[std::size_t(slot)].gone >= kPerCage) cages[std::size_t(slot)].emptied = true;
    }
    // A caged prisoner with a player by it (FUN_00588dd0): log 5.
    void cried() { if (open && log != 5 && dead < kFail) { log = 5; news = true; } }
    // Its door broken, a player by it (FUN_005888d0): true when the cage's
    // portal is to open now; then the opener's bits.
    bool portal_due(int slot) const { return slot >= 0 && !cages[std::size_t(slot)].portal; }
    void portal_opened(QuestBits& quest_bits, int slot);
    // Qual-Kehk's talk closes: log 1 after 20096.
    void talk_closed(int npc);
    // Heard (FUN_00587b00): the runes paid (their count) and whether the chain opens.
    struct Heard { int runes = 0; bool chain = false; };
    Heard said(QuestBits& quest_bits, int npc, int string);
    // Blocks (0x733940 by 0x734270; FUN_00587d30).
    static inline const std::vector<act5_detail::Entry> kBlocks[6] = {
        { { 515, 20096, true } },
        { { 511, 20100, false }, { 520, 20098, false }, { 512, 20099, false }, { 513, 20101, false }, { 514, 20102, false }, { 515, 20097, false } },
        { { 511, 20107, false }, { 520, 20105, false }, { 512, 20106, false }, { 513, 20108, false }, { 514, 20109, false }, { 515, 20103, false } },
        { { 511, 20113, false }, { 520, 20111, false }, { 512, 20112, false }, { 513, 20114, false }, { 514, 20115, false }, { 515, 20110, true } },
        { { 511, 20113, false }, { 520, 20111, false }, { 512, 20112, false }, { 513, 20114, false }, { 514, 20115, false }, { 515, 20110, false } },
        { { 515, 20104, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const;
    // The "!": Qual-Kehk, and every prisoner till it's done.
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const;
    // The log: the records' default (+0xe8 none; done at +0xd 4).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }

private:
    // Five dead (FUN_00588040's tail): log 12, the others' bit 14 (FUN_00587f60).
    void failed(QuestBits& quest_bits) {
        if (dead < kFail || log == 0xc) return;
        log = 0xc; news = true; told_heard = false;
        someone_else(quest_bits);
    }
public:
    // Every cage up and each prisoner made dead or freed (the last portal's test).
    [[nodiscard]] bool all_in() const { return made == dead + freed && std::ranges::all_of(cages, &Cage::up); }
    // A party member's share of it, 12 or more freed: in act 5, neither done nor due: bits 13, 1 and 5 / 6 / 7
    // by the count.
    void party_share(QuestBits& quest_bits) const;
    static void someone_else(QuestBits& quest_bits) {
        if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 13)) quest::set(quest_bits, kQuest, 14);
    }
};

// Prison of Ice (a5q3.cpp, word 0x25, record 0x21, FUN_0058a410). State: 0
// shut, 1 open (a5q2's chain, or the Arreat Plateau), 2 Malah told (20116),
// 3 the Crystalline Passage / Frozen River, or out of Harrogath at 2; 4
// frozen Anya's 20131 (no potion), 5 thawed (OperateFn 67), 6 Nihlathak's
// Temple first. The Frozen River's marker (object 460) makes frozen Anya
// (fana, object 558) 25 frames on (FUN_0058a500); Malah gives the Thawing
// Potion ("ice", 20127); used on her, she thaws (FUN_0058aab0: mode 2, then
// drehyaiced at her spot, the chain). Malah's 20132 then gives the Scroll of
// Resistance ("tr2", bit 8) and sends Anya home (record +0x84: 0 frozen, 1
// thawed, 2 in town); Anya's 20136 a rare class item (bit 9). Both: bit 0.
// Record +0x88: Nihlathak's left town (FUN_005893e0), and the Halls of
// Vaught's marker (462, InitFn 69) has him there. Log: 1 told, 2 she's
// found, 3 no potion, 4 the potion carried, 5 thawed, 6 the scroll, 13
// both rewards.
struct PrisonQuest {
    static constexpr int kQuest = 0x25, kMalah = act5_detail::kMalah, kAnya = act5_detail::kAnya, kFrozen = act5_detail::kFrozenAnya;
    static constexpr int kTold = 20116, kPotion = 20127, kNoPotion = 20131, kScroll = 20132, kAnyaReward = 20136;
    static constexpr int kFana = 558, kRiverMarker = 460, kAnyaMarker = 459, kNihlathakMarker = 462, kNihlathakBoss = 60;   // objects.txt; SuperUniques
    static constexpr int kFanaFrames = 0x19;
    static constexpr const char* kPotionCode = "ice";
    static constexpr const char* kScrollCode = "tr2";
    // Anya's item (0x735f6c normal, 0x735efc nightmare, 0x735f34 hell; by
    // class, the seventh the assassin's): rare, at the player's level.
    static inline const std::array<std::array<std::vector<const char*>, 7>, 3> kAnyaItems{ {
        { { { "am1", "am2", "am3", "am4", "am5" }, { "ob1", "ob2", "ob3", "ob4", "ob5" }, { "ne1", "ne2", "ne3", "ne4", "ne5" }, { "pa1", "pa2", "pa3", "pa4", "pa5" },
            { "ba1", "ba2", "ba3", "ba4", "ba5" }, { "dr1", "dr2", "dr3", "dr4", "dr5" }, { "ktr", "wrb", "axf", "ces", "clw", "btl", "skr" } } },
        { { { "am6", "am7", "am8", "am9", "ama" }, { "ob6", "ob7", "ob8", "ob9", "oba" }, { "ne6", "ne7", "ne8", "ne9", "nea" }, { "pa6", "pa7", "pa8", "pa9", "paa" },
            { "ba6", "ba7", "ba8", "ba9", "baa" }, { "dr6", "dr7", "dr8", "dr9", "dra" }, { "9ar", "9wb", "9xf", "9cs", "9lw", "9tw", "9qr" } } },
        { { { "amb", "amc", "amd", "ame", "amf" }, { "obb", "obc", "obd", "obe", "obf" }, { "neb", "nec", "ned", "nee", "nef" }, { "pab", "pac", "pad", "pae", "paf" },
            { "bab", "bac", "bad", "bae", "baf" }, { "drb", "drc", "drd", "dre", "drf" }, { "7ar", "7wb", "7xf", "7cs", "7lw", "7tw", "7qr" } } },
    } };
    bool open = true;
    int state = 0, log = 0;
    bool told_heard = false;       // data +0xac
    bool potion_heard = false;     // data +0x101
    bool scroll_again = false;     // data +0x102
    int potions = 0;               // data +0xd8: potions out
    int anya = 0;                  // data +0x84
    bool town_left = false;        // data +0x88
    bool fana_made = false;        // data +0xa4
    bool thawing = false;          // data +0x100: the thaw's timer
    int thaw_step = 0;             // data +0x8c
    int thaw_steps = 0;            // quest steps since the thaw's timer last ran
    bool news = false;

    // The player joins.
    void join_more(QuestBits& quest_bits, bool betrayal_open) const;
    void join(const QuestBits& quest_bits, bool carries_potion);
    // a5q2's chain (FUN_00589160).
    void opened() { if (open && state == 0) state = 1; }
    // FUN_005893e0's record part: Nihlathak leaves town.
    void leave_town() { town_left = true; }
    // A level change.
    void enter(QuestBits& quest_bits, int from, int into);
    // Fana's InitFn (74, FUN_0058aa50): the log 2.
    void fana_seen() { if (open && log < 2) { log = 2; news = true; } }
    // Fana used.
    bool thaw(QuestBits& quest_bits, bool carries_potion);
    // A quest-clock step.
    bool thaw_quest_step();
    // The thaw's timer, a run each: 1 fana's mode 2; 2 she's drehyaiced, the chain on.
    int thaw_tick();
    // The Scroll of Resistance read: only with bit 8 (given) and not bit 7 (read); bit 7 set.
    static bool read_scroll(QuestBits& quest_bits);
    // The scroll's resistances in an expansion game: 10 for each difficulty whose word has bit 7.
    static int scroll_resist(const std::array<QuestBits, 3>& by_difficulty);
    // Malah's talk closes.
    void talk_closed(int npc);
    // Heard (FUN_00589580): the item to give and its quality / level (the
    // player's), and Anya sent home.
    struct Heard { const char* give = nullptr; bool rare = false, anya_home = false; };
    Heard said(QuestBits& quest_bits, int npc, int string, bool carries_potion, bool carries_scroll, int difficulty, int player_class, const auto& roll) {   // roll: drawn only for Anya's item
        Heard out;
        if (npc == kMalah) {
            if (string == kTold) { told_heard = true; state = 2; act5_detail::mark(quest_bits, kQuest, state); return out; }
            if (string == kPotion) {
                if (carries_potion || potions != 0) return out;
                ++potions; potion_heard = true; out.give = kPotionCode;
                return out;
            }
            if (string == kScroll) {
                if (quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 0)) {
                    out.give = kScrollCode;
                    quest::set(quest_bits, kQuest, 8);
                    if (!quest::bit(quest_bits, kQuest, 9)) { if (open && log < 6) { log = 6; news = true; } }
                    else { quest::set(quest_bits, kQuest, 1, false); quest::set(quest_bits, kQuest, 0); }
                    if (anya == 1) { anya = 2; out.anya_home = true; }
                    return out;
                }
                if ((quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) && !scroll_again && quest::bit(quest_bits, kQuest, 8)
                    && !quest::bit(quest_bits, kQuest, 7) && !carries_scroll) { scroll_again = true; out.give = kScrollCode; }
                return out;
            }
        } else if (npc == kAnya && string == kAnyaReward) {
            if (!quest::bit(quest_bits, kQuest, 1) || quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 10)) return out;
            const auto& items = kAnyaItems[std::size_t(std::clamp(difficulty, 0, 2))][std::size_t(std::clamp(player_class, 0, 6))];
            out.give = items[roll() % items.size()]; out.rare = true;
            quest::set(quest_bits, kQuest, 9);
            if (quest::bit(quest_bits, kQuest, 8)) { quest::set(quest_bits, kQuest, 1, false); quest::set(quest_bits, kQuest, 0); }
            quest::set(quest_bits, kQuest, 10);
            return out;
        }
        if (string == kNoPotion && open) { heard_no_potion(); if (log < 3) { log = 3; news = true; } }
        return out;
    }
    // Blocks (0x734320 by 0x734304; FUN_00589b00).
    static inline const std::vector<act5_detail::Entry> kBlocks[7] = {
        { { 513, 20116, true } },
        { { 511, 20119, false }, { 520, 20118, false }, { 513, 20117, false }, { 514, 20120, false }, { 515, 20121, false } },
        { { 511, 20124, false }, { 520, 20123, false }, { 513, 20122, false }, { 514, 20125, false }, { 515, 20126, false } },
        { { 511, 20129, false }, { 520, 20128, false }, { 527, 20131, true }, { 513, 20127, true }, { 515, 20130, false } },
        { { 511, 20129, false }, { 520, 20128, false }, { 515, 20130, false } },
        { { 511, 20134, false }, { 520, 20133, false }, { 512, 20136, true }, { 513, 20132, true }, { 515, 20135, false } },
        { { 511, 20134, false }, { 520, 20133, false }, { 512, 20136, false }, { 513, 20132, false }, { 515, 20135, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, bool carries_potion) const;
    // The "!".
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc, bool carries_potion) const;
    // The log.
    [[nodiscard]] int log_state(const QuestBits& quest_bits, bool carries_potion) const;

private:
    // 20131 heard (FUN_00589580's tail): state 4 from under, Nihlathak gone.
    void heard_no_potion() { if (open && state < 4) { state = 4; leave_town(); } }
};

// Betrayal of Harrogath (a5q4.cpp, word 0x26, record 0x22, FUN_0058bb20).
// State: 0 shut, 1 open (a5q3's chain), 2 Anya told (20137), 3 Harrogath
// after, 4 Nihlathak dead, 5 rewarded (20148, bit 4: her personalizing
// then due).
struct BetrayalQuest {
    static constexpr int kQuest = 0x26, kAnya = act5_detail::kAnya, kNihlathak = 0x20e;   // nihlathakboss
    static constexpr int kTold = 20137, kReward = 20148;
    bool open = true;
    int state = 0, log = 0;
    bool told_heard = false;       // data +0x86
    bool portal_due = false;       // data +0x87: Anya's portal at her talk's close
    bool portal_made = false;      // data +0x88
    bool news = false;

    // The player joins.
    void join(const QuestBits& quest_bits);
    // a5q3's chain (FUN_0058b140): 0 → 1.
    void opened() { if (open && state == 0) state = 1; }
    // A level change: out of Harrogath at 2 → 3; into the temple (121) with the log 1 → 2.
    void enter(QuestBits& quest_bits, int from, int into);
    // Nihlathak died (FUN_0058b7a0): bits 13 and 1 for those in his room or
    // one by it with Prison of Ice done or due (FUN_0058b4a0), 14 the rest
    // (FUN_0058b6d0); state 4; the log 4 eight ticks on (FUN_00543f10 ->
    // FUN_0058b770: log_timer), once.
    static constexpr int kLogTicks = 8;
    bool killed(QuestBits& quest_bits, bool about);
    void log_timer() { if (log != 4) { log = 4; news = true; } }
    bool timer_set = false;        // data +0x85
    // Anya's talk closes: the log 1 after 20137; true when her portal to the temple is to open.
    bool talk_closed(int npc);
    // Heard: whether the chain (a5q5) opens.
    bool said(QuestBits& quest_bits, int npc, int string);
    // Anya's personalizing used (FUN_0058bc00): done.
    void personalized(QuestBits& quest_bits) { quest::set(quest_bits, kQuest, 0); quest::set(quest_bits, kQuest, 1, false); }
    // Blocks (0x734ac8 by 0x7350e8; FUN_0058b2c0): Prison of Ice done or due.
    static inline const std::vector<act5_detail::Entry> kBlocks[5] = {
        { { 512, 20137, true } },
        { { 511, 20141, false }, { 520, 20139, false }, { 513, 20140, false }, { 515, 20142, false } },
        { { 511, 20145, false }, { 520, 20144, false }, { 512, 20143, false }, { 513, 20146, false }, { 515, 20147, false } },
        { { 511, 20150, false }, { 520, 20149, false }, { 512, 20148, true }, { 513, 20151, false }, { 515, 20152, false } },
        { { 511, 20150, false }, { 520, 20149, false }, { 512, 20148, false }, { 513, 20151, false }, { 515, 20152, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const;
    // The "!".
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const;
    // The log.
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const;
};

// Rite of Passage (a5q5.cpp, word 0x27, record 0x23, FUN_0058ce30). State:
// 0 shut, 1 open (a5q4's chain), 2 Qual-Kehk told (20153) or the altar
// used, 3 the Arreat Summit, or out of Harrogath at 2; 5 the Ancients dead.
// The altar (object 546, OperateFn 65) has the Ancients speak (20002); that
// heard, the three statues (474..476) go to mode 3 and each, 20 frames on
// (event 7, FUN_0058c0e0), turns into its Ancient (superuniques 0x2d, 0x2b,
// 0x2c). They sleep (their AI, FUN_0058cf90) till then. All three dead: the
// players on the Summit of level 20 / 40 / 60 or more get bits 0 and 13 and
// experience (FUN_0058c5c0), the rest bit 14; the stairs (547, OperateFn 66)
// lead on to the Worldstone Keep. A player dying on the Summit puts them
// back (FUN_005be290 -> FUN_0058c000). Log: 1 told, 2 on the Summit, 3 the
// altar, 13 done.
struct RiteQuest {
    static constexpr int kQuest = 0x27, kQualKehk = act5_detail::kQualKehk, kSummit = 120;
    static constexpr int kTold = 20153, kSpeech = 20002, kAside = 20169, kWakeFrames = 20;
    static constexpr int kAltar = 546, kStairs = 547, kFarewell = 561;
    // The statues (objects) and their Ancients (FUN_0058c0e0: [0x744304] +0xb3a / +0xb36 / +0xb38).
    static constexpr std::array<int, 3> kStatues{ 475, 474, 476 }, kSuperuniques{ 0x2b, 0x2d, 0x2c };
    static constexpr std::array<int, 3> kAncients{ 0x21c, 0x21d, 0x21e };   // ancientbarb1..3
    static constexpr std::array<int, 3> kStatueOf{ 0, 2, 1 };                // each Ancient's statue (kStatues index: its superunique's class)
    // The town's congratulations (FUN_0058c290): their bits 5..9.
    struct Congrats { int npc, string, bit; };
    static constexpr std::array<Congrats, 5> kCongrats{ { { 511, 20167, 5 }, { 520, 20165, 6 }, { 512, 20166, 7 }, { 513, 20168, 8 }, { 515, 20164, 9 } } };
    bool open = true;
    int state = 0, log = 0;
    bool done = false;             // data +0: the three dead
    bool told_heard = false;       // data +1
    bool heard = false, awake = false;   // data +0x10 / +0x11
    int portals = 0;                     // data +0xc: the town portals open on the Summit
    std::array<int, 3> statue_mode{};    // data +0x30: 0, 3 waking, 4 an Ancient, 2 back
    std::array<bool, 3> statue_dead{};   // data +0x3c: its Ancient fell, its statue not back yet
    bool returning = false;              // data +4: the statues' return timer (FUN_0058bd50) runs
    int made = 0, alive = 0;       // data +0x40 / +0x44
    bool fighting = false;         // data +0x48
    bool stairs_open = false;      // data +0x4a
    int altar_mode = 0;            // data +0x4c
    bool news = false;

    static bool level_enough(int clvl, int difficulty) { return clvl >= (difficulty + 1) * 20; }   // (+0x6d + 1) x 20
    // FUN_0058c210: the steps marked, for a player of the level.
    void mark(QuestBits& quest_bits, int clvl, int difficulty) const {
        if (quest::bit(quest_bits, kQuest, 0) || !level_enough(clvl, difficulty)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, 3);
    }
    // The player joins.
    void join(const QuestBits& quest_bits);
    // a5q4's chain (FUN_0058cd30).
    void opened() { if (open && state == 0) state = 1; }
    // A level change.
    void enter(QuestBits& quest_bits, int from, int into, int clvl, int difficulty);
    // Qual-Kehk's talk closes (LAB_0058be80).
    void talk_closed(int npc) { if (npc == kQualKehk && std::exchange(told_heard, false)) { log = 1; news = true; } }
    bool altar(QuestBits& quest_bits);
    // Heard (FUN_0058c290): true when the statues wake (their event 7 due).
    bool said(QuestBits& quest_bits, int npc, int string, int clvl, int difficulty) {
        if (string == kTold) {
            if (npc != kQualKehk || !open || state != 1) return false;
            state = 2;
            if (log != 1) { log = 1; news = true; }
            mark(quest_bits, clvl, difficulty);
            told_heard = true;
            return false;
        }
        if (string == kSpeech) {
            if (open && state < 4 && log != 3) { log = 3; news = true; }
            if (std::ranges::any_of(statue_mode, [](int mode) { return mode != 0; })) return false;
            statue_mode = { 3, 3, 3 };
            made = alive = 0;
            heard = awake = true;
            return true;
        }
        if (string == kAside) { quest::set(quest_bits, kQuest, 4); return false; }
        for (const auto& congrats : kCongrats) if (npc == congrats.npc && string == congrats.string) quest::set(quest_bits, kQuest, congrats.bit);
        return false;
    }
    // A statue's event 7: its Ancient's superunique to make (-1 none).
    int statue_wakes(int which);
    // An Ancient fell while they're up: its statue's due back, the return timer on.
    bool ancient_fell(int statue);
    std::array<bool, 3> statues_back();
    // An Ancient died: true when it was the last, the Rite won (state 5, the log 13, the chain); then each
    // player's reward().
    bool ancient_died();
    // A player's share: on the Summit, of the level, not done: bits 13 and 0 and the experience; else bit 14.
    bool reward(QuestBits& quest_bits, bool on_summit, int clvl, int difficulty) const;
    // FUN_0058c5c0: 1.4M / 20M / 40M, at most what the next level takes.
    static constexpr std::array<std::int64_t, 3> kExperience{ 1400000, 20000000, 40000000 };
    // The Rite not won, the Ancients go, the statues and the altar back to
    // mode 0 (FUN_0058c000).
    bool reset() {
        if (done) return false;
        heard = awake = fighting = returning = false;
        statue_mode = {};
        statue_dead = {};
        made = alive = 0;
        altar_mode = 0;
        return true;
    }
    // A player died on the Summit (FUN_00580ec0 -> FUN_005350f0 ->
    // FUN_0058d560): with them awake and no portal open, and no other
    // player left there (`others`, LAB_0058d510's count), reset.
    bool died(bool others) { return awake && portals == 0 && !others && reset(); }
    // A town portal opened on the Summit: not won, one more open and reset.
    bool portal_opened();
    // One closed there: the last, heard again, wakes them.
    void portal_closed();
    // The stairs: whether they let the player on.
    [[nodiscard]] bool stairs_pass(const QuestBits& quest_bits) const;
    // Blocks (0x735100 by 0x7357e4; FUN_0058c440).
    static inline const std::vector<act5_detail::Entry> kBlocks[6] = {
        { { 515, 20153, true } },
        { { 511, 20157, false }, { 520, 20155, false }, { 513, 20158, false }, { 515, 20154, false }, { 512, 20156, false } },
        { { 511, 20162, false }, { 520, 20160, false }, { 512, 20161, false }, { 513, 20163, false }, { 515, 20159, false } },
        { { 511, 20167, false }, { 520, 20165, false }, { 512, 20166, false }, { 513, 20168, false }, { 515, 20164, false } },
        { { 537, 20002, true }, { 538, 20002, true }, { 539, 20002, true } },
        { { 511, 20167, true }, { 520, 20165, true }, { 512, 20166, true }, { 513, 20168, true }, { 515, 20164, true } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const;
    // The "!" (FUN_0058ccf0).
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const { return npc == kQualKehk && state == 1 && !quest::bit(quest_bits, kQuest, 0); }
    // The log: the records' default (+0xe8 none; done at +0xd 4).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
};

// Eve of Destruction (a5q6.cpp, word 0x28, record 0x24, FUN_0058e500).
// State: 0 shut, 2 open (a5q5's chain, LAB_0058e390), 3 out of Harrogath
// after, 5 Baal dead. Baal on his throne (baalthrone, the BaalThrone think
// FUN_005ef320) sends his five Subjects (superuniques 61..65), each when the
// room's clear; then he's baalcrabstairs (0x22f), walking into the
// Worldstone Chamber's portal (object 563, the BaalToStairs think
// FUN_005ef620: record +0x86, the log 3), which then lets players through
// (OperateFn 70). Baal (baalcrab, bound to 0x24) dead (FUN_0058df20): those
// in the Chamber get bits 13 and 0 and the next difficulty (FUN_0058dd30),
// the rest bit 14; the log 4. Tyrael (0x209) comes as Baal's light fades
// (missile 625's end, 650 frames: FUN_0058e920); his talk closed, the last
// portal (object 565, OperateFn 72) home: bit 10. Log: 1 told, 2 the Throne,
// 3 Baal gone in, 4 dead.
struct EveQuest {
    static constexpr int kQuest = 0x28, kTyrael = 0x209, kCain = act5_detail::kCain, kThrone = 131, kChamber = 132;
    static constexpr int kChamberPortal = 563, kThronePortal = 569, kLastPortal = 565, kWaves = 5, kFirstSubject = 61;   // 563 / 569: both OperateFn 70
    static constexpr int kBaalThrone = 543, kBaal = 544, kBaalStairs = 0x22f;
    static constexpr int kTyraelFrames = 650, kCryFrames = 250, kWaveFrames = 100;
    static constexpr int kEndMovie = 7;   // the last portal's cinematic (S->C 0x61 {7}: D2X_OUT)
    struct Congrats { int npc, string, bit; };
    static constexpr std::array<Congrats, 6> kCongrats{ { { 511, 20178, 4 }, { 520, 20177, 5 }, { 513, 20179, 6 }, { 521, 20175, 7 }, { 515, 20180, 8 }, { 512, 20176, 9 } } };
    bool open = true;
    int state = 0, log = 0;
    bool told = false;             // data +0x90
    bool baal_in = false;          // data +0x86
    bool tyrael_heard = false;     // data +0x98
    bool news = false;
    // The mode the next Baal's portal (InitFn 75) / last portal (InitFn 77) starts in: data +0x94 / +0x9c,
    // 1 at the record's alloc (FUN_0058e500), 2 once one's made (FUN_0058e670 / FUN_0058e710).
    int chamber_portal_mode = 1, last_portal_mode = 1;

    // A later player joins (+0xd8, LAB_0058e3f0): done, bit 10.
    static void join_more(QuestBits& quest_bits) { if (quest::bit(quest_bits, kQuest, 0)) quest::set(quest_bits, kQuest, 10); }
    // The player joins: done, bit 10 and the next difficulty.
    bool join(QuestBits& quest_bits);
    // a5q5's chain.
    void opened(QuestBits& quest_bits, bool in_act5);
    // FUN_0058d8b0: the steps marked, for a player in act 5.
    void mark(QuestBits& quest_bits, bool in_act5) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1) || !in_act5) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, 3);
    }
    // A level change.
    void enter(QuestBits& quest_bits, int from, int into);
    // Baal into the Chamber (FUN_0058e600).
    void baal_gone_in() { baal_in = true; if (open && log < 3) { log = 3; news = true; } }
    // Baal dead (FUN_0058df20). A player's share (FUN_0058dea0 / FUN_0058dde0):
    // in the Chamber, not done: bits 13 and 0, and true (the next
    // difficulty); else bit 14.
    void killed() { if (!open) return; log = 4; news = true; state = 5; }
    static bool reward(QuestBits& quest_bits, bool in_chamber);
    // Tyrael's talk closes (LAB_0058d870): his portals, once.
    bool talk_closed(int npc) { return npc == kTyrael && !std::exchange(tyrael_heard, true); }
    // Heard: the town's congratulations.
    static void said(QuestBits& quest_bits, int npc, int string);
    // Blocks (0x735800 by 0x735ee4; FUN_0058d9f0).
    static inline const std::vector<act5_detail::Entry> kBlocks[6] = {
        { { 511, 20171, false }, { 520, 20170, false }, { 513, 20172, false }, { 515, 20174, false }, { 512, 20173, false } },
        { { 511, 20178, true }, { 520, 20177, true }, { 513, 20179, true }, { 521, 20175, true }, { 515, 20180, true }, { 512, 20176, true } },
        { { 511, 20178, true }, { 513, 20179, true }, { 521, 20175, true }, { 515, 20180, true }, { 512, 20176, true } },
        { { 511, 20178, false }, { 513, 20179, false }, { 521, 20175, false }, { 515, 20180, false }, { 512, 20176, false } },
        { { 520, 20177, true } },
        { { 520, 20177, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const;
    // The "!": once done, each who's yet to say theirs.
    [[nodiscard]] static bool alert(const QuestBits& quest_bits, int npc);
    // The log: the records' default (+0xe8 none; done at +0xd 4).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
};

// FUN_005459a0 (InitFn 68's town Nihlathak, LAB_0058a610; Anya's markers call
// it too): where a marker's NPC is made, quests-act5.md "Prison of Ice".
// `rect` is the marker room's subtiles {x, y, width, height}. The callbacks:
// `free_spot(room, x, y)` FUN_00545340 (size 2, mask 0x100, 100 rings; moves the
// spot, false when none), `near_room(room, x, y)` FUN_00463740 (the room near
// `room` holding the spot, nullopt when none), `make_at(room, x, y, retry)`
// FUN_005b2f20 (true when made). True when made.
template <class Room, class Free, class Near, class Make>
bool marker_npc(Room marker_room, const std::array<int, 4>& rect, int marker_x, int marker_y, Free&& free_spot, Near&& near_room, Make&& make_at) {
    int x = marker_x, y = marker_y, step = 0;
    const auto inside = [&]() { return x >= rect[0] && y >= rect[1] && x < rect[0] + rect[2] - 1 && y < rect[1] + rect[3] - 1; };
    while (step < 21 && !inside()) { ++x; ++y; ++step; }
    if (step == 21) x = marker_y;                                                  // bugs.md #47: x takes the marker's y (0x545a0d)
    Room room = marker_room;
    if (!free_spot(room, x, y)) { x = marker_x; y = marker_y; }
    if (make_at(room, x, y, -1) || make_at(room, x, y, 5)) return true;
    for (int attempt = 0; attempt < 20; ++attempt) {                               // cmp 0x14 at 0x545b04
        ++x; ++y;
        if (const std::optional<Room> held = near_room(room, x, y)) room = *held;    // 0x545a97
        else { room = marker_room; x = marker_x; y = marker_y; }
        if (!free_spot(room, x, y)) { room = marker_room; x = marker_x; y = marker_y; }   // 0x545ad5
        if (make_at(room, x, y, 10)) return true;
    }
    return make_at(marker_room, marker_x, marker_y, 15);
}

// The marker fields owned by a game's quest records, not by any level whose
// rooms happen to contain their preset objects.
struct MarkerRecords {
    bool larzuk_made = false;                         // record 0x1f data +0x15 (FUN_00587840)
};

// InitFn 71 (FUN_00587840, object 543, quest record 0x1f): Larzuk's marker.
// `made` is the record's byte +0x15 (set when he's made, and no second is).
// `free_spot(room, x, y)` FUN_00545340 (size 2, mask 0x100, 100 rings) from the
// marker's spot: moves it and hands the room holding it (false: none, and he
// isn't made); `make_at(room, x, y, radius)` FUN_005b2f20 (class 0x1ff, mode 1,
// flags 0) in that room, once, at radius 5 (rings 3..15: the spot itself isn't
// tried, as FUN_005459a0's -1 try is). True when made.
template <class Room, class Free, class Make>
bool larzuk_marker(bool& made, Room marker_room, int marker_x, int marker_y, Free&& free_spot, Make&& make_at) {
    if (made) return false;
    Room room = marker_room;
    int x = marker_x, y = marker_y;
    if (!free_spot(room, x, y)) return false;
    if (!make_at(room, x, y, 5)) return false;
    made = true;                                                                   // 0x5878b6
    return true;
}

}  // namespace d2d::rules
