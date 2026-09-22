# Char-create master table — game.exe 1.14d

Source binary: `game.exe` (1.14d monolith). Master menu table for the
character-creation screen sits at **`0x00708e00`** conceptually (same
kind-dispatch system as [[frontend-menu-table]]), but the char-create
records themselves live in a contiguous block at **`0x0070ae40..0x0070b500`**
— 33 records × 48 bytes each.

Record layout is identical to `frontend-menu-table.md` (kind, x, y, w, h,
flags, tbl_string_id, handle_slot, on_click, sub_table, pad, pad).

Kinds observed in this block:
- `2` — sprite / static background
- `3` — paired-anim / class silhouette (uses `sub_table` for anim slots)
- `4` — hitbox (invisible click zone)
- `6` — button (chrome + label + click handler)

## Records (decoded from a raw byte dump)

| Addr       | Kind | (x,y,w,h)              | tbl_id | handle       | on_click     | sub_table    | Purpose |
|------------|------|------------------------|--------|--------------|--------------|--------------|---------|
| 0x70ae40   | 2    | (0, 599, 800, 600)     |        | `0x00779724` |              |              | Char-create BG (charactercreationscreenEXP.dc6) — y=599 = bottom-Y BG convention |
| 0x70ae70   | 3    | (345, 470, 110, 127)   |        | `0x00779728` |              |              | Fire — lower copy (fire.dc6) |
| 0x70aea0   | 3    | (345, 454, 110, 127)   |        | `0x00779728` |              |              | Fire — upper copy |
| 0x70aed0   | 3    | (195, 341, 88, 184)    |        |              | `0x00433bf0` | `0x00708aa0` | Class silhouette (classic layout) |
| 0x70af00   | 3    | (301, 333, 88, 184)    |        |              | `0x00433bf0` | `0x00708af0` | Class silhouette |
| **0x70af30** | 3  | **(400, 330, 88, 184)** |       |              | `0x00433bf0` | `0x00708b40` | **BARBARIAN** (matches our kClassPos) |
| 0x70af60   | 3    | (521, 344, 88, 184)    |        |              | `0x00433bf0` | `0x00708b90` | Class silhouette |
| 0x70af90   | 3    | (610, 359, 88, 184)    |        |              | `0x00433bf0` | `0x00708be0` | Class silhouette |
| **0x70afc0** | 3  | **(217, 360, 88, 184)** |       |              | `0x00433bf0` | `0x00708af0` | **NECROMANCER** |
| **0x70aff0** | 3  | **(626, 353, 88, 184)** |       |              | `0x00433bf0` | `0x00708b90` | **SORCERESS** |
| **0x70b020** | 3  | **(521, 339, 88, 184)** |       |              | `0x00433bf0` | `0x00708be0` | **PALADIN** |
| **0x70b050** | 3  | **(100, 337, 88, 184)** |       |              | `0x00433bf0` | `0x00708aa0` | **AMAZON** |
| 0x70b080   | 4    | (339, 581, 100, 32)    |        |              |              |              | Hitbox — HARDCORE label click zone |
| **0x70b0b0** | 6  | **(319, 560, 15, 16)**  |       | `0x007797c0` (clickbox.dc6) | `0x00430730` |     | **HARDCORE checkbox — sets bit 0x04** |
| 0x70b0e0   | 4    | (339, 561, 200, 32)    |        |              |              |              | Hitbox — LADDER label click zone |
| 0x70b110   | 6    | (319, 540, 15, 16)     |        | clickbox     | `0x00430750` |              | LADDER checkbox — sets bit 0x40 |
| 0x70b140   | 4    | (339, 581, 200, 32)    |        |              |              |              | Alt hitbox |
| 0x70b170   | 6    | (319, 560, 15, 16)     |        | clickbox     | `0x00430750` |              | LADDER checkbox — alt position |
| 0x70b1a0   | 4    | (339, 601, 200, 32)    |        |              |              |              | Alt hitbox |
| 0x70b1d0   | 6    | (319, 580, 15, 16)     |        | clickbox     | `0x00430750` |              | LADDER checkbox — alt position |
| 0x70b200   | 4    | (0, 180, 800, 100)     |        |              |              |              | Wide hitbox (upper class-select zone?) |
| 0x70b230   | 4    | (250, 210, 300, 100)   |        |              |              |              | Wide hitbox |
| 0x70b260   | 4    | (210, 610, 430, 120)   |        |              |              |              | Wide hitbox (bottom-of-screen no-op?) |
| **0x70b290** | 2  | **(319, 519, 169, 26)** |       | `0x007797bc` (textbox.dc6) |     |         | **Name-entry field chrome** |
| 0x70b2c0   | 4    | (321, 512, 200, 32)    |        |              |              |              | Name-field hitbox |
| 0x70b2f0   | 4    | (339, 561, 100, 32)    |        |              |              |              | Alt HC label hitbox |
| 0x70b320   | 6    | (319, 540, 15, 16)     |        | clickbox     | `0x00430730` |              | HARDCORE — alt position |
| 0x70b350   | 1    | (318, 510, 157, 16)    | 8, 4   |              | `0x004369f0` |              | Kind 1 marker — name-field enter handler |
| 0x70b380   | 2    | (268, 350, 264, 176)   |        | `0x00779780` |              |              | Side panel (class description bg?) |
| 0x70b3b0   | 6    | (281, 337, 96, 32)     | 0x13ef |              | `0x00436590` |              | Popup button — EXIT / CANCEL for modal |
| 0x70b3e0   | 6    | (421, 337, 96, 32)     | 0x13ee | `0x007797b0` | `0x00436360` |              | Popup button — OK for modal |
| 0x70b410   | 4    | (268, 320, 264, 120)   |        |              |              |              | Popup hitbox |
| **0x70b440** | 3  | **(232, 364, 88, 184)** |       |              | `0x00433bf0` | `0x00708a00` | **ASSASSIN** |
| **0x70b470** | 3  | **(720, 370, 88, 184)** |       |              | `0x00433bf0` | `0x00708a50` | **DRUID** |
| 0x70b4a0   | 4    | (339, 561, 200, 32)    |        |              |              |              | Alt LADDER hitbox |
| 0x70b4d0   | 6    | (319, 540, 15, 16)     |        | clickbox     | `0x00430770` |              | EXPANSION checkbox — sets bit 0x20 (gated by unlock check) |

