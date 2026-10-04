# Superuniques and the Act 1 bosses (game.exe 1.14d)

What game.exe does per superunique, beyond the spawn in preset-specials.md
§1.4 (FUN_005a49b0) and the mods in monsters.md. Every read of a
SuperUniques record was traced, so the list of special cases below is
complete.

## The record (FUN_006556e0, 0x34 bytes, table game +0xad4, count +0xadc)

| off | column | read by |
|---|---|---|
| +4 | Class (MonStats row) | FUN_005a49b0, FUN_005ef320 (act 3) |
| +8 | hcIdx | the switches in FUN_005a49b0, FUN_005a4440, FUN_00543a30 |
| +0xc | Mod1..3 (int) | FUN_005a49b0 |
| +0x18 | MonSound (MonSounds row, int) | client FUN_004ca410 |
| +0x1c / +0x20 | MinGrp / MaxGrp | FUN_005a0c00 |
| +0x24 | AutoPos | FUN_005a49b0 |
| +0x26 | Stacks | FUN_005a49b0 |
| +0x28 | Utrans x3 (bytes) | client FUN_00466360 |
| +0x2c | TC x3 (u16) | FUN_005a6600 via FUN_005a03a0 |

Nothing reads EClass (+0x25) or Replaceable (+0x27). The monster keeps its
superunique index at monster data +0x26, valid while flag +0x16 & 2 is set
(FUN_005a03a0 server, FUN_004ac7c0 client).

All readers of the record (the 7 FUN_006556e0 calls):

- FUN_00466360: Utrans (ported).
- FUN_0049eb10: the "slain by" text for a hardcore death (not ported).
- FUN_004ac870: the name (ported).
- FUN_004ca410: the sound set (ported, below).
- FUN_005a49b0: the spawn (ported).
- FUN_005a6600 → FUN_005a03a0: the TC (ported).
- FUN_005ef320: act 3.

The only other per-superunique test is FUN_005a1650 (su 37 → Fanaticism).

## Act 1 superuniques

FUN_005a49b0's switch has only one Act 1 case: hcIdx 6, the Countess (stat
0x76, quest 5, special AI 0xd). The Cow King (0x27) gets quest 4. Every
other Act 1 superunique works from its data alone:

- Bishibosh, Bonebreak, Coldcrow, Rakanishu, Treehead, Griswold, Pitspawn,
  Flamespike, Boneash, the Smith and Corpsefire need only their mods
  (Rakanishu's bolts are mod 17; Corpsefire's elements are mod 27).
- Griswold and the Smith have their own MonAI (monster-ai.md).
- Blood Raven is not a superunique. She is MonPlace 5 (below).
- The Countess's runes come from her TC (quests-act1.md "Countess drop").

## MonUMod 22 (questcomplete)

FUN_005a49b0 gives this mod to every superunique. Its only hook is event 1
(after a mode change), at 0x5a3250. That hook runs only in mode 0 (death)
and switches on the MonStats class (monster +4):

| class | function |
|---|---|
| 0x9c andariel | FUN_005dfe00 |
| 0xe5 | FUN_005dfe20 |
| 0xf2 | FUN_005dfdb0 |
| 0x100 | FUN_005e0020 |
| 0x10b bloodraven | FUN_005dfd90 |
| 0x1df | FUN_005e0040 |
| 0x21c..0x21e | FUN_005e0060 |
| 0x2c0, 0x2c1, 0x2c5 | FUN_005e0070 |

So for every Act 1 superunique, mod 22 does nothing.

## Act bosses at spawn: FUN_005b1cf0 (from FUN_005b2a00)

The switch is on BaseId (MonStats +2). Each `FUN_005a4850(mod, 1)` also runs
FUN_005a0320: it sets flag 8 and counts one unique, but only if flag 8 was
not set yet.

- **0x9c Andariel**: mod 22 and quest 6. Class 0x2c3 (uberandariel) gets
  mods 23, 6 and 0x1d instead.
- **0x10b Blood Raven**: mod 12 (bloodraven; it has no init or hook, only a
  name handler in the client table 0x724d78), mod 22, quest 2, and stat
  0x76 = 1 (half freeze).
- **0x11c maggotqueen**: mods 23 and 22.
- **0x192 the Smith** (Barracks): mod 22 only, no quest call. Found by the
  sweep: level 28 printed `402!22` in game.exe and `402` in d2d.

Neither mod 12 nor 22 has an init in the mod table at 0x73c008 (both 0),
so the bosses get flag 8 and nothing else of a unique: no leveladd or
hpmultiply (those are mods 4 and 2, from FUN_005a2120). Flag 8 then gives
the gold name, the "Demon" label (both are MonStats demon) and
TreasureClass3 (FUN_005a6600 tests flag 4 for TC2, then flag 8 for TC3;
both bosses' TC3 is their TC1). noUniqueShift is set on both rows, so no
colour shift.

d2d: `rules::act_boss` in `spawn_monsters` (Boss::unique without
make_boss's stats). Their quests key off the kill's MonStats row
(world.cpp). Checked: `monsters.py` prints the mods FUN_005b1cf0 adds and
the stat 0x76 (`267!12.22h`, `156!22`, `402!22`), as `drlg-dump ... monsters` does.

By FUN_005a0320's flag-8 guard each boss counts one unique, whatever its
mods; objgroups.cpp counts 1 for Andariel, Blood Raven and the Maggot Queen.
Counting the Smith there too puts level 28's later monsters off game.exe
(sweep, seeds 1, 3, 5, 6, 8), so its unique is counted some other way or
not at all; not traced.

## Client: the name colour and the sound set

**The name over the life bar** (FUN_00454ad0): the colour passed to
FUN_005022f0.

- 4 (gold) with flag 8 (FUN_004ae360). That covers uniques and superuniques,
  and champions too, since FUN_005a48c0 → FUN_005a0320 sets it.
- 3 (blue) with flag 4 (FUN_004ae320), a champion. This overrides the gold.
- 1 (red) for a corpse.
- Always 4 for MonStats rows 0xd3, 0xe5, 0xf2, 0xf3, 0x14d, 0x220, 0x23a
  and 0x2c0..0x2c5: duriel, radament, mephisto, diablo, the clones,
  baalcrab and the ubers. Andariel is not on the list.
- d2d: `rules::bar_name_colour`, drawn by town.cpp's draw_monster_bar.

**The sound set** (FUN_004ca410, UnitSnd.cpp; record 0x94 bytes at
DAT_0096c6b4):

1. A superunique (flag 2) whose record +0x18 is > 0 uses that row. In
   Act 1 that is the Countess's "countess" and the Smith's "smith".
2. Else, flag 8 or 0x10 (FUN_004ae360 / FUN_004ae380) with MonStats +0x16
   (UMonSound) > 0 uses that. Only the zombies differ here: "zombieunique".
3. Else MonStats +0x14 (MonSound), via FUN_00656fc0.

d2d: `rules::boss_sound`, used by Fight::monster_sounds. d2d's monster
kinds are a proxy for the flags: Boss::champion, unique and superunique
stand for flag 8, Boss::minion for 0x10.

## FUN_005a4440: a superunique restored

FUN_005424f0 calls FUN_005a4440 to rebuild a stored monster when its room
comes back. It re-applies the saved mods and the superunique index. It
re-binds quests by hcIdx: 6 → 5, 0x27 → 4, and the later acts'.

It does **not** re-set special AI 0xd on a restored Countess (bugs.md 15).
d2d never frees a room, so a d2d Countess keeps it (drlg.md "A room's
life on the server").
