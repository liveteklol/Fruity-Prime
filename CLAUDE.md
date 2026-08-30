# Fruity Prime — tools, design, and the mechanics catalogue

**The project is Fruity Prime. The code is still `namespace MphRead`, and stays
that way.** Upstream is NoneGiven/MphRead and every pull from it is a
fast-forward only while the 221 files that declare that namespace and the 271
that import it are untouched; renaming it would put a conflict in all of them
for a string only a developer ever reads. The rename is the product, the
binaries, the window title and the release artifacts. `Mods/Branding.cs` is
where the name lives — nothing else should spell it out.

| Build | Binary |
|---|---|
| Windows game | `FruityPrime.exe` |
| Windows server | `FruityPrimeServer.exe` |
| Linux game, Linux and ARM64 server | `FruityPrime` |

This file exists so a fresh session can pick the work up without rediscovering
the environment or the failure modes. Everything below has been used; nothing
is aspirational. It stays short on purpose: depth for a given area lives in
`.claude/` (indexed in `.claude/CLAUDE-INDEX.md`) and is loaded only when that
area is the one being touched.

## Where things are

| Path | What |
|---|---|
| `~/MphRead-dev` | the source. Upstream is NoneGiven/MphRead; everything added lives under `src/MphRead/Mods/` so pulling upstream stays a fast-forward |
| `src/MphRead.Android/` | the Android head: the same sources, an APK, a front screen and a match, over GL ES and touch controls |
| `src/MphRead/Mods/Network/` | the whole multiplayer feature |
| `src/MphRead/Mods/Launcher/` | the launcher: `Gui/` is every window (Avalonia, all platforms), `Portable/` is the logic and the text screen |
| `~/mph-net-test/` | the test rig: a copy of the build in `bin/`, extracted game files, `run-check.sh`, `compare-reports.py` |
| `C:\Users\livetek\Desktop\MPH\MphRead-develop\` | the Windows deliverable |
| `net.livetek.fr:27888` | the dedicated server on the user's Pi (systemd unit `mphread-server`) |

## Environment recipe (WSL)

Three things will waste an hour each if you do not know them:

```bash
export PATH="$HOME/.dotnet:$PATH"          # dotnet is not on PATH
export MESA_GL_VERSION_OVERRIDE=4.5COMPAT  # else Mesa hands out a Core profile
export ALSOFT_DRIVERS=null PULSE_SERVER=   # else ALSA retries stall frames
```

- If `~/.dotnet` is empty, the SDK is not installed at all:
  `curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 9.0`
  puts it there.
- The Avalonia launcher needs `libICE` and `libSM`, which the game itself does
  not and a minimal WSL install does not have. Without them it falls back to the
  text launcher rather than failing, so a window that never appears is this and
  not a bug in the screen. `sudo apt install libice6 libsm6`.
- `dotnet` aborting on startup with *"Couldn't find a valid ICU package"* is a
  missing `libicu`, not a broken SDK. `sudo apt install libicu-dev` is the fix;
  `export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1` gets a build out of a box
  with no root, at the cost of culture-aware string handling — fine for
  building and for the server checks, not something to leave set while
  testing anything that formats text for a player.
- **`MESA_GL_VERSION_OVERRIDE=4.5COMPAT` is not optional.** Without it Mesa gives
  a Core profile despite the Compatability request, every `GL.Begin` fails
  silently with `InvalidOperation`, and every frame renders black. Nothing in
  any log says so.
- A window created with `StartVisible = false` has no usable back buffer under
  Mesa. Screenshots must read the scene's offscreen target
  (`Scene.ReadSceneTarget`, used by `Mods/ScreenCapture.cs`), which carries the
  world but not the HUD.
- `paths.txt` must sit **next to the DLL**, not in the working directory:
  `ConsoleSetup.Run` does `Directory.SetCurrentDirectory(BaseDirectory)`.
- Audio failures used to kill the process from a static constructor. That is now
  non-fatal, but under WSL the audio device is flaky enough that the test rig
  disables it outright.
- Do not `cp` over a DLL while a process is using it: .NET memory-maps it and
  the process dies with an opaque crash. Stop the server first, or write to a
  new name and `mv`.

## Commands

| Command | Use |
|---|---|
| `MphRead -server -port N -players 8` | dedicated relay server; needs no game files. `-servername "NAME"` is what a browser shows; it announces itself to `net.livetek.fr` unless `-nomaster` is passed, and `-master HOST -masterport N` points it elsewhere |
| `MphReadServer.exe -server ...` | the same server on Windows, as its own console binary. `MphRead.exe` can also do it, but it is a GUI binary: a shell will not wait for it and its exit code never reaches `%ERRORLEVEL%`. Run with no arguments it prints what it is for |
| `MphRead -masterserver [-port N] [-public HOST] [-hostports A-B]` | the server directory the launcher's browser asks, and the machine that runs matches for players who cannot open a port. Same binary, no game files, keeps nothing on disk. `-public` is the address to publish for servers registering from this same machine, whose heartbeats arrive over the loopback |
| `MphRead -hostgame "ROOM" [-mode M] [-master HOST]` | ask the directory to run a match and join it. No port forwarding anywhere; the only way to host from a machine with no launcher |
| `MphRead -servers [-master HOST] [-masterport N]` | print the server list the launcher's browser would show, with each server's map, players and round trip |
| `MphRead -connect HOST -port N -name X -hunter H` | join from the command line, no launcher |
| `MphRead -netcheck HOST -port N -name X -hunter H -seconds N [-shots DIR] [-size WxH]` | a real client driven by a script, which reports what it saw. Exit code 0 = pass |
| `~/mph-net-test/run-remote.sh HOST PORT SECONDS hunter...` | the same check against a server that is not on this machine -- which is the one that matters, since eight clients on one box measure the box |
| `~/mph-net-test/run-demo.sh SEC [authority\|client]` | record a demo from a scripted client and print what landed in the file. The authority is the case that matters: it is whichever client joined first, so it is normally whoever set the match up, and the server sends it no snapshots at all |
| `~/mph-net-test/run-rejoin.sh SEC LEAVE REJOIN [host] [port]` | the rejoin scenario, with a control: A hosts and leaves, the authority moves, then one client takes the vacated slot and another takes a fresh one. Prints what each took. `.claude/multiplayer/NETWORK-DIAGNOSTICS.md` |
| `~/mph-net-test/run-lag.sh MS SECONDS hunter...` | the same check against a loopback server behind `udp-lag.py`, which holds every datagram for `MS` before passing it on. A latency bug reproduced at a number you chose, rather than at whatever the internet is doing -- and the Pi answers in 7-17 ms, so it is the *worse* instrument for one |
| `MphRead -maptest "ROOM" -players 8 -seconds 22` | load one room with a full house, drive every player, and report what the map holds and whether it survived |
| `MphRead -maptest "ROOM" -players 8 -bots` | the same, but AI bots instead of the scripted tour -- a different code path, the only one that finds what only `PlayerAi` touches |
| `MphRead -maptest "ROOM" -renderprobe` | stand on every spawn point in the room in turn, read the frame, walk forward five seconds, read the worst. Catches a room that draws nothing -- the failure no other check can see, because everything else about it passes. `-shots DIR` writes the PNGs, `-allnodes` draws without room-part culling (which separates "the geometry is missing" from "the cull lost it"), and `-hudshots` uses a real visible window and reads *its* buffer, which is the only capture that includes the HUD |
| `MphRead -rooms` | list every multiplayer room, one per line, for a shell loop. **27** is the whole cartridge and the right answer with no custom map source present; anything more is a custom map |
| `MphRead -q3convert FILE.pk3 -map LEVEL -name ROOM [-noclip]` | a Quake 3 .pk3 to a custom map in one command: textures baked from the level's own art, scale and extents picked from its geometry, spawns from its entities. Places no weapons or powerups -- where those go decides how the map plays. `.claude/mapgen/MAP-PIPELINE.md` |
| `MphRead -mapgen ["NAME"]` | generate the room binaries for the custom maps in `maps/` (recursively: a map may sit in a folder of its own with its level and textures beside it), from the player's own textures. **No level ships there** -- `maps/dust2/dust2.json` is a recipe for de_dust2 and needs the player's own `df_dust2.pk3` to become a room. `-mapmaterials "ROOM"` prints what textures a room can lend. A map is a JSON file; the `.bin` it produces is never committed. `.claude/mapgen/MAP-PIPELINE.md` |
| `MphRead -cel on\|off [-celbands N] [-celedge N]` / `-fog on\|off` | render options for every path that never opens a launcher, which is every screenshot command. `.claude/render/CEL-SHADING.md` |
| `MphRead -uishot DIR` | pictures of the launcher's own screens -- home, settings, the map picker, the pause menu -- rendered without anyone looking at a display. The one part of the program that could not otherwise be checked from a headless box |
| `MphRead -demoinfo FILE [-replay]` | what a recorded match contains -- records, frames, a packet-type histogram, and how well it compressed. `-replay` then runs the file through the real player with no room or window and reports how the packets landed per frame, which is the measurement "the replay stutters" is about. Needs no game files. `.claude/multiplayer/NETWORK-DEMOS.md` |
| `MphRead -netcheck ... -recorddemo` | the harness client, recording a demo as it plays |
| `MphRead -mechanics` | print the catalogue in `MECHANICS.md`, generated from the game's own tables |
| `MphRead` (no arguments, Windows or macOS) | the front screen. The Windows build is a GUI binary, so double-clicking it opens the launcher with no terminal behind it |
| `MphRead -menu` | the console menu, for people who typed something |
| `MphRead -launcher [-console]` | the front screen explicitly; `-console` also gives it a terminal. The same Avalonia screen on Windows, Linux and macOS, or the text one when there is no display. A bare `MphRead` on Linux still opens upstream's `-menu` prompts, unchanged |
| `FruityPrime -launcher -text` | the text front screen on a machine that has a display. What an SSH session gets anyway |
| `FruityPrime -update` | check GitHub for a newer release and open its page. Installs nothing; the one command that answers "am I on the latest build" |
| `FruityPrime -noupdate` | do none of that, on any command that would have |
| `FruityPrime -credits` | who this is built on, from `Mods/Credits.cs` |
| `MphRead -fullscreen` / `-windowed` / `-nohelmet` | display choices for the paths that never open a launcher |

## The launcher

**One launcher, in Avalonia, on Windows, Linux, macOS and Android** — one
thread, one toolkit setup per process
(`GuiLauncher.EnsureSetup`), each visit a nested dispatcher loop. `-launcher`
opens a front screen, not a settings dialog: a map picture on the left, the
things you can do on the right.

| Entry | What it does |
|---|---|
| Host | the story from a save slot, or a match: map, mode, hunter, and a `Where` row -- **Local** is an offline match with 0-7 bots and their skill, **Online** asks the directory to run it. The listen-host path (`NetHostSession`, the dedicated server in this process over the loopback) still exists and is still what `LaunchKind.Host` can do, but the card no longer offers it: the port, "let the directory run it" and "list it" rows are built and forced rather than shown, because every one of them is a question about the player's router. Running a server yourself is the dedicated server's job |
| Join | name, hunter, `host` or `host:port`, and a live line saying what that server is running. **Find a server** opens the browser |
| Watch a demo | pick a `.fpdemo` and replay it |
| Settings | display, audio, controls, match rules, and profile (name, hunter, server addresses, updates, game files, credits). Also reachable from the pause menu during a match. Cheats, bugfixes and the leftover feature flags have **no UI any more** and no longer load from `settings.json` -- they sit at their code defaults |
| Game files | where the .nds goes. Shown first, and everything else greyed out, when there is nothing set up yet |

Gotchas worth keeping in view without opening another file:

- **Cheats are all off in a networked match**, not just the obviously leaky
  ones: `NetLaunch.DisableCheatsForMatch` walks every `public static bool` on
  `Cheats` by reflection, so the list can't drift.
- **There is no console window at all** on the Windows build (`WinExe`).
  `Mods.ConsoleWindow.Prepare` attaches to a parent console when a command was
  typed, allocates one when double-clicked, and does neither for the launcher.
- **A bare invocation opens the launcher on Windows and macOS**; on Linux it
  still opens upstream's console menu, since that's a screen people there
  already use — `-launcher` asks for the window there too.
- Offline matches can hold eight players; `PlayerEntity.MaxPlayers` defaults
  to four (a DS match's cap), so the launcher raises it before creating
  players or asking for seven opponents silently produces three.
- `PacketType.StatusQuery` answers "what map, what mode, how many players"
  without claiming a slot, which is what lets the browser poll idly. A server
  built before it falls back to a slot-taking Hello/Bye probe — redeploy the
  server to get the cheap path. Full account, plus the directory and hosting
  design: `.claude/multiplayer/NETWORK-BROWSER.md`.

Deep dive (UI components, settings window, first-run/extraction, macOS/Android):
`.claude/launcher/LAUNCHER-OVERVIEW.md`, `LAUNCHER-DESIGN.md`,
`LAUNCHER-SETTINGS.md`, `LAUNCHER-FIRSTRUN.md`.

## Android

The head builds a playable APK. The engine's `GL` is redirected to OpenGL ES 3.0
by **one using alias** in the Android csproj, pointing the name at
`Mods/Render/GlEs.cs`, which emulates the four things ES does not have —
immediate mode, display lists, the current colour and the alpha test — so not
one call site in upstream's renderer changed. Input is the same trick from the
other end: `AndroidInput` hands the scene a keyboard and a mouse of its own and
presses whatever the player has bound, which is why rebinding, aim sensitivity
and the DS weapon wheel all work without touching `ProcessAllInput`.

**The front screen runs; the match has never been loaded.** An emulator (API
30, x86_64, software CPU and GL) shows the screen and the game-files card; what
that box cannot do is load a room, having no extracted game files, so the
renderer and the touch controls are still unmeasured. Two traps that killed the
app before any of this project's code ran — an activity theme that was not an
AppCompat descendant, and a Debug APK that carries no managed code unless
`EmbedAssembliesIntoApk=true` — are written up with the rest in
`.claude/android/ANDROID-PORT.md`, along with the build recipe, the game-files
directory, and how to run an emulator here.

## Updating

`Mods/Update/`. The program checks GitHub for a newer release on its own, says
so, and **installs nothing** — "Update now" opens the release page; download
and unpacking are the player's. It checks by itself because
`NetConfig.ProtocolVersion` makes a server refuse a client on a different
build outright at Hello, so a copy one release behind can't join anything, and
that's worth automating; it does not install because that means downloading
and executing a file with no signing behind it, so the guarantee would only
ever be "TLS, and GitHub was not compromised" — not doing it is better than
doing it carefully.

| | When it checks | What "update now" does |
|---|---|---|
| Launcher window | in the background once the window is up | opens the release page; badge shows the address if there's no browser |
| Text launcher | at startup, waiting up to 2 s | prints the address, opens a browser if there is one |
| `-update` | when asked | prints the address and opens it |
| Server and directory | at startup, before binding | nothing — logs one line, keeps running (a server has no one at the keyboard to decide, and replacing its binary mid-match drops whoever is playing) |

`launcher.txt` carries `auto_update`, on by default; `-noupdate` turns it off
anywhere. A local build without the release workflow's version stamp reports
itself `a local build` and stands down, since there's no way to tell it apart
from a release either ahead or behind.

## The test method

The failure that matters here is invisible from one side: two clients can be
perfectly connected — right slots, agreed clock — while each holds a scene
containing only itself. `-netcheck` runs the **real client** (real
`Scene.OnUpdateFrame`, real `PlayerEntity` simulation, real net hooks, hidden
window) driven by `NetTestScript`'s fixed 15-phase tour, keyed to the
**server's** clock so every client is in the same phase at once. Every client
records what it *did* and what it *saw*; `compare-reports.py` cross-checks
that what one claims to have done shows up as what every other client says it
saw.

```bash
cd ~/mph-net-test
./run-check.sh 150 Samus Weavel Sylux Trace Samus Noxus   # seconds, then hunters
```

Read the output in this order: per-feature `MISMATCH` lines, then
`scoreboards agree`, then `damage pipeline`, then `remote position snaps`.

Map sweeps (`-maptest`, `-maptest -bots`), the world/affliction probes, how to
read every metric the harness prints, and the traps that have already cost
time: `.claude/testing/TEST-HARNESS.md` and `.claude/testing/TEST-METRICS.md`
(the latter also carries the last verified pass/fail status).

## Building and releasing

`.github/workflows/build.yml` publishes `win-x64`, `linux-x64`,
`linux-x64-server`, `linux-arm64`, `osx-x64` and `osx-arm64` on every push and
PR; `release.yml` publishes those six plus the Windows server (seven packages)
on a pushed `v*` tag:

```bash
git tag v0.36.0 && git push origin v0.36.0
```

`tools/check-no-game-assets.sh` (no Nintendo asset ever published) and
`tools/check-dedicated-server.sh` (the server actually starts) both run in CI
and are worth running locally before pushing:

```bash
tools/check-no-game-assets.sh                    # the repository
tools/check-no-game-assets.sh publish/win-x64    # a build
```

Tagging gotcha, PE-header subsystem split, why only the Windows server is
renamed, and the CI runner layout: `.claude/build-deploy/BUILD-WORKFLOW.md`.

## Deployment

```bash
# server and directory (rebuilds ARM64, installs both units, restarts them)
MPH_SERVER_HOST=net.livetek.fr MPH_SERVER_USER=livetek \
  MPH_SERVER_PASS="$(read -rsp 'pi password: ' p; echo "$p")" ./deploy-server.sh
