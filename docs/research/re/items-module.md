# ITEMS module trace inventory

The 133 cited or untouched function entries in the local ITEMS inventory at
`1265c32d6903e856257733878bb04d9085f9af30` have now been read against the
local stock 1.14d decompilation, with assembly checks for ambiguous register
arguments, return values and empty helpers. This completes static tracing
of that inventory; it does not establish complete porting or runtime parity.
Module assignment follows `game-srcfiles.tsv` and the gap map's inference
between matching source-file assertions, not a claim about every item-related
function elsewhere in the executable.

| Trace document | Previously cited or untouched entries covered |
|---|---:|
| [Creation and lifecycle](items-lifecycle.md) | 9 |
| [Early server item modes](items-mode-early.md) | 26 |
| [Later server item modes](items-mode-late.md) | 27 |
| [Common item helpers](items-common-trace.md) | 71 |

Each selected entry has a descriptive row and a unique name in `names.tsv`, alongside
the earlier [item traces](items.md), [save bitstream trace](d2s-items.md),
[common item rules](d2common-items.md), and the other citations already in
the inventory. Numeric names preserve fields whose semantic table-column
meaning was not established. Assert exits, undefined upper return bits,
stock empty functions and missing bounds checks are recorded explicitly.

The selection baseline contains 308 ITEMS entries: 117 verified, 58 ported,
21 cited and 112 untouched. That local branch predates the consolidation of
earlier research on upstream `83fc7ba1181f548aa65250fa0b2074a3896902de`,
which already retains 112 of the selected entries' names and the earlier
[ItemMode inventory](itemmode.md). This pass preserves those symbols,
adds the remaining 21 names and corrects the socket-stat sum's parent-mode
condition in that inventory. It does not claim all these traces were absent
from current upstream.

The regenerated inventory on that upstream base plus this pass contains
132 verified, 56 ported and 120 cited raw entries, with no untouched ITEMS
entries. Twenty entries are excluded as stock stubs/orphans; counted totals
are 131 verified, 56 ported and 101 cited. Four lifecycle entry points and
their type-0x35 forwarding helper have bounded original-code observations
in `tools/emu/item_lifecycle.py`. The other selected traces remain static;
names do not promote them to ported or verified. The map's Done percentage
still means ported/verified, not statically read.

No production behavior changed. The oracle's hooks and 344 passing cases
are documented in [the lifecycle trace](items-lifecycle.md), which also
pins executable and cached-evidence hashes. The cases do not exercise a
live game, real event queues, inventory replication, round-trip saves or
all allocation failures. Those remain separate verification work.
