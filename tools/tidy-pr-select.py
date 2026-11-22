#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pick the TUs a change needs clang-tidy to re-run on.

Reads:
  - the changed files (--changed, default changed.txt): one .cpp / .hpp /
    .h / .inc path per line, relative to the cwd or absolute; a line may end
    in a tab and `comment` for a header whose change is comments only
  - $BUILD_DIR/compile_commands.json (default build): every TU the build knows
  - the repository's own #include lines (no build needed: CI's tidy job only
    configures, so Ninja has no dependency records)
Writes to stdout one absolute .cpp path per line. Empty output: nothing to tidy.

A changed .cpp is tidied. A changed header H: the TUs that include H
themselves, and one TU for H and for each header that includes it, directly
or not. include-cleaner holds every TU to including what it uses, so a TU that
reaches H only through another header uses nothing of H's: what H changes
can't change its findings (unless its .clang-tidy's IgnoreHeaders lets it
reach H that way, as apps/d2d reaches the game's headers: those TUs too). A header whose change is comments only (--base
tells, or the `comment` mark) needs one TU. A changed .inc: every TU that
reaches it. --transitive: every TU that reaches a changed file (for checks
that don't follow the include rule, such as GCC's -Wshadow). --lean: one TU
for any changed header, as for a comment-only one: what presubmit --quick
reports (findings on the branch's lines and include-cleaner in its changed
files) shows from any TU that compiles the header; its includers' own
findings are in files the branch didn't change.

--annotate --base REV: print the changed list with the comment-only headers
marked, and nothing else (validate-linux.sh runs it on the host, where git is).
"""
import argparse
import collections
import json
import os
import re
import subprocess
import sys

ROOT = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SOURCE_DIRS = ("apps", "components", "tests", "tools")
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.M)
HEADER = (".hpp", ".h")


def project_files(root=None):
    """Every source and header under the source dirs (realpaths)."""
    root = root or ROOT
    found = []
    for top in SOURCE_DIRS:
        for folder, dirs, files in os.walk(os.path.join(root, top)):
            dirs[:] = [d for d in dirs if d not in (".venv", ".cache", "__pycache__", "project")]
            found += [os.path.realpath(os.path.join(folder, name)) for name in files
                      if name.endswith((".cpp", ".hpp", ".h", ".inc"))]
    return found


def include_graph(files):
    """{file: set of project files it includes} and its reverse."""
    by_name = collections.defaultdict(list)
    for path in files:
        by_name[os.path.basename(path)].append(path)
    known = set(files)
    forward = {}
    for path in files:
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        targets = set()
        for _, name in INCLUDE.findall(text):
            local = os.path.realpath(os.path.join(os.path.dirname(path), name))
            if local in known:
                targets.add(local)
                continue
            # by name, through the include dirs: every project file the name could be
            targets.update(hit for hit in by_name.get(os.path.basename(name), []) if hit.endswith("/" + name) or name == os.path.basename(hit))
        targets.discard(path)
        forward[path] = targets
    reverse = collections.defaultdict(set)
    for path, targets in forward.items():
        for target in targets:
            reverse[target].add(path)
    return forward, reverse


_ignore_cache = {}


def ignored_headers(tu, root=None):
    """The include-cleaner IgnoreHeaders regexes of the .clang-tidy nearest `tu`:
    a header they match is one the TU needn't include itself."""
    root = root or ROOT
    folder = os.path.dirname(tu)
    while True:
        if folder not in _ignore_cache:
            config = os.path.join(folder, ".clang-tidy")
            found = None
            if os.path.isfile(config):
                match = re.search(r"misc-include-cleaner\.IgnoreHeaders:\s*'([^']*)'", open(config).read())
                if match:
                    found = [re.compile("(?:" + pattern + ")$") for pattern in match.group(1).split(";") if pattern]
            _ignore_cache[folder] = found
        if _ignore_cache[folder] is not None or folder == root or len(folder) <= len(root):
            return _ignore_cache[folder] or []
        folder = os.path.dirname(folder)


def comment_only(path, base, cwd=None):
    """True when `path`'s change since `base` (working tree included) is only comment or blank lines."""
    diff = subprocess.run(["git", "diff", "-U0", base, "--", path], capture_output=True, text=True, cwd=cwd, check=False)
    if diff.returncode != 0:
        return False
    lines = [line[1:].strip() for line in diff.stdout.splitlines()
             if line[:1] in "+-" and not line.startswith(("+++", "---"))]
    return bool(lines) and all(not line or line.startswith(("//", "/*", "*")) for line in lines)


