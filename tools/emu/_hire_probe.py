import emu, sys
from unicorn.x86_const import *
e = emu.Emu()
reg = lambda r: e.mu.reg_read(r)
out = []
def wstr(s): return e.put((s + '\0').encode('utf-16-le'))
def rw(a):
    b = bytearray()
    while len(b) < 400:
        c = e.read(a + len(b), 2)
        if c == b'\0\0': break
        b += c
    return b.decode('utf-16-le', 'replace')
W = int(sys.argv[1])
e.hook(0x524a30, lambda m: wstr('S%x' % reg(UC_X86_REG_ECX)), 0)
e.hook(0x501820, lambda m: W, 0)
font = [-1]
def setfont(m): p = font[0]; font[0] = reg(UC_X86_REG_ECX); return p
e.hook(0x502ef0, setfont, 0)
e.hook(0x502320, lambda m: out.append(('text', rw(reg(UC_X86_REG_ECX)), reg(UC_X86_REG_EDX), e.s32(reg(UC_X86_REG_ESP) + 4), m.arg(1), font[0])) or 0, 3)
e.hook(0x502520, lambda m: 0, 0)
e.hook(0x625480, lambda m: 50, 3); e.hook(0x6253b0, lambda m: 50, 3); e.hook(0x625500, lambda m: 0, 3)
e.hook(0x625d10, lambda m: 100 << 8, 1)
e.hook(0x4a7660, lambda m: 0, 0)
for f in (0x451f50, 0x479080, 0x464820, 0x6223f0, 0x63a550, 0x44dcc0, 0x451f60, 0x63bde0, 0x6289c0):
    e.hook(f, lambda m: 0, 0)
e.hook(0x464a60, lambda m: wstr('MercName'), 0)
e.w32(0x7a2858, 0x50); e.w32(0x7a285c, 0xffffffc4); e.w32(0x71146c, 800); e.w32(0x711470, 600)
unit = e.alloc(0x200); e.mu.mem_write(unit, bytes(0x200))
for fn, regs in ((0x48a7d0, {}), (0x48a890, {}), (0x48a990, {'ebx': unit})):
    try: e.call(fn, regs=regs)
    except Exception as err: print('ERR', hex(fn), err)
for o in out: print(o)
