---
name: reverse-engineering
description: How to reverse-engineer game.exe (D2 1.14d) in this repo — Ghidra headless scripts to find and read functions, the unicorn emulator (tools/emu) to run game.exe's own code as an oracle and diff it against our C++ port, and where findings go. Use for any "RE", "decompile", "what does FUN_xxx do", "match game.exe", "bit exact", or "verify against the original" task.
---

# Reverse-engineering game.exe

## 1. Find and read code (Ghidra, headless)

Project: `tools/ghidra/project` (make it with `tools/ghidra/import.sh`).
Run one analyzeHeadless at a time (the project lock); chain several
`-postScript` calls in one run instead:

```
H=$(command -v ghidra-analyzeHeadless || find /opt/homebrew/Cellar/ghidra -name analyzeHeadless -type f | head -1)
$H tools/ghidra/project D2Decomp -process game.exe -noanalysis -readOnly -scriptPath tools/ghidra/scripts \
  -postScript DecompileAt out.c 00675360 006750f0 \
  -postScript Disasm 00612280 006122f0 out.txt \
  -postScript XrefsTo 006e681c xrefs.txt
```

- `DecompileAt <out.c> <addr>...` works on function starts only.
- `Disasm <start> <end> <out>`: the decompiler drops fastcall ECX/EDX args
  and `RET n` counts, so read the asm for calling conventions.
  `CallArgs.java` recovers them at call sites.
- `XrefsTo <addr> <out>` / `XrefsRange` show who calls or indexes something.
  Strings live in `docs/research/re/game-strings.tsv` and functions in
  `game-functions.tsv`, so grep those first.
- Source file names in asserts (`.\DRLG\Drlg.cpp`) show which module a
  function belongs to.

## 2. Prove it (tools/emu, unicorn)

Don't claim "matches game.exe" from reading alone. Run game.exe's own code
on the same input and diff:

```
cd tools/emu && uv sync
uv run python -c "import emu; e = emu.Emu(); print(e.call(0x45c3e0, ecx=seed_ptr, edx=100))"
```

- A pure function: `Emu()` then `call(addr, *stack, ecx=, edx=)`.
- Anything that needs data tables: `emu.serve_files(e); e.crt_init();
  e.call(0x619300, 0, 1, 1)` loads every excel table plus the preset/LvlSub
  DS1s from the MPQs. `drlg.boot()` does exactly this.
- An unstubbed import stops the run with its name and a stack. Add a
  `w_<Name>` returning `(eax, stdcall argc)` to `emu.py`, or `e.hook(addr,
  fn, nstack)` to cut off a subsystem you don't care about.
- Pair every oracle with a small C++ dump tool (like `tools/drlg-dump`)
  that prints the same text, then `diff`. Sweep many seeds or inputs,
  because one match proves little.

Existing oracle: `tools/emu/drlg.py` for level generation (act, level
records, outdoor cell grids, room list and room seeds).

## 3. Write it down

- Findings go in `docs/research/re/<subsystem>.md`: addresses, struct
  offsets, tables, and the algorithm in prose. Raw decompiled `.c` stays
  out of git.
- The port goes in `components/<name>` with a test in `tests/`. Leave
  `ponytail:` notes on known gaps.
- Python tools are uv projects (`pyproject.toml` + `.venv`), never system
  pip.
