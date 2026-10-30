# Contributing

You don't need to be a programmer. If you use an AI agent (Claude Code,
Codex, Cursor, Copilot, Gemini or any other), clone the repo, open your agent
in it and say:

    Help me contribute to D2Decomp. Follow AGENTS.md.

Your agent will show you what we ask and what we promise, check your setup,
offer a few tasks and walk you through one to a bug report or a pull
request. [AGENTS.md](AGENTS.md) is the same for every agent, and it's short
enough to read yourself first. No agent? This page and AGENTS.md work for
people too.

## Three ways in

| Kind | You need | You give |
|---|---|---|
| **Play and report** | a [release](https://github.com/psi29a/d2decomped/releases) and your own D2 1.14d install | an issue: what you did, what the original game does, `d2d.log`, a screenshot |
| **Trace a claim** | the above + Ghidra and [uv](https://docs.astral.sh/uv/) | a pull request with one claim about game.exe, and the address that proves it |
| **Fix or port** | the above + a build ([README](README.md#build-and-run)) | a pull request with a test, citing where in game.exe the behaviour comes from |

What's left is on the **[gap map](https://psi29a.github.io/d2decomped/)**:
every game.exe function marked untouched, cited, ported or verified, every
listed gap (todo, unverified, `ponytail:` notes, issues), and the
"frontier", untouched code that ported code calls. Build it yourself with
`python3 tools/gaps.py`. Any agent that can read a URL can query its
`gaps.json` and `functions.tsv`.

When nothing's left, the map says `DONE`. What remains then is playing and
reporting where d2d differs from the original.

## Our side

We ask only for the three kinds of work above, and we promise:

- **No other asks:** no donations, no accounts to link, no game files to
  share, nothing to run for anyone else.
- **No hidden instructions.** AGENTS.md and this page are everything we tell
  an agent. Changes to them need a maintainer's review and show in git
  history.
- **You decide what leaves your machine.** Your agent asks before every
  issue, pull request, comment or push. AGENTS.md lists everything the tools
  run, write and connect to. Nothing sends telemetry, and crash reports are
  off unless you turn them on.
- **Your data and credit stay yours.** We never ask for game files, saves or
  keys. Your name (or a pseudonym) stays in git and the credits.
- **Honest review.** Everyone's work gets the same checks, ours included. A
  rejection comes with a reason.

## Your side

- **game.exe 1.14d is the ground truth.** A claim cites an address, a table
  or an assert string that someone else can re-check. "It looks like" is not
  evidence. Other projects can confirm a reading, never replace one.
- **Your own copy.** Trace and test with the Diablo II you own. Never commit
  MPQ data, the installer, `game.exe`, the Ghidra project, decompiled C or
  `game-strings.tsv` ([LEGAL.md](LEGAL.md)).
- **One claim per pull request or issue**, with the commands that re-check
  it. Small and checkable beats big and vague.
- **Say which agent helped** (`Agent: <name>` or `Agent: none`). Any agent is
  welcome, and the checks are the same either way. Read what it wrote before
  you send it: it's your name on it.

## How we check

The same for every pull request, whoever or whatever wrote it:

- `tools/pr_guard.py` runs on every pull request. It fails addresses that
  can't be in game.exe (a `FUN_` inside another function's body is one)
  and binary files. It also stops a change that touches CI, downloads,
  networking, process launches or the agent instructions until a
  maintainer has read it and added the `security-reviewed` label.
  Run it yourself first: `python3 tools/pr_guard.py`.
- A reviewer re-runs your evidence on their own copy before merging.
- Changes to CI, the build, the tools, the launcher and the agent
  instructions need a maintainer's review (`.github/CODEOWNERS`).

## Credit

- **Code and traces:** you're the git author of your commits, even when an
  agent typed them. You chose the task, checked the evidence and answered the
  review.
- **Reports and playtests:** the fix carries `Reported-by: Your Name`.
- **A trace someone else ports:** the port carries `Traced-by: Your Name`.

The gap map's credits count all three.

Questions: the [Discord](https://discord.gg/EJxctR2hqn).
