# Running a Fruity Prime server

You do not need any of this to play online. **Host → Where: Online** in the launcher asks a public
machine to run the match and joins you to it, with nothing to open on your router. This page is for
running a machine of your own that is always up.

A server needs **no game files** and keeps nothing on disk. A Raspberry Pi is enough.

```bash
# Linux
./FruityPrime -server -port 27888 -players 8 -servername "My server"
# Windows -- the console binary, not FruityPrime.exe
FruityPrimeServer.exe -server -port 27888 -players 8 -servername "My server"
```

| Flag | |
|---|---|
| `-port N` | UDP port. Default 27888 |
| `-players N` | slots. Default 4, use 8 |
| `-servername "NAME"` | the name shown in the browser |
| `-rotation FILE` | default `maprotation.txt`, written beside the binary on first run |
| `-friendlyfire` | team damage on |
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

Stop the service before replacing the binary — systemd holds the file open, and .NET maps it into
memory, so copying over a running one takes the process down in a way nothing explains.

`deploy-server.sh` does build, upload, units and restart against a remote box in one go.

## Your own server list

The list players' browsers ask is the same binary:

```bash
FruityPrime -masterserver -port 27889
```

Add `-public HOST` if a game server shares the box (its heartbeats arrive over the loopback, and the
address published for it has to be the one the internet can reach), and `-hostports A-B` for the
port range it may run matches on for players who cannot open one.

Point servers at it with `-master HOST`, and players in **Settings → Servers**.

## Versions must match

A server refuses a client built against a different protocol, at the first packet, with a line in
its log. That is deliberate: the wire format does not move between versions, so an old client would
read every byte correctly and then play a different game. Update the server before handing out a
client built from a newer release.
