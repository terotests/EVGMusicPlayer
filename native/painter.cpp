// SPDX-License-Identifier: AGPL-3.0-or-later
//
// See painter.h. The shapes are signed-distance rounded boxes, the same
// function lib/evg/gl/evg-webgl.js uses; paths are filled through the stencil
// buffer; text is rasterised once per glyph and size into one atlas.

#include "painter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "gl.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"

namespace {

const char* VERT = R"GLSL(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform vec2 uRes;
out vec2 vP;
out vec2 vUV;
void main() {
  vP = aPos;
  vUV = aUV;
  gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0);
}
)GLSL";

// uMode: 0 filled rounded box, 1 rounded ring of uThick, 2 soft shadow of
// uBlur, 3 no mask (stroke triangles, path cover). uGrad: -1 solid, 0 top to
// bottom, 1 left to right, across uBox.
const char* SHAPE_FRAG = R"GLSL(#version 330 core
in vec2 vP;
out vec4 o;
uniform vec4 uBox;
uniform float uRadius;
uniform vec4 uC1;
uniform vec4 uC2;
uniform int uGrad;
uniform int uMode;
uniform float uThick;
uniform float uBlur;
float sdBox(vec2 p) {
  vec2 h = uBox.zw * 0.5;
  vec2 c = uBox.xy + h;
  float r = min(uRadius, min(h.x, h.y));
  vec2 d = abs(p - c) - (h - vec2(r));
  return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0) - r;
}
void main() {
  vec4 col = uC1;
  if (uGrad == 0) col = mix(uC1, uC2, clamp((vP.y - uBox.y) / max(uBox.w, 1.0), 0.0, 1.0));
  if (uGrad == 1) col = mix(uC1, uC2, clamp((vP.x - uBox.x) / max(uBox.z, 1.0), 0.0, 1.0));
  float cov = 1.0;
  if (uMode == 0) {
    cov = 1.0 - smoothstep(-0.5, 0.5, sdBox(vP));
  } else if (uMode == 1) {
    float d = sdBox(vP);
    cov = (1.0 - smoothstep(-0.5, 0.5, d)) * smoothstep(-0.5, 0.5, d + uThick);
  } else if (uMode == 2) {
    float b = max(uBlur, 0.5);
    cov = 1.0 - smoothstep(-b, b, sdBox(vP));
    cov *= cov;
  }
  o = vec4(col.rgb, col.a * cov);
}
)GLSL";

const char* TEXT_FRAG = R"GLSL(#version 330 core
in vec2 vUV;
out vec4 o;
uniform sampler2D uAtlas;
uniform vec4 uColor;
void main() {
  o = vec4(uColor.rgb, uColor.a * texture(uAtlas, vUV).r);
}
)GLSL";

// What every effect gets, named as lib/evg/gl/evg-webgl.js names it, so the
// GLSL in ../shaders compiles unchanged in both.
const char* FX_PRE = R"GLSL(#version 330 core
in vec2 vP;
out vec4 outColor;
uniform vec2 uRes;
uniform vec4 uBox;
uniform float uRadius;
uniform float uTime;
float fxBoxDistance(vec2 p) {
  vec2 halfBox = uBox.zw * 0.5;
  vec2 centre = uBox.xy + halfBox;
  float r = min(uRadius, min(halfBox.x, halfBox.y));
  vec2 d = abs(p - centre) - (halfBox - vec2(r));
  return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0) - r;
}
float fxBoxCoverage(vec2 p) {
  return 1.0 - smoothstep(-0.75, 0.75, fxBoxDistance(p));
}
)GLSL";

const char* FX_MAIN = R"GLSL(
void main() {
  vec2 p = vP;
  vec2 local = (p - uBox.xy) / max(uBox.zw, vec2(1.0));
  vec4 c = fxColor(p, local);
  outColor = vec4(c.rgb, c.a * fxBoxCoverage(p));
}
)GLSL";

GLuint compile(GLenum type, const std::string& src, std::string& err) {
  GLuint s = glCreateShader(type);
  const char* p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(s, sizeof log, nullptr, log);
    err += log;
    glDeleteShader(s);
    return 0;
  }
  return s;
}

