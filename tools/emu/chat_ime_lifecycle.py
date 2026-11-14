# SPDX-License-Identifier: GPL-3.0-or-later
"""Check UI/chat.cpp IME lifecycle in local game.exe 1.14d.

Run: python chat_ime_lifecycle.py --exe /path/to/Game.exe
Exercises FUN_0047ae00, FUN_0047b090 and FUN_0047b2e0 with scripted IME,
window, allocator and event-table calls. No OS input method or MPQ is used.
The checks cover stock control flow, not d2d parity.
"""
import argparse
import hashlib
import itertools
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

SHA256 = '631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf'
MODE, ENABLED, REGISTERED, STATUS, LIST, COMPOSE, EXTRA = 0x7BB5DC, 0x7BB5E0, 0x7BB5E1, 0x7BB5F0, 0x7BB5F4, 0x7BB5F8, 0x7BC850
WINDOW, CONTEXT, FILENAME = 0x1337, 0x5150, 0x6D7288


def require(actual, expected, label):
    """Report the first difference between an observation and its model."""
    if actual != expected:
        raise AssertionError(f'{label}: {actual!r} != {expected!r}')


class Rig:
    """One stock image with deterministic, recorded external operations."""
    def __init__(self, exe):
        """Hook imports and helpers while keeping the target functions real."""
        self.e = emu.Emu(str(exe))
        self.log = []
        self.context = CONTEXT
        self.language = 0x411
        self.mode = 6
        self.gate = 1
        self.ime = 1
        self.open = 1
        self.set_open = 1
        self.conversion = 1
        self.sentence = 0x456
        for addr, count, name in ((0x420290, 1, 'pre'), (0x451DB0, 1, 'register'), (0x451E10, 1, 'unregister'), (0x40B3C0, 2, 'free'), (0x47AD60, 0, 'candidates')):
            self.e.hook(addr, self.hook(name), count)
        self.e.hook(0x525150, lambda current: self.mode, 0)
        self.e.hook(0x47B010, lambda current: self.gate, 0)
        self.e.w32(0x7C8CBC, WINDOW)
        for name, count, handler in (
            ('ImmGetContext', 1, self.get_context), ('ImmReleaseContext', 2, self.release),
            ('ImmGetOpenStatus', 1, self.open_status), ('ImmSetOpenStatus', 2, self.set_status),
            ('ImmGetConversionStatus', 3, self.get_conversion), ('ImmSetConversionStatus', 3, self.set_conversion),
            ('ImmIsIME', 1, self.is_ime), ('ImmSimulateHotKey', 2, self.hotkey),
            ('GetKeyboardLayout', 1, self.layout), ('GetSystemDefaultLangID', 0, self.lang)):
            for slot, (symbol, _) in list(self.e.slots.items()):
                if symbol == name:
                    self.e.slots[slot] = (symbol, lambda current, handler=handler, count=count: (handler(current), count))
                    break
            else:
                raise AssertionError(f'missing import {name}')

    def hook(self, name):
        """Capture the custom calling conventions of the hooked game helpers."""
        def record(current):
            """Record relevant registers and stack arguments."""
            ecx, edx = (current.mu.reg_read(reg) for reg in (UC_X86_REG_ECX, UC_X86_REG_EDX))
            if name in ('register', 'unregister'):
                self.log.append((name, ecx, edx, current.arg(0)))
            elif name == 'free':
                self.log.append((name, ecx, edx, current.arg(0), current.arg(1)))
            elif name == 'candidates':
                self.log.append((name, current.mu.reg_read(UC_X86_REG_EDI)))
            else:
                self.log.append((name, current.arg(0)))
            return 0
        return record

    def get_context(self, e):
        """Return a selected context for the requested window."""
        self.log.append(('get', e.arg(0)))
        return self.context

    def release(self, e):
        """Record context release."""
        self.log.append(('release', e.arg(0), e.arg(1)))
        return 1

    def open_status(self, e):
        """Return the scripted input-method open state."""
        self.log.append(('is_open', e.arg(0)))
        return self.open

    def set_status(self, e):
        """Record the requested input-method open state."""
        self.log.append(('set_open', e.arg(0), e.arg(1)))
        return self.set_open

    def get_conversion(self, e):
        """Return scripted conversion and sentence modes."""
        self.log.append(('get_conversion', e.arg(0)))
        e.w32(e.arg(1), self.conversion)
        e.w32(e.arg(2), self.sentence)
        return 1

    def set_conversion(self, e):
        """Record conversion and sentence mode changes."""
        self.log.append(('set_conversion', e.arg(0), e.arg(1), e.arg(2)))
        return 1

    def is_ime(self, e):
        """Return the scripted layout classification."""
        self.log.append(('is_ime', e.arg(0)))
        return self.ime

    def hotkey(self, e):
        """Record a simulated IME hotkey."""
        self.log.append(('hotkey', e.arg(0), e.arg(1)))
        return 1

    def layout(self, e):
        """Return a fixed keyboard layout."""
        self.log.append(('layout', e.arg(0)))
        return 0x4090409

    def lang(self, e):
        """Return the selected system language."""
        self.log.append(('language',))
        return self.language

    def reset(self, mode, registered=0, old=0):
        """Initialize shared state with sentinels that reveal partial writes."""
        self.log.clear()
        self.e.w32(MODE, mode)
        self.e.mu.mem_write(ENABLED, bytes((0xA5, registered)))
        self.e.w32(STATUS, 0xBEEF)
        self.e.w32(LIST, old)
        self.e.w32(COMPOSE, 0x12345678)
        self.e.w32(EXTRA, 0x87654321)

    def run(self, address, *args):
        """Require both return to the sentinel and correct stack cleanup."""
        self.e.call(address, *args, limit=100000)
        require(self.e.mu.reg_read(UC_X86_REG_EIP), emu.RET_MAGIC, 'instruction limit')
        require(self.e.mu.reg_read(UC_X86_REG_ESP), emu.STACK + emu.STACK_SIZE - 0x1000, 'stack cleanup')


