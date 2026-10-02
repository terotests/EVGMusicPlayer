#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// native/icon/icon-1024.png, drawn by the player itself: the round screen in
// its bezel, lit with a fixed spectrum (`evg-player --icon`). The macOS build
// makes the bundle's AppIcon.icns from it; the running player sets its window
// and Dock icon from the same drawing without this file.
//
//   npm run native && node scripts/make-icon.mjs

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import zlib from "node:zlib";
import { spawnSync } from "node:child_process";
import { ROOT } from "./ranger.mjs";

const bin = path.join(ROOT, "native", "build", "evg-player");
if (!fs.existsSync(bin)) { console.error("build it first: npm run native"); process.exit(2); }
const pam = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "evgp-icon-")), "icon.pam");
let cmd = bin, args = ["--icon", pam];
if (process.platform === "linux" && !process.env.DISPLAY) {
  args = ["-a", "-s", "-screen 0 1024x768x24 +extension GLX", cmd, ...args];
  cmd = "xvfb-run";
}
const r = spawnSync(cmd, args, { env: { ...process.env, SDL_AUDIODRIVER: "dummy" }, encoding: "utf8", timeout: 60000 });
if (!fs.existsSync(pam)) { console.error(r.stdout, r.stderr); process.exit(1); }

// PAM (RGB_ALPHA) to PNG.
const buf = fs.readFileSync(pam);
const end = buf.indexOf("ENDHDR\n") + 7;
const head = buf.subarray(0, end).toString("latin1");
const w = Number(/WIDTH (\d+)/.exec(head)[1]), h = Number(/HEIGHT (\d+)/.exec(head)[1]);
const px = buf.subarray(end);
const raw = Buffer.alloc((w * 4 + 1) * h);
for (let y = 0; y < h; y++) px.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4);
const crcTable = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
const crc = (b) => { let c = 0xffffffff; for (const x of b) c = crcTable[(c ^ x) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (type, data) => {
  const t = Buffer.concat([Buffer.from(type, "latin1"), data]);
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const c = Buffer.alloc(4); c.writeUInt32BE(crc(t));
  return Buffer.concat([len, t, c]);
};
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
const png = Buffer.concat([
  Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
  chunk("IHDR", ihdr), chunk("IDAT", zlib.deflateSync(raw, { level: 9 })), chunk("IEND", Buffer.alloc(0)),
]);
const out = path.join(ROOT, "native", "icon", "icon-1024.png");
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, png);
console.log(`  native/icon/icon-1024.png (${w}x${h})`);
