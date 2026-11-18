#!/bin/bash
# Copy a WSL vend tree to a Windows folder for the recompiled exe.
# Win32 can't open WSL symlinks: file links become copies, directory links are listed in
# <dst>/.junctions ("link<TAB>target", both relative to vend) for fixlinks.ps1 to make junctions.
# usage (root in WSL): bash exportvend.sh <work>/vend-old.pristine <port>/vend-152
set -eu
SRC=$(readlink -f "${1:?src}"); DST=${2:?dst}
# absolute links point into /vend: resolve them with SRC bound on /vend in a private mount namespace
if [ -z "${EXPORTVEND_NS:-}" ]; then
  exec env EXPORTVEND_NS=1 unshare -m --propagation private bash "$0" "$@"
fi
mkdir -p /vend 2>/dev/null || true
mount --bind "$SRC" /vend
mkdir -p "$DST"
rsync -a --no-links --delete --exclude .junctions "$SRC/" "$DST/"
: > "$DST/.junctions"
cd "$SRC"
find . -type l -printf '%P\n' | sort | while read -r l; do
  t=$(readlink -f "$l") || { echo "dangling: $l"; continue; }
  case $t in "$SRC"/*) rel=${t#"$SRC"/};; /vend/*) rel=${t#/vend/};; *) echo "outside vend: $l -> $t"; continue;; esac
  t=$SRC/$rel
  if [ -d "$t" ]; then printf '%s\t%s\n' "$l" "$rel" >> "$DST/.junctions"
  else cp -p "$t" "$DST/$l"; fi
done
echo "exported $SRC -> $DST ($(wc -l < "$DST/.junctions") junctions)"
