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
the environment or the failure modes. Everything below has been used; nothing is
aspirational.

## Where things are

| Path | What |
|---|---|
| `~/MphRead-dev` | the source. Upstream is NoneGiven/MphRead; everything added lives under `src/MphRead/Mods/` so pulling upstream stays a fast-forward |
| `src/MphRead/Mods/Network/` | the whole multiplayer feature |
| `src/MphRead/Mods/Launcher/` | the Windows front screen, and the settings window behind it |
| `~/mph-net-test/` | the test rig: a copy of the build in `bin/`, extracted game files, `run-check.sh`, `compare-reports.py` |
| `C:\Users\livetek\Desktop\MPH\MphRead-develop\` | the Windows deliverable |
| `france-mining.com:27888` | the dedicated server on the user's Pi (systemd unit `mphread-server`) |

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
| `MphRead -maptest "ROOM" -players 8 -seconds 22` | load one room with a full house, drive every player, and report what the map holds and whether it survived |
| `MphRead -maptest "ROOM" -players 8 -bots` | the same, but the other seven are **AI bots** rather than the scripted tour. A different code path -- the tour writes Controls and never touches the behaviour trees -- and the only one that finds what only PlayerAi touches |
| `MphRead -rooms` | list every multiplayer room, one per line, for a shell loop |
| `MphRead -mechanics` | print the catalogue below, generated from the game's own tables |
| `MphRead` (no arguments, Windows) | the front screen. The Windows build is a GUI binary, so double-clicking it opens the launcher with no terminal behind it |
| `MphRead -menu` | the console menu, for people who typed something |
| `MphRead -launcher [-console]` | the front screen explicitly; `-console` also gives it a terminal. **Off Windows this opens the Avalonia front screen**, or the text one when there is no display. A bare `MphRead` off Windows still opens upstream's `-menu` prompts, unchanged |
| `FruityPrime -launcher -text` | the text front screen on a machine that has a display. What an SSH session gets anyway |
| `FruityPrime -update` | check GitHub for a newer release, install it, and say so. The one command that answers "am I on the latest build" |
| `FruityPrime -noupdate` | do none of that, on any command that would have |
| `FruityPrime -credits` | who this is built on, from `Mods/Credits.cs` |
| `MphRead -fullscreen` / `-windowed` / `-nohelmet` | display choices for the paths that never open a launcher |

## The launcher (Windows)

`MphRead -launcher` opens a front screen, not a settings dialog: a map picture
on the left, four things you can do on the right. Everything that is not a
per-session choice lives in the settings window, which is one of the four and
is also what the pause menu opens mid-match.

| Entry | What it does |
|---|---|
| Play online | name, hunter, `host` or `host:port`, and a live line saying what that server is running. **Find a server** opens the browser below. Connecting happens in the window -- "connecting", "could not join, it may be off or UDP may be blocked" -- instead of in a console nobody sees |
| Play offline | map, mode, 0-7 bots and their skill, hunter, and straight into the match |
| Host a game | the same choices plus a port. **Runs the dedicated server in this process** and joins it over the loopback, so a hosted match has the roster, names, hunters, pings and clock a dedicated one has; friends reach it through Play online |
| Settings | display, audio, controls, match rules, features, cheats, bugfixes, preview generation -- a rail of sections down the left and one page at a time on the right, the same shape as the front screen. Also reachable from the pause menu during a match |
| Game files | where the .nds goes. Shown first, and everything else greyed out, when there is nothing set up yet |

- Every control is painted by this code (`LauncherTheme`, `MenuButton`,
  `ChoiceRow`, `HunterPicker`, `FieldBox`, `SplashPanel`). WinForms will not
  draw a dark combo box or tab strip whatever you set on it, and a half-dark
  window reads as broken rather than as a choice.
- The picture is a map preview out of `thumbnails/`, rendered from the user's
  own files -- no art is shipped. A `splash.png` beside the exe replaces the
  home picture. The online card shows the map the server is on; the others show
  the map about to be played.
- Choices live in `launcher.txt` beside the exe (`LauncherPrefs`) and keys in
  `controls.txt` (`InputSettings`), deliberately not in upstream's
  `MenuSettings`, which gains fields as upstream develops.
- The six First Hunt "biodefense chamber" rooms are left out of the map list:
  they have no player spawn points, so a match there places nobody.
- The launcher window is borderless -- drag the picture to move it, `Escape`
  goes back and then quits -- and the whole menu works from the keyboard. In a
  match `Escape` means the pause menu instead.
- Offline matches can hold eight players. `PlayerEntity.MaxPlayers` defaults to
  the four a DS match could hold, so the launcher raises it before creating
  them; without that, asking for seven opponents silently produces three.
- **There is no console window at all.** The Windows build is `WinExe`, so
  Windows never gives the process a terminal; `Mods.ConsoleWindow.Prepare`
  attaches to the parent's console when a command was typed, allocates one when
  a command was double-clicked, and does neither for the launcher or for a
  child process whose output is already being captured. If the game fails to
  start, the launcher allocates one and prints the error there and in a message
  box.
- **Cheats are all off in a networked match**, not just the four that obviously
  leak: `NetLaunch.DisableCheatsForMatch` walks every `public static bool` on
  `Cheats` by reflection, so the list cannot drift, and a map rotation
  re-asserts it. `FreeWeaponSelect` defaults to *on*, which is how a player
  ended up with every weapon and full ammo in a match.
- The default server is an address, not a hostname. The hostname belongs to the
  people working on this; a copy of the launcher in somebody else's hands
  should not follow it wherever it points next.

### The launcher on Linux

`MphRead -launcher` opens a window off Windows too. There are two screens behind
that one flag and the build picks between them:

| | Where | What |
|---|---|---|
| `Mods/Launcher/` | Windows | the WinForms front screen. **Untouched by the Linux work** |
| `Mods/Launcher/Gui/` | non-Windows game builds | the Avalonia front screen |
| `Mods/Launcher/Portable/` | everywhere | preferences, game files, the launch plan, the launch itself, and the text screen |

All three sit on the same `LauncherPrefs`, the same `GameFiles`, and the same
`MatchStart`, so no two of them can come to disagree about what "host a game"
does. `LaunchPlan`/`LaunchKind` moved out of `HomeForm` and
`Launch`/`AddLocalPlayers` moved out of `LauncherEntry` into `MatchStart` for
exactly that reason: they agree by sharing the code, not by each being kept
correct. `GameFiles` lost a `[SupportedOSPlatform("windows")]` it never needed --
it is files, a child process and upstream's `Paths`.

**Windows keeps WinForms and never sees Avalonia.** The packages are referenced
only when `MphReadAvalonia` is set, which is a game build that is not the
WinForms one, so a Windows publish does not restore them and a server build does
not carry them. Checked, not assumed: `MphRead.exe` contains 0 references to
`Avalonia.Controls` and 642 to `System.Windows.Forms`; `MphRead` for linux-x64 is
the other way round.

**Three screens, one of which is text.** `-launcher` opens the window; with no
`DISPLAY` and no `WAYLAND_DISPLAY` -- an SSH login, a container, a headless box
-- it says so and opens `TextLauncher` instead, and `-launcher -text` asks for
that on a machine that has both. The fallback is the point: a launcher that
cannot open a window must not be a build that will not start.

The Avalonia screen is a port of the design, not of the code. Same palette,
value for value (`GuiTheme` repeats `LauncherTheme`'s numbers, because
`LauncherTheme` is System.Drawing and does not compile here); same card-at-a-time
shape; same painted menu entries with a marker bar and tracked capitals. Two
things are deliberately different, both because Linux is not Windows:

- **The window has a frame.** The WinForms one is borderless and dragged by the
  picture. An undecorated window that a given window manager will not let you
  move is a trap, and there are many window managers.
- **Only the text boxes and scroll bars are stock controls**, under Fluent dark.
  Everything else is drawn, for the same reason the WinForms screen draws its
  own.

The gap this closes is not that Linux could not play: `-connect`, `-servers`,
`-hostgame` and `-menu` were all already there. It is that they are separate
commands with addresses to copy between them, nothing remembered what you chose
last time, and **an offline match against bots had no command-line spelling at
all** -- `-room` is the viewer's room path with no bots and `-maptest` is the
test harness driving them to a script.

Known limits, none of them hidden from the user:

- Play online, offline and host are unusable until game files are set up, and
  both screens say so rather than failing when pressed.
- Volumes, controls, match rules and cheats are still `-menu`. Neither new
  screen reimplements the settings window; both say where it is.
- There is no pause menu off Windows -- `PauseMenuForm` is WinForms. In a match,
  `Escape` does what it did before.
- Avalonia binds X11 client libraries the game itself does not: a system without
  `libICE`/`libSM` gets the text launcher rather than a window. That is what the
  fallback is for, and it is not hypothetical -- the WSL box this was built on
  was missing both.

### The settings window

A rail of sections down the left -- Display, Audio, Controls, Match rules,
Features, Cheats, Bugfixes, Map previews -- and one page at a time on the
right, over the same painted controls as the front screen. It replaced a
WinForms tab strip that no theming reaches, and it no longer carries a map
list: choosing the map is a per-session decision and belongs on the card that
starts the match.

Rows are laid out in `LayoutPages`, after the content panel has a size:
a `Label`'s height cannot be measured before its width is known, and doing it
in the constructor is what produced overlapping notes.

**Saving writes the file and applies it.** For a long time it only did the
first, and on the launcher's path nothing ever read the file back: upstream's
only reader is `Menu.ShowMenuPrompts`, the console menu, which parses
settings.json into private statics and hands them to the engine itself -- and
which the launcher never runs. So the music slider moved a number in
settings.json and left the music exactly where it was, and so did the sound
effects volume, the language, and every match rule (point goal, time limit,
damage level, friendly fire, hunter radar, affinity weapons). `Mods.GameSettings`
is the missing half: `Apply` does the two volumes and the language the moment
they change -- which is what makes the music slider work *during* a match,
since this window also opens from the pause menu -- and `ApplyMatchRules` does
the rest from `Renderer`, after `GameState.Setup` has chosen the mode's
defaults and where the console menu's equivalent already sat. A server's rules
still win: it publishes the point goal and the clock, and those are adopted a
few frames later.

Saving also cannot take the window down any more. `Commit` runs inside a
`try`, and a failure becomes a line in the footer rather than an exception --
which, from the pause menu, would have killed the thread the menu itself runs
on and left a match going behind a menu that no longer answered. The two files
beside the executable (`controls.txt`, `launcher.txt`) now catch every
exception rather than only `IOException`: an install under Program Files raises
`UnauthorizedAccessException`, which is not one.

- **Window**: windowed, or borderless fullscreen. Borderless rather than
  exclusive: it alt-tabs instantly and keeps the desktop resolution.
  `Mods.WindowMode` owns it; `F11` and `Alt+Enter` switch at any time.
  `Escape` no longer leaves fullscreen -- it opens the pause menu, which has
  the switch as an entry. The engine hooks are four lines in `Renderer.cs`.
- **Hunter helmet**: one switch over `Features.HelmetOpacity` **and**
  `Features.VisorOpacity`, with a slider each behind it. The helmet is three
  layers -- `Layer3` shell behind the readouts, `Layer2` shell in front,
  `Layer1` visor pane over the whole view -- and clearing only `HelmetOpacity`
  leaves the visor pane on screen with nothing behind it, which is exactly what
  "I turned the helmet off and something is still there" was. `-nohelmet` zeroes
  both for the same reason. `HudOpacity` is separate and stays separate: the
  readouts are not the helmet.
- **Controls**: mouse sensitivity, invert either axis, and every key.
  `Mods.InputSettings` holds one canonical `PlayerControls`, writes it to
  `controls.txt` beside the exe, and `PlayerControls.GetDefault` applies it to
  every set the game creates. A rebind made from the pause menu also goes
  through `ApplyToPlayers`, because `Apply` copies values into each player's own
  `Keybind` objects and the players in a running match already have theirs.
  Sensitivity was a `/ 4f` literal in two places in `PlayerInput` with an
  "itodo" beside it; `1.00x` is that literal exactly.

### The pause menu

`Escape` during a match opens it (`Mods.PauseMenu` + `Launcher.PauseMenuForm`):
**Resume**, **Fullscreen/Windowed**, **Settings**, **Leave match**, **Quit**.
The cursor comes back, the player stops being driven, and a windowed game can
be moved or resized -- which is what Escape is for in every other game and did
not exist here.

- It runs on **its own STA thread with its own message loop** and talks to the
  game through volatile flags. GLFW window calls belong to the thread that
  created the window, and a WinForms loop pumped from inside the render loop
  would tie the menu's responsiveness to the frame rate. `PauseMenu.Poll` is
  called once a frame and does the window work on the game's thread.
- `ApplicationConfiguration.Initialize()` throws `InvalidOperationException`
  once a form exists in the process, which is every time the launcher opened
  first. Swallowing that is the whole reason the menu appears at all.
- The form centres on **the game window**, not the desktop: `HandleEscape`
  records `ClientLocation`/`ClientSize` before starting the thread.
- **Settings** opens over the menu, not under it. This menu is `TopMost` --
  the game behind it may be borderless fullscreen -- and Windows keeps every
  topmost window above every ordinary one, while a modal dialog is only
  guaranteed to sit above its *owner*. The settings window therefore opened,
  took the keyboard, and drew underneath the panel that had launched it, so
  nothing on it could be reached. The dialog is topmost while in-game, and the
  menu steps out of the topmost band while the dialog is up, so the two are
  never arguing about which is in front.
- **Leave match** closes the window and returns to the launcher;
  `LauncherEntry.Run` is a loop, and `Quit` is the only entry that ends the
  program. The loop re-reads `settings.json` and `launcher.txt` each time round,
  because the pause menu's settings window commits its own copy of both, and it
  sets `PlayerEntity.MaxPlayers` from the bot count rather than raising it, so a
  seven-bot match does not leave the next one at eight. A second match is
  otherwise the same path as the first: `new Scene` calls `GameState.Reset` and
  `PlayerEntity.Construct`, so every slot is rebuilt.

### First run: the .nds file

`Game files` is the first thing a fresh install sees, because there is nothing
to play without it. The button opens a file picker, and the extraction is
upstream's own (`Extract.Setup`, the same code that runs when a ROM is dragged
onto the exe) **in a child process**: it asks its questions and reports its
errors on a console, with `Console.ReadKey` waits that would hang a window that
has none. The child gets its answers on stdin and its output comes back as text
on the card.

Two consequences worth knowing:

- `-launcher` is dispatched **before** upstream's `CheckSetup`, in
  `ModEntry.TryHandleHeadless`. It has to be: that check exits with "press any
  key" when paths.txt is missing, on a console nobody is looking at, so a fresh
  install could never reach the screen that fixes it. The launcher does the
  `Paths.UpdatePaths / ChooseMphPath / ChooseFhPath` work itself
  (`GameFiles.ApplyPaths`).
- `GameFiles.Problem()` is what the rest of the screen keys off: no paths.txt,
  a paths.txt from an older extract version, or a path pointing at a directory
  that is no longer there.

### Asking a server what it is running

`PacketType.StatusQuery` / `StatusReply` (`NetStatus`) answers "what map, what
mode, how many players" **without claiming a slot**, which is what lets the
front screen poll it every few seconds while somebody reads the screen. A
server built before that packet ignores it, so the launcher falls back to a
Hello/Bye join probe: that one does take a slot, is refused outright by a full
server, and counts itself among the players (`NetStatus` subtracts it), so it
is used only until the cheap path answers once, and then rarely. **Redeploy the
server** after taking this build to get the cheap path.

### The server browser and the directory

Until this existed, playing online meant already knowing an address, which is
the one thing a new player does not have. **Find a server** on the online card
opens a list.

**Play online opens the list, not a form.** The address is the one thing a new
player cannot invent, so it is the first question: the list, with a field on it
for an address somebody was handed directly, and only then the card with the
name and the hunter. Back goes to whichever card opened it, and *Change server*
on the online card goes back to it.

It is a **card on the front screen**, not a window over it -- the same shape as
Play online, Host a game and Game files, with its own scrolling list sized by
the same spacer arithmetic (`LayoutSpacer(_browseCard, _serverList, 1f)`: a
scrolling list is a spacer that happens to have things in it). It began as a
separate `Form` and that was the wrong call twice over: a popup over a launcher
that is itself a custom-painted window reads as a different program, and a
`Form` is the one thing in this codebase that cannot be exercised from a
headless machine.

**A hosted game can be listed too.** *Host a game* runs the dedicated server in
the player's own process, so there is no reason it cannot be found the same way
-- but it is somebody's home machine, and being listed publishes its address.
So it is a switch on the card (`ListHostedGame`), on by default because a game
nobody can find is a game nobody joins, and the server is named after the host
rather than after their PC.

### Hosting without opening a port

Being listed is not being reachable. A server on a home PC needs UDP forwarded
to it from the router, and most people cannot or will not do that -- which made
*Host a game* a feature that worked on a LAN and nowhere else.

The fix is the one Age of Empires II: DE uses and it is not NAT traversal:
**the match runs somewhere reachable and the host joins it by connecting out**,
like everybody else. That fits this engine exactly, because its netcode is
already "everyone connects to one relay" -- so putting the relay somewhere with
an open port is the whole of the work.

| Piece | What |
|---|---|
| `HostRequest` / `HostReply` | launcher -> directory: room, mode, time limit, point goal, cap, name. The directory starts an ordinary `DedicatedServer` on a port from its range and answers with the port |
| `-hostports 27900-27919` on the directory | the range it may use, one port per game. Default on: a feature that has to be configured to work is a feature nobody has. `-hostports none` turns it off |
| `Run it: Online, no setup / On this PC` | the choice on the host card. The first is the default, and hides the port and listing rows -- there is no port to choose and being findable is the point |
| `MphRead -hostgame "ROOM" [-mode M]` | the same thing from a command line, which is the only way to host from a machine with no launcher |
| `HostedIdleSeconds` (180) | a game nobody joined is shut down and its port handed back. Generous, because the usual reason one is empty is that the person who asked for it is still loading the map |

The resulting server is *ordinary* in every respect: it registers itself in the
listing, answers status queries, runs its own single-map rotation, and is
joined by a client that has no idea it was started this way. That is what makes
this cheap -- there is no relay framing, no punching, no new transport path,
and **no client change at all**. The client sees a server at an address.

Hole punching was the other candidate and is what a peer-to-peer game would
have to do. It was not worth it here: it needs a rendezvous protocol, it needs
a relay fallback anyway, and it has a failure mode for every symmetric NAT.
This has none.

Measured end to end on one machine: `-hostgame "MP6 HEADSHOT"` asked the
directory, got port 27900, joined it, became the authority and loaded the room;
the directory listed it as `Livetek's game ... MP6 HEADSHOT Battle 1/8`; and a
second client joined it as slot 1 with no special handling.

