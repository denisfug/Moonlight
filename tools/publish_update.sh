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
# Known-good test video for Configuracoes > Testar internet > Testar video (H.264 High + AAC 48 kHz,
# moov at the start), generated locally: 20 s of a test pattern and a 440 Hz tone.
VID="$ROOT/out/moonlight_teste.mp4"
if [ ! -f "$VID" ]; then
    ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=1280x720:rate=30:duration=20" \
        -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=20" -c:v libx264 -profile:v high -level 4.0 \
        -pix_fmt yuv420p -preset veryfast -b:v 1500k -c:a aac -b:a 128k -ar 48000 -ac 2 -movflags +faststart "$VID"
fi
cp "$VID" "$WORK/"
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
