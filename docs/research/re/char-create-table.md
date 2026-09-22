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

## OK / EXIT on the char-create screen — RE'd

The bottom-row buttons are NOT in the char-create table proper; they
sit **immediately before it**, as the last two records of the
char-select master table (which runs `0x70ac00..0x70ae40` and shares
its bottom-bar buttons with the char-create screen):

| Addr       | Kind | (x, y, w, h)       | flags | tbl_id | handle       | on_click     | Purpose      |
|------------|------|--------------------|-------|--------|--------------|--------------|--------------|
| **0x70ade0** | 6  | **(33, 572, 128, 35)** | 0x1b | 0x13ed | `0x00779744` (MediumSelButtonBlank) | `0x00430c30` | **EXIT** |
| **0x70ae10** | 6  | **(627, 572, 128, 35)** | 0x0d | 0x13ee | `0x00779744` | `0x004369f0` | **OK** |

Both use `MediumSelButtonBlank.dc6` chrome, TBL ids `0x13ed`/`0x13ee`,
and match our current placement exactly — so those buttons were placed
correctly by eye and are now RE-confirmed.

The `flags` byte differs (0x1b on EXIT, 0x0d on OK) — likely render
hints (highlight/disabled state; the main-menu EXIT record at
`0x708fe0` also uses 0x1b, suggesting a "quit-family" marker).

The two records at `0x70b3b0` / `0x70b3e0` (x=281/421, y=337) that ARE
inside the char-create table proper are the **modal-popup** OK/CANCEL
that appears when the user commits or aborts an in-progress creation
("Are you sure you want to create <name> the <class>?"), separate from
the always-visible bottom-bar buttons above.

## Preceding table (char-select, 0x70ac00..0x70ae40)

The char-select records are outside the scope of this doc but were
captured en route to finding the bottom buttons. Notable rows:

| Addr       | Kind | (x, y, w, h)       | tbl_id  | handle   | Purpose (guessed) |
|------------|------|--------------------|---------|----------|--------------------|
| 0x70ac00   | 6    | (233, 528, 168, 60) | 22732  | 0x779730 (TallButtonBlank) | Bottom-row "CONVERT..." button? |
| 0x70ac30   | 6    | (433, 528, 168, 60) | 5272   | 0x779730 | Bottom-row button |
| 0x70ac90/acc0/acf0 | 6 | (264, 297/340/383, 272, 35) | 10018/10017/10016 | 0x779738 (WideButton) | 3 stacked char-list slots |
| 0x70ad50   | 2    | (237, 400, 326, 200) | | 0x77976c | Char-select BG panel |

## Screen state machine — how records get onto the screen

The whole record pool sits at **`DAT_00708d10`**, one contiguous
48-byte-record array. A record's *global index* is `(addr - 0x708d10) / 48`
— so the char-create BG at `0x70ae40` is record `0xB1` (177 decimal), and
the Hardcore checkbox at `0x70b0b0` is record `0xBE` (190).

Two runtime primitives own the pool:

| Address       | Fn                    | Role |
|---------------|-----------------------|------|
| `FUN_0042f430(N)` | `register_record(N)` | Copies record N into the active screen's live list: calls `FUN_004f93c0(&DAT_00708d10 + N * 0x30)`, stores the returned handle at `DAT_00779350[DAT_00779944++]`. Caps at 64 records (`0x3f`). |
| `FUN_0042f480()`  | `teardown_screen()`  | Walks `DAT_00779350[0..DAT_00779944]`, calls `FUN_004f95c0(handle)` (destroy) on each, resets the count to 0. Called at the start of every screen-init. |

`FUN_004f93c0` is the record processor — a per-kind dispatch:

| Kind | Handler       | Purpose                       |
|------|---------------|-------------------------------|
| 1    | `FUN_005001d0` | Kind-1 marker (name-field enter handler etc.) |
| 2    | `FUN_004fd6c0` | Sprite / static background     |
| 3    | `FUN_00500850` | Paired-anim (class silhouette, fire) |
| 4    | `FUN_004fc7a0` | Hitbox                         |
| 5    | `FUN_005084f0` | (unknown — sub-menu marker?)    |
| 6    | `FUN_00501290` | Button (chrome + label + click) |
| 7    | `FUN_005075e0` | (unknown)                       |
| 8    | `FUN_004fd9d0` | Kind-8 marker (hotspot?)        |
| 9    | `FUN_00506b70` | (unknown)                       |
| 10   | `FUN_00507e50` | (unknown)                       |
| 12   | `FUN_00507ca0` | (unknown)                       |
| 13   | `FUN_004fd770` | (unknown)                       |

