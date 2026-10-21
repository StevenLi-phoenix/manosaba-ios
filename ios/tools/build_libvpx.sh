#!/bin/bash
# libvpx for the iOS app (plan item 62b: VP9 movies; Tears of the Kingdom's 28 cutscenes are WebM VP9). The VP8 / VP9 decoders of libvpx 1.17.0,
# static, arm64, iOS 18.0 and later (both the iOS 26 build and the Metal 3 iOS 18 one), into $DM_ROOT/build/ios/libvpx; ios/run.sh links it when
# it is there (ios/tools/vpx_check.py checks it first). The source archive is pinned by its SHA-256 (the same value totk-mobile pins for this tag,
# 1020f184...); GirlNet removed VideoToolbox's VP9 decoder from its iOS build after it faulted at a movie's start (76ca341), so libvpx it is.
# Usage: DM_ROOT=<workspace> ios/tools/build_libvpx.sh      (again: rebuilds)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${DM_ROOT:-$HOME/dm-work}
VER=1.17.0
SHA=1020f184046187baa2985dbde38e0691f49c44088bca7a1842b0236c6081dc0a
DEPS=$ROOT/build/ios/deps; OUT=$ROOT/build/ios/libvpx
mkdir -p "$DEPS"
A=$DEPS/libvpx-$VER.tar.gz
[ -f "$A" ] || { curl --fail --location --retry 2 --max-time 300 "https://github.com/webmproject/libvpx/archive/refs/tags/v$VER.tar.gz" -o "$A.part"; mv "$A.part" "$A"; }
GOT=$(shasum -a 256 "$A" | cut -d' ' -f1)
[ "$GOT" = "$SHA" ] || { echo "[red] $A: SHA-256 $GOT, not the pinned $SHA: refused (delete it to fetch again)"; exit 1; }
# configure does not take a path with a space (the repository's has one): unpacked, built and installed in a temporary directory, then copied
W=$(mktemp -d "${TMPDIR:-/tmp}/dm-libvpx.XXXXXX"); trap 'rm -rf "$W"' EXIT
case "$W" in *" "*) echo "[red] the temporary directory $W has a space: set TMPDIR"; exit 1;; esac
tar -xzf "$A" -C "$W"; SRC=$W/libvpx-$VER; mkdir -p "$W/build"; cd "$W/build"
# configure.sh fixes the iOS minimum of a static build at 7.0 (IOS_VERSION_MIN from the environment is overwritten) and adds
# -miphoneos-version-min=7.0; against our -target arm64-apple-ios18.0 its -Werror feature checks then failed ("overriding
# '-miphoneos-version-min=7.0' option"), which turned NEON off and left -DNDEBUG -O3 out: a C-only, unoptimized decoder with assertions, 3 fps
# on a 1080p cutscene on an iPhone 17 Pro (plan item 62b2). The minimum is set to ours, and the build stops unless NEON and -DNDEBUG -O3 came out on
sed -i '' 's/IOS_VERSION_MIN="7.0"/IOS_VERSION_MIN="18.0"/' "$SRC/build/make/configure.sh"
grep -q 'IOS_VERSION_MIN="18.0"' "$SRC/build/make/configure.sh" || { echo "[red] libvpx $VER: the iOS minimum in configure.sh is not where it was: check it"; exit 1; }
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
export CC="$(xcrun --sdk iphoneos --find clang)" CXX="$(xcrun --sdk iphoneos --find clang++)" IOS_VERSION_MIN=18.0
export CFLAGS="-target arm64-apple-ios18.0 -isysroot $SDK" CXXFLAGS="-target arm64-apple-ios18.0 -isysroot $SDK" LDFLAGS="-target arm64-apple-ios18.0 -isysroot $SDK"
"$SRC/configure" --target=arm64-darwin-gcc --prefix="$W/install" --disable-examples --disable-tools --disable-docs --disable-unit-tests \
  --disable-shared --enable-static --disable-vp8-encoder --disable-vp9-encoder > configure.log 2>&1 || { tail -20 configure.log; exit 1; }
grep -q '^#define HAVE_NEON 1' vpx_config.h || { echo "[red] configure turned NEON off:"; grep -B2 -A2 -i 'neon' config.log | tail -12; exit 1; }
grep -q -- '-DNDEBUG -O3' libs-*.mk || { echo "[red] configure left NDEBUG or -O3 out (assertions compiled in, not optimized)"; exit 1; }
make -j"${DM_JOBS:-4}" > make.log 2>&1 || { tail -30 make.log; exit 1; }
make install > install.log 2>&1 || { tail -20 install.log; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT"; cp -R "$W/install/include" "$W/install/lib" "$OUT/"
python3 "$HERE/vpx_check.py" "$OUT/lib/libvpx.a" 18.0
echo "libvpx $VER for iOS: $OUT"
