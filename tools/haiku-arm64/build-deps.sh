#!/bin/sh
# build-deps.sh — cross-builds the libraries Powergram links for Haiku arm64,
# where HaikuPorts has no packages yet: zlib, OpenSSL and TDLib, all static,
# so the app needs nothing beyond Haiku itself. (Opus and libogg, for voice
# messages, wait for Haiku arm64's Media Kit.)
#
#   sh tools/haiku-arm64/make-sysroot.sh ~/.cache/haiku-arm64    (once)
#   . ~/.cache/haiku-arm64/env.sh
#   sh tools/haiku-arm64/build-deps.sh                             (~15 min)
#
# Installs into $HAIKU_ARM64/deps. The same versions, checksums and Haiku
# patches as HaikuPorts' x86_64 packages (tdlib 0.0.20260718, openssl3 3.5.9);
# the patches are fetched from HaikuPorts' recipes.
# TDLib's generated sources are made first with a native build (it needs
# gperf on the host; one is built if missing).
set -eu
[ -n "${HAIKU_ARM64:-}" ] || { echo "build-deps: source env.sh from make-sysroot.sh first" >&2; exit 1; }
W=$HAIKU_ARM64
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$W/src; B=$W/build; DEPS=$W/deps; HOST=$W/host
JOBS=$(nproc 2>/dev/null || echo 4)
export TMPDIR=${TMPDIR:-$W/tmp}; mkdir -p "$TMPDIR" "$SRC/recipes" "$B" "$DEPS" "$HOST"
TARGET=aarch64-unknown-haiku
HP=https://raw.githubusercontent.com/haikuports/haikuports/master
say() { printf '\n== %s\n' "$*"; }

TD_REV=022d60202e446ad1287b9fb68e687c8a0760788b
fetch() {   # url file sha256
	[ -s "$SRC/$2" ] || curl -fsSL -o "$SRC/$2" "$1"
	echo "$3  $SRC/$2" | sha256sum -c --quiet
}
fetch https://github.com/tdlib/td/archive/$TD_REV.tar.gz tdlib.tar.gz \
	b0837cd880a6de8d45abdfd5024fe0f042c100eb5f241a5f185ba65579acfc32
fetch https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz \
	openssl.tar.gz 603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
fetch https://zlib.net/fossils/zlib-1.3.1.tar.gz zlib.tar.gz \
	9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
fetch https://ftp.gnu.org/gnu/gperf/gperf-3.1.tar.gz gperf.tar.gz \
	588546b945bba4b70b6a3a616e80b4ab466e3f33024a352fc2198112cdbb3ae2
for p in net-libs/tdlib/patches/tdlib-0.0.20260718.patchset \
		dev-libs/openssl/patches/openssl3-3.5.9.patchset; do
	[ -s "$SRC/recipes/${p##*/}" ] || curl -fsSL -o "$SRC/recipes/${p##*/}" "$HP/$p"
done

unpack() {  # archive dir [patchset]
	rm -rf "$B/$2"; mkdir -p "$B/$2"
	tar xf "$SRC/$1" -C "$B/$2" --strip-components=1
	[ -z "${3:-}" ] || ( cd "$B/$2" && patch -p1 -s < "$SRC/recipes/$3" )
}
CFLAGS_X="$HAIKU_CROSS_CFLAGS -O2 -fPIC"
LDFLAGS_X="$HAIKU_CROSS_LDFLAGS"

say zlib
unpack zlib.tar.gz zlib
( cd "$B/zlib" && CC=$CC CFLAGS="$CFLAGS_X" LDFLAGS="$LDFLAGS_X" CHOST=$TARGET \
	./configure --static --prefix="$DEPS" > /dev/null && make -j"$JOBS" install > /dev/null )

