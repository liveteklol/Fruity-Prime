# Chat

T opens a line, Enter sends it, Escape throws it away. Three messages are on
screen at once, small, green, top left, on nothing -- no plate behind them;
each one is gone ten seconds after it arrived. That is Quake 3's shape and it
was chosen because it is the one every player already knows how to read.

**The chat log has its own font**, and that is the largest single piece of
this. See `Mods/Chat/ChatFont.cs`: the game's own font is 8x8, bold, and has
one alphabet, so every line anybody typed came out shouted and about half as
wide again as the sentence needed. `ChatFont` is a proportional pixel font
with real lowercase, real descenders and per-glyph widths (2 units for an i,
6 for an m), caps six rows tall instead of eight. It is authored as pixel art
in the file rather than shipped as an asset -- 95 glyphs, editable in place,
and nothing for `tools/check-no-game-assets.sh` to have an opinion about. The
chat HUD therefore does **not** go through `DrawText2D`: `ChatDraw` is that
routine's Align.Left branch with a different font in it and the Kanji handling
dropped, since chat is ASCII by the time it arrives.

The frame-rate counter used to be in that corner and is now in the right-hand
one. Nothing competes for the right: the ammo readout is at the foot of the
screen and the scoreboard is drawn down the middle. On Android the chat log
keeps the 30-unit inset the counter used to have, because the MENU button is
drawn over the scene in that same corner.

## Where it lives

| Path | What |
|---|---|
| `Mods/Chat/ChatBox.cs` | the log, the prompt, and every key press while the prompt is up |
| `Mods/Chat/ChatFont.cs` | the font, as pixel art, and the width table derived from it |
| `Mods/Chat/PlayerEntityChatHud.cs` | the drawing, as a partial of `PlayerEntity` |
| `Mods/Network/NetProtocol.cs` | `PacketType.Chat` and `ChatPacket` |
| `Mods/Network/NetSession.cs` | `SendChat`, and the received line |
| `Mods/Network/DedicatedServer.cs` | `HandleChat`: attribution, the rate limit, the relay |
| `Mods/InputSettings.cs` | `ChatKey`, saved to `controls.txt` as `chat_key` |
| `MphRead.Android/GameView.cs` | Android's key events, and the soft-keyboard editor |
| `MphRead.Android/TouchControls.cs` | the CHAT button |
| `~/mph-net-test/probe-chat.py` | the three things `-netcheck` cannot ask |

## The protocol

**No version bump.** `PacketType.Chat` is 23, additive and ignorable in both
directions -- the same argument `RefusedPacket` makes. A server built before
this drops the type on the floor: the sender still sees its own line, nobody
else does, and the match is otherwise unaffected. A client built before it is
never sent one. So chat can be deployed without taking every running match
offline, and `NetConfig.ProtocolVersion` stays at 4.

What that costs is the one failure mode worth knowing about: **against a
server that predates this build, chat looks exactly like nobody answering.**
Redeploy the server (`deploy-server.sh`) and it works.

`ChatPacket` is fixed at 114 bytes: slot, kind, a 16-byte name and 96 bytes of
text, all ASCII with anything unprintable replaced by `?` on the way in *and*
on the way out -- so a hostile client cannot put a control character on
anybody else's screen.

**The server writes the slot and the name, and does not trust the sender's.**
A client can put any name it likes in those fields, and a line that appears to
come from somebody else is the entire attack; the endpoint a datagram arrived
from is the only thing here that cannot be typed into a text box. The client
fills them in anyway, so that a demo recorded against a server that predates
chat still replays with a name attached.

**Rate limited at the relay**, which is the only place it can be: a flood from
one client is multiplied by the number of people in the match and paid for by
all of them. A leaky bucket, three deep, refilling at one message every two
seconds -- faster than anybody types, slower than anything worth calling a
flood. An interval instead of a bucket would punish two quick lines of one
thought exactly as hard as a hundred. Dropped lines are logged once per burst,
not once per packet, so the flood does not become the log's problem too.

Kinds are `Say`, `Team` and `System`. **Team is reserved and relayed as Say**:
nothing yet asks the server which side a slot is on, and delivering a line to
everybody while telling the reader it went to one team is worse than not
having the channel.

**24 and 25 are left free for voice.** Speech is a stream of frames, not a
line of text: it wants its own packet type, its own cadence and its own "who
is talking" packet, and putting an audio codec inside the packet the
scoreboard reads would be the wrong shape. Nothing in the current wire format
has to change when it arrives, because the relay forwards what it recognises
and drops what it does not -- a build that speaks voice and a build that does
not can share a match.

## Where it is not

**Never in the story.** `ChatBox.Available` is `!GameState.SinglePlayer`, and
both adventure paths (`MatchStart.LaunchAdventure` and
`AndroidMatch.BuildAdventure`) set `GameState.Mode = GameMode.SinglePlayer`.
There is no server, no roster and nobody to read a line; T goes back to
meaning whatever it meant before, and the log does not draw. The check is
inside `ChatBox` rather than in each caller, so Android gets it for free.

