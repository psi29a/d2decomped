// SPDX-License-Identifier: GPL-3.0-or-later
// A controller hold that confirms one action. A short press of an eligible
// item clicks on release. Leaving the item or the context cancels, and that
// release does not click. A finished hold does not click either, and the
// button has to come up before another hold. A button that is already down
// when the item becomes eligible does not start one.
#pragma once

namespace d2d::client::hold_confirm {

enum class Step { Idle, Pending, Click, Act };

struct Hold {
    float delay = 0.35f;
    [[nodiscard]] float fraction() const { return !armed || cancelled || delay <= 0.f ? 0.f : time / delay < 1.f ? time / delay : 1.f; }

    Step step(bool edge, bool held, bool eligible, int item_id, int context_id, float seconds) {
        if (!held) {
            const bool click = armed && !fired && !cancelled;
            clear();
            return click ? Step::Click : Step::Idle;
        }
        if (edge) {
            clear();
            if (!eligible) return Step::Idle;
            armed = true;
            item = item_id;
            context = context_id;
            return Step::Pending;
        }
        if (!armed || cancelled || fired) return Step::Idle;
        if (!eligible || item_id != item || context_id != context) {
            cancelled = true;
            return Step::Idle;
        }
        if (seconds > 0.f) time += seconds;
        if (time >= delay) {
            fired = true;
            return Step::Act;
        }
        return Step::Pending;
    }

    void clear() { const float kept = delay; *this = Hold{}; delay = kept; }

    int item = -1;
    int context = 0;
    float time = 0.f;
    bool armed = false;
    bool fired = false;
    bool cancelled = false;
};

}  // namespace d2d::client::hold_confirm