GLuint link(const std::string& vs, const std::string& fs, std::string& err) {
  GLuint v = compile(GL_VERTEX_SHADER, vs, err);
  GLuint f = compile(GL_FRAGMENT_SHADER, fs, err);
  if (!v || !f) return 0;
  GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(p, sizeof log, nullptr, log);
    err += log;
    return 0;
  }
  return p;
}

bool readFile(const std::string& path, std::vector<unsigned char>& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return !out.empty();
}

void color(const JVal* c, float out[4]) {
  out[0] = out[1] = out[2] = 0;
  out[3] = 1;
  if (!c || c->type != JVal::Arr || c->arr.size() < 3) return;
  out[0] = (float)c->arr[0].num / 255.f;
  out[1] = (float)c->arr[1].num / 255.f;
  out[2] = (float)c->arr[2].num / 255.f;
  out[3] = c->arr.size() > 3 ? (float)c->arr[3].num : 1.f;
}

unsigned nextCodepoint(const std::string& s, size_t& i) {
  unsigned char c = (unsigned char)s[i++];
  if (c < 0x80) return c;
  int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  unsigned cp = c & (0x3F >> extra);
  while (extra-- > 0 && i < s.size()) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
  return cp;
}

// The rings of a path command: `pts` is x,y pairs and `ends` the index (into
// pts) where each ring stops; no `ends` is one ring.
std::vector<std::vector<float>> rings(const JVal& c) {
  std::vector<std::vector<float>> out;
  const JVal* pts = c.get("pts");
  if (!pts || pts->type != JVal::Arr) return out;
  std::vector<size_t> ends;
  if (const JVal* e = c.get("ends")) {
    for (const JVal& v : e->arr) ends.push_back((size_t)v.num);
  }
  if (ends.empty()) ends.push_back(pts->arr.size());
  size_t start = 0;
  for (size_t stop : ends) {
    std::vector<float> ring;
    for (size_t k = start; k < stop && k < pts->arr.size(); k++) ring.push_back((float)pts->arr[k].num);
    if (ring.size() >= 4) out.push_back(std::move(ring));
    start = stop;
  }
  return out;
}

}  // namespace

bool Painter::init(const std::string& fontDir, const std::vector<FxDef>& effects, const std::string& commonGlsl) {
  shapeProg_ = link(VERT, SHAPE_FRAG, error);
  textProg_ = link(VERT, TEXT_FRAG, error);
  if (!shapeProg_ || !textProg_) return false;
  sRes_ = glGetUniformLocation(shapeProg_, "uRes");
  sBox_ = glGetUniformLocation(shapeProg_, "uBox");
  sRadius_ = glGetUniformLocation(shapeProg_, "uRadius");
  sC1_ = glGetUniformLocation(shapeProg_, "uC1");
  sC2_ = glGetUniformLocation(shapeProg_, "uC2");
  sGrad_ = glGetUniformLocation(shapeProg_, "uGrad");
  sMode_ = glGetUniformLocation(shapeProg_, "uMode");
  sThick_ = glGetUniformLocation(shapeProg_, "uThick");
  sBlur_ = glGetUniformLocation(shapeProg_, "uBlur");
  tRes_ = glGetUniformLocation(textProg_, "uRes");
  tAtlas_ = glGetUniformLocation(textProg_, "uAtlas");
  tColor_ = glGetUniformLocation(textProg_, "uColor");

  for (const FxDef& def : effects) {
    std::string decls;
    for (auto& p : def.params) decls += "uniform float p_" + p.first + ";\n";
    std::string err;
    GLuint prog = link(VERT, std::string(FX_PRE) + decls + commonGlsl + def.glsl + FX_MAIN, err);
    if (!prog) {
      // An effect that does not compile leaves its box as the skin drew it,
      // as the web painter does; the rest of the window still works.
      std::fprintf(stderr, "evg-player: effect %s did not compile:\n%s\n", def.name.c_str(), err.c_str());
      continue;
    }
    FxProg fp;
    fp.prog = prog;
    fp.def = def;
    for (auto& p : def.params) fp.loc[p.first] = glGetUniformLocation(prog, ("p_" + p.first).c_str());
    fp.uBox = glGetUniformLocation(prog, "uBox");
    fp.uRadius = glGetUniformLocation(prog, "uRadius");
    fp.uRes = glGetUniformLocation(prog, "uRes");
    fp.uTime = glGetUniformLocation(prog, "uTime");
    fx_[def.name] = fp;
  }

  const char* files[2] = {"NotoSans-Regular.ttf", "NotoSans-Bold.ttf"};
  for (int f = 0; f < 2; f++) {
    Face& face = faces_[f];
    if (!readFile(fontDir + "/" + files[f], face.data)) {
      error = "cannot read font " + fontDir + "/" + files[f];
      return false;
    }
    auto* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, face.data.data(), stbtt_GetFontOffsetForIndex(face.data.data(), 0))) {
      error = std::string("bad font ") + files[f];
      return false;
    }
    face.info = info;
    int a, d, g;
    stbtt_GetFontVMetrics(info, &a, &d, &g);
    float em = stbtt_ScaleForMappingEmToPixels(info, 1.f);
    face.ascent = a * em;
    face.descent = d * em;
    face.lineGap = g * em;
  }

  atlas_.assign((size_t)atlasW_ * atlasH_, 0);
  glGenTextures(1, &atlasTex_);
  glBindTexture(GL_TEXTURE_2D, atlasTex_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasW_, atlasH_, 0, GL_RED, GL_UNSIGNED_BYTE, atlas_.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
  return true;
}

