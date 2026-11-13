# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d's UI open/close state functions: the speech box teardown
(FUN_0049f960, FUN_004a0680, FUN_004a0770 and its two getters FUN_0049f900 /
FUN_004a0000), the proximity-player flag reset (FUN_0049f8c0) and the three
UI\\ui.cpp panel lifecycle walkers (FUN_00453d90, FUN_00453de0, FUN_00453ee0).

Usage: uv run python ui_state.py
       (export D2_MPQ_DIR=<dir with bin/game.exe> first)

Each function runs for real. Every callee is hooked and logs (address, ECX,
EDX, EAX, stack arg 0, stack arg 1) in call order, so the log is the function's
call sequence with its register and stack arguments; the globals it writes are
read back. The expected sequences come from the disassembly (`disasm.py`), not
from the decompile: FUN_004a0770 and FUN_0049f960 push 7 before `call
FUN_004f59a0`, a getter of `[0x7c8cbc]` with no argument, and FUN_00451e10 pops
that 7 (`ret 4`), so the call is FUN_00451e10(ECX = [0x7c8cbc], EDX = 0x722600,
7), not FUN_004f59a0(7). No MPQs are read.
"""
import emu

LINE_OF = 0x6DA13C   # ".\UI\text.cpp" for the frees
TEXT_EDX = 0x722600
G = dict(
    text_list=0x7BF250, speech=0x7BF0A4, speech2=0x7BF1A8, box_obj=0x7BF1B0, box_vt=0x7BF1B4, flag=0x7BF24B,
    wait=0x7BF260, quest=0x7BF27C, voice=0x7BF230, voice_id=0x7BF212, done=0x7BF1BC, open=0x7BF20A,
    cb=0x7BF258, cb_ctx=0x7BF25C, once=0x7BF1C4, npc=0x7BF202, getter=0x7C8CBC,
    w1=0x7A2794, w2=0x7A2894, w3=0x7A2898,
    u_a=0x7A27B4, u_b=0x7A27A8, u_c=0x7A27A0, u_d=0x7A27A4, u_e=0x7A27AC, u_f=0x7A27B0, u_g=0x7A279C, u_out=0x7A2A68,
)
# callee: stdcall arguments popped (a hooked game function returns, popping this many dwords)
CALLEES = {
    0x463990: 0, 0x6611A0: 2, 0x467430: 0, 0x49D370: 0, 0x40B3C0: 2, 0x4B9610: 0, 0x4B9EF0: 2, 0x451E10: 1,
    0x4786A0: 1, 0x6612A0: 1, 0x4B8020: 0, 0x49FE10: 0, 0x49D4D0: 1, 0x49D410: 1, 0x514CC0: 0, 0x4B95C0: 0,
    0x49D5A0: 0, 0x453AE0: 0, 0x4B3D10: 0, 0x49C0A0: 0, 0x4A7AA0: 0, 0x4AA920: 0, 0x4A8A30: 0, 0x4B8720: 0,
    0x487990: 0, 0x498E80: 0, 0x48A600: 0, 0x4A3050: 0, 0x4939B0: 0, 0x457640: 0, 0x47E720: 0, 0x49C0F0: 0,
    0x4A7AD0: 0, 0x4AA980: 0, 0x4A8C00: 0, 0x4A3020: 0, 0x4B32F0: 0, 0x4B91F0: 0, 0x4A0010: 0, 0x492F90: 0,
    0x478A00: 0, 0x47E750: 0, 0x468070: 0, 0x46AA20: 0, 0x489880: 0, 0x4A7A80: 0,
    # spare game addresses standing in for virtual methods and a callback
    0x4A1510: 1, 0x4A01A0: 0, 0x4A0050: 1,
}
VDEL, VGET, CALLBACK = 0x4A1510, 0x4A01A0, 0x4A0050


class Rig:
    """One emulated game.exe with every callee hooked to log its call."""

    def __init__(self):
        """Hook each callee: log (address, ECX, EDX, EAX, arg 0, arg 1) and return self.ret[address]."""
        self.e = emu.Emu()
        self.log = []
        self.ret = {}
        for addr, nstack in CALLEES.items():
            self.e.hook(addr, self._recorder(addr), nstack)

    def _recorder(self, addr):
        """A hook for addr that logs the registers and two stack arguments."""
        def hook(e):
            regs = [e.mu.reg_read(r) for r in (emu.UC_X86_REG_ECX, emu.UC_X86_REG_EDX, emu.UC_X86_REG_EAX)]
            self.log.append((addr, *regs, e.arg(0), e.arg(1)))
            return self.ret.get(addr, 0)
        return hook

    def reset(self):
        """Zero every global the functions touch, the call log and the return values."""
        for addr in G.values():
            self.e.w32(addr, 0)
        self.e.mu.mem_write(G["voice_id"], b"\0\0")
        self.log.clear()
        self.ret.clear()

    def put(self, name, value):
        """Write a dword global."""
        self.e.w32(G[name], value)

    def get(self, name):
        """Read a dword global."""
        return self.e.r32(G[name])

    def obj(self, first=0):
        """A 16-byte object whose first dword is `first`."""
        addr = self.e.alloc(16)
        self.e.w32(addr, first)
        return addr

    def vobj(self):
        """An object with a vtable: slot 0 is the deleting destructor stand-in, slot 0x2c the getter stand-in."""
        vtable, addr = self.e.alloc(0x40), self.e.alloc(16)
        self.e.w32(vtable, VDEL)
        self.e.w32(vtable + 0x2C, VGET)
        self.e.w32(addr, vtable)
        return addr


def entry(addr, ecx=None, edx=None, eax=None, arg0=None, arg1=None):
    """An expected log row; None means not checked."""
    return (addr, ecx, edx, eax, arg0, arg1)


def check(rig, expected, what):
    """Assert the call log equals `expected` row by row (None cells skipped)."""
    got = rig.log
    ok = len(got) == len(expected) and all(
        all(want is None or want == have for want, have in zip(row, log)) for row, log in zip(expected, got))
    assert ok, f"{what}: expected {[tuple(hex(v) if v is not None else None for v in r) for r in expected]}\n got {[tuple(hex(v) for v in r) for r in got]}"


def getters(rig):
    """FUN_0049f900 returns [0x7bf250]; FUN_004a0000 returns [0x7bf0a4] != 0 (the doc's 0, 0x12345678 runs plus more)."""
    for value in (0, 1, 0x12345678, 0xFFFFFFFF):
        rig.reset()
        rig.put("text_list", value)
        assert rig.e.call(0x49F900) == value
        rig.put("speech", value)
        assert rig.e.call(0x4A0000) & 0xFF == (value != 0), value


def proximity_flag(rig):
    """FUN_0049f8c0 (ECX = a 0x76 packet: +1 byte type, +2 dword id): the unit from FUN_00463990(ECX = id, EDX = type) has its +0xa4 released through FUN_006611a0(0, +0xa4) when set, then zeroed."""
    packet = rig.e.alloc(8)
    rig.e.mu.mem_write(packet, bytes((0x76, 0)) + (0x1234567).to_bytes(4, "little"))
    for kind in (0, 1):
        rig.e.mu.mem_write(packet + 1, bytes((kind,)))
        rig.reset()
        rig.e.call(0x49F8C0, ecx=packet)
        check(rig, [entry(0x463990, ecx=0x1234567, edx=kind)], f"no unit, type {kind}")
    for held in (0, 0x5555):
        rig.reset()
        unit = rig.obj()
        rig.e.w32(unit + 0xA4, held)
        rig.ret[0x463990] = unit
        rig.e.call(0x49F8C0, ecx=packet)
        check(rig, [entry(0x463990, ecx=0x1234567, edx=1)] + ([entry(0x6611A0, arg0=0, arg1=held)] if held else []), f"held {held}")
        assert rig.e.r32(unit + 0xA4) == 0


def free_speech(rig, ptr, mark_first):
    """The expected rows for the speech record's release: FUN_0049d370 (EAX = first dword), then FUN_0040b3c0(ptr, text.cpp, 0x68e, 0)."""
    return [entry(0x49D370, eax=mark_first), entry(0x40B3C0, ecx=ptr, edx=LINE_OF, arg0=0x68E, arg1=0)]


def speech_close(rig):
    """FUN_0049f960 on every branch: the speech record, the wait flag, the quest toggle and the voice."""
    run = lambda: rig.e.call(0x49F960)
    rig.reset()
    rig.put("flag", 5)
    run()
    check(rig, [entry(0x467430, ecx=0)], "idle")
    assert rig.get("flag") == 0
    rig.reset()
    speech = rig.obj(0xABC)
    rig.put("speech", speech)
    run()
    check(rig, [entry(0x467430, ecx=0)] + free_speech(rig, speech, 0xABC), "speech")
    assert rig.get("speech") == 0
    for answer in (0, 0x55):
        rig.reset()
        rig.put("wait", 9)
        rig.ret[0x4B9610] = answer
        run()
        check(rig, [entry(0x467430), entry(0x4B9610, ecx=9)] + ([entry(0x4B9EF0, ecx=0x55, edx=0, arg0=0, arg1=4)] if answer else []), f"wait {answer}")
        assert rig.get("wait") == 0
    for quest, called in ((1, True), (2, False), (0, False)):
        rig.reset()
        rig.put("quest", quest)
        rig.put("getter", 0x3333)
        run()
        check(rig, [entry(0x467430)] + ([entry(0x451E10, ecx=0x3333, edx=TEXT_EDX, arg0=7)] if called else []), f"quest {quest}")
        assert rig.get("quest") == (quest if not called else 0)
    rig.reset()
    rig.put("voice", 1)
    rig.e.mu.mem_write(G["voice_id"], b"\x34\x12")
    rig.e.mu.mem_write(G["voice_id"] + 2, b"\xad\xde")     # the next word: movzx reads only the low 16 bits
    run()
    assert len(rig.log) == 2 and rig.log[1][0] == 0x4786A0 and rig.log[1][2] == 0xFFFFFFFF and rig.log[1][1] & 0xFF == 0x31 and rig.log[1][4] == 0x1234, rig.log
    assert rig.get("voice") == 0


def text_clear(rig):
    """FUN_004a0680: free the gossip list, both speech records, the box object and the three FUN_0049d4d0 buffers, in that order."""
    rig.reset()
    text, one, two, box = 0x900D, rig.obj(0xA1), rig.obj(0xA2), rig.vobj()
    rig.put("text_list", text)
    rig.put("speech", one)
    rig.put("speech2", two)
    rig.put("box_obj", 1)
    rig.put("box_vt", box)
    rig.put("flag", 3)
    rig.e.call(0x4A0680)
    check(rig, [entry(0x6612A0, arg0=text)] + free_speech(rig, one, 0xA1) + free_speech(rig, two, 0xA2)[:2]
          + [entry(0x40B3C0, ecx=two, edx=LINE_OF, arg0=0x6E4, arg1=0), entry(0x4B8020, ecx=1), entry(VDEL, ecx=box, arg0=1),
             entry(0x49FE10), entry(0x467430, ecx=0), entry(0x49D4D0, arg0=0x7BF1E4), entry(0x49D4D0, arg0=0x7BF1EA), entry(0x49D4D0, arg0=0x7BF1F0)], "full")
    assert all(rig.get(n) == 0 for n in ("text_list", "speech", "speech2", "box_obj", "box_vt", "flag"))
    rig.reset()
    rig.e.call(0x4A0680)
    check(rig, [entry(0x49FE10), entry(0x467430), entry(0x49D4D0, arg0=0x7BF1E4), entry(0x49D4D0, arg0=0x7BF1EA), entry(0x49D4D0, arg0=0x7BF1F0)], "empty")


def speech_step(rig):
    """FUN_004a0770 (EAX = a record, one stack arg): the abort flag, the redraw gate, the callback and the close paths."""
    def run(arg, record):
        """Call with EAX = record and one stack argument."""
        return rig.e.call(0x4A0770, arg, regs={"eax": record})
    rig.reset()
    record = rig.obj()
    assert run(1, record) == 0 and rig.e.r32(record + 0x16) & 0xFFFFFFFF == 2 and rig.log == []
    rig.reset()
    rig.put("flag", 1)
    rig.ret[0x514CC0], rig.ret[0x4B95C0] = 7, 0
    assert run(0, 0) == 1 and rig.get("flag") == 1 and [row[0] for row in rig.log] == [0x514CC0, 0x4B95C0]
    rig.reset()
    rig.put("flag", 1)
    rig.ret[0x514CC0], rig.ret[0x4B95C0] = 0, 0
    run(0, 0)                                     # 0x514cc0 gave 0: falls through, clears the flag, asks FUN_0049d5a0
    assert rig.get("flag") == 0 and [row[0] for row in rig.log] == [0x514CC0, 0x4B95C0, 0x49D5A0]
    rig.reset()
    rig.ret[0x49D5A0] = 0
    assert run(0, 0) == 1 and [row[0] for row in rig.log] == [0x514CC0, 0x49D5A0] and rig.get("done") == 0
    # the box is done: a callback gets (ECX = 0x7bf25c, EDX = 0, 0) and its answer comes back
    rig.reset()
    rig.ret[0x49D5A0], rig.ret[CALLBACK] = 1, 0x42
    rig.put("cb", CALLBACK)
    rig.put("cb_ctx", 0xC0DE)
    rig.put("open", 1)
    assert run(0, 0) == 0x42 and rig.get("done") == 1
    check(rig, [entry(0x514CC0), entry(0x49D5A0), entry(0x467430, ecx=0), entry(CALLBACK, ecx=0xC0DE, edx=0, arg0=0)], "callback")
    # no callback, open flag clear, a speech record: freed through FUN_0049d410 first; the once-flag 1 is cleared instead of FUN_004b3d10
    rig.reset()
    speech = rig.obj(0xE1)
    rig.ret[0x49D5A0] = 1
    rig.put("speech", speech)
    rig.put("once", 1)
    rig.put("quest", 1)
    rig.put("getter", 0x2222)
    assert run(0, 0) == 0 and rig.get("speech") == 0 and rig.get("once") == 0 and rig.get("quest") == 0 and rig.get("open") == 0
    # FUN_0049f960 (real here) sees the quest toggle first and fires FUN_00451e10 itself, so the later check finds it 0
    check(rig, [entry(0x514CC0), entry(0x49D5A0), entry(0x49D410, arg0=speech), entry(0x467430, ecx=0), entry(0x467430, ecx=0),
                entry(0x451E10, ecx=0x2222, edx=TEXT_EDX, arg0=7), entry(0x453AE0), entry(0x467430, ecx=0)], "close")
    # box still open (flag set), no callback, once-flag not 1: the NPC menu closes with FUN_004b3d10(ECX = [0x7bf202])
    rig.reset()
    rig.ret[0x49D5A0] = 1
    rig.put("open", 1)
    rig.put("npc", 0x77)
    assert run(0, 0) == 0 and rig.get("open") == 0
    check(rig, [entry(0x514CC0), entry(0x49D5A0), entry(0x467430), entry(0x4B3D10, ecx=0x77), entry(0x453AE0), entry(0x467430)], "open close")


def lifecycle(rig):
    """FUN_00453d90 / FUN_00453de0 / FUN_00453ee0: the panel init, free and the per-panel state reset call order."""
    rig.reset()
    rig.e.call(0x453D90)
    check(rig, [entry(a) for a in (0x49C0A0, 0x4A7AA0, 0x4AA920, 0x4A8A30, 0x4B8720, 0x487990, 0x498E80, 0x48A600, 0x4A3050, 0x4939B0, 0x457640, 0x47E720)], "453d90")
    free_calls = [entry(a) for a in (0x49C0F0, 0x4A7AD0, 0x4AA980, 0x4A8C00, 0x4A3020, 0x4B32F0, 0x4B91F0, 0x4A0010, 0x492F90)]
    tail = [entry(0x47E750), entry(0x468070)]
    rig.reset()
    rig.e.call(0x453DE0)
    check(rig, free_calls + tail, "453de0 none")
    rig.reset()
    for name in ("w1", "w2", "w3"):
        rig.put(name, 1)
    rig.e.call(0x453DE0)
    check(rig, free_calls + [entry(0x478A00)] * 3 + tail, "453de0 all")
    assert all(rig.get(n) == 0 for n in ("w1", "w2", "w3"))
    rig.reset()
    rig.put("w2", 5)
    rig.e.call(0x453DE0)
    check(rig, free_calls + [entry(0x478A00)] + tail, "453de0 w2")
    # FUN_00453ee0
    rig.reset()
    rig.e.call(0x453EE0)
    check(rig, [entry(0x4A7A80)], "453ee0 empty (mode 0)")
    for mode, tailcall in ((0, 0x4A7A80), (1, 0x489880), (4, 0x489880), (5, None), (-1 & 0xFFFFFFFF, None)):
        rig.reset()
        rig.put("u_g", mode)
        rig.e.call(0x453EE0)
        check(rig, [entry(tailcall)] if tailcall else [], f"453ee0 mode {mode:#x}")
    rig.reset()
    parts = {name: rig.vobj() for name in ("u_b", "u_d", "u_e", "u_f")}
    for name, ptr in parts.items():
        rig.put(name, ptr)
    rig.put("u_a", 1)
    rig.put("u_c", 1)
    rig.put("u_g", 2)
    rig.ret[VGET] = 0x777
    rig.e.call(0x453EE0)
    check(rig, [entry(0x46AA20, ecx=1, edx=1), entry(VGET, ecx=parts["u_b"]), entry(0x4B8020, ecx=1),
                entry(VDEL, ecx=parts["u_d"], arg0=1), entry(VDEL, ecx=parts["u_b"], arg0=1),
                entry(VDEL, ecx=parts["u_e"], arg0=1), entry(VDEL, ecx=parts["u_f"], arg0=1), entry(0x489880)], "453ee0 full")
    assert all(rig.get(n) == 0 for n in ("u_a", "u_b", "u_c", "u_d", "u_e", "u_f")) and rig.get("u_out") == 0x777 and rig.get("u_g") == 2


def main():
    """Run every group and print one line."""
    rig = Rig()
    for group in (getters, proximity_flag, speech_close, text_clear, speech_step, lifecycle):
        group(rig)
    print("ok: FUN_0049f900, 004a0000, 0049f8c0, 0049f960, 004a0680, 004a0770, 00453d90, 00453de0, 00453ee0 call sequences and globals checked")


if __name__ == "__main__":
    main()
