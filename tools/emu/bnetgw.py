# SPDX-License-Identifier: GPL-3.0-or-later
"""Check selected BNetGW.cpp parser and selection helpers in stock 1.14d.

Run with D2_MPQ_DIR set: uv run python bnetgw.py
The PE is hash checked. Parser, accessors, save, update and load run on
synthetic buffers. Allocator, config IO, decimal formatting, logging and
Realms.bin load are scripted; no live file IO or Battle.net is exercised.
"""
import argparse
import hashlib
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
LIMIT = 100000


def call(machine, address, *args, ecx=0):
    """Call a stock function and reject instruction-limit stops."""
    result = machine.call(address, *args, ecx=ecx, limit=LIMIT)
    if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
        raise RuntimeError(f"{address:#x}: instruction limit at {machine.mu.reg_read(UC_X86_REG_EIP):#x}")
    expected_sp = emu.STACK + emu.STACK_SIZE - 0x1000
    if machine.mu.reg_read(UC_X86_REG_ESP) != expected_sp:
        raise RuntimeError(f"{address:#x}: incorrect stack cleanup")
    return result


def require(condition, message):
    """Fail on an unexpected observation, including under python -O."""
    if not condition:
        raise RuntimeError(message)


def integration(exe):
    """Run save, update and reload with scripted allocation and configuration IO."""
    m = emu.Emu(str(exe))
    records, writes, frees = {}, [], []

    def alloc(current):
        """Allocate a synthetic buffer for the stock allocator boundary."""
        return current.alloc(current.arg(0))

    def free(current):
        """Record the released pointer without recycling the emulator heap."""
        frees.append(current.arg(0))
        return 1

    def get(current):
        """Implement the two-phase configuration read over an in-memory map."""
        section, key, typ, output, capacity, size_out = [current.arg(i) for i in range(6)]
        require(current.cstr(section) == "Configuration" and typ == 0x82, "configuration read arguments")
        value = records.get(current.cstr(key))
        if value is None:
            return 0
        if size_out:
            current.w32(size_out, len(value))
        if output:
            require(capacity >= len(value), "configuration read capacity")
            current.mu.mem_write(output, value)
        return 1

    def put(current):
        """Record the stock write and retain the bytes for the next read."""
        section, key, typ, data, size = [current.arg(i) for i in range(5)]
        require(current.cstr(section) == "Configuration" and typ == 0x82, "configuration write arguments")
        name = current.cstr(key)
        records[name] = current.read(data, size)
        writes.append((name, records[name]))
        return 1

    def sprintf(current):
        """Model the two decimal sprintf calls used to rebuild the list."""
        require(current.cstr(current.arg(1)) == "%d", "unexpected sprintf format")
        value = current.arg(2)
        if value & 0x80000000:
            value -= 0x100000000
        text = str(value).encode()
        current.mu.mem_write(current.arg(0), text + b"\0")
        return len(text)

    m.hook(0x6823fd, sprintf, 0)
    m.hook(0x413020, alloc, 4)
    m.hook(0x412650, free, 4)
    m.hook(0x414de0, get, 6)
    m.hook(0x415000, put, 5)
    m.hook(0x5182b0, lambda current: 0, 0)  # Realms.bin load
    m.hook(0x518c70, lambda current: 0, 0)
    m.hook(0x518d40, lambda current: 0, 0)

    # GetGatewayList (0x518190) read directly, not just through Load (0x5186d0):
    # the registry value's size lands at +0x14, its raw bytes are copied to +0x10,
    # and the leading digits of the value are parsed into +0x18 via strtoul.
    gw_obj = m.alloc(36)
    records["Gateway Key"] = b"1002\0east.example\0-5\0East\0\0"
    call(m, 0x518190, m.put(b"Gateway Key\0"), ecx=gw_obj)
    require(m.r32(gw_obj + 0x18) == 1002, "GetGatewayList parses the version prefix")
    require(m.r32(gw_obj + 0x14) == len(records["Gateway Key"]), "GetGatewayList records the value size")
    require(m.cstr(m.r32(gw_obj + 0x10)) == "1002", "GetGatewayList copies the registry value")
    del records["Gateway Key"]

    # GetBattlenetGatewayList (0x518270): tries "Override Battle.net gateways" first
    # and only falls back to "Diablo II Battle.net gateways" when that key is absent.
    override_obj = m.alloc(36)
    records["Override Battle.net gateways"] = b"1003\0override.example\0-6\0Override\0\0"
    call(m, 0x518270, ecx=override_obj)
    require(m.r32(override_obj + 0x1c) == 1, "GetBattlenetGatewayList prefers an override list")
    require(m.cstr(m.r32(override_obj + 0x10)) == "1003", "GetBattlenetGatewayList loads the override buffer")
    del records["Override Battle.net gateways"]

    fallback_obj = m.alloc(36)
    records["Diablo II Battle.net gateways"] = b"1004\0default.example\0-7\0Default\0\0"
    call(m, 0x518270, ecx=fallback_obj)
    require(m.r32(fallback_obj + 0x1c) == 0, "GetBattlenetGatewayList falls back with no override")
    require(m.cstr(m.r32(fallback_obj + 0x10)) == "1004", "GetBattlenetGatewayList loads the default buffer")
    del records["Diablo II Battle.net gateways"]

    obj = m.alloc(36)
    packed = b"1000\0" + b"01\0" + b"old.example\0-5\0Old\0\0"
    original = m.put(packed)
    m.w32(obj + 8, 1)
    m.w32(obj + 12, 1)
    m.w32(obj + 0x10, original)
    m.w32(obj + 0x14, len(packed))
    m.w32(obj + 0x18, 1000)
    ini = (b"[Server List Version]\nVER=1001\n[Server Gateways]\n"
           b"1=east.example\n2=west.example\n"
           b"[east.example]\nZONE=-5\nENU=East\n"
           b"[west.example]\nZONE=-8\nENU=West\n\0")
    call(m, 0x518850, obj, m.put(ini))
    require(m.r32(obj + 0x18) == 1001, "updated version")
    require(m.r32(obj + 8) == 2 and m.r32(obj + 12) == 1, "reloaded count and closest gateway")
    require(m.cstr(call(m, 0x518040, obj, 1)) == "east.example", "first gateway")
    require(m.cstr(call(m, 0x5180a0, obj, 2)) == "West", "second gateway name")
    require(original in frees, "original list released")
    require(len(writes) == 1 and writes[0][0] == "Diablo II Battle.net gateways", "one default-list write")
    require(writes[0][1] == (b"1001\0" + b"01\0" + b"east.example\0-5\0East\0" +
                              b"west.example\0-8\0West\0\0"), "serialized gateway list")
    count = len(writes)
    call(m, 0x518850, obj, m.put(ini))
    require(len(writes) == count, "same version must not rewrite")
    records["Override Battle.net gateways"] = packed
    load_obj = m.alloc(36)
    call(m, 0x5186d0, load_obj)
    require(m.r32(load_obj + 0x1c) == 1 and m.r32(load_obj + 8) == 1, "load prefers override")
    require(m.cstr(call(m, 0x518040, load_obj, 1)) == "old.example", "override content")
    pointer = m.r32(load_obj + 0x10)
    call(m, 0x5186d0, load_obj)
    require(m.r32(load_obj + 0x10) == pointer, "already loaded is a no-op")
    del records["Override Battle.net gateways"]
    load_obj = m.alloc(36)
    call(m, 0x5186d0, load_obj)
    require(m.r32(load_obj + 0x1c) == 0 and m.r32(load_obj + 8) == 2, "default list fallback")
    save_obj = m.alloc(36)
    save_data = m.put(packed)
    realms = m.put(b"realm\0")
    m.w32(save_obj + 4, 1)
    m.w32(save_obj + 12, 9)
    m.w32(save_obj + 0x10, save_data)
    m.w32(save_obj + 0x14, len(packed))
    m.w32(save_obj + 0x1c, 1)
    m.w32(save_obj + 0x20, realms)
    call(m, 0x517ed0, save_obj)
    require(writes[-1] == ("Override Battle.net gateways", packed.replace(b"01\0", b"09\0", 1)),
            "override save contents")
    require(m.r32(save_obj + 0x10) == m.r32(save_obj + 0x14) == m.r32(save_obj + 0x20) == 0,
            "save clears pointers and length")
    require(save_data in frees and realms in frees, "save releases both buffers")
    m.w32(save_obj + 4, 0)
    save_data = m.put(packed)
    m.w32(save_obj + 0x10, save_data)
    m.w32(save_obj + 0x14, len(packed))
    before = len(writes)
    call(m, 0x517ed0, save_obj)
    require(len(writes) == before and save_data in frees, "clean unload frees without writing")
    return 23


