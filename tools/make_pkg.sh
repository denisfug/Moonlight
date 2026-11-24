#!/usr/bin/env bash
# ELF -> eboot.bin (create-fself) -> param.sfo -> fake PKG (PkgTool.Core), same steps as the
# OpenOrbis samples and our earlier console-tested builds.
# Usage: OO_PS4_TOOLCHAIN=... tools/make_pkg.sh build-ps4/moonlight out/
set -euo pipefail
ELF="${1:?ELF path}"; OUT="${2:?output dir}"
T="${OO_PS4_TOOLCHAIN:?set OO_PS4_TOOLCHAIN}"
export OO_PS4_TOOLCHAIN  # create-fself reads this env var internally
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TITLE="Moonlight HB"
TITLE_ID="MLHB00001"
CONTENT_ID="IV0000-MLHB00001_00-MOONLIGHTHB00000"
VERSION="$(grep -oP 'set\(ML_VERSION "\K[0-9.]+' "$ROOT/CMakeLists.txt")"
_FSELF_SRC="$T/bin/linux/create-fself"; _GP4_SRC="$T/bin/linux/create-gp4"; _PKGTOOL_SRC="$T/bin/linux/PkgTool.Core"
# On WSL, Linux binaries on NTFS (/mnt/c/...) fail silently with exit 255.
# Copy them to the Linux tmpfs so they execute reliably; also copy if binfmt rejects them.
_BIN_TMPDIR=""
_need_copy=0
[[ "$T" == /mnt/[a-zA-Z]/* ]] && _need_copy=1
if [ "$_need_copy" -eq 0 ] && ! "$_FSELF_SRC" --version >/dev/null 2>&1 && ! "$_FSELF_SRC" -h >/dev/null 2>&1; then
    _need_copy=1
fi
if [ "$_need_copy" -eq 1 ]; then
    _BIN_TMPDIR="$(mktemp -d)"
    cp "$_FSELF_SRC" "$_BIN_TMPDIR/create-fself"
    cp "$_GP4_SRC"   "$_BIN_TMPDIR/create-gp4"
    cp "$_PKGTOOL_SRC" "$_BIN_TMPDIR/PkgTool.Core"
    chmod +x "$_BIN_TMPDIR/create-fself" "$_BIN_TMPDIR/create-gp4" "$_BIN_TMPDIR/PkgTool.Core"
fi
FSELF="${_BIN_TMPDIR:+$_BIN_TMPDIR/create-fself}"; FSELF="${FSELF:-$_FSELF_SRC}"
GP4="${_BIN_TMPDIR:+$_BIN_TMPDIR/create-gp4}"; GP4="${GP4:-$_GP4_SRC}"
_PKGTOOL_BIN="${_BIN_TMPDIR:+$_BIN_TMPDIR/PkgTool.Core}"; _PKGTOOL_BIN="${_PKGTOOL_BIN:-$_PKGTOOL_SRC}"
# PkgTool.Core is a .NET binary; on systems without ICU installed it needs DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
# PkgTool.Core needs libssl.so.1.1 (OpenSSL 1.1); on systems with only OpenSSL 3 we
# look for a pre-extracted copy and add it to LD_LIBRARY_PATH automatically.
_SSL11_PATH=""
for _candidate in \
    /tmp/ssl11/extracted/usr/lib/x86_64-linux-gnu \
    /usr/lib/x86_64-linux-gnu \
    /lib/x86_64-linux-gnu; do
    [ -f "$_candidate/libssl.so.1.1" ] && { _SSL11_PATH="$_candidate"; break; }
done
PKGTOOL() {
    local _extra_ld=""
    [ -n "$_SSL11_PATH" ] && _extra_ld="$_SSL11_PATH"
    if [ -n "$_extra_ld" ]; then
        DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 LD_LIBRARY_PATH="${_extra_ld}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$_PKGTOOL_BIN" "$@"
    else
        DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 "$_PKGTOOL_BIN" "$@"
    fi
}
export -f PKGTOOL
export _PKGTOOL_BIN _SSL11_PATH
PAID=0x3800000000000011

STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE" "${_BIN_TMPDIR:-}"' EXIT
IMG="$STAGE/image"
mkdir -p "$IMG/sce_sys/about" "$IMG/sce_module" "$IMG/assets/fonts" "$OUT"

"$FSELF" -in="$ELF" -out="$STAGE/app.oelf" --eboot "$IMG/eboot.bin" --paid "$PAID" >/dev/null
[ "$(head -c 4 "$IMG/eboot.bin" | od -An -tx1 | tr -d ' ')" = "4f153d1d" ] || { echo "eboot.bin is not a SELF"; exit 1; }
# Refuses a package whose eboot lost part of the initialized data (old create-fself): closes at launch.
python3 "$(dirname "$0")/check_eboot.py" "$ELF" "$IMG/eboot.bin" || exit 1

SFO="$IMG/sce_sys/param.sfo"
PKGTOOL sfo_new "$SFO" >/dev/null
set_e() { PKGTOOL sfo_setentry "$SFO" "$1" --type "$2" --maxsize "$3" --value "$4" >/dev/null; }
set_e APP_TYPE Integer 4 1
set_e APP_VER Utf8 8 "$VERSION"
set_e ATTRIBUTE Integer 4 0
set_e CATEGORY Utf8 4 gd
set_e CONTENT_ID Utf8 48 "$CONTENT_ID"
set_e DOWNLOAD_DATA_SIZE Integer 4 0
set_e SYSTEM_VER Integer 4 0
set_e TITLE Utf8 128 "$TITLE"
set_e TITLE_ID Utf8 12 "$TITLE_ID"
set_e VERSION Utf8 8 "$VERSION"

cp "$ROOT/sce_sys/icon0.png" "$ROOT/sce_sys/pic1.png" "$IMG/sce_sys/"
cp "$T/src/modules/right.sprx" "$IMG/sce_sys/about/"
cp "$T/src/modules/libc.prx" "$T/src/modules/libSceFios2.prx" "$IMG/sce_module/"
cp "$ROOT/assets/fonts/Outfit-Regular.ttf" "$ROOT/assets/fonts/Outfit-Bold.ttf" "$ROOT/assets/fonts/OFL.txt" "$IMG/assets/fonts/"

FILES="$(cd "$IMG" && find . -type f | sed 's|^\./||' | sort | tr '\n' ' ')"
(cd "$IMG" && "$GP4" -out pkg.gp4 --content-id="$CONTENT_ID" --files "$FILES" >/dev/null)
(cd "$IMG" && PKGTOOL pkg_build pkg.gp4 "$STAGE" >/dev/null)
PKG="$STAGE/$CONTENT_ID.pkg"
[ -f "$PKG" ] || { echo "pkg_build did not produce $CONTENT_ID.pkg"; exit 1; }
cp "$PKG" "$OUT/Moonlight-HB_${VERSION}.pkg"
cp "$IMG/eboot.bin" "$OUT/eboot.bin"
echo "PKG: $OUT/Moonlight-HB_${VERSION}.pkg ($(stat -c %s "$OUT/Moonlight-HB_${VERSION}.pkg") bytes, version $VERSION, $TITLE_ID)"
