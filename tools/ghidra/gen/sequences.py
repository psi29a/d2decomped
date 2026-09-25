"""Regenerate components/rules/sequences.hpp from game.exe's player sequence
table. Dump the bytes first (0x745000..0x748418, the frames and the table):
  analyzeHeadless tools/ghidra/project D2Decomp -process game.exe -noanalysis -readOnly \
    -scriptPath tools/ghidra/scripts -postScript DumpBytes 0x745000 0x748418 4 <dir>/seqraw.txt
then: python3 tools/ghidra/gen/sequences.py <dir> > components/rules/sequences.hpp
(<dir> holds seqraw.txt; the header template is sequences_head.txt beside this script)."""
import os,re,sys
S=sys.argv[1]
HERE=os.path.dirname(os.path.abspath(__file__))
mem={}
for l in open(S+"/seqraw.txt"):
    m=re.match(r"([0-9a-f]{8}): ((?:[0-9a-f]{2} )+)",l)
    if m:
        a=int(m.group(1),16)
        for i,b in enumerate(m.group(2).split()): mem[a+i]=int(b,16)
def u32(a): return sum(mem[a+i]<<(8*i) for i in range(4))
seqs=[u32(0x7483b8+4*i) for i in range(24)]
W=["hth","1ht","2ht","1hs","2hs","bow","xbw","stf","1js","1jt","1ss","1st","ht1","ht2"]
flat=[]; idx=[]
for n in range(1,24):
    for w in range(14):
        p,c=u32(seqs[n]+w*12),u32(seqs[n]+w*12+4)
        if not p: continue
        assert all(mem[p+i*6]==0 and mem[p+i*6+1]==0 and mem[p+i*6+4]==0 for i in range(c))
        idx.append((n,w,len(flat),c))
        flat += [(mem[p+i*6+2],mem[p+i*6+3],mem[p+i*6+5]) for i in range(c)]
def rows(items):
    out=[]; line="   "
    for s in items:
        if len(line)+len(s)>110: out.append(line); line="   "
        line+=s
    return out+[line]
hdr = open(os.path.join(HERE, "sequences_head.txt")).read()
body = "\n".join(rows([f" {{{a},{b},{c}}}," for a,b,c in flat]))
ents = "\n".join(rows([f" {{{n},{w},{o},{c}}}," for n,w,o,c in idx]))
print(hdr.replace("@FRAMES@", body).replace("@ENTRIES@", ents), end="")
print(len(flat), len(idx), file=sys.stderr)
