#!/bin/sh
# Saves the changes made in build/re3 into web/re3-web.patch.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here/../build/re3"
git add -A src vendor/librw
git diff --cached HEAD > "$here/re3-web.patch"
echo "web/re3-web.patch: $(grep -c '^diff --git' "$here/re3-web.patch") files"
