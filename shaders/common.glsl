// SPDX-License-Identifier: AGPL-3.0-or-later
// Helpers every player effect gets, prepended to each one.
vec3 vzHue(float deg) {
  float h = fract(deg / 360.0) * 6.0;
  return clamp(abs(mod(h + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
}
float vzHash(vec2 p) {
  p = fract(p * vec2(127.31, 311.7));
  p += dot(p, p + 34.53);
  return fract(p.x * p.y);
}
