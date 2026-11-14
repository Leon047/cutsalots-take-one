#!/bin/sh
# usage: stage_status.sh <key>
# Shows every pipeline stage of an existing character (file, time, git state) and names the first stale step.
# A stage is stale when its file is missing, is older than the stage before it, or the stage before it has
# uncommitted changes while this one has none. Times are only a hint after a fresh clone or a merge.
key="$1"
[ -n "$key" ] || { echo "usage: stage_status.sh <key>"; exit 2; }
Key=$(printf '%s' "$key" | sed 's/./\U&/')
cd "$(dirname "$0")/../../../.." || exit 1

ref="Docs/References/FurryAxolotlClasses/$key.png"
tpose="Docs/References/FurryAxolotlClasses/tripo_input/${key}_front.png"
export="Art/Tripo/axolotl_$key/rigged/${key}_rigged.fbx"
blender="Art/Tripo/axolotl_$key/SK_Axolotl$Key.fbx"
mesh="UE/Content/Characters/axolotl_$key/Axolotl_$Key.uasset"
clip="UE/Content/Characters/axolotl_$key/Axolotl_${Key}CharacterArmature_Idle.uasset"
review="Art/Tripo/axolotl_$key/review_day.png"

mt() { [ -e "$1" ] && stat -c %Y "$1" || echo 0; }
gs() {
    [ -e "$1" ] || { echo missing; return; }
    s=$(git status --porcelain -- "$1" | cut -c1-2)
    [ -z "$s" ] && echo clean || echo "changed($s)"
}
row() {
    t=$(mt "$2"); d="-"; [ "$t" != 0 ] && d=$(date -d "@$t" '+%Y-%m-%d %H:%M')
    printf '%-22s %-16s %-12s %s\n' "$1" "$d" "$(gs "$2")" "$2"
}

row "0 reference"      "$ref"
row "1 T-pose"         "$tpose"
row "2 Tripo export"   "$export"
row "3 Blender output" "$blender"
row "4 UE mesh"        "$mesh"
row "4 UE clips"       "$clip"
row "6 review still"   "$review"

if grep -q "Axolotl$Key{" UE/Source/TheSagaCutter/TSCUnit.cpp; then
    echo "5 C++ routing          present: $(grep -n "Preferred = &Axolotl$Key;" UE/Source/TheSagaCutter/TSCUnit.cpp | head -1)"
else
    echo "5 C++ routing          MISSING"
fi

zip=$(ls -t "$USERPROFILE"/Downloads/tripo_convert_*.zip 2>/dev/null | head -1)
if [ -n "$zip" ]; then
    echo "newest Tripo zip:      $(date -d "@$(mt "$zip")" '+%Y-%m-%d %H:%M')  $zip"
    [ "$(mt "$zip")" -gt "$(mt "$export")" ] && echo "  (newer than the placed export: it may be this character's new export)"
fi

# first stale step: output of step N is $2, its input is $1
stale=""
check() {
    [ -n "$stale" ] && return
    in="$1"; out="$2"; step="$3"
    if [ ! -e "$out" ]; then stale="$step (missing $out)"; return; fi
    [ -e "$in" ] || return
    if [ "$(mt "$in")" -gt "$(mt "$out")" ]; then stale="$step ($in is newer than $out)"; return; fi
    if [ "$(gs "$in")" != clean ] && [ "$(gs "$out")" = clean ]; then stale="$step ($in changed, $out did not)"; fi
}
check "$ref"     "$tpose"   1
check "$tpose"   "$export"  2
check "$export"  "$blender" 3
check "$blender" "$mesh"    4
check "$mesh"    "$clip"    4
check "$clip"    "$review"  6
echo
echo "first stale step: ${stale:-none (everything is up to date)}"
