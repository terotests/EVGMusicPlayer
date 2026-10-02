// SPDX-License-Identifier: MIT
// evgp-spectrum: the round screen. Three visualisers over 32 band levels
// (p_b0..p_b31), chosen by p_mode. Shared by the WebGL page (web/viz.js) and
// the native host (native/host.cpp); both supply fxColor's inputs and the
// uniforms named in effects.json.
float gB[32];

// A band level at t in 0..1, interpolated between the 32 bands.
float band(float t) {
  float x = clamp(t, 0.0, 1.0) * 31.0;
  int i = int(floor(x));
  int j = min(i + 1, 31);
  return mix(gB[i], gB[j], smoothstep(0.0, 1.0, fract(x)));
}

vec3 ringViz(vec2 uv, float r) {
  float PI = 3.14159265;
  float a = atan(uv.x, -uv.y);
  float t = abs(a) / PI;                     // 0 at the top, 1 at the bottom, mirrored
  float v = band(t);
  float inner = 0.30 + p_level * 0.07;
  float outer = inner + 0.03 + v * 0.5;
  float seg = fract(t * 40.0);
  float gap = smoothstep(0.0, 0.12, seg) * smoothstep(1.0, 0.88, seg);
  float bar = smoothstep(inner - 0.005, inner + 0.005, r) * smoothstep(outer + 0.006, outer - 0.006, r) * gap;
  float k = clamp((r - inner) / 0.6, 0.0, 1.0);
  vec3 cA = vzHue(p_hue);
  vec3 cB = vzHue(p_hue2);
  vec3 col = mix(cA, cB, k) * bar * (0.7 + 0.9 * k);
  // A soft halo past the tip of every bar.
  col += mix(cA, cB, 0.5) * exp(-max(r - outer, 0.0) * 14.0) * v * 0.35 * step(inner, r);
  // The orb in the middle breathes with the bass.
  float orb = exp(-r * r * (26.0 - p_level * 14.0));
  col += mix(vec3(1.0), cA, 0.4) * orb * (0.25 + p_level * 1.4);
  // A slow ripple across the dark glass.
  col += cA * 0.04 * (0.5 + 0.5 * sin(r * 38.0 - uTime * 2.4)) * smoothstep(inner, 0.0, r);
  return col;
}

vec3 tunnelViz(vec2 uv, float r) {
  float a = atan(uv.y, uv.x);
  float speed = 0.5 + p_level * 2.2;
  float z = 0.32 / (r + 0.06) + uTime * speed;
  float m = band(abs(a) / 3.14159265);
  float swirl = sin(a * 5.0 + z * 3.2 + sin(z * 0.7) * 2.5 + m * 3.0);
  float c = 0.5 + 0.5 * swirl;
  vec3 col = mix(vzHue(p_hue), vzHue(p_hue2), c) * (0.25 + c * 0.75) * (0.4 + m * 1.1);
  col *= smoothstep(0.0, 0.35, r);
  col += vzHue(p_hue) * exp(-r * 9.0) * (0.3 + p_level * 1.2);
  return col;
}

vec3 barsViz(vec2 local) {
  float x = (local.x - 0.12) / 0.76;
  float y = 1.0 - local.y;                    // up from the bottom of the screen
  if (x < 0.0 || x > 1.0) return vec3(0.0);
  float n = 22.0;
  float idx = floor(x * n);
  float cell = fract(x * n);
  float v = band(idx / (n - 1.0));
  float h = y - 0.2;
  float lit = step(0.0, h) * step(h, v * 0.62);
  float led = smoothstep(0.0, 0.15, fract(h * 34.0)) * smoothstep(0.12, 0.2, cell) * smoothstep(0.88, 0.8, cell);
  float k = clamp(h / 0.62, 0.0, 1.0);
  vec3 col = mix(vzHue(p_hue), vzHue(p_hue2), k) * lit * led * 1.3;
  // The reflection below the floor line.
  float hr = 0.2 - y;
  float refl = step(0.0, hr) * step(hr, v * 0.18) * smoothstep(0.12, 0.2, cell) * smoothstep(0.88, 0.8, cell);
  col += vzHue(p_hue) * refl * 0.25 * (1.0 - hr / 0.2);
  return col;
}

vec4 fxColor(vec2 p, vec2 local) {
  float vals[32] = float[32](p_b0, p_b1, p_b2, p_b3, p_b4, p_b5, p_b6, p_b7, p_b8, p_b9, p_b10, p_b11, p_b12, p_b13, p_b14, p_b15, p_b16, p_b17, p_b18, p_b19, p_b20, p_b21, p_b22, p_b23, p_b24, p_b25, p_b26, p_b27, p_b28, p_b29, p_b30, p_b31);
  for (int i = 0; i < 32; i++) gB[i] = vals[i];
  vec2 uv = (local - 0.5) * 2.0;
  float r = length(uv);
  vec3 col;
  if (p_mode < 0.5) col = ringViz(uv, r);
  else if (p_mode < 1.5) col = tunnelViz(uv, r);
  else col = barsViz(local);
  // Idle: a faint starry glass when nothing is playing.
  float star = step(0.996, vzHash(floor(p * 0.5))) * (0.5 + 0.5 * sin(uTime * 2.0 + vzHash(floor(p)) * 6.28));
  col += vec3(star) * 0.5 * (1.0 - p_playing);
  // Darken towards the rim, so the bezel reads as depth.
  col *= mix(0.35, 1.0, smoothstep(1.0, 0.75, r));
  col += vec3(0.01, 0.015, 0.03);
  return vec4(col, 1.0);
}
