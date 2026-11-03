<!--
AGENTS.md §6. One claim per pull request, or a batch from one subsystem
(AGENTS.md §5), one claim or code change per commit: then fill in
"Claims", a row each. Fill in each
section and delete the ones that don't apply. Comments like this one don't show on GitHub.
-->

## What changes

<!-- One or two sentences a player could follow: what d2d did before, what it
does now, and what game.exe does. -->

## Evidence from game.exe 1.14d

<!-- Strongest first: assert strings and source names, then code that reads
the field, then the data it uses. One row per address. -->

| Address | What it shows |
|---|---|
| `00000000` | |

## Claims (a batch only)

<!-- One row per commit: one claim or one code change (with its test) each;
a reviewer can drop a row and merge the rest. -->

| Commit | Claim or change | Evidence | Re-check |
|---|---|---|---|
| | | `00000000` | |

## How to re-check

<!-- Commands a reviewer can paste as-is on their own copy. For an oracle,
the exact tools/emu call and its inputs, not "run the oracle". -->

```sh
python3 tools/pr_guard.py
cmake --build build && ctest --test-dir build --output-on-failure
```

## Screenshots

<!-- Only for something you can see. game.exe and d2d at the same spot and
seed. Drag each PNG into this box on github.com (never commit it) and put
the link it gives in src. width="400" keeps the pair side by side. -->

| game.exe | d2d |
|:---:|:---:|
| <img src="" width="400" alt="game.exe"> | <img src="" width="400" alt="d2d"> |

## Checklist

- [ ] A test in `tests/` (fix or port)
- [ ] The finding in `docs/research/re/`, its `todo.md` / `unverified.md` row moved (trace)
- [ ] `python3 tools/pr_guard.py` passes

---

Agent: <!-- its name, or none -->
<!-- Reported-by: / Traced-by: when someone else did that part -->
