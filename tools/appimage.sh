#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Wraps an installed Linux package (`cmake --install build --prefix <absolute dir>`:
# d2d-launcher, d2d, lib/, plugins/, qt.conf, as the .tar.gz holds them) into
# an AppImage, after checking that nothing in it needs a newer glibc or
# libstdc++ than the oldest system it targets.
#
#   tools/appimage.sh <install dir> <out.AppImage>
#
# APPIMAGETOOL: appimagetool (an AppImage itself; run unpacked where there's
# no FUSE, APPIMAGE_EXTRACT_AND_RUN=1). APPIMAGE_RUNTIME: the type2 runtime
# it embeds, given so appimagetool fetches nothing. CI pins and checks both
# (.github/workflows/ci.yml, appimage job).
# MAX_GLIBC (default 2.41, Void Linux's) and MAX_GLIBCXX (default 3.4.33,
# GCC 14's libstdc++, Debian 13's and Void's): the newest symbol versions any
# ELF in the AppImage may need. `--check-only` stops after the check.
set -euo pipefail

usage() { echo "usage: $0 [--check-only] <install dir> <out.AppImage>" >&2; exit 2; }
check_only=0
if [[ "${1:-}" == "--check-only" ]]; then check_only=1; shift; fi
[[ $# -eq 2 ]] || usage
install_dir=$(cd "$1" && pwd)
out=$2
max_glibc=${MAX_GLIBC:-2.41}
max_glibcxx=${MAX_GLIBCXX:-3.4.33}
repo=$(cd "$(dirname "$0")/.." && pwd)

[[ -x "$install_dir/d2d-launcher" && -x "$install_dir/d2d" ]] \
    || { echo "$install_dir: no d2d-launcher and d2d (cmake --install build --prefix it first)" >&2; exit 1; }

# The newest version of `prefix` (GLIBC_ / GLIBCXX_) any ELF under $1 needs,
# each file over the limit printed; returns 1 when one is.
too_new() {
    local dir=$1 prefix=$2 limit=$3 bad=0 file need symbols
    while IFS= read -r -d '' file; do
        head -c 4 "$file" 2>/dev/null | grep -q $'^\x7fELF' || continue
        # An ELF objdump can't read fails the check rather than passing unread.
        if ! symbols=$(objdump -T "$file" 2>&1); then
            echo "${file#"$dir"/}: objdump can't read its symbols: ${symbols##*$'\n'}" >&2
            bad=1
            continue
        fi
        need=$(grep -oE "\b${prefix}_[0-9][0-9.]*\b" <<<"$symbols" \
            | sed "s/^${prefix}_//" | sort -V | tail -n 1)
        [[ -n "$need" ]] || continue
        if [[ "$(printf '%s\n%s\n' "$need" "$limit" | sort -V | tail -n 1)" != "$limit" ]]; then
            echo "${file#"$dir"/}: needs ${prefix}_$need (newest allowed: $limit)" >&2
            bad=1
        fi
    done < <(find "$dir" -type f -print0)
    return "$bad"
}

command -v objdump >/dev/null || { echo "appimage: objdump (binutils) is needed for the glibc check" >&2; exit 1; }

appdir=$(mktemp -d "${TMPDIR:-/tmp}/d2d-appdir.XXXXXX")
trap 'rm -rf "$appdir"' EXIT
cp -a "$install_dir/." "$appdir/"
# The system's C++ runtime, not Debian's: a GL driver loaded into the process
# (Mesa's LLVM) may need a newer one than the bundled copy would give it.
rm -f "$appdir"/lib/libstdc++.so* "$appdir"/lib/libgcc_s.so*
# A StormLib built from source (no system libstorm) installs its headers,
# archive and CMake config too; none of it runs.
rm -rf "${appdir:?}/include" "${appdir:?}/lib64" "${appdir:?}/share/StormLib"
rmdir "$appdir/share" 2>/dev/null || true

status=0
too_new "$appdir" GLIBC "$max_glibc" || status=1
too_new "$appdir" GLIBCXX "$max_glibcxx" || status=1
if [[ $status -ne 0 ]]; then
    echo "appimage: a binary needs a newer glibc / libstdc++ than the targets have (build on an older base), or objdump could not read it" >&2
    exit 1
fi
echo "appimage: every ELF needs at most GLIBC_$max_glibc and GLIBCXX_$max_glibcxx"
[[ $check_only -eq 0 ]] || exit 0

: "${APPIMAGETOOL:?set APPIMAGETOOL to appimagetool}"
: "${APPIMAGE_RUNTIME:?set APPIMAGE_RUNTIME to the type2 runtime file}"

# The launcher is what a player runs; d2d sits beside it, where the launcher
# looks for it (engineBinaryPath), and both find lib/ through their DT_RPATH.
cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
here=$(dirname "$(readlink -f "$0")")
exec "$here/d2d-launcher" "$@"
EOF
chmod +x "$appdir/AppRun"
sed -e 's/^Icon=.*/Icon=d2d/' "$repo/assets/flatpak/desktop.in" > "$appdir/d2d.desktop"
cp "$repo/assets/icon/d2d.png" "$appdir/d2d.png"
ln -s d2d.png "$appdir/.DirIcon"

arch=$(uname -m)
ARCH=$arch "$APPIMAGETOOL" --no-appstream --runtime-file "$APPIMAGE_RUNTIME" "$appdir" "$out"
echo "appimage: $out"