| Piece | What |
|---|---|
| `MphRead -masterserver` (`Network/NetMaster.cs`) | the directory. Servers announce themselves every 15 s, entries expire after 50 s of silence, and a `MasterQuery` gets the list back in as many datagrams as it takes. It relays no gameplay, stores nothing, and shares a box with the server it lists |
| `MphRead -servers [-master HOST]` | the same list, printed. Makes the two calls the browser makes and formats the same fields, which is the only way to exercise that data path on a machine with no WinForms -- which is every machine this is developed on |
| `MasterReporter` | the server's end. One datagram every 15 s. Every failure is swallowed and retried -- a directory being down must never touch a match -- and the first one prints a line so an operator who expected to be listed can see why they are not |
| `ServerBrowserForm` | the list. Rows come from the directory and are then **confirmed by this machine**: one `StatusQuery` each, which answers map, mode, head count and round trip in a single exchange, and which fails for exactly the servers this player could not have joined anyway. Rows appear as they answer, sorted by players and then by latency |

Two decisions worth keeping:

- **The address in the list is the one the heartbeat arrived from**, not the
  one the server believes it has. A server behind a router knows only its
  private address, and a directory full of `192.168` entries is a list of
  servers nobody can reach. The port is taken from the heartbeat, because the
  source port of a datagram is not necessarily the one it listens on.
