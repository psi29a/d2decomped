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
| `FUN_004203f0` | `slink_next_link` | `ECX` link, offset (`RET 4`): the next link: a negative next `+4` complemented (the terminator); else next + offset, and for a negative offset the link's own offset, link − its previous link's next | no counterpart |
| `FUN_00420520` | `slist_link_or_terminator` | `ECX` list, link (`RET 4`): the link, or the terminator `+4` when it is 0 | no counterpart |
| `FUN_004205f0` | `slist_next_node` | `ECX` list, link (`RET 4`; 0 for the terminator): the link's next node `+4` when positive (signed), else 0, so the first node for 0 and 0 past the last | no counterpart |
| `FUN_00420650` | `slist_unlink` | `ECX` list, link (`RET 4`; 0 for the terminator): with a non-zero previous link, the next link (as `slink_next_link` with the link's own offset) takes the previous link, the previous link's next takes the link's next, and the link becomes 0, 0; nothing when its previous link is 0 | no counterpart |
| `FUN_004206e0` | `slist_raw_next` | `ECX` list, link (`RET 4`): the link's next `+4`, or the list's first node `+8` when the link is 0, as stored (negative at the end) | no counterpart |
