# Not yet traced in game.exe

game.exe 1.14d is the ground truth. Anything d2d does on the word of
another source (OpenDiablo2, OpenD2, the Phrozen Keep, community specs)
is marked where it's used:

    // unverified (source: <where it came from>): <what>; <evidence so far>

and listed here until it's traced in game.exe; then the tag goes, the
code cites the function or table, and the row moves to "Traced". Other
projects are for checking our reading, never for taking behaviour on
trust. `git grep -n "unverified (source"` lists them.

| What | Where in d2d | Source | Evidence so far | Trace in game.exe |
|---|---|---|---|---|
| DC6/DCC frame anchor: bottom-left, `top = y_offset - height + 1` | apps/d2d/common.hpp `blit_at_anchor` | OpenDiablo2 dcc_direction_frame.go | char-create fire.dc6 (oy=132, heights vary) lands on its logs with FUN_005005b0 subtracting frame 0's offsets; the camp fire RB sits in its ring | the DC6/DCC blitters (D2Gfx part) |
| COF transparent layers: which alpha draw effects 0..2 are | apps/d2d/world_view.hpp `draw_mode` | OpenDiablo2's naming | none | the composite draw's blend switch (after `FUN_00470ec0`) |
| DCC format as a whole | components/dcc/dcc.hpp | OpenDiablo2's reader, Necrolis' notes | every DCC in the MPQs decodes and draws as the game shows it | the DCC decoder (D2Cmp part) |
| DS1 layer stream order | components/ds1/ds1.hpp | OpenDiablo2's reader | every DS1 parses to its exact size | the DS1 loader (`FUN_00665950` area) |
| DT1 block y-shift, `max(0, -min(block.y))` | components/dt1/dt1.hpp | OpenDiablo2's renderer | tiles draw seamlessly | the DT1 loader / tile blitter |
| pal.dat is BGR triples | components/palette/palette.hpp | OpenDiablo2's d2dat.Load | colours match the game's screens | the palette loader |
| TBL values are Latin-1 bytes | components/tbl/tbl.hpp | OpenD2's Latin path | English strings read right | `.\StrTable\strtable.cpp`'s reader |
| .d2s header layout | components/d2s/d2s.hpp | the Phrozen Keep d2s spec | 19 real 1.14d saves (test_d2s) | the save reader |
| .d2s item bitstream layout | components/d2s/d2s_items.hpp | the community d2s spec | 19 real saves: every list ends on the next "JM" | the item (de)serializer |
| PL2 layout past the base palette | docs/research/formats/pl2.md | OpenDiablo2's d2pl2 | probing menu1/Pal.PL2 | the PL2 loader |
| Packet names | docs/research/re/network.md | the community's (d2-clientless, D2MOO) | handlers marked ✓ were read | names only; behaviour is from the handlers |
| Charsi's Imbue pick: after the menu, the next item click takes the item up and imbues it | apps/d2d/town.cpp `imbue_with` | the identify pick's pattern | 0x4b35b0 opens the inventory in UI mode 7; the server imbues the cursor item (act1-end.md) | the panel's click path, 0x4c0620 / 0x4c01e0 |
| A converted monster (align 2, no MonStats interact) gets no hover and no pick: the cursor walks there, a held attack drops it | components/game/ai.hpp `ally_not_interact`, apps/d2d/town.cpp `view_units`, components/game/world.cpp `apply` (UseSkill) | redivivus D2Client/UNIT/CUnit.cpp `HitTestScreen` / `CanInteractWith` (!IsAllyOf for a monster) | the server side is traced: FUN_00554200 makes a converted monster no foe (fight.cpp `land`) | game.exe's client hit test and the interact check it calls |

## Traced

| What | Traced in | Doc |
|---|---|---|
| Throw's stored base level and weapon eligibility are separate. d2d keeps Throw at level 1 and gates the weapon apart (#125: `throw_ready`, `skill_refused`). | `FUN_00647ee0` / `FUN_00647110` grant initial skills at level 1; `FUN_006442a0` reads the level; `FUN_00647640` checks equipment. 35 isolated original-code contexts passed with inventory/item helpers replaced; no C++ parity claim. | [throw.md](throw.md) |
| TCP host join name: 2..15 bytes; ASCII letters and at most one ' - _, not first or last | `FUN_0053eff0` (length), `FUN_0052c5b0` (characters), EDI = 0x10 at `0052c726` | net-join.md "Stage 1" / "Stage 2" |
| Character-create name: 2..15 characters; ASCII letters and at most one - _, not first or last; no apostrophes | `FUN_00430590` (input), `FUN_00430620` (OK state) | char-create-table.md "Name input and OK gating" |
| MonStats AI Idle (the cow): stands, never chases | `FUN_005b0cd0` (MonAI row 1, search type 0): stand 200 in a room | monster-ai.md "Idle" |
| Iso projection: a subtile is (x - y) * 16, (x + y) * 8; units centre on H/2 - 8, floors on the view less the 40-pixel panel | `FUN_00643260`, `FUN_00643310`, `FUN_0045b440` / `FUN_0045afd0`, `FUN_0044c990` / `FUN_00476000`, `FUN_004de730` | apps/d2d/common.hpp `kViewY` |
| COF priority row for a direction (round the compass, not DCC order) | `FUN_00470ec0`, `FUN_004db2e0`, `FUN_00600e20` (0x6e55a0), `FUN_004db110` | cof-draw-order.md |
| Object animation length: Objects.txt FrameCnt[mode] (not the COF / AnimData), held on the last frame without CycleAnim, else back to Start[mode] | `FUN_004bcbb0` | objects.md "Animation length" |
