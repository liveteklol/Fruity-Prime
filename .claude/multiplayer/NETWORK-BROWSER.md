# Multiplayer — server browser, directory, and hosting without a port

## Asking a server what it's running

`PacketType.StatusQuery`/`StatusReply` (`NetStatus`) answers "what map, what
mode, how many players" **without claiming a slot** — what lets the front
screen poll a server every few seconds while somebody reads the screen. A
server built before that packet ignores it, so the launcher falls back to a
Hello/Bye join probe: that one *does* take a slot, is refused outright by a
full server, and counts itself among the players (`NetStatus` subtracts it) —
so it's used only until the cheap path answers once, then rarely.
**Redeploy the server** after taking a build with `StatusQuery` to get the
cheap path.

## The server browser

**Play online opens the list, not a form.** The address is the one thing a
new player can't invent, so the list comes first (with a field for an address
someone was handed directly), and only then the card asking for a name and
hunter.

It's a **card on the front screen**, not a window over it — same shape as
Play online/Host a game/Game files, its own scrolling list sized by the same
spacer arithmetic. It began as a separate `Form`, which was wrong twice over:
a popup over a launcher that is itself custom-painted reads as a different
program, and `Form` is the one thing in this codebase that can't be exercised
headless.

Rows come from the directory and are then **confirmed by this machine**: one
`StatusQuery` each, answering map/mode/head count/round trip in one exchange,
failing for exactly the servers this player couldn't have joined anyway. Rows
appear as they answer, sorted by players then latency.

**A hosted game can be listed too.** *Host a game* runs the dedicated server
in the player's own process, so it can be found the same way — but it's
somebody's home machine, and listing publishes its address. `ListHostedGame`
is a switch on the card, on by default (a game nobody can find is a game
nobody joins), and the server is named after the host, not their PC.

## Hosting without opening a port

Being listed isn't being reachable — most people can't or won't forward UDP
from their router, which made *Host a game* work on a LAN and nowhere else.

The fix (same one Age of Empires II: DE uses, and it isn't NAT traversal):
**the match runs somewhere reachable and the host joins it by connecting
out**, like everybody else. This engine's netcode is already "everyone
connects to one relay," so putting the relay somewhere with an open port is
the whole of the work — no relay framing, no punching, no new transport path,
**no client change at all**.

| Piece | What |
|---|---|
| `HostRequest`/`HostReply` | launcher → directory: room, mode, time limit, point goal, cap, name. Directory starts an ordinary `DedicatedServer` on a port from its range and answers with the port |
| `-hostports 27900-27919` on the directory | the range it may use, one port per game. Default on — a feature that has to be configured to work is a feature nobody has. `-hostports none` disables it |
| Host card: "Online, no setup" vs "On this PC" | first is default and hides the port/listing rows — nothing to choose, findability is the point |
| `MphRead -hostgame "ROOM" [-mode M]` | same thing from a command line — the only way to host with no launcher |
| `HostedIdleSeconds` (180) | an unjoined game is shut down and its port returned. Generous, since the usual reason one's empty is that the requester is still loading the map |

Hole punching was the other candidate and wasn't worth it: needs a rendezvous
protocol, needs a relay fallback anyway, and has a failure mode for every
symmetric NAT. This has none.

Measured end to end: `-hostgame "MP6 HEADSHOT"` asked the directory, got port
27900, joined it, became authority, loaded the room; the directory listed it
correctly; a second client joined as slot 1 with no special handling.

## The directory (master server)

| Piece | What |
|---|---|
| `MphRead -masterserver` (`Network/NetMaster.cs`) | the directory. Servers announce every 15 s, entries expire after 50 s of silence, `MasterQuery` returns the list in as many datagrams as it takes. Relays no gameplay, stores nothing, can share a box with the server it lists |
| `MphRead -servers [-master HOST]` | prints the same list the browser would show — exercises that data path on a machine with no display |
| `MasterReporter` | the server's end: one datagram every 15 s, every failure swallowed and retried (a directory outage must never touch a match), first failure logs a line |
| Server browser | rows confirmed by a `StatusQuery` each, as above |

Two decisions worth keeping:

- **The address in the list is the one the heartbeat arrived from**, not the
  one the server believes it has — a server behind a router only knows its
  private address, and a directory full of `192.168.x.x` is useless. Port
  comes from the heartbeat too, since a datagram's source port isn't
  necessarily the one it listens on.
- **Latency is measured by the launcher, not reported by the directory** —
  the master could only ever report its own round trip to each server, not
  the number the person reading the screen cares about.

`net.livetek.fr:27889` is the configured default (`NetMasterConfig`) — a
hostname, deliberately unlike the *game* server default (an address on
purpose), because a directory has to be able to move without a new build
reaching every operator. **That name does not currently resolve.** Both ends
are pointed at the Pi's other name instead: launcher via
`master_host=net.livetek.fr` in `launcher.txt`, server via
`-master 127.0.0.1` (shares the directory's box).

`tools/systemd/mphread-master.service` is the unit; `deploy-server.sh`
installs both it and the game server's, filling in user/directory, and
**leaves an existing unit alone** on later deploys — so the two hand-added
options on the Pi (`-master 127.0.0.1` on the server, `-public
net.livetek.fr` on the directory) survive a redeploy and aren't in the
templates.

Two things had to be true before anything appeared in a list, neither visible
from the code:

- **`-public` on the directory.** Right for a server behind a router, exactly
  wrong for a server sharing a box with the directory — that heartbeat
  arrives from `127.0.0.1`, and a list of loopback addresses sends every
  player to their own machine. `-public net.livetek.fr` tells the
  directory once what to publish for anything registering from loopback or a
  private range.
- **UDP 27889 through the firewall**, separately from the game port. `ufw`
  on the Pi allowed 27888 and nothing else, so the game server answered from
  outside while the directory timed out — which looks exactly like a
  directory that isn't running. Check `sudo ufw status | grep 2788` before
  believing anything else.
- **Being listed and being reachable are different things.** A server behind
  a home router registers fine (directory records the heartbeat's public
  source) and is then unjoinable because nothing forwards UDP to it. The
  browser shows this honestly as a red "did not answer" row, which is the
  signal to check the router, not the server.

`ServerStatus`/`MasterListing` return `""` rather than `null` through backing
fields — they're structs, so `default` is an ordinary value the browser holds
for every row it hasn't probed yet, and a plain auto-property would hand that
row a null to call `.Length` on, crashing the window on the first row of the
first list anyone opened. No headless check could have caught it; `-servers`
exists partly so this path is checkable without a Windows box.
