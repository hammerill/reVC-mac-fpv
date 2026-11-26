#!/bin/sh
# Developer loop: builds the plugin and installs it with a local libfpv build.
#   ./deploy.sh                       # libfpv from ~/Projects/libfpv/build/mac
#   LIBFPV_BUILD=/path/to/libfpv/build ./deploy.sh
# On a machine with LIBFPV_KAMIKAZE_DEV=1 (or true) local builds use
# libfpv's build/mac-kamikaze and build the plugin to match (libfpv AGENTS.md).
# Restart the game afterwards: dylibs are loaded once, at launch.
set -e
cd "$(dirname "$0")"
LIBFPV_DIR="${LIBFPV_DIR:-$HOME/Projects/libfpv}"
case "${LIBFPV_KAMIKAZE_DEV:-}" in
    1|true) DEV=ON; DEFAULT_BUILD="$LIBFPV_DIR/build/mac-kamikaze" ;;
    *) DEV=OFF; DEFAULT_BUILD="$LIBFPV_DIR/build/mac" ;;
esac
LIBFPV_BUILD="${LIBFPV_BUILD:-$DEFAULT_BUILD}"
cmake -S . -B build -DLIBFPV_DIR="$LIBFPV_DIR" -DREVC_FPV_KAMIKAZE=$DEV >/dev/null
cmake --build build -j
LIBFPV_DYLIB="$LIBFPV_BUILD/libfpv.dylib" LIBFPV_LICENSE="$LIBFPV_DIR/LICENSE" ./install.sh "$@"
