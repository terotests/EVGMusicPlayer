# EVG Player

A skinned music player in the spirit of the old Windows Media Player skins:
the player is a shape, not a window, its chrome is painted, and the round
screen in the middle is a GPU visualiser driven by the music. Written in
Ranger, drawn by EVG's WebGL painter.

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

## How it is put together

- `PlayerApp.rgr` — the skin's tree (the body, wings, crest and chin are SVG
  paths in page pixels), the playlist, and what each button means. A press
  returns a command for the host (`load`, `play`, `pause`, `open-files`…);
  the app never touches audio or files.
- `player.css` — every colour and position. The three skins are `@vars`
  themes; switching skin is `applyTree(root, "lime")`.
- `web/viz.js` — two surface effects registered with EVG's painter:
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

- Browser only. No EVG host makes a shaped OS-level window yet (the SDL hosts
  open ordinary rectangular windows).
- EVG draws box shadows around boxes, not around path outlines, and does not
  clip to a path; the skin is built from shapes that do not need either.
- Titles are file names; ID3 tags are not read.
- No accessibility mirror yet (the EVGUI demos have one via `evg-a11y.js`).