void Painter::quad(float x, float y, float w, float h) {
  float v[24] = {x, y, 0, 0, x + w, y, 1, 0, x + w, y + h, 1, 1,
                 x, y, 0, 0, x + w, y + h, 1, 1, x, y + h, 0, 1};
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, 6);
}

void Painter::tris(const std::vector<float>& xy) {
  if (xy.size() < 6) return;
  std::vector<float> v;
  v.reserve(xy.size() * 2);
  for (size_t i = 0; i + 1 < xy.size(); i += 2) {
    v.push_back(xy[i]);
    v.push_back(xy[i + 1]);
    v.push_back(0);
    v.push_back(0);
  }
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 4));
}

void Painter::applyClip() {
  if (clipStack_.empty()) {
    glDisable(GL_SCISSOR_TEST);
    return;
  }
  size_t n = clipStack_.size();
  float x = clipStack_[n - 4], y = clipStack_[n - 3], w = clipStack_[n - 2], h = clipStack_[n - 1];
  glEnable(GL_SCISSOR_TEST);
  int sx = (int)std::floor(x * dpr_);
  int sy = (int)std::floor(drawH_ - (y + h) * dpr_);
  glScissor(sx, sy, std::max(0, (int)std::ceil(w * dpr_)), std::max(0, (int)std::ceil(h * dpr_)));
}

void Painter::rect(const JVal& c) {
  float x = (float)c.numOr("x", 0), y = (float)c.numOr("y", 0);
  float w = (float)c.numOr("w", 0), h = (float)c.numOr("h", 0);
  float r = (float)c.numOr("r", 0);
  if (const JVal* rc = c.get("rc")) {
    if (rc->arr.size() == 4) r = (float)rc->arr[0].num;  // one radius for all corners
  }
  glUseProgram(shapeProg_);
  glUniform2f(sRes_, (float)pageW_, (float)pageH_);
  if (const JVal* sh = c.get("sh")) {
    float sc[4];
    color(sh->get("c"), sc);
    float blur = (float)sh->numOr("blur", 0);
    float sx = x + (float)sh->numOr("x", 0), sy = y + (float)sh->numOr("y", 0);
    glUniform4f(sBox_, sx, sy, w, h);
    glUniform1f(sRadius_, r);
    glUniform4fv(sC1_, 1, sc);
    glUniform1i(sGrad_, -1);
    glUniform1i(sMode_, 2);
    glUniform1f(sBlur_, blur);
    float pad = blur * 2 + 2;
    quad(sx - pad, sy - pad, w + pad * 2, h + pad * 2);
  }
  float c1[4], c2[4];
  color(c.get("c"), c1);
  color(c.get("c2"), c2);
  int grad = c.get("gd") ? (int)c.numOr("gd", 0) : -1;
  if (c1[3] <= 0 && (grad < 0 || c2[3] <= 0)) return;
  glUniform4f(sBox_, x, y, w, h);
  glUniform1f(sRadius_, r);
  glUniform4fv(sC1_, 1, c1);
  glUniform4fv(sC2_, 1, c2);
  glUniform1i(sGrad_, grad);
  glUniform1i(sMode_, 0);
  quad(x - 1, y - 1, w + 2, h + 2);
}

