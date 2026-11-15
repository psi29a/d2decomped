# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's chat line parser (FUN_0047c420) and the /players setter (FUN_00535780).

    uv run python chat_parse.py [--exe path/to/Game.exe] [--inc ../../tests/chat_players_cases.inc]

The Enter handler (FUN_0047ca70) hands FUN_0047c420 the typed line in the
ANSI code page (stack 1), the count of leading blanks to skip (stack 2), and
the UTF-16 box text (ECX, 0x7bb638). The parser:

* "/fps" and "/framerate" (whole line, any case: _stricmp) call FUN_00477c60 /
  FUN_00477c00, then shut the box (FUN_00455f20(5, 1, 0)) and stop.
* "/players" (first 8 characters, any case: _strnicmp) in game types
  (FUN_0044db30) 0, 1, 6 or 8: n = atol(line + 8), 1 when n <= 1, 8 when n >= 8;
  FUN_00535780(n) keeps n in 0x883d70 when n < 9 (unsigned); the line becomes
  "players set to %d" and goes on down the parser. In any other game type the
  line goes on unchanged.
* "/nopickup" (whole line) flips 0x7a6a90, shuts the box and stops.
* "soundchaosdebug" (no slash) flips 0x7c5468 (FUN_004babc0, which calls
  FUN_004baa50 when the flag goes back to 0), shuts the box and stops.
* Anything else is chat: FUN_0047bad0 reads a leading "/" "!" or "@"; a line
  whose wide text isn't empty goes to FUN_004787b0 as C->S 0x15, type 1, the
  language byte 0x7bb5dc, the text ("!text": 0x14, type 0, the text after "!").
  In a game type other than 0 a line starting with "/" is a Battle.net command
  instead: FUN_0047c1f0 for "/msg" "/m" "/whisper" "/w", FUN_0047c100 for
  "/help" "/?", then FUN_0044b7b0's object; nothing goes out as chat.

Each case starts from fresh flags. The hooks: the UI toggle (FUN_00455f20), the
two frame-rate toggles, FUN_004baa50, the game type, the packet builder
FUN_004787b0, the ignore list getter (FUN_00611560: the run stops there), the
/w and /help helpers (FUN_0047c1f0, FUN_0047c100) and FUN_0044b7b0. The CRT,
FUN_0047bad0, FUN_0047b3b0 and FUN_00535780 run for real. No MPQs are read.

--inc writes game.exe's answers for tests/test_chat_players.cpp: each line in
game type 0 with the count it stores (-1 for none) and the text it sends, and
FUN_00535790 in a single player game (+0x6a 3) for settings 0..9 and 0..9
players.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
PARSE = 0x47C420
UI_TOGGLE = 0x455F20
FPS = 0x477C60
FRAMERATE = 0x477C00
GAME_TYPE = 0x44DB30
SOUND_CHAOS_RESET = 0x4BAA50
SEND_CHAT = 0x4787B0
IGNORE_LIST = 0x611560
WHISPER_COMMAND = 0x47C1F0
HELP_COMMAND = 0x47C100
BNET_STATE = 0x44B7B0
CHAT_LINE_ADD = 0x49E3A0
SET_PLAYERS = 0x535780
PLAYER_COUNT = 0x535790
WALK_PLAYERS = 0x5538D0
COUNT_CALLBACK = 0x535760
PLAYERS = 0x883D70
NO_PICKUP = 0x7A6A90
SOUND_CHAOS = 0x7C5468
IME_OPEN = 0x7BB5E1
LANGUAGE = 0x7BB5DC
WIDE_TEXT = 0x7BB638
SENTINEL = 0xDEAD
LIMIT = 2_000_000


def require(condition, message):
    """Stop when game.exe's code disagrees with the claim."""
    if not condition:
        raise RuntimeError(message)


def atol_model(text):
    """MSVC's atol on what follows "/players": leading blanks, a sign, digits, held to 32 bits (strtol)."""
    index = 0
    while index < len(text) and text[index] in " \t\n\v\f\r":
        index += 1
    sign = 1
    if index < len(text) and text[index] in "+-":
        sign = -1 if text[index] == "-" else 1
        index += 1
    value = 0
    while index < len(text) and text[index].isdigit():
        value = value * 10 + int(text[index])
        index += 1
    return min(max(sign * value, -(1 << 31)), (1 << 31) - 1)


