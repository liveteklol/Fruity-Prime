# Multiplayer — match end, rotation and the double-counted kill

A match that somebody won used to end the session for that client alone:
`GameState.ProcessFrame` ran the winner's camera, then the scoreboard, then
faded to black -- correct offline, and on a server it meant every client
dropped back to its own launcher while the server's rotation, which had never
heard anybody won, kept counting down a map nobody was still playing.

| Piece | What |
|---|---|
| `NetMatchEnd` | on the authority, sends `PacketType.MatchEnd` when `GameState.MatchState` leaves `InProgress`, repeating until the server's own state answers `FlagEnding`. Any client that sees `FlagEnding` while it still thinks the match is running sets `MatchTime = 0`, so results play out normally rather than cutting away |
| `DedicatedServer` intermission | both endings (clock and score) now enter the same 9-second intermission before `AdvanceMap` -- the client's own sequence (3 s winner's camera, 5 s scoreboard) plus a second, so the fade belongs to the rotation instead of cutting the results short |
| `MatchStatePacket.MatchId` | counts matches from the server's start. The room key alone can't answer "is this a new match" -- a one-map rotation (what **Host a game** builds) plays the same room repeatedly, and a client watching only the name sat on its results screen for the rest of the session |
| `MatchStatePacket.PointGoal` | the score that wins, from the rotation file; belongs to the server for the same reason the clock does |

Two things this needed underneath:

- **`NetMatchSync` must not adopt the clock during results.** `MatchTime` is
  the countdown the results sequence itself runs on; putting the old map's
  remaining time back on top of it every frame meant the sequence never
  finished.
- **The authority must stop reporting once the server has heard.** A client
  lingers in results for a second or two after the server has rotated; during
  that window the authority was reporting the end of the *new* match the
  instant rotation landed -- one whole map skipped per rotation, visible as
  two `match over` lines in the server log in the same second.

## The double-counted kill (2026-08-23)

Reported as "6-2 on the scoreboard and the match was already over for the
second client" -- which then had to be hunted down as a motionless zombie and
killed to reach 7, and crashed at the rotation that followed.

**A replayed kill was counted twice.** `NetDamage.Replay` runs the authority's
hit through `PlayerEntity.TakeDamage` so the victim feels it (indicator,
flinch, banner) -- but `TakeDamage` also *awards* the kill (`Points`/`Kills`
for the attacker, `Deaths` for the victim). The authority had already done all
three and the snapshot being applied already carried the result, so on every
machine except the authority the kill landed on the scoreboard twice, for one
frame -- enough, because `EndIfPointGoalReached` reads `TeamPoints`, and
`UpdateState` rebuilds `TeamPoints` from `Points` at the end of the same
frame, after the replay and before the next snapshot corrects it. On the sixth
kill of a seven-point match the client saw seven, ended its match, and sat on
`6-2` for the rest of the round while its player stood frozen in everyone
else's game (`UpdateScene` doesn't run outside `MatchState.InProgress`).
Reproduced exactly: one client at `matchState=GameOver score=1/2p` while the
authority was still at `score=1/1p`, 23 seconds apart.

Fix is **not** "assign the authority's scores after the replay instead of
before" -- that looks equivalent and isn't, since the replay runs on the
victim's slot and moves the *attacker's* row, so it only works if the attacker
happens to apply after the victim. `NetDamage` saves and restores the three
score arrays around the replay instead, so ordering stops mattering.

**Then the rotation exposed the same bug from a different angle.** A client
resets its scores on loading the next room, but the two machines don't change
room on the same frame -- so a client that finished loading first was still
receiving the *finished* match's snapshots, carrying the winning scores, and
applied them to a fresh match at zero. `ApplyState` now ignores peer scores
while `NetRoomChange.Settling`, for the same reason it already ignores peer
positions there: a match starts at zero on every machine, so there's nothing
to learn from the authority during that second.

**The rule that makes a third variant of this survivable:** only the machine
that keeps score may decide the score has been reached
(`NetMatchEnd.MayEndOnScore`, checked by `EndIfPointGoalReached` and
`ModeStateDefender`). Every other client's `Points` comes from the authority's
snapshot, so a client reaching the goal "first" is disagreeing with the only
copy of the scoreboard that counts, and nothing returns `MatchState` to
`InProgress` until the next rotation.

