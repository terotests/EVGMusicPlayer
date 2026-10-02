// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The player's GPU effects, registered with EVG's WebGL painter.
//
//   evgp-spectrum  the round screen: three visualisers over 32 band levels
//   evgp-speaker   the speaker cones on the wings, pushed by the bass
//
// Each is a manifest and a GLSL body in ../shaders, which the native app
// registers with EVG's native painter unchanged (EffectDef::fromManifest);
// here EVG's evg-fx-def.js does the same for evg-webgl.js. build.mjs embeds
// them in generated.js. The band levels are not in the document: main.js
// writes them into the effect instance's parameters every frame, so the music
// never causes a layout.

import { registerEffectManifest } from "./evg/gl/evg-fx-def.js";
import { EFFECTS, COMMON_GLSL } from "./generated.js";

export const BANDS = 32;

for (const fx of EFFECTS) registerEffectManifest(fx, fx.glsl, COMMON_GLSL);
