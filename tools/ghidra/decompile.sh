#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Put d2d's names and citations into the Ghidra project (tools/gaps.py's
# symbols.tsv, ApplySymbols.java), then decompile all of game.exe into
# tools/ghidra/project/game_all.c (gitignored: decompiled C stays local).
# Read one function from it with tools/ghidra/fn.py. About 10 minutes.
#
# Usage: tools/ghidra/decompile.sh   (after tools/ghidra/import.sh)

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
HEADLESS="${GHIDRA_INSTALL_DIR:+$GHIDRA_INSTALL_DIR/support/analyzeHeadless}"
HEADLESS="${HEADLESS:-$(command -v ghidra-analyzeHeadless || command -v analyzeHeadless || find /opt/homebrew/Cellar/ghidra -name analyzeHeadless -type f 2>/dev/null | head -1)}"
[[ -n "$HEADLESS" ]] || { echo "error: analyzeHeadless not found (see import.sh)" >&2; exit 1; }

(cd "$ROOT" && python3 tools/gaps.py)
"$HEADLESS" "$HERE/project" D2Decomp -process game.exe -noanalysis \
  -scriptPath "$HERE/scripts" \
  -postScript ApplySymbols.java "$ROOT/build/gaps/symbols.tsv" \
  -postScript DecompileAll.java "$HERE/project/game_all.c" 2>&1 | tee "$HERE/project/decompile.log"
# analyzeHeadless exits 0 when a script fails to compile or throws.
! grep -q "SCRIPT ERROR" "$HERE/project/decompile.log"
