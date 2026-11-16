#!/bin/sh
# build-hpkg-arm64.sh — builds powergram-<version>-<revision>-arm64.hpkg on
# Linux, with Haiku's cross toolchain and the libraries tools/haiku-arm64 builds.
#
#   sh tools/haiku-arm64/make-sysroot.sh ~/.cache/haiku-arm64    (once, ~20 min)
#   . ~/.cache/haiku-arm64/env.sh
#   sh tools/haiku-arm64/build-deps.sh                             (once, ~15 min)
#   sh packaging/build-hpkg-arm64.sh
#
# The same package as build-hpkg.sh, except:
#  - TDLib, OpenSSL, zlib, Opus and libogg are linked in statically: HaikuPorts
#    has no arm64 repository, so the package can require only Haiku itself.
#    Emoji need noto_emoji, which the app asks for when it's missing.
#  - Haiku's host mimeset can't read arm64 binaries, so the attributes it
#    would set (type, signature) are written with the host addattr.
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
. ./haiku-app.env

ARCH=arm64
OUT="$ROOT/${APP_CMD}-${VERSION}-${REVISION}-${ARCH}.hpkg"

[ -n "${HAIKU_CROSS_CFLAGS:-}" ] || {
	echo "build-hpkg-arm64: source the env.sh from tools/haiku-arm64/make-sysroot.sh first" >&2; exit 1; }
for tool in aarch64-unknown-haiku-g++ rc xres package addattr; do
	command -v "$tool" >/dev/null 2>&1 || { echo "build-hpkg-arm64: '$tool' not found" >&2; exit 1; }
done
[ -e "$HAIKU_ARM64/deps/lib/libtdcore.a" ] || {
	echo "build-hpkg-arm64: run tools/haiku-arm64/build-deps.sh first" >&2; exit 1; }

# 1. Build, stripped at link time (xres appends the resources after the ELF data)
make clean >/dev/null
make HAIKU_CROSS=1 LDFLAGS="${LDFLAGS:-} -s"
for bin in "$APP_NAME" obj/PowergramDeskbar; do
	case "$(od -An -tx1 -j18 -N1 "$bin" | tr -d ' ')" in
		b7) ;;
		*) echo "build-hpkg-arm64: $bin is not an aarch64 binary" >&2; exit 1 ;;
	esac
done

# The package; haiku >= r1~beta6 is satisfied by the arm64 nightlies
# (r1~beta6_hrevNNNNN). The host mimeset can't read arm64 binaries.
PKG="$ROOT/obj/hpkg-$ARCH"
REQ=""
set_attrs() {
	addattr -t mime BEOS:TYPE application/x-vnd.Be-elfexecutable "$1"
	addattr -t mime BEOS:APP_SIG "$APP_SIG" "$1"
}
. packaging/hpkg-tree.sh
# The arm64 objects would otherwise be picked up by a native build or make test
make clean >/dev/null
echo "Built $OUT"
