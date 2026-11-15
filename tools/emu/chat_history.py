# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's chat box keys: Enter, Backspace, Esc, the click, and the 8-line history.

    uv run python chat_history.py [--exe path/to/Game.exe] [--steps N] [--seed S]

The box's handlers come from the table at 0x712f4c (FUN_0047b090 registers it):
WM_CHAR -> FUN_0047ca70, keys 0x26 / 0x28 (Up / Down) -> FUN_0047bca0,
WM_LBUTTONDOWN -> FUN_0047bc70. A message: +8 wParam, +0xc lParam, +0x18
handled, +0x1c result. The box text is UTF-16 at 0x7bb638 with its count at
0x7bb5e8; 0x7bb5ec says the box takes characters.

* FUN_0047ca70 (ret 4) does nothing while 0x7bb5ec < 1. Else wParam 8 drops the
  last unit (count - 1, that unit 0) when count > 0; 0x1b shuts the box
  (FUN_00455f20(5, 1, 0)); 0xd skips leading units under 0x100 that isspace
  calls blank. Nothing left: the box shuts. Else the rest goes into the history
  (FUN_0047ba00), the whole box is converted (WideCharToMultiByte(CP_ACP,
  0x250, box, -1, local, 0x100, 0, 0)) and FUN_0047c420(local, blanks) runs
  with ECX = the box. Then (every path but the first) handled 1, result 0,
  FUN_00420290(message).
* The history: 8 slots of 0x202 bytes at 0x7bb840; 0x712f44 the slot written
  last, 0x712f48 the next to reuse once all 8 are full, 0x7bb5d8 the slot Up
  shows next, 0x7bb5e4 (u16) how many are full. FUN_0047ba00 (EDX text) writes
  slots 7, 6, ... 0, then reuses 0x712f48 and steps it down (7 after 0), and
  points 0x7bb5d8 at the slot written. FUN_0047ba80(step) moves 0x7bb5d8 by
  step, 8 -> 0 and -1 -> 7, but stays put when fewer than 7 are full and the
  new slot isn't one of them (8 - slot > full). With exactly 7 full the test is
  skipped, so the move from slot 7 reaches slot 0, never written: Up shows an
  empty line once a round. FUN_0047b9c0 copies 0x7bb5d8's slot into the box and
  sets the count to its length. FUN_0047bd30 resets 0x712f44 = 0x712f48 = 7,
  0x7bb5d8 = full = count = 0 (the slots keep their text).
* FUN_0047bca0 (ret 4): handled 1, result 0, FUN_00420290; nothing more for a
  repeat (lParam bit 30); Up shows 0x7bb5d8's slot then steps +1, Down steps
  -1 then shows.