def read_changed(source):
    """[(realpath, comment_marked)] from the changed list."""
    stream = sys.stdin if source == "-" else open(source)
    out = []
    for line in stream:
        if not line.strip():
            continue
        path, _, mark = line.rstrip("\n").partition("\t")
        out.append((os.path.realpath(path.strip()), mark.strip() == "comment"))
    return out


def select(changed, tus, forward, reverse, transitive=False, lean=False):
    """The TUs (realpaths, a subset of `tus`) to tidy for `changed` [(path, comment_only)]."""
    def reaching(path):
        """Every TU that includes `path`, directly or not."""
        seen, todo, out = {path}, [path], set()
        while todo:
            for up in reverse.get(todo.pop(), ()):
                if up not in seen:
                    seen.add(up)
                    todo.append(up)
                    if up in tus:
                        out.add(up)
        return out

    reach_cache = {}
    def reaches(tu):
        if tu not in reach_cache:
            seen, todo = {tu}, [tu]
            while todo:
                for down in forward.get(todo.pop(), ()):
                    if down not in seen:
                        seen.add(down)
                        todo.append(down)
            reach_cache[tu] = seen
        return reach_cache[tu]

    picked = set()
    def cover(header):
        """One TU that reaches `header`, unless one picked already does."""
        if any(header in reaches(tu) for tu in picked):
            return
        candidates = sorted(reaching(header))
        if candidates:
            picked.add(min(candidates, key=lambda tu: (header not in forward.get(tu, ()), tu)))

    for path, marked in changed:
        if path in tus:
            picked.add(path)
            continue
        if not os.path.exists(path):
            continue                                 # deleted: whoever included it changed too, or fails to build
        if path not in forward:
            return None                              # not a file the graph knows: can't narrow
        if transitive or not path.endswith(HEADER):
            picked |= reaching(path)
            continue
        if marked or lean:
            cover(path)
            continue
        picked |= {tu for tu in reverse.get(path, ()) if tu in tus}
        # a TU whose .clang-tidy lets it reach this header through another (apps/d2d through game_api.hpp)
        picked |= {tu for tu in reaching(path) if any(pattern.search(path) for pattern in ignored_headers(tu))}
        # the headers that include it, directly or not: their own code may use what changed
        seen, todo = {path}, [path]
        while todo:
            for up in reverse.get(todo.pop(), ()):
                if up not in seen and up.endswith(HEADER):
                    seen.add(up)
                    todo.append(up)
        for header in sorted(seen):
            cover(header)
    return picked


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--changed", default="changed.txt")
    parser.add_argument("--base", help="mark headers whose change since BASE is comments only")
    parser.add_argument("--annotate", action="store_true", help="print the changed list, marked, and stop")
    parser.add_argument("--transitive", action="store_true", help="every TU that reaches a changed file")
    parser.add_argument("--lean", action="store_true", help="one TU for any changed header (presubmit --quick)")
    args = parser.parse_args(argv)
    if args.changed != "-" and not os.path.exists(args.changed):
        return 0
    changed = read_changed(args.changed)
    if args.base:
        changed = [(path, marked or (path.endswith(HEADER) and comment_only(path, args.base, cwd=ROOT))) for path, marked in changed]
    if args.annotate:
        for path, marked in changed:
            print(os.path.relpath(path, ROOT) + ("\tcomment" if marked else ""))
        return 0
    if not changed:
        return 0
    build_dir = os.environ.get("BUILD_DIR", "build")
    with open(os.path.join(build_dir, "compile_commands.json")) as source:
        entries = json.load(source)
    entries = [entry for entry in entries if "/_deps/" not in entry["file"] and "/build/" not in entry["file"]]
    files = {os.path.realpath(os.path.join(entry["directory"], entry["file"])): entry["file"] for entry in entries}
    forward, reverse = include_graph(sorted(set(project_files()) | set(files)))
    picked = select(changed, set(files), forward, reverse, args.transitive, args.lean)
    if picked is None:                               # fall back to every project TU
        picked = set(files)
    for tu in sorted(files[path] for path in picked):
        print(tu)
    return 0


if __name__ == "__main__":
    sys.exit(main())
