#!/bin/sh
# Developer loop: builds the plugin and installs it with a local libfpv build.
#   ./deploy.sh                       # libfpv from ~/Projects/libfpv/build/mac
#   LIBFPV_BUILD=~/Projects/libfpv/build/mac-kamikaze ./deploy.sh
# Restart the game afterwards: dylibs are loaded once, at launch.
set -e
cd "$(dirname "$0")"
LIBFPV_DIR="${LIBFPV_DIR:-$HOME/Projects/libfpv}"
LIBFPV_BUILD="${LIBFPV_BUILD:-$LIBFPV_DIR/build/mac}"
cmake --build build -j
LIBFPV_DYLIB="$LIBFPV_BUILD/libfpv.dylib" LIBFPV_LICENSE="$LIBFPV_DIR/LICENSE" ./install.sh "$@"
