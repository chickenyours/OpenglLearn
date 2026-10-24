// Balatro Shader Lab: original LÖVE shader bodies, the project Render RHI
// compatibility layer, and an inspectable multi-pass card renderer. See Report
// for provenance.
#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <glfw/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "rhi_bridge.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stb_image.h>
#include <stb_image_write.h>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs = std::filesystem;
static fs::path root = BALATRO_LAB_DIR;
static std::string read(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    throw std::runtime_error("Cannot read " + p.string());
  return {std::istreambuf_iterator<char>(f), {}};
}
static const char *vertex = R"GLSL(#version 330 core
layout(location=0) in vec2 aPosition;
layout(location=1) in vec2 aUV;
out vec2 vUV;
uniform vec4 love_ScreenSize;
uniform vec2 mouse_screen_pos;
uniform float hovering;
uniform float screen_scale;
void main(){
  vec4 p=vec4(2.*aPosition.x/love_ScreenSize.x-1.,1.-2.*aPosition.y/love_ScreenSize.y,0.,1.);
  // Exact position() formula from dissolve/foil/holo/polychrome.fs.
  if(hovering>0.){
    float mid_dist=length(aPosition-.5*love_ScreenSize.xy)/length(love_ScreenSize.xy);
    vec2 mouse_offset=(aPosition-mouse_screen_pos)/screen_scale;
    float scale=.2*(-.03-.3*max(0.,.3-mid_dist))*hovering*dot(mouse_offset,mouse_offset)/(2.-mid_dist);
    p.w+=scale;
  }
  vUV=aUV; gl_Position=p;
})GLSL";
static std::string fragment(std::string s) {
  // Macro adaptation leaves all extracted .fs files byte-for-byte intact.
  return std::string(R"GLSL(#version 330 core
#define extern uniform
#define number float
#define Image sampler2D
#define highp
#define mediump
uniform vec4 love_ScreenSize;
uniform sampler2D uTexture;
uniform vec4 uColour;
uniform bool uFlip;
uniform int uDebug;
in vec2 vUV;
out vec4 fragColour;
vec4 loveTexel(sampler2D image,vec2 p){return texture(image,vec2(p.x,uFlip?1.-p.y:p.y));}
#define Texel loveTexel
)GLSL") + s +
         R"GLSL(
void main(){fragColour=effect(uColour,uTexture,vUV,vec2(gl_FragCoord.x,love_ScreenSize.y-gl_FragCoord.y));}
)GLSL";
}
static std::string extractedVertex(const std::string &s) {
  return std::string(R"GLSL(#version 330 core
#define VERTEX
#define extern uniform
#define number float
#define Image sampler2D
#define highp
#define mediump
uniform vec4 love_ScreenSize;
uniform bool uFlip;
layout(location=0) in vec2 aPosition;
layout(location=1) in vec2 aUV;
out vec2 vUV;
vec4 loveTexel(sampler2D image,vec2 p){return texture(image,p);}
#define Texel loveTexel
)GLSL") + s +
         R"GLSL(
void main(){
 mat4 transform_projection=mat4(2./love_ScreenSize.x,0,0,0, 0,-2./love_ScreenSize.y,0,0, 0,0,1,0, -1,1,0,1);
 gl_Position=position(transform_projection,vec4(aPosition,0,1));vUV=aUV;
}
)GLSL";
}
static std::string instrument(std::string s, const std::string &name) {
  auto after = [&](const std::string &needle, const std::string &code) {
    auto at = s.find(needle);
    if (at != std::string::npos)
      s.insert(at + needle.size(), code);
  };
  if (name == "background" || name == "splash") {
    after("number uv_len = length(uv);",
          "\nif(uDebug==1)return vec4(uv+0.5,0.35,1.);\n");
    if (name == "background") {
      after("uv *= 30.;", "\nif(uDebug==2)return "
                          "vec4(vec3(0.15)+vec3(.7,.5,.25)*step(.89,max(cos(uv."
                          "x*2.),cos(uv.y*2.))),1.);\n");
      after("number c3p = 1. - min(1., c1p + c2p);",
            "\nif(uDebug==3)return vec4(c1p,c2p,c3p,1.);\n");
    } else {
      after("sv *= 30.;", "\nif(uDebug==2)return "
                          "vec4(vec3(0.15)+vec3(.7,.5,.25)*step(.89,max(cos(sv."
                          "x*2.),cos(sv.y*2.))),1.);\n");
      after("number cb = 1. - min(1., c1p + c2p);",
            "\nif(uDebug==3)return vec4(c1p,c2p,cb,1.);\n");
    }
    auto domain = s.find(name == "background" ? "//Make the paint amount"
                                              : "//Make the smoke amount");
    if (domain != std::string::npos)
      s.insert(domain, name == "background"
                           ? "if(uDebug==5)return "
                             "vec4(vec3(.15)+vec3(.7,.5,.25)*step(.89,max(cos("
                             "uv.x*2.),cos(uv.y*2.))),1.);\n"
                           : "if(uDebug==5)return "
                             "vec4(vec3(.15)+vec3(.7,.5,.25)*step(.89,max(cos("
                             "sv.x*2.),cos(sv.y*2.))),1.);\n");
  } else if (name == "CRT") {
    auto at = s.find("rgb_result += crt_tex.rgb * rgb_scanline");
    if (at != std::string::npos)
      s.insert(at, "if(uDebug==4)return vec4(rgb_scanline*.35+0.5,1.);\n");
  }
  return s;
}
struct Program : LabRhi::Program {
  Program() = default;
  explicit Program(const std::string &frag, const std::string &vsText = vertex)
      : LabRhi::Program(vsText, fragment(frag)) {}
};
static Program compileOriginal(const std::string &name,
                               const std::string &src) {
  auto vs = src.find("vec4 position(") != std::string::npos
                ? extractedVertex(src)
                : vertex;
  auto frag = src.find("vec4 effect(") != std::string::npos ||
                      src.find("vec4 effect (") != std::string::npos
                  ? instrument(src, name)
                  : "vec4 effect(vec4 c,Image t,vec2 uv,vec2 pc){return "
                    "Texel(t,uv)*c;}";
  std::ostringstream diagnostics;
  auto *previous = std::cerr.rdbuf(diagnostics.rdbuf());
  try {
    Program p(frag, vs);
    std::cerr.rdbuf(previous);
    return p;
  } catch (const std::exception &e) {
    std::cerr.rdbuf(previous);
    throw std::runtime_error(diagnostics.str() + e.what());
  }
}
using Texture = LabRhi::Texture;
using Target = LabRhi::Target;
static Texture loadTexture(const fs::path &p) {
  int w = 0, h = 0, c = 0;
  auto data = stbi_load(p.string().c_str(), &w, &h, &c, 4);
  if (!data)
    throw std::runtime_error("Cannot load " + p.string());
  try {
    auto tex = LabRhi::createTexture(w, h, data);
    stbi_image_free(data);
    return tex;
  } catch (...) {
    stbi_image_free(data);
    throw;
  }
}
struct Settings {
  int mode = 0, preset = 0, effect = 1, enhancement = 0, rank = 12, suit = 3,
      source = 0, debug = 0, seal = 0, sticker = 0;
  float time = 12, speed = 1, crt = 70, bloom = 0, glitch = 0, dissolve = .45f,
        phase = 0, hover = 1, amount = 5, contrast = 1, spin = 0;
  bool pause = false, crtOn = true, showSource = false, autoPhase = true,
       followMouse = true;
  float rotation = 0, juice = 0, seed = 81.2f;
  float mx = 800, my = 420;
};
static const char *modes[] = {
    "Overview",       "Background",  "Card editions", "Enhancements",
    "Card layers",    "Hover tilt",  "Dissolve",      "Flame",
    "CRT comparison", "Single card", "Other shaders", "Auxiliary effects"};
