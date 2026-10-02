// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The player's GPU effects, registered with EVG's WebGL painter.
//
//   evgp-spectrum  the round screen: three visualisers over 32 band levels
//   evgp-speaker   the speaker cones on the wings, pushed by the bass
//
// The GLSL lives in ../shaders and is shared with the native host
// (native/host.cpp); build.mjs embeds it in generated.js. The stylesheet puts
// the effects on elements (`evg-surface-effect: evgp-spectrum`) and sets the
// colours per skin (`evg-fx-hue`). The band levels are not in the document at
// all: main.js writes them into the effect instance's parameter bag every
// frame, and the painter reads that bag on every draw, so the music never
// causes a layout.

import { registerSurfaceEffect } from "./evg/gl/evg-webgl.js";
import { EFFECTS, COMMON_GLSL } from "./generated.js";

export const BANDS = 32;

for (const fx of EFFECTS) {
  const params = { ...fx.params };
  for (let i = 0; i < fx.bands; i++) params["b" + i] = 0;
  registerSurfaceEffect({ name: fx.name, layer: "source", params, frag: COMMON_GLSL + fx.glsl });
}
