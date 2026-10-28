#!/bin/bash
# Installs reVC-fpv into the macOS version of GTA Vice City.
#
#   ./install.sh ["/path/to/GTA Vice City.app"]
#
# Copies reVC-fpv.dylib and libfpv.dylib into the app's Frameworks folder and
# makes the app's launcher script load the plugin. Run uninstall.sh to undo.
# The files are taken from next to this script (a release download) or, for
# developers, from build/ and $LIBFPV_DYLIB.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
APP="${1:-${GTAVC_APP:-/Applications/GTA Vice City.app}}"
FRAMEWORKS="$APP/Contents/Frameworks"
LAUNCHER="$APP/Contents/MacOS/reVC"
BACKUP="$LAUNCHER.fpv-backup"
MARK="Written by the reVC-fpv installer"

fail() { echo "error: $*" >&2; exit 1; }

find_file() {  # first existing path
    for candidate in "$@"; do
        if [ -n "$candidate" ] && [ -f "$candidate" ]; then echo "$candidate"; return; fi
    done
}
PLUGIN="$(find_file "$HERE/reVC-fpv.dylib" "$HERE/build/reVC-fpv.dylib")"
LIBFPV="$(find_file "${LIBFPV_DYLIB:-}" "$HERE/libfpv.dylib")"
LICENSE="$(find_file "$HERE/libfpv.LICENSE.txt" "${LIBFPV_LICENSE:-}")"

[ -n "$PLUGIN" ] || fail "reVC-fpv.dylib not found next to this script."
[ -n "$LIBFPV" ] || fail "libfpv.dylib not found next to this script (developers: set LIBFPV_DYLIB)."
[ -d "$APP" ] || fail "\"$APP\" not found. Pass the app's path: ./install.sh \"/path/to/GTA Vice City.app\""
[ -f "$LAUNCHER" ] || fail "\"$LAUNCHER\" not found. Is this the macOS reVC build of Vice City?"

# The launcher script runs the real game executable; find its name.
GAME_EXE="$(cd "$APP/Contents/MacOS" && for f in *; do
    [ -f "$f" ] && [ "$f" != reVC ] && file "$f" | grep -q 'Mach-O.*executable' && echo "$f" && break
done || true)"
[ -n "$GAME_EXE" ] || fail "couldn't find the game executable in \"$APP/Contents/MacOS\"."
file "$APP/Contents/MacOS/$GAME_EXE" | grep -q arm64 || fail "the game executable is not arm64."

echo "Installing into $FRAMEWORKS"
install_dylib() {  # source, name in Frameworks
    local name="$2"
    # cp onto a running copy would crash it; replace the file instead.
    rm -f "$FRAMEWORKS/$name"
    cp "$1" "$FRAMEWORKS/$name"
    # A downloaded copy carries a quarantine flag that would stop it from loading.
    xattr -d com.apple.quarantine "$FRAMEWORKS/$name" 2>/dev/null || true
    codesign --force --sign - "$FRAMEWORKS/$name" >/dev/null 2>&1 || true
}
install_dylib "$PLUGIN" reVC-fpv.dylib
install_dylib "$LIBFPV" libfpv.dylib
if [ -n "$LICENSE" ]; then cp "$LICENSE" "$FRAMEWORKS/libfpv.LICENSE.txt"; fi

if ! grep -q "$MARK" "$LAUNCHER" && [ ! -f "$BACKUP" ]; then
    cp "$LAUNCHER" "$BACKUP"
    echo "Saved the original launcher as $(basename "$BACKUP")"
fi

# Loads every plugin that is installed, so other plugins for this build of
# the game (such as Hammerill Trainer) keep working next to the drone.
cat > "$LAUNCHER" <<EOF
#!/bin/bash
# $MARK. The previous launcher is reVC.fpv-backup;
# run uninstall.sh (or copy it back) to remove the mod.
HERE="\$(cd "\$(dirname "\$0")" && pwd)"
cd "\$HERE/../Resources/"
INSERT=""
for plugin in reVC-fpv.dylib HammerillTrainerVC.dylib; do
    if [ -f "\$HERE/../Frameworks/\$plugin" ]; then
        INSERT="\${INSERT:+\$INSERT:}\$HERE/../Frameworks/\$plugin"
    fi
done
if [ -n "\$INSERT" ]; then
    export DYLD_INSERT_LIBRARIES="\$INSERT"
fi
exec "\$HERE/$GAME_EXE" "\$@"
EOF
chmod +x "$LAUNCHER"

echo "Done. Start GTA Vice City as usual, load a game and press F7."