def main():
    """Run parser boundary and permissive-matching observations."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise RuntimeError("requires stock game.exe 1.14d")
    m = emu.Emu(str(exe))
    obj = m.alloc(0x24)
    checks = 0
    for text, end, expected in [(b"abc\rxyz", 7, 3), (b"abc\nxyz", 7, 3),
                                (b"abc\0xyz", 7, 3), (b"abcxyz", 2, 2),
                                (b"\n\r\rxy", 5, 3), (b"\n\rxy", 1, 1),
                                (b"\0xy", 3, 0)]:
        p = m.put(text + b"\0")
        fn = 0x5183c0 if text[0] in (0, 10, 13) else 0x518390
        got = call(m, fn, p, p + end, ecx=obj) - p
        require(got == expected, f"line scan: {text!r}, {end}: {got} != {expected}")
        checks += 1
    for text, name in [(b"[alpha]\nKEY=one\n\0", b"alpha"),
                       (b"junk[ALPHAbeta]\r\nKEY=two\n\0", b"alpha"),
                       (b"[alpha!junk\nKEY=three\n\0", b"alpha")]:
        p, n = m.put(text), m.put(name + b"\0")
        got = call(m, 0x5183f0, p, n, ecx=obj)
        require(m.cstr(got).startswith("KEY="), f"section: {text!r}, {got:#x}")
        checks += 1
    for text, key, expected in [(b"KEY=value\n\0", b"key", b"value"),
                                (b"KEYXvalue\n\0", b"key", b"value"),
                                (b"KEY =value\n\0", b"key", b"=value"),
                                (b"no=1\nKEY=second\n\0", b"key", b"second"),
                                (b"[next]\nKEY=later\n\0", b"key", b"later"),
                                (b"KEY=first\nKEY=second\n\0", b"key", b"first")]:
        p, n = m.put(text), m.put(key + b"\0")
        got = call(m, 0x5184c0, p, n, ecx=obj)
        require(m.cstr(got).split("\n")[0].encode() == expected, f"key: {text!r}, {got:#x}")
        checks += 1
    for text, key in [(b"missing=1\n\0", b"key"), (b"]\nKEY=later\n\0", b"key")]:
        p, n = m.put(text), m.put(key + b"\0")
        require(call(m, 0x5184c0, p, n, ecx=obj) == 0, f"unexpected key in {text!r}")
        checks += 1
    source, target = m.alloc(36), m.alloc(36)
    words = list(range(0x100, 0x109))
    for i, word in enumerate(words):
        m.w32(source + 4 * i, word)
    require(call(m, 0x517e40, source, ecx=target) == target, "assignment return")
    require([m.r32(target + 4 * i) for i in range(9)] == words, "assignment fields")
    checks += 1
    tz_state = {"bias": 0, "lang": 0}

    def timezone(current):
        """Stub the Windows API's timezone bias output."""
        current.w32(current.arg(0), tz_state["bias"])
        return 1, 1

    for address, (name, _) in list(m.slots.items()):
        if name == "GetTimeZoneInformation":
            m.slots[address] = (name, timezone)
        elif name == "GetUserDefaultLangID":
            m.slots[address] = (name, lambda current: (tz_state["lang"], 0))
    for lang, bias, expected in [(0x0409, 300, 300), (0x0c09, 300, 480),
                                 (0x0c09, -60, 480), (0x0809, -60, -60)]:
        tz_state.update(lang=lang, bias=bias)
        result = call(m, 0x517e60, ecx=obj)
        require(result == (expected & 0xffffffff), f"timezone {lang:#x}/{bias}: {result:#x}")
        checks += 1
    packed = b"1000\0" + b"01\0" + b"east.example\0-5\0East\0" + b"west.example\0-8\0West\0\0"
    buf = m.put(packed)
    m.w32(obj + 8, 2)
    m.w32(obj + 0x10, buf)
    m.w32(obj + 0x14, len(packed))
    for index, dns, zone, name in [(1, "east.example", -5, "East"), (2, "west.example", -8, "West")]:
        for address, expected in [(0x518040, dns), (0x5180a0, name)]:
            ptr = call(m, address, obj, index)
            require(m.cstr(ptr) == expected, f"{address:#x} index {index}: {m.cstr(ptr)!r}")
            checks += 1
        value = call(m, 0x518060, obj, index)
        require(value == zone & 0xffffffff, f"GMT {index}: {value:#x}")
        checks += 1
    realms = m.put(b"EAST.EXAMPLE\0EastRealm\0west.example\0WestRealm\0\0")
    m.w32(obj + 0x20, realms)
    for index, expected in [(1, "EastRealm"), (2, "WestRealm")]:
        ptr = call(m, 0x5180e0, obj, index)
        require(m.cstr(ptr) == expected, f"realm {index}: {m.cstr(ptr)!r}")
        checks += 1
    m.w32(obj + 0x20, m.put(b"no-match\0Other\0\0"))
    require(m.cstr(call(m, 0x5180e0, obj, 1)) == "", "realm missing")
    checks += 1
    for index in (0, 3, 0xffffffff):
        ptr = call(m, 0x517fd0, index, ecx=obj)
        require(ptr == obj, f"Nth invalid {index}: {ptr:#x}")
        checks += 1
    m.w32(obj + 0x14, 3)
    require(call(m, 0x517fd0, 1, ecx=obj) == obj, "Nth short buffer")
    m.w32(obj + 0x14, len(packed))
    m.w32(obj + 0x10, 0)
    require(call(m, 0x517fd0, 1, ecx=obj) == obj, "Nth no buffer")
    m.w32(obj + 0x10, buf)
    checks += 2
    for index, count, expected, dirty in [(1, 2, 1, 1), (2, 2, 2, 1), (3, 2, 42, 0),
                                          (0, 2, 42, 0), (100, 100, 99, 1)]:
        m.w32(obj + 4, 0)
        m.w32(obj + 8, count)
        m.w32(obj + 12, 42)
        call(m, 0x518150, obj, index)
        require((m.r32(obj + 12), m.r32(obj + 4)) == (expected, dirty), f"set {index}/{count}")
        checks += 1
    m.hook(0x518c70, lambda current: 0, 0)
    m.hook(0x518d40, lambda current: 0, 0)
    m.w32(obj + 8, 2)
    for minutes, expected in [(-300, 1), (-480, 2), (-390, 1), (-420, 2), (0, 1)]:
        m.w32(obj + 4, 0)
        m.w32(obj + 12, 0)
        call(m, 0x518580, minutes & 0xffffffff, ecx=obj)
        require((m.r32(obj + 12), m.r32(obj + 4)) == (expected, 1), f"closest {minutes}")
        checks += 1
    checks += integration(exe)
    print(f"ok: {checks} BNetGW parser, access and lifecycle checks")


if __name__ == "__main__":
    main()