Not "is a session running", though: an **offline match against bots** still
has chat. Nothing leaves the machine, and the log is where the game's own
notices go.

## On Android

Two ways in, because a phone has no T to press:

- **A CHAT button**, third along the top row past MENU and SCORE. It shows
  only while a networked match is running -- offline there is nobody to read a
  line -- and pressing it opens the prompt *and* asks for the soft keyboard.
  The IME call belongs to the UI thread and the button is read on the GL one,
  so `GameView` asks and `MainActivity.ShowSoftKeyboard` does it, the same
  arrangement the pause menu has.
- **A keyboard**, if one is attached -- USB, Bluetooth, or the emulator's.
  `GameView.OnKeyDown` handles it, and T opens the prompt exactly as on the
  desktop.

Three things about that are worth knowing:

- **`InputTypes.Null` is what makes the soft keyboard usable at all.** It
  tells the IME this view cannot be edited through `commitText`, and every
  keyboard worth the name answers by sending plain key events instead --
  which is the path `OnKeyDown` already handles. The alternative is an
  `InputConnection` holding an editable buffer kept in step with `ChatBox`'s,
  which is two copies of one string and a second set of rules for composing.
- **`NoFullscreen | NoExtractUi`**, or the IME replaces the whole screen with
  its own text box in landscape -- which is every phone playing this.
- **Android delivers the character with the key event**, where GLFW raises two
  callbacks for one press. So `HandleKey` is both of the desktop's paths in
  one method, and it opens the prompt with `swallowOpeningChar: false`: there
  is no second delivery to swallow, and swallowing anyway would eat the first
  real letter.

## Traps

- **The key that opens the prompt types its own letter.** GLFW raises the key
  callback before the character callback for the same physical press, so
  opening on T and then accepting text put a stray "t" at the front of every
  message. `ChatBox` swallows exactly one character after opening.
- **A held key stays held.** `ProcessInput` skips the local player entirely
  while chat has the keyboard, and a keybind's `IsDown` is only refreshed
  inside the pass that was skipped -- so somebody who opened chat mid-stride
  walked into the nearest wall for as long as they typed. The renderer clears
  the local player's controls *every* frame the prompt is up, not once when it
  opens.
- **And the mouse keeps moving.** Aim is the difference between two mouse
  positions a frame apart, and the stored one stops advancing while the player
  types -- so the frame the prompt closed on carried however far the mouse had
  drifted across the whole message and snapped the view round by all of it.
  `ModForgetInputDeltas` throws that one frame's difference away.
- **Chat draws before the pause and spectator early-returns** in
  `DrawHudObjects`, like the frame counter above it. Somebody watching a match
  is in the position where reading what the players are saying is most of the
  point.
- **Debug builds lose the texture-list toggle in a match.** `Keys.T` toggles
  `_showTextures` inside `#if DEBUG` in `Scene.OnKeyDown`, and chat takes the
  key first whenever the camera is the player's. It still answers in the model
  viewer's own camera modes, which is where that toggle is used.
- Text comes from `OnTextInput`, not from the key events: GLFW reports
  physical keys, so a message read out of those would be spelled in US QWERTY
  whoever wrote it.

## Checking it

`-netcheck` clients each say two lines, at frames 300 and 1500, and report
`chat: sent=N received=M`. With N clients everyone should hear `2*(N-1)`: a
client's own lines are echoed locally and never come back.

```
cd ~/mph-net-test && ./run-check.sh 60 Samus Weavel Sylux
grep 'chat:' *.log          # sent=2 received=4 on all three
grep chat server.log        # six lines, each attributed to the right name
```

`probe-chat.py` asks the three questions a real client cannot, by sending what
no real client would send:

```
./probe-chat.py [host] [port]     # default 127.0.0.1 27999
```

1. a line reaches everybody else and not the sender;
2. a line labelled with somebody else's slot and name arrives labelled with
   the sender's own;
3. a flood is cut off after the burst allowance, and the same client is
   talking again once the bucket has refilled.

Verified against the real server binary on 2026-09-03: all three pass, and a
three-client 60 s tour reported `sent=2 received=4` on every client with zero
mismatches elsewhere.

## Not done

- **No settings-screen row.** The key is rebindable through `controls.txt`
  (`chat_key=T`, or `none` to give the key back), but the launcher's controls
  page lists `PlayerControls` properties and this is deliberately not one of
  them -- see the comment on `InputSettings.ChatKey`.
- **No team channel and no voice**, per the protocol section above.
- **The Android CHAT button hides offline.** A local match against bots does
  have chat on the desktop (see "Where it is not"), and on a phone with no
  keyboard attached there is no way to reach it. Nobody would read it either
  way, so the button is tied to `NetSession.Active`.