- **Latency is measured by the launcher, not reported by the directory.** The
  master could only ever report its own round trip to each server, which is
  not the number the person reading the screen cares about.

`net.livetek.fr:27889` is the default, in `NetMasterConfig`. A hostname rather
than an address -- deliberately unlike the default *game* server, which is an
address on purpose -- because a directory has to be able to move without a new
build reaching every server operator. **That name still does not resolve.**
Until it does, both ends are pointed at the Pi's other name: the launcher
through `master_host=france-mining.com` in `launcher.txt`, and the server
through `-master 127.0.0.1`, since the directory shares its box.

`tools/systemd/mphread-master.service` is the unit; `deploy-server.sh` installs
both it and the game server's, filling in the user and directory, and **leaves
an existing unit alone** on later deploys -- so the two options added on the Pi
by hand (`-master 127.0.0.1` on the server, `-public france-mining.com` on the
directory) survive a redeploy and are not in the templates.

Two things had to be true before anything appeared in a list, and neither is
visible from the code:

- **`-public` on the directory.** The address in a listing is the one the
  heartbeat arrived from, which is right for a server behind a router and
  exactly wrong for the server sharing a box with the directory: that
  heartbeat arrives from `127.0.0.1`, and a list of loopback addresses sends
  every player to their own machine. `-public france-mining.com` is the
  directory being told, once, what to publish for anything registering from
  the loopback or a private range.
- **UDP 27889 through the firewall.** `ufw` on the Pi allowed 27888 and
  nothing else, so the game server answered from outside while the directory
  timed out -- which looks exactly like a directory that is not running.
  Check `sudo ufw status | grep 2788` before believing anything else.
- **Being listed and being reachable are different things.** A server behind a
  home router registers perfectly -- the directory records the public address
  the heartbeat arrived from -- and is then unjoinable by anybody, because
  nothing forwards UDP 27888 to it. Measured from here: a server on this
  machine appeared as `Livetek local test 89.160.128.233`, answered a status
  query on `127.0.0.1`, and timed out on its own public address. The browser
  shows exactly that, as a red row reading "did not answer", which is the
  honest answer and the one that points at the router.

**`ServerStatus` and `MasterListing` return "" rather than null**, through
backing fields. They are structs, so `default` is an ordinary value of them --
the browser holds one for every row it has not probed yet -- and plain
auto-properties handed those rows a null to call `.Length` on. That crashed the
window on the first row of the first list anybody opened, which none of the
headless checks could have caught: nothing outside WinForms ever constructs a
`ServerStatus` it has not filled in. `-servers` exists partly so that the rest
of that path is checkable without a Windows box.

### Ending a match, and the next map

A match that somebody wins used to end the session. `GameState.ProcessFrame`
runs the winner's camera, then the scoreboard, then fades to black with
`AfterFade.Exit` -- correct offline, and on a server it meant every client
dropped back to its own launcher, so a match ending scattered the people
playing it. The server, meanwhile, had never heard that anybody won: its
rotation only knew about the clock, so it kept counting down a map nobody was
still playing.

| Piece | What |
|---|---|
| `NetMatchEnd` | on the authority, sends `PacketType.MatchEnd` when `GameState.MatchState` leaves `InProgress`, repeating until the server's own state comes back with `FlagEnding`. On every client, a server that says `FlagEnding` while this one still thinks the match is running sets `MatchTime = 0`, so the results play out normally rather than being cut to |
| `DedicatedServer` intermission | both endings -- the clock and the score -- now enter the same 9-second intermission before `AdvanceMap`. That is the client's own sequence (3 s of the winner's camera, 5 s of the scoreboard) plus a second, so the fade to black belongs to the rotation rather than cutting the results short |
| `MatchStatePacket.MatchId` | counts matches from the server's start. The room key cannot answer "is this a new match": a rotation one map long -- which is what **Host a game** builds -- plays the same room over and over, and a client watching only the name sat on its results screen for the rest of the session. `NetRoomChange` keys on this instead, so the same room simply loads again, which is a clean restart: every slot rebuilt, every score zeroed |
| `MatchStatePacket.PointGoal` | the score that wins, from the rotation file. It decides when a match ends, so it belongs to the server for the same reason the clock does |

Two things this needed underneath it:

- **`NetMatchSync` must not adopt the clock during the results.** `MatchTime` is
  the countdown the results sequence itself runs on, so putting the old map's
  remaining time back on top of it every frame meant the sequence never
  finished.
- **The authority must stop reporting once the server has heard.** A client
  stays in its results for a second or two after the server has rotated, and
  during that window its match state still says "not in progress" while the
  server's has stopped saying "ending" -- so the authority reported the end of
  the *new* match the instant the rotation landed and the server ended it. One
  whole map skipped per rotation, visible in the server log as two `match over`
  lines in the same second.

## Updating

`Mods/Update/`. A release on GitHub is fetched and installed over the running
build, on Linux and Windows alike.

**Why it is automatic rather than offered.** `NetConfig.ProtocolVersion` makes a
server refuse a client on a different build outright, at Hello. That is the
right behaviour — the alternative is reading the right bytes at the wrong
offsets — but it means a copy one release behind is not slightly worse, it is
one that cannot join anything. "There is an update" and "nothing you press will
work" are the same news, so acting on it is not left to the player.

**How a running program replaces itself.** Neither OS will let a running
executable be overwritten — Windows holds the image open, Linux answers
ETXTBSY — but both will let it be *renamed*, because the name and the inode are
different things and the process holds the second one. So every file in the
package is moved aside to `*.old-update`, the new one is written to the name
that just came free, and the aside copies are deleted on the next start, when
nothing has them open. One code path for both platforms, and until that next
start the previous build is still on disk to go back to. Verified end to end on
Linux: rename, write, keep running, start the replacement, clean up.

**What stops it going wrong:**

- **A local build is never updated.** The release workflow stamps the tag it is
  building into the assembly (`-p:Version=`); a build without that stamp reports
  `a local build` and the updater stands down. Without this, a developer's own
  binary would be replaced by a download whenever its version happened to
  compare low. `BuildVersion` also refuses the `1.0.0` the SDK invents when
  nothing was asked for, because it cannot be told from a real v1.0.0.
- **The asset is matched, not reconstructed.** By runtime identifier and by
  whether this is a server build, rather than by rebuilding the release
  workflow's file name — which would have to be kept in step by hand, and whose
  failure when it drifted would be a silent "no update available" forever. It is
  also why a package still named `MphRead-…` would still be found.
- **Only GitHub.** HTTPS, and a host allow-list. A truncated download is
  detected by length and thrown away rather than unpacked.
- **All or nothing.** If a file cannot be moved half way through, the ones
  already moved are put back before anything is reported as installed.
- There is **no signature check**. This trusts TLS and GitHub, which is the same
  trust as downloading the release by hand, and no more.

**When each thing checks.** The moment to replace a program is one where nothing
depends on it staying up:

