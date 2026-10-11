#!/usr/bin/env bash
# A Quake 3 level (.pk3) to a playable Fruity Prime map, in one command and
# with nobody choosing anything along the way:
#
#   tools/convert-q3-map.sh LEVEL.pk3|LEVEL.zip [-map BSPNAME] [-name ROOM]
#                           [-overlay OVERLAY.json] [-noclip] [-nopaks] [-scale N]
#                           [-bin PATH/TO/FruityPrime] [-out DIR] [-maptest]
#
# What the level holds comes across by itself (-q3convert, then -mapgen):
#   - geometry and collision, curves included, clip brushes unless -noclip;
#   - textures: the level's own art first, then the game's texture paks it
#     was built against. Quake 3's own paks are id Software's and are not
#     fetched; OpenArena's (GPL) reuse most of the same names and are, from
#     Debian, into ~/.cache/fruityprime/openarena. A shader is resolved the
#     way Quake does -- its .shader script's first image, the image at its
#     name, its sky box -- and a lit surface that still has no image takes the
#     nearest-named image in the same folder ("stand-in" lines in the log);
#   - jump pads (trigger_push -> target), re-solved for this game's gravity;
#   - teleporters (trigger_teleport -> misc_teleporter_dest), as the
#     cartridge's multiplayer teleporter pads;
#   - spawns (info_player_deathmatch), and the level's pickups mapped onto
#     this game's: weapons, health, UA, missiles. Never a powerup: Double
#     Damage, Cloak and Deathalt are refused in custom maps;
#   - the bots' node graph, with every pad and teleporter joined to it as a
#     one-way route.
#
# What the level does not hold -- a race map has one start and no weapons --
# goes in an OVERLAY: a JSON object whose keys replace the generated
# recipe's ("spawns", "items", "inGameName", "pointLimit", ...). Keep it next
# to the level; running this again reproduces the same map from the two.
#
# Output: maps/<room>/ (recipe, level, .tex) and maps/<room>.fpmap, the one
# file a player or a server needs.
set -euo pipefail

usage() { sed -n '2,34p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

[ $# -ge 1 ] || usage
LEVEL=$(readlink -f "$1"); shift
MAP=""; ROOM=""; OVERLAY=""; NOCLIP=""; SCALE=""; BIN=""; OUT=""; MAPTEST=0; NOPAKS=0
while [ $# -gt 0 ]; do
    case "$1" in
        -map) MAP=$2; shift 2 ;;
        -name) ROOM=$2; shift 2 ;;
        -overlay) OVERLAY=$(readlink -f "$2"); shift 2 ;;
        -noclip) NOCLIP=-noclip; shift ;;
        -nopaks) NOPAKS=1; shift ;;
        -scale) SCALE=$2; shift 2 ;;
        -bin) BIN=$(readlink -f "$2"); shift 2 ;;
        -out) OUT=$(readlink -f "$2"); shift 2 ;;
        -maptest) MAPTEST=1; shift ;;
        *) usage ;;
    esac
done
[ -f "$LEVEL" ] || { echo "no such level: $LEVEL" >&2; exit 1; }
WORK=$(mktemp -d "${TMPDIR:-$HOME}/q3map.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

# Levels are often handed out as a .zip with the .pk3 and a readme inside.
case "$LEVEL" in
    *.zip|*.ZIP)
        unzip -q -o "$LEVEL" '*.pk3' -d "$WORK/zip"
        INNER=$(find "$WORK/zip" -iname '*.pk3' | head -1)
        [ -n "$INNER" ] || { echo "no .pk3 inside $LEVEL" >&2; exit 1; }
        LEVEL=$INNER ;;
esac

HERE=$(cd "$(dirname "$0")/.." && pwd)
if [ -z "$BIN" ]; then
    for candidate in "$HERE/build/linux/FruityPrime" "$HERE/FruityPrime"; do
        [ -x "$candidate" ] && { BIN=$candidate; break; }
    done
fi
[ -x "${BIN:-}" ] || { echo "no FruityPrime binary; pass -bin" >&2; exit 1; }
BINDIR=$(dirname "$BIN")
[ -f "$BINDIR/paths.txt" ] || { echo "$BINDIR/paths.txt is missing: the room binaries are built from the game files it names" >&2; exit 1; }

if [ -z "$MAP" ]; then
    MAP=$(unzip -Z1 "$LEVEL" | sed -n 's#^maps/\(.*\)\.bsp$#\1#p' | head -1)
fi
[ -n "$MAP" ] || { echo "no .bsp in $LEVEL" >&2; exit 1; }
ROOM=${ROOM:-$(echo "$MAP" | tr '[:lower:]' '[:upper:]')}
PREFIX=$(echo "$ROOM" | tr '[:upper:]' '[:lower:]')
MAPS=${OUT:-$BINDIR/maps}
mkdir -p "$MAPS"

