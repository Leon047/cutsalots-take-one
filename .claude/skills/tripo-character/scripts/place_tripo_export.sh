#!/bin/sh
# usage: place_tripo_export.sh <key> [zip]
# Unpacks a Tripo export zip to Art/Tripo/axolotl_<key>/rigged/ as <key>_rigged.fbx + <key>_rigged.fbm/.
# Without [zip] it takes the newest tripo_convert_*.zip from Downloads. An old export of the same key is replaced.
set -e
key="$1"
[ -n "$key" ] || { echo "usage: place_tripo_export.sh <key> [zip]"; exit 2; }
zip="${2:-$(ls -t "$USERPROFILE"/Downloads/tripo_convert_*.zip | head -1)}"
[ -f "$zip" ] || { echo "no zip: $zip"; exit 1; }
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
dst="$repo/Art/Tripo/axolotl_$key/rigged"
mkdir -p "$dst"
unzip -o -q "$zip" -d "$dst"
base=$(basename "$zip" .zip)
mv -f "$dst/$base.fbx" "$dst/${key}_rigged.fbx"
rm -rf "$dst/${key}_rigged.fbm"; mv "$dst/$base.fbm" "$dst/${key}_rigged.fbm"
# unzip keeps the times stored in the zip (another time zone); stage_status.sh needs the time of placing
touch "$dst/${key}_rigged.fbx" "$dst/${key}_rigged.fbm"/*
echo "$zip -> $dst"; ls -l "$dst" "$dst/${key}_rigged.fbm"
