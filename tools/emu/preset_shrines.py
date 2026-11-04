# SPDX-License-Identifier: GPL-3.0-or-later
"""A preset shrine's kind (574..579, FUN_0054f0d0) against rules::preset_shrine.

    uv run python preset_shrines.py

FUN_00555230 (the object's making and InitFn) returns a bare unit whose
seed (+0x20) is set here; the kind is what lands in its data +4.
"""
import emu
e=emu.Emu()
UNIT=[0]
e.hook(0x555230, lambda e: UNIT[0], 7)
e.hook(0x6414b0, lambda e: 0, 1)
e.hook(0x621bb0, lambda e: 0, 2)
RANGES={574:(2,6),575:(7,7),576:(8,11),577:(12,12),578:(1,5),579:(14,14)}
def port(oid,low,high):           # rules::preset_shrine
    lo,hi=RANGES[oid]; n=hi-lo; k=lo
    if n>=1:
        p=low*0x6AC690C5+high; low=p&0xffffffff; high=p>>32
        k=lo+(low&(n-1) if n&(n-1)==0 else low%n)
    return 2 if k in (4,5) else k, low, high
bad=n=0
for oid in RANGES:
    for seed in range(1,400,3):
        u=e.alloc(0x100); d=e.alloc(0x40)
        e.w32(u,2); e.w32(u+0x14,d); e.w32(u+0x20,seed); e.w32(u+0x24,666)
        UNIT[0]=u
        e.call(0x54f0d0, oid, 0, 0, 0, ecx=0, edx=1)
        want,lo,hi=port(oid,seed,666)
        got=e.read(d+4,1)[0]
        n+=1
        if (got,e.r32(u+0x20),e.r32(u+0x24))!=(want,lo,hi): bad+=1; print(oid,seed,got,want)
print(f"ok: {n} cases" if not bad else f"{bad} of {n} differ")
if bad:
    raise SystemExit(1)
