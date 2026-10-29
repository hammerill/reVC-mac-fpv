#!/bin/bash
# Removes reVC-fpv from the macOS version of GTA Vice City.
#
#   ./uninstall.sh ["/path/to/GTA Vice City.app"]
#
# Your settings and radio setup are kept in ~/Library/Application Support/libfpv,
# which other libfpv games share; delete it (and Contents/Resources/fpv-data, from
# older versions) to remove everything.
set -euo pipefail

APP="${1:-${GTAVC_APP:-/Applications/GTA Vice City.app}}"
FRAMEWORKS="$APP/Contents/Frameworks"
LAUNCHER="$APP/Contents/MacOS/reVC"
BACKUP="$LAUNCHER.fpv-backup"

[ -d "$APP" ] || { echo "error: \"$APP\" not found." >&2; exit 1; }

for f in reVC-fpv.dylib libfpv.dylib libfpv.LICENSE.txt; do
    if [ -f "$FRAMEWORKS/$f" ]; then
        rm "$FRAMEWORKS/$f"
        echo "Removed $f"
    fi
done
rm -f "$APP/Contents/Resources/reVC-fpv.log"

# Hammerill Trainer still installed: the launcher keeps loading it.
if [ -f "$FRAMEWORKS/HammerillTrainerVC.dylib" ]; then
    rm -f "$BACKUP"
    echo "Kept the launcher for Hammerill Trainer."
elif [ -f "$BACKUP" ]; then
    mv "$BACKUP" "$LAUNCHER"
    chmod +x "$LAUNCHER"
    echo "Restored the original launcher."
fi
echo "reVC-fpv is uninstalled. Settings are still in ~/Library/Application Support/libfpv"