# MPH_DEPLOY_MASTER=0 to leave the directory alone
```

The exe is often locked by a running game: write `MphRead.new.exe`, then `mv`.

**`NetConfig.ProtocolVersion` is 4.** Any protocol change means server **and**
every client must be the same build — a mismatched client is refused outright
at Hello with a line in the server log, which is the intended outcome and not
a layout issue: the wire format doesn't move, an old client would read every
byte correctly and then simulate a different game (frozen in place, shooting
from its ankles) with nothing in the protocol to notice. Deploy the server
before handing out a client built against a new protocol. Publish commands and
the deploy script's env vars: `.claude/build-deploy/DEPLOY-SERVERS.md`.

## Multiplayer: bugs found and fixed

A "damage is broken" report chased as latency for a fortnight turned out to be
eleven separate faults — frozen remote puppets,
shots fired from ankle height, respawn placement races, a derived-velocity
launch bug, a stale settling guard, a per-machine damage-sequence reset, a
divergence backstop comparing against the wrong instant, jump pads misread as
desyncs, a damage-direction vector abused as a launch velocity, unreplicated
ammo making a puppet briefly untouchable, and an unordered snapshot stream —
plus a double-counted kill that could end a match early for one client and not
another, and a transport queue that dropped the newest packets under load
instead of the oldest. None of it was actually latency; all of it reproduced
at single-digit-millisecond pings on loopback or the Pi.

Shapes worth keeping without opening anything else:

- **A stale input is not harmless just because it's only a position.** The
  intent stream has no notion of "this predates what just happened," so
  anything the authority does to a player of its own accord (a spawn, a
  teleport) can be undone by the next packet that predates it.
- **Look for this shape whenever a remote player can do something on their
  own machine and not on anyone else's:** the puppet is running the same code
  with different *resources* (ammo, in this case), and only the owner's copy
  of those is authoritative.
- **`untested` is a question about the harness, not a pass or a fail.** The
  zoom-replication check read `untested` for months because the tour never
  actually pressed the zoom button, not because zoom was broken.
- **A frame counter that restarts is not an out-of-order packet.** Every
  ordering guard on the wire compared frame numbers and nothing else, so a
  client rejoining a match it had been in for five minutes -- counter back to
  1, the authority still holding 18000 -- had every intent it sent refused for
  the next five minutes. Reproduced, and the shape to look for is any guard
  that says "older than what I have" without also asking "older by how much".
- **A ping is a measurement of the code path, not only of the wire.** The
  number on the scoreboard read 20 ms to a server 1 ms away by ICMP because
  the reply waited for the next rendered frame and for two poll-sleeps on the
  way. Now 1 ms on loopback, where it was 8-11.
- **A randomised run against a loopback server is a regression check, not a
  real-world one, and must not be reported as one** — it has none of the
  reordering, jitter or CPU load the bugs above were found under.

Recording and watching a match back -- the file format, the two things a demo
has to synthesize because they were never received, and why the player counts
frames rather than milliseconds: `.claude/multiplayer/NETWORK-DEMOS.md`.

Full postmortem, measurements, before/after tables, and the traps that cost
the most time: `.claude/multiplayer/NETWORK-DIAGNOSTICS.md`. The
double-counted-kill bug and match-end/rotation handling specifically:
`.claude/multiplayer/NETWORK-MATCHEND.md`. Current verified pass/fail status:
`.claude/testing/TEST-METRICS.md`.

## Known gaps

Claims that are unproven or only partly proven — not bugs, but not to be
re-claimed as solid either: `.claude/KNOWN-GAPS.md`.

## Mechanics catalogue

Weapons, damage multipliers, hunters, movement, states/afflictions, spawning,
match modes, world interactions, pickups, bots, and the multiplayer protocol
rules are in `MECHANICS.md` at the repository root, regenerated with
`MphRead -mechanics`.
