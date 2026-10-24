# Research

Notes, format specs, RE walkthroughs. Cite sources. No pasted code from
other projects — read, understand, describe, then write our own.

## Layout

- `formats/` — one file per on-disk format (MPQ, DC6, DCC, DS1, DT1, COF,
  TBL, TXT, PL2, SPK, SMK/BIK video).
- `re/` — findings from game.exe 1.14d (the old DLLs are linked into it),
  one file per subsystem (`re/monster-ai.md`, `re/drlg.md`, …), plus
  `bugs.md` (the original's bugs), `deviations.md` (d2d's intended
  differences), `stubs.md` (dead code) and `unverified.md` (borrowed,
  untraced facts).

## Prior art (local, in `../../..`)

| Project        | Lang | Value                                              |
|----------------|------|----------------------------------------------------|
| OpenD2         | C    | Closest to engine layout; format parsers to study. |
| OpenDiablo2    | Go   | High-level design docs, asset structure.           |
| AbyssEngine    | C    | Later attempt; Lua-driven game logic.              |

## External refs to seed

- Phrozen Keep — the modding wiki (file formats, .txt columns).
- d2mods / D2Mods GitHub org.
- Necrolis' RE writeups.
- Sanki's reverse-engineering docs (`d2mods`).