void Painter::border(const JVal& c) {
  float x = (float)c.numOr("x", 0), y = (float)c.numOr("y", 0);
  float w = (float)c.numOr("w", 0), h = (float)c.numOr("h", 0);
  float c1[4];
  color(c.get("c"), c1);
  glUseProgram(shapeProg_);
  glUniform2f(sRes_, (float)pageW_, (float)pageH_);
  glUniform4f(sBox_, x, y, w, h);
  glUniform1f(sRadius_, (float)c.numOr("r", 0));
  glUniform4fv(sC1_, 1, c1);
  glUniform1i(sGrad_, -1);
  glUniform1i(sMode_, 1);
  glUniform1f(sThick_, (float)c.numOr("t", 1));
  quad(x - 1, y - 1, w + 2, h + 2);
}

const Painter::Glyph& Painter::glyph(int face, int px, unsigned cp) {
  unsigned long long key = ((unsigned long long)face << 48) | ((unsigned long long)px << 32) | cp;
  auto it = glyphs_.find(key);
  if (it != glyphs_.end()) return it->second;
  auto* info = (stbtt_fontinfo*)faces_[face].info;
  float scale = stbtt_ScaleForMappingEmToPixels(info, (float)px);
  int adv, lsb, x0, y0, x1, y1;
  stbtt_GetCodepointHMetrics(info, (int)cp, &adv, &lsb);
  stbtt_GetCodepointBitmapBox(info, (int)cp, scale, scale, &x0, &y0, &x1, &y1);
  Glyph g{};
  g.adv = adv * scale;
  int gw = x1 - x0, gh = y1 - y0;
  if (gw > 0 && gh > 0) {
    if (penX_ + gw + 1 >= atlasW_) {
      penX_ = 1;
      penY_ += rowH_ + 1;
      rowH_ = 0;
    }
    if (penY_ + gh + 1 >= atlasH_) {
      // Full: start again. Every glyph is re-rasterised as it is next asked
      // for, which happens only when a page shows a great many sizes.
      glyphs_.clear();
      std::fill(atlas_.begin(), atlas_.end(), 0);
      penX_ = penY_ = 1;
      rowH_ = 0;
      return glyph(face, px, cp);
    }
    stbtt_MakeCodepointBitmap(info, &atlas_[(size_t)penY_ * atlasW_ + penX_], gw, gh, atlasW_, scale, scale, (int)cp);
    g.u0 = (float)penX_ / atlasW_;
    g.v0 = (float)penY_ / atlasH_;
    g.u1 = (float)(penX_ + gw) / atlasW_;
    g.v1 = (float)(penY_ + gh) / atlasH_;
    g.w = (float)gw;
    g.h = (float)gh;
    g.xoff = (float)x0;
    g.yoff = (float)y0;
    g.ok = true;
    penX_ += gw + 1;
    rowH_ = std::max(rowH_, gh);
    atlasDirty_ = true;
  }
  return glyphs_[key] = g;
}

