// The Den of Evil through a game: Akara gives it, the Den's cleared, she
// rewards once; a later game picks the state up from the flags.
#include <quests.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::rules;

int main() {
    QuestBits quest_bits{};
    DenQuest den;
    auto akara = [&] { return den.talk(quest_bits, 148); };

    assert(den.alert(quest_bits, 148) && !den.alert(quest_bits, 150));
    // Not given: Akara greets with it; the others say nothing.
    assert(akara().size() == 1 && akara()[0].string == 64 && akara()[0].greet);
    assert(den.talk(quest_bits, 150).empty());
    assert(!den.said(quest_bits, 148, 64) && den.state == 2 && qbit(quest_bits, 1, 2));
    assert(den.talk(quest_bits, 150).size() == 1 && den.talk(quest_bits, 150)[0].string == 66 && !den.talk(quest_bits, 150)[0].greet);

    // Into the Den: bit 4; the others' early-return topics.
    den.enter_den(quest_bits);
    assert(den.state == 3 && qbit(quest_bits, 1, 4) && !qbit(quest_bits, 1, 3));
    assert(akara()[0].string == 71 && !akara()[0].greet);

    // A new game from these flags: back in the Den's state.
    DenQuest again;
    again.join(quest_bits);
    assert(again.state == 3 && again.log == 2);

    assert(den.killed(quest_bits, 9) == DenQuest::Kill::none && den.log == 2);
    assert(den.killed(quest_bits, 5) == DenQuest::Kill::few && den.log == 4);         // "Monsters remaining"

    assert(den.killed(quest_bits, 0) == DenQuest::Kill::cleared && den.state == 4);
    assert(qbit(quest_bits, 1, 1) && qbit(quest_bits, 1, 13) && den.killed(quest_bits, 0) == DenQuest::Kill::none);

    // Akara's reward, once.
    assert(akara().size() == 1 && akara()[0].string == 76 && akara()[0].greet);
    assert(den.talk(quest_bits, 147)[0].string == 79);
    assert(den.said(quest_bits, 148, 76) && qbit(quest_bits, 1, 0) && !qbit(quest_bits, 1, 1) && !qbit(quest_bits, 1, 4));
    assert(qbit(quest_bits, 41, 1) && qbit(quest_bits, 41, 13));
    assert(!den.said(quest_bits, 148, 76) && akara().empty() && !den.alert(quest_bits, 148));

    // Cleared without talking to her first: still her reward.
    QuestBits other_bits{};
    DenQuest other_den;
    other_den.enter_den(other_bits);
    assert(other_den.killed(other_bits, 0) == DenQuest::Kill::cleared && other_den.talk(other_bits, 148)[0].string == 76);
    std::puts("ok");
}
