# The .d2s header (`FUN_00568f20`, `FUN_0056a090`)

`.\PLAYER\PlrSave2.cpp`. `FUN_00568f20` builds the 0x14f-byte header on
the stack and copies it to the save; `FUN_0056a090` checks it and makes
the player. The field list is in `components/d2s/d2s.hpp`; the offsets
are the code's and 21 real 1.14d saves agree with them.

## Written (`FUN_00568f20`)

- Magic `0xaa55aa55`, version `0x60`, the name (`SStrCopy`), the status
  as a u32: the client's, `| 0x20` in an expansion game, `| 0x40` when
  the game isn't single player.
- Class (+0x28), level (+0x2b), creation time (+0x2c, the client's: 0 in
  every save seen), `_time32` (+0x30), `0xffffffff` (+0x34), the map seed
  (+0xab), 0x10 (+0x29) and the skills per class (+0x2a, 30).
- +0x10 bit 0: the weapon switch's second set.
- 16 hotkeys from +0x38 and the four hands from +0x78: a u16 skill
  (`| 0x8000` for the left button on a hotkey) and a u16 item index
  (`FUN_00568dc0`: which item gives the skill; 0 none). A skill id of
  0x8000 or more is fatal (line 0x1eb).
- The played difficulty's byte (+0xa8 + difficulty): the act `| 0x80`.
- Appearance (+0x88, +0x98): sixteen `0xff`, then `SaveCharAppearance`.
- The merc (`FUN_00568e60`): seed (+0xb3), name index less the
  Hireling.txt row's NameFirst (+0xb7), type (+0xb9), experience (+0xbb),
  flags (+0xaf: 0x10000 dead). Nothing without a hired merc.
- +0xcf: the guild rank. The rest of the header stays zero.

## Read (`FUN_0056a090`)

In order: the checksum (+0x0c, the sum computed with the field zeroed),
the file size (+0x08), the version (accepted 0x5c to 0x60), the name
against the client's (`_stricmp`; byte 15 forced to 0), the status's low
16 bits (`FUN_00569d80`), the class (above 7 refused). Status bit 0
returns 2 before anything else is read. Then the act: the played
difficulty's low 7 bits, **5 or more becomes act 1**; a single-player
game takes the map seed from +0xab when that difficulty's byte has 0x80
and no seed is set. Then the hotkeys and hands.

## What this changed in d2d

- `Header::active_act` clamped `& 7` to 4; it is now `& 0x7f`, and 5 or
  more is act 1, as game.exe reads it.
- `write_save` writes `0xffffffff` at +0x34 as game.exe does. d2d's own
  saves from before had 0 there; they mend on the next save.