def notify(rig):
    """Sweep notification codes, modes, context and scripted IME state."""
    count = 0
    for mode, code, have, bit, old in itertools.product((0, 6, 7, 8, 9, 12), range(1, 10), (0, 1), (0, 1), (0, 0x4444)):
        rig.reset(mode, old=old)
        rig.context = CONTEXT if have else 0
        rig.open = rig.ime = rig.conversion = bit
        message = rig.e.alloc(0x20)
        rig.e.w32(message + 8, code)
        rig.e.w32(message + 24, 99)
        rig.e.w32(message + 28, 99)
        rig.run(0x47AE00, message)
        require((rig.e.r32(message + 24), rig.e.r32(message + 28)), (1, 0), 'notification handled')
        expected = [('pre', message), ('get', WINDOW)]
        status, enabled, pointer = 0xBEEF, 0xA5, old
        if have:
            if code == 2 and mode == 9:
                expected += [('layout', 0), ('is_ime', 0x4090409)]
                status = 0 if bit else 4
            if code in (3, 5) and (mode == 9 or code == 5 and mode == 8):
                expected += [('candidates', CONTEXT)]
            if code == 4 and mode in (8, 9) and old:
                expected += [('free', old, FILENAME, 0x160 if mode == 9 else 0x16B, 0)]
                pointer = 0
            if code == 6:
                expected += [('get_conversion', CONTEXT)]
                if mode in (7, 12):
                    enabled, status = bit, bit
            if code == 8 and mode in (6, 8, 9):
                expected += [('is_open', CONTEXT)]
                enabled, status = (1, {6: 3, 8: 5, 9: 4}[mode]) if bit else (0, 0)
            expected += [('release', WINDOW, CONTEXT)]
        require(rig.log, expected, f'notification {mode} {code} {have} {bit} {old}')
        require((rig.e.read(ENABLED, 1)[0], rig.e.r32(STATUS), rig.e.r32(LIST)), (enabled, status, pointer), 'notification state')
        count += 1
    return count