def model(line, game_type):
    """What the claim says the parser does with a line: (effects, players, nopickup, chaos)."""
    effects = []
    players = SENTINEL
    no_pickup = sound_chaos = 0
    lowered = line.lower()
    if lowered == "/fps":
        return ["fps", "ui 5 1 0"], players, no_pickup, sound_chaos
    if lowered == "/framerate":
        return ["framerate", "ui 5 1 0"], players, no_pickup, sound_chaos
    if lowered.startswith("/players") and game_type in (0, 1, 6, 8):
        count = min(max(atol_model(line[8:]), 1), 8)
        players = count
        line = f"players set to {count}"
        lowered = line
    if lowered == "/nopickup":
        return ["ui 5 1 0"], players, 1, sound_chaos
    if lowered == "soundchaosdebug":
        return ["ui 5 1 0"], players, no_pickup, 1
    if line.startswith("/") and game_type != 0:
        # FUN_0047bad0's "/" row (type 3, not in game type 0): the Battle.net
        # commands, then the Battle.net object (FUN_0044b7b0, 0 here)
        effects += [f"whisper command {name}" for name in ("/msg", "/m", "/whisper", "/w")]
        effects += [f"help command {name}" for name in ("/help", "/?")]
        return effects + ["ui 5 1 0"], players, no_pickup, sound_chaos
    packet = 0x14 if line.startswith("!") and len(line) > 1 else 0x15
    text = line[1:] if packet == 0x14 else line
    effects += [f"send {packet:02x} {0 if packet == 0x14 else 1} 0 {text}", "ui 5 1 0"]
    return effects, players, no_pickup, sound_chaos