| | When | What it does after |
|---|---|---|
| Launcher (both screens) | at startup, before the window | installs and relaunches |
| Dedicated server and directory | at startup, before binding the socket | installs and exits, for systemd or NSSM to restart it |
| `-update` | when asked | installs and says to start it again |

A running server is left alone: replacing the binary under a match would
disconnect exactly the people the feature exists for. `launcher.txt` carries
`auto_update`, on by default.

## The test method

The failure that matters here is invisible from one side: two clients can be
perfectly connected — right slots, agreed clock — while each holds a scene
containing only itself. So:

1. **`-netcheck` runs the real client.** Real `Scene.OnUpdateFrame`, real
   `PlayerEntity` simulation, real net hooks, in a hidden window. Harnesses that
   drove hand-built packets, or pumped the network without stepping the engine,
   stayed green while the shipping client could not put two players in one match.
2. **`NetTestScript` replaces the AI with a fixed tour** of 15 phases (idle,
   walk, jump, turn, shoot, weapon switch, charge, morph A/B, alt attack A/B,
   unmorph, zoom, afflict, duel), keyed to the **server's clock** so every
   client is in the same phase at the same moment. The afflict phase hands each
   hunter its own weapon (`ModArmAffinityWeapon`) before duelling, because
   freeze, burn and disrupt exist only on the affinity version of a weapon and
   a tour that waited to walk over the right pickup never reached them. Half the players morph while the other
   half shoot at them, then they swap — that is how "can a morphed player be
   hit" gets an answer.
3. **Every client records what it *did* and what it *saw*.** That is what makes
   a failure attributable: "I never saw them fire" means nothing if they never
   fired.
4. **`compare-reports.py` cross-checks the reports.** What each client says it
   did must show up as what every other client says it saw, normalised by how
   long each side actually watched. This is the strict check — no single client
   can judge "they never switched weapons", because only their own client knows
   whether they did.

```bash
cd ~/mph-net-test
./run-check.sh 150 Samus Weavel Sylux Trace Samus Noxus   # seconds, then hunters
```

Read the output in this order: the per-feature `MISMATCH` lines, then
`scoreboards agree`, then `damage pipeline`, then `remote position snaps`.

### The scoreboard's ping column

Tab shows it, as it always did; in a networked match there is now a third
column. The numbers are the server's measurement of each peer, carried in the
roster it already broadcasts every second, smoothed so one late datagram is not
a worse connection. Green under 80 ms, amber under 160, red above, `--` before
the first measurement lands.

Two things to expect. A player on the same machine as the server reads ~20 ms
rather than 0: the round trip includes the client's own frame, since a client
answers the ping when it next pumps the session. And the two stock columns move
left when a session is active (`ModScoreColumn1/2`) -- "deaths" is six
characters at eight pixels and ends at x=239 on a 256-wide screen, so there is
no room for a third column otherwise.

### Sweeping every map

`-netcheck` needs a server and several processes; the map audit needs neither.
It loads one room with eight players -- a different hunter per slot, so one run
covers several alt forms and affinity weapons -- drives all of them through the
same tour, and prints an inventory of what the room contains.

```bash
cd ~/mph-net-test
./run-maps.sh 8 22          # players, seconds per map; ~15 minutes for 33 rooms
grep MAPCRASH maps3.log     # a crash is a crash a real match would have had
grep MAPFAIL  maps3.log
```

A line reads:

```
MAPTEST MP14 OUTER REACH | players 8 | frames 734 | spawned 8/8 | alt form 3/8
  | fired 7/8 | deaths 7 | afflicted freeze 0 burn 1 disrupt 0 | spawnpoints 4
  | jumppads 7 (7/7 launched) teleporters 0 (0/0 moved) doors 0 ...
```

`spawned 8/8` with `spawnpoints 4` is the interesting one: the maps were drawn
for four players and the spawn fallback is what lets eight in.

**The world probe.** Counting jump pads answers whether a map contains one, not
whether it works, and driving players around and hoping they step on one is
luck. After the tour ends, the audit stands a player on every jump pad and
teleporter in the room in turn -- `player.Teleport` into it, hold it still for
twelve frames, watch for the event -- and reports what fired. "Into it" is the
part that took three tries to get right: a trigger volume is positioned
relative to its entity, and several pads carry theirs beside or above the
model, so standing at the entity's own position stands the player next to the
box. The probe aims at the volume's centre (`JumpPadEntity.ModVolume`), and a
pad that stays silent is tried twice more -- higher, then in alt form, in case
it is one of the ones flagged to ignore bipeds -- before it is called silent.
Three of MP6 HEADSHOT's eight pads were reported dead for a fortnight's worth
of sweeps on the strength of the first mistake; all eight fire. The engine says so
itself: `Mods.WorldEvents` is called from `JumpPadEntity` and `TeleporterEntity`
at the moment they act on a player, and is inert unless a test turns it on. A
map where *every* pad stays silent is a `MAPFAIL`; one silent pad is not, since
some are meant to be dropped onto from above.

**The affliction probe.** Freeze, burn and disrupt are the states the tour could
never reach. Three things had to be true at once, and each was false:

1. *The hunter has to be holding its own weapon.* Those states exist only on the
   affinity entry of a weapon (`beam + 9`), which in a match is picked up, not
   issued. `ModArmAffinityWeapon` issues it.
2. *The shot has to be charged.* Every affliction in the game sits on the
   **charged** entry of its weapon's affliction pair, and a weapon without the
   `PartialCharge` flag only counts as charged at `FullCharge * 2` -- 120 frames
   of holding for the Judicator. A probe that held for 64 frames fired hit after
   hit that could never freeze anybody. The probe now holds until the weapon
   itself says it is charged (`ModChargeReady`) and then lets go.
3. *Letting go has to reach the engine.* `NetTestScript` computed press and
   release edges inside every `Hold` call, so a phase that cleared all the
   buttons and then set one to the same value wiped the edge the clear had
   produced. A charged weapon fires **on release**, so scripted players charged
   and never fired: the charge phase produced nothing, and neither did the
   afflictions. Edges are now worked out once, at the end of the frame, from
   the state at the start of it.

The probe stands the one hunter that can inflict each state two units in front
of somebody, arms it, charges, fires, and watches for the state. Everybody else
stands down for the duration, so the victim's health is evidence about our shot
rather than about the six players who were duelling around it. It reports `ok`,
`nohit` (the charged shot went out and missed), `FAIL` (it hit and the state did
not follow) or `n/a` (that hunter is not in this match). `-netdebug` prints what
each probe saw: the weapon, the peak charge, and what afflictions its shots
actually carried.

This is how the slot-capacity work was finished. Raising `SlotCapacity` to 8 is
not one constant: **every array indexed by a player slot has to grow with it**,
and the ones that do not are invisible until a ninth-slot index hits them.
Found this way, each as a crash on a specific map:

| Array | Crashed in |
|---|---|
| `GameState.BeamKills[4, 9]` | at startup with five players |
| `TeleporterEntity._triggeredSlots` | any map with a teleporter |
| `AreaVolumeEntity._triggeredSlots`, `_cooldownSlots`, `_prioritySlots` | any map with area volumes (MP6, MP8) |
| `PlayerAi._slotHits`, `_slotDamage`, `captureList` | with bots |
| `PlayerAi._playerVisibility` (`bool[4,4]`) | the first bot to look for a target, with five players |
| `PlayerAi._globalObjs` (four `AiGlobals`) | the fifth bot to pick a destination, with eight players |

If you raise the capacity again, grep for `[4]` and for `[player.SlotIndex]`
before trusting it.

The two `PlayerAi` entries above are the ones the scripted tour could never
find: it writes `Controls` directly and never runs a behaviour tree, so a
crash that only bots reach was invisible to every sweep. `-maptest -bots` runs
the same rooms with the AI driving, which is the path the launcher's offline
match uses. Thirty-three rooms, eight bots each: no crashes.

### What the numbers mean

| Line | Reads as |
|---|---|
| `damage pipeline (resolved here / replayed here)` | the two ends of the damage path. The authority shows `N/0`, everyone else `0/N`. They must match: a shortfall means hits were resolved and never reached the victim |
| `remote position snaps` | visible teleports. Smoothed catch-up is invisible; a snap is not. Healthy is 0-3 per client per 100 s, and the worst ones are respawns. **A run with rotations in it is not comparable**: clients do not finish loading at the same instant, and for about a second some peers are still reporting positions in the room this one has left. `NetRoomChange.Settling` now ignores peer-reported positions for that second -- the authority's snapshot places everybody meanwhile -- but the figure still climbs across a rotation |
| `late=N` in the packets line | snapshots that arrived after a newer one and were refused. Not loss: these are reordered, and applying them ran health, score and the damage counter backwards |
| `scoreboards agree (within N event)` | N > 1 means the clients are keeping different scores. Clients stop a few seconds apart, so one event of difference is timing |
| `form disagreed ... longest run` | one morph animation's worth is normal and desirable. A long run means a puppet is stuck in the wrong form |
| `FAIL: no beam can hurt these players` | `BeamEffectiveness` is all-Zero — the player is literally invulnerable. See the spawn section of the catalogue |
| `untested` | the feature was never performed, so nothing is being claimed. Not a pass |
| `dropped=N` in the packets line | this client could not keep up with what it was sent. Non-zero here reads on *other* clients' reports as "they never saw me turn" |
| `pings: slot 0 12 ms ...` | what the server measured for each slot, which is what the scoreboard draws |
| `alt-attack` | rising edges on the alt attack button, on both sides. Separates "the press never arrived" from "it arrived and laid no bomb" |
| `jumppads 7 (7/7 launched)` | seven in the room, seven tried, seven launched the player standing on them |
| `afflicted freeze 1 burn 2 disrupt 0` | how many players were frozen, set on fire or disrupted at least once during the run |
| `(probe freeze ok burn nohit disrupt FAIL)` | the affliction probe: `ok` inflicted it, `nohit` the charged shot missed, `FAIL` it hit and the state did not follow, `n/a` that hunter was not in the match |