def shutdown(rig):
    """Check handler unregister, context cleanup and candidate-list release."""
    count = 0
    for mode, registered, have, old in itertools.product((0, 6, 7, 8, 9, 12), (0, 1), (0, 1), (0, 0x4444)):
        rig.reset(mode, registered, old)
        rig.context = CONTEXT if have else 0
        rig.run(0x47B2E0)
        expected = [('unregister', WINDOW, 0x712F4C, 4)]
        if registered:
            expected += [('unregister', WINDOW, 0x712FA8, 7), ('get', WINDOW)]
            if have:
                expected += [('set_open', CONTEXT, 0)]
                if mode in (7, 12):
                    expected += [('get_conversion', CONTEXT), ('set_conversion', CONTEXT, 0, rig.sentence)]
                expected += [('release', WINDOW, CONTEXT)]
            if old:
                expected += [('free', old, FILENAME, 0x29D, 0)]
        require(rig.log, expected, 'shutdown call order')
        require(rig.e.r32(LIST), 0 if registered else old, 'shutdown candidate pointer')
        require(rig.e.r32(COMPOSE), 0x12340000 if registered else 0x12345678, 'shutdown compose word')
        require(rig.e.r32(EXTRA), 0x87650000 if registered else 0x87654321, 'shutdown extra word')
        count += 1
    return count


def initialize(rig):
    """Check language gates, registration and mode-specific initialization."""
    count = 0
    for mode, language, gate, have, ime, opened, conversion in itertools.product((0, 6, 7, 8, 9, 12), (0x411, 0x412, 0x812, 0x409), (0, 1), (0, 1), (0, 1), (0, 1), (0, 1)):
        rig.reset(0, registered=0xA5)
        rig.mode, rig.language, rig.gate = mode, language, gate
        rig.context, rig.ime, rig.set_open, rig.conversion = (CONTEXT if have else 0), ime, opened, conversion
        rig.run(0x47B090)
        expected = [('register', WINDOW, 0x712F4C, 4)]
        registered, enabled, status = 0xA5, 0xA5, 0xBEEF
        recognized = mode in (6, 7, 8, 9, 12)
        active = recognized and gate and have
        if recognized:
            registered = 0
        if recognized and gate:
            expected += [('language',), ('get', WINDOW)]
        if active:
            if mode == 6 and language == 0x411:
                registered = 1
                expected += [('register', WINDOW, 0x712FA8, 7), ('set_open', CONTEXT, 1)]
                enabled, status = (1, 3) if opened else (0, 0)
            elif mode in (7, 12) and language in (0x412, 0x812):
                registered = 1
                expected += [('set_open', CONTEXT, 1), ('get_conversion', CONTEXT), ('set_conversion', CONTEXT, 1, rig.sentence), ('get_conversion', CONTEXT)]
                status = conversion
                enabled = 1 if conversion else 0xA5
                expected += [('register', WINDOW, 0x712FA8, 7)]
            elif mode in (8, 9):
                registered, enabled, status = 1, 0, 5 if mode == 8 else 4
                expected += [('layout', 0), ('is_ime', 0x4090409)]
                if not ime:
                    expected += [('hotkey', WINDOW, 0x10 if mode == 8 else 0x70)]
                expected += [('set_open', CONTEXT, 1), ('register', WINDOW, 0x712FA8, 7)]
            expected += [('release', WINDOW, CONTEXT)]
        require(rig.log, expected, f'init {mode} {language:x} {gate} {have} {ime} {opened} {conversion}')
        require(rig.e.r32(MODE), mode, 'init mode')
        require((rig.e.read(REGISTERED, 1)[0], rig.e.read(ENABLED, 1)[0], rig.e.r32(STATUS)), (registered, enabled, status), 'init state')
        require(rig.e.r32(COMPOSE), 0x12340000 if active else 0x12345678, 'init compose word')
        require(rig.e.r32(EXTRA), 0x87650000 if active else 0x87654321, 'init extra word')
        count += 1
    return count


def main():
    """Verify the game image and run all lifecycle groups."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path)
    args = parser.parse_args()
    require(hashlib.sha256(args.exe.read_bytes()).hexdigest(), SHA256, 'game.exe SHA-256')
    rig = Rig(args.exe)
    for function in (notify, shutdown, initialize):
        print(f'{function.__name__}: {function(rig)} cases passed', flush=True)


if __name__ == '__main__':
    main()
