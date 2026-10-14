# Running a Fruity Prime server

You do not need any of this to play online. **Host → Where: Online** in the launcher asks a public
machine to run the match and joins you to it, with nothing to open on your router. This page is for
running a machine of your own that is always up.

A game server **runs the match itself**, so it needs the game files. It reads
its settings/rotation and writes logs. Hosted rooms also use a generated
rotation file. Historical Raspberry Pi measurements are in
[NETWORK-SERVERAUTH.md](.claude/multiplayer/NETWORK-SERVERAUTH.md); current
Native performance on your server should be measured with `-simcheck`.

## Authority, hosting and hit registration

Both **Dedicated server** and **Hosted lobby** use a dedicated authoritative
server. The creator and the first participant are normal clients, including
when the server runs on their own PC. Lobby ownership allows editing and
starting the lobby; it grants no game-simulation or damage privilege.

Clients fire and predict hits immediately. The shooter reports hits through
hit claims, which the server can settle even when its own projectile replay
missed. Moving authority to the server preserves this shooter priority; it
does not make the client wait for the server's shot to hit. Existing claim
validation and duplicate-damage handling remain.

Hosted rooms run in separate processes with separate UDP ports. One physical
server can run multiple matches, while each room has its own session and
world state. A creator leaving does not stop or transfer the simulation.ｓｓｓ
Online directories clean up empty rooms; the local server process can outlive
the client that created it. Each room needs assets and its own memory.
Creating a room may wait for the child server to load its game files: the
launcher/CLI allow 35 seconds for the request, with a 30-second child startup
limit.

See [NETWORK-SERVERAUTH.md](.claude/multiplayer/NETWORK-SERVERAUTH.md) for the
Native implementation and validation.

```bash
# Linux
./FruityPrime -server -port 27888 -players 8 -servername "My server"
# Windows -- the console server binary when supplied by the package
FruityPrimeServer.exe -server -port 27888 -players 8 -servername "My server"
# Native Windows game builds also accept -server
FruityPrime.exe -server -port 27888 -players 8 -servername "My server"
```

The console binary lets a shell wait and read its exit code. A GUI executable
launched directly from a shell does not provide the same console behaviour;
the launcher's hosting paths start and monitor it as a child process.

## Game files are required

**This changed, and it is a breaking change.** A server used to be a relay: the
first client to connect ran the match, and the server only forwarded packets,
so it needed nothing. Now the server runs the match, which means it runs the
engine, which means it needs the files.

Put a `paths.txt` beside the binary pointing at them in the normal
Windows/Linux layout, the same file a client uses. macOS keeps it in the
application's user-data directory; see [.claude/build-deploy/MACOS.md](.claude/build-deploy/MACOS.md).

```
0.35.1.0
AMHP1=/home/you/fruityprime-server/files/AMHP1
```

A server without them **will not start**. It says so and exits:

```
[server] cannot run the match: game files could not be located
[server] a dedicated server runs the match itself now, so this one will not
         start. Put the game files on this machine and paths.txt beside the
         binary -- see SERVER.md
```

That is deliberate. The alternative was to fall back to relaying, and relaying
is the thing being removed: it put the match on a player's machine, where that
player's own shots resolved instantly while everybody else's took a round trip,
and where a disconnection required an authority handover. Hosted lobbies now
use the same server-authority model.

`-simulate` and `-authority` are still accepted and now do nothing -- an
existing systemd unit or launch script keeps working unchanged.

| Flag | |
|---|---|
| `-port N` | UDP port. Default 27888 |
| `-players N` | slots. Default 4, use 8 |
| `-servername "NAME"` | the name shown in the browser |
| `-rotation FILE` | default `maprotation.txt` in the platform user-data directory; beside the binary in the normal Windows/Linux layout |
| `-friendlyfire` | team damage on |
| `-instagib` | Instagib rule: everybody has the Imperialist with infinite ammo, one hit kills, nothing on the map can be picked up, and each shot leaves a Quake-style rail in the shooter's colour. Works with any game type; a server broadcasts it, so clients need nothing |
| `-lobby` | wait in a persistent lobby before the owner starts the match |
| `-format NAME` | match format, for example `Auto`, `FreeForAll` or `TwoVsTwo` |
| `-requireready on\|off` | whether participants must be ready before starting |
| `-joininprogress on\|off` | whether a running match accepts new participants |
| `-hostports A-B` | let this server start extra isolated rooms in this UDP port range |
| `-nomaster` | stay off every server list |
| `-master HOST` `-masterport N` | use a server list other than `net.livetek.fr:27889` |

## Ports

UDP only. Forward **27888** to the machine. The server list uses **27889**.

Your server is listed on `net.livetek.fr` automatically, so people find it in **Join → Find a
server**. Check it arrived with `FruityPrime -servers`, which prints the list the browser shows.
`-nomaster` keeps it private.

## Map rotation

`maprotation.txt`, one match per line, `#` for comments:

```
MP1 SANCTORUS      | Battle | 7 | 7
MP3 PROVING GROUND | Battle | 7 | 7
```

`ROOM KEY | mode | minutes | points`. Only the key is required.

`FruityPrime -rooms` lists every key — the 27 cartridge rooms and any custom map. It reads the game
files to do that, so run it on a machine that has them, not necessarily on the server.

## As a service

systemd units are in `tools/systemd/`:

```bash
sed -e 's|__USER__|youruser|' -e 's|__DIR__|/home/youruser/fruityprime-server|' \
    tools/systemd/mphread-server.service | sudo tee /etc/systemd/system/mphread-server.service
sudo systemctl enable --now mphread-server
```

## The server package

Use the `server-linux-x64` or `server-linux-arm64` tarball from a release. Unpack it into the
server's directory and run `./FruityPrime` there -- that file is a launch script. The game itself
is `bin/FruityPrime`, and it starts through the C library and loader in `lib/`, so the package runs
on any Linux with a kernel from the last ten years whatever its own glibc is. `paths.txt`,
`maprotation.txt`, `maps/` and `logs/` stay at the top, beside the script.

## Updates

A server and a server list update themselves: at start, and every ten minutes after, they ask
GitHub for the latest release, download its server package, and swap it in **once nobody is
connected**. Under systemd (`Restart=always`) the old build then exits and systemd starts the new
one; elsewhere it restarts itself. `-noautoupdate` turns this off. Two services sharing one
directory must not both update it -- give each its own directory.

## Your own server list

The list players' browsers ask is the same binary:

```bash
FruityPrime -masterserver -port 27889
```

Add `-public HOST` if a game server shares the box (its heartbeats arrive over the loopback, and the
address published for it has to be the one the internet can reach), and `-hostports A-B` for the
port range it may run matches on for players who cannot open one. Each
requested room is a dedicated child process, not a client-authority relay.
The directory alone can run without game files; actually hosting a requested
room requires game files and a compatible server executable on that machine.
Forward the chosen room port range as well as the directory port.

Point servers at it with `-master HOST`, and players in **Settings → Servers**.

## Versions must match

A server refuses clients with a different protocol at admission. The current
Native C++ build uses **protocol 33**; update the client and the server together.
An installed package from an older release may still speak an older protocol.
Wire fields and gameplay rules can both differ between versions, so matching
only the application name is insufficient. Hosted room startup also checks
that the child server answers with the expected protocol before joining it.
