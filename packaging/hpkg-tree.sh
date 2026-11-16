# hpkg-tree.sh — the package tree, .PackageInfo and package that build-hpkg.sh
# and build-hpkg-arm64.sh share. Sourced from the project root after
# haiku-app.env, with ARCH, OUT, PKG and REQ (extra requires, one per line,
# each starting with a newline and a tab) set, and set_attrs, which gives the
# app its type and signature.

# 2. Package tree, relative to the install location (/boot/system)
rm -rf "$PKG"
mkdir -p "$PKG/apps" "$PKG/bin" "$PKG/data/deskbar/menu/Applications" \
	"$PKG/documentation/packages/$APP_CMD"
cp "$APP_NAME" "$PKG/apps/$APP_NAME"
set_attrs "$PKG/apps/$APP_NAME"
ln -s "../apps/$APP_NAME" "$PKG/bin/$APP_CMD"
ln -s "../../../../apps/$APP_NAME" \
	"$PKG/data/deskbar/menu/Applications/${DESKBAR_NAME:-$APP_NAME}"
cp README.md LICENSE "$PKG/documentation/packages/$APP_CMD/" 2>/dev/null || :
# Other files the app needs (haiku-app.env: "built-file:path-in-package").
for pair in ${HPKG_EXTRA_FILES:-}; do
	from=${pair%%:*}; to=${pair#*:}
	mkdir -p "$PKG/$(dirname "$to")"
	cp "$from" "$PKG/$to"
done

# 3. .PackageInfo
DESC=$(sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e '2,$s/^/\t/' "$DESCRIPTION_FILE")
# Packages this one takes over (an earlier name of the app).
REPLACES=""
if [ -n "${HPKG_REPLACES:-}" ]; then
	REPLACES="replaces		{"
	for r in $HPKG_REPLACES; do REPLACES="$REPLACES
	$r"; done
	REPLACES="$REPLACES
}"
fi
cat > "$PKG/.PackageInfo" <<EOF
name			$APP_CMD
version			$VERSION-$REVISION
architecture	$ARCH
summary			"$SUMMARY"
description		"$DESC"
packager		"$PACKAGER"
vendor			"$APP_NAME"
copyrights		{
	"$COPYRIGHT"
}
licenses		{
	"$LICENSE_NAME"
}
urls			{
	"$HOMEPAGE"
}
provides		{
	$APP_CMD = $VERSION
	app:$APP_NAME = $VERSION
	cmd:$APP_CMD
}
requires		{
	haiku >= r1~beta6$REQ
}
$REPLACES
EOF

# 4. Package. The licence must be one Haiku knows or one shipped in the
#    package; `package create` says which when it isn't.
rm -f "$OUT"
package create -C "$PKG" "$OUT"
package list -i "$OUT" >/dev/null
