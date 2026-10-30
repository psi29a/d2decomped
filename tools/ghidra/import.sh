#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Headless import + auto-analyze of a D2 PE into a shared Ghidra project.
# Reruns are idempotent: -overwrite replaces the imported program.
#
# Usage:
#   tools/ghidra/import.sh                          # imports the launcher's bin/game.exe
#   tools/ghidra/import.sh /path/to/other.exe       # imports something else
#
# The Ghidra project lives under tools/ghidra/project/ (gitignored).
# Post-scripts under tools/ghidra/scripts/ run after analysis.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$HERE/project"
PROJECT_NAME="D2Decomp"
SCRIPT_DIR="$HERE/scripts"

# Default binary: whatever the launcher installed.
DATA_PATH="$(defaults read 'com.d2decomp.D2 Launcher' game.dataPath 2>/dev/null || true)"
DEFAULT_BIN="${DATA_PATH:+$DATA_PATH/bin/game.exe}"
BIN="${1:-$DEFAULT_BIN}"

if [[ -z "$BIN" || ! -f "$BIN" ]]; then
  echo "error: no binary. pass a path, or run the launcher first." >&2
  exit 1
fi

# Sanity: the launcher must deliver a bare PE. If MZ isn't at offset 0 the
# launcher's PE-wrapper strip regressed — fail loudly rather than paper over.
if ! head -c 2 "$BIN" | grep -q "^MZ"; then
  echo "error: $BIN doesn't start with MZ. Launcher extraction is broken." >&2
  exit 1
fi

# Locate analyzeHeadless: GHIDRA_INSTALL_DIR (a download from ghidra-sre.org),
# then PATH, then Homebrew's layout.
HEADLESS=""
if [[ -n "${GHIDRA_INSTALL_DIR:-}" && -x "$GHIDRA_INSTALL_DIR/support/analyzeHeadless" ]]; then
  HEADLESS="$GHIDRA_INSTALL_DIR/support/analyzeHeadless"
fi
if [[ -z "$HEADLESS" ]]; then
  HEADLESS="$(command -v ghidra-analyzeHeadless || command -v analyzeHeadless || true)"
fi
if [[ -z "$HEADLESS" ]]; then
  HEADLESS="$(find /opt/homebrew/Cellar/ghidra -name analyzeHeadless -type f 2>/dev/null | head -1)"
fi
if [[ -z "$HEADLESS" ]]; then
  echo "error: analyzeHeadless not found. brew install ghidra, or download it from" >&2
  echo "       ghidra-sre.org and set GHIDRA_INSTALL_DIR to the unpacked folder." >&2
  exit 1
fi

mkdir -p "$PROJECT_DIR"

echo "==> importing $(basename "$BIN") into $PROJECT_NAME"
"$HEADLESS" "$PROJECT_DIR" "$PROJECT_NAME" \
  -import "$BIN" \
  -overwrite \
  -scriptPath "$SCRIPT_DIR" \
  -postScript ExportOverview.java "$HERE/../../docs/research/re" \
  -postScript ExportCalls.java "$HERE/../../docs/research/re"