void Painter::text(const JVal& c) {
  std::string s = c.strOr("text", "");
  if (s.empty()) return;
  float size = (float)c.numOr("size", 14);
  std::string font = c.strOr("font", "");
  bool bold = c.strOr("weight", "") == "bold" || font.find("Bold") != std::string::npos;
  int face = bold ? 1 : 0;
  Face& f = faces_[face];
  auto* info = (stbtt_fontinfo*)f.info;
  int px = std::max(1, (int)std::lround(size * dpr_));
  float x = (float)c.numOr("x", 0), y = (float)c.numOr("y", 0), h = (float)c.numOr("h", size);
  // CSS half-leading: the face's ascent-to-descent box centred in the line.
  float content = (f.ascent - f.descent) * size;
  float baseline = y + (h - content) / 2 + f.ascent * size;
  float c1[4];
  color(c.get("c"), c1);

  std::vector<float> v;
  float pen = x;
  float emPx = stbtt_ScaleForMappingEmToPixels(info, (float)px);
  int prev = 0;
  for (size_t i = 0; i < s.size();) {
    unsigned cp = nextCodepoint(s, i);
    if (prev) pen += stbtt_GetCodepointKernAdvance(info, prev, (int)cp) * emPx / dpr_;
    const Glyph& g = glyph(face, px, cp);
    if (g.ok) {
      float gx = pen + g.xoff / dpr_, gy = baseline + g.yoff / dpr_;
      float gw = g.w / dpr_, gh = g.h / dpr_;
      float q[24] = {gx, gy, g.u0, g.v0, gx + gw, gy, g.u1, g.v0, gx + gw, gy + gh, g.u1, g.v1,
                     gx, gy, g.u0, g.v0, gx + gw, gy + gh, g.u1, g.v1, gx, gy + gh, g.u0, g.v1};
      v.insert(v.end(), q, q + 24);
    }
    pen += g.adv / dpr_;
    prev = (int)cp;
  }
  if (v.empty()) return;
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, atlasTex_);
  if (atlasDirty_) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, atlasW_, atlasH_, GL_RED, GL_UNSIGNED_BYTE, atlas_.data());
    atlasDirty_ = false;
  }
  glUseProgram(textProg_);
  glUniform2f(tRes_, (float)pageW_, (float)pageH_);
  glUniform1i(tAtlas_, 0);
  glUniform4fv(tColor_, 1, c1);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 4));
}

void Painter::pathFill(const JVal& c) {
  auto rs = rings(c);
  if (rs.empty()) return;
  float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
  glUseProgram(shapeProg_);
  glUniform2f(sRes_, (float)pageW_, (float)pageH_);

  // 1. Every ring as a fan into the stencil: the winding (or its parity) is
  //    left in each pixel, and no colour is written.
  glEnable(GL_STENCIL_TEST);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glStencilMask(0xFF);
  glStencilFunc(GL_ALWAYS, 0, 0xFF);
  if (c.numOr("eo", 0) != 0) {
    glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
  } else {
    glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
    glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP);
  }
  glUniform1i(sMode_, 3);
  glUniform1i(sGrad_, -1);
  for (auto& ring : rs) {
    std::vector<float> t;
    for (size_t k = 2; k + 3 < ring.size(); k += 2) {
      t.insert(t.end(), {ring[0], ring[1], ring[k], ring[k + 1], ring[k + 2], ring[k + 3]});
    }
    for (size_t k = 0; k + 1 < ring.size(); k += 2) {
      minX = std::min(minX, ring[k]);
      maxX = std::max(maxX, ring[k]);
      minY = std::min(minY, ring[k + 1]);
      maxY = std::max(maxY, ring[k + 1]);
    }
    tris(t);
  }

  // 2. One cover quad where the stencil is not zero, which also clears it.
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
  glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
  float c1[4], c2[4];
  color(c.get("c"), c1);
  color(c.get("c2"), c2);
  glUniform4f(sBox_, (float)c.numOr("x", 0), (float)c.numOr("y", 0), (float)c.numOr("w", 0), (float)c.numOr("h", 0));
  glUniform4fv(sC1_, 1, c1);
  glUniform4fv(sC2_, 1, c2);
  glUniform1i(sGrad_, c.get("gd") ? (int)c.numOr("gd", 0) : -1);
  quad(minX - 1, minY - 1, maxX - minX + 2, maxY - minY + 2);
  glDisable(GL_STENCIL_TEST);
}

void Painter::stroke(const JVal& c) {
  auto rs = rings(c);
  float half = std::max(0.5f, (float)c.numOr("t", 1) / 2);
  float c1[4];
  color(c.get("c"), c1);
  glUseProgram(shapeProg_);
  glUniform2f(sRes_, (float)pageW_, (float)pageH_);
  glUniform4fv(sC1_, 1, c1);
  glUniform1i(sGrad_, -1);
  glUniform1i(sMode_, 3);
  // Each segment a quad, lengthened by half the width at both ends so the
  // joins between short flattened segments close. Overlaps are drawn twice,
  // which only shows on a translucent stroke; the skin's are opaque.
  std::vector<float> t;
  for (auto& ring : rs) {
    for (size_t k = 0; k + 3 < ring.size(); k += 2) {
      float x1 = ring[k], y1 = ring[k + 1], x2 = ring[k + 2], y2 = ring[k + 3];
      float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
      if (len < 1e-4f) continue;
      float ux = dx / len * half, uy = dy / len * half;
      float nx = -uy, ny = ux;
      x1 -= ux; y1 -= uy; x2 += ux; y2 += uy;
      t.insert(t.end(), {x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny,
                         x1 + nx, y1 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny});
    }
  }
  tris(t);
}

