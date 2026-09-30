# Live join test against a real 1.14d TCP/IP host: join, stay 15 s, leave, save-back.
# python3 join_live.py <save.d2s> [host]  (reads the Huffman and size tables from your own game.exe)
import socket, struct, sys, time, collections, select
EXE = __import__('os').environ.get('D2_GAME_EXE', __import__('os').path.expanduser('~/Workspace/private/diablo2/bin/game.exe'))
SAVE = sys.argv[1]
HOST = sys.argv[2] if len(sys.argv) > 2 else "192.168.50.7"
d = open(EXE,'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec, optsz = struct.unpack_from('<H', d, pe+6)[0], struct.unpack_from('<H', d, pe+20)[0]
base = struct.unpack_from('<I', d, pe+52)[0]
secs = [struct.unpack_from('<IIII', d, pe+24+optsz+40*i+8) for i in range(nsec)]
def va(addr, n):
    for vsz, v, rsz, raw in secs:
        if base+v <= addr < base+v+rsz: return d[raw+addr-base-v:][:n]
L = va(0x7076c0, 256); assert sum(2.0**-l for l in L) == 1.0
SIZES = struct.unpack('<181i', va(0x730ae8, 181*4))
order = sorted(range(256), key=lambda s: (-L[s], s))
codes = {}; code = 0; prev = None
for s in order:
    if prev is not None: code = (code + 1) >> (L[prev] - L[s])
    codes[(L[s], code)] = s; prev = s
def decomp(b):
    out = []; acc = n = 0
    for ch in ''.join(f'{x:08b}' for x in b):
        acc = acc*2 + (ch == '1'); n += 1
        if (n, acc) in codes: out.append(codes[(n, acc)]); acc = n = 0
    return bytes(out)
def psize(p):
    i = p[0]
    if i == 0x16 or i == 0x5b: return struct.unpack_from('<H', p, 1)[0]
    if i == 0x3e: return p[1]
    if i == 0x94: return (p[1]+2)*3
    if i in (0x9c, 0x9d): return p[2]
    if i == 0xa6: return struct.unpack_from('<H', p, 2)[0]
    if i in (0xa8, 0xaa): return p[6]
    if i == 0xac: return p[0xc]
    if i == 0xae: return struct.unpack_from('<H', p, 1)[0] + 3
    if i == 0xaf: return p[1]+1 if p[1] else 2
    if i == 0xb3: return p[1]+7
    if i == 0x26: return None
    return SIZES[i] if i < len(SIZES) and SIZES[i] > 0 else None
def split(stream):
    out = []; i = 0
    while i < len(stream):
        n = psize(stream[i:])
        if not n: out.append(stream[i:]); break
        out.append(stream[i:i+n]); i += n
    return out

save = open(SAVE,'rb').read()
name = save[0x14:0x24].split(b'\0')[0]; cls = save[0x28]
print(f"joining as {name.decode()} class {cls}, save {len(save)} bytes")
s = socket.create_connection((HOST, 4000), timeout=5)
assert s.recv(2) == b'\xaf\x01'
s.sendall(struct.pack('<BIHBIIIB', 0x68, 0, 1, cls, 0x0e, 0x2185edd6, 0x91a519b6, 0) + name.ljust(16, b'\0'))
for off in range(0, len(save), 0xff):
    chunk = save[off:off+0xff]
    pkt = struct.pack('<BBI', 0x6c, len(chunk), len(save)) + chunk + b'\0'
    assert len(pkt) == len(chunk) + 7
    s.sendall(pkt)
buf = b''; hist = collections.Counter(); first = []; back = bytearray(); total = None
t0 = time.time(); joined = left = False; last_ping = 0; closed = False
def handle(p):
    global joined, total
    hist[p[0]] += 1
    if len(first) < 40: first.append(p[:24].hex())
    if p[0] == 0x02 and not joined: s.sendall(b'\x6b')
    if p[0] == 0x04: joined = True; print(f"[04] in the world at {time.time()-t0:.2f}s")
    if p[0] == 0xb4: print("REFUSED reason", hex(struct.unpack_from('<I', p, 1)[0]))
    if p[0] == 0xb3:
        n = p[1]; total = struct.unpack_from('<I', p, 3)[0]; back.extend(p[7:7+n])
while not closed and time.time() - t0 < 60:
    now = time.time()
    if joined and not left and now - last_ping > 5:
        s.sendall(struct.pack('<BIII', 0x6d, int(now*1000) & 0xffffffff, 0, 0)); last_ping = now
    if joined and not left and now - t0 > 15:
        print("leaving (0x69)"); s.sendall(b'\x69'); left = True
    r, _, _ = select.select([s], [], [], 0.1)
    if not r: continue
    c = s.recv(4096)
    if not c: closed = True; break
    buf += c
    while buf:
        b0 = buf[0]
        if b0 < 0xF0: ln, hdr = b0, 1
        elif len(buf) >= 2: ln, hdr = ((b0 & 0x0F) << 8 | buf[1]), 2
        else: break
        if len(buf) < ln: break
        for p in split(decomp(buf[hdr:ln])): handle(p)
        buf = buf[ln:]
s.close()
print(f"socket closed={closed} after {time.time()-t0:.1f}s")
print("packet ids:", ' '.join(f'{k:02x}x{v}' for k, v in sorted(hist.items())))
print("first packets:"); [print("  ", x) for x in first]
if total:
    open(f"/tmp/joined_{name.decode()}.d2s", 'wb').write(back)
    print(f"save-back {len(back)}/{total} bytes, magic ok={back[:4]==b'\x55\xaa\x55\xaa'}, same as sent={bytes(back)==save}")
