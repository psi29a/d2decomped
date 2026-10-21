import emu, struct, sys
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX
def run(width):
    e = emu.Emu()
    out = []
    state = {'font': 0}
    def wstr(s):
        return e.put((s + '\0').encode('utf-16-le'))
    def rw(a):
        b = bytearray()
        while True:
            c = e.read(a + len(b), 2)
            if c == b'\0\0': break
            b += c
        return b.decode('utf-16-le')
    e.hook(0x524a30, lambda m: wstr('S%x' % m.mu.reg_read(UC_X86_REG_ECX)), 0)
    e.hook(0x469de0, lambda m: wstr('K%x/%d' % (m.mu.reg_read(UC_X86_REG_ECX), m.mu.reg_read(UC_X86_REG_EDX))), 0)
    def sprintf(m):
        dst, fmt, arg = m.arg(1), m.arg(2), m.arg(3)
        s = rw(fmt) + '|' + rw(arg)
        m.mu.mem_write(dst, (s + '\0').encode('utf-16-le'))
        return 0
    e.hook(0x5269d0, sprintf, 0)
    e.hook(0x501820, lambda m: width, 0)
    def draw(m):
        out.append((rw(m.mu.reg_read(UC_X86_REG_ECX)), m.mu.reg_read(UC_X86_REG_EDX), m.arg(0), m.arg(1), m.arg(2), state['font']))
        return 0
    e.hook(0x502320, draw, 3)
    def font(m):
        prev = state['font']; state['font'] = m.mu.reg_read(UC_X86_REG_ECX); return prev
    e.hook(0x502ef0, font, 0)
    e.call(int(sys.argv[1], 16))
    return out
a, b = run(0), run(100)
for (t, x, y, c, f, font), (_, x2, *_r) in zip(a, b):
    align = 'left' if x == x2 else 'centre' if x - x2 == 50 else 'right' if x - x2 == 100 else '?'
    print(f"{t:28s} x={x:4d} y={y:4d} colour={c} font={font} {align}")
