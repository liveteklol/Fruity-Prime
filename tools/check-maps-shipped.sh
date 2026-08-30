#!/usr/bin/env bash
# Fail if a custom map would reach a player without the level it converts.
#
# A map in maps/ is two files: the recipe, and the level. The recipe alone
# registers a room that the game then declines to build -- the player sees the
# 27 cartridge rooms and no sign that anything was meant to be there. That is
# the right behaviour for a level nobody may publish, and a silent regression
# for one we ship on purpose, which is why it is checked rather than assumed.
#
#   tools/check-maps-shipped.sh                    # the repository
#   tools/check-maps-shipped.sh publish/win-x64    # a build we are about to release
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

root="${1:-}"
if [ -n "$root" ]; then
  maps="$root/maps"
  what="$root"
else
  maps="maps"
  what="the repository"
fi

fail=0
found=0
if [ ! -d "$maps" ]; then
  echo "no maps/ in $what; nothing to check"
  exit 0
fi

# The level a map converts is named by import.source. Read it without a JSON
# parser: the field is one line in every file this writes, and a dependency on
# jq for a five-line check is worse than a grep.
while IFS= read -r file; do
  found=$((found + 1))
  name=$(basename "$file")
  source=$(grep -oiE '"source"[[:space:]]*:[[:space:]]*"[^"]+"' "$file" \
    | head -n1 | sed -E 's/.*"source"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/')
  if [ -z "$source" ]; then
    echo "ok:      $name builds from its own description, no level needed"
    continue
  fi
  if [ -f "$(dirname "$file")/$source" ]; then
    echo "ok:      $name has $source beside it"
  else
    echo "MISSING: $name converts $source, which is not in $(dirname "$file")"
    fail=1
  fi
done < <(find "$maps" -name '*.json' -not -name '*.example' | sort)

if [ "$found" -eq 0 ]; then
  echo "no maps in $what"
  exit 0
fi
if [ "$fail" -ne 0 ]; then
  echo
  echo "A map whose level is absent is left out of the room list at startup."
  echo "Either ship the level beside it, or take the map file out."
  exit 1
fi
echo "every map in $what has what it needs"
