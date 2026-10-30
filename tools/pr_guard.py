#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The same checks on every pull request, whoever (or whatever agent) wrote it.

Usage: tools/pr_guard.py [BASE [HEAD]]   (default origin/main HEAD; reads the diff BASE...HEAD)

  FAIL  a FUN_/LAB_ address outside game.exe's code, or any address outside
        its image: evidence that can't be true
  FAIL  a new binary file outside assets/ and docs/screenshots/, or a file that
        must stay local (game-strings.tsv, decompiled C, the Ghidra project,
        MPQs, saves)
  FAIL  risky surface (network, downloads, process launches, workflows, the
        agent instructions, a long byte table) without the `security-reviewed`
        label, which only a maintainer adds after reading it
  FAIL  a FUN_ address inside another function's body in game-functions.tsv:
        made up, or a label (write LAB_ for code inside a function)
  WARN  a FUN_ address in no function's body (Ghidra misses code reached
        through tables: say how you know)

Labels come from PR_LABELS (a JSON list; CI passes the PR's labels).
A passing run proves the evidence is possible, not that it's right: a
reviewer still re-runs it (AGENTS.md "Evidence").
"""
import bisect, json, os, re, subprocess, sys

TEXT = (0x401000, 0x6cb5a1)  # game.exe 1.14d .text, from its PE header
IMAGE = ((0x400000, 0x401000), (0x401000, 0x6cb5a1), (0x6cc000, 0x704a10), (0x705000, 0x994ca4), (0x995000, 0x996988))
BINARY_OK = ("assets/", "docs/screenshots/")
LOCAL_ONLY = re.compile(r"(^|/)game-strings\.tsv$|\.decomp\.c$|\.c\.ghidra$|^docs/research/re/[^/]*\.c$"
                        r"|^tools/ghidra/project/|\.(gpr|rep|mpq|d2s|exe|dll)$", re.IGNORECASE)
REVIEW_LABEL = "security-reviewed"
# Paths that steer CI or every contributor's agent.
RISKY_PATHS = re.compile(r"^(\.github/|AGENTS\.md$|CLAUDE\.md$|\.claude/|CONTRIBUTING\.md$|LEGAL\.md$)")
RISKY_CODE = [  # (what, pattern) on added lines outside .md files
    ("a URL", re.compile(r"https?://")),
    ("a download", re.compile(r"FetchContent_Declare|ExternalProject_Add|\bcurl\b|\bwget\b|Invoke-WebRequest|urlopen")),
    ("networking", re.compile(r"\bsocket\s*\(|WinHttp|InternetOpen|QNetwork|\bconnect\s*\(|\brequests\.|\bhttpx\b"
                              r"|\burllib|http\.client|aiohttp|\bfetch\s*\(|XMLHttpRequest|Net\.WebClient")),
    ("a process launch", re.compile(r"\bsystem\s*\(|\bpopen\s*\(|CreateProcess|ShellExecute|subprocess|os\.system|\bexecv|\beval\s*\(")),
    ("encoded data", re.compile(r"b64decode|base64")),
    ("a long byte table", re.compile(r"(0x[0-9a-fA-F]{2}\s*,\s*){16}")),
]
FUN = re.compile(r"\b(?:thunk_)?(FUN|LAB)_([0-9a-fA-F]{8})\b")
HEX = re.compile(r"\b0x([0-9a-fA-F]{6,8})\b")


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout


def added_lines(base, head):
    """(path, line number, text) for every line the branch adds."""
    path, number = None, 0
    for line in git("diff", "--unified=0", "--no-color", f"{base}...{head}").splitlines():
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
        elif line.startswith("@@"):
            number = int(re.search(r"\+(\d+)", line).group(1))
        elif line.startswith("+") and path:
            yield path, number, line[1:]
            number += 1


def in_ranges(address, ranges):
    return any(low <= address < high for low, high in ranges)


def check(base, labels, entries, head="HEAD"):
    fails, warns, risky = [], [], []
    for row in git("diff", "--numstat", "--no-renames", "--diff-filter=d", f"{base}...{head}").splitlines():
        added, _, path = row.split("\t", 2)
        if LOCAL_ONLY.search(path):
            fails.append(f"{path}: must stay local (game files and game.exe's bytes; LEGAL.md)")
        elif added == "-" and not path.startswith(BINARY_OK):
            fails.append(f"{path}: a binary file (allowed only under {', '.join(BINARY_OK)})")
        if RISKY_PATHS.match(path):
            risky.append(f"{path}: steers CI or contributors' agents")
    for path, number, text in added_lines(base, head):
        where = f"{path}:{number}"
        if path.startswith("docs/research/re/game-"):
            continue  # the exported tables themselves
        if not path.startswith(("tools/test_", "tools/pr_guard.py")):  # made-up addresses, section ends
            check_addresses(where, text, entries, fails, warns)
        if not path.endswith(".md"):
            for what, pattern in RISKY_CODE:
                if pattern.search(text):
                    risky.append(f"{where}: {what}: {text.strip()[:100]}")
    if risky and REVIEW_LABEL not in labels:
        fails += [f"needs the `{REVIEW_LABEL}` label (a maintainer reads these first): {r}" for r in risky]
    return fails, warns, risky


def check_addresses(where, text, entries, fails, warns):
    for kind, digits in FUN.findall(text):
        address = int(digits, 16)
        if not in_ranges(address, (TEXT,)):
            fails.append(f"{where}: {kind}_{digits} is outside game.exe's code (.text)")
        elif kind == "FUN" and address not in entries:
            starts = sorted(entries)
            holder = starts[bisect.bisect_right(starts, address) - 1] if address > starts[0] else None
            if holder is not None and address < holder + entries[holder]:
                fails.append(f"{where}: FUN_{digits} is inside FUN_{holder:08x}'s body: "
                             f"not a function (LAB_{digits} if it's a label)")
            else:
                warns.append(f"{where}: FUN_{digits} isn't a function entry in game-functions.tsv")
    for digits in HEX.findall(text):
        address = int(digits, 16)
        if 0x400000 <= address < 0xa00000 and not in_ranges(address, IMAGE):
            fails.append(f"{where}: 0x{digits} is between game.exe's sections")


def function_entries():
    with open("docs/research/re/game-functions.tsv") as f:
        rows = [line.split("\t") for line in list(f)[1:]]
    return {int(address, 16): int(size) for address, _, size, *_ in rows}  # entry: body size


if __name__ == "__main__":
    base = sys.argv[1] if len(sys.argv) > 1 else "origin/main"
    head = sys.argv[2] if len(sys.argv) > 2 else "HEAD"
    labels = json.loads(os.environ.get("PR_LABELS") or "[]")
    fails, warns, risky = check(base, labels, function_entries(), head)
    for line in warns:
        print("WARN ", line)
    for line in fails:
        print("FAIL ", line)
    if risky and REVIEW_LABEL in labels:
        print(f"ok    {len(risky)} risky lines, reviewed (`{REVIEW_LABEL}`)")
    print(f"pr_guard: {len(fails)} FAIL, {len(warns)} WARN against {base}")
    sys.exit(1 if fails else 0)
