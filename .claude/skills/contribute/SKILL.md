---
name: contribute
description: Onboard a new contributor to D2Decomp, often someone with Claude but little dev experience. Ask which lane they want (play and report, trace a claim, fix or port), check their setup, pick one small task, do it with them and finish with an issue or a pull request. Use when someone says "help me contribute", "where do I start", "I want to help", or "contribute skill".
---

# Onboard a contributor

The person driving you may never have built C++ or opened Ghidra. Keep each
step to one command or one decision, say what it does before you run it, and
stop when a step fails instead of working around it.

## 1. Pick a lane

Ask once, with these options (CONTRIBUTING.md has the table):

- **Play and report:** no build. Best for a first contribution.
- **Trace a claim:** check one thing d2d does against game.exe.
- **Fix or port:** change code. Suggest it only if they've built software before.

## 2. Check the setup

Every lane needs their own Diablo II 1.14d install. Never download, share
or commit game files.

- Play and report: the latest release from `gh release list` (or the
  Releases page) and the launcher pointed at their install.
- Trace a claim: also Ghidra (`brew install ghidra`, or ghidra-sre.org) and
  uv. Then `tools/ghidra/import.sh /path/to/game.exe` (takes a few
  minutes) and `cd tools/emu && uv sync`.
- Fix or port: also the build in README.md "Build and run".

## 3. Pick one task

- Play and report: an act they like, against the original game if they can
  run it. A behaviour gap beats a crash hunt; both count.
- Trace a claim: one row of `docs/research/re/unverified.md`, or an open
  issue labelled `good first issue` (`gh issue list -l "good first issue"`).
  Suggest two or three and let them choose.
- Fix or port: a `good first issue`, or a row they traced.

## 4. Do it

- Play and report: help them write the issue: steps, what d2d did, what
  the original does, d2d version, `d2d.log` (location in apps/d2d/README.md),
  a screenshot. One problem per issue.
- Trace a claim: follow the `reverse-engineering` skill. Find the function
  (grep `game-functions.tsv` / `game-strings.tsv` first), read it, and prove
  it with tools/emu where the code can run. Write what you found into the
  subsystem doc under `docs/research/re/`, and move the row to "Traced" (or
  note that game.exe disagrees, which is a better find: log it in the doc and
  open an issue).
- Fix or port: the change plus a test in `tests/`, citing the address.

Evidence is an address, xref, table or assert string someone can re-check.
Behaviour alone isn't. If you aren't sure, say so in the PR; never guess.

## 5. Send it

- Branch, commit with a short subject line, open the PR with `gh pr create`.
  The body says what was claimed, the evidence, and how to re-check it.
- Tell them CI and CodeRabbit will comment, and that answering a review is
  part of the job.
- Point them at the Discord (README "Collaborate") for the next task.