static const char *presets[] = {"Main menu / splash.fs",
                                "Small blind / background.fs",
                                "Big blind / background.fs",
                                "Boss: The Hook",
                                "Boss: The Wall",
                                "Showdown"};
static const char *effects[] = {
    "Normal",         "Foil",   "Holographic", "Polychrome",     "Negative",
    "Negative shine", "Debuff", "Played",      "Hologram Joker", "Gold seal"};
static const char *seals[] = {"None", "Gold", "Purple", "Red", "Blue"};
static const char *stickerNames[] = {"None",  "Eternal", "Perishable", "Rental",
                                     "White", "Red",     "Green",      "Black",
                                     "Blue",  "Purple",  "Orange",     "Gold"};
static const char *enhancements[] = {"Base",  "Bonus", "Mult",  "Wild", "Lucky",
                                     "Glass", "Steel", "Stone", "Gold"};
static const char *shaderNames[] = {
    "background", "splash",     "CRT",      "dissolve",       "foil",
    "holo",       "polychrome", "negative", "negative_shine", "debuff",
    "played",     "flame",      "hologram", "gold_seal",      "booster",
    "voucher",    "flash",      "skew",     "vortex"};
struct Lab {
  std::map<std::string, Program> programs;
  Texture deck, centers, jokers, stickers, vouchers, boosters, white;
  Target scene, output;
  Render::RenderResourceHandle<Render::VertexBufferSpec> mesh;
  int w = 1600, h = 900;
  std::string errors;
  std::array<char, 64000> editor{};
  int loadedSource = -1;
  ~Lab() {
    for (auto &[n, p] : programs)
      p.release();
    if (mesh.IsValid())
      LabRhi::backend().DeleteVertexBuffer({mesh});
    for (auto *t :
         {&deck, &centers, &jokers, &stickers, &vouchers, &boosters, &white})
      if (t->handle.IsValid())
        LabRhi::backend().DeleteTexture({t->handle});
  }
  void reload() {
    std::map<std::string, Program> fresh;
    try {
      for (auto n : shaderNames) {
        auto src = read(root / "Assets/shaders" / (std::string(n) + ".fs"));
        try {
          fresh.emplace(n, compileOriginal(n, src));
        } catch (const std::exception &e) {
          throw std::runtime_error(std::string(n) + ": " + e.what());
        }
      }
      fresh.emplace("copy", Program("vec4 effect(vec4 c,Image t,vec2 uv,vec2 "
                                    "pc){return Texel(t,uv)*c;}"));
      for (auto &[k, p] : programs)
        p.release();
      programs = std::move(fresh);
      errors.clear();
    } catch (const std::exception &e) {
      for (auto &[k, p] : fresh)
        p.release();
      errors = e.what();
      throw;
    }
  }
  void init() {
    reload();
    deck = loadTexture(root / "Assets/textures/8BitDeck.png");
    centers = loadTexture(root / "Assets/textures/Enhancers.png");
    jokers = loadTexture(root / "Assets/textures/Jokers.png");
    stickers = loadTexture(root / "Assets/textures/stickers.png");
    vouchers = loadTexture(root / "Assets/textures/Vouchers.png");
    boosters = loadTexture(root / "Assets/textures/boosters.png");
    unsigned char p[4] = {255, 255, 255, 255};
    white = LabRhi::createTexture(1, 1, p);
    Render::CreateVertexBufferCommand c;
    c.vertexLayout.typeSlots = {Render::VertexFieldType::Vec2,
                                Render::VertexFieldType::Vec2};
    c.numVertex = 6;
    c.usage = Render::BufferUsage::Stream;
    c.vertexData.resize(sizeof(float) * 24);
    mesh = LabRhi::backend().CreateVertexBuffer(c);
    if (!mesh.IsValid())
      throw std::runtime_error("RHI quad mesh creation failed");
  }

