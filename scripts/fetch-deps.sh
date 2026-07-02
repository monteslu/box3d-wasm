#!/usr/bin/env bash
# Fetch upstream Box3D source at the SHA pinned in scripts/versions.json.
# Idempotent: skips the fetch when deps/box3d is already at the pin.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ROOT="$(dirname "$DIR")"

SHA="$(node -p "JSON.parse(require('fs').readFileSync('$DIR/versions.json','utf8')).box3d.sha")"
REPO="$(node -p "JSON.parse(require('fs').readFileSync('$DIR/versions.json','utf8')).box3d.repo")"
DEST="$ROOT/deps/box3d"

if [ -d "$DEST/.git" ] && [ "$(git -C "$DEST" rev-parse HEAD 2>/dev/null)" = "$SHA" ]; then
  echo "box3d already at $SHA"
  exit 0
fi

rm -rf "$DEST"
mkdir -p "$DEST"
git -C "$DEST" init -q
git -C "$DEST" remote add origin "$REPO"
git -C "$DEST" fetch -q --depth 1 origin "$SHA"
git -C "$DEST" checkout -q FETCH_HEAD
echo "box3d fetched at $SHA"