### Traps this harness has already fallen into

- Comparing raw totals when clients joined at different times, or after a
  rotation reset the counters. Normalise by observed time.
- Judging a Sylux against a Weavel's abilities: only three hunters lay bombs and
  only Weavel leaves a halfturret.
- Reporting about one remote player when there are five.
- Counting a respawn as a teleport, and a smoothed catch-up as a jump.
- A tolerance of 1.0 silently turns a check into decoration. If a check stops
  failing, make sure it can still fail.
- Writing a button twice in one frame. The script clears every control and then
  sets the ones a phase wants; when the edges were computed inside each write,
  setting a button to the value the clear had just given it wiped the edge. A
  charged weapon fires on release, so nothing charged ever fired.
- Measuring a 60 Hz path against a 30 Hz reconstruction of it. See the gap
  section below: it looked exactly like packet loss for months.
- Assuming a hit is a hit. Afflictions ride on the *charged* shot only, and a
  weapon without the `PartialCharge` flag is not charged until `FullCharge * 2`
  frames of holding.
- Standing at an entity's position and expecting its trigger to notice. The
  volume is placed relative to the entity, not on it.
- Reading one client's column of a cross-check as a defect before checking the
  others. A number that is low for *every* observer is systematic -- a rate, a
  sampling difference, a collapsed edge; a number that is low for one is that
  client's own story, usually that it joined late and missed a burst.
- Reading `damage pipeline` as a pass because the two ends are both non-zero.
  `25/0` against `0/258` is not a healthy pipeline with noise on it; a byte
  counter that has run backwards reads as almost a full wrap forwards, and the
  three-digit number is the tell.
- Testing a rotation with a rotation that has more than one map in it. A
  server hosting a single map plays the same room over and over, and every
  bug about "is this a new match" hides in exactly that case. Test both --
  `maprotation-test.txt` (three maps) and `maprotation-one.txt` (one) in
  `~/mph-net-test/bin/`, both with a two-point goal so a match ends inside a
  test run.

## Building and releasing

| Workflow | When | What |
|---|---|---|
| `.github/workflows/build.yml` | every push and pull request | publishes `win-x64`, `linux-x64` and `linux-arm64` (that one as the server package) on one Ubuntu runner — the csproj's `EnableWindowsTargeting` is what lets the WinForms build come from Linux — and uploads each as an artifact. A second job on a **Windows** runner builds the Windows dedicated server and starts it there |
| `.github/workflows/release.yml` | a `v*` tag, or by hand | those three plus the Windows server, packaged into two zips and two tarballs with a short note, attached to a GitHub release |

**Two Windows executables, and the difference is one field in the PE header.**
`MphRead.exe` is `WinExe`, so double-clicking it opens the launcher with no
terminal behind it. That same property makes it useless as a server: Windows
bakes the subsystem in at link time, so cmd and PowerShell do not wait for it,
its exit code never reaches `%ERRORLEVEL%`, and a service supervisor cannot tell
whether it is still up. `dotnet publish -r win-x64 -p:MphReadServer=true`
publishes the same sources with the launcher left out and a console header, as
`MphReadServer.exe` — which is exactly the Linux server build with a Windows RID
on it. `tools/check-subsystem.sh gui|console <exe>` asserts each one, in both
workflows, because it comes out of a csproj condition and nothing else would
notice it changing.

**`-p:MphReadServer=true` is the server package**, and both server targets are
published with it: `win-x64-server` and `linux-arm64`. It leaves out the
launcher of either kind and the UI toolkit behind it — nobody sits at either
machine — and it defines `MPHREAD_SERVER`, which is a different question from
"has no launcher": the Linux game build has no WinForms launcher either and is
still a game. That define is what makes a bare invocation print what the binary
is for, rather than falling through to upstream's setup check and answering a
server that ships without game files with "could not find paths.txt, drag a ROM
onto the executable". Double-clicked on Windows it waits for a key before the
window closes; `ConsoleWindow.OwnsItsConsole` is how it tells that from a shell.

**Only the Windows server is renamed.** The subsystem does not exist off
Windows and those packages hold one binary, so the ARM64 server is still
`MphRead` — `tools/systemd/*.service` and `deploy-server.sh` have pointed at
that name since before the server package existed, and renaming it would put a
second binary on the Pi beside the one systemd starts. Dropping the toolkit took
the ARM64 download from 104 MB to 86 MB.

`tools/check-dedicated-server.sh` runs on Windows too, in Git Bash, rather than
being translated into PowerShell that would then have to be kept in step. Three
things differ there and each is handled where it happens: the binary is
`MphReadServer.exe`, `python3` may only be `python`, and a path the script makes
has to go through `cygpath` before a .NET process reads it as anything but a path
on the current drive.

**The dedicated server is started, not just compiled.**
`tools/check-no-game-assets.sh` proves what is *not* in a build; the linux-x64
job now also runs `tools/check-dedicated-server.sh publish/linux-x64`, which
starts a server and a directory out of that build, waits for the server to
register itself, and asks each of them a question a player depends on. A
compile says nothing about whether the server still starts on a machine with no
game data, no display and no sound device -- and that is the machine it is
for. The release workflow runs the same check before it tags anything.

**No Nintendo asset is ever published.** `tools/check-no-game-assets.sh` runs
twice in each workflow: once over what git is tracking, once over what the
build produced. It refuses game-file extensions, the directories extraction and
the preview cache write into (`thumbnails/`, `files/`, `_archives/`,
`Savedata/`, `netcheck-shots/`, `paths.txt`, `netlog-*.txt`), and -- for
tracked files only -- anything over 2 MB, on the grounds that nothing that big
is source. Map previews are the easy mistake: they are rendered locally and
look like ordinary screenshots. Something of ours that trips it (a logo, a
screenshot of the launcher) goes in `tools/asset-guard-allow.txt`; nothing is
exempt by default. Run it locally before pushing:

```bash
tools/check-no-game-assets.sh                    # the repository
tools/check-no-game-assets.sh publish/win-x64    # a build
```

## Deployment

```bash
# server and directory (rebuilds ARM64, installs both units, restarts them)
MPH_SERVER_HOST=france-mining.com MPH_SERVER_USER=livetek \
  MPH_SERVER_PASS="$(read -rsp 'pi password: ' p; echo "$p")" ./deploy-server.sh
# MPH_DEPLOY_MASTER=0 to leave the directory alone
# An SSH key removes the password from this line entirely, which is the setup
# worth moving to: this file is in the repository, and a password written into
# it is a password published with it.

# Windows client
dotnet publish src/MphRead/MphRead.csproj -c Release -r win-x64 \
  --self-contained true -p:PublishSingleFile=true -o publish/win-x64

# Windows dedicated server (console subsystem, no launcher, no game files)
dotnet publish src/MphRead/MphRead.csproj -c Release -r win-x64 \
  -p:MphReadServer=true --self-contained true -p:PublishSingleFile=true \
  -o publish/win-x64-server
```

The exe is often locked by a running game: write `MphRead.new.exe`, then `mv`.
Any protocol change means server **and** every client must be the same build.

`NetConfig.ProtocolVersion` is **3** as of this build. Version 2 grew the
roster by a ping per entry; version 3 added the shooter's ammo to the intent,
the point goal and the match number and the ending flag to the match state, a
name to the status reply, and the `MatchEnd` handshake. Every one of those
moves a field, so a client one version behind would read the right bytes at the
wrong offsets -- the version is what turns that into a refusal at Hello with a
line in the server log instead. Older clients cannot join until they are
updated, which is the intended outcome.

**The server and every client must be the same build**, and that now includes
the Pi: a version 2 server refuses a version 3 client outright, so deploy the
server before handing the client to anybody.

## Audit status

Since the damage work (2026-08-20), re-verified with `run-check.sh`:

| Check | Result |
|---|---|
| 3 clients, 130 s, two of them Trace | **PASS on all three**, 0 mismatches, scoreboards agree within 0 events |
| Damage pipeline on that run | 69/56/49 resolved on the authority, 69/56/48-49 replayed on both observers -- the shortfall is one hit still in flight at the cutoff |
| Snapshots refused as reordered (`late=`) | 0 on loopback; the guard is for the wire, and the counter is in every report |
| Match won on points, 3-map rotation, 4 clients | every rotation announced once, all four peers carried across, `2-3 rotation(s) followed` per client |
| Match won on points, **one**-map rotation | "server started a new match on MP3 PROVING GROUND; loading it" on every client -- the case that used to strand everybody |
| Authority leaving mid-session | promoted to the next peer, session continued |
| `tools/check-dedicated-server.sh` | server and directory both start from a published build, register, and answer |

