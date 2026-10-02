// SPDX-License-Identifier: MIT
//
// The browser host for the EVG player. It owns what a browser has and Ranger
// does not:
//
//   the files    — <input type=file>, a folder picker, drag and drop
//   the sound    — one <audio> element through a Web Audio analyser
//   the bands    — 32 log-spaced levels, written straight into the
//                  visualiser's effect parameters every frame
//   the pixels   — one transparent WebGL 2 canvas, fed PlayerApp's display list
//
// Everything about the skin — its tree, its playlist, what a button means —
// is PlayerApp.rgr's. A press goes to `app.press(id)`, and what comes back is
// a command for this file: "open-files", "load", "play", "pause", "stop"…
//
// The canvas is transparent where the skin paints nothing, so the page shows
// around the shape. A press is only the player's when it lands inside the
// outline (`PlayerApp.bodyPath()` and the wings, as Path2D); the body itself
// is the handle that drags the player around the page.

import { prepareDisplayList } from "./evg/gl/evg-webgl.js";
import { createEffectDriver } from "./evg/gl/evg-fx.js";
import { installCanvasMeasurer } from "./evg/gl/evg-measure.js";
import PlayerModule, { PlayerApp } from "./generated-host.js";
import { PLAYER_CSS } from "./generated.js";
import { BANDS } from "./viz.js";

const canvas = document.getElementById("c");
const errEl = document.getElementById("err");
const audio = document.getElementById("audio");
const fileInput = document.getElementById("files");
const folderInput = document.getElementById("folder");
const params = new URLSearchParams(location.search);
const DEMO = params.has("demo");

installCanvasMeasurer(PlayerModule);
const app = new PlayerApp();
app.init(PLAYER_CSS);
window.__player = app;
for (let i = 0; i < app.styleErrorCount(); i++) console.warn("player.css:", app.styleErrorAt(i));

const W = app.widthPx();
const H = app.heightPx();
const dpr = Math.min(2, window.devicePixelRatio || 1);
canvas.style.width = `${W}px`;
canvas.style.height = `${H}px`;
canvas.width = Math.round(W * dpr);
canvas.height = Math.round(H * dpr);

const gl = canvas.getContext("webgl2", {
  alpha: true,
  antialias: true,
  premultipliedAlpha: false,
  stencil: true,
  preserveDrawingBuffer: true,
});
if (!gl) errEl.textContent = "WebGL 2 is not available in this browser";

const fx = createEffectDriver();

// --- the outline --------------------------------------------------------------
const outline = [PlayerApp.bodyPath(), PlayerApp.wingLeftPath(), PlayerApp.wingRightPath()].map((d) => new Path2D(d));
const probe = document.createElement("canvas").getContext("2d");
const inside = (x, y) => outline.some((p) => probe.isPointInPath(p, x, y));

// --- placement: centred, then wherever it is dragged --------------------------
let posX = Math.max(0, (window.innerWidth - W) / 2);
let posY = Math.max(0, (window.innerHeight - H) / 2);
function place() {
  canvas.style.left = `${Math.round(posX)}px`;
  canvas.style.top = `${Math.round(posY)}px`;
}
place();

// --- the sound ----------------------------------------------------------------
let ctx = null;
let analyser = null;
let freq = null;
function ensureAudioGraph() {
  if (ctx) {
    if (ctx.state === "suspended") ctx.resume();
    return;
  }
  ctx = new (window.AudioContext || window.webkitAudioContext)();
  const src = ctx.createMediaElementSource(audio);
  analyser = ctx.createAnalyser();
  analyser.fftSize = 2048;
  analyser.smoothingTimeConstant = 0.72;
  src.connect(analyser);
  analyser.connect(ctx.destination);
  freq = new Uint8Array(analyser.frequencyBinCount);
}

