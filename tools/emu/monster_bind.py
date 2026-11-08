# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify three MONSTER entry points that bind a monster or announce it, in stock game.exe 1.14d.

    uv run python monster_bind.py [--mutation-check]

Runs game.exe's own code against a hand-written model with every callee hooked
and logged (registers and stack arguments, in call order), and compares the
callee log, the bytes the function wrote and the bytes the `ret n` popped.
EAX on return is not claimed for any of them.

  FUN_00573270 ECX game, EDX owner, stack (monster, id, record, flag), `ret 0x10`
      (summon / mercenary bind): returns at once when owner, monster or record
      is null or the owner's type (+0x0) is not 0. Otherwise ORs 0x4020200 into
      monster +0xc4, makes the callees in the log, builds a 12-byte block
      {record dword +4, record word +0, first dword of the 0x48-byte buffer
      that 0x66_37f0 fills (the block overlaps the buffer's last 12 bytes in the frame; the oracle's stand-in writes only the first 0x3c)}, ORs 0x80000000 into monster +0xc4, and (id word
      0xffff only) makes three more calls. Returns early when 0x66_37f0 is 0
      or, for id 0xffff, 0x65_62f0 is 0. When the monster is type 1 with data,
      it zeroes data bytes +4, +5, +9, +0xa, +0xb. flag 0 adds the 0x5b_1900
      call; a non-zero 0x57_4ec0 adds the five-call block before the buffer.
  FUN_005735a0 ECX unit-like, EDX monster, stack (a, b, third), `ret 0xc` (the third is not read): reads the
      class word at MonStats row +0x32 (row = [[0x744304]+0xa78] +
      class*0x1a8, class = monster +4); a null monster or a class outside
      [0, [table+0xa80]) reads address 0x32 and faults. Calls 0x64_8690,
      0x62_3f50 and 0x5a_7e60 (which fills a 0x20-byte block), overwrites block
      +0xc with a and +0x10 with b, then calls 0x5a_7c20 with that block.
  FUN_00597d70 ESI monster, EBX client record, stack (target), `ret 4`: returns
      when 0x62_0250(ESI) is 0. Otherwise calls 0x64_3ce0 and 0x64_42a0 and
      picks the id (ESI null: -1, else ESI +0xc); a non-null EBX that
      0x53_87f0(EBX, target) accepts gets 0x53_d530 (7 arguments, DL 1);
      otherwise 0x64_8a10, 0x64_8a00 and 0x53_d4d0 (7 arguments, DL 1), which
      reads ESI +0x2c (null ESI faults there). The high 24 bits of one
      argument are stack garbage in game.exe and are masked here.