  void common(Program &p, Settings &s) {
    p.use();
    p.v4("love_ScreenSize", float(w), float(h), 1, 1);
    p.i("uTexture", 0);
    p.i("uFlip", 0);
    p.i("uDebug", 0);
    p.v4("uColour", 1, 1, 1, 1);
    p.f("time", s.seed);
    p.f("dissolve", 0);
    p.i("shadow", 0);
    p.v2("mouse_screen_pos", s.mx, s.my);
    p.f("hovering", 0);
    p.f("screen_scale", 65);
    p.f("vortex_amt", s.time);
    p.f("mid_flash", .4f);
    p.v4("burn_colour_1", 1, .44f, .12f, 1);
    p.v4("burn_colour_2", 1, .9f, .35f, 1);
    float phase = s.phase + std::min(s.rotation * 3.f, 1.f) + s.juice * 20.f +
                  (s.autoPhase ? s.time / 28.f : 0);
    for (auto n : {"foil", "holo", "polychrome", "negative", "negative_shine",
                   "debuff", "played", "hologram", "voucher", "booster"})
      p.v2(n, phase, s.time);
    p.v4("gold_seal", phase, 0, 0, 1);
  }
  void quad(Program &p, Texture &t, float x, float y, float cw, float ch,
            float ux = 0, float uy = 0, float uw = 1, float uh = 1,
            float rotation = 0) {
    float v[] = {x,       y,       ux,     uy,     x + cw,  y,
                 ux + uw, uy,      x + cw, y + ch, ux + uw, uy + uh,
                 x,       y,       ux,     uy,     x + cw,  y + ch,
                 ux + uw, uy + uh, x,      y + ch, ux,      uy + uh};
    if (rotation != 0) {
      float c = std::cos(rotation), ss = std::sin(rotation);
      for (int i = 0; i < 24; i += 4) {
        float px = v[i] - x - cw / 2, py = v[i + 1] - y - ch / 2;
        v[i] = x + cw / 2 + c * px - ss * py;
        v[i + 1] = y + ch / 2 + ss * px + c * py;
      }
    }
    Render::UpdateVertexBufferCommand update;
    update.handle = mesh;
    update.numVertex = 6;
    update.vertexData = LabRhi::bytes(v, sizeof(v));
    if (!LabRhi::backend().UpdateVertexBuffer(update))
      throw std::runtime_error("RHI quad upload failed");
    p.flush();
    LabRhi::backend().SetVertexBuffer({mesh});
    LabRhi::backend().BindTexture({t.handle, 0});
    LabRhi::backend().Draw({6});
  }

