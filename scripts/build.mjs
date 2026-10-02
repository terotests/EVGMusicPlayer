#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Compile the player for the browser and assemble the page.
//
//   node scripts/build.mjs [--out DIR]
//
// The compiler is ranger-compiler from npm and EVG comes from terotests/evg
// through `rgrc install` (see scripts/ranger.mjs); nothing else is needed.

import path from "node:path";
import { execFileSync } from "node:child_process";
import { ROOT, install, compile, evgDir } from "./ranger.mjs";

const argv = process.argv.slice(2);
const outAt = argv.indexOf("--out");

install();
const compiled = path.join(ROOT, "bin", "PlayerApp.cjs");
compile(["-es6", "-nodemodule", "PlayerApp.rgr", "-d=bin", "-o=PlayerApp.cjs"], compiled);
console.log("  bin/PlayerApp.cjs compiled");

const page = ["web/build.mjs", "--evg", evgDir()];
if (outAt >= 0) page.push("--out", path.resolve(argv[outAt + 1]));
execFileSync(process.execPath, page, { cwd: ROOT, stdio: "inherit" });