`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail. No MPQs are read.
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import Fault, require

BIND, ANIM, ANNOUNCE = 0x573270, 0x5735A0, 0x597D70
FILE_AI, FILE_MON, FILE_MSG = 0x6E1560, 0x6E1578, 0x6E1F10
SEED, UNIT_OF, ROW = 0x7777, 0x5151, 0x6262
BUF_FILLED = 0x3C        # the 12-byte block sits at the buffer's last 12 bytes (frame -0xc..-0x4); the model fills only the part before it
MUTATIONS = ("bind-flag-ored-once", "bind-flag-test-inverted", "bind-id-full-dword", "bind-skip-clear-byte",
             "bind-owner-type-any", "anim-b-first", "anim-shift-7", "anim-count-le", "announce-id-minus-two",
             "announce-broadcast-swapped", "announce-word-not-masked")
mutation = None
R = mw.REGISTERS


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def reg(current, name):
    """A register of the running machine."""
    return current.mu.reg_read(R[name])


def bind_hooks(world):
    """Hook the callees of FUN_00573270."""
    script, machine = world.script, world.m
    world.logger(0x55_43B0, 1, "pose", regs=("ecx", "dl"))
    world.logger(0x5B_1900, 2, "pet", regs=("ecx", "edx"))
    world.logger(0x57_4EC0, 2, "slot", ret=lambda c: script["slot"], regs=("ecx", "edx"))
    world.logger(0x53_E0E0, 1, "label", regs=("ecx", "edx"))
    world.logger(0x58_F030, 4, "place", regs=("ecx", "edx"))
    world.logger(0x57_50E0, 2, "link", regs=("ecx", "edx"))
    world.logger(0x62_0BB0, 1, "row", ret=ROW, regs=())
    world.logger(0x61_A270, 3, "notify", regs=())
    world.logger(0x55_5600, 0, "drop", regs=("ecx", "edx"))
    world.logger(0x66_3750, 1, "seed", ret=SEED, regs=("ecx", "edx"))
    world.logger(0x65_62F0, 3, "check", ret=lambda c: script["check"], regs=())
    world.logger(0x62_7260, 4, "stat", regs=())
    world.logger(0x57_2840, 2, "assign", regs=("ecx", "edx"))
    world.logger(0x57_26C0, 1, "refresh", regs=("ecx", "edx"))
    world.logger(0x5A_4850, 2, "settle", regs=("ecx", "edx"))

    def unit_of(current):
        """0x55_31c0: log (ECX, file name, line) and return a token."""
        world.log.append(("unit-of", reg(current, "ecx"), world.file_of(reg(current, "edx")), current.arg(0)))
        return UNIT_OF
    machine.hook(0x55_31C0, unit_of, 1)

    def fill(current):
        """0x66_37f0: log the arguments, fill the 0x48-byte buffer, return the scripted result."""
        world.log.append(("fill", reg(current, "ecx"), reg(current, "edx"), current.arg(0), current.arg(1), current.arg(2)))
        current.mu.mem_write(current.arg(3), script["buf"][:BUF_FILLED])
        return script["fill"]
    machine.hook(0x66_37F0, fill, 4)

    def block(current):
        """0x57_5e90: log the arguments and the 12-byte block the third argument points at."""
        world.log.append(("block", reg(current, "ecx"), reg(current, "edx"), current.arg(0), current.arg(1),
                          bytes(current.read(current.arg(2), 12))))
        return 0
    machine.hook(0x57_5E90, block, 3)


def check_bind(world, rng):
    """FUN_00573270 on random game, owner, monster, record and flag."""
    machine, script = world.m, world.script
    game = machine.alloc(0x100)
    world.put_words(game + 0x70, [rng.randrange(1, 1 << 30)])
    machine.mu.mem_write(game + 0x6D, bytes([rng.randrange(256)]))
    owner = machine.alloc(0x100)
    world.write(owner, rng.randbytes(0x100))
    world.put_words(owner, [rng.choice((0, 0, 0, 1, 2))])
    data = machine.alloc(0x40)
    world.write(data, rng.randbytes(0x40))
    monster = machine.alloc(0xE0)
    world.write(monster, rng.randbytes(0xE0))
    world.put_words(monster, [rng.choice((1, 1, 1, 0, 2))])
    world.put_words(monster + 0x14, [data if rng.random() < 0.8 else 0])
    record = machine.alloc(8)
    word = rng.randrange(1 << 16)
    world.put_words(record, [word, rng.randrange(1 << 32)])
    slot = machine.alloc(0x20) if rng.random() < 0.5 else 0
    if slot:
        world.put_words(slot + 0xC, [rng.randrange(1 << 32)])
    ident = rng.choice((0xFFFF, 0xFFFF, 0x1FFFF, rng.randrange(1 << 16)))
    flag = rng.choice((0, 0, 1, 7))
    null = rng.choice((None, None, None, None, "owner", "monster", "record"))
    buf = rng.randbytes(BUF_FILLED)
    script.update(slot=slot, fill=rng.choice((0, 1, 1, 1, 9)), check=rng.choice((0, 1, 1, 5)), buf=buf)
    before, before_data = machine.read(monster, 0xE0), machine.read(data, 0x40)
    args = (0 if null == "monster" else monster, ident, 0 if null == "record" else record, flag)
    world.run(BIND, *args, ecx=game, edx=0 if null == "owner" else owner, nstack=4)
    want, image, dimage = [], bytearray(before), bytearray(before_data)
    owner_type, owner_c, owner_d0 = struct.unpack("<III", machine.read(owner, 4) + machine.read(owner + 0xC, 4) + machine.read(owner + 0xD0, 4))
    g70, g6d = machine.r32(game + 0x70), machine.read(game + 0x6D, 1)[0]
    if not null and (owner_type == 0 or mut("bind-owner-type-any")):
        struct.pack_into("<I", image, 0xC4, struct.unpack_from("<I", image, 0xC4)[0] | 0x4020200)
        want.append(("pose", monster, 2, 0))
        if (flag == 0) != mut("bind-flag-test-inverted"):
            want.append(("pet", game, monster, 0, owner_d0))
        want.append(("slot", game, owner, 7, 1))
        if slot:
            slot_c = machine.r32(slot + 0xC)
            want += [("unit-of", owner, world.file_of(FILE_AI), 0x52), ("label", UNIT_OF, 0xFFFF, 0),
                     ("place", game, slot, 0xFFFFFFFF, 1, 0, 0), ("link", game, owner, slot_c, 0),
                     ("row", slot), ("notify", ROW, 1, slot_c), ("drop", game, slot)]
        rec4 = machine.r32(record + 4)
        want += [("seed", g70, 0, word), ("fill", g70, owner, rec4, SEED, g6d)]
        if script["fill"]:
            buf0, buf1, buf6 = struct.unpack_from("<I", buf, 0)[0], struct.unpack_from("<I", buf, 4)[0], struct.unpack_from("<I", buf, 0x18)[0]
            want.append(("block", game, owner, monster, 7, struct.pack("<III", rec4, word, buf0)))
            if not mut("bind-flag-ored-once"):
                struct.pack_into("<I", image, 0xC4, struct.unpack_from("<I", image, 0xC4)[0] | 0x80000000)
            want.append(("place", game, monster, owner_c, owner_type, 0, 0))
            wide = ident == 0xFFFF if mut("bind-id-full-dword") else ident & 0xFFFF == 0xFFFF
            finished = True
            if wide:
                want.append(("check", g70, buf0 & 0xFFFF, buf1))
                if script["check"]:
                    want += [("stat", monster, 0xD, buf6, 0), ("assign", game, owner, monster, buf1), ("refresh", game, owner, 1)]
                else:
                    finished = False
            if finished:
                want.append(("settle", game, monster, 0x13, 0))
                data_ptr = struct.unpack_from("<I", image, 0x14)[0]
                if struct.unpack_from("<I", image, 0)[0] == 1 and data_ptr:
                    for offset in (4, 9, 0xA, 0xB) + (() if mut("bind-skip-clear-byte") else (5,)):
                        dimage[offset] = 0
    require(world.log == want, f"bind {null} id {ident:#x} flag {flag}: log\n{world.log}\n!=\n{want}")
    require(machine.read(monster, 0xE0) == bytes(image), "bind: the monster record differs")
    require(machine.read(data, 0x40) == bytes(dimage), "bind: the monster data differs")


def anim_hooks(world):
    """Hook the callees of FUN_005735a0."""
    machine = world.m
    world.logger(0x64_8690, 4, "row-touch", regs=())
    world.logger(0x62_3F50, 3, "state-touch", regs=())

    def fill(current):
        """0x5a_7e60: log (unit, mode) and fill the 0x20-byte block."""
        world.log.append(("fill", current.arg(0), current.arg(1)))
        current.mu.mem_write(current.arg(2), world.script["block"])
        return 0
    machine.hook(0x5A_7E60, fill, 3)

    def apply(current):
        """0x5a_7c20: log the first argument, the block bytes and the third argument."""
        world.log.append(("apply", current.arg(0), bytes(current.read(current.arg(1), 0x20)), current.arg(2)))
        return 0
    machine.hook(0x5A_7C20, apply, 3)


def check_anim(world, rng):
    """FUN_005735a0 on a random unit, class and arguments."""
    machine = world.m
    count = rng.randrange(1, 6)
    table = machine.alloc(0xB00)
    rows = machine.alloc((count + 1) * 0x1A8)
    machine.w32(mw.TABLE_PTR, table)
    machine.w32(table + 0xA78, rows)
    machine.w32(table + 0xA80, count)
    words = [rng.choice((0, 1, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(1 << 16))) for _ in range(count + 1)]
    for index, value in enumerate(words):
        machine.mu.mem_write(rows + index * 0x1A8 + 0x32, struct.pack("<H", value))
    cls = rng.choice((0, count - 1, count, count + 1, 0xFFFFFFFF, rng.randrange(count)))
    cls_signed = cls - (1 << 32) if cls & 0x80000000 else cls
    unit = 0 if rng.random() < 0.08 else machine.alloc(0x40)
    mode, ptr2c, obj, a, b = (rng.randrange(1 << 32) for _ in range(5))
    if unit:
        world.put_words(unit + 4, [cls])
        world.put_words(unit + 0x10, [mode])
        world.put_words(unit + 0x2C, [ptr2c])
    world.script["block"] = rng.randbytes(0x20)
    in_range = 0 <= cls_signed < count or (mut("anim-count-le") and cls_signed == count)
    try:
        world.run(ANIM, a, b, rng.randrange(1 << 32), ecx=obj, edx=unit, nstack=3)
        faulted = False
    except Fault:
        faulted = True
    require(faulted == (not unit or not in_range), f"anim unit {unit:#x} class {cls_signed} count {count}: faulted {faulted}")
    if faulted:
        return
    value = words[cls_signed]
    value = value - 0x10000 if value & 0x8000 else value
    shifted = (value << (7 if mut("anim-shift-7") else 8)) & 0xFFFFFFFF
    block = bytearray(world.script["block"])
    first, second = (b, a) if mut("anim-b-first") else (a, b)
    struct.pack_into("<I", block, 0xC, first)
    struct.pack_into("<I", block, 0x10, second)
    want = [("row-touch", ptr2c, shifted, FILE_MON, 0x17F), ("state-touch", unit, FILE_MON, 0x180),
            ("fill", unit, mode), ("apply", obj, bytes(block), 1)]
    require(world.log == want, f"anim: log\n{world.log}\n!=\n{want}")


def announce_hooks(world):
    """Hook the callees of FUN_00597d70."""
    machine, script = world.m, world.script
    world.logger(0x62_0250, 1, "mon-of", ret=lambda c: script["edi"], regs=())
    world.logger(0x64_3CE0, 3, "word", ret=lambda c: script["word"], regs=())
    world.logger(0x64_42A0, 3, "byte", ret=lambda c: script["byte"], regs=())
    world.logger(0x53_87F0, 0, "accept", ret=lambda c: script["accept"], regs=("ecx", "edx"))
    world.logger(0x64_8A10, 1, "high", ret=lambda c: script["high"], regs=())
    world.logger(0x64_8A00, 1, "low", ret=lambda c: script["low"], regs=())

    def send(name):
        """A 7-argument sender: log ECX, DL and the arguments (argument 5's high 24 bits are stack garbage)."""
        def hook(current):
            """Record the call."""
            args = [current.arg(i) for i in range(7)]
            args[5] &= 0xFF
            world.log.append((name, reg(current, "ecx"), reg(current, "edx") & 0xFF) + tuple(args))
            return 0
        return hook
    machine.hook(0x53_D530, send("direct"), 7)
    machine.hook(0x53_D4D0, send("broadcast"), 7)


def check_announce(world, rng):
    """FUN_00597d70 on random monster, client and scripted answers."""
    machine, script = world.m, world.script
    unit = 0 if rng.random() < 0.15 else machine.alloc(0x40)
    ptr = rng.randrange(1 << 32)
    unit_id = rng.randrange(1 << 32)
    if unit:
        world.put_words(unit + 0xC, [unit_id])
        world.put_words(unit + 0x2C, [ptr])
    client = 0 if rng.random() < 0.2 else machine.alloc(0x20)
    if client:
        world.put_words(client, [rng.randrange(1 << 32)])
        world.put_words(client + 0xC, [rng.randrange(1 << 32)])
    target = rng.randrange(1, 1 << 30)
    script.update(edi=rng.choice((0, 0x4321, 0x1111)), word=rng.randrange(1 << 32), byte=rng.randrange(1 << 32),
                  accept=rng.choice((0, 1, 7)), high=rng.randrange(1 << 16), low=rng.randrange(1 << 16))
    ident = 0xFFFFFFFF if not unit else unit_id
    if mut("announce-id-minus-two") and not unit:
        ident = 0xFFFFFFFE
    s = script
    direct = bool(client) and bool(s["accept"])
    want = [("mon-of", unit)]
    must_fault = False
    if s["edi"]:
        want += [("word", s["edi"], FILE_MSG, 0x96), ("byte", unit, s["edi"], 1)]
        if client:
            want.append(("accept", client, target))
        if direct:
            want.append(("direct", target, 1, ident, machine.r32(client), machine.r32(client + 0xC), s["word"], 0, s["byte"] & 0xFF, 0))
        else:
            must_fault = not unit
            if unit:
                low_word = s["word"] if mut("announce-word-not-masked") else s["word"] & 0xFFFF
                x, y = (s["low"], s["high"]) if mut("announce-broadcast-swapped") else (s["high"], s["low"])
                want += [("high", ptr), ("low", ptr), ("broadcast", target, 1, ident, y, x, low_word, 0, s["byte"] & 0xFF, 0)]
            else:
                want.append(("high",))
    try:
        world.run(ANNOUNCE, target, regs={"esi": unit, "ebx": client}, nstack=1)
        faulted = False
    except Fault:
        faulted = True
    require(faulted == must_fault, f"announce: faulted {faulted}, expected {must_fault}")
    if not faulted:
        require(world.log == want, f"announce: log\n{world.log}\n!=\n{want}")


def run_all():
    """Run every check on seeded random inputs; returns the case count."""
    rng = random.Random(0x573270)
    bind = mw.World()
    bind_hooks(bind)
    anim = mw.World()
    anim_hooks(anim)
    announce = mw.World()
    announce_hooks(announce)
    cases = 0
    for world, check, repeat in ((bind, check_bind, 800), (anim, check_anim, 300), (announce, check_announce, 400)):
        for _ in range(repeat):
            check(world, rng)
            cases += 1
    return cases


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "monster bind, animation and announce cases")


if __name__ == "__main__":
    main()
