#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A smoke check of the native player: run it for a few frames with a made-up
// WAV, read back the last frame and check that
//   - the corners are transparent (alpha 0): the window is the skin, not a box
//   - the body, the screen and the readout are painted
//   - the track is playing (the seek bar has moved off zero)
//
//   node scripts/check-native.mjs        # after `npm run native`
//
// On Linux without a display it runs under xvfb-run. Audio goes to SDL's
// dummy driver, so nothing is heard.

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), "..");
const bin = path.join(ROOT, "native", "build", "evg-player");
if (!fs.existsSync(bin)) { console.error("build it first: npm run native"); process.exit(2); }

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "evgp-"));
// 6 s of kick drum and a sweep.
const sr = 22050, n = sr * 6, wav = Buffer.alloc(44 + n * 2);
wav.write("RIFF", 0); wav.writeUInt32LE(36 + n * 2, 4); wav.write("WAVEfmt ", 8); wav.writeUInt32LE(16, 16);
wav.writeUInt16LE(1, 20); wav.writeUInt16LE(1, 22); wav.writeUInt32LE(sr, 24); wav.writeUInt32LE(sr * 2, 28);
wav.writeUInt16LE(2, 32); wav.writeUInt16LE(16, 34); wav.write("data", 36); wav.writeUInt32LE(n * 2, 40);
for (let i = 0; i < n; i++) {
  const t = i / sr, beat = t % 0.5;
  const v = Math.sin(2 * Math.PI * (60 + 80 * Math.exp(-beat * 30)) * beat) * Math.exp(-beat * 8) * 0.7 +
    0.3 * Math.sin(2 * Math.PI * (300 + 2500 * (0.5 + 0.5 * Math.sin(t))) * t);
  wav.writeInt16LE(Math.round(Math.max(-1, Math.min(1, v)) * 20000), 44 + i * 2);
}
const track = path.join(tmp, "check.wav");
fs.writeFileSync(track, wav);
const shot = path.join(tmp, "frame.pam");

let cmd = bin, args = [track, "--frames", "150", "--shot", shot];
if (process.platform === "linux" && !process.env.DISPLAY) {
  args = ["-a", "-s", "-screen 0 1024x768x24 +extension GLX", cmd, ...args];
  cmd = "xvfb-run";
}
const r = spawnSync(cmd, args, { env: { ...process.env, SDL_AUDIODRIVER: "dummy" }, encoding: "utf8", timeout: 60000 });
if (r.status !== 0 || !fs.existsSync(shot)) {
  console.error(r.stdout, r.stderr, r.error || "");
  console.error("the player did not run to its last frame");
  process.exit(1);
}

const pam = fs.readFileSync(shot);
const head = pam.subarray(0, 200).toString("latin1");
const w = Number(/WIDTH (\d+)/.exec(head)[1]), h = Number(/HEIGHT (\d+)/.exec(head)[1]);
const data = pam.subarray(head.indexOf("ENDHDR\n") + 7);
// The frame may be HiDPI; sample in page points (560 x 600).
const at = (x, y) => {
  const px = Math.floor(x * w / 560), py = Math.floor(y * h / 600), o = (py * w + px) * 4;
  return [data[o], data[o + 1], data[o + 2], data[o + 3]];
};
const fails = [];
const expect = (what, ok) => { if (!ok) fails.push(what); console.log(`  ${ok ? "ok  " : "FAIL"} ${what}`); };
for (const [x, y] of [[2, 2], [557, 2], [2, 597], [557, 597]]) expect(`corner ${x},${y} is transparent`, at(x, y)[3] === 0);
expect("the body is painted (opaque)", at(280, 50)[3] === 255);
expect("the screen is painted", at(280, 300)[3] === 255);
expect("the readout panel is painted", at(200, 430)[3] === 255);
// The seek bar's fill starts at x=170; 150 frames in, it has moved.
const seek = at(174, 444);
expect("the seek bar shows progress (track is playing)", seek[3] === 255 && seek[0] + seek[1] + seek[2] > 200);
fs.rmSync(tmp, { recursive: true, force: true });
process.exit(fails.length ? 1 : 0);
