#!/usr/bin/env node
// SPDX-License-Identifier: MIT
//
// Serve the player page.
//
//   npm run web   # build, serve, print the URL
//
// A static server rooted at this directory and nothing else.

import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const argv = process.argv.slice(2);
const portFlag = argv.indexOf("--port");
const PORT = portFlag >= 0 ? Number(argv[portFlag + 1]) : 8131;

const TYPES = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".map": "application/json; charset=utf-8",
  ".png": "image/png",
  ".mp3": "audio/mpeg",
};

const server = http.createServer((req, res) => {
  const pathname = req.url.split("?")[0];
  const name = pathname === "/" ? "/index.html" : pathname;
  // /about/ is the introduction page, which lives beside web/ in the repo.
  if (name === "/about") {
    res.writeHead(301, { Location: "/about/" });
    res.end();
    return;
  }
  let root = HERE;
  let rel = name;
  if (rel.startsWith("/about/")) {
    root = path.join(HERE, "..", "about");
    rel = rel.slice("/about".length);
    if (rel.endsWith("/")) rel += "index.html";
  }
  const file = path.join(root, path.normalize(rel).replace(/^(\.\.[/\\])+/, ""));
  if (!file.startsWith(root) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
    res.writeHead(404, { "Content-Type": "text/plain" });
    res.end("not found");
    return;
  }
  res.writeHead(200, { "Content-Type": TYPES[path.extname(file)] ?? "application/octet-stream" });
  res.end(fs.readFileSync(file));
});

server.listen(PORT, () => {
  process.stdout.write(`\n  the EVG player on http://127.0.0.1:${PORT}/\n`);
});
