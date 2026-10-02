// SPDX-License-Identifier: AGPL-3.0-or-later
//
// An EVG display list (the JSON `EVGDisplayList.toJson` writes) drawn with
// OpenGL 3.3 core — the native counterpart of lib/evg/gl/evg-webgl.js for the
// commands the player's skin uses:
//
//   k0 rect      solid or 2-stop linear gradient, rounded, drop shadow / glow,
//                and the surface effect the rect carries (`efx`)
//   k1 border    rounded ring
//   k3 text      Noto Sans regular / bold through stb_truetype
//   k4 / k5      rectangular clip push / pop (scissor)
//   k6 path fill stencil-then-cover, even-odd or non-zero, with a gradient
//   k7 stroke    the flattened rings as thick segments
//
// Images (k2) and backdrop blur are not drawn: the skin has none.
//
// The framebuffer is cleared to transparent and blended so that it holds
// premultiplied colour, which is what a transparent window composites.

#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "json.h"

struct FxDef {
  std::string name;
  std::string glsl;                          // fxColor() and its helpers
  std::vector<std::pair<std::string, float>> params;  // name, default
};

// What the host supplies for each effect instance at draw time: the values it
// overrides (band levels, level, mode…). Called with the instance's kind.
using FxOverride = std::function<void(const std::string& kind, std::map<std::string, float>& p)>;

class Painter {
 public:
  bool init(const std::string& fontDir, const std::vector<FxDef>& effects, const std::string& commonGlsl);
  // Draw a parsed list into the bound framebuffer. pageW/H are list pixels,
  // drawW/H the framebuffer's.
  void draw(const JVal& list, int pageW, int pageH, int drawW, int drawH, float timeSec, const FxOverride& fx);
  std::string error;

 private:
  struct Glyph { float u0, v0, u1, v1, w, h, xoff, yoff, adv; bool ok; };
  struct Face { std::vector<unsigned char> data; void* info = nullptr; float ascent = 0, descent = 0, lineGap = 0; };
  struct FxProg { unsigned prog = 0; FxDef def; std::map<std::string, int> loc; int uBox, uRadius, uRes, uTime; };

  unsigned shapeProg_ = 0, textProg_ = 0, vao_ = 0, vbo_ = 0, atlasTex_ = 0;
  int sRes_, sBox_, sRadius_, sC1_, sC2_, sGrad_, sMode_, sThick_, sBlur_;
  int tRes_, tAtlas_, tColor_;
  std::map<std::string, FxProg> fx_;
  Face faces_[2];
  std::map<unsigned long long, Glyph> glyphs_;
  std::vector<unsigned char> atlas_;
  int atlasW_ = 1024, atlasH_ = 1024, penX_ = 1, penY_ = 1, rowH_ = 0;
  bool atlasDirty_ = false;
  float dpr_ = 1;
  int pageW_ = 0, pageH_ = 0, drawW_ = 0, drawH_ = 0;
  std::vector<float> clipStack_;

  void quad(float x, float y, float w, float h);
  void tris(const std::vector<float>& xy);
  void rect(const JVal& c);
  void border(const JVal& c);
  void text(const JVal& c);
  void pathFill(const JVal& c);
  void stroke(const JVal& c);
  void effect(const JVal& inst, float timeSec, const FxOverride& fx);
  void applyClip();
  const Glyph& glyph(int face, int px, unsigned cp);
};