say OpenSSL
unpack openssl.tar.gz openssl openssl3-3.5.9.patchset
# And ours: no SIGILL feature probing, which panics Haiku arm64's kernel
( cd "$B/openssl" && patch -p1 -s < "$HERE/openssl-haiku-arm64.patch" )
mkdir -p "$B/openssl-conf"
# Haiku's own targets add "-include os-dep/haiku.h", a C header, to every
# compile; on aarch64 the assembly is preprocessed too, so this one guards it
cat > "$B/openssl-conf/haiku-aarch64.h" <<'EOF'
#ifndef __ASSEMBLER__
#include <sys/select.h>
#include <sys/time.h>
#endif
EOF
cat > "$B/openssl-conf/60-haiku-aarch64.conf" <<EOF
my %targets = (
    "haiku-aarch64" => {
        inherit_from     => [ "BASE_unix" ],
        CFLAGS           => picker(default => "-Wall", release => "-O2"),
        cflags           => add_before("-DL_ENDIAN -include $B/openssl-conf/haiku-aarch64.h",
                                       threads("-D_REENTRANT")),
        HASHBANGPERL     => "/bin/env perl",
        sys_id           => "HAIKU",
        ex_libs          => add("-lnetwork"),
        thread_scheme    => "pthreads",
        dso_scheme       => "dlfcn",
        bn_ops           => "SIXTY_FOUR_BIT_LONG RC4_CHAR",
        asm_arch         => 'aarch64',
        perlasm_scheme   => "linux64",
    },
);
EOF
# The certificates where Haiku keeps them, as HaikuPorts' openssl3 does
( cd "$B/openssl" && OPENSSL_LOCAL_CONFIG_DIR="$B/openssl-conf" CC=$CC \
	./Configure haiku-aarch64 --prefix="$DEPS" --libdir=lib --openssldir=/boot/system/data/ssl \
	no-shared no-tests no-apps no-docs no-module enable-zlib \
	--with-zlib-include="$DEPS/include" --with-zlib-lib="$DEPS/lib" \
	$HAIKU_CROSS_CFLAGS $HAIKU_CROSS_LDFLAGS > "$B/openssl-configure.log" \
	&& make -j"$JOBS" > "$B/openssl-build.log" 2>&1 && make install_sw > /dev/null ) \
	|| { tail -20 "$B/openssl-build.log" 2>/dev/null; exit 1; }

say "TDLib: generated sources (native)"
if ! command -v gperf > /dev/null; then
	if [ ! -x "$HOST/bin/gperf" ]; then
		unpack gperf.tar.gz gperf
		( cd "$B/gperf" && ./configure --prefix="$HOST" CXXFLAGS="-O2 -std=c++11" > /dev/null \
			&& make -j"$JOBS" install > /dev/null )
	fi
	export PATH="$HOST/bin:$PATH"
fi
unpack tdlib.tar.gz td tdlib-0.0.20260718.patchset
# A native build with the host compiler, outside the cross environment
( cd "$B/td" && mkdir native && cd native \
	&& env -u CC -u CXX cmake -DTD_GENERATE_SOURCE_FILES=ON .. > ../native.log 2>&1 \
	&& env -u CC -u CXX cmake --build . --target prepare_cross_compiling -j"$JOBS" >> ../native.log 2>&1 ) \
	|| { tail -20 "$B/td/native.log"; exit 1; }

say "TDLib (about 10 minutes)"
cat > "$B/haiku-arm64.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Haiku)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER $(command -v "$CC"))
set(CMAKE_CXX_COMPILER $(command -v "$CXX"))
set(CMAKE_SYSROOT $HAIKU_ARM64_SYSROOT)
set(CMAKE_C_FLAGS_INIT "$HAIKU_CROSS_CFLAGS")
set(CMAKE_CXX_FLAGS_INIT "$HAIKU_CROSS_CFLAGS")
set(CMAKE_EXE_LINKER_FLAGS_INIT "$HAIKU_CROSS_LDFLAGS -lnetwork")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "$HAIKU_CROSS_LDFLAGS -lnetwork")
set(CMAKE_FIND_ROOT_PATH $DEPS $HAIKU_ARM64_SYSROOT $HAIKU_ARM64_SYSROOT/develop)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
# The same options as HaikuPorts' recipe: the full library, then the e2e-only
# part it installs as well (libtde2e)
for e2e in OFF ON; do
	( cd "$B/td" && cmake -B build-$e2e -S . -DCMAKE_TOOLCHAIN_FILE="$B/haiku-arm64.cmake" \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS" \
		-DOPENSSL_ROOT_DIR="$DEPS" -DOPENSSL_USE_STATIC_LIBS=TRUE \
		-DZLIB_ROOT="$DEPS" -DZLIB_USE_STATIC_LIBS=ON \
		-DTD_INSTALL_STATIC_LIBRARIES=ON -DTD_INSTALL_SHARED_LIBRARIES=OFF \
		-DTD_ENABLE_JNI=OFF -DTD_ENABLE_DOTNET=OFF -DTD_ENABLE_LTO=OFF \
		-DTD_E2E_ONLY=$e2e -DBUILD_TESTING=OFF -DMEMPROF=OFF > "$B/td-cmake-$e2e.log" 2>&1 \
		&& cmake --build build-$e2e -j"$JOBS" > "$B/td-build-$e2e.log" 2>&1 \
		&& cmake --install build-$e2e > /dev/null ) \
		|| { tail -30 "$B/td-cmake-$e2e.log" "$B/td-build-$e2e.log" 2>/dev/null; exit 1; }
done

ls "$DEPS/lib"/*.a | sed 's|.*/||' | tr '\n' ' '; echo
say "done: $DEPS"
