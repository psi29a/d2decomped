#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a branch before the push: the build, lint and test failures CI
hit on PRs #15-#97, and the patterns behind most of their review comments.

    tools/presubmit.py                  # everything: lint, strict build, clang-tidy, ctest
    tools/presubmit.py --quick          # lint, review checks, and GCC + clang-tidy on the changed
                                        # files when an earlier build left compile_commands.json
    tools/presubmit.py --docker         # CI's tools/validate-linux.sh in place of the local build
    tools/presubmit.py --base REF       # diff base (default: upstream/main, else origin/main)
    tools/presubmit.py --build DIR      # build dir (default: build-presubmit)
    tools/presubmit.py -C PATH          # another worktree

Each check prints FAIL (exit 1), WARN (look at it) or ok. The WARN lines
are the review classes in tools/review-checklist.md: a change made on one
path and not its siblings, new state nobody resets, no test, docs left
behind, unchecked errors. Answer each one (fix it, or a line in the PR body),
then read the diff against that checklist.

clang-tidy and ruff findings fail only on lines the branch added; the rest
are counted, not shown (a newer local clang-tidy flags more than CI's).
Data tests run when D2_MPQ_DIR (and D2_PATCH_INSTALLER) are set, as in CI.
"""
import argparse
import concurrent.futures
import hashlib
import importlib.util
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

results = []                                           # (level, check, message)


def report(level, check, message):
    results.append((level, check, message))
    print(f"{level:5} {check}: {message}", flush=True)


def git(tree, *args):
    return subprocess.run(["git", "-C", str(tree), *args], capture_output=True, text=True, check=True).stdout



def tool_tests(tree):
    """The tracked tools/**/test_*.py: an untracked tools/emu/.venv's tests aren't ours."""
    return sorted(path for path in git(tree, "ls-files", "tools").splitlines()
                  if path.endswith(".py") and Path(path).name.startswith("test_"))

def added_lines(tree, base):
    """{path: {line numbers added}} and {path: [(line, text)]} from the branch's diff."""
    numbers, texts, path, line = {}, {}, None, 0
    for row in git(tree, "diff", "-U0", "--no-color", f"{base}...HEAD").splitlines():
        if row.startswith("+++ "):
            path = row[6:] if row.startswith("+++ b/") else None
        elif row.startswith("@@") and path:
            line = int(re.match(r"@@ -\S+ \+(\d+)", row).group(1))
        elif row.startswith("+") and path:
            numbers.setdefault(path, set()).add(line)
            texts.setdefault(path, []).append((line, row[1:]))
            line += 1
    return numbers, texts


# ---------------------------------------------------------------- hygiene

# A co-author or AI attribution line in a commit message (AGENTS.md §6: the
# commit is the contributor's; Reported-by / Traced-by credit the others).
ATTRIBUTION = re.compile(r"^\s*co-authored-by:|generated (with|by) .*(claude|copilot|chatgpt|codex|cursor|gemini|\bai\b)|"
                         r"noreply@anthropic\.com|\U0001F916", re.IGNORECASE | re.MULTILINE)


def attribution_lines(message):
    """The lines of a commit message that credit a co-author or an AI tool."""
    return [line.strip() for line in message.splitlines() if ATTRIBUTION.search(line)]


def check_commit_messages(tree, base):
    log = git(tree, "log", "--format=%h%x00%B%x01", f"{base}..HEAD")
    found = []
    for entry in filter(str.strip, log.split("\x01")):
        sha, _, message = entry.strip().partition("\x00")
        found += [f"{sha}: {line}" for line in attribution_lines(message)]
    report("FAIL" if found else "ok", "attribution",
           f"{found[0]} (+{len(found) - 1} more): drop co-author / AI lines from the commit messages" if found else
           "no co-author or AI lines in the commit messages")


def check_hygiene(tree, base, changed):
    whitespace = subprocess.run(["git", "-C", str(tree), "diff", "--check", f"{base}...HEAD"],
                                capture_output=True, text=True, check=False)
    report("FAIL" if whitespace.returncode else "ok", "whitespace",
           (whitespace.stdout.strip().splitlines() or ["git diff --check"])[0] if whitespace.returncode else
           f"{len(changed)} files")


# ---------------------------------------------------------------- linters

def check_shell(tree, changed, numbers):
    def is_shell(path):
        if path.endswith(".sh"):
            return True
        file = tree / path
        if not file.is_file() or path.endswith(".py"):
            return False
        with open(file, "rb") as source:
            first = source.readline()
        return first.startswith(b"#!") and re.search(rb"\b(ba|da)?sh\b", first) is not None

    scripts = [path for path in changed if is_shell(path)]
    if not scripts:
        return
    check_traps(tree, scripts)
    if not shutil.which("shellcheck"):
        report("WARN", "shellcheck", "not installed; " + ", ".join(scripts) + " unchecked")
        return
    run = subprocess.run(["shellcheck", "-f", "gcc", *scripts], cwd=tree, capture_output=True, text=True, check=False)
    mine, elsewhere = [], 0
    for hit in re.finditer(r"^(\S+?):(\d+):\d+: (?:error|warning): (.*)$", run.stdout, re.M):
        if int(hit.group(2)) in numbers.get(hit.group(1), set()):
            mine.append(hit.group(0))
        else:
            elsewhere += 1
    report("FAIL" if mine else "ok", "shellcheck", "\n      ".join(mine[:15]) if mine else
           f"{len(scripts)} scripts ({elsewhere} on lines this branch didn't add)")


def check_traps(tree, scripts):
    """sweep.sh on #91: a trap that cleans up but doesn't exit."""
    for path in scripts:
        text = (tree / path).read_text(errors="replace")
        for line in text.splitlines():
            if not re.match(r"\s*trap\b.*\b(INT|TERM|HUP)\b", line) or "exit" in line:
                continue
            handlers = re.findall(r"trap\s+['\"]?(\w+)", line)
            # a one-line `stop() { ...; exit "$1"; }` or a body ending at a `}` line
            bodies = [re.search(rf"^{name}\s*\(\)\s*\{{(.*?\}}\s*$|.*?^\}})", text, re.S | re.M) for name in handlers]
            if not handlers or not all(body and "exit" in body.group(1) for body in bodies):
                report("WARN", "shell-trap", f"{path}: `{line.strip()}` handles a signal without exiting")


RUFF_RULES = "F,E9,B,BLE001,PLW1510,S602,S604"
RUFF_HARD = ("F", "E9", "B")                # the rest (unchecked subprocess, broad except, shell=True) warn


def check_python(tree, changed, numbers):
    """Check Python syntax, run tool tests and report Ruff findings on added lines."""
    scripts = [path for path in changed if path.endswith(".py") and (tree / path).is_file()]
    if not scripts:
        return
    def broken_syntax(path):                # compile() in memory: py_compile would leave __pycache__ in the tree
        try:
            compile((tree / path).read_bytes(), path, "exec")
            return False
        except SyntaxError:
            return True
    broken = [path for path in scripts if broken_syntax(path)]
    if broken:
        report("FAIL", "py-compile", ", ".join(broken))
    if any(path.startswith("tools/") for path in scripts):
        check_tool_tests(tree)
    ruff = shutil.which("ruff") and ["ruff"] or shutil.which("uvx") and ["uvx", "--quiet", "ruff@0.8.6"]
    if not ruff:
        report("WARN", "ruff", "neither ruff nor uvx; Python unchecked")
        return
    run = subprocess.run([*ruff, "check", "--no-cache", "--output-format", "json", "--select", RUFF_RULES, *scripts],
                         cwd=tree, capture_output=True, text=True, check=False)
    report_ruff_result(tree, scripts, numbers, run)


def report_ruff_result(tree, scripts, numbers, run):
    """Validate Ruff output and report findings that fall on the branch's added lines."""
    if run.returncode not in (0, 1):
        report("FAIL", "ruff", f"exit {run.returncode}: {(run.stderr or run.stdout).strip()[:300]}")
        return
    try:
        found = json.loads(run.stdout)
        if not isinstance(found, list) or (run.returncode == 1 and not found):
            raise ValueError("inconsistent Ruff result")
    except (json.JSONDecodeError, ValueError):
        report("FAIL", "ruff", "invalid or inconsistent JSON output: " + (run.stderr or run.stdout).strip()[:300])
        return
    mine = [hit for hit in found
            if hit["location"]["row"] in numbers.get(Path(os.path.relpath(hit["filename"], tree)).as_posix(), set())]
    def line(hit):
        return f"{os.path.relpath(hit['filename'], tree)}:{hit['location']['row']}: {hit['code'] or 'syntax'} {hit['message']}"
    # a syntax error has no code (null): py-compile above already failed it
    hard = [line(hit) for hit in mine if (hit["code"] or "E999").startswith(RUFF_HARD) and not (hit["code"] or "").startswith("BLE")]
    soft = [line(hit) for hit in mine if line(hit) not in hard]
    report("FAIL" if hard else "ok", "ruff", "\n      ".join(hard[:15]) if hard else
           f"{len(scripts)} files ({len(found) - len(mine)} on lines this branch didn't add)")
    for text in soft:
        report("WARN", "ruff", text)


def tool_test_variants(real, linked):
    """Prepare the ordinary and symlinked temp setups supported by this host."""
    variants = [("", {})]
    try:
        linked.symlink_to(real, target_is_directory=True)
    except OSError as error:
        if sys.platform != "win32" or getattr(error, "winerror", None) != 1314:
            raise
        report("WARN", "tool-tests", "symlinked TMPDIR skipped: Windows denied symlink creation")
    else:
        variants.append((" (symlinked TMPDIR)", {"TMPDIR": str(linked)}))
    return variants


def check_tool_tests(tree):
    """Run tracked tool tests with normal and symlinked temporary directories."""
    tests = tool_tests(tree)
    if tests:
        # Twice: as is, and with TMPDIR behind a symlink as macOS's /var -> /private/var
        # (#91: abspath against a resolved getcwd() picked every TU on the macOS leg)
        with tempfile.TemporaryDirectory() as real:
            linked = Path(real + "-link")
            variants = tool_test_variants(real, linked)
            try:
                for test in tests:
                    for label, extra in variants:
                        run = subprocess.run([sys.executable, "-m", "unittest", Path(test).name], cwd=tree / Path(test).parent,
                                             capture_output=True, text=True,
                                             env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1", **extra}, check=False)
                        report("FAIL" if run.returncode else "ok", "tool-tests",
                               f"{test}{label}: " + (run.stderr.strip().splitlines() or ["no output"])[-1])
            finally:
                linked.unlink(missing_ok=True)


# ---------------------------------------------------------------- C++: CI parity

def wait_for_other_builds(limit_s=1800):
    """One build at a time: two at once on a 15 GB machine got cc1plus
    OOM-killed. Wait while a compiler or clang-tidy runs, a docker build's
    (root's) too; not `ld`, which any project's link matches. Linux only:
    Apple's compiler runs as clang / clang++, not cc1plus, and Docker
    Desktop's builds run in its VM, out of the host's pgrep, so on macOS
    this never waits."""
    import time
    waited = 0
    while shutil.which("pgrep"):
        running = subprocess.run(["pgrep", "-l", "-x", "cc1plus|clang-tidy"],
                                 capture_output=True, text=True, check=False).stdout.split("\n")
        running = [row for row in running if row]
        if not running:
            return
        if waited == 0:
            print(f"presubmit: another build is running ({', '.join(running[:4])}); waiting for it", flush=True)
        if waited >= limit_s:
            sys.exit(f"presubmit: another build is still running after {limit_s // 60} min "
                     f"({', '.join(running[:4])}); rerun when it is done")
        time.sleep(15)
        waited += 15


def build_jobs():
    """Compile jobs that fit in memory: about 2 GB a C++ job at -O3, at most
    one a core. Linux reads MemAvailable; elsewhere it is one a core."""
    try:
        with open("/proc/meminfo") as info:
            available_kb = next(int(line.split()[1]) for line in info if line.startswith("MemAvailable:"))
    except (OSError, StopIteration, ValueError):
        return os.cpu_count() or 4
    return max(1, min(os.cpu_count() or 4, available_kb // (2048 * 1024)))


def memory_cap_gb():
    """A container memory limit that leaves the host about 3 GB."""
    try:
        with open("/proc/meminfo") as info:
            total_kb = next(int(line.split()[1]) for line in info if line.startswith("MemTotal:"))
    except (OSError, StopIteration, ValueError):
        return 8
    return max(4, total_kb // (1024 * 1024) - 3)


CI_FLAGS = "-Wshadow -Wconversion -Wno-sign-conversion -Werror"     # tools/validate-linux.sh's


def docker_command(command):
    """The command as is, or through `sg docker` when this shell predates the
    user's docker group membership (added without a re-login)."""
    if not shutil.which("docker") or not shutil.which("sg") or \
            subprocess.run(["docker", "info"], capture_output=True, check=False).returncode == 0:
        return command
    return ["sg", "docker", "-c", " ".join(shlex.quote(part) for part in command)]


# CI's readability-identifier-length (.clang-tidy): 3+ letters, except these.
# A fast stand-in that runs in --quick too (#93's test `at`, #92's `c`).
SHORT_NAME = re.compile(r"(?:\b(?:auto|int|float|bool|double|std::\w+(?:<[^;={}()]*>)?|std::size_t|unsigned|char)(?:\b|(?<=>))[&*\s]+|\bconst\s+\w+[&*\s]+)"
                        r"([A-Za-z_]\w?)\s*(?:=|\{|;|:(?!:)|,|\))")
SHORT_OK = re.compile(r"^(x|y|dx|dy|id|ok)$")             # IgnoredVariableNames
PARAM_OK = re.compile(r"^(x|y|dx|dy|id)$")                # IgnoredParameterNames: no `ok`
CATCH_OK = re.compile(r"^e$")                             # IgnoredExceptionVariableNames (tidy's default, ^[e]$)
LOOP_OK = re.compile(r"^([ijkxy]|\w\w)$")                 # a for-init counter: IgnoredLoopCounterNames, or 2+ letters
FOR_INIT = re.compile(r"\bfor\s*\(\s*(?:const\s+)?$")
CATCH = re.compile(r"\bcatch\s*\(\s*(?:const\s+)?$")


def short_name(code, hit):
    """The name if CI's tidy would flag it: a for-init counter by the loop rules, a catch's
    variable by the exception rules, a name followed by `,` or `)` (a function's or a
    lambda's parameter) by the parameter rules, the rest as variables."""
    name = hit.group(1)
    before, after = code[:hit.start()], code[hit.end(1):].lstrip()
    if CATCH.search(before):
        rule = CATCH_OK
    elif FOR_INIT.search(before) and not after.startswith(":"):
        rule = LOOP_OK
    elif after.startswith((",", ")")):
        rule = PARAM_OK
    else:
        rule = SHORT_OK
    return None if rule.match(name) else name


def check_short_names(texts):
    hits = []
    for path, rows in texts.items():
        if not path.endswith((".cpp", ".hpp")):
            continue
        for line, text in rows:
            code = text.split("//", 1)[0]
            for hit in SHORT_NAME.finditer(code):
                if name := short_name(code, hit):
                    hits.append(f"{path}:{line}: '{name}' is shorter than 3 letters (readability-identifier-length)")
    report("FAIL" if hits else "ok", "short-names", "\n      ".join(hits[:15]) if hits else "no 1-2 letter names added")


# The sort checks live in pr_guard (CI's gate on every pull request); presubmit runs them early.
_GUARD = importlib.util.spec_from_file_location("pr_guard", Path(__file__).with_name("pr_guard.py"))
pr_guard = importlib.util.module_from_spec(_GUARD)
_GUARD.loader.exec_module(pr_guard)
unsorted_runs, unsorted_tables = pr_guard.unsorted_runs, pr_guard.unsorted_tables


def check_sorted(tree, changed):
    """Whole files, not just added lines: a file the branch touches leaves sorted."""
    hits = [f"{path}:{number}: {entry} is out of order" for path in changed if (tree / path).is_file()
            for number, entry in unsorted_runs(path, (tree / path).read_text(encoding="utf-8", errors="replace").splitlines())]
    hits += [f"{path}:{number}: {address} is out of address order (a table in another order says so above it)"
             for path in changed if path.startswith("docs/research/") and path.endswith(".md") and (tree / path).is_file()
             for number, address in unsorted_tables((tree / path).read_text(encoding="utf-8").splitlines())]
    report("FAIL" if hits else "ok", "sorted", "\n      ".join(hits[:15]) if hits else "includes, source lists and address tables in order")


def build_volume(tree):
    """validate-linux.sh's build volume for worktree `tree`: d2d-build-linux-<folder>-<path hash>."""
    path = str(Path(tree).resolve())
    return "d2d-build-linux-" + re.sub(r"[^a-z0-9_.-]+", "-", Path(path).name.lower()) + "-" + \
        hashlib.sha1(path.encode()).hexdigest()[:8]


def find_tool(name):
    """On PATH, or Homebrew's llvm (keg-only: brew install llvm doesn't link clang-tidy)."""
    return shutil.which(name) or next((str(path) for path in (Path("/opt/homebrew/opt/llvm/bin") / name,
                                                              Path("/usr/local/opt/llvm/bin") / name) if path.is_file()), None)


def find_gcc():
    """A real GCC, as CI builds with: Homebrew's g++-NN first (macOS's g++ is clang)."""
    versioned = sorted({Path(found).name for folder in os.environ.get("PATH", "").split(os.pathsep) if os.path.isdir(folder)
                        for found in Path(folder).glob("g++-[0-9]*")}, key=lambda name: int(name.split("-")[1]))
    for name in reversed(versioned):
        return shutil.which(name)
    gxx = shutil.which("g++")
    if gxx and "Free Software Foundation" in subprocess.run([gxx, "--version"], capture_output=True, text=True, check=False).stdout:
        return gxx
    return None


def select_units(tree, build, sources):
    """The translation units that compile the changed files (tools/tidy-pr-select.py)."""
    # tidy-pr-select reads changed.txt from its cwd: run it in the build dir, not the worktree
    (build / "changed.txt").write_text("".join(f"{tree / path}\n" for path in sources))
    select = subprocess.run([sys.executable, str(tree / "tools/tidy-pr-select.py")], cwd=build, capture_output=True,
                            text=True, env={**os.environ, "BUILD_DIR": str(build)}, check=False)
    return select.stdout.split() if select.returncode == 0 and select.stdout.strip() else \
        [str(tree / path) for path in sources if path.endswith(".cpp")]


def unlisted_sources(tree, build, sources):
    """The changed .cpp files `build`'s compile_commands.json has no entry for."""
    listed = {os.path.realpath(entry["file"]) for entry in json.loads((build / "compile_commands.json").read_text())}
    return [path for path in sources if path.endswith(".cpp") and os.path.realpath(tree / path) not in listed]


def check_gcc(build, units):
    """GCC's -Wshadow / -Wconversion on each unit, -fsyntax-only: what CI's Linux build fails on
    and Apple clang lets by (most of the fix-up commits after #150)."""
    gcc = find_gcc()
    if not gcc:
        report("WARN", "gcc", "no GCC (brew install gcc): CI's -Wshadow / -Wconversion errors show only there")
        return
    entries = {entry["file"]: entry for entry in json.loads((build / "compile_commands.json").read_text())}

    def compile_one(unit):
        entry = entries.get(unit)
        if not entry:
            return ""
        args, flags, skip = entry.get("arguments") or shlex.split(entry["command"]), [], False
        for arg in args[1:]:
            if skip:
                skip = False
            elif arg in ("-o", "-arch"):            # -arch is Apple clang's alone
                skip = True
            elif arg != "-c":
                flags.append(arg)
        return subprocess.run([gcc, "-fsyntax-only", "-Wno-psabi", *CI_FLAGS.split(), *flags], cwd=entry["directory"],
                              capture_output=True, text=True, check=False).stderr
    with concurrent.futures.ThreadPoolExecutor(build_jobs()) as pool:
        outputs = list(pool.map(compile_one, units))
    errors = sorted({hit for output in outputs for hit in re.findall(r"^\S+:\d+:\d+: (?:fatal )?error: .*$", output, re.M)})
    report("FAIL" if errors else "ok", "gcc", "\n      ".join(errors[:15]) if errors else
           f"{len(units)} TUs, {Path(gcc).name} {CI_FLAGS}")


def check_tidy(tree, build, units, numbers, changed):
    clang_tidy = find_tool("clang-tidy")
    if not clang_tidy:
        report("WARN", "clang-tidy", "not installed (brew install llvm)")
        return
    def tidy(unit):
        return subprocess.run([clang_tidy, "-p", str(build), "--quiet", "--extra-arg=-Wno-unknown-warning-option", unit],
                              capture_output=True, text=True, check=False).stdout
    mine, elsewhere = [], 0
    with concurrent.futures.ThreadPoolExecutor(build_jobs()) as pool:   # CI: xargs -P 4; capped by memory
        outputs = list(pool.map(tidy, units))
    for output in outputs:
        for hit in re.finditer(r"^(\S+?):(\d+):\d+: (?:warning|error): (.*)$", output, re.M):
            path = os.path.relpath(hit.group(1), tree)
            # an include the branch dropped shows on old lines: any include-cleaner hit in a changed file
            if int(hit.group(2)) in numbers.get(path, set()) or (path in changed and "misc-include-cleaner" in hit.group(3)):
                mine.append(f"{path}:{hit.group(2)}: {hit.group(3)}")
            else:
                elsewhere += 1
    mine = sorted(set(mine))
    report("FAIL" if mine else "ok", "clang-tidy", "\n      ".join(mine[:20]) if mine else
           f"{len(units)} TUs ({elsewhere} findings on lines this branch didn't add; CI's older tidy may not flag them)")


def check_cpp(tree, changed, numbers, build, quick, docker):
    sources = [path for path in changed if path.endswith((".cpp", ".hpp")) and (tree / path).is_file()]
    if quick:
        report("WARN", "build", "skipped (--quick): no build or ctest")
        # an earlier build's compile commands are enough for GCC and tidy on the changed files
        known = next((folder for folder in (build, tree / "build") if (folder / "compile_commands.json").is_file()), None)
        if sources and known:
            if stale := unlisted_sources(tree, known, sources):
                # tidy-pr-select keeps only the build's TUs: a .cpp the build hasn't
                # configured yet (a new test) would pass unchecked
                report("FAIL", "compile commands", f"{known.name}/compile_commands.json lacks {', '.join(stale[:8])}: "
                       f"run `cmake --build {os.path.relpath(known, tree)}` (or a full tools/presubmit.py) first")
            units = select_units(tree, known, sources)
            check_gcc(known, units)
            check_tidy(tree, known, units, numbers, set(changed))
        elif sources:
            report("WARN", "gcc", "no compile_commands.json yet: one full tools/presubmit.py run makes it")
        return False
    wait_for_other_builds()
    if docker:
        # CI's own script, with -j cut to what fits and the container capped
        # below the host's memory, so an OOM stops the build and not the host
        # (macOS has no /proc/meminfo: build_jobs() is then a core count, so
        # cap it to the container's memory at ~2 GB a job as well)
        cap = memory_cap_gb()
        env = {**os.environ, "JOBS": os.environ.get("JOBS", str(max(1, min(build_jobs(), cap // 2)))),
               "D2D_DOCKER_MEMORY": os.environ.get("D2D_DOCKER_MEMORY", f"{cap}g"),
               # a build volume per worktree: they all mount at /src, so with one shared volume
               # ninja takes another branch's newer objects as up to date (a link then misses
               # a function the branch added); the folder's name and a hash of its full path, so
               # a/d2d and b/d2d don't share one (tools/README.md: listing and removing them)
               "D2D_BUILD_VOLUME": os.environ.get("D2D_BUILD_VOLUME", build_volume(tree))}
        run = subprocess.run(docker_command([str(tree / "tools/validate-linux.sh")]), cwd=tree, env=env, check=False)
        report("FAIL" if run.returncode else "ok", "validate-linux", f"exit {run.returncode}")
        return run.returncode == 0
    build.mkdir(parents=True, exist_ok=True)
    # an interrupted build can leave 0-byte files that ninja and FFmpeg's "built" stamp
    # still trust: video.hpp fails with "AVCodecContext does not name a type", a link with
    # "archive has no index". Dropping only the FFmpeg prefix isn't enough (its
    # ExternalProject stamps then skip configure), so start the build dir over.
    empty = [f for pattern in ("ffmpeg-*/include/**/*.h", "**/*.a", "**/*.o")
             for f in build.glob(pattern) if f.is_file() and f.stat().st_size == 0]
    if empty:
        report("WARN", "build-dir", f"{len(empty)} empty files (an interrupted build, e.g. {empty[0].relative_to(build)}); "
                                    f"removed {build.name}, building from scratch")
        shutil.rmtree(build)
        build.mkdir(parents=True)
    configure = ["cmake", "-S", str(tree), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 f"-DCMAKE_CXX_FLAGS={CI_FLAGS}", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DD2D_BUILD_TESTS=ON"]
    with open(build / "presubmit-build.log", "w") as log:
        ok = (subprocess.run(configure, stdout=log, stderr=subprocess.STDOUT, check=False).returncode == 0 and
              subprocess.run(["cmake", "--build", str(build), "--", "-k", "0", "-j", str(build_jobs())], stdout=log,
                             stderr=subprocess.STDOUT, check=False).returncode == 0)
    if not ok:
        log_text = (build / "presubmit-build.log").read_text(errors="replace")
        if "Killed signal" in log_text:     # the OOM killer, not the code: rerun with less else building
            report("FAIL", "strict-build", f"cc1plus was OOM-killed; free memory and rerun ({build / 'presubmit-build.log'})")
            return False
        # compiler diagnostics and ninja's FAILED lines, not log strings that happen to say "error:"
        errors = re.findall(r"^(?:FAILED: .*|\S+:\d+:\d+: (?:fatal )?error: .*)$", log_text, re.M)
        report("FAIL", "strict-build", f"({CI_FLAGS}) " + "\n      ".join(errors[:10] or ["see " + str(build / "presubmit-build.log")]))
        return False
    report("ok", "strict-build", CI_FLAGS)
    if not sources:
        return True
    units = select_units(tree, build, sources)
    check_gcc(build, units)
    check_tidy(tree, build, units, numbers, set(changed))
    return True


def check_ctest(build, quick, docker, built):
    if quick or docker or not built:
        return
    env = dict(os.environ)
    if not env.get("D2_MPQ_DIR"):
        report("WARN", "ctest", "D2_MPQ_DIR isn't set: the data tests skip")
    with open(build / "presubmit-ctest.log", "w") as log:
        run = subprocess.run(["ctest", "--test-dir", str(build), "--output-on-failure", "-j4"], env=env,
                             stdout=log, stderr=subprocess.STDOUT, check=False)
    text = (build / "presubmit-ctest.log").read_text(errors="replace")
    summary = re.search(r"\d+% tests passed.*", text)
    # ctest lists both under its "did not run" / "failed" footers as "N - name (Why)";
    # a test that exits SKIP_RETURN_CODE (77) is "(Skipped)", not a failure
    failed = re.findall(r"^\s*\d+ - (\S+) \((?!Skipped\))", text, re.M)
    skipped = re.findall(r"^\s*\d+ - (\S+) \(Skipped\)", text, re.M)
    if skipped:   # a skip means the data-backed checks didn't run
        report("WARN", "ctest-skipped", f"{len(skipped)} skipped: {', '.join(skipped)}")
    if run.returncode and failed:
        # the d2d-driving tests (smoke_d2d) can fail when run beside each other;
        # one that passes alone is a WARN to rerun, not the branch's failure
        with open(build / "presubmit-ctest-rerun.log", "w") as log:
            rerun = subprocess.run(["ctest", "--test-dir", str(build), "--output-on-failure", "--rerun-failed"], env=env,
                                   stdout=log, stderr=subprocess.STDOUT, check=False)
        if rerun.returncode == 0:
            report("WARN", "ctest", f"{', '.join(failed)} failed under -j4 and passed alone (flaky in parallel)")
            return
    report("FAIL" if run.returncode else "ok", "ctest",
           (summary.group(0) if summary else f"exit {run.returncode}") + (": " + ", ".join(failed) if failed else ""))


# ---------------------------------------------------------------- review nudges (WARN)

# The biggest class of review findings on PRs #15-#97 (59 of 189): a change reached one of these and not its siblings.
GAME = ("components/game/", "components/rules/")
SIBLINGS = [   # (family, the paths it reads, members): a member counts at 2+ added lines, so a passing mention doesn't
    ("unit kinds", GAME, {"player": r"\b(player|hero)\b", "merc": r"\b(merc|hireling)", "pet": r"\b(pet|summon|minion)",
                    "monster": r"\bmonster", "co-op partner": r"\b(other_?player|OtherPlayer|coop|co-op|partner|joined)"}),
    ("hit paths", GAME, {"melee": r"\b(melee|strike|swing)", "missile": r"\b(missile|shot|projectile)", "skill": r"\bskill"}),
    ("net roles", ("apps/d2d/", "components/net/", *GAME), {"host": r"\bhost\b", "client": r"\b(client|join(ed|er)?)\b"}),
]
RISKY = [   # error paths CodeRabbit kept finding (unchecked-error 12, leaks 9, python 13)
    (r"\.cpp$|\.hpp$", r"std::ofstream|fopen\(", "file write: checked and closed before use/rename?"),
    (r"\.cpp$|\.hpp$", r"(fs|filesystem)::(exists|create_directories|rename|remove|copy)\([^)]*\)(?!.*\b(ec|error)\b)",
     "std::filesystem call that throws: error_code overload?"),
    (r"\.cpp$|\.hpp$", r"\[\s*\w+\s*-\s*1\s*\]|size_t\(\s*-", "index - 1 / size_t(-1): empty or not-found case?"),
    (r"\.cpp$|\.hpp$", r"(level|town_of|find_\w+)\([^)]*\)\s*->", "result dereferenced directly: can it be null?"),
    (r"\.py$", r"except(\s+Exception)?\s*:", "broad except: does the failure still surface?"),
    (r"\.py$", r"struct\.unpack(_from)?\(", "binary parse: length checked first?"),
    (r"\.(sh|py)$|Makefile", r"\bexit 0\b|sys\.exit\(0\)", "exit 0: also after a partial failure or zero matches?"),
]
STATE = re.compile(r"^\s+(?:bool|int|unsigned|float|double|std::\w+(?:<[^;]*>)?|[A-Z]\w*\*?)\s+(\w+)\s*(?:=[^;]*|\{[^;]*\})?;")


def check_review(tree, base, changed, texts):
    code = {path: rows for path, rows in texts.items() if path.endswith((".cpp", ".hpp"))}
    # siblings from the change itself, not its tests (#90: a test's `std::vector<Missile>` read as a missile path),
    # in the code where the family lives (#101: always on for any diff that says `player`)
    for family, where, members in SIBLINGS:
        lines = [text for path, rows in code.items() if path.startswith(where) for _, text in rows]
        seen = [name for name, pattern in members.items() if sum(1 for text in lines if re.search(pattern, text, re.I)) >= 2]
        if 0 < len(seen) < len(members):
            missing = [name for name in members if name not in seen]
            report("WARN", "siblings", f"{family}: the diff touches {', '.join(seen)}, not {', '.join(missing)}. "
                                       "Same rule there? Say so in the PR if not.")
    for path, rows in code.items():
        if path.endswith(".hpp"):
            for num, text in rows:
                field = STATE.match(text)
                if field and "(" not in text and "static" not in text and "constexpr" not in text:
                    name = field.group(1)
                    # git grep exits 1 on no match (a field in tests/ or tools/ only)
                    grep = subprocess.run(["git", "-C", str(tree), "grep", "-n", "-w", name, "--", "components", "apps"],
                                          capture_output=True, text=True, check=False)
                    resets = grep.stdout.count("\n")
                    if resets <= 2:
                        report("WARN", "new-state", f"{path}:{num}: `{name}` is new; reset on new game / leave / "
                                                    f"co-op end / load? ({resets} mentions in the tree)")
    for path, rows in texts.items():
        if path.startswith("tests/") or "/test_" in path or path == "tools/presubmit.py":   # its own patterns
            continue
        for path_pattern, pattern, question in RISKY:
            if re.search(path_pattern, path):
                for num, text in rows:
                    if re.search(pattern, text) and not text.lstrip().startswith(("//", "#")):
                        report("WARN", "error-path", f"{path}:{num}: {question}")
    src = [path for path in changed if path.startswith(("components/", "apps/")) and path.endswith((".cpp", ".hpp"))]
    tests = [path for path in changed if path.startswith("tests/") or "/test_" in path]
    if src and not tests:
        report("WARN", "test-gap", f"{len(src)} source files changed, no test changed (86 of 189 findings a test would have caught)")
    for path, rows in texts.items():
        if path.startswith("tests/"):
            for num, text in rows:
                if re.search(r"\b(load_\w+|mpq|monprop|\.txt\b|\.bin\b)", text, re.I) and \
                        "patched" not in "".join(t for _, t in rows):
                    report("WARN", "data-gating", f"{path}:{num}: reads game data; 1.14d-only? gate on data->patched")
                    break
    check_docs(tree, changed, texts)


UNION_TABLES = ("docs/research/re/bugs.md", "docs/research/re/deviations.md", "docs/research/re/stubs.md",
                "docs/research/re/todo.md", "docs/research/re/unverified.md")    # merge=union in .gitattributes


def table_clashes(lines):
    """A union merge keeps both sides' new rows: a `#` two rows share in one section, or a row twice."""
    def separator(line):
        return line.startswith("|") and set(line.strip()) <= set("|-: ")

    clashes, heading, numbers, rows = [], "", {}, {}
    for num, line in enumerate(lines, 1):
        if line.startswith("#"):
            heading, numbers = line.lstrip("#").strip(), {}
        if not line.startswith("|") or separator(line) or (num < len(lines) and separator(lines[num])):
            continue                                                          # prose, a separator, a header
        if line in rows:
            clashes.append(f"line {num} repeats line {rows[line]}")
        rows.setdefault(line, num)
        first = line.strip().strip("|").split("|")[0].strip()
        if first.isdigit():
            if first in numbers:
                clashes.append(f"line {num}: #{first} is also line {numbers[first]} ({heading})")
            numbers.setdefault(first, num)
    return clashes


def check_docs(tree, changed, texts):
    docs = [path for path in changed if path.endswith(".md")]
    if "docs/PLAN.md" in changed:
        report("WARN", "plan", "docs/PLAN.md changed: the maintainer updates it after a merge; "
                               "put the status in the PR body (AGENTS.md section 6)")
    for path in docs:
        if path in UNION_TABLES:
            clashes = table_clashes((tree / path).read_text(encoding="utf-8").splitlines())
            if clashes:
                report("FAIL", "table-rows", f"{path}: " + "; ".join(clashes[:6]) +
                       ". Two branches' rows met: renumber or drop yours")
    for path in docs:
        missing = set()
        for num, text in texts.get(path, []):
            for ref in re.findall(r"`((?:apps|components|tests|tools|docs)/[\w./-]+\.\w+)`", text):
                if not (tree / ref).exists():
                    missing.add(f"{ref} (line {num})")
        if missing:
            report("FAIL", "doc-paths", f"{path} names files that don't exist: " + ", ".join(sorted(missing)))
    if any("devctl" in path and path.endswith((".cpp", ".hpp")) for path in changed) and \
            not any("control_channel" in path for path in docs):
        report("WARN", "docs-sync", "devctl changed, docs/control_channel.md didn't")
    if any(path.startswith(("components/", "apps/")) for path in changed) and not docs:
        report("WARN", "docs-sync", "code changed, no .md changed: todo.md row, the system's doc, README?")
    for path in docs:
        rows = texts.get(path, [])
        claims = [num for num, text in rows if re.search(r"\b(all|every|always|never|full(y)?|complete)\b", text, re.I)]
        if claims:
            report("WARN", "doc-claims", f"{path}: lines {claims[:6]} claim all/every/never; true for every path?")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-C", dest="worktree", default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument("--base")
    parser.add_argument("--build")
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--docker", action="store_true")
    args = parser.parse_args()
    tree = Path(args.worktree).resolve()
    if not (tree / ".git").exists():
        sys.exit(f"presubmit: {tree} is not a git worktree")
    def has(ref):
        return subprocess.run(["git", "-C", str(tree), "rev-parse", "-q", "--verify", ref], capture_output=True, check=False).returncode == 0
    base = args.base or next((ref for ref in ("upstream/main", "origin/main") if has(ref)), "upstream/main")
    if not has(base):
        sys.exit(f"presubmit: no {base} (git fetch upstream?)")
    args.base = base
    if args.docker and (not shutil.which("docker") or
                        subprocess.run(docker_command(["docker", "info"]), capture_output=True, check=False).returncode):
        sys.exit("presubmit: --docker, but the docker daemon isn't reachable")
    branch = git(tree, "branch", "--show-current").strip() or "detached"
    build = Path(args.build).resolve() if args.build else tree / "build-presubmit"
    dirty = git(tree, "status", "--porcelain", "--untracked-files=no").strip()
    print(f"presubmit: {branch} in {tree}, against {args.base}")
    if dirty:
        # lint and the review checks read the commits (base...HEAD); cmake, clang-tidy and ctest the working tree
        report("WARN", "dirty-tree", "uncommitted changes: the build and tests include them, lint and review checks don't. "
                                     "Commit first so both see the same code.")

    changed = [path for path in git(tree, "diff", "--name-only", "--diff-filter=AMR", f"{args.base}...HEAD").split()]
    if not changed:
        sys.exit(f"presubmit: nothing changed against {args.base}")
    check_commit_messages(tree, args.base)
    numbers, texts = added_lines(tree, args.base)
    check_hygiene(tree, args.base, changed)
    check_shell(tree, changed, numbers)
    check_python(tree, changed, numbers)
    check_review(tree, args.base, changed, texts)
    check_short_names(texts)
    check_sorted(tree, changed)
    built = check_cpp(tree, changed, numbers, build, args.quick, args.docker)
    check_ctest(build, args.quick, args.docker, built)

    fails = sum(1 for level, _, _ in results if level == "FAIL")
    warns = sum(1 for level, _, _ in results if level == "WARN")
    print(f"\npresubmit: {fails} FAIL, {warns} WARN. "
          + ("Fix the FAILs. " if fails else "") + "Answer each WARN (fix, or one line in the PR body), "
          "then read the diff against tools/review-checklist.md.")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