  void solid(Settings &s, float x, float y, float ww, float hh, float r,
             float g, float b, float a) {
    auto &p = programs.at("copy");
    common(p, s);
    p.v4("uColour", r, g, b, a);
    quad(p, white, x, y, ww, hh);
  }
  void background(Settings &s) {
    auto &p = programs.at(s.preset == 0 ? "splash" : "background");
    common(p, s);
    p.i("uDebug", s.debug);
    p.f("time", s.time);
    p.f("vort_speed", .4f);
    p.f("vort_offset", 0);
    p.f("mid_flash", 0);
    p.v4("colour_1", 254.f / 255, 95.f / 255, 85.f / 255);
    p.v4("colour_2", 0, 157.f / 255, 1);
    p.v4("colour_3", .12f, .16f, .17f);
    p.f("contrast", s.contrast);
    p.f("spin_amount", s.spin);
    p.f("spin_time", s.time * s.spin);
    if (s.preset == 1 || s.preset == 2) {
      p.v4("colour_1", .9f * 80 / 255, .9f * 132 / 255, .9f * 110 / 255);
      p.v4("colour_2", 1.3f * 80 / 255, 1.3f * 132 / 255, 1.3f * 110 / 255);
      p.v4("colour_3", .7f * 80 / 255, .7f * 132 / 255, .7f * 110 / 255);
    }
    if (s.preset == 3 || s.preset == 4) {
      float r = s.preset == 3 ? 168.f / 255 : 138.f / 255,
            g = s.preset == 3 ? 64.f / 255 : 89.f / 255,
            b = s.preset == 3 ? 36.f / 255 : 165.f / 255;
      float nr = (.3f * r + .7f * 55 / 255) * .9f + .1f,
            ng = (.3f * g + .7f * 66 / 255) * .9f + .1f,
            nb = (.3f * b + .7f * 68 / 255) * .9f + .1f;
      p.v4("colour_1", r, g, b);
      p.v4("colour_2", 1.3f * nr, 1.3f * ng, 1.3f * nb);
      p.v4("colour_3", .4f * nr, .4f * ng, .4f * nb);
    }
    if (s.preset == 5)
      p.v4("colour_3", .6f * 55 / 255, .6f * 66 / 255, .6f * 68 / 255);
    quad(p, white, 0, 0, float(w), float(h));
  }
  void cell(Settings &s, const char *shader, Texture &t, int cx, int cy,
            float x, float y, float scale, float hover = 0, bool shadow = false,
            float dis = 0, float extraRotation = 0) {
    auto &p = programs.at(shader);
    Settings local = s;
    local.phase += hover > 0 ? std::abs((s.mx - x) / (142 * scale) +
                                        (s.my - y) / (190 * scale) - 1) *
                                   .3f
                             : 0;
    common(p, local);
    p.v4("texture_details", float(cx), float(cy), 71, 95);
    p.v2("image_details", float(t.w) / 2, float(t.h) / 2);
    p.f("hovering", hover);
    p.i("shadow", shadow);
    p.f("dissolve", dis);
    if (std::string(shader) == "debuff")
      p.v2("debuff", 1, 1);
    quad(p, t, x, y, 142 * scale, 190 * scale, 142.f * cx / t.w,
         190.f * cy / t.h, 142.f / t.w, 190.f / t.h,
         s.rotation + extraRotation);
  }
  void card(Settings &s, int effect, int enhancement, float x, float y,
            float scale, float hover = 0, float dis = 0, int layer = 0) {
    static const int ex[] = {1, 1, 2, 3, 4, 5, 6, 5, 6},
                     ey[] = {0, 1, 1, 1, 1, 1, 1, 0, 0};
    int cx = ex[enhancement], cy = ey[enhancement];
    auto pass = [&](const char *n, bool sh = false) {
      if (layer != 2)
        cell(s, n, centers, cx, cy, x + (sh ? 6 : 0), y + (sh ? 11 : 0), scale,
             sh ? 0 : hover, sh, dis);
      if (enhancement != 7 && layer != 1)
        cell(s, n, deck, s.rank, s.suit, x + (sh ? 6 : 0), y + (sh ? 11 : 0),
             scale, sh ? 0 : hover, sh, dis);
    };
    if (enhancement != 5 && layer == 0)
      cell(s, "dissolve", centers, 1, 0, x + 6, y + 11, scale, 0, true, dis);
    if (effect == 4) {
      pass("negative");
      if (layer != 2)
        cell(s, "negative_shine", centers, cx, cy, x, y, scale, hover, false,
             dis);
      return;
    }
    pass("dissolve");
    static const char *map[] = {
        "dissolve",       "foil",   "holo",   "polychrome", "negative",
        "negative_shine", "debuff", "played", "hologram",   "voucher"};
    if (effect > 0) {
      if (effect == 5) {
        if (layer != 2)
          cell(s, map[effect], centers, cx, cy, x, y, scale, hover, false, dis);
      } else if (effect == 6 || effect == 7) { // State effects replace, rather
                                               // than layer, normal rendering.
        // Erase-free mode is achieved by selecting those in renderOne below.
      } else
        pass(map[effect]);
    }
  }
  void decorate(Settings &s, float x, float y, float scale, float hover,
                float dis, int sealOverride = 0) {
    int seal = sealOverride ? sealOverride : s.seal;
    if (seal) {
      static const int sx[] = {0, 2, 4, 5, 6}, sy[] = {0, 0, 4, 4, 4};
      cell(s, "dissolve", centers, sx[seal], sy[seal], x, y, scale, hover,
           false, dis);
      if (seal == 1)
        cell(s, "voucher", centers, 2, 0, x, y, scale, hover, false, dis);
    }
    if (s.sticker) {
      static const int sx[] = {0, 0, 0, 1, 1, 2, 3, 0, 4, 1, 2, 3},
                       sy[] = {0, 0, 2, 2, 0, 0, 0, 1, 0, 1, 1, 1};
      cell(s, "dissolve", stickers, sx[s.sticker], sy[s.sticker], x, y, scale,
           hover, false, dis);
      cell(s, "voucher", stickers, sx[s.sticker], sy[s.sticker], x, y, scale,
           hover, false, dis);
    }
  }
  void renderOne(Settings &s, int effect, int enhancement, float x, float y,
                 float scale, float hover = 0, float dis = 0, int layer = 0) {
    if (effect == 8) {
      cell(s, "dissolve", jokers, 4, 12, x + 6, y + 11, scale, 0, true, dis);
      cell(s, "dissolve", jokers, 4, 12, x, y, scale, hover, false, dis);
      float sm = .07f + .02f * std::sin(1.8f * s.time),
            rm = .05f * std::sin(1.219f * s.time);
      cell(s, "hologram", jokers, 2, 9, x - 142 * scale * sm,
           y - 190 * scale * sm, scale * (1 + 2 * sm), hover * 1.5f, false, dis,
           2 * rm);
      return;
    }
    if (effect == 6 || effect == 7) {
      if (effect == 6)
        card(s, 0, enhancement, x, y, scale, hover, dis, layer);
      decorate(s, x, y, scale, hover, dis);
      static const int ex[] = {1, 1, 2, 3, 4, 5, 6, 5, 6},
                       ey[] = {0, 1, 1, 1, 1, 1, 1, 0, 0};
      auto n = effect == 6 ? "debuff" : "played";
      cell(s, n, centers, ex[enhancement], ey[enhancement], x, y, scale, hover,
           false, dis);
      if (enhancement != 7)
        cell(s, n, deck, s.rank, s.suit, x, y, scale, hover, false, dis);
    } else {
      card(s, effect == 9 ? 0 : effect, enhancement, x, y, scale, hover, dis,
           layer);
      if (layer == 0)
        decorate(s, x, y, scale, hover, dis, effect == 9 ? 1 : 0);
    }
  }
  void render(Settings &s, int width, int height) {
    w = width;
    h = height;
    scene.resize(w, h);
    output.resize(w, h);
    scene.bind(true);
    LabRhi::blend = true;
    background(s);
    float k = w / 1600.f;
    auto one = [&](int e, int en, float x, float y, float sc, float hov = 0,
                   float dis = 0, int layer = 0) {
      renderOne(s, e, en, x * k, y * k, sc * k, hov, dis, layer);
    };
    if (s.mode == 0 || s.mode == 2 || s.mode == 8) {
      solid(s, 35 * k, 165 * k, 1530 * k, 610 * k, .025f, .035f, .045f, .84f);
      for (int i = 0; i < 5; i++)
        one(i, 0, 100.f + 300 * i, 250, 1.55f);
      if (s.mode == 0) {
        for (int i = 0; i < 5; i++)
          one(i + 5, 0, 100.f + 300 * i, 560, .74f);
      }
    }
    if (s.mode == 3) {
      solid(s, 35 * k, 155 * k, 1530 * k, 650 * k, .025f, .035f, .045f, .9f);
      for (int i = 0; i < 9; i++) {
        int col = i % 5, row = i / 5;
        one(s.effect, i, 100.f + 300 * col, 210.f + 310 * row, 1.25f);
      }
    }
    if (s.mode == 4) {
      solid(s, 35 * k, 155 * k, 1530 * k, 650 * k, .025f, .035f, .045f, .9f);
      one(0, 0, 180, 270, 2, 0, 0, 1);
      one(0, 0, 650, 270, 2, 0, 0, 2);
      one(0, 0, 1120, 270, 2);
    }
    if (s.mode == 5) {
      solid(s, 35 * k, 155 * k, 1530 * k, 650 * k, .025f, .035f, .045f, .9f);
      one(s.effect, s.enhancement, 250, 250, 2.2f);
      one(s.effect, s.enhancement, 1000, 250, 2.2f, s.hover);
    }
    if (s.mode == 6) {
      solid(s, 35 * k, 155 * k, 1530 * k, 650 * k, .025f, .035f, .045f, .9f);
      one(s.effect, s.enhancement, 680, 215, 2.6f, 0, s.dissolve);
    }
    if (s.mode == 9) {
      solid(s, 35 * k, 155 * k, 1530 * k, 650 * k, .025f, .035f, .045f, .9f);
      one(s.effect, s.enhancement, 650, 205, 2.8f, s.hover);
    }
    if (s.mode == 10) {
      solid(s, 35 * k, 155 * k, 1530 * k, 650 * k, .025f, .035f, .045f, .9f);
      cell(s, shaderNames[s.source], centers, 1, 0, 650 * k, 220 * k, 2.5f * k,
           s.hover);
    }
    if (s.mode == 11) {
      solid(s, 35 * k, 155 * k, 1530 * k, 650 * k, .025f, .035f, .045f, .9f);
      one(6, 0, 100, 250, 1.55f);
      one(7, 0, 400, 250, 1.55f);
      cell(s, "dissolve", vouchers, 0, 0, 700 * k, 250 * k, 1.55f * k);
      cell(s, "voucher", vouchers, 0, 0, 700 * k, 250 * k, 1.55f * k);
      cell(s, "dissolve", boosters, 0, 0, 1000 * k, 250 * k, 1.55f * k);
      cell(s, "booster", boosters, 0, 0, 1000 * k, 250 * k, 1.55f * k);
      one(8, 0, 1300, 250, 1.55f);
    }
    if (s.mode == 7) {
      auto &p = programs.at("flame");
      common(p, s);
      p.f("time", s.time);
      p.f("amount", s.amount);
      p.f("id", 7);
      p.v4("texture_details", 0, 0, float(w), float(h));
      p.v2("image_details", float(w), float(h));
      p.v4("colour_1", 1, .35f, .12f, 1);
      p.v4("colour_2", 1, .9f, .3f, 1);
      quad(p, white, 0, 0, float(w), float(h));
    }
    output.bind(true);
    LabRhi::blend = false;
    auto &p = programs.at(s.crtOn ? "CRT" : "copy");
    common(p, s);
    p.i("uFlip", 1);
    p.i("uDebug", s.debug == 4 ? 4 : 0);
    if (s.crtOn)
      p.f("hovering", 1);
    float c = s.crt * .3f / 100;
    p.f("time", 400 + s.time);
    p.v2("distortion_fac", 1 + .07f * c, 1 + .1f * c);
    p.v2("scale_fac", 1 - .008f * c, 1 - .008f * c);
    p.f("feather_fac", .01f);
    p.f("noise_fac", .001f * c);
    p.f("crt_intensity", .16f * c);
    p.f("bloom_fac", s.bloom);
    p.f("glitch_intensity", s.glitch);
    p.f("scanlines", h * .75f);
    quad(p, scene.tex, 0, 0, float(w), float(h));
    LabRhi::blend = true;
  }
  void save(const fs::path &p) {
    fs::create_directories(p.parent_path());
    output.bind();
    std::vector<unsigned char> bytes(size_t(w) * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
    for (int y = 0; y < h / 2; y++)
      for (int x = 0; x < w * 4; x++)
        std::swap(bytes[size_t(y) * w * 4 + x],
                  bytes[size_t(h - 1 - y) * w * 4 + x]);
    if (!stbi_write_png(p.string().c_str(), w, h, 4, bytes.data(), w * 4))
      throw std::runtime_error("PNG write failed");
  }
};
static void setPreset(Settings &s, int p) {
  s.preset = p;
  s.spin = p == 3 || p == 4 ? .25f : p == 5 ? .5f : 0;
  s.contrast = p >= 3 ? (p == 5 ? 3.f : 2.f) : 1;
}
static void text(ImDrawList *d, float x, float y, float size,
                 const std::string &s, ImU32 c = IM_COL32(236, 239, 232, 255)) {
  d->AddText(nullptr, size, ImVec2(x, y), c, s.c_str());
}
static void labels(Settings &s, int width, int height) {
  float k = width / 1600.f;
  auto *d = ImGui::GetForegroundDrawList();
  d->AddRectFilled({0, 0}, {float(width), 115 * k}, IM_COL32(8, 13, 19, 235));
  text(d, 46 * k, 24 * k, 30 * k, "BALATRO / SHADER LAB");
  text(d, 46 * k, 68 * k, 18 * k,
       std::string(modes[s.mode]) + "   |   " + presets[s.preset] +
           "   |   t=" + std::to_string(s.time).substr(0, 5));
  if (s.mode == 0 || s.mode == 2 || s.mode == 8) {
    for (int i = 0; i < 5; i++)
      text(d, (100 + 300 * i) * k, 205 * k, 21 * k, effects[i]);
    if (s.mode == 0)
      for (int i = 0; i < 5; i++)
        text(d, (100 + 300 * i) * k, 705 * k, 18 * k, effects[i + 5]);
  }
  if (s.mode == 3)
    for (int i = 0; i < 9; i++)
      text(d, (100 + 300 * (i % 5)) * k, (185 + 310 * (i / 5)) * k, 18 * k,
           enhancements[i]);
  if (s.mode == 4) {
    text(d, 180 * k, 210 * k, 25 * k, "1 / Center atlas");
    text(d, 650 * k, 210 * k, 25 * k, "2 / Front atlas");
    text(d, 1120 * k, 210 * k, 25 * k, "3 / Alpha composite");
  }
  if (s.mode == 5) {
    text(d, 250 * k, 200 * k, 25 * k, "Hover = 0");
    text(d, 1000 * k, 200 * k, 25 * k, "Hover = 1 / perspective W");
  }
  if (s.mode == 6)
    text(d, 75 * k, 195 * k, 26 * k,
         "Dissolve = " + std::to_string(s.dissolve).substr(0, 4));
  if (s.mode == 9)
    text(d, 80 * k, 195 * k, 26 * k, effects[s.effect]);
  if (s.mode == 10)
    text(d, 80 * k, 195 * k, 26 * k, shaderNames[s.source]);
  if (s.mode == 11) {
    static const char *names[] = {"Debuff", "Played", "Voucher", "Booster",
                                  "Hologram Joker"};
    for (int i = 0; i < 5; i++)
      text(d, (100 + 300 * i) * k, 205 * k, 21 * k, names[i]);
  }
  d->AddRectFilled({0, float(height) - 56 * k}, {float(width), float(height)},
                   IM_COL32(8, 13, 19, 235));
  text(d, 46 * k, height - 38 * k, 17 * k,
       "Original GLSL  /  ApplicationWindow + Render RHI   |   CRT " +
           std::string(s.crtOn ? "ON" : "OFF") +
           "   |   Educational reconstruction");
}
static void checkGL() {
  auto e = glGetError();
  if (e != GL_NO_ERROR)
    throw std::runtime_error("OpenGL error " + std::to_string(e));
}
static void previewLabels(Settings &s, ImVec2 origin, float scale) {
  auto *d = ImGui::GetWindowDrawList();
  auto label = [&](float x, float y, const char *value) {
    text(d, origin.x + x * scale, origin.y + y * scale, 20 * scale, value);
  };
  if (s.mode == 0 || s.mode == 2 || s.mode == 8) {
    for (int i = 0; i < 5; i++)
      label(100.f + 300 * i, 205, effects[i]);
    if (s.mode == 0)
      for (int i = 0; i < 5; i++)
        label(100.f + 300 * i, 705, effects[i + 5]);
  }
  if (s.mode == 3)
    for (int i = 0; i < 9; i++)
      label(100.f + 300 * (i % 5), 185.f + 310 * (i / 5), enhancements[i]);
  if (s.mode == 4) {
    label(180, 210, "1 / Center atlas");
    label(650, 210, "2 / Front atlas");
    label(1120, 210, "3 / Composite");
  }
  if (s.mode == 5) {
    label(250, 200, "Hover = 0");
    label(1000, 200, "Hover perspective");
  }
  if (s.mode == 9)
    label(80, 195, effects[s.effect]);
  if (s.mode == 10)
    label(80, 195, shaderNames[s.source]);
  if (s.mode == 11) {
    const char *names[] = {"Debuff", "Played", "Voucher", "Booster",
                           "Hologram Joker"};
    for (int i = 0; i < 5; i++)
      label(100.f + 300 * i, 205, names[i]);
  }
}
int main(int argc, char **argv) {
  try {
    bool smoke = false;
    fs::path captures, captureUI;
    int frames = 0;
    for (int i = 1; i < argc; i++) {
      std::string a = argv[i];
      if (a == "--smoke-test")
        smoke = true;
      else if (a == "--capture-dir" && i + 1 < argc)
        captures = argv[++i];
      else if (a == "--capture-ui" && i + 1 < argc)
        captureUI = argv[++i];
      else if (a == "--capture-frames" && i + 1 < argc)
        frames = std::stoi(argv[++i]);
      else if (a == "--assets" && i + 1 < argc)
        root = argv[++i];
      else if (a == "--help") {
        std::cout << "balatro_shader_lab [--smoke-test] [--capture-dir PATH] "
                     "[--capture-frames N] [--capture-ui PNG] [--assets "
                     "MODULE_DIR]\n";
        return 0;
      }
    }
    ApplicationWindow::Window appWindow(
        {1600, 900, "Balatro Shader Lab - ApplicationWindow + Render RHI", true,
         Render::BackendType::Opengl});
    if (!appWindow.Activate())
      throw std::runtime_error("ApplicationWindow activation failed");
    auto *window = appWindow.GetNativeWindow();
    if (smoke || !captures.empty() || !captureUI.empty())
      glfwHideWindow(window);
    glfwSetWindowSizeLimits(window, 1000, 650, GLFW_DONT_CARE, GLFW_DONT_CARE);
    auto contextSpec = *appWindow.GetRenderContextAsOpengl();
    LabRhi::Context rhiContext(contextSpec);
    LabRhi::context = &rhiContext;
    std::cout << "Renderer: " << glGetString(GL_RENDERER)
              << "\nOpenGL: " << glGetString(GL_VERSION) << '\n';

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    Lab lab;
    lab.init();
    Settings s;
    std::vector<std::string> captureRecords;
    auto validation = [&](const fs::path &file, int scenes) {
      fs::create_directories(file.parent_path());
      std::ofstream f(file);
      f << "{\n  \"renderer\": \"" << glGetString(GL_RENDERER)
        << "\",\n  \"opengl\": \"" << glGetString(GL_VERSION)
        << "\",\n  \"window_module\": \"ApplicationWindow::Window\",\n  "
           "\"render_module\": \"Render::OpenglBackend\",\n  "
           "\"uniform_transport\": \"std140 RHI uniform buffers in 256-byte "
           "updates\",\n  \"fragment_shaders\": 17,\n  "
           "\"vertex_only_shaders\": 2,\n  \"extracted_shaders_compiled\": "
           "19,\n  \"scene_preset_combinations\": "
        << scenes
        << ",\n  \"card_effects\": 10,\n  \"gl_error\": 0,\n  "
           "\"logical_card_size\": [71,95],\n  \"physical_card_size\": "
           "[142,190],\n  \"atlas_filter\": \"LINEAR\",\n  "
           "\"captured_frames\": [";
      for (size_t i = 0; i < captureRecords.size(); i++) {
        if (i)
          f << ",";
        f << "\n    " << captureRecords[i];
      }
      f << "\n  ]\n}\n";
    };
    auto frame = [&]() {
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
    };
    auto capture = [&](const std::string &name) {
      lab.render(s, 1600, 900);
      LabRhi::windowTarget(1600, 900);
      auto &p = lab.programs.at("copy");
      lab.common(p, s);
      p.i("uFlip", 1);
      lab.quad(p, lab.output.tex, 0, 0, 1600, 900);
      frame();
      labels(s, 1600, 900);
      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(
          ImGui::GetDrawData()); // Capture annotations from the same
                                 // GPU-rendered framebuffer.
      std::vector<unsigned char> bytes(1600 * 900 * 4);
      glReadPixels(0, 0, 1600, 900, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
      for (int y = 0; y < 450; y++)
        for (int x = 0; x < 6400; x++)
          std::swap(bytes[y * 6400 + x], bytes[(899 - y) * 6400 + x]);
      fs::create_directories(captures);
      if (!stbi_write_png((captures / name).string().c_str(), 1600, 900, 4,
                          bytes.data(), 6400))
        throw std::runtime_error("Capture failed");
      checkGL();
      std::ostringstream rec;
      rec << "{\"file\":\"" << name << "\",\"scene\":" << s.mode
          << ",\"preset\":" << s.preset << ",\"time\":" << s.time
          << ",\"crt\":" << (s.crtOn ? "true" : "false")
          << ",\"debug\":" << s.debug << ",\"dissolve\":" << s.dissolve
          << ",\"bloom_factor\":" << s.bloom << "}";
      captureRecords.push_back(rec.str());
      std::cout << "Captured " << name << '\n';
    };
    if (smoke) {
      for (int mode = 0; mode < 12; mode++) {
        s.mode = mode;
        for (int p = 0; p < 6; p++) {
          setPreset(s, p);
          lab.render(s, 640, 360);
          checkGL();
        }
      }
      for (int e = 0; e < 10; e++) {
        s.effect = e;
        s.mode = 5;
        lab.render(s, 640, 360);
        checkGL();
      }
      s.mode = 10;
      for (int i = 0; i < 19; i++) {
        s.source = i;
        lab.render(s, 640, 360);
        checkGL();
      }
      bool rejected = false;
      try {
        auto invalid =
            compileOriginal("foil", "vec4 effect( INVALID EDITOR BUFFER");
        invalid.release();
      } catch (const std::exception &) {
        rejected = true;
      }
      if (!rejected)
        throw std::runtime_error("Invalid shader editor buffer was accepted");
      validation(root / "Evidence/gpu-validation.json", 72);
      std::cout << "PASS: 19 extracted shaders (17 fragment, 2 vertex-only), "
                   "72 scene/preset combinations, 10 card effects, 19 shader "
                   "draws; framebuffer complete, GL_NO_ERROR.\n";
      return 0;
    }
    if (!captures.empty()) {
      s.mode = 0;
      capture("overview.png");
      s.mode = 1;
      for (int p = 0; p < 6; p++) {
        setPreset(s, p);
        capture(std::string("background-") +
                std::array<const char *, 6>{"menu", "small", "big", "boss",
                                            "wall", "showdown"}[p] +
                ".png");
      }
      setPreset(s, 0);
      s.crtOn = false;
      for (int d = 1; d <= 3; d++) {
        s.debug = d;
        capture(
            std::string("background-") +
            std::array<const char *, 3>{"coordinates", "warp", "mix"}[d - 1] +
            ".png");
      }
      s.debug = 5;
      capture("background-domain.png");
      s.debug = 0;
      s.crtOn = true;
      s.mode = 2;
      capture("card-editions.png");
      s.time = 52;
      capture("card-editions-later.png");
      s.time = 12;
      s.mode = 9;
      for (int e = 0; e < 5; e++) {
        s.effect = e;
        capture(std::string("edition-") +
                std::array<const char *, 5>{"base", "foil", "holo",
                                            "polychrome", "negative"}[e] +
                ".png");
      }
      s.mode = 3;
      s.effect = 0;
      capture("card-enhancements.png");
      s.mode = 4;
      capture("card-layers.png");
      s.mode = 5;
      s.effect = 2;
      s.mx = 1260;
      s.my = 390;
      capture("card-hover.png");
      s.mode = 6;
      s.effect = 0;
      for (float d : {0.f, .5f, .9f}) {
        s.dissolve = d;
        capture(d == 0    ? "dissolve-0.png"
                : d < .6f ? "dissolve-50.png"
                          : "dissolve-90.png");
      }
      s.mode = 7;
      capture("flame.png");
      s.mode = 11;
      capture("effects.png");
      s.mode = 8;
      s.crtOn = false;
      capture("crt-before.png");
      s.crtOn = true;
      capture("crt-after.png");
      s.debug = 4;
      capture("crt-scanlines.png");
      s.debug = 0;
      s.bloom = 0;
      capture("crt-bloom-off.png");
      s.bloom = 2;
      capture("crt-bloom-on.png");
      s.bloom = 0;
      s.mode = 10;
      for (int i = 0; i < 19; i++) {
        s.source = i;
        capture(std::string("shader-") + shaderNames[i] + ".png");
      }
      s.mode = 9;
      s.effect = 0;
      for (int i = 1; i < 5; i++) {
        s.seal = i;
        capture(std::string("seal-") + seals[i] + ".png");
      }
      s.seal = 0;
      for (int i = 1; i < 4; i++) {
        s.sticker = i;
        capture(std::string("sticker-") + stickerNames[i] + ".png");
      }
      s.sticker = 0;
      s.mode = 0;
      s.crt = 70;
      for (int i = 0; i < frames; i++) {
        s.time = i / 30.f;
        capture("frame-" + std::to_string(i) + ".png");
      }
      validation(captures / "capture-manifest.json", 0);
      std::cout << "CAPTURE PASS\n";
      return 0;
    }
    int uiFrames = 0;
    double last = glfwGetTime();
    while (!appWindow.ShouldClose()) {
      appWindow.PollEvents();
      double now = glfwGetTime();
      if (!s.pause)
        s.time += float(now - last) * s.speed;
      last = now;
      frame();
      int ww, hh;
      glfwGetFramebufferSize(window, &ww, &hh);
      LabRhi::windowTarget(ww, hh, true);
      ImGui::SetNextWindowPos({0, 0});
      ImGui::SetNextWindowSize({350.f, float(hh)});
      ImGui::Begin("Shader controls", nullptr,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoCollapse);
      ImGui::Text("BALATRO / SHADER LAB");
      ImGui::TextDisabled("Original GLSL, inspectable passes");
      ImGui::Separator();
      ImGui::Combo("Scene", &s.mode, modes, 12);
      int old = s.preset;
      if (ImGui::Combo("Background", &s.preset, presets, 6) && s.preset != old)
        setPreset(s, s.preset);
      ImGui::Combo("Edition/state", &s.effect, effects, 10);
      ImGui::Combo("Enhancement", &s.enhancement, enhancements, 9);
      ImGui::SliderInt("Rank 2..A", &s.rank, 0, 12);
      ImGui::SliderInt("Suit H C D S", &s.suit, 0, 3);
      ImGui::Combo("Seal", &s.seal, seals, 5);
      ImGui::Combo("Sticker", &s.sticker, stickerNames, 12);
      ImGui::SeparatorText("Timeline");
      ImGui::Checkbox("Pause", &s.pause);
      ImGui::SameLine();
      if (ImGui::Button("Reset"))
        s.time = 0;
      ImGui::SliderFloat("Time", &s.time, 0, 120, "%.2f s");
      ImGui::SliderFloat("Speed", &s.speed, 0, 3);
      ImGui::Checkbox("Animated phase", &s.autoPhase);
      ImGui::SliderFloat("Phase", &s.phase, -2, 4);
      ImGui::SliderFloat("Rotation", &s.rotation, -.3f, .3f);
      ImGui::SliderFloat("Juice", &s.juice, 0, .1f);
      ImGui::SliderFloat("Card seed", &s.seed, 0, 3000);
      ImGui::SliderFloat("Dissolve", &s.dissolve, 0, 1);
      ImGui::SliderFloat("Hover", &s.hover, 0, 2);
      ImGui::SliderFloat("Flame amount", &s.amount, 0, 10);
      ImGui::SeparatorText("Background / CRT");
      ImGui::SliderFloat("Contrast", &s.contrast, .4f, 4);
      ImGui::SliderFloat("Spin amount", &s.spin, 0, 1);
      ImGui::SliderInt("Debug stage", &s.debug, 0, 5);
      ImGui::Combo("Other shader", &s.source, shaderNames, 19);
      ImGui::Checkbox("CRT post-process", &s.crtOn);
      ImGui::SliderFloat("CRT setting", &s.crt, 0, 100);
      ImGui::SliderFloat("Bloom factor", &s.bloom, 0, 2);
      ImGui::SliderFloat("Glitch", &s.glitch, 0, 2);
      ImGui::Checkbox("Mouse follows preview", &s.followMouse);
      ImGui::Checkbox("GLSL source / editor", &s.showSource);
      if (ImGui::Button("Reload shaders [F5]") ||
          ImGui::IsKeyPressed(ImGuiKey_F5)) {
        try {
          lab.reload();
          lab.loadedSource = -1;
        } catch (const std::exception &e) {
          lab.errors = e.what();
        }
      }
      if (ImGui::Button("Save current PNG")) {
        lab.render(s, 1600, 900);
        lab.save(root / "Captures/current.png");
      }
      ImGui::TextWrapped(
          "Select a scene to inspect each pass. Editions are drawn over the "
          "normal card. Negative has its own base and shine passes.");
      if (!lab.errors.empty())
        ImGui::TextWrapped("%s", lab.errors.c_str());
      ImGui::End();
      ImGui::SetNextWindowPos({350, 0});
      ImGui::SetNextWindowSize({float(ww - 350), float(hh)});
      ImGui::Begin("Live preview", nullptr,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoCollapse);
      ImVec2 avail = ImGui::GetContentRegionAvail();
      float vh = std::min(avail.y, avail.x * 9 / 16), vw = vh * 16 / 9;
      auto pos = ImGui::GetCursorScreenPos();
      vw = std::max(vw, 1.f);
      vh = std::max(vh, 1.f);
      if (s.followMouse) {
        auto m = ImGui::GetIO().MousePos;
        s.mx = std::clamp((m.x - pos.x) * 1600 / vw, 0.f, 1600.f);
        s.my = std::clamp((m.y - pos.y) * 900 / vh, 0.f, 900.f);
      }
      lab.render(s, 1600, 900);
      LabRhi::windowTarget(ww, hh);
      ImGui::Image((ImTextureID)(intptr_t)lab.output.tex.id, {vw, vh}, {0, 1},
                   {1, 0});
      previewLabels(s, pos, vw / 1600.f);
      ImGui::Text("%.1f FPS | %s | %s", ImGui::GetIO().Framerate, modes[s.mode],
                  presets[s.preset]);
      ImGui::End();
      if (s.showSource) {
        ImGui::SetNextWindowSize({800, 720}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Extracted GLSL editor", &s.showSource);
        ImGui::Combo("Source", &s.source, shaderNames, 19);
        if (lab.loadedSource != s.source) {
          auto t = read(root / "Assets/shaders" /
                        (std::string(shaderNames[s.source]) + ".fs"));
          std::snprintf(lab.editor.data(), lab.editor.size(), "%s", t.c_str());
          lab.loadedSource = s.source;
        }
        if (ImGui::Button("Compile edited buffer (session only)")) {
          try {
            Program p =
                compileOriginal(shaderNames[s.source], lab.editor.data());
            auto &oldp = lab.programs.at(shaderNames[s.source]);
            oldp.release();
            oldp = p;
            lab.errors.clear();
          } catch (const std::exception &e) {
            lab.errors = e.what();
          }
        }
        ImGui::TextDisabled(
            "Original asset files stay unchanged. Reload restores them.");
        ImGui::InputTextMultiline("##GLSL", lab.editor.data(),
                                  lab.editor.size(), {-1, -100},
                                  ImGuiInputTextFlags_AllowTabInput);
        ImGui::TextWrapped("%s", lab.errors.c_str());
        ImGui::End();
      }
      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      if (!captureUI.empty() && ++uiFrames >= 3) {
        if (!captureUI.parent_path().empty())
          fs::create_directories(captureUI.parent_path());
        std::vector<unsigned char> pixels(size_t(ww) * hh * 4);
        glReadPixels(0, 0, ww, hh, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (int yy = 0; yy < hh / 2; yy++)
          for (int xx = 0; xx < ww * 4; xx++)
            std::swap(pixels[size_t(yy) * ww * 4 + xx],
                      pixels[size_t(hh - 1 - yy) * ww * 4 + xx]);
        if (!stbi_write_png(captureUI.string().c_str(), ww, hh, 4,
                            pixels.data(), ww * 4))
          throw std::runtime_error("UI capture failed");
        checkGL();
        std::cout << "UI captured " << captureUI.string() << "\n";
        break;
      }
      LabRhi::backend().EndFrame({true});
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << '\n';
    return 1;
  }
}
