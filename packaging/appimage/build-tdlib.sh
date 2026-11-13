#!/bin/sh
# build-tdlib.sh PREFIX — TDLib's static libraries and headers for the Linux
# build, at the commit the Haiku packages use (HaikuPorts' tdlib
# 0.0.20260718; tools/haiku-arm64/build-deps.sh has the same TD_REV).
# Needs cmake, gperf, a C++17 compiler, OpenSSL and zlib headers. Takes
# 30-60 minutes; CI caches PREFIX by TD_REV.
set -eu
PREFIX=$1
TD_REV=022d60202e446ad1287b9fb68e687c8a0760788b
TD_SHA256=b0837cd880a6de8d45abdfd5024fe0f042c100eb5f241a5f185ba65579acfc32
JOBS=${JOBS:-$(nproc)}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

curl -fsSL "https://github.com/tdlib/td/archive/$TD_REV.tar.gz" -o "$WORK/td.tar.gz"
echo "$TD_SHA256  $WORK/td.tar.gz" | sha256sum -c -
mkdir "$WORK/td"
tar xf "$WORK/td.tar.gz" -C "$WORK/td" --strip-components=1
cmake -S "$WORK/td" -B "$WORK/build" -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$PREFIX" -DTD_ENABLE_LTO=OFF -DTD_ENABLE_JNI=OFF
cmake --build "$WORK/build" --target install -j"$JOBS"
ls "$PREFIX/lib/libtdcore.a" >/dev/null
echo "TDLib in $PREFIX"
