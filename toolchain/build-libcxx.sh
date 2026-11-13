#!/usr/bin/env bash
# Builds libc++ + libc++abi (LLVM 18.1.8) as static libraries for the PS4 target
# (x86_64-pc-freebsd12-elf, OpenOrbis musl libc). Needed because the OpenOrbis toolchain
# built from source has no C++ standard library (the prebuilt release, which ships one,
# could not be downloaded in our environment).
#
# Usage: OO_PS4_TOOLCHAIN=/path/to/oosdk toolchain/build-libcxx.sh [workdir]
# Output: toolchain/libcxx/{include/c++/v1, lib/libc++.a, lib/libc++abi.a}
#
# Decisions (see docs/COMPATIBILITY.md):
#   - exceptions OFF, localization/wide chars/filesystem OFF (no iostream, no std::filesystem)
#   - -U__FreeBSD__: OpenOrbis' libc is musl, not FreeBSD libc; libc++'s FreeBSD paths
#     (xlocale.h, sys/umtx.h, arc4random) do not exist there
#   - -nostdinc: never let host (Linux) headers leak into the target build
#   - overlay/math.h: OpenOrbis math.h defines C++ overloads (isinf...) that clash with
#     libc++ 18; the overlay switches it to the plain C macros.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
T="${OO_PS4_TOOLCHAIN:?set OO_PS4_TOOLCHAIN to the OpenOrbis toolchain}"
W="${1:-$here/.libcxx-work}"
OUT="$here/libcxx"
TAG=llvmorg-18.1.8
RES="$(clang -print-resource-dir)/include"

mkdir -p "$W"
if [ ! -d "$W/src/.git" ]; then
  git clone --depth 1 --branch "$TAG" --filter=blob:none --sparse https://github.com/llvm/llvm-project "$W/src"
  git -C "$W/src" sparse-checkout set libcxx libcxxabi libunwind runtimes cmake llvm/cmake llvm/utils/llvm-lit
fi

F="--target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -U__FreeBSD__ -D_DEFAULT_SOURCE -nostdinc \
   -isystem $here/overlay -isystem $T/include -isystem $RES"
rm -rf "$W/build" && mkdir -p "$W/build"
cmake -S "$W/src/runtimes" -B "$W/build" -G "Unix Makefiles" \
  -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS="$F" -DCMAKE_CXX_FLAGS="$F" \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_SYSTEM_NAME=FreeBSD -DCMAKE_CROSSCOMPILING=ON \
  -DCMAKE_INSTALL_PREFIX="$OUT" \
  -DLIBCXX_CXX_ABI=libcxxabi -DLIBCXX_HAS_MUSL_LIBC=ON \
  -DLIBCXX_ENABLE_SHARED=OFF -DLIBCXXABI_ENABLE_SHARED=OFF -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON \
  -DLIBCXX_ENABLE_EXCEPTIONS=OFF -DLIBCXXABI_ENABLE_EXCEPTIONS=OFF \
  -DLIBCXX_ENABLE_LOCALIZATION=OFF -DLIBCXX_ENABLE_WIDE_CHARACTERS=OFF -DLIBCXX_ENABLE_FILESYSTEM=OFF \
  -DLIBCXX_ENABLE_TIME_ZONE_DATABASE=OFF -DLIBCXX_HAS_PTHREAD_API=ON -DLIBCXXABI_HAS_PTHREAD_API=ON \
  -DLIBCXXABI_USE_LLVM_UNWINDER=OFF \
  -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXX_INCLUDE_BENCHMARKS=OFF -DLIBCXXABI_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF
make -C "$W/build" -j"$(nproc)" cxx cxxabi
make -C "$W/build" install-cxx install-cxxabi
ls -la "$OUT/lib"
