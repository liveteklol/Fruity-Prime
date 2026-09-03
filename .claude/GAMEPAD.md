# Gamepads

A pad drives the game on the desktop and on Android, over USB or Bluetooth.
Bluetooth needs nothing of its own on either platform: by the time events
reach an application the operating system has already decided which driver
produced them, and a paired pad is an input device like any other.

| Path | What |
|---|---|
| `Mods/Input/GamepadState.cs` | the neutral snapshot both platforms fill in |
| `Mods/Input/GamepadInput.cs` | dead zones, the response curve, and what every button means |
| `Mods/Input/GamepadDesktop.cs` | the desktop reader, polling GLFW |
| `Mods/Input/GamepadProbe.cs` | `-gamepad`, the diagnostic |
| `MphRead.Android/GamepadBridge.cs` | the Android reader, accumulating key and motion events |
| `Mods/InputSettings.cs` | the four settings, saved to `controls.txt` |

## The layout

Xbox-shaped, because that is what both platforms hand over: GLFW remaps every
pad it recognises through SDL's controller database, and Android's own
`KEYCODE_BUTTON_*` constants are the same fifteen in the same places. A
DualShock's cross arrives as A from both.

| Pad | Action |
|---|---|
| Left stick | move (and roll, in the ball) |
| Right stick | aim |
| A | jump / boost |
| B | morph |
| X | scan |
| Y | scan visor |
| Right trigger | shoot, and the alt form's attack |
| Left trigger | zoom |
| LB / d-pad ← | previous weapon |
| RB / d-pad → | next weapon |
| D-pad ↑ | missile |
| D-pad ↓ | power beam |
| Back | scoreboard |
| Start | pause menu |

**No weapon wheel.** `PlayerHud`'s weapon select reads the *absolute* pointer
position, because on the DS it was a touch screen and the slot under the
stylus is the one that gets picked. A stick has no position, so driving it
would mean warping the mouse cursor about to fake one — which fights whoever
also has a hand on the mouse, and is a surprising thing for a controller to do
to a desktop. The bumpers and the d-pad reach every weapon without it.

## How it reaches the game

The same trick the Android touch controls use, from the other end: rather than
fork `ProcessAllInput`, `GamepadInput.Apply` waits until it has run and then
**ors** the pad's contribution onto the same `Keybind` flags the keyboard just
filled in. Everything downstream — firing, morphing, what goes on the wire as
an intent — reads those flags and cannot tell where they came from, so a pad
and a keyboard work at once, rebinding still applies, and not one call site in
upstream's input code changed.

Two things cannot go through a keybind:

- **Aim**, because a stick is analogue and a key is not. It goes in at
  `ApplyModAim`, which is the one hook upstream already offers inside the
  player's own input step, in the same units the mouse's arrives in (degrees
  of turn per frame) and at the same point in the frame. Aim applied a few
  lines earlier or later than the mouse's would feel different from the mouse
  for reasons nobody could name.
- **Start**, because the pause menu is not a thing the *player* does: it is a
  window the host platform opens. `TakeMenuPress` is consumed by whatever owns
  a window — `RenderWindow.OnRenderFrame` on the desktop, the render loop on
  Android.

## Feel

- **The dead zone is radial, not per axis.** Per-axis is the common mistake
  and it is visible: the neutral area comes out a square, so a stick pushed
  diagonally starts to move before one pushed straight, and slow circles turn
  into slow octagons. Past the edge the magnitude is rescaled, so the first
  movement past the dead zone is the smallest movement rather than a jump to
  the dead zone's own size.
- **The look curve is squared, keeping the sign.** The useful half of a
  stick's travel is the first half, where a shooter wants to make small
  corrections; linear, one stick has to do both the flick and the nudge and is
  bad at the nudge.
- **3.5 degrees per frame at full deflection** — 210 a second, which is where
  console shooters have sat since they settled the question. The sensitivity
  setting runs 0.25x to 3x around it.
- **Walking is a threshold, not a curve** (half deflection), because the walk
  keys are on or off. Larger than the aim dead zone: a thumb resting on the
  stick should not walk you off a ledge.

## Settings

`controls.txt`, and the launcher's Controls page has a Gamepad section:

```
gamepad=true
gamepad_deadzone=0.2
gamepad_look=1
gamepad_invert_y=false
```

Vertical invert is its own setting rather than sharing the mouse's, because a
great many people invert one and not the other.

## Checking it

```
FruityPrime -gamepad [-seconds N]
```

Prints the pad's name, every axis as it moves, and **which game action each
button reaches**. It exists because "my controller does nothing" has four
causes that look identical from inside a match — the pad is not connected,
GLFW has no mapping for it, the dead zone is eating the sticks, or the mapping
is wrong — and this separates them. In particular it distinguishes a pad that
is *present* from one that is *mapped*: an unmapped pad is ignored by the game
entirely and looks exactly like nothing being plugged in.

### Testing without a pad

A virtual one, on Linux, through `uinput`. The vendor and product ids are
Microsoft's real ones, which is what makes GLFW's controller database
recognise it and hand the game a mapped gamepad rather than a nameless
joystick:

```python
from evdev import UInput, AbsInfo, ecodes as e
ui = UInput(caps, name="Microsoft X-Box 360 pad", vendor=0x045e,
            product=0x028e, version=0x110, bustype=e.BUS_USB)
ui.write(e.EV_ABS, e.ABS_Y, -32768); ui.syn()   # left stick forward
```

Needs `python3-evdev` and root for `/dev/uinput`, and the device it creates is
root-owned — `chmod a+r /dev/input/event*` afterwards or the game cannot read
it. Under WSL there is no `/dev/input` at all until the first such device is
created.

Measured this way on 2026-09-03, against a real match started from the text
launcher: every button reached the action listed above, full right-stick
deflection produced exactly the 3.5 degrees a frame, a stick at 0.12 (inside
the 0.20 dead zone) produced nothing while 0.37 produced a small correct turn,
and the player walked about 40 units across MP3 Proving Ground on the left
stick alone. Unplugging mid-run was handled by the rescan without a stall.

## Known gaps

- **Android is unmeasured.** The bridge compiles and the shared half of it is
  the half that was tested above, but no pad has been held against the APK:
  the emulator here would not keep its package service up long enough to
  install one, and there is no phone on this machine.
- **The launcher itself is not navigable with a pad** — only a match is. The
  front screen is Avalonia and would need its own focus handling.
- **No rumble, and no per-button rebinding.** The layout above is fixed; the
  four settings are the whole of what can be tuned.
- **One pad.** The first that answers wins, since nothing here has a second
  player to give a second pad to.