# The texture paks, fetched only when the level's own archive does not paint
# it. OpenArena 0.8.5 from Debian: pak4-textures.pk3 holds the base_* sets,
# pak0.pk3 the shader scripts and the rest.
fetch_paks() {
    CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/fruityprime/openarena
    if [ ! -f "$CACHE/pak4-textures.pk3" ] || [ ! -f "$CACHE/pak0.pk3" ]; then
        echo "fetching OpenArena's texture paks (GPL) into $CACHE"
        mkdir -p "$CACHE/deb"
        POOL=https://deb.debian.org/debian/pool/main/o
        for deb in openarena-textures/openarena-081-textures_0.8.5split-17_all.deb \
                   openarena-data/openarena-data_0.8.5split-17_all.deb; do
            curl -fsSL -o "$CACHE/deb/$(basename "$deb")" "$POOL/$deb"
        done
        for deb in "$CACHE"/deb/*.deb; do
            dpkg-deb -x "$deb" "$CACHE/deb/x"
        done
        find "$CACHE/deb/x" -path '*baseoa/*.pk3' -exec cp {} "$CACHE/" \;
        rm -rf "$CACHE/deb"
    fi
    PAKS="$CACHE/pak4-textures.pk3,$CACHE/pak0.pk3"
}

run() { (cd "$BINDIR" && "$BIN" "$@"); }

# A bundle and a folder of the same name are the same map and the bundle
# wins, so last run's bundle would be what -mapgen builds.
rm -f "$MAPS/$PREFIX.fpmap"

convert() {
    run -q3convert "$LEVEL" -map "$MAP" -name "$ROOM" -out "$MAPS/$PREFIX" \
        ${PAKS:+-texpaks "$PAKS"} $NOCLIP ${SCALE:+-scale "$SCALE"} | tee "$WORK/convert.log"
}

echo "== convert $MAP from $(basename "$LEVEL") as $ROOM"
PAKS=""
convert
if [ "$NOPAKS" = 0 ] && grep -q "no image for" "$WORK/convert.log"; then
    echo "== the level does not carry every image it draws; again with the texture paks"
    fetch_paks
    convert
fi

RECIPE="$MAPS/$PREFIX/$PREFIX.json"
if [ -n "$OVERLAY" ]; then
    echo "== overlay $(basename "$OVERLAY")"
    python3 - "$RECIPE" "$OVERLAY" <<'PY'
import json, re, sys
recipe_path, overlay_path = sys.argv[1], sys.argv[2]
def load(path):
    text = open(path, encoding="utf-8").read()
    # Recipes allow // comments; strip the ones outside strings.
    text = re.sub(r'("(?:\\.|[^"\\])*")|//[^\n]*', lambda m: m.group(1) or "", text)
    return json.loads(text)
recipe, overlay = load(recipe_path), load(overlay_path)
lower = {k.lower(): k for k in recipe}
for key, value in overlay.items():
    if key.startswith("//"):
        continue
    if key.lower() == "import" and isinstance(value, dict):
        target = recipe[lower.get("import", "Import")]
        inner = {k.lower(): k for k in target}
        for k, v in value.items():
            target[inner.get(k.lower(), k)] = v
        continue
    recipe[lower.get(key.lower(), key)] = value
json.dump(recipe, open(recipe_path, "w", encoding="utf-8"), indent=2)
PY
fi

echo "== build"
FP_NODEDEBUG=1 run -mapgen "$ROOM" | tee "$WORK/build.log"
# The launcher's picture is taken from the recipe's camera; a fresh one has
# none, and -mapgen prints the one it would use.
PREVIEW=$(sed -n 's/^ *\("preview": {.*}\),$/{\1}/p' "$WORK/build.log" | head -1)
if [ -n "$PREVIEW" ]; then
    python3 - "$RECIPE" "$PREVIEW" <<'PY'
import json, re, sys
path, preview = sys.argv[1], json.loads(sys.argv[2])
text = re.sub(r'("(?:\\.|[^"\\])*")|//[^\n]*', lambda m: m.group(1) or "", open(path, encoding="utf-8").read())
recipe = json.loads(text)
if not any(k.lower() == "preview" for k in recipe):
    recipe.update(preview)
    json.dump(recipe, open(path, "w", encoding="utf-8"), indent=2)
PY
fi
echo "== check"
run -mapcheck "$ROOM" || true
echo "== bundle"
run -mapbundle "$ROOM" -mapdir "$MAPS" -out "$MAPS/$PREFIX.fpmap"
ls -la "$MAPS/$PREFIX.fpmap"
if [ "$MAPTEST" = 1 ]; then
    echo "== maptest"
    run -maptest "$ROOM" -players 8 -seconds 30
fi