// The bands: log-spaced from 40 Hz to 16 kHz, so the bass gets as many bars
// as the treble. Each rises fast and falls slowly, which is what makes a
// meter look like it is listening rather than flickering.
const bands = new Float32Array(BANDS);
let level = 0;
function bandEdges(sampleRate, bins) {
  const lo = 40, hi = 16000, edges = [];
  for (let i = 0; i <= BANDS; i++) {
    const f = lo * Math.pow(hi / lo, i / BANDS);
    edges.push(Math.min(bins - 1, Math.max(1, Math.round((f / (sampleRate / 2)) * bins))));
  }
  return edges;
}
let edges = null;

function updateBands(dt, t) {
  const fall = Math.exp(-dt / 220);
  if (DEMO) {
    for (let i = 0; i < BANDS; i++) {
      const target = 0.25 + 0.35 * Math.sin(t * 0.0021 + i * 0.45) ** 2 + 0.35 * Math.max(0, Math.sin(t * 0.009 - i * 0.12)) * (1 - i / BANDS);
      bands[i] = Math.max(target, bands[i] * fall);
    }
  } else if (analyser && !audio.paused) {
    analyser.getByteFrequencyData(freq);
    if (!edges) edges = bandEdges(ctx.sampleRate, freq.length);
    for (let i = 0; i < BANDS; i++) {
      let sum = 0, n = 0;
      for (let k = edges[i]; k <= Math.max(edges[i], edges[i + 1] - 1); k++) { sum += freq[k]; n++; }
      // Treble is quieter in every recording: lift it so the ring is not lopsided.
      const tilt = 1 + (i / BANDS) * 0.6;
      const target = Math.min(1, Math.pow((sum / n) / 255, 1.4) * tilt);
      bands[i] = target > bands[i] ? target : bands[i] * fall;
    }
  } else {
    for (let i = 0; i < BANDS; i++) bands[i] *= fall;
  }
  const bass = (bands[0] + bands[1] + bands[2] + bands[3]) / 4;
  level = bass > level ? bass : level * Math.exp(-dt / 160);
}

// --- the playlist -------------------------------------------------------------
let files = [];
let objectUrl = "";
const AUDIO_EXT = /\.(mp3|ogg|oga|wav|flac|m4a|aac|opus|webm)$/i;

function setFiles(list) {
  const chosen = [...list]
    .filter((f) => AUDIO_EXT.test(f.name) || (f.type || "").startsWith("audio/"))
    .sort((a, b) => (a.webkitRelativePath || a.name).localeCompare(b.webkitRelativePath || b.name, undefined, { numeric: true }));
  if (!chosen.length) {
    app.note("NO AUDIO FILES");
    return;
  }
  files = chosen;
  app.clearTracks();
  for (const f of files) app.addTrack(f.name);
  run("load");
}

function loadCurrent() {
  const i = app.currentIndex();
  if (i < 0 || i >= files.length) return false;
  if (objectUrl) URL.revokeObjectURL(objectUrl);
  objectUrl = URL.createObjectURL(files[i]);
  audio.src = objectUrl;
  return true;
}

function run(cmd) {
  switch (cmd) {
    case "open-files": fileInput.click(); break;
    case "open-folder": folderInput.click(); break;
    case "load":
      ensureAudioGraph();
      if (loadCurrent()) audio.play().catch((e) => app.note("CANNOT PLAY"));
      break;
    case "play":
      ensureAudioGraph();
      if (!audio.src && !loadCurrent()) break;
      audio.play().catch(() => app.note("CANNOT PLAY"));
      break;
    case "pause": audio.pause(); break;
    case "stop": audio.pause(); audio.currentTime = 0; break;
    case "restart": audio.currentTime = 0; break;
  }
}

fileInput.addEventListener("change", () => { setFiles(fileInput.files); fileInput.value = ""; });
folderInput.addEventListener("change", () => { setFiles(folderInput.files); folderInput.value = ""; });
audio.addEventListener("play", () => app.setPlaying(true));
audio.addEventListener("pause", () => app.setPlaying(false));
audio.addEventListener("ended", () => run(app.trackEnded()));
audio.addEventListener("error", () => { app.note("UNSUPPORTED FILE"); });
audio.volume = 0.8;

