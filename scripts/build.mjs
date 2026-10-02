#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Compile the player against a Ranger checkout and assemble the page.
//
//   node scripts/build.mjs [--ranger DIR] [--out DIR]
//
// The Ranger checkout (default ../Ranger, or RANGER_DIR) supplies the
// compiler (dist/rgrc.js) and EVG (lib/evg, put there by its `npm run deps`).
// The sources are copied to <ranger>/gallery/evgmusicplayer/ before compiling,
// so `ranger.json`'s "../../lib/evg" resolves to that checkout's EVG, the same
// way EVGUI builds.

import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), "..");
const argv = process.argv.slice(2);
const flag = (name) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : null; };
const RANGER = path.resolve(flag("--ranger") || process.env.RANGER_DIR || path.join(ROOT, "..", "Ranger"));
const OUT = flag("--out");

const rgrc = path.join(RANGER, "dist", "rgrc.js");
if (!fs.existsSync(rgrc)) {
  console.error(`no Ranger compiler at ${rgrc} — pass --ranger <checkout> or set RANGER_DIR`);
  process.exit(2);
}
if (!fs.existsSync(path.join(RANGER, "lib", "evg", "EVGElement.rgr"))) {
  console.error(`${RANGER}/lib/evg is missing — run \`npm run deps\` (or \`npm ci\`) in the Ranger checkout`);
  process.exit(2);
}

const overlay = path.join(RANGER, "gallery", "evgmusicplayer");
fs.mkdirSync(overlay, { recursive: true });
for (const f of ["PlayerApp.rgr", "ranger.json"]) fs.copyFileSync(path.join(ROOT, f), path.join(overlay, f));

const bin = path.join(ROOT, "bin");
fs.mkdirSync(bin, { recursive: true });
const compiled = path.join(bin, "PlayerApp.cjs");
fs.rmSync(compiled, { force: true });
let log = "";
try {
  log = execFileSync("node", [rgrc, "-es6", "-nodemodule", path.join(overlay, "PlayerApp.rgr"), `-d=${bin}`, "-o=PlayerApp.cjs"],
    { cwd: RANGER, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });
} catch (e) {
  log = String(e.stdout || "") + String(e.stderr || "");
}
// The compiler has exited 0 on a failed build before; the log and the file decide.
if (/Compilation FAILED|\[FAIL\]/.test(log) || !fs.existsSync(compiled)) {
  process.stderr.write(log);
  console.error("PlayerApp.rgr did not compile");
  process.exit(1);
}
console.log("  bin/PlayerApp.cjs compiled");

const page = ["web/build.mjs", "--lib", path.join(RANGER, "lib")];
if (OUT) page.push("--out", path.resolve(OUT));
execFileSync("node", page, { cwd: ROOT, stdio: "inherit" });
