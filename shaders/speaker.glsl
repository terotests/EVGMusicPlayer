// SPDX-License-Identifier: AGPL-3.0-or-later
// evgp-speaker: a speaker cone pushed by p_level (the bass).
vec4 fxColor(vec2 p, vec2 local) {
  vec2 uv = (local - 0.5) * 2.0;
  float r = length(uv) / (1.0 + p_level * 0.10);
  // The cone: rings that move as it is pushed.
  float rings = 0.5 + 0.5 * sin(r * 20.0 - p_level * 5.0);
  float shade = mix(0.05, 0.22, rings) * smoothstep(1.0, 0.25, r);
  // Light from the top left on the cone.
  shade += 0.12 * clamp(dot(normalize(uv + 0.0001), vec2(-0.6, -0.8)), 0.0, 1.0) * smoothstep(0.2, 0.9, r);
  vec3 col = vec3(shade);
  // The dust cap glows with the bass.
  float cap = smoothstep(0.34, 0.28, r);
  col = mix(col, vzHue(p_hue) * (0.25 + p_level * 1.3), cap);
  // And the surround catches it at the rim.
  col += vzHue(p_hue) * exp(-abs(r - 0.88) * 18.0) * p_level * 0.8;
  return vec4(col, 1.0);
}