// Drop files (or a whole folder's worth) anywhere on the page.
window.addEventListener("dragover", (ev) => ev.preventDefault());
window.addEventListener("drop", (ev) => {
  ev.preventDefault();
  if (ev.dataTransfer && ev.dataTransfer.files.length) setFiles(ev.dataTransfer.files);
});

// --- the pointer --------------------------------------------------------------
let drag = null;     // { dx, dy } while the body is being dragged
let seeking = false;

function local(ev) {
  const r = canvas.getBoundingClientRect();
  return [ev.clientX - r.left, ev.clientY - r.top];
}

function seekTo(x, y) {
  const f = app.seekFraction(x, y);
  if (f >= 0 && isFinite(audio.duration)) audio.currentTime = f * audio.duration;
}

canvas.addEventListener("pointerdown", (ev) => {
  const [x, y] = local(ev);
  if (!inside(x, y)) return;
  canvas.setPointerCapture(ev.pointerId);
  const id = app.hitId(x, y);
  app.setPressed(id);
  if (id === "seek") {
    seeking = true;
    seekTo(x, y);
    return;
  }
  if (id === "body" || id === "screen" || id === "") {
    drag = { dx: ev.clientX - posX, dy: ev.clientY - posY };
    canvas.style.cursor = "grabbing";
    return;
  }
  run(app.press(id));
});

canvas.addEventListener("pointermove", (ev) => {
  const [x, y] = local(ev);
  if (drag) {
    posX = ev.clientX - drag.dx;
    posY = ev.clientY - drag.dy;
    place();
    return;
  }
  if (seeking) {
    seekTo(x, y);
    return;
  }
  const id = inside(x, y) ? app.hitId(x, y) : "";
  app.setHover(id);
  canvas.style.cursor = !inside(x, y) ? "default" : (id === "body" || id === "screen" || id === "") ? "grab" : "pointer";
});

function release() {
  drag = null;
  seeking = false;
  app.setPressed("");
  canvas.style.cursor = "default";
}
canvas.addEventListener("pointerup", release);
canvas.addEventListener("pointercancel", release);
canvas.addEventListener("pointerleave", () => app.setHover(""));

// The wheel over the player is the volume knob.
canvas.addEventListener("wheel", (ev) => {
  ev.preventDefault();
  audio.volume = Math.min(1, Math.max(0, audio.volume - Math.sign(ev.deltaY) * 0.05));
  app.setVolume(audio.volume);
}, { passive: false });

window.addEventListener("keydown", (ev) => {
  const map = { " ": "play", ArrowRight: "next", ArrowLeft: "prev", s: "stop", v: "viz", k: "skin", o: "open-files" };
  const id = map[ev.key];
  if (!id) return;
  ev.preventDefault();
  run(app.press(id));
});

// --- the frame ----------------------------------------------------------------
let last = performance.now();
function frame(now) {
  const dt = Math.min(now - last, 250);
  last = now;
  try {
    app.setTime(audio.currentTime || 0, isFinite(audio.duration) ? audio.duration : 0);
    app.tick(dt);
    updateBands(dt, now);
    const list = JSON.parse(app.displayListJson());
    fx.tick(dt, list);
    for (const inst of list.effects || []) {
      if (inst.kind === "evgp-spectrum") {
        for (let i = 0; i < BANDS; i++) inst.p["b" + i] = bands[i];
        inst.p.level = level;
        inst.p.mode = app.currentVizMode();
        inst.p.playing = DEMO || !audio.paused ? 1 : 0;
      } else if (inst.kind === "evgp-speaker") {
        inst.p.level = level;
      }
    }
    if (gl) {
      const built = prepareDisplayList(gl, { width: W, height: H, list }, { dpr });
      built.draw();
      built.dispose();
    }
    errEl.textContent = "";
  } catch (e) {
    errEl.textContent = String((e && e.stack) || e);
  }
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