So there is no "drawer that iterates the master table". Each kind's
handler registers itself with the game's global draw/tick lists at
create-time, and per-frame drawing runs off those subsystem lists —
not off `DAT_00779350` directly.

## Char-create init — which records this screen actually shows

`FUN_00435580` (called by `FUN_00435e10`, the screen dispatch) is the
init. It first calls `FUN_0042f480` (tear down previous screen), then
zeros the character-status flags word:

```c
*(undefined2 *)(DAT_007795d4 + 0x1ef) = 0;
```

...then makes many `register_record(N)` calls (captured from raw x86
asm; the decompiler hid the fastcall ECX args). The exact indices for
LoD 1.14d:

```
0xB1 charcreate_bg (0x70ae40)
0xC5 wide hitbox (0x70b200)
0xC7 wide hitbox (0x70b260)
0xAF EXIT button (0x70ade0) ← from the preceding table
0xC8 name-field textbox (0x70b290)
0xCC name-field enter marker (0x70b350)
0xD3 ladder hitbox (0x70b4a0)   ┐
0xD4 EXPANSION checkbox y=540  ├ inside `if (DAT_007795ec == 1)` branch
0xD5 (past table, 0x70b500)    ┘   — only registered when expansion is installed
0xBE HARDCORE checkbox y=560   ← THE primary HC render
0xBD HARDCORE hitbox           ← its label click zone
0xC3 (hitbox 0x70b1a0)
0xCA (hitbox 0x70b2f0)
0xCB HARDCORE checkbox y=540   ← alt / modal-dialog variant (also drawn)
0xC1 (hitbox 0x70b140)
0xBF (hitbox 0x70b0e0)
0xB0 OK button (0x70ae10)      ← from the preceding table
```

Alt-checkbox mystery resolved: **BOTH** HC records (0xBE at y=560 and
0xCB at y=540) are registered on the LoD char-create path. The y=560
record is the canonical rendered position (matches D2's reference
screen). The y=540 one is likely for the "confirm creation" modal
overlay that opens on OK-click — the char-create table's records at
0x70b380..0x70b410 look like modal-panel content, and 0xCB sits in
that same cluster.

Runtime flags used by init:

| Global         | Meaning                                        |
|----------------|------------------------------------------------|
| `DAT_007795d4` | Pointer to "character being created" struct    |
| `+0x1ef` byte  | D2S Character Status: bit 2=HC, 5=Expansion, 6=Ladder |
| `DAT_007795ec` | 1 = expansion (LoD) is installed               |
| `DAT_00779da4` | Non-zero = some LoD sub-mode toggle (guards the alt-load path) |
| `FUN_00408f20()` | Returns non-zero when expansion mode is active |

## How this table was captured

Three new Ghidra headless scripts under `tools/ghidra/scripts/`:

- **`DumpBytes.java`** — dumps a virtual-address range as one line per
  record (hex bytes + 32-bit LE dwords side-by-side). Ideal for 48-byte
  struct-array tables like this one:
  ```
  analyzeHeadless project D2Decomp -noanalysis -process game.exe \
    -postScript DumpBytes.java 0x70ae40 0x70b500 0x30 out.txt
  ```
- **`FindPattern.java`** — scans every loaded memory block for a byte
  pattern with `??` wildcards. Used to sweep for records with a specific
  `(y, w)` shape (e.g. `06 00 00 00 ?? ?? ?? ?? 3c 02 00 00` = kind=6
  button at y=572).
- **`XrefsTo.java`** — lists every reference to a target address, with
  the containing function. Used to walk from `FUN_004326f0` (asset
  loader) → `FUN_00435580` (screen init) → `FUN_00435e10` (dispatch).
- **`Disasm.java`** — raw x86 disassembly for a range. Needed to read
  the fastcall `ECX` args to `FUN_0042f430` which the decompiler drops.

The three checkbox handlers (`0x430730`, `0x430750`, `0x430770`) were
disassembled by hand from the raw byte dump because Ghidra hadn't
identified them as function entries — they're small stubs in a code
gap between `FUN_004306c0` and `FUN_004307a0`.
