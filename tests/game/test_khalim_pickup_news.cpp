// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Khalim's item event FUN_005b8210 chooses a direct S->C 0x5d state and
// FUN_005b81c0 sends it only to that event player's client. The stock matrix
// is frozen by tools/emu/khalim_pickup_news.py.
#include <gamedata.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <string_view>
#include <variant>

using namespace d2d::game;
using Khalim = d2d::rules::KhalimQuest;

namespace {

struct OracleCase {
    const char* code;
    int carried_mask;
    bool bit5;
    bool bit7;
    bool prerequisite;
    bool done;
    int state;
    int record_log;
};

constexpr std::array<const char*, 5> kCodes{ Khalim::kEyeCode, Khalim::kBrainCode, Khalim::kHeartCode,
                                             Khalim::kFlailCode, Khalim::kWillCode };

// Decodes the oracle's eye/brain/heart/flail/Will bit mask.
Khalim::Parts parts_of(int mask) {
    return { .eye = (mask & 1) != 0, .brain = (mask & 2) != 0, .heart = (mask & 4) != 0,
             .flail = (mask & 8) != 0, .will = (mask & 16) != 0 };
}

}  // namespace

// Replays every frozen stock branch through the rules helper and the World's
// one-player event path. Without the port, the World raises no a3q2 news.
int main() {
    static constexpr OracleCase oracle_cases[]{
#include "khalim_pickup_news_cases.inc"
    };
    GameData data;
    World world(&data, -1, -1);
    namespace quest = d2d::rules::quest;
    for (const auto& sample : oracle_cases) {
        d2d::rules::QuestBits bits{};
        quest::set(bits, 0xf, 0, sample.prerequisite);
        quest::set(bits, Khalim::kQuest, 0, sample.done);
        quest::set(bits, Khalim::kQuest, 5, sample.bit5);
        quest::set(bits, Khalim::kQuest, 7, sample.bit7);
        Khalim record;
        record.log = 3;
        const int state = record.pickup_log_state(bits, sample.code, parts_of(sample.carried_mask)).value_or(-1);
        assert(state == sample.state);
        assert(record.log == sample.record_log);

        world.quests() = bits;
        world.khalim = {};
        world.khalim.log = 3;
        world.actor->events.clear();
        world.actor->held.reset();
        world.actor->character.items.clear();
        for (std::size_t index = 0; index < kCodes.size(); ++index)
            if (sample.carried_mask & (1 << index)) world.actor->character.items.push_back({ .code = kCodes[index] });
        world.item_event(sample.code, 0);
        const auto sent = std::ranges::count_if(world.actor->events, [](const Event& event) {
            const auto* news = std::get_if<ev::QuestNews>(&event);
            return news && news->quest == Khalim::kQuest;
        });
        assert(sent == (sample.state >= 0 ? 1 : 0));
        assert(world.khalim.log == sample.record_log);
    }
    std::printf("OK: %zu a3q2 pickup sends match FUN_005b8210 -> FUN_005b81c0\n", std::size(oracle_cases));
}
