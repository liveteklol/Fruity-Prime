# Maps

This folder is empty of maps on purpose: the game ships the 27 multiplayer
rooms of the cartridge and nothing else. What lives here is the *hook* for
custom ones — drop a `.json` in and the game builds it and lists it.

Each `.json` describes one custom room. The binaries a room is actually made
of are generated on the machine that runs the game, from that machine's own
extracted files, because a room's textures are borrowed from a shipped room
and end up inside its model file. That is why no `.bin` is ever committed, and
why the asset guard refuses one.

- `q3dm17.json.example` — rename to `q3dm17.json` and drop your own `pak0.pk3`
  beside it to convert the original Quake III map. Nothing here downloads it
  for you: that is id Software's commercial data.

The format, the Quake 3 importer and the traps are in
`../.claude/mapgen/MAP-PIPELINE.md`. `FruityPrime -mapgen` builds every map in
this folder; `FruityPrime -mapmaterials "MP3 PROVING GROUND"` prints the
textures a shipped room can lend.
