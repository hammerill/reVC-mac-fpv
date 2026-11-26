#!/bin/sh
# Developer loop: builds the plugin and installs it with a local libfpv build.
#   ./deploy.sh                       # libfpv from ~/Projects/libfpv/build/mac
#   LIBFPV_BUILD=/path/to/libfpv/build ./deploy.sh
# On a machine with LIBFPV_KAMIKAZE_DEV=1 (or true) local builds use
# libfpv's build/mac-kamikaze and build the plugin to match (libfpv AGENTS.md).
# The plugin and libfpv must be of one kind (REVC_FPV_KAMIKAZE): fpv_frame's
# layout differs, so a mismatched pair is refused.
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
MOD_KAMIKAZE=$(sed -n 's/^REVC_FPV_KAMIKAZE:BOOL=//p' build/CMakeCache.txt)
LIB_KAMIKAZE=$(sed -n 's/^LIBFPV_KAMIKAZE:BOOL=//p' "$LIBFPV_BUILD/CMakeCache.txt" 2>/dev/null)
[ "${MOD_KAMIKAZE:-OFF}" = "${LIB_KAMIKAZE:-OFF}" ] || {
  echo "the plugin is built with REVC_FPV_KAMIKAZE=${MOD_KAMIKAZE:-OFF} but $LIBFPV_BUILD has LIBFPV_KAMIKAZE=${LIB_KAMIKAZE:-OFF}:" >&2
  echo "  cmake -S . -B build -DREVC_FPV_KAMIKAZE=${LIB_KAMIKAZE:-OFF} && cmake --build build" >&2
  exit 1; }
LIBFPV_DYLIB="$LIBFPV_BUILD/libfpv.dylib" LIBFPV_LICENSE="$LIBFPV_DIR/LICENSE" ./install.sh "$@"
