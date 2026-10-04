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
| DC6/DCC frame anchor: bottom-left, `top = y_offset - height + 1` | apps/d2d/common.hpp `blit_at_anchor` | OpenDiablo2 dcc_direction_frame.go | the fire's constant oy=132 over frames of varying height | the DC6/DCC blitters (D2Gfx part) |
| COF transparent layers: which alpha draw effects 0..2 are | apps/d2d/world_view.hpp `draw_mode` | OpenDiablo2's naming | none | the composite draw's blend switch (after `FUN_00470ec0`) |
| DCC format as a whole | components/dcc/dcc.hpp | OpenDiablo2's reader, Necrolis' notes | every DCC in the MPQs decodes and draws as the game shows it | the DCC decoder (D2Cmp part) |
| DS1 layer stream order | components/ds1/ds1.hpp | OpenDiablo2's reader | every DS1 parses to its exact size | the DS1 loader (`FUN_00665950` area) |
| Objects.txt CycleAnim0..7: 0 = play once, hold the last frame | apps/d2d/ingame.cpp `holds` | objects.txt's column names | an opened chest stops open | the object animation update |
| DT1 block y-shift, `max(0, -min(block.y))` | components/dt1/dt1.hpp | OpenDiablo2's renderer | tiles draw seamlessly | the DT1 loader / tile blitter |
| Iso projection: a cell is 160x80, x → (+80, +40), y → (-80, +40) | components/game/game.hpp `kIsoW`/`kIsoH` | OpenDiablo2's mapengine | floor tiles are 160x80 diamonds and meet | the world→screen transform |
| pal.dat is BGR triples | components/palette/palette.hpp | OpenDiablo2's d2dat.Load | colours match the game's screens | the palette loader |
| TBL values are Latin-1 bytes | components/tbl/tbl.hpp | OpenD2's Latin path | English strings read right | `.\StrTable\strtable.cpp`'s reader |
| .d2s header layout | components/d2s/d2s.hpp | the Phrozen Keep d2s spec | 19 real 1.14d saves (test_d2s) | the save reader |
| .d2s item bitstream layout | components/d2s/d2s_items.hpp | the community d2s spec | 19 real saves: every list ends on the next "JM" | the item (de)serializer |
| PL2 layout past the base palette | docs/research/formats/pl2.md | OpenDiablo2's d2pl2 | probing menu1/Pal.PL2 | the PL2 loader |
| Packet names | docs/research/re/network.md | the community's (d2-clientless, D2MOO) | handlers marked ✓ were read | names only; behaviour is from the handlers |

## Traced

| What | Traced in | Doc |
|---|---|---|
| COF priority row for a direction (round the compass, not DCC order) | `FUN_00470ec0`, `FUN_004db2e0`, `FUN_00600e20` (0x6e55a0), `FUN_004db110` | cof-draw-order.md |
