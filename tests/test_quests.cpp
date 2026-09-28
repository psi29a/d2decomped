// The Den of Evil through a game: Akara gives it, the Den's cleared, she
// rewards once; a later game picks the state up from the flags.
#include <quests.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::rules;

int main() {
    QuestBits f{};
    DenQuest q;
    auto akara = [&] { return q.talk(f, 148); };

    assert(q.alert(f, 148) && !q.alert(f, 150));
    // Not given: Akara greets with it; the others say nothing.
    assert(akara().size() == 1 && akara()[0].string == 64 && akara()[0].greet);
    assert(q.talk(f, 150).empty());
    assert(!q.said(f, 148, 64) && q.state == 2 && qbit(f, 1, 2));
    assert(q.talk(f, 150).size() == 1 && q.talk(f, 150)[0].string == 66 && !q.talk(f, 150)[0].greet);

    // Into the Den: bit 4; the others' early-return topics.
    q.enter_den(f);
    assert(q.state == 3 && qbit(f, 1, 4) && !qbit(f, 1, 3));
    assert(akara()[0].string == 71 && !akara()[0].greet);

    // A new game from these flags: back in the Den's state.
    DenQuest again;
    again.join(f);
    assert(again.state == 3 && again.log == 2);

    assert(q.killed(f, 9) == DenQuest::Kill::none && q.log == 2);
    assert(q.killed(f, 5) == DenQuest::Kill::few && q.log == 4);         // "Monsters remaining"

    assert(q.killed(f, 0) == DenQuest::Kill::cleared && q.state == 4);
    assert(qbit(f, 1, 1) && qbit(f, 1, 13) && q.killed(f, 0) == DenQuest::Kill::none);

    // Akara's reward, once.
    assert(akara().size() == 1 && akara()[0].string == 76 && akara()[0].greet);
    assert(q.talk(f, 147)[0].string == 79);
    assert(q.said(f, 148, 76) && qbit(f, 1, 0) && !qbit(f, 1, 1) && !qbit(f, 1, 4));
    assert(qbit(f, 41, 1) && qbit(f, 41, 13));
    assert(!q.said(f, 148, 76) && akara().empty() && !q.alert(f, 148));

    // Cleared without talking to her first: still her reward.
    QuestBits g{};
    DenQuest r;
    r.enter_den(g);
    assert(r.killed(g, 0) == DenQuest::Kill::cleared && r.talk(g, 148)[0].string == 76);
    std::puts("ok");
}