Last full map pass (2026-08-20), before the above:

| Check | Result |
|---|---|
| Every multiplayer map, 8 players, 24 s each | **33/33, zero crashes** |
| Every multiplayer map, 8 **AI bots** (`-bots`) | **33/33, zero crashes** -- after two slot-indexed arrays in `PlayerAi` were grown from four |
| Players placed | 8/8 on 27 maps; 0/8 on the six First Hunt "biodefense chamber" rooms, which have no spawn points -- the only `MAPFAIL`s in the sweep |
| Jump pads, each stood in in turn | **every pad on all 24 maps that have one**, MP6 HEADSHOT's eight included |
| Teleporters, each stood on in turn | both of AD1 TRANSFER LOCK's moved the player |
| Afflictions, probed per map | freeze landed on 14 maps, disrupt on 13, burn on 6 |
| 6 clients against the Pi, 110 s | 3 mismatches, all late-joiner or stop-skew artifacts; pings 6-16 ms; `dropped=0` |
| 8 clients, 150 s, one machine | 10 mismatches, none of them `facing` |
| Pi 3B under six clients | server process 11-16% of one core, system 65-75% idle |
| Damage pipeline resolved vs replayed | matches |
| Invulnerable players (`BeamEffectiveness` all-Zero) | none |

The affliction probe is a sample, not a verdict: it gets about two charged shots
per state per map, in a live room, and an arcing Magmaul at two units misses
more often than a Judicator does. Read the tally across the sweep -- all three
states land on plenty of maps, so the states work -- rather than one map's
result. `FAIL` in particular overstates: the probe's first press of each cycle
fires an *uncharged* shot, so a hit that lands from that one while the charged
shot misses is recorded as "hit, no affliction". Burn shows this most, which is
why its column is the weakest.

## Known gaps

- **Two front screens now exist and only one of them has been used in anger.**
  The Avalonia screen has been opened, navigated and read pixel by pixel on
  WSLg; nobody has yet played a match from it on a real Linux desktop, and the
  card that most needs that is "host a game", whose failure paths are the ones a
  screenshot cannot show. The WinForms screen is unchanged and unaffected.
- **The pause menu is still Windows-only.** `PauseMenuForm` is WinForms; off
  Windows, `Escape` in a match does what it did before. That is the one entry
  from the Windows launcher's list with no counterpart yet.
- **The updater has never installed a real release.** There are none yet: the
  repository has published nothing, so what is proven is the version logic, the
  asset matching, the refusals, and the file-swap mechanism on Linux — each
  tested on its own. The first tagged release is what tests the whole path, and
  the honest order is to tag one, download it by hand, and only then trust the
  automatic install. The Windows swap in particular has never run.
- **The rename leaves a migration on the Pi.** `deploy-server.sh` rewrites an
  `ExecStart` that still names `MphRead` and deletes the old binary, but that
  code has not been run against the real box. Look at
  `systemctl cat mphread-server` after the first deploy.
- **The ARM64 server package has never been started by CI.** It is
  cross-compiled on an x64 runner, so `check-dedicated-server.sh` cannot run it
  there; the same build configuration is started on every push for linux-x64 and
  win-x64, which is the nearest thing to a check it gets. The Pi is the real
  test, through `deploy-server.sh`.
- **The Windows dedicated server is started in CI, but only there.** The
  `windows-server` job runs `check-dedicated-server.sh` on a Windows runner, so
  the claim is checked on every push; nobody has yet run it on a Windows machine
  behind a real firewall for a long session, which is the arrangement the Linux
  server has had on the Pi and this one has not.
- **Late joiners and bursty features.** The tour does its bombing and its
  unmorphing in particular phases, and clients start three seconds apart. A
  client that joined a phase late reports a fraction of what the subject did,
  and the normalisation by observed time cannot fix a burst it was not there
  for. Six clients against the Pi: bombs matched 79-99% for every observer
  except the last to join, which saw 18%. Judge these against the clients that
  were present, not against the tally.
- **Alt-attack presses read ~60% on every observer.** Not loss -- loss would
  differ per observer. Two presses that fall inside one intent window arrive as
  one, because the edge history is ORed into a single mask per packet. The
  bombs those presses would have laid still land 79-99%, so what is being lost
  is presses that would have done nothing anyway (cooldown, wrong form). Worth
  knowing before reading the number as a fault.
- Kanden and Spire show lower fidelity on `unmorph` and projectile lifetime than
  the other hunters. Not explained.
- The scoreboard rows tighten to fit past four players, down to 19 px; beyond
  eight it would need a second column.
- The First Hunt "biodefense chamber" rooms are listed as multiplayer but carry
  **no player spawn points**, so nobody can be placed in them. They are survival
  rooms; keep them out of a Battle rotation rather than trying to fix them. The
  launcher leaves them out of its map list for the same reason.
- `zoom` and `double damage` only get tested when a bot happens to pick the item
  up, so they are often reported `untested`.
- **A run with match restarts in it is not a clean read of the tour.**
  `NetTestScript` keys its 15 phases to the *server's* clock, and a new match
  restarts that clock, so clients that finish loading a fraction of a second
  apart are briefly in different phases. Testing the point-goal path means a
  two-point goal, which restarts every 40 seconds; expect `alt form` and
  `their form stayed wrong for N frames` failures that a run without
  restarts does not produce. The `181 frames` figure in particular is the
  correction machinery working as designed -- 90 frames of grace, a real
  transition attempted, 90 more, then the form forced -- not a puppet that is
  stuck.

### "Observers only see half your turn", and what it actually was

Listed here for a long time as packet loss under load. It was two things, and
neither was quite that:

1. **The transport dropped the wrong packets.** `NetTransport` queued 256
   received packets for the game loop and, when the queue was full, dropped the
   *arriving* one. Eight clients on one machine produce roughly two thousand
   packets a second between them, so one 130 ms frame -- ordinary when eight
   copies of the engine share a CPU -- overflows it. It then threw away the
   newest packets while a backlog of stale ones drained, which is exactly the
   wrong choice for a protocol whose packets say "this is where I am aiming
   *now*". The queue is 2048 now, the socket buffers are 1 MB, and an overflow
   drops the oldest. Eight clients, 150 s: `dropped=0`, where the same run used
   to drop.
2. **The check compared two different things.** A player publishes position and
   aim every `NetConfig.IntentSendInterval` frames, so a remote copy moves in
   30 Hz steps. `NetFeatureCheck` measured the local player's path every frame
   -- a 60 Hz path -- and compared its length against that 30 Hz reconstruction.
   With one opponent the aim moves slowly and the two agree, which is why two
   clients always looked clean; with seven it slews between targets several
   times a second, the reconstruction cuts every corner, and the observer reads
   a third of it. The check now samples the local player at the instants it
   publishes an intent, and remote players every frame -- both sides then
   measure the same polyline, and a shortfall means a packet went missing.
   Snap detection stayed per-frame: whether a position jumped between two
   frames is a different question from how far it travelled.

Eight clients, 150 s, one machine: **33 mismatches before, 10 after, none of
them `facing`**. Two clients agree to within 2% either way, which is what says
the sampling change corrected a bias rather than hid one.

### Flying bodies, invincible players, and hits that were never counted

Three separate faults, reported together as "weird things happen with two
Trace clients", and each one has a shape worth recognising again.

**A hit launched the victim across the level.** `PlayerEntity.TakeDamage` adds
its `direction` argument straight onto `Speed`, so whatever is in it is a
velocity in units per frame. A beam supplies one that `GetDamageDirection`
built from a unit vector times the weapon's own magnitude -- a fraction of a
unit. `NetDamage.Note`, when a hit carried no direction of its own (which is
most of them: `DamageDirType` is 0 for most beams and knocks nobody back),
filled one in as `victim.Position - attacker.Position`. That is the *distance
between the two players*, so a hit from ten units away launched the victim at
ten units a frame and put them through the wall. The engine's own fallback
uses that same vector, but only for the damage indicator, never for `Speed`.
The fix is to relay the impulse verbatim, zero included, and let the receiver
turn zero into a null direction -- which is exactly what makes `TakeDamage`
take its own fallback path, for the indicator only.

It looked asymmetric -- "A shoots B and B flies, B shoots A and it is fine" --
because A was the authority. The authority applies its own damage directly,
with the real vector; everybody else replays.

**A player could not be damaged at all, until the shooter changed weapon.**
`BeamProjectileEntity` refuses to spawn a beam whose ammo cost exceeds the
shooter's ammo. Every machine simulates every player's shots and spends the
ammo, but pickups are collected locally and are not replicated, so only the
owner's count is ever right -- and the puppet on the authority's machine runs
dry within a round. From then on the shooter watched their own beam leave the
gun and connect while the authority created no projectile at all, so the target
took nothing. Switching weapons "fixed" it for as long as the other ammo pool
(missiles against universal ammo) had something left in it. The intent now
carries the owner's ammo, the way it already carried their position and their
weapon, and `ModSetAmmo` writes it onto the puppet.

