# SPDX-License-Identifier: GPL-3.0-or-later
"""A pet's clear line to its foe (FUN_005dc640) against rules::pet_line_clear.

    uv run python pet_line.py

Lines from the pet (its size) to the foe's spot and to two spots beside it
(2..4 off by their gap, the step across the line), each to a size-2 end,
mask 0x805 (FUN_006229f0 -> FUN_00622920); any one clear will do.
"""
import random

import emu
from sight import port as sight
e=emu.Emu()
W=H=48
words=e.alloc(W*H*2)
coll=e.alloc(0x40); e.w32(coll+8,W); e.w32(coll+0x20,words)
room=e.alloc(0x80); e.w32(room+0x20,coll)
for off,v in ((0x4c,0),(0x50,0),(0x54,W),(0x58,H)): e.w32(room+off,v)
e.hook(0x620bb0, lambda e: room, 1)
SIZES={}
e.hook(0x620510, lambda e: SIZES.get(e.arg(0),1), 1)
def unit(x,y,size):
    u=e.alloc(0x100); p=e.alloc(0x100); e.mu.mem_write(u,bytes(0x100)); e.mu.mem_write(p,bytes(0x100))
    e.w32(u,1); e.w32(u+0x2c,p); e.w32(p,x<<16|0x8000); e.w32(p+4,y<<16|0x8000); e.w32(p+0x1c,room); e.w32(p+0x30,u)
    SIZES[u]=size
    return u
TABLE=[2,2,2]+[3]*8+[4]*14
def port(px,py,ps,tx,ty,ts,grid,ssize):
    dx,dy=max(abs(px-tx)-ps,0),max(abs(py-ty)-ps,0)   # 5dc380 with the pet's size (guess)
    d=(dy+dx*2 if dx>dy else dx+dy*2)//2
    k=TABLE[d] if d<25 else 3
    sx=(k if px>tx else -k if px<tx else 0); sy=(k if py>ty else -k if py<ty else 0)
    wall=lambda x,y: not (0<=x<W and 0<=y<H) or grid[y][x]&0x805!=0
    for (x,y) in ((tx,ty),(tx-sy,ty+sx),(tx+sy,ty-sx)):
        if not sight(px,py,ps,x,y,ssize,wall): return 1
    return 0
rng=random.Random(3); bad={2:0}; n=0
for case in range(1500):
    grid=[[0]*W for _ in range(H)]
    for _ in range(rng.choice((0,20,80,300))): grid[rng.randrange(H)][rng.randrange(W)]=rng.choice((1,4,0x800,0x400))
    e.mu.mem_write(words, bytes(b for row in grid for v in row for b in v.to_bytes(2,'little')))
    px,py,tx,ty=(rng.randrange(8,W-8) for _ in range(4)); ps,ts=rng.randrange(0,4),rng.randrange(0,4)
    mark=e.brk
    pet=unit(px,py,ps); t=unit(tx,ty,ts)
    got=e.call(0x5dc640, ecx=pet, edx=t)&0xff
    e.brk=mark
    n+=got
    for s in bad:
        if bool(got)!=bool(port(px,py,ps,tx,ty,ts,grid,s)): bad[s]+=1
print(f"ok: 1500 cases, {n} clear" if not bad[2] else f"{bad[2]} of 1500 differ")
