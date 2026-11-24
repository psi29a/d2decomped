# SOURCE: Storm intrusive list helpers — game.exe 1.14d

`SOURCE` rows (Storm, statically linked) of `build/gaps/functions.tsv`
that were `untouched` on 2026-11-24, with a setter next to the cited
`storm_get_7792f4` of [source-frontier.md](source-frontier.md).
`tools/gaps.py` counts no callers for them but one for `FUN_00420750`
(`FUN_00420790`) and one for `FUN_0041c770`; rows ending in `_2` are a
second copy of the same code. A list keeps its link offset at `+0` and its terminator
link at `+4`; a link keeps the previous link at `+0` and the next node at
`+4`, or the terminator's address complemented (negative) after the last
node; a list's terminator `+8` is its first node. Read from the
disassembly; each row is run by `tools/emu/storm_list.py` (its
`gaps-runs` line) on well-formed lists of 0 to 6 nodes against a model,
with `--mutation-check`. Re-check with `tools/ghidra/fn.py <address>`. The
**d2d** column is as in [game-frontier.md](game-frontier.md); d2d keeps
its lists in standard containers.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_004203a0` | `storm_set_7792f4` | Value (`RET 4`): `DAT_007792f4` = value, returns 1 (the getter is `storm_get_7792f4`) | no counterpart |
