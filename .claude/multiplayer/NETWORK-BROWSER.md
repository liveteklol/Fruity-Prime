# Multiplayer — server browser and directory

This file covers server discovery, the directory (master), the browser, and hosting choices.

Server browser

- "Find a server" opens a card on the front screen with a scrolling list. Rows come from the directory and are confirmed by sending a `StatusQuery` to the server for map, mode, head count and round trip.
- The address shown is the one the heartbeat arrived from, not necessarily the server's private address.

Directory / Master server

- `MphRead -masterserver` (see `Network/NetMaster.cs`) implements the directory. Servers announce themselves every 15 s; entries expire after 50 s.
- Default master: `net.livetek.fr:27889` is configured in `NetMasterConfig`. Note: the default name may not be currently resolvable; launcher and server can be configured to point elsewhere.

Hosted games and listing

- Hosting via the launcher runs the dedicated server in-process and by default lists it. `ListHostedGame` toggles whether to publish the host's game to the directory.
- `HostRequest`/`HostReply` allow the launcher to request a hosted server from the directory, which starts a `DedicatedServer` on a port from its range `-hostports`.
