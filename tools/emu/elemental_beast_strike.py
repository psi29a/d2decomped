# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ElementalBeast's in-melee S1 strike scheduling in FUN_005f6b70.

The stock think runs on synthetic AI-control and parameter records. Its mode
setter is a boundary; the event queue is observed directly. No game is run.
"""

import sys
from pathlib import Path

import _monster_world as harness

THINK = 0x5F6B70
SET_MODE = 0x5DDF90
QUEUE_EVENT = 0x5417D0
MUTATIONS = ("same_frame", "wrong_kind", "state_unchanged")
mutation = None


def expected(frame):
    """Return the modeled event and post-think state, with one optional defect."""
    kind = 1 if mutation == "wrong_kind" else 0
    delay = 0 if mutation == "same_frame" else 1
    state = 1 if mutation == "state_unchanged" else 2
    return kind, frame + delay, state


def run_all(captured=None):
    """Run the stock in-melee branch for varied frames and target pointers."""
    world = harness.World()
    machine = world.m
    seen = []

    def set_mode(current):
        seen.append(("mode", current.mu.reg_read(harness.UC_X86_REG_ECX), current.mu.reg_read(harness.UC_X86_REG_EDX),
                     current.arg(0), current.arg(1)))
        return 0

    def queue_event(current):
        seen.append(("event", current.arg(0), current.arg(1), current.arg(2), current.arg(3)))
        return 0

    machine.hook(SET_MODE, set_mode, 2)
    machine.hook(QUEUE_EVENT, queue_event, 4)
    cases = 0
    for frame in (0, 1, 17, 0x123456, 0x7FFFFFFE):
        for target in (0, 0x10203040):
            game = machine.alloc(0xB0)
            control = machine.alloc(0x20)
            params = machine.alloc(0x24)
            row = machine.alloc(0x80)
            machine.w32(game + 0xA8, frame)
            machine.w32(control + 0x14, 1)
            machine.w32(params, control)
            machine.w32(params + 2 * 4, target)
            machine.w32(params + 6 * 4, 1)
            machine.w32(params + 7 * 4, row)
            seen.clear()
            world.run(THINK, params, ecx=game, nstack=1)
            events = [call for call in seen if call[0] == "event"]
            modes = [call for call in seen if call[0] == "mode"]
            want_kind, want_frame, want_state = expected(frame)
            harness.require(len(modes) == 1, f"frame {frame}: mode calls {modes}")
            harness.require(events == [("event", want_kind, want_frame & 0xFFFFFFFF, 0, 0)],
                            f"frame {frame}: events {events}")
            harness.require(machine.r32(control + 0x14) == want_state,
                            f"frame {frame}: state {machine.r32(control + 0x14)}")
            if captured is not None:
                captured.append((events[0][2] - frame) & 0xFFFFFFFF)
            cases += 1
    return cases


if __name__ == "__main__":
    if "--dump" in sys.argv:
        dump_index = sys.argv.index("--dump")
        output = Path(sys.argv[dump_index + 1])
        del sys.argv[dump_index:dump_index + 2]
        delays = []
        run_all(delays)
        harness.require(len(set(delays)) == 1, f"strike delays differ: {delays}")
        output.write_text(f"// tools/emu/elemental_beast_strike.py --dump: FUN_005f6b70's event delay.\n{delays[0]}\n")
    else:
        harness.drive(__doc__, sys.modules[__name__], run_all, "FUN_005f6b70 in-melee strike cases")
