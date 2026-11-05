#!/bin/sh
# Prepares the reVC source for the web build in build/re3: a copy of your
# reVC (re3, miami branch) checkout with web/re3-web.patch applied.
#
#   web/setup.sh [path to the reVC checkout]    (default ~/Projects/re3-miami)
#
# The checkout must have its generated shaders (src/extras/shaders/obj), as
# a normal reVC build tree does. After editing build/re3, save the changes
# back into the patch with web/tools/save_patch.sh.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=${1:-$HOME/Projects/re3-miami}
dst="$here/../build/re3"
if [ ! -f "$src/src/core/config.h" ]; then
  echo "No reVC source in $src" >&2
  exit 1
fi
if [ ! -d "$src/src/extras/shaders/obj" ]; then
  echo "$src has no src/extras/shaders/obj (generated shaders); build reVC there once, or copy them in" >&2
  exit 1
fi
if [ -e "$dst" ]; then
  echo "$dst exists; delete it first to start over" >&2
  exit 1
fi
mkdir -p "$(dirname "$dst")"
git clone -q "$src" "$dst"
cp -r "$src/src/extras/shaders/obj" "$dst/src/extras/shaders/"
cd "$dst"
git checkout -q -b web
git apply --whitespace=nowarn "$here/re3-web.patch"
echo "reVC for the web is in $dst"
