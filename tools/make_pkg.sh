#!/usr/bin/env bash
# ELF -> eboot.bin (create-fself) -> param.sfo -> fake PKG (PkgTool.Core), same steps as the
# OpenOrbis samples and our earlier console-tested builds.
# Usage: OO_PS4_TOOLCHAIN=... tools/make_pkg.sh build-ps4/moonlight out/
set -euo pipefail

# Trap ERR to show the failing line — helps diagnose silent failures
trap 'echo "make_pkg.sh: error on line $LINENO (exit $?)" >&2' ERR

ELF="${1:?ELF path}"; OUT="${2:?output dir}"
T="${OO_PS4_TOOLCHAIN:?set OO_PS4_TOOLCHAIN}"
export OO_PS4_TOOLCHAIN  # create-fself reads this env var internally
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TITLE="Moonlight HB"
TITLE_ID="MLHB00001"
CONTENT_ID="IV0000-MLHB00001_00-MOONLIGHTHB00000"
VERSION="$(grep -oP 'set\(ML_VERSION "\K[0-9.]+' "$ROOT/CMakeLists.txt")"

# Persistent cache for Linux toolchain binaries (survives between builds).
# Priority: $T/bin/linux/ (native Linux install) → $HOME/.cache (downloaded) → download now.
_OO_CACHE="$HOME/.cache/openorbis-linux-bin"
_OO_VERSION="v0.5.4"
_OO_RELEASE_TAR="https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/${_OO_VERSION}/toolchain-llvm-18.tar.gz"

# Check if a binary is available (native or cached). Returns 0 if found, 1 if not.
_bin_available() {
    local name="$1"
    if [ -f "$T/bin/linux/$name" ] && [ -x "$T/bin/linux/$name" ]; then
        return 0
    elif [ -f "$_OO_CACHE/$name" ] && [ -x "$_OO_CACHE/$name" ]; then
        return 0
    fi
    return 1
}

# Resolve/download OpenOrbis Linux binaries. Uses set +e internally to avoid silent abort.
_resolve_oo_bins() {
    local need_download=0
    local name
    for name in create-fself create-gp4 PkgTool.Core; do
        if ! _bin_available "$name"; then
            need_download=1
            break
        fi
    done
    if [ "$need_download" -eq 0 ]; then
        return 0
    fi
    echo ">>> OpenOrbis Linux binaries not found; downloading from GitHub release ${_OO_VERSION}..." >&2
    echo "    (this is a one-time ~130MB download; cached in $_OO_CACHE)" >&2
    mkdir -p "$_OO_CACHE"
    # Use mktemp without --suffix for POSIX compatibility (BusyBox, old coreutils)
    local tmp_tar
    tmp_tar="$(mktemp /tmp/oo-XXXXXX.tar.gz)"
    local download_ok=0
    if command -v wget >/dev/null 2>&1; then
        wget -q --show-progress "$_OO_RELEASE_TAR" -O "$tmp_tar" >&2 && download_ok=1
    fi
    if [ "$download_ok" -eq 0 ] && command -v curl >/dev/null 2>&1; then
        curl -L --progress-bar "$_OO_RELEASE_TAR" -o "$tmp_tar" >&2 && download_ok=1
    fi
    if [ "$download_ok" -eq 0 ]; then
        rm -f "$tmp_tar"
        echo "ERROR: download failed. Put create-fself, create-gp4, PkgTool.Core in $_OO_CACHE/" >&2
        return 1
    fi
    local tmp_x
    tmp_x="$(mktemp -d)"
    # Try exact path first, then wildcard fallback (different tar versions)
    tar -xf "$tmp_tar" -C "$tmp_x" \
        "OpenOrbis-PS4-Toolchain/bin/linux/create-fself" \
        "OpenOrbis-PS4-Toolchain/bin/linux/create-gp4" \
        "OpenOrbis-PS4-Toolchain/bin/linux/PkgTool.Core" 2>/dev/null || \
    tar -xf "$tmp_tar" -C "$tmp_x" --wildcards \
        '*/bin/linux/create-fself' '*/bin/linux/create-gp4' '*/bin/linux/PkgTool.Core' 2>/dev/null || true
    # Copy extracted binaries to cache
    find "$tmp_x" -name "create-fself" -exec cp {} "$_OO_CACHE/create-fself" \; 2>/dev/null || true
    find "$tmp_x" -name "create-gp4"   -exec cp {} "$_OO_CACHE/create-gp4"   \; 2>/dev/null || true
    find "$tmp_x" -name "PkgTool.Core" -exec cp {} "$_OO_CACHE/PkgTool.Core" \; 2>/dev/null || true
    chmod +x "$_OO_CACHE/create-fself" "$_OO_CACHE/create-gp4" "$_OO_CACHE/PkgTool.Core" 2>/dev/null || true
    rm -rf "$tmp_tar" "$tmp_x"
    local ok=1
    for name in create-fself create-gp4 PkgTool.Core; do
        if [ ! -f "$_OO_CACHE/$name" ]; then
            echo "ERROR: Failed to extract $name from archive." >&2
            echo "       Download $_OO_RELEASE_TAR manually" >&2
            echo "       and place bin/linux/{create-fself,create-gp4,PkgTool.Core} in $_OO_CACHE/" >&2
            ok=0
        fi
    done
    [ "$ok" -eq 1 ] || return 1
    echo ">>> OpenOrbis Linux binaries cached in $_OO_CACHE" >&2
}