def main():
    """Run the parser over typed lines and game types and compare with the model."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", help="game.exe 1.14d (default: <D2_MPQ_DIR>/bin/game.exe)")
    parser.add_argument("--inc", help="write the C++ test's cases here")
    args = parser.parse_args()
    exe = Path(args.exe) if args.exe else Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    machine.crt_init()
    state = {"type": 0, "effects": []}

    def record(text):
        state["effects"].append(text)

    def ui_toggle(e):
        record(f"ui {e.mu.reg_read(UC_X86_REG_ECX)} {e.mu.reg_read(UC_X86_REG_EDX)} {e.arg(0)}")
        return 1

    def send_chat(e):
        packet = e.mu.reg_read(UC_X86_REG_ECX)
        head = e.read(packet, 3)
        record(f"send {head[0]:02x} {head[1]} {head[2]} {e.cstr(packet + 3)}")
        return 0

    def stop(name):
        def hooked(e):
            record(name)
            e.mu.emu_stop()
            return 0
        return hooked

    machine.hook(UI_TOGGLE, ui_toggle, 1)
    machine.hook(FPS, lambda e: record("fps"), 0)
    machine.hook(FRAMERATE, lambda e: record("framerate"), 0)
    machine.hook(GAME_TYPE, lambda e: state["type"], 0)
    machine.hook(SOUND_CHAOS_RESET, lambda e: record("chaos reset"), 0)
    machine.hook(SEND_CHAT, send_chat, 0)
    machine.hook(IGNORE_LIST, stop("ignore list"), 0)
    machine.hook(WHISPER_COMMAND, lambda e: record(f"whisper command {e.cstr(e.mu.reg_read(UC_X86_REG_EBX))}"), 1)
    machine.hook(HELP_COMMAND, lambda e: record(f"help command {e.cstr(e.mu.reg_read(UC_X86_REG_EBX))}"), 0)
    machine.hook(BNET_STATE, lambda e: 0, 0)
    machine.hook(CHAT_LINE_ADD, stop("chat line"), 0)
    line_buffer = machine.alloc(0x110)

    def run(line, game_type, blanks=0):
        state["type"] = game_type
        state["effects"] = []
        machine.w32(PLAYERS, SENTINEL)
        machine.w32(NO_PICKUP, 0)
        machine.w32(SOUND_CHAOS, 0)
        machine.mu.mem_write(IME_OPEN, b"\0")
        machine.mu.mem_write(LANGUAGE, b"\0")
        typed = " " * blanks + line
        machine.mu.mem_write(line_buffer, typed.encode("latin-1")[:0xFF].ljust(0x110, b"\0"))
        machine.mu.mem_write(WIDE_TEXT, typed.encode("utf-16-le")[:0x200].ljust(0x202, b"\0"))
        machine.call(PARSE, line_buffer, blanks, ecx=WIDE_TEXT, limit=LIMIT)
        return (state["effects"], machine.s32(PLAYERS) if machine.r32(PLAYERS) != SENTINEL else SENTINEL,
                machine.r32(NO_PICKUP), machine.r32(SOUND_CHAOS))

    lines = ["/fps", "/FPS", "/fps ", "/framerate", "/FrameRate", "/nopickup", "/NOPICKUP", "soundchaosdebug",
             "/soundchaosdebug", "hello", "players set to 3", "!shout", "/players", "/players ", "/players 0",
             "/players 1", "/players 2", "/players 3", "/players 7", "/players 8", "/players 9", "/players 99",
             "/players -5", "/players abc", "/players 4abc", "/players8", "/PLAYERS 5", "/Players   6",
             "/players 2147483647", "/players -2147483648", "/players 4294967297", "/player 5", "/playersX",
             "/players +3", "/players\t4"]
    count = 0
    for game_type in range(10):
        for line in lines:
            for blanks in (0, 2):
                got = run(line, game_type, blanks)
                want = model(line, game_type)
                require(got == want, f"type {game_type} {line!r} blanks {blanks}: game.exe {got}, claim {want}")
                count += 1
    print(f"chat_parse: {count} lines match (10 game types, {len(lines)} lines, 0 and 2 leading blanks)")

    # The toggles from on: /nopickup back to 0; soundchaosdebug back to 0 calls FUN_004baa50.
    for line, flag, effects in (("/nopickup", NO_PICKUP, ["ui 5 1 0"]),
                                ("soundchaosdebug", SOUND_CHAOS, ["chaos reset", "ui 5 1 0"])):
        state["effects"] = []
        machine.w32(flag, 1)
        machine.mu.mem_write(line_buffer, line.encode("latin-1").ljust(0x110, b"\0"))
        machine.mu.mem_write(WIDE_TEXT, line.encode("utf-16-le").ljust(0x202, b"\0"))
        machine.call(PARSE, line_buffer, 0, ecx=WIDE_TEXT, limit=LIMIT)
        require(machine.r32(flag) == 0 and state["effects"] == effects, f"{line} from on: {state['effects']}")

    # FUN_00535780 (ECX n): keeps n only when n < 9, unsigned.
    for value in [*range(0, 21), 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF]:
        machine.w32(PLAYERS, SENTINEL)
        machine.call(SET_PLAYERS, ecx=value)
        require(machine.r32(PLAYERS) == (value if value < 9 else SENTINEL), f"FUN_00535780({value:#x})")
    # FUN_00535790 (ECX game): the players FUN_005538d0 counts (its callback
    # 0x535760 adds one a player; hooked to give the count), or the /players
    # value when game +0x6a is 1..3 and the value is larger (signed).
    game = machine.alloc(0x200)
    real = {"count": 0}

    def count_players(e):
        require(e.mu.reg_read(UC_X86_REG_ECX) == game and e.mu.reg_read(UC_X86_REG_EDX) == COUNT_CALLBACK,
                "FUN_00535790 walks the game's players with 0x535760")
        e.w32(e.arg(0), real["count"])
        return 0

    machine.hook(WALK_PLAYERS, count_players, 1)
    reads = 0
    for game_kind in range(256):
        for setting in (0, 1, 2, 5, 8, 0xFFFFFFFF):
            for players in range(0, 10):
                machine.mu.mem_write(game + 0x6A, bytes([game_kind]))
                machine.w32(PLAYERS, setting)
                real["count"] = players
                signed = setting - (1 << 32) if setting & 0x80000000 else setting
                want = signed if 1 <= game_kind <= 3 and signed > players else players
                got = machine.call(PLAYER_COUNT, ecx=game)
                require(got == want & 0xFFFFFFFF, f"FUN_00535790 type {game_kind} setting {signed} players {players}: {got}")
                reads += 1
    print(f"chat_parse: FUN_00535780 for 24 values, FUN_00535790 for {reads} (game +0x6a 0..255, setting, players)")
    if args.inc:
        write_cases(Path(args.inc), lines, run, machine, game, real)
    fresh = emu.Emu(str(exe))
    print(f"  0x883d70 in the image: {fresh.r32(PLAYERS)}")
    for line in ("/players 8", "/players 0", "/players 99", "/nopickup", "hello"):
        print(f"  type 0 {line!r}: {run(line, 0)}")
    print(f"  type 9 '/players 8': {run('/players 8', 9)}")


def c_string(text):
    """A C string literal: backslashes, quotes and control characters escaped (the literal split after one)."""
    backslash = chr(92)
    return '"' + "".join(f'{backslash}{ord(letter):03o}" "' if letter < " " else backslash + letter if letter in '"' + backslash else letter
                         for letter in text) + '"'


def write_cases(path, lines, run, machine, game, real):
    """game.exe's answers, as the arrays tests/test_chat_players.cpp includes."""
    out = ["// Stock game.exe 1.14d, FUN_0047c420 and FUN_00535790; generated by tools/emu/chat_parse.py --inc.",
           "// The lines in game type 0: the line, the count stored (-1: none), the text sent.",
           "constexpr LineCase kLines[]{"]
    for line in lines:
        effects, players, _, _ = run(line, 0)
        sent = next((effect.split(" ", 4)[4] for effect in effects if effect.startswith("send ")), "")
        out.append(f"    {{ {c_string(line)}, {-1 if players == SENTINEL else players}, {c_string(sent)} }},")
    out += ["};", "// FUN_00535790 in a single player game (+0x6a 3): the setting, the players counted, the count.",
            "constexpr CountCase kCounts[]{"]
    machine.mu.mem_write(game + 0x6A, bytes([3]))
    for setting in range(10):
        for players in range(10):
            machine.w32(PLAYERS, setting)
            real["count"] = players
            out.append(f"    {{ {setting}, {players}, {machine.call(PLAYER_COUNT, ecx=game)} }},")
    out.append("};")
    path.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    print(f"chat_parse: wrote {len(lines)} lines and 100 counts to {path}")


if __name__ == "__main__":
    main()