`NetMatchEnd.RecoverIfStranded` is the backstop under all of it: a client
showing results for twelve seconds while the server says the match is running
(not ending) is put back into play. Twelve is longer than the whole results
sequence and the server's own intermission, so a legitimate ending is never
cut short, and the next bug of this shape costs a hiccup instead of a player.

## The results screen flying the previous map's camera (2026-09-06)

**The crash after rotation, explained and fixed.** It was one static:
`CameraSequence.Intro`, the multiplayer intro fly-through. `SceneSetup` loads
it when a room is loaded from scratch and **nothing reloaded it for a
transition**, so after the first rotation it still held the sequence belonging
to the map the session started on -- for the rest of the session, every map.

That only bites at a match *end*, which is why it hid for so long.
`GameState.EnsureIntroCamSeq` runs the intro on a loop for the results screen
(`MatchState.GameOver` with no winner to watch, and all of `Ending`), and
`SetUp` writes the first keyframe's `NodeRef` straight into the main player's
`CameraInfo`. Keyframe node refs are resolved by node *name* against whatever
room was loaded when `Initialize` last ran, so what the camera got handed was
a part and a node of a room that is no longer in memory, and
`RoomEntity.UpdateRoomParts` walks the **new** room's portal graph from it.

Both of the reported symptoms are that one ref, and which one you get is only
which way the rotation went:

| | What the stale indices do | What a player sees |
|---|---|---|
| big map -> small map | out of range | `ArgumentOutOfRangeException` in `RoomEntity.DrawRoomParts` at `Model.Nodes[nodeIndex]`, and the process is gone. Measured: MP1 SANCTORUS part 5 node 75, in a MP3 PROVING GROUND with 2 parts and 27 nodes -- **all three clients died within a second of each other**, at the end of the first rotated match |
| small map -> big map | quietly in range | the room is drawn from a part the camera is not in, so it comes out black, and every other player is culled against the same wrong active set and disappears. Measured: MP3 PROVING GROUND part 1 node 19, in a MP1 SANCTORUS with 9 parts |

Two fixes, and the second is the one that matters longer than this bug:

- `RoomEntity.LoadIntroCamSeq`, called at the end of `LoadRoom`, does for a
  transition what `SceneSetup` does for a first load -- loads the new room's
  intro and `Initialize`s it, so its keyframes name this room's nodes. It also
  clears it for single player and for a custom map, which have none.
- **A node ref that names another room may not cull anything.** `NodeRef`
  carries `RoomName` and nothing had ever compared it -- `==` is three ints.
  `RoomEntity.IsOwnNodeRef` now checks the name *and* every index against the
  room actually loaded; `UpdateRoomParts` refuses a foreign ref before the
  bounds test (which fails **open** for a part this room does not have, so it
  could not catch this), `IsNodeRefVisible`/`IsNodeRefAudible` answer "visible"
  rather than culling against a number that means something else here, and
  `DrawRoomParts` range-checks the node and model index it is about to use.
  Refusing leaves `_partVisInfoHead` null, which `GetDrawInfo` already reads
  as "draw every part": the room is drawn uncull ed, which is correct and
  merely slower. `Setup` also clears the vis-info chain, and with `-debuglog`
  a refused ref writes one `[room] ... does not belong to ...` line per room.

Proved in three runs of `run-rotate.sh`, four maps, 30-second matches:
before, 3/3 clients crashed at the first rotated match end; with the guard but
the intro still stale, the log line fires at every rotated match end and every
client survives all four rotations; with both, four rotations, no crash and
**zero** refused refs -- which is also the evidence the guard rejects nothing
legitimate.

**Why `hard/run-rotation.sh` never caught it:** it crosses one boundary
against the public server's 7-minute matches, so the run always ended in the
middle of the second map, and the second map's *end* is the only place a
session-old static can show. Its single-map case ends four matches, but on the
same room every time, where a stale intro is the right intro.

The netlog couldn't show any of this and now can: `STATE` lines carry
`matchState=` and `goal=`, and each slot carries
`score=<points>/<teampoints>p <kills>k<deaths>d` -- which is what made the
double count visible as `1/2p` in a single line.
