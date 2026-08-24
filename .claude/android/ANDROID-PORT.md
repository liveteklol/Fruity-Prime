# Android — the renderer and the touch controls

The head is `src/MphRead.Android/`. It compiles the same sources as the desktop
project with `ANDROID` defined, and it now builds a match, not just a screen.
Two things had to be answered to get there: the engine draws through OpenTK's
desktop GL, and it reads a keyboard and a mouse. Neither exists on a phone.

## The renderer: one alias

The engine calls `GL.Something` from about 250 places in files that are
upstream's. Rewriting those was not an option -- everything this project adds
lives under `Mods/` so a pull from NoneGiven/MphRead stays a fast-forward. So
the Android head points the *name* somewhere else, in one line of its csproj:

```xml
<Using Include="MphRead.Mods.Render.GlEs" Alias="GL" />
```

A global using alias is resolved before the `using OpenTK.Graphics.OpenGL;` at
the top of those files, in every namespace of the compilation (checked: it wins
in `MphRead` and in `MphRead.Formats` alike). Not one call site changed, and the
desktop build never sees it. The enum types the call sites pass are still the
desktop ones -- they are the same GL constants -- and `GlEs` casts them across
to `OpenTK.Graphics.ES30`.

`Mods/Render/GlEs.cs` then answers the four things ES 3.0 does not have:

| Missing | Answer |
|---|---|
| Immediate mode | `Begin`/`Vertex3`/`End` accumulate into a vertex buffer. Quads, quad strips, triangle strips and fans become indexed triangles; `LineLoop` becomes lines |
| Display lists | `NewList`/`EndList` bake that buffer into a VBO, an IBO and a VAO. `CallList` is one `glDrawElements` -- the same trade the display list was making |
| The current colour | A vertex with no colour of its own takes the colour current at *execution* time, which is the `GL.Color3(item.Diffuse)` `DoMaterial` issues per render item. Vertices carry a flag; the ones without their own colour read the `imm_color` uniform |
| `glAlphaFunc` | The engine asks for exactly two comparisons, Equal 1.0 and Less 1.0. The fragment shader discards on them |

And one thing that is bookkeeping rather than emulation: the engine binds
texture names it made up itself (`_textureCount++`), which a compatibility
profile allows and ES does not. `GlEs` keeps a map from those to real names and
creates one on first bind, with a single high-water counter so `GenTexture`
keeps handing out the next number in the engine's own sequence.

**What is lost:** `glPolygonMode`, so the wireframe and collision-volume debug
views draw solid. Nothing a player sees uses it.

## The shaders

`Mods/Render/EsShaders.cs` holds the five shaders of `Shaders.cs` written for
`#version 300 es`. The desktop ones are GLSL 1.20 reading `gl_Vertex`,
`gl_Color`, `gl_Normal` and `gl_MultiTexCoord0` out of the fixed-function
pipeline -- which is *why* the desktop build needs a compatibility profile and
`MESA_GL_VERSION_OVERRIDE=4.5COMPAT`. The ES ones declare those four as
attributes at fixed locations 0-4 and are otherwise the same program, expression
for expression, plus `imm_color`/`a_color_set` and `alpha_test`.

`GlEs.ShaderSource` substitutes them by reference as the engine compiles. They
do not follow a change to `Shaders.cs` on their own, and a silent divergence
would be a rendering bug with no message anywhere, so each is checked against
the SHA-256 of the source it was written from and a mismatch throws with the
name of the shader that moved. Recompute with the extractor in the commit
message or by hand: normalise CRLF to LF first.

Verified with `glslang` (16.5.0): all three programs -- main, RTT, and the shift
program that shares the RTT vertex shader -- compile and link clean as GLSL ES
3.00. That is an offline check of the riskiest file, not a check that the
picture is right.

## The loop

`GameView` is a `GLSurfaceView`, which supplies what GLFW supplies on the
desktop: an EGL context, a thread that owns it, and a callback per frame.
Everything the engine does with GL -- a room's textures, its baked geometry, the
shaders, the drawing -- happens on that thread, so the scene is *built* inside
`OnSurfaceChanged` rather than by whoever asked for the match. Loading blocks
that thread for seconds, which is the right thread to block: the UI thread stays
free and the loading notice on top keeps drawing.

The order is the desktop's: `GameState.ApplyPause()`, `Scene.OnUpdateFrame()`,
`Scene.OnRenderFrame()`, `Scene.AfterRenderFrame()`.

The pacing is not. `RenderWindow` asks OpenTK for 60 updates a second; here the
callback arrives once per display refresh, which on a modern phone is 90 or 120.
**The update is the frame**: the render item lists are built during the update
and cleared after the draw, so a render with no update in front of it draws
nothing. Rendering more often than the game ticks is therefore not an option --
the thread waits for the next 60 Hz tick instead.

## The controls

No hook in the engine's input path, and none needed. `ProcessAllInput` reads a
`KeyboardState` and a `MouseState` once a frame and turns them into the
`Keybind` flags the player code reads. `AndroidInput` hands the scene a keyboard
and a mouse of its own and presses the keys the player has bound. OpenTK does
not let anyone else build those two types, so the constructors and setters are
reached by reflection, once, into delegates.

Three things come free from doing it that way:

- **Rebinding works.** A thumb on FIRE presses whatever `Controls.Shoot` says,
  key or mouse button.
- **Sensitivity and inverted aim work**, because aiming moves a pointer and the
  engine reads the same delta it always did.
- **The weapon wheel works**, because it was a touchscreen mechanic on the DS:
  `UpdateWeaponSelect` reads the pointer's absolute position. While WEAPON is
  held the pointer *is* the finger; the rest of the time it accumulates drag.

Layout (`TouchControls`, drawn by `TouchOverlayView` with a Canvas rather than
in GL -- it is a dozen circles that change when touched):

| Control | Bind |
|---|---|
| Floating stick, left half | `MoveUp/Down/Left/Right` **and** `RollUp/Down/Left/Right`, eight-way. Both sets, because walking reads one and the morph ball the other |
| FIRE | `Shoot` |
| JUMP | `Jump` **and** `Boost` -- one button on the DS, and the same key by default: jumping on foot is boosting in the ball |
| MORPH | `Morph` |
| ALT | `AltAttack` |
| WEAPON | `WeaponMenu`, held, with the pointer following the finger |
| ZOOM | `Zoom` |
| MENU | `Pause` |
| Anywhere else on the right | aim |

Drag is converted to density-independent pixels before it becomes pointer
movement, so a swipe turns the same amount on any phone; `GameView.AimScale` is
the one number to change if it feels wrong, and the player's own mouse
sensitivity scales it after that.

## Game files

The package's own directory is read-only, and the extracted game files are
hundreds of megabytes the player has to copy onto the device themselves. So both
`LauncherPrefs.Directory` and `GameFiles.Root` (added for this) point at
`GetExternalFilesDir(null)` -- the directory reachable over USB under
`Android/data/fr.livetek.fruityprime/files` without the app asking for a storage
permission -- and that is made the working directory, because upstream's `Paths`
reads `paths.txt` relative to it.

Extract on a desktop, then copy `paths.txt` and the extracted folders there. The
front screen says so when they are missing and has a "Check for game files"
entry, since they arrive while it is already up.

## Sound

Half of it is there by accident of packaging and half is not, and the split is
worth knowing before someone calls it broken.

- **Music** goes through SoundFlow, which ships `libminiaudio.so` for Android;
  it is in the APK. Whether it plays is untested.
- **SFX** go through OpenAL (`OpenTK.Audio.OpenAL`), and no `libopenal.so` is in
  the APK. `Sfx.Load` catches the failure and installs the silent stub, the same
  path a desktop with no audio device takes, so this is quiet rather than fatal.
  Giving Android sound effects means shipping an OpenAL Soft build for
  `arm64-v8a`.

## Building

```bash
export PATH="$HOME/.dotnet:$PATH"
export JAVA_HOME=$HOME/jdk17            # a JDK 17; the workload does not bring one
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1   # or install libicu
dotnet workload install android
dotnet build src/MphRead.Android/MphRead.Android.csproj -c Debug \
  -p:AndroidSdkDirectory=$HOME/android-sdk
```

The SDK needs `platforms;android-35` and `build-tools;35.0.0` to match the
`net9.0-android35.0` target; `sdkmanager --sdk_root=$HOME/android-sdk` installs
them. `EnableAvaloniaXamlCompilation=false` is deliberate and explained in the
csproj.

The APK lands in `bin/Debug/net9.0-android35.0/fr.livetek.fruityprime-Signed.apk`
(~20 MB; a Release publish is ~45 MB, being every ABI with the trimmer run).
`adb install -r` it.

Nobody has to do any of that to get one, though: `build.yml` has an `android`
job on every push, and its **FruityPrime-android** artifact holds a release APK,
an untrimmed debug APK beside it, and an INSTALL.txt. `release.yml` puts
`FruityPrime-<tag>-android.apk` in a tagged release. Both are signed with the
SDK's debug key -- enough to install, not enough for a store.

**The trimmer is why `Properties/TrimmerRoots.xml` exists.** A Release publish
really does run ILLink over this app, and every use of `KeyboardState` and
`MouseState` is through a `MethodInfo`, so without the descriptor the touch
controls work in Debug and throw on the first frame of a release APK. Checked,
not assumed: the members survive in
`obj/Release/.../android-arm64/linked/OpenTK.Windowing.GraphicsLibraryFramework.dll`.

## What has not happened

**No device has run this.** See `.claude/KNOWN-GAPS.md`. It compiles, the
shaders are validated offline, and the desktop build is unaffected -- and that
is the whole of what is proven. The first run should be watched for, in this
order:

1. `[gles] shader ... failed to compile` in logcat. The compile status is only
   read under a debugger upstream, so `GlEs.CompileShader` logs it.
2. `Scene.FramebufferStatus` -- the offscreen target is an unsized `GL_RGB`
   colour texture with a `DEPTH24_STENCIL8` renderbuffer. RGB8 is
   colour-renderable in ES 3.0, but a driver that disagrees makes the whole
   picture black while every other signal looks fine.
3. Geometry that is inside out or missing: that is the strip/quad/fan winding in
   `GlEs.EmitIndices`, which is the one piece of this with no test behind it.
4. Untextured or wrongly-coloured meshes: that is the `imm_color`/`a_color_set`
   path, i.e. a mesh whose display list never set a colour of its own.