* FUN_0047bc70 (ret 4): a click whose y (lParam's high word) is below
  H - 0x31 (0x711470; signed) shuts the box.
The parser, the UI toggle, FUN_00420290 and WideCharToMultiByte are hooked; the
CRT (isspace) and FUN_00526800 run for real. No MPQs are read.
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
ON_CHAR = 0x47CA70
ON_KEY = 0x47BCA0
ON_CLICK = 0x47BC70
RESET = 0x47BD30
PARSE = 0x47C420
UI_TOGGLE = 0x455F20
MESSAGE_DONE = 0x420290
HANDLERS = 0x712F4C
BOX = 0x7BB638
COUNT = 0x7BB5E8
TAKES_KEYS = 0x7BB5EC
SLOTS = 0x7BB840
LAST = 0x712F44
REUSE = 0x712F48
SHOWN = 0x7BB5D8
FULL = 0x7BB5E4
HEIGHT = 0x711470
SLOT = 0x202
LIMIT = 200_000


def require(condition, message):
    """Stop when game.exe's code disagrees with the claim."""
    if not condition:
        raise RuntimeError(message)


def wide(units):
    """UTF-16 units as bytes, with the 0."""
    return b"".join(unit.to_bytes(2, "little") for unit in units) + b"\0\0"


def units_at(data):
    """The units before the first 0."""
    out = []
    for index in range(0, len(data) - 1, 2):
        unit = int.from_bytes(data[index:index + 2], "little")
        if not unit:
            break
        out.append(unit)
    return out


class Model:
    """The claim: the box, its count and the history."""

    def __init__(self):
        self.slots = [[] for _ in range(8)]
        self.written = [False] * 8
        self.last = self.reuse = 7
        self.shown = self.full = self.count = 0
        self.box = []
        self.log = []
        self.empty_recalls = 0

    def reset(self):
        self.last = self.reuse = 7
        self.shown = self.full = self.count = 0

    def push(self, text):
        if self.full == 8:
            self.last = self.reuse
            self.reuse = 7 if self.reuse == 0 else self.reuse - 1
        else:
            if self.full:
                self.last = 7 if self.last == 0 else self.last - 1
            self.full += 1
        self.shown = self.last
        self.slots[self.last] = list(text)
        self.written[self.last] = True

    def step(self, delta):
        slot = self.shown + delta
        slot = 0 if slot >= 8 else 7 if slot < 0 else slot
        if self.full < 7 and 8 - slot > self.full:
            return
        self.shown = slot

    def recall(self):
        if not self.written[self.shown]:
            self.empty_recalls += 1
        self.box = list(self.slots[self.shown])
        self.count = len(self.box)

    def char(self, code, takes):
        if takes < 1:
            return
        if code == 8:
            if self.count:
                self.count -= 1
                self.box = self.box[:self.count]
        elif code == 0x1B:
            self.log.append("ui 5 1 0")
        elif code == 0x0D:
            blanks = 0
            while blanks < len(self.box) and self.box[blanks] < 0x100 and self.box[blanks] in (9, 10, 11, 12, 13, 32):
                blanks += 1
            if blanks == len(self.box):
                self.log.append("ui 5 1 0")
            else:
                self.push(self.box[blanks:])
                text = "".join(chr(unit) if unit < 0x100 else "?" for unit in self.box)
                self.log.append(f"parse {blanks} {text}")
        self.log.append("done")

    def key(self, code, repeat):
        self.log.append("done")
        if repeat:
            return
        if code == 0x26:
            self.recall()
            self.step(1)
        elif code == 0x28:
            self.step(-1)
            self.recall()

    def click(self, y, height):
        if y > height - 0x31:
            self.log.append("ui 5 1 0")


def main():
    """Drive the box's handlers through random keys and compare every step with the model."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", help="game.exe 1.14d (default: <D2_MPQ_DIR>/bin/game.exe)")
    parser.add_argument("--steps", type=int, default=20000)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    exe = Path(args.exe) if args.exe else Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    log = []
    converted = {}

    def wide_to_multi(e):
        """WideCharToMultiByte(page, flags, text, -1, out, size, 0, 0): Latin-1, '?' past 0xff."""
        page, flags, text, length, out, size = (e.arg(index) for index in range(6))
        units = units_at(e.read(text, SLOT))
        data = bytes(unit if unit < 0x100 else 0x3F for unit in units)[:size - 1] + b"\0"
        e.mu.mem_write(out, data)
        converted.update(page=page, flags=flags, text=text, length=length, size=size)
        return len(data), 8

    emu.Emu.w_WideCharToMultiByte = wide_to_multi
    machine = emu.Emu(str(exe))
    machine.crt_init()
    require([machine.r32(HANDLERS + 12 * row + 4) for row in range(4)] == [0x102, 0x26, 0x28, 0x201]
            and [machine.r32(HANDLERS + 12 * row + 8) for row in range(4)] == [ON_CHAR, ON_KEY, ON_KEY, ON_CLICK],
            "the box's handler table at 0x712f4c")
    require(machine.read(SLOTS, 8 * SLOT) == b"\0" * 8 * SLOT, "the history starts empty")

    def parse(e):
        require(e.mu.reg_read(UC_X86_REG_ECX) == BOX, "FUN_0047c420 gets the box in ECX")
        require(converted == {"page": 0, "flags": 0x250, "text": BOX, "length": 0xFFFFFFFF, "size": 0x100},
                f"WideCharToMultiByte arguments {converted}")
        log.append(f"parse {e.arg(1)} {e.cstr(e.arg(0))}")
        return 0

    machine.hook(PARSE, parse, 2)
    machine.hook(UI_TOGGLE, lambda e: log.append(
        f"ui {e.mu.reg_read(UC_X86_REG_ECX)} {e.mu.reg_read(UC_X86_REG_EDX)} {e.arg(0)}") or 1, 1)
    message = machine.alloc(0x20)

    def done(e):
        require(e.arg(0) == message and e.r32(message + 0x18) == 1 and e.r32(message + 0x1c) == 0,
                "handled 1, result 0 before FUN_00420290")
        log.append("done")
        return 0

    machine.hook(MESSAGE_DONE, done, 1)
    machine.call(RESET)
    model = Model()
    rng = random.Random(args.seed)
    alphabet = [0x41, 0x62, 0x2F, 0xE9, 0x20, 0x09, 0x0D, 0xA0, 0x3000, 0x4E2D]
    for step in range(args.steps):
        log.clear()
        model.log = []
        machine.w32(message + 0x18, 0)
        machine.w32(message + 0x1C, 7)
        roll = rng.random()
        if roll < 0.3:
            units = [rng.choice(alphabet) for _ in range(rng.choice([0, 1, 3, 8, 40]))]
            machine.mu.mem_write(BOX, wide(units).ljust(SLOT, b"\0"))
            machine.w32(COUNT, len(units))
            model.box, model.count = units, len(units)
            action = f"enter {units}"
            takes = 0 if rng.random() < 0.05 else 1
            machine.w32(TAKES_KEYS, takes)
            machine.w32(message + 8, 0x0D)
            machine.call(ON_CHAR, message, limit=LIMIT)
            model.char(0x0D, takes)
        elif roll < 0.4:
            code = rng.choice([8, 8, 0x1B])
            action = f"char {code:#x}"
            takes = 0 if rng.random() < 0.05 else 1
            machine.w32(TAKES_KEYS, takes)
            machine.w32(message + 8, code)
            machine.call(ON_CHAR, message, limit=LIMIT)
            model.char(code, takes)
        elif roll < 0.95:
            code = rng.choice([0x26, 0x26, 0x28])
            repeat = rng.random() < 0.1
            action = f"key {code:#x}{' repeat' if repeat else ''}"
            machine.w32(message + 8, code)
            machine.w32(message + 0xC, 0x40000000 if repeat else 0)
            machine.call(ON_KEY, message, limit=LIMIT)
            model.key(code, repeat)
        elif roll < 0.99:
            height = rng.choice([480, 600])
            y = rng.randint(height - 0x40, height + 4) if rng.random() < 0.8 else rng.randint(0, 0xFFFF)
            action = f"click y {y} h {height}"
            machine.w32(HEIGHT, height)
            machine.w32(message + 0xC, y << 16)
            machine.call(ON_CLICK, message, limit=LIMIT)
            model.click(y, height)
        else:
            action = "reset"
            machine.call(RESET)
            model.reset()
        got = {"last": machine.r32(LAST), "reuse": machine.r32(REUSE), "shown": machine.r32(SHOWN),
               "full": machine.r16(FULL), "count": machine.r32(COUNT), "box": units_at(machine.read(BOX, SLOT))[:model.count] if model.count else [],
               "slots": [units_at(machine.read(SLOTS + SLOT * slot, SLOT)) for slot in range(8)], "log": list(log)}
        want = {"last": model.last, "reuse": model.reuse, "shown": model.shown, "full": model.full,
                "count": model.count, "box": model.box[:model.count], "slots": model.slots, "log": model.log}
        require(got == want, f"step {step} ({action}): game.exe {got}, claim {want}")
    print(f"chat_history: {args.steps} steps match (Enter, Backspace, Esc, Up, Down, repeats, clicks, resets)")
    print(f"chat_history: {model.empty_recalls} recalls showed a slot never written (7 lines kept)")

    # A fresh history, n lines sent, then Up n + 2 times: what the box shows each time.
    for sent in (3, 7, 8):
        machine.mu.mem_write(SLOTS, b"\0" * 8 * SLOT)
        machine.call(RESET)
        machine.w32(TAKES_KEYS, 1)
        for line in range(sent):
            units = [ord(letter) for letter in f"line {line + 1}"]
            machine.mu.mem_write(BOX, wide(units).ljust(SLOT, b"\0"))
            machine.w32(COUNT, len(units))
            machine.w32(message + 8, 0x0D)
            machine.call(ON_CHAR, message, limit=LIMIT)
        shown = []
        for _ in range(sent + 2):
            machine.w32(message + 8, 0x26)
            machine.w32(message + 0xC, 0)
            machine.call(ON_KEY, message, limit=LIMIT)
            shown.append("".join(map(chr, units_at(machine.read(BOX, SLOT)))) or "(empty)")
        want = [f"line {sent - index % sent}" for index in range(sent + 2)] if sent == 8 else \
            [f"line {sent - index}" for index in range(sent)] + (["(empty)", f"line {sent}"] if sent == 7 else
                                                                 ["line 1"] * 2)
        require(shown == want, f"{sent} lines, Up x{sent + 2}: {shown}, claim {want}")
        print(f"  {sent} lines sent, Up x{sent + 2}: {', '.join(shown)}")


if __name__ == "__main__":
    main()