Look for this shape whenever a remote player can do something on their own
machine and not on anyone else's: the puppet is running the same code with
different *resources*, and only the owner's copy of those is authoritative.

**Snapshots were the one stream nobody ordered.** Both intent streams refuse a
frame older than the newest they have seen; `HandleSnapshot` did not, and it is
the stream carrying health, score and the damage counter. A datagram overtaken
in flight put a player back where they had been, undid a kill, and ran the
damage counter backwards -- and since that counter is a byte, `Replay` read the
difference as about two hundred and fifty new hits. In an eight-player run the
cross-check showed it plainly: 25 hits resolved on the authority against 258
"replayed" on a client. Ordering the stream took three lines and, on a
three-client run, took one client's visible position snaps from 13 to 1 and
made the damage pipeline agree exactly. `NetDamage.Replay` also refuses more
than 32 hits in one snapshot now, so a single bad packet cannot flinch, shove
or -- if it happened to carry zero health -- kill somebody.

### And it was never the Pi

Measured while six clients played on it (`ps`/`top` over SSH, sampled every
three seconds):

| | Idle | Six clients |
|---|---|---|
| `MphRead` server process | 5-7% of one core | 5-22%, typically 11-16% |
| System, four cores | ~97% idle | 65-75% idle |
| Packets dropped | 0 | 0 |

A Raspberry Pi 3B has room for several times this. The one thing worth fixing
was the idle figure: the run loop slept a millisecond between passes whether or
not anybody was connected, which is 5-7% of a core burnt around the clock for
nothing. It now sleeps twenty milliseconds while the server is empty -- the only
thing waiting on that loop is the next Hello.

---

# Metroid Prime Hunters — multiplayer mechanics

Generated by `MphRead -mechanics` from the game's own tables.

## Weapons (multiplayer table)

Damage is per shot before any multiplier. "Charged" is a full charge; "min" is the smallest charge that counts as charged.

| Beam | dmg | min-chg | charged | headshot | hs charged | splash | ammo | cost | cooldown | afflictions | notes |
|---|---|---|---|---|---|---|---|---|---|---|---|
| PowerBeam | 6 | 6 | 36 | 8 | 48 | 0/0 | UA | 0/0 | 5 | - | repeat fire, chargeable |
| VoltDriver | 14 | 56 | 56 | 21 | 56 | 0/56 | UA | 5/25 | 5 | - | chargeable |
| Missile | 32 | 48 | 48 | 32 | 48 | 24/32 | missile | 10/15 | 20 | - | chargeable |
| Battlehammer | 12 | 12 | 12 | 12 | 12 | 8/8 | UA | 4/4 | 10 | - | repeat fire |
| Imperialist | 72 | 72 | 72 | 200 | 200 | 0/0 | UA | 20/20 | 60 | - | can zoom, repeat fire |
| Judicator | 24 | 24 | 24 | 32 | 32 | 12/10 | UA | 5/25 | 15 | - | chargeable, can hurt the shooter |
| Magmaul | 32 | 56 | 56 | 32 | 56 | 16/28 | UA | 10/20 | 20 | - | chargeable, ricochets, can hurt the shooter |
| ShockCoil | 10 | 10 | 10 | 10 | 10 | 0/0 | UA | 10/10 | 0 | - | continuous, repeat fire |
| OmegaCannon | 200 | 200 | 200 | 200 | 200 | 200/200 | UA | 0/0 | 60 | - | - |

Affinity versions -- what a hunter gets when it carries its own weapon. Note the extra damage and the afflictions the plain versions do not have:

| Beam | dmg | min-chg | charged | headshot | hs charged | splash | ammo | cost | cooldown | afflictions | notes |
|---|---|---|---|---|---|---|---|---|---|---|---|
| PowerBeam | 6 | 6 | 40 | 8 | 52 | 0/0 | UA | 0/0 | 4 | - | repeat fire, chargeable |
| VoltDriver | 14 | 56 | 56 | 21 | 56 | 0/56 | UA | 5/25 | 5 | charged: Disrupt | chargeable |
| Missile | 32 | 48 | 48 | 32 | 48 | 24/32 | missile | 10/15 | 20 | - | chargeable |
| Battlehammer | 18 | 18 | 18 | 18 | 18 | 12/12 | UA | 5/5 | 15 | - | repeat fire |
| Imperialist | 72 | 72 | 72 | 200 | 200 | 0/0 | UA | 20/20 | 60 | - | can zoom, repeat fire |
| Judicator | 24 | 12 | 12 | 32 | 12 | 12/0 | UA | 5/25 | 15 | charged: Freeze | chargeable, area of effect, can hurt the shooter |
| Magmaul | 32 | 48 | 48 | 32 | 48 | 16/18 | UA | 10/20 | 20 | charged: Burn | chargeable, ricochets, can hurt the shooter |
| ShockCoil | 10 | 10 | 10 | 10 | 10 | 0/0 | UA | 10/10 | 0 | - | continuous, repeat fire |
| OmegaCannon | 60 | 60 | 60 | 60 | 60 | 60/60 | UA | 0/0 | 60 | - | - |


Affinity weapon per hunter (the one whose enhanced version it uses):

- Samus: Missile
- Kanden: VoltDriver
- Trace: Imperialist
- Sylux: ShockCoil
- Noxus: Judicator
- Spire: Magmaul
- Weavel: Battlehammer

## Damage multipliers, in the order the code applies them

| Rule | Effect | Where |
|---|---|---|
| Beam effectiveness vs the target | x0 / x0.5 / x1 / x2 | `PlayerEntity.TakeDamage`, from `BeamEffectiveness[beam]`. Players are set to Normal for every beam in `Spawn()`; **a player that never spawned has x0 for everything and cannot be hurt at all** |
| Double damage pickup | x2, and *not* applied to Shock Coil | `BeamProjectileEntity` |
| Prime Hunter | x1.5 | `BeamProjectileEntity` |
| **Imperialist without zoom** | **/2** | `BeamProjectileEntity`: `if (weapon.Beam == Imperialist && !equip.Zoomed) damage /= 2` |
| Quadruple Damage cheat | x4 | `BeamProjectileEntity`, from settings.json. Disabled automatically while connected to a server |
| Match damage level | x0.75 low / x1 medium / x1.25 high | `PlayerEntity.TakeDamage` |
| Headshot | uses the weapon's headshot damage instead | `BeamProjectileEntity` |
| Friendly fire off, same team | x0 | `PlayerEntity.TakeDamage` |
| Weavel halfturret alive | damage is split between body and turret | `PlayerEntity.TakeDamage` |
| Affinity weapon | the hunter uses entry `beam + 9`, which is a different set of numbers entirely -- more damage and, for several weapons, an affliction the plain version does not inflict | `PlayerEntity.TryEquipWeapon` |

Invulnerability windows: a hit sets a damage-invulnerability timer (per hunter, `Values.DamageInvuln`), and spawning sets a spawn-invulnerability timer. Both reject further damage until they run out.

## Hunters

| Hunter | energy tank | MP max health | MP ammo cap | alt form | bombs | boost | alt attack |
|---|---|---|---|---|---|---|---|
| Samus | 100 | 199 | 599 | Morph Ball | yes | yes | bombs |
| Kanden | 100 | 199 | 599 | Stinglarva | yes | no | bombs |
| Trace | 100 | 199 | 599 | Triskelion | no | no | cloak and lunge |
| Sylux | 100 | 199 | 599 | Lockjaw | yes | no | bombs |
| Noxus | 100 | 199 | 599 | Vhoscythe | no | no | spin attack |
| Spire | 100 | 199 | 599 | Dialanche | no | no | slam |
| Weavel | 100 | 199 | 599 | Halfturret | no | no | leaves a turret that shoots on its own |

Health on spawn is `EnergyTank - 1`; the maximum in multiplayer is `2 * EnergyTank - 1`, so a full pickup run doubles a hunter's effective health.

## Movement, per hunter

Speeds are units per frame as the engine stores them (20.12 fixed point converted to float on load).

| Hunter | walk cap | strafe cap | jump | biped gravity | alt gravity (air/ground) | boost cap | boost charge (min-max) | alt radius |
|---|---|---|---|---|---|---|---|---|
| Samus | 0.24 | 0.24 | 0.3 | -0.0188 | -0.0598/-0.0269 | 0.6 | 5-15 | 0.5 |
| Kanden | 0.24 | 0.24 | 0.3 | -0.0188 | -0.0598/-0.0269 | 0.6 | 5-22 | 0.4 |
| Trace | 0.24 | 0.24 | 0.3 | -0.0188 | -0.0598/-0.0269 | 0.6 | 5-22 | 0.63 |
| Sylux | 0.24 | 0.24 | 0.3 | -0.0188 | -0.0398/-0.0269 | 0.6 | 5-22 | 0.63 |
| Noxus | 0.24 | 0.24 | 0.3 | -0.0188 | -0.0598/-0.0269 | 0.6 | 5-15 | 0.5 |
| Spire | 0.24 | 0.24 | 0.3 | -0.0188 | -0.0598/-0.0269 | 0.6 | 5-22 | 0.5 |
| Weavel | 0.24 | 0.24 | 0.3 | -0.0188 | -0.0598/-0.0269 | 0.6 | 5-22 | 0.4 |

