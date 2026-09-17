# Persistent multiplayer lobbies (protocol 8)

`DedicatedServer` owns the session outside the game scene. `SessionPhase` is
Lobby → Starting → InMatch → PostMatch → Lobby; it is independent of the
engine's `MatchState`. Standalone `-server` retains **Continuous** rotation by
default. `-server -lobby` opts in. Both Hosted and Dedicated paths in the
launcher create a Lobby session by default. CLI-created hosted sessions still
default to Continuous.

## Authority and protocol

`MatchDefinition` carries the map, mode, format, limits and gameplay rules.
Lobby overrides do not rewrite the configured `MapRotation`; the results
ballot/rotation seeds the next lobby. Map votes during play end the current
match and go through results before returning to the lobby.

Packets 36–40 are SessionState, LobbyCommand, LobbyCommandResult, MatchLoaded
and MatchLoadFailed. Custom-map reservations 32–35 are unchanged. This wire
format is intentionally incompatible with protocol 7. Roster entries now
include a signed team (-1 means FFA), lobby readiness, and a roster revision.
The client rejects older revisions using wrapping ushort ordering and checks
the server endpoint before accepting control or gameplay data.

Commands carry an ID and expected revision. The server validates ownership,
phase, revision, configuration, team capacity and ready state. A peer caches
64 results and returns the original result on duplicate IDs, including after
socket rebinding. The client allows one outstanding command, retries after
250/500/750/1000 ms and reports a missing acknowledgement. A stale command is
rejected, not silently applied. Identity uses the existing Identify packet,
repeated once a second so hunter/suit changes survive packet loss.

Hosted creation issues a random 128-bit owner token in HostReply. The creator
claims it in Hello; the server consumes it once. Local dedicated creation
passes the token with `-ownertoken`. Manually launched lobby servers without
a token choose the first identified ClientId. Ownership survives an endpoint
change and moves to the oldest remaining peer on disconnect. The existing
master/HostPool architecture still uses its client simulation authority for
hosted games; the lobby, membership, teams, permissions and match clock are
always owned by DedicatedServer. Standalone dedicated servers simulate in
ServerSim as before.

## Match boundaries

Start freezes the definition and expected participants. A simulating server
builds the map before advertising Starting and does not call ServerSim.Advance
until the barrier opens. The deadline gives clients 15 seconds **after the
server finishes building**. Each client acknowledges only once its scene has
loaded, repeats the acknowledgement while Starting, and pumps control traffic
without simulation, movement, firing or clock advancement. An arrival during
Starting is outside the frozen barrier and loads at InMatch. Explicit load
failure removes that peer; a disconnect revalidates exact team requirements.
Timeout releases the barrier for loaded clients, with late loading handled as
join-in-progress. Full teams refuse additional active players.

Results retain their existing timing, ReadyState intent and map ballot.
`Peer.PostMatchReady` is distinct from `Peer.LobbyReady`. Lobby configuration
changes clear all lobby readiness; hunter/suit/team changes clear the affected
player's readiness. No lobby-ready state carries into the next lobby.

`NetLaunch.Connect` waits for Welcome, SessionState and roster, not a running
room. `NetSession.Pump` runs without a Scene. `LobbyScreen` pumps at 30 Hz and
stops before handing the session to the game thread. StartScreen.MatchRequested
preserves the navigation stack. Desktop Shell and Android GameView/MainActivity
separate returning to that stack from leaving/disconnecting. Android posts the
return only after scene cleanup on its GL thread; its UI timer resumes after
the render thread stops. Match-only resets preserve socket, ClientId, LocalSlot,
roster, identity, owner and chat history.

The terminal `Join` wrapper waits in the lobby and accepts `ready`, `start`,
and `leave` without blocking the network pump. Its existing render window is
reused after results; the console becomes its lobby UI between rounds.

## Formats

FFA uses an independent scoring identity per slot. 2v2 requires exactly four
players with two per team; 4v4 requires eight with four per team. Auto preserves
the existing mode's FFA/two-team topology without an exact-count requirement.
The server assigns the least-populated available team and publishes it; clients
and ServerSim consume that roster instead of deriving teams from slot parity.

The wire model supports team indices 0–3 and the four-team format value, but
**2v2v2v2 remains rejected and is not offered in the UI**. GameState rendering,
survival, objectives, HUD and other gameplay paths still contain two-team
assumptions. Assigning four numbers does not make those paths correct. Spectator
slots and auto-start are not implemented by this change.

## Validation

`dotnet FruityPrime.dll -netlobbytest` requires no game assets and uses real
loopback UDP. It covers packet round trips, maximum room lengths, truncation,
enum validation, wrapping revisions, two/eight-peer rosters, token claim races,
permissions, stale commands, duplicate/replayed results, ready invalidation,
exact teams, full teams, cancellation after a loading disconnect, load timeout,
late joins, owner migration/rebind, results and continuous rotation. It also
runs the actual NetSession client through two matches with the same socket,
slot and ClientId under added latency/jitter and deliberate 100% command loss
followed by recovery. Match completion is signalled by a simulated authority;
the harness does **not** load or play a rendered scene.

`dotnet FruityPrime.dll -lobbyshot DIR` renders sample eight-player lobbies at
1280×720, 960×540 and 800×400 using the shared Avalonia UI. Scrollable roster
and settings columns keep chat and leave/ready/start controls reachable.

Rendered desktop and Android two-round acceptance with extracted game files,
and Internet testing, remain separate from the asset-free regression. Do not
present loopback control-plane results as proof of those paths.
