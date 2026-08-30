# Maps

No level lives here, only recipes: the game ships the 27 multiplayer rooms of
the cartridge and nothing else. What lives here is the *hook* for custom ones —
drop a `.json` in, or a folder with a `.json` in it, and the game builds it and
lists it. A map whose source level is not on this machine is left out rather
than listed and broken.

Each `.json` describes one custom room. The binaries a room is actually made
of are generated on the machine that runs the game, from that machine's own
extracted files, because a room's textures are borrowed from a shipped room
and end up inside its model file. That is why no `.bin` is ever committed, and
why the asset guard refuses one.

- `q3dm17.json.example` — rename to `q3dm17.json` and drop your own `pak0.pk3`
  beside it to convert the original Quake III map. Nothing here downloads it
  for you: that is id Software's commercial data.
- `dust2/dust2.json` — de_dust2, by way of the DeFRaG level `df_dust2`. Put
  your own `df_dust2.pk3` in `dust2/` and the room appears. A map may keep its
  level and its baked textures in a folder of its own like this; both are
  looked for beside the map file first.

`.gitignore` refuses a `.pk3`, a `.bsp` and a `.tex` by extension, for the same
reason it refuses a cartridge dump. The `.json` is the part that travels.

The format, the Quake 3 importer and the traps are in
`../.claude/mapgen/MAP-PIPELINE.md`. `FruityPrime -mapgen` builds every map in
this folder; `FruityPrime -mapmaterials "MP3 PROVING GROUND"` prints the
textures a shipped room can lend.