_resolve_oo_bins

# Pick the binary source: native toolchain path (if available) or cache
_bin_src() {
    local name="$1"
    if [ -f "$T/bin/linux/$name" ] && [ -x "$T/bin/linux/$name" ]; then
        echo "$T/bin/linux/$name"
    else
        echo "$_OO_CACHE/$name"
    fi
}
_FSELF_SRC="$(_bin_src create-fself)"
_GP4_SRC="$(_bin_src create-gp4)"
_PKGTOOL_SRC="$(_bin_src PkgTool.Core)"

# On WSL, Linux binaries on NTFS (/mnt/c/...) fail silently with exit 255.
# Copy to a tmpfs directory so they execute reliably.
_BIN_TMPDIR=""
_need_copy=0
case "$_FSELF_SRC" in
    /mnt/[a-zA-Z]/*) _need_copy=1 ;;
esac
if [ "$_need_copy" -eq 1 ]; then
    _BIN_TMPDIR="$(mktemp -d)"
    cp "$_FSELF_SRC"   "$_BIN_TMPDIR/create-fself"
    cp "$_GP4_SRC"     "$_BIN_TMPDIR/create-gp4"
    cp "$_PKGTOOL_SRC" "$_BIN_TMPDIR/PkgTool.Core"
    chmod +x "$_BIN_TMPDIR/create-fself" "$_BIN_TMPDIR/create-gp4" "$_BIN_TMPDIR/PkgTool.Core"
fi
if [ -n "$_BIN_TMPDIR" ]; then
    FSELF="$_BIN_TMPDIR/create-fself"
    GP4="$_BIN_TMPDIR/create-gp4"
    _PKGTOOL_BIN="$_BIN_TMPDIR/PkgTool.Core"
else
    FSELF="$_FSELF_SRC"
    GP4="$_GP4_SRC"
    _PKGTOOL_BIN="$_PKGTOOL_SRC"
fi

# PkgTool.Core is a .NET binary; on systems without ICU installed it needs DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
# PkgTool.Core needs libssl.so.1.1 (OpenSSL 1.1); on systems with only OpenSSL 3 we
# look for a pre-extracted copy and add it to LD_LIBRARY_PATH automatically.
_SSL11_PATH=""
for _candidate in \
    /tmp/ssl11/extracted/usr/lib/x86_64-linux-gnu \
    /usr/lib/x86_64-linux-gnu \
    /lib/x86_64-linux-gnu; do
    if [ -f "$_candidate/libssl.so.1.1" ]; then
        _SSL11_PATH="$_candidate"
        break
    fi
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

echo ">>> create-fself: $FSELF" >&2
"$FSELF" -in="$ELF" -out="$STAGE/app.oelf" --eboot "$IMG/eboot.bin" --paid "$PAID" 2>&1 || {
    _rc=$?
    echo "ERROR: create-fself failed (exit $_rc)" >&2
    echo "DT_NEEDED entries in ELF (must not be empty):" >&2
    readelf -d "$ELF" 2>/dev/null | grep NEEDED >&2 || echo "  (none — link problem)" >&2
    exit $_rc
}
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
