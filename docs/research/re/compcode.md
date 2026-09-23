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
