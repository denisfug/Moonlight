#!/usr/bin/env bash
# Publishes out/Moonlight-HB_<version>.pkg on the "claude/updates" branch (update.json + the .pkg),
# which the app reads in Configuracoes > Atualizar o Moonlight. The branch keeps a single commit
# (force-pushed) so the repository history does not grow with every package.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="$(sed -n 's/^set(ML_VERSION "\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")"
PKG="$ROOT/out/Moonlight-HB_${VER}.pkg"
[ -f "$PKG" ] || { echo "missing $PKG (run make pkg)"; exit 1; }
NOTES="${1:-Moonlight HB $VER}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp "$PKG" "$WORK/"
SIZE=$(stat -c %s "$PKG")
SHA=$(sha256sum "$PKG" | cut -d' ' -f1)
python3 - "$WORK/update.json" "$VER" "$(basename "$PKG")" "$SIZE" "$SHA" "$NOTES" <<'PY'
import json, sys
path, ver, pkg, size, sha, notes = sys.argv[1:]
json.dump({"version": ver, "pkg": pkg, "size": int(size), "sha256": sha, "notes": notes}, open(path, "w"), ensure_ascii=False, indent=1)
PY
cd "$WORK"
git init -q
git checkout -q -b claude/updates
git add .
git -c user.name="$(git -C "$ROOT" config user.name || echo moonlight)" -c user.email="$(git -C "$ROOT" config user.email || echo moonlight@localhost)" \
    commit -qm "Moonlight HB $VER (update feed)"
git push -q -f "$(git -C "$ROOT" remote get-url origin)" claude/updates
echo "published $VER ($SIZE bytes, sha256 $SHA)"
