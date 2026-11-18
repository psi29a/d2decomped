// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// MonUMod 34 (ai_after_death, the Reanimated Horde): hook 1 (FUN_005a3840)
// queues its rise as it dies, hook 2 (FUN_005a3910) raises it or tries again.
// tests/ai_after_death_cases.inc is game.exe's own answer
// (tools/emu/ai_after_death.py inc); the rules must give it.

#include <rules.hpp>
#include <uniques.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::rules;

int main() try {
    int queues = 0, rises = 0;
#define QUEUE(mode, align, chance, aip1, seed_low, seed_high, want_cleared, want_frames, new_low, new_high) \
    { Rng rng; rng.low = seed_low; rng.high = seed_high; \
      const auto got = ai_after_death_queue(mode, align, chance, aip1, rng); \
      assert(got.cleared == bool(want_cleared)); \
      assert((got.frames ? *got.frames : -1) == want_frames); \
      assert(rng.low == new_low && rng.high == new_high); ++queues; }
#define RISE(dead, hidden, unique, blocked, can, align, scratch, aip1, want_kind, want_frames, drops, put, flags) \
    { const auto got = ai_after_death_rise({ bool(dead), bool(hidden), bool(unique), bool(blocked), bool(can), align, scratch, aip1 }); \
      assert(int(got.kind) == want_kind && got.frames == want_frames); \
      if (want_kind == 2) { assert(put == scratch + 1 && (0xffffffffu & kRiseFlagsKept) == flags && want_frames == kRiseThink); } \
      else assert(flags == 0xffffffffu && put == -1); \
      ++rises; }
#include "ai_after_death_cases.inc"
#undef QUEUE
#undef RISE
    assert(queues == 3000 && rises == 768);
    assert(kRiseSkill == 0x125 && kRiseMode == 8 && kRiseLimit == 2);
    std::puts("test_ai_after_death: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_ai_after_death: %s\n", error.what());
    return 1;
}
