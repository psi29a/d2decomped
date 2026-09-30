"""game.exe under unicorn: map the PE, stub its imports, call its functions.

The oracle for bit-exactness checks: run game.exe's own code on the same
inputs as our C++ port and diff the results. Nothing here is Windows; the
few Win32 calls the code paths we run need are stubbed in Python.

    uv run python -c "import emu; e = emu.Emu(); print(e.call(0x45c3e0, ...))"
"""
import os
import struct
import subprocess
from pathlib import Path

import pefile
import unicorn.x86_const
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_INVALID, UC_PROT_ALL
from unicorn.x86_const import *

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
STUB, STUB_SIZE = 0x7F000000, 0x10000        # one 16-byte slot per import / hooked function
STACK, STACK_SIZE = 0x7E000000, 0x800000
HEAP, HEAP_SIZE = 0x20000000, 0x5C000000     # bump allocator, never frees (~60 KB per Blood Moor act)
TEB = 0x7EF00000
RET_MAGIC = STUB + STUB_SIZE - 16            # a call returns here


def data_path():
    return subprocess.run(["defaults", "read", "com.d2decomp.D2 Launcher", "game.dataPath"],
                          capture_output=True, text=True, check=True).stdout.strip()


class Emu:
    def __init__(self, exe=None):
        exe = exe or os.path.join(data_path(), "bin", "game.exe")
        pe = pefile.PE(exe, fast_load=False)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        mu = self.mu = Uc(UC_ARCH_X86, UC_MODE_32)
        size = (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF
        mu.mem_map(self.base, size, UC_PROT_ALL)
        mu.mem_write(self.base, pe.get_memory_mapped_image())
        for r, s in ((STUB, STUB_SIZE), (STACK, STACK_SIZE), (HEAP, HEAP_SIZE), (TEB, 0x10000)):
            mu.mem_map(r, s, UC_PROT_ALL)
        mu.mem_write(STUB, b"\xc3" * STUB_SIZE)
        self.brk = HEAP
        self.slots = {}          # stub address -> (name, handler)
        self.hooks = {}          # game.exe address -> handler (replaces the function)
        self.trace = None
        for imp_dll in pe.DIRECTORY_ENTRY_IMPORT:
            for imp in imp_dll.imports:
                name = (imp.name or f"ord{imp.ordinal}".encode()).decode()
                a = STUB + 16 * len(self.slots)
                self.slots[a] = (name, getattr(Emu, "w_" + name, None))
                mu.mem_write(imp.address, struct.pack("<I", a))
        # fs:[0x18] = TEB self pointer; fs:[0] = SEH chain end
        mu.mem_write(TEB, struct.pack("<I", 0xFFFFFFFF))
        mu.mem_write(TEB + 0x18, struct.pack("<I", TEB))
        # flat SS/DS/ES and FS = the TEB, via a GDT (unicorn has no FS_BASE in 32-bit mode)
        def desc(base, lim, access):
            return (lim & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (access << 40) | (((lim >> 16) & 0xF) << 48) | (0xC << 52) | ((base >> 24) << 56)
        gdt = TEB + 0x8000
        mu.mem_write(gdt + 8, struct.pack("<QQ", desc(0, 0xFFFFF, 0x92), desc(TEB, 0xF, 0x92)))
        mu.reg_write(UC_X86_REG_GDTR, (0, gdt, 0x100, 0))
        for r in (UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES): mu.reg_write(r, 1 << 3)
        mu.reg_write(UC_X86_REG_FS, 2 << 3)
        mu.hook_add(UC_HOOK_CODE, self._stub, begin=STUB, end=STUB + STUB_SIZE - 1)
        mu.hook_add(UC_HOOK_MEM_INVALID, self._bad)
        self.files = {}
        self._mpqs = os.path.join(data_path())
        self._cat = ROOT / "build" / "tools" / "mpq-cat" / "mpq-cat"
        self.last_error = 0

    # ---- memory -------------------------------------------------------
    def alloc(self, n, zero=True):
        a = self.brk
        self.brk = (self.brk + max(n, 1) + 15) & ~15
        if self.brk > HEAP + HEAP_SIZE: raise MemoryError("emu heap exhausted")
        if zero: self.mu.mem_write(a, b"\0" * n)
        return a

    def r32(self, a): return struct.unpack("<I", self.mu.mem_read(a, 4))[0]
    def s32(self, a): return struct.unpack("<i", self.mu.mem_read(a, 4))[0]
    def w32(self, a, v): self.mu.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))
    def read(self, a, n): return bytes(self.mu.mem_read(a, n))
    def cstr(self, a):
        out = bytearray()
        while (b := self.mu.mem_read(a + len(out), 1)[0]): out.append(b)
        return out.decode("latin-1")
    def put(self, data):
        # 64 zero bytes past the end: game.exe reads past some files (Trees.ds1's 14th group)
        a = self.alloc(len(data) + 64)
        self.mu.mem_write(a, bytes(data))
        return a

    # ---- calls --------------------------------------------------------
    def arg(self, i): return self.r32(self.mu.reg_read(UC_X86_REG_ESP) + 4 + 4 * i)

    def hook(self, addr, fn, nstack):
        """Replace game.exe's function at addr: fn(emu) -> eax; callee pops nstack args."""
        a = STUB + 16 * (len(self.slots) + 1)
        self.slots[a] = (f"hook_{addr:x}", lambda e: (fn(e), nstack))
        self.mu.mem_write(addr, b"\xe9" + struct.pack("<i", a - (addr + 5)))

    def call(self, addr, *args, ecx=0, edx=0, cdecl=False, limit=0, regs=None):
        mu = self.mu
        sp = STACK + STACK_SIZE - 0x1000
        for v in reversed((RET_MAGIC,) + tuple(args)):
            sp -= 4
            self.w32(sp, v)
        mu.reg_write(UC_X86_REG_ESP, sp)
        mu.reg_write(UC_X86_REG_EBP, 0)
        mu.reg_write(UC_X86_REG_ECX, ecx)
        mu.reg_write(UC_X86_REG_EDX, edx)
        for name, v in (regs or {}).items(): mu.reg_write(getattr(unicorn.x86_const, "UC_X86_REG_" + name.upper()), v)   # custom-convention args (EBX, ESI, EDI, EAX)
        try:
            mu.emu_start(addr, RET_MAGIC, count=limit)
        except UcError as err:
            eip = mu.reg_read(UC_X86_REG_EIP)
            raise RuntimeError(f"emu fault at {eip:08x}: {err}; stack {self.backtrace()}") from None
        return mu.reg_read(UC_X86_REG_EAX)

    def backtrace(self):
        """Code addresses on the stack, nearest first (a heuristic, not an unwind)."""
        sp = self.mu.reg_read(UC_X86_REG_ESP)
        n = min(512, (STACK + STACK_SIZE - sp) // 4)
        return " ".join(f"{v:x}" for v in struct.unpack(f"<{n}I", self.read(sp, 4 * n)) if 0x401000 <= v < 0x6cc000)

    def _bad(self, mu, access, addr, size, value, _):
        print(f"emu: bad memory access {access} at {addr:08x} (eip {mu.reg_read(UC_X86_REG_EIP):08x})")
        return False

    def _stub(self, mu, addr, size, _):
        if addr == RET_MAGIC or addr not in self.slots: return
        name, fn = self.slots[addr]
        if fn is None:
            ret = self.r32(mu.reg_read(UC_X86_REG_ESP))
            raise SystemExit(f"emu: unstubbed import {name} (called from {ret:08x}; stack {self.backtrace()})")
        r = fn(self)
        eax, n = r if isinstance(r, tuple) else (r, None)
        if n is None: raise SystemExit(f"emu: stub {name} gave no arg count")
        sp = mu.reg_read(UC_X86_REG_ESP)
        ret = self.r32(sp)
        mu.reg_write(UC_X86_REG_EAX, (eax or 0) & 0xFFFFFFFF)
        mu.reg_write(UC_X86_REG_ESP, sp + 4 + 4 * n)
        mu.reg_write(UC_X86_REG_EIP, ret)

    def fatal(e):
        a = [e.arg(i) for i in range(4)]
        txt = [e.cstr(v)[:120] if 0x400000 <= v < 0x80000000 else hex(v) for v in a]
        ret = e.r32(e.mu.reg_read(UC_X86_REG_ESP))
        raise SystemExit(f"emu: game.exe fatal error from {ret:08x}: {txt}")

    def crt_init(self):
        """The statically linked MSVC CRT's startup: heap, per-thread data, static constructors."""
        self.hook(0x408a60, Emu.fatal, 0)
        # ponytail: string tables not loaded; name lookups (FUN_00524d30(key, &str)) get id 0, ""
        empty = self.alloc(4)
        self.hook(0x524d30, lambda e: (e.w32(e.arg(1), empty) if e.arg(1) else None, 0)[1], 2)
        # __get_osplatform / __get_winmajor: NT, 6
        self.hook(0x681b00, lambda e: (e.w32(e.arg(0), 2), 0)[1], 0)
        self.hook(0x681baf, lambda e: (e.w32(e.arg(0), 6), 0)[1], 0)
        self.call(0x68c7d1, 1)      # __heap_init
        self.call(0x688fbb)         # __mtinit
        # ponytail: __cinit (0x681c95) skipped - its C initialisers need locale setup; add if a global ctor matters

    # ---- files --------------------------------------------------------
    def mpq_file(self, name):
        """A file from the game's MPQ stack (1.14d patch first), cached."""
        key = name.lower().replace("/", "\\")
        if key not in self.files:
            cache = HERE / ".cache" / key.replace("\\", "_")
            if not cache.exists():
                env = dict(os.environ)
                env.setdefault("D2_PATCH_INSTALLER", str(Path.home() / "Downloads/Diablo II + LoD/patch/LODPatch_114d.exe"))
                p = subprocess.run([str(self._cat), self._mpqs, name], capture_output=True, env=env)
                cache.parent.mkdir(exist_ok=True)
                cache.write_bytes(p.stdout if p.returncode == 0 else b"")
                if p.returncode: cache.with_suffix(".missing").touch()
            self.files[key] = None if cache.with_suffix(".missing").exists() else cache.read_bytes()
        return self.files[key]

    # ---- Win32 stubs: w_<name>(emu) -> (eax, stdcall arg count) ----------
    def w_GetLastError(self): return self.last_error, 0
    def w_SetLastError(self): self.last_error = self.arg(0); return 0, 1
    def w_HeapAlloc(self): return self.alloc(self.arg(2)), 3
    def w_HeapFree(self): return 1, 3
    def w_HeapSize(self): return 0, 3
    def w_HeapReAlloc(self):
        old, n = self.arg(2), self.arg(3)
        a = self.alloc(n)
        if old: self.mu.mem_write(a, self.read(old, min(n, self.brk - old)))
        return a, 4
    def w_GetProcessHeap(self): return 0x1234, 0
    def w_VirtualAlloc(self): return self.alloc((self.arg(1) + 0xFFF) & ~0xFFF), 4
    def w_VirtualFree(self): return 1, 3
    def w_EnterCriticalSection(self): return 0, 1
    def w_LeaveCriticalSection(self): return 0, 1
    def w_InitializeCriticalSection(self): return 0, 1
    def w_InterlockedIncrement(self):
        a = self.arg(0); v = self.r32(a) + 1; self.w32(a, v); return v, 1
    def w_InterlockedDecrement(self):
        a = self.arg(0); v = self.r32(a) - 1; self.w32(a, v); return v, 1
    def w_GetTickCount(self): return 0, 0
    def w_GetCurrentThreadId(self): return 1, 0
    tls = {}
    def w_TlsAlloc(self): Emu.tls[len(Emu.tls) + 1] = 0; return len(Emu.tls), 0
    def w_TlsGetValue(self): return Emu.tls.get(self.arg(0), 0), 1
    def w_TlsSetValue(self): Emu.tls[self.arg(0)] = self.arg(1); return 1, 2
    def w_TlsFree(self): return 1, 1
    w_FlsAlloc = lambda self: (Emu.w_TlsAlloc(self)[0], 1)
    w_FlsGetValue, w_FlsSetValue, w_FlsFree = w_TlsGetValue, w_TlsSetValue, w_TlsFree
    def w_EncodePointer(self): return self.arg(0), 1
    def w_DecodePointer(self): return self.arg(0), 1
    def w_GetModuleFileNameA(self):
        v = b"C:\\D2\\Game.exe"; self.mu.mem_write(self.arg(1), v + b"\0"); return len(v), 3
    def w_GetModuleHandleA(self): return self.base, 1
    def w_GetCurrentProcessId(self): return 1, 0
    def w_QueryPerformanceCounter(self): self.w32(self.arg(0), 0); self.w32(self.arg(0) + 4, 0); return 1, 1
    def w_Sleep(self): return 0, 1
    def w_OutputDebugStringA(self): return 0, 1
    def w_HeapCreate(self): return 0x1234, 3
    def w_GetModuleHandleW(self): return self.base, 1
    def w_GetProcAddress(self): return 0, 2        # CRT falls back to the Tls* imports
    def w_GetACP(self): return 1252, 0
    def w_GetCommandLineA(self): return self.put(b"Game.exe"), 0
    def w_SetUnhandledExceptionFilter(self): return 0, 1
    def w_InitializeCriticalSectionAndSpinCount(self): return 1, 2
    def w_DeleteCriticalSection(self): return 0, 1
    def w_RegOpenKeyExA(self): return 2, 5            # ERROR_FILE_NOT_FOUND
    def w_RegCloseKey(self): return 0, 1
    def w_RegCreateKeyExA(self): return 5, 9          # ERROR_ACCESS_DENIED
    def w_RegQueryValueExA(self): return 2, 6
    def w_RegSetValueExA(self): return 5, 6
    def w_GetFileAttributesA(self): return 0xFFFFFFFF, 1  # no loose files on disk
    def w_CreateFileA(self): return 0xFFFFFFFF, 7
    def w_CopyRect(self): self.mu.mem_write(self.arg(0), self.read(self.arg(1), 16)); return 1, 2
    def w_PtInRect(self):
        l, t, r, b = struct.unpack("<4i", self.read(self.arg(0), 16))
        x, y = struct.unpack("<2i", struct.pack("<2I", self.arg(1), self.arg(2)))
        return int(l <= x < r and t <= y < b), 3
    def w_wsprintfA(self):              # cdecl: caller pops
        out, fmt = self.arg(0), self.cstr(self.arg(1))
        args, i, s = [], 2, ""
        j = 0
        while j < len(fmt):
            c = fmt[j]
            if c != "%": s += c; j += 1; continue
            k = j + 1
            while fmt[k] in "0123456789-.l": k += 1
            spec, conv = fmt[j:k + 1], fmt[k]
            v = self.arg(i); i += 1
            if conv == "s": s += self.cstr(v)
            elif conv in "dixXu": s += (spec.replace("l", "") % (v if conv != "d" and conv != "i" else struct.unpack("<i", struct.pack("<I", v))[0]))
            elif conv == "c": s += chr(v)
            else: s += "%"; i -= 1
            j = k + 1
        self.mu.mem_write(out, s.encode("latin-1") + b"\0")
        return len(s), 0



def serve_files(e):
    """Storm's file calls (all stdcall) answered from the MPQ stack."""
    open_files = {}

    def trace(msg):
        if e.trace: e.trace(msg)

    def sopen(e):
        name, ph = e.cstr(e.arg(0)), e.arg(1)
        data = e.mpq_file(name)
        trace(f"file {name}: {'missing' if data is None else len(data)}")
        if data is None:
            e.last_error = 2
            return 0
        h = 0x5000 + 4 * len(open_files)
        open_files[h] = [name, data, 0]
        e.w32(ph, h)
        return 1

    def ssize(e):
        if e.arg(1): e.w32(e.arg(1), 0)
        return len(open_files[e.arg(0)][1])

    def sread(e):
        f = open_files[e.arg(0)]
        chunk = f[1][f[2]:f[2] + e.arg(2)]
        e.mu.mem_write(e.arg(1), chunk)
        f[2] += len(chunk)
        if e.arg(3): e.w32(e.arg(3), len(chunk))
        return 1 if len(chunk) == e.arg(2) else 0

    def sseek(e):
        f = open_files[e.arg(0)]
        pos = struct.unpack("<i", struct.pack("<I", e.arg(1)))[0]
        f[2] = [0, f[2], len(f[1])][e.arg(3)] + pos
        return f[2]

    def sname(e):
        e.mu.mem_write(e.arg(1), open_files[e.arg(0)][0].encode("latin-1") + b"\0")
        return 1

    e.hook(0x419980, sopen, 2)
    e.hook(0x416370, ssize, 2)
    e.hook(0x41aad0, sread, 7)
    e.hook(0x4198d0, lambda e: (open_files.pop(e.arg(0), None), 1)[1], 1)
    e.hook(0x416230, sname, 3)
    e.hook(0x4165a0, sseek, 4)
