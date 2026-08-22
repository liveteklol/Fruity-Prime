# Multiplayer — match end, rotation and state

This document explains how matches end and how rotation and the results interlock between server and clients.

Match ending flow

- Previously a client-side win faded to black and clients dropped back to their launcher while the server rotation kept counting a clock. Now:
  - `NetMatchEnd` on the authority sends `PacketType.MatchEnd` when `GameState.MatchState` leaves `InProgress`, repeating until the server's own state has `FlagEnding`.
  - `DedicatedServer` intermission: both clock and score endings enter the same 9-second intermission before `AdvanceMap`.
  - `MatchStatePacket.MatchId` counts matches from the server's start so clients can decide whether a match is new.
  - `MatchStatePacket.PointGoal` is carried by the server and decides when a match ends.

Important fixes

- `NetMatchSync` must not adopt the clock during the results; `MatchTime` is for the results sequence.
- The authority must stop reporting once the server has heard the end to avoid skipping a map during rotation.