## Checkbox click handlers

Decoded by disassembling the raw bytes at each `on_click` address —
Ghidra didn't auto-recognize them as functions because they're inline
handlers in a code gap (0x00430700..0x00430790 between the surrounding
`FUN_004306c0` and `FUN_004307a0`).

All three follow the same shape:

```asm
mov eax, [0x7795d4]                      ; load per-character-in-creation struct pointer
or  word ptr [eax+0x1ef], <mask>         ; set flag bit(s) at struct+0x1ef
mov eax, 1
ret 4                                    ; stdcall, 1 arg
```

The `[0x7795d4]` global holds a pointer to the "character being created"
struct. Offset `+0x1ef` is the `Character Status` byte in the D2S save
file format, so:

| Handler      | Mask   | Bit | D2S Character-Status meaning |
|--------------|--------|-----|------------------------------|
| `0x00430730` | `0x04` | 2   | **Hardcore** |
| `0x00430750` | `0x40` | 6   | **Ladder** |
| `0x00430770` | `0x20` | 5   | **Expansion** (guarded by a `FUN_?` check first — likely the LoD-installed check) |

Multiple records with the same handler at different `(x, y)` are
alternative-layout versions of the same checkbox that the drawer
enables/disables based on screen mode (classic vs LoD, char-create vs
char-select, etc.). LoD 1.14d uses:
- **HARDCORE** at (319, 560) — record `0x70b0b0`
- LADDER and EXPANSION checkboxes exist in the table but are not
  rendered on the standard char-create screen (they belong to the
  Battle.net char-create path, which we don't have plumbed).

## Sub-table for classes — `0x00708a00`

Kind 3 class records point `sub_table` into `0x00708aa0..0x00708be0`
(and `0x00708a00..0x00708a50` for the LoD-added Assassin/Druid). This
is a 16-byte-per-record class table; each row holds the 5 animation
slot pointers (nu1, nu2, fw, nu3, bw). Documented in
[[class-table]].

## OK / EXIT on the char-create screen

The buttons at 0x70b3b0 / 0x70b3e0 (x=281 and x=421, y=337) are the
**modal-popup** OK/EXIT — probably the "Are you sure?" that appears
when clicking EXIT with unsaved input. The main char-create OK/EXIT
buttons at the bottom of the screen are **NOT** in this table — likely
either hardcoded, sourced from a different table, or belong to the
generic "bottom-bar buttons" pattern shared with char-select. Follow-up:
locate them (they're what our code positions at (627, 572) and (33, 572)
by eye — currently unverified against RE).

## How this table was captured

`tools/ghidra/scripts/DumpBytes.java` — a new Ghidra headless script
that dumps a virtual-address range as one line per record (both hex
bytes and 32-bit LE dwords), which makes 48-byte struct arrays trivial
to slice. Invoked as:

```
analyzeHeadless project D2Decomp -noanalysis -process game.exe \
  -postScript DumpBytes.java 0x70ae40 0x70b500 0x30 out.txt
```

The three checkbox handlers were disassembled by hand from the raw
byte dump because Ghidra hadn't identified them as function entries
(they're small stubs in a code gap).