- Morphing plays an animation and the form only changes when it ends (`EnterAltForm` sets Morphing; `ProcessPlayer` applies `UpdateForm` on `AnimFlags.Ended`). Unmorphing applies the change immediately.
- Alt form swaps the collision volume (`PlayerVolumes[hunter, 2]`), and the position is shifted by the difference between the two volume centres.
- Boost is a charge-then-release: hold to build from `BoostChargeMin` to `BoostChargeMax`, release to convert it into speed between `BoostSpeedMin` and `BoostSpeedMax`.

## States a player can be put into

| State | Cause | Duration | Effect |
|---|---|---|---|
| Frozen | Judicator, affinity version, **charged shot only** | 75 frames doubled; 15 doubled if refrozen within 60 doubled | cannot act; the ice layer draws over the hunter; animation frames stop advancing |
| Disrupted | Volt Driver, affinity, **charged** | 60 doubled | HUD distortion, aim disrupted |
| Burning | Magmaul, affinity, **charged** | 150 doubled | damage over time, attributed to whoever set the fire |

Every affliction in the game sits on the charged entry of its weapon's
affliction pair (`WeaponInfo.Afflictions[1]`). An uncharged hit from the same
weapon inflicts nothing, and "charged" means `ChargeLevel >= FullCharge * 2`
unless the weapon carries the `PartialCharge` flag, in which case
`MinCharge * 2` is enough.
| Double damage | pickup | timer | x2 outgoing damage, Shock Coil excepted; a visible effect is attached to the gun |
| Cloaked | pickup | timer | alpha drops towards invisible |
| Deathalt | pickup | timer | forced alt form, health drains, heavy damage output |
| Halfturret | Weavel morphs | until unmorph or death | the turret is a separate entity that shoots on its own; damage is split between it and the body, and unmorphing gives its remaining health back to Weavel |
| Damage invulnerable | any hit | `Values.DamageInvuln` doubled | further hits are rejected outright |
| Spawn invulnerable | spawning | timer | hits are rejected unless the damage carries `IgnoreInvuln` or `Death` |

## Spawning and respawning

`PlayerProcess.GetRespawnPoint` picks a point by these rules, in order:

1. Consider at most 25 spawn points. Skip any that is inactive, still on cooldown, or (on the very first frame) flagged by availability.
2. In Capture, skip points belonging to the other team.
3. A point is *valid* only if every living player is at least 10 units away. Among the valid ones, the choice rotates with the frame counter.
4. If none is valid, take the one furthest from any living player.
5. If even that is unavailable -- every point on cooldown, which happens with more players than the map was drawn for -- take any active point. Without this last step the player simply does not spawn and waits at the origin.

The chosen point goes on a cooldown of 2 frames doubled, so a crowd cannot all land on the same one.

A dead player waits on `_respawnTimer`; it may spawn early by holding fire. Health on spawn is `EnergyTank - 1`. **`Spawn()` is also what fills in `BeamEffectiveness`** -- a player that reaches the map without it takes zero damage from every beam in the game.

## Match modes

| Mode | Scored on | Second column on the scoreboard |
|---|---|---|
| Battle / Battle Teams | points (a kill is +1, dying is -1) | deaths |
| Survival / Survival Teams | time alive | deaths; running out of lives puts a player out of the game |
| Capture | octoliths taken | kills |
| Bounty / Bounty Teams | octoliths delivered | kills |
| Nodes / Nodes Teams | points from held nodes | kills |
| Defender / Defender Teams | time holding the node | kills |
| Prime Hunter | time spent as the prime hunter | kills; the prime hunter deals x1.5 damage and is shown to everyone |

A match ends on the point goal or the time limit, whichever comes first. Team play merges the per-player tallies into two team tallies, and with friendly fire off a shot at a team-mate does nothing at all.

## World interactions

| Thing | Behaviour |
|---|---|
| Jump pad | launches whatever touches it along a fixed vector; this is the one place a player legitimately covers a lot of ground in a few frames |
| Teleporter | moves the player to the linked pad, optionally forcing alt form on arrival |
| Door / force field | opens on contact or on a weapon of the right colour; locked doors ignore everything else |
| Kill height | a room-wide floor: below it the player dies. This is what most "random deaths" in a fast match actually are |
| Lava and hazard volumes | set the player on fire while standing in them, except for Spire, who is immune |
| Morph camera | a volume that forces the camera behind a morphed player, and blocks unmorphing while inside |
| Item spawner | respawns its item on a timer once taken |

## Pickups

| Item | Effect |
|---|---|
| HealthSmall / HealthMedium / HealthBig | restores health |
| UASmall / UABig | universal ammo |
| MissileSmall / MissileBig | missile ammo |
| VoltDriver, Battlehammer, Imperialist, Judicator, Magmaul, ShockCoil | grants the weapon and its ammo |
| DoubleDamage | x2 damage for a time, Shock Coil excepted |
| Cloak | invisibility for a time |
| Deathalt | forced alt form, drains health, heavy damage |
| OmegaCannon | one-shot kill weapon |

A player killed in multiplayer drops ammo of the type its killer's weapon uses.

## Bots

A bot is an ordinary player whose `Controls` are written by `PlayerAi.ProcessInput` instead of by a keyboard. That is the whole of the difference, and it is why a networked player can reuse the same surface: relayed input is simply a third writer of the same buttons.

| Piece | What it does |
|---|---|
| `PlayerEntity.IsBot` | marks the slot as AI-driven. `Scene.AddPlayer` sets it on every player after the first, which is right for a local match and wrong for a networked one -- the AI would overwrite relayed input, so a networked session clears it on every slot |
| `BotLevel` (0-2) | difficulty; clamped and used to index reaction and accuracy tables |
| `AiPersonality` | per-hunter behaviour trees loaded from the ROM's own data, one set per hunter and encounter. `AiPersonalityData1` nodes hold conditions and the function ids to run |
| `AiData.Process()` | run once per frame per bot from `Scene.UpdateScene`, but only while the bot is alive |
| `UpdateExecutionPath` / `Execute` | walks the tree and dispatches `Func24Id` to the behaviour functions -- move, aim, fire, morph, use the alt attack, pick a weapon |
| Weapon choice | prefers the hunter's affinity weapon (`Weapons.AffinityWeapons[hunter]`) and zooms when it holds a weapon that can |
| `AiFlags3` | the spawn/despawn handshake: one bit asks for the spawn effect and sound, another marks the bot as despawned |

For testing, `NetTestScript` replaces the AI entirely: it writes the same `Controls`, but to a fixed script rather than a behaviour tree, so two machines can be asked to do the same thing at the same moment and compared.

## How multiplayer works here

This is not the DS Wi-Fi protocol and cannot talk to real hardware or an emulator. It connects MphRead instances to each other.

| Piece | Rule |
|---|---|
| Server | a relay with no game files: it assigns slots, keeps the match clock and the map rotation, and forwards packets. It never simulates |
| Authority | the first client to connect. It resolves damage, deaths and scores for everybody |
| Position | owned by the player it belongs to. Each client publishes its own position in every intent and everyone else follows it, including the authority. Two simulations of one player fighting over a position is what produced rubber-banding |
| Input | relayed to every client, not only the authority. Input is what makes a player fire, morph, lay a bomb or swing an alt attack; clients that received only positions drew opponents gliding in silence |
| Ammo | in the intent, alongside the position and the weapon. Everyone simulates a player's shots and spends the ammo; only the owner walks over the pickups that refill it. A puppet that has run dry makes its owner's shots vanish on the machine that decides what they hit |
| Ordering | every stream refuses a frame older than the newest applied -- intents, relayed intents, and snapshots. UDP reorders as a matter of course, and the snapshot is the one carrying health, score and the damage counter |
| One-frame presses | carried as an 8-frame history of rising edges, because a press exists in exactly one packet and UDP loses packets. Edges are taken *only* from that history: deriving them from the button level as well applied each press twice, which for a toggle means never |
| Snapshot | the authority's view of every player: health, score, form, weapon, zoom, and a damage record. Sent every frame |
| Damage | resolved only by the authority; every other client throws away locally-resolved hits. The authority stamps each hit with a counter, and victims replay the *difference* in that counter, so several hits between two snapshots are all accounted for |
| Score | carried in the snapshot. Counting locally worked only for whoever had been present since the first kill |
| Remote smoothing | remote players ease toward their reported position (35% of the gap per frame, 60% when it is wide) and only jump past 15 units, so a lost burst glides instead of popping |
| Slots | `PlayerEntity.SlotCapacity` (8). Every slot-indexed array is sized from it |
| Map rotation | the server owns it; clients poll the match state and load the new room, rebuilding every player slot and resetting the scores |

