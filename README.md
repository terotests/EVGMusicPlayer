# EVG Player

A skinned music player in the spirit of the old Windows Media Player skins:
the player is a shape, not a window, its chrome is painted, and the round
screen in the middle is a GPU visualiser driven by the music. Written in
Ranger and drawn by EVG: in a browser with EVG's WebGL painter, and as a
native desktop app whose window is the shape of the skin.

**License: AGPL-3.0-or-later** (see `LICENSE`).

## Building

The player is compiled with [Ranger](https://github.com/terotests/Ranger)'s
committed compiler and uses its `lib/evg`, so it needs a Ranger checkout,
by default beside this one:

```sh
git clone https://github.com/terotests/Ranger
git clone https://github.com/terotests/EVGMusicPlayer
(cd Ranger && npm ci)              # puts lib/evg in place
cd EVGMusicPlayer
npm run web                        # compile, assemble, serve on http://127.0.0.1:8131/
```

`--ranger <dir>` or `RANGER_DIR` names another checkout
(`node scripts/build.mjs --ranger ~/src/Ranger`). The build copies
`PlayerApp.rgr` and `ranger.json` to `<ranger>/gallery/evgmusicplayer/`
before compiling, so `ranger.json`'s `../../lib/evg` resolves to that
checkout's EVG. `node scripts/build.mjs --out site/` also writes a
deployable copy of the page.

Open files with the note button, a whole folder with the folder button, or
drop files anywhere on the page. `?demo` runs the visualiser on made-up
levels without any audio.

| Control | |
| --- | --- |
| drag the body | moves the player around the page |
| ⏮ ▶/⏸ ⏭ ⏹ | transport (⏮ restarts the track after 3 s) |
| seek bar | click or drag |
| mouse wheel | volume |
| bars button / `v` | visualiser: ring, tunnel, bars |
| drop button / `k` | skin: ice, lime, ember |
| space, ← →, `s`, `o` | play/pause, previous/next, stop, open |

## Native app (macOS, Linux)

The same `PlayerApp.rgr`, compiled to C++ by Ranger, in a borderless window
cut to the skin's outline: nothing around the player, clicks outside it go to
whatever is behind, and the body drags the window.

```sh
# macOS: brew install sdl2            Linux: sudo apt-get install libsdl2-dev libgl-dev
npm run native                        # -> native/build/evg-player (+ "EVG Player.app" on macOS)
native/build/evg-player ~/Music/*.mp3 # or open it and use the note / folder buttons
npm run native:check                  # smoke check: transparent corners, a track playing
```

Plays MP3 (dr_mp3) and WAV. Close and minimise are the two small buttons on
the crest; Esc or Q also quits. Files can be passed on the command line or
dropped on the window. The file and folder buttons use `osascript` on macOS
and `zenity` or `kdialog` on Linux.

How the shape is made:

- **macOS** — the window is not opaque, its background is clear and the
  OpenGL surface's opacity is 0 (`native/mac_window.mm`). The alpha the
  painter leaves is the window's alpha, so the edge is antialiased and the
  system shadow follows the outline.
- **Linux (X11)** and other SDL2 platforms — `SDL_CreateShapedWindow` with a
  mask read back from the first frame. The edge is hard (1 bit), since X11
  has no per-pixel window alpha without a compositor.

`native/`:

- `host.cpp` — the window, input, file pickers and the frame loop: web/main.js
  for a desktop. It includes the Ranger-generated `PlayerApp.cpp`.
- `painter.cpp` — EVG's display-list JSON drawn with OpenGL 3.3: rounded and
  gradient boxes with shadows, stencil-filled paths, strokes, text through
  stb_truetype, and the surface effects. The effects are the same GLSL files
  the browser compiles (`shaders/`).
- `audio.cpp` — playback through SDL audio and the analyser: a Blackman
  window, 2048-point FFT, the browser analyser's smoothing and dB range, then
  the same 32 bands as the page.
- `third_party/` — `stb_truetype.h` and `dr_mp3.h` (public domain / MIT);
  `fonts/` — Noto Sans (Apache 2.0, `fonts/LICENSE-Apache-2.0.txt`).

The macOS build has not been run yet: it was written against the same SDL2
and OpenGL calls the Linux build uses and checked there (under Xvfb with
Mesa), plus the AppKit calls in `mac_window.mm`. Windows would need an OpenGL
loader and is not wired up.

## How it is put together

- `PlayerApp.rgr` — the skin's tree (the body, wings, crest and chin are SVG
  paths in page pixels), the playlist, and what each button means. A press
  returns a command for the host (`load`, `play`, `pause`, `open-files`…);
  the app never touches audio or files.
- `player.css` — every colour and position. The three skins are `@vars`
  themes; switching skin is `applyTree(root, "lime")`.
- `shaders/` — the effects' GLSL and their parameters (`effects.json`),
  shared by the page and the native app.
- `web/viz.js` — registers them as surface effects with EVG's painter:
  `evgp-spectrum` (the screen, 32 band parameters `b0`…`b31`, `level`,
  `mode`) and `evgp-speaker` (the cones, pulsed by `level`).
- `scripts/build.mjs` — compiles against the Ranger checkout and runs
  `web/build.mjs`, which wraps the compiled module as an ES module and copies
  EVG's browser helpers (`evg-webgl.js`, `evg-fx.js`, `evg-measure.js`)
  beside the page.
- `web/main.js` — file pickers, `<audio>`, a Web Audio `AnalyserNode`, and
  the 32 log-spaced bands (40 Hz – 16 kHz) it writes into the effect
  instances' parameters each frame. The music never causes a layout.

The canvas is transparent where nothing is painted, so the page shows around
the shape. EVG's hit test is box-based, so the host first tests the pointer
against the outline (`PlayerApp.bodyPath()` and the wings, as `Path2D`); a
press outside the shape does nothing.

## Limits

- EVG draws box shadows around boxes, not around path outlines, and does not
  clip to a path; the skin is built from shapes that do not need either.
- Titles are file names; ID3 tags are not read.
- No accessibility mirror yet (the EVGUI demos have one via `evg-a11y.js`).
