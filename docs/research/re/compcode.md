# Composite component table — game.exe 1.14d

What a .d2s appearance byte means. The save header stores 16 bytes at
0x88 (one per composite layer: HD TR LG RA LA RH LH SH S1..S8) and 16
tint bytes at 0x98. Char-select builds each character's portrait from
them (`FUN_005066c0(class, mode, appearance, tints)`), and each byte is
an index into a table built once at startup.

## Builder: `FUN_00506000` → table at `0x87d838`

12-byte entries `{code, wclass, type}` (+ 2-handed wclass in a parallel
array at `0x87e430`), count forced to 0xff at the end.

1. Entries 1..3 are hard-coded `"lit"`, `"med"`, `"hvy"` (body-part
   armour tiers). Entry 0 = none.
2. Walk every item record (0x1a8 bytes; weapons, then armor, then misc).
   Code = `+0x90` alternategfx, else `+0x80` code. Skip it if the code
   already appears **below the cursor**.
3. Keep only items whose type (`+0x11e`) is-a Weapon (ItemTypes 45),
   Armor/tors (3), Any Shield (51) or Helm (37), and not Circlet (75).
   Is-a = `FUN_00504a20`, walking Equiv1/Equiv2.
4. Slot search starts at the cursor (initially 4) and skips a slot if
   its **reserved type** and the item are both weapons (45), or both any
   armour (50), or the slot is taken. Reserved types are the first dword
   of each 12-byte record of a static list at `0x72e1e8` (283 entries,
   embedded in components/compcode). Past 0xfe → cursor slot.
5. The cursor advances only when an item lands on it.

Weapon classes come from the record's `+0xc0` / `+0xc4` wclass codes
mapped through `{code, n}` pairs at `0x72ef68` (bow 1hs 1ht stf 2hs 2ht
xbw 1js 1jt 1ss 1st ht1 ht2 → 1..13).

The step-4 quirk is visible in the result: claws (`clw` 43, `skr` 44,
`ktr` 45, `axf` 46) land ahead of the battle bows/crossbows (47..50)
that precede them in weapons.txt.

## Verification

Equipped items in 19 real 1.14d saves (items list, location 1 = body,
slot 1 head / 4 right hand / 5 left hand) paired with their appearance
bytes: every pair matches (`tests/test_compcode.cpp`), e.g. 4 hax,
11 bwn, 43 clw, 51 ob1, 53 ob4, 57 cap, 61 ghm, 79 buc, 82 tow, 83 bhm,
89/90/91 ba1/ba3/ba5, 93/94 pa3/pa5. Circlets are 0xff (not drawn).

Two things tripped the first model: weapons.txt spells the column
`alternateGfx` (armor.txt `alternategfx`), and the "Expansion" separator
rows are not items.

## Weapon class: `FUN_00504af0`

Picks the composite's weapon class from the RH / LH / SH bytes. Ids
index D2Comp's token list (`"" hth 1ht 2ht 1hs 2hs bow xbw stf 1js 1jt
1ss 1st ht1 ht2`, after the layer names at 0x72e0f0); an item's wclass
token maps to the same id through `{code, n}` pairs at 0x72ef68 and
0x72ef30[n] (unmatched → 1, hth).

- Both hands used → both sides take their two-handed class; a lone RH
  weapon (LH and SH empty) also takes its two-handed class (lone giant
  sword → 2hs).
- Claw classes (13/14) only count for the Assassin; an armour-typed
  item in a hand counts as nothing.
- Pairs: 1hs+1hs → 1ss, 1hs+1ht → 1js, 1ht+1hs → 1st, 1ht+1ht → 1jt,
  2hs with 1ht / 1hs / 2hs → 1ss, bow+bow / xbw+xbw kept, staff wins,
  two claws → ht1 (not ht2). Anything else → 0, and the caller
  (`FUN_00504040`) falls back to class 7, mode TN, hth.
- Composite name tables sit together in game.exe: classes (count 25 at
  0x72e04c: AM SO NE PA BA DZ AI RO RH RH O1..), modes (count 20: DT NU
  WL RN GH TN TW A1 ..), layers (HD TR LG RA LA RH LH SH S1..S8), armour
  tiers (lit med hvy), weapon classes (count 15).

## The look from what's worn

The header's 16 bytes follow from the worn items (body locations 1 head,
3 torso, 4 right hand, 5 left hand), each through its armor / weapons /
misc.txt `component` column (0 HD, 1 TR, 5 RH, 6 LH, 7 SH, 10 S3 for a
necromancer's head, 16 none):
- a helm, weapon, shield or head: its graphic's index in this table
  (alternategfx, else code); a one-hand weapon (component 5) in the left
  hand goes on LH; bows are component 6 (LH) wherever they're held;
  circlets aren't drawn (0xff);
- body armour: TR LG RA LA S1 S2 = 1 (lit) + its Torso, Legs, rArm, lArm,
  rSPad, lSPad tier (0..2);
- nothing worn: TR LG RA LA S1 S2 lit, the rest 0xff.

Checked against all 20 real saves (test_d2s). d2d rebuilds the look each
tick (`GameData::look_of`), so equipping changes it.
### Tints (+0x98)

Each drawn layer's tint = ((its armor / weapons.txt Transform x 32) + 1 +
colour) & 0xff, colour the Colors.txt row (0 whit .. 20 bwht); 0xff for
no colour or Transform 0, 3 or 4. The colour (FUN_0062c100):
- unique: UniqueItems chrtransform (by row without separators);
- set item: SetItems chrtransform;
- magic, rare: the first suffix with a transformcolor (item data
  +0x3e..0x42), else the first prefix (+0x38..0x3c), else the class
  automod (+0x36, AutoMagic). Affix ids index the raw MagicPrefix /
  MagicSuffix rows, separators included (the save's ids).
- anything else, crafted included: if it's socketed (flag 0x800) and can
  hold sockets, its first socketed item's gems.txt `transform` when that
  item is a gem (ItemTypes 20 and its children: not runes or jewels), else
  the automod. The saves' 25 worn runeword items (runes in, 0xff) agree;
  no worn item holds a gem.

Transform 8 wraps past the byte (uld white → 0x01, uth dark purple →
0x13), on purpose: the reader, `FUN_005038d0`, takes byte − 1, colour =
the low 5 bits, and sends Transform 0 to Transform 8's colormaps (bugs.md
#12, not a bug). `FUN_005066c0` (character select) stores the tints as
byte − 1 the same way. All 20 real saves' tints match (test_d2s,
`compcode::tints`).

The automod id is 1-based into AutoMagic (FUN_00633ee0 looks every
affix up 1-based in one table: MagicSuffix, MagicPrefix, AutoMagic rows,
built at ItemTbls.cpp). The saves' class items show it: paladin shields
hold 26/27, Prismatic/Chromatic (all resists) 1-based but "Sharp" (attack
rating) 0-based; orbs 13 and 21 land on life and mana, a necromancer head
33 on poison. No worn item's colour comes from its automod in the saves.

Drawing (FUN_00600c20): the layer's pixels go through colormap
0x8adbb8 + (Transform x 105 + colour) x 256 before the palette: the files
`Data\Global\Items\Palette\<name>.dat`, 21 x 256 each, by Transform
1 grey, 2 grey2, 3 gold, 4 brown, 5 greybrown, 6 invgrey, 7 invgrey2,
8 invgreybrown. d2d remaps a layer's pixels once when it's decoded
(`PlayerAnim::layer`); the look carries the tints as bytes 16..31.