void Painter::effect(const JVal& inst, float timeSec, const FxOverride& override) {
  std::string kind = inst.strOr("kind", "");
  auto it = fx_.find(kind);
  if (it == fx_.end()) return;
  FxProg& fp = it->second;
  const JVal* box = inst.get("box");
  if (!box || box->arr.size() < 4) return;
  float x = (float)box->arr[0].num, y = (float)box->arr[1].num, w = (float)box->arr[2].num, h = (float)box->arr[3].num;
  // The plugin's defaults, then what the stylesheet wrote, then the host's.
  std::map<std::string, float> p;
  for (auto& d : fp.def.params) p[d.first] = d.second;
  if (const JVal* wrote = inst.get("p")) {
    for (auto& kv : wrote->obj) {
      if (kv.second.type == JVal::Num) p[kv.first] = (float)kv.second.num;
    }
  }
  if (override) override(kind, p);
  glUseProgram(fp.prog);
  glUniform2f(fp.uRes, (float)pageW_, (float)pageH_);
  glUniform4f(fp.uBox, x, y, w, h);
  glUniform1f(fp.uRadius, (float)inst.numOr("r", 0));
  glUniform1f(fp.uTime, timeSec);
  for (auto& kv : fp.loc) {
    if (kv.second >= 0) glUniform1f(kv.second, p[kv.first]);
  }
  quad(x, y, w, h);
}

void Painter::draw(const JVal& list, int pageW, int pageH, int drawW, int drawH, float timeSec, const FxOverride& fx) {
  pageW_ = pageW;
  pageH_ = pageH;
  drawW_ = drawW;
  drawH_ = drawH;
  dpr_ = pageW > 0 ? (float)drawW / (float)pageW : 1.f;
  clipStack_.clear();

  glViewport(0, 0, drawW, drawH);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0, 0, 0, 0);
  glClearStencil(0);
  glStencilMask(0xFF);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  glEnable(GL_BLEND);
  // Straight-alpha sources, premultiplied result: over a transparent clear
  // this leaves rgb * a in the buffer, which a transparent window expects.
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glBindVertexArray(vao_);

  std::map<std::string, const JVal*> effects;
  if (const JVal* e = list.get("effects")) {
    for (const JVal& inst : e->arr) effects[inst.strOr("id", "")] = &inst;
  }
  const JVal* cmds = list.get("cmds");
  if (!cmds) return;
  for (const JVal& c : cmds->arr) {
    int k = (int)c.numOr("k", -1);
    switch (k) {
      case 0: {
        rect(c);
        std::string id = c.strOr("efx", "");
        if (!id.empty() && effects.count(id)) effect(*effects[id], timeSec, fx);
        break;
      }
      case 1: border(c); break;
      case 3: text(c); break;
      case 4: {
        float x = (float)c.numOr("x", 0), y = (float)c.numOr("y", 0);
        float x2 = x + (float)c.numOr("w", 0), y2 = y + (float)c.numOr("h", 0);
        size_t n = clipStack_.size();
        if (n >= 4) {  // nested clips intersect
          x = std::max(x, clipStack_[n - 4]);
          y = std::max(y, clipStack_[n - 3]);
          x2 = std::min(x2, clipStack_[n - 4] + clipStack_[n - 2]);
          y2 = std::min(y2, clipStack_[n - 3] + clipStack_[n - 1]);
        }
        clipStack_.insert(clipStack_.end(), {x, y, std::max(0.f, x2 - x), std::max(0.f, y2 - y)});
        applyClip();
        break;
      }
      case 5:
        if (clipStack_.size() >= 4) clipStack_.resize(clipStack_.size() - 4);
        applyClip();
        break;
      case 6: pathFill(c); break;
      case 7: stroke(c); break;
      default: break;
    }
  }
  glDisable(GL_SCISSOR_TEST);
}
