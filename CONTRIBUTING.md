# Contributing

You don't need to be a programmer. If you have Claude Code (a Claude
subscription is enough), clone the repo, open Claude Code in it and paste:

    Help me contribute to D2Decomp. Use the contribute skill.

The skill asks what you'd like to do, checks your setup, picks a task that
fits and walks you through it to a bug report or a pull request.

## Three ways in

| Lane | You need | You give |
|---|---|---|
| **Play and report** | a [release](https://github.com/psi29a/d2decomped/releases) and your own D2 1.14d install | an issue: what you did, what the original game does, `d2d.log`, a screenshot |
| **Trace a claim** | the above + Ghidra and [uv](https://docs.astral.sh/uv/) | a PR that moves one row of [unverified.md](docs/research/re/unverified.md) to "Traced", with the game.exe address as evidence |
| **Fix or port** | the above + a build ([README](README.md#build-and-run)) | a PR with a test, citing where in game.exe the behaviour comes from |

Open tasks are on the issue tracker under
[`good first issue`](https://github.com/psi29a/d2decomped/labels/good%20first%20issue).

## Rules

- **game.exe 1.14d is the ground truth.** A claim cites an address, a table
  or an assert string that someone else can re-check. "It looks like" is not
  evidence, and other projects only confirm a reading, they don't replace one.
- **Your own copy.** Trace and test with the Diablo II you own. Never commit
  MPQ data, the installer, `game.exe`, the Ghidra project or decompiled C.
  The function, import and string lists in `docs/research/re/game-*.tsv`
  are fine: they're facts about the binary, not its code.
- **One claim per PR or issue.** Small and checkable beats big and vague.
- **Say when Claude did the work.** That's welcome; just read what it wrote
  before you send it.

Questions: the [Discord](https://discord.gg/EJxctR2hqn).
