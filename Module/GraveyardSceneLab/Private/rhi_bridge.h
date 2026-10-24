#pragma once
// Synchronous adapter over OpenglLearn's existing Render module. All scene GPU
// resources, pipeline state, buffer updates and draws are owned by that module.
// ImGui's upstream renderer is used only for editor chrome and text labels.
#include "ApplicationWindow/public/window.h"
#include "Render/Private/Backend/Opengl/backend.h"
#include <array>
#include <cstring>
#include <map>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>
namespace LabRhi {
using namespace Render;
struct Context {
  RHIResourcePool resources;
  OpenglBackend backend{RHIBackContext{&resources}};
  explicit Context(OpenglBackendContext &spec) { backend.Init(&spec); }
  ~Context() { backend.Shutdown(); }
};
inline Context *context = nullptr;
inline bool blend = true;
inline OpenglBackend &backend() { return context->backend; }
inline void windowTarget(int w, int h, bool clear = false) {
  RHICommand::SetRenderTarget c;
  c.width = std::max(1, w);
  c.height = std::max(1, h);
  c.clearFlags = clear ? RHICommand::ClearColor : RHICommand::ClearNone;
  c.clearColor = {.025f, .035f, .05f, 1};
  backend().SetRenderTarget(c);
}
struct UniformLayout {
  std::map<std::string, std::string> types;
  std::map<std::string, size_t> slots;
  static const std::regex &declaration() {
    static std::regex r(
        R"((?:extern|uniform)\s+(?:MY_HIGHP_OR_MEDIUMP\s+)?(number|float|int|bool|vec2|vec3|vec4)\s+([A-Za-z_]\w*)\s*;)");
    return r;
  }
  void collect(const std::string &s) {
    for (auto i = std::sregex_iterator(s.begin(), s.end(), declaration());
         i != std::sregex_iterator(); ++i) {
      auto type = (*i)[1].str(), name = (*i)[2].str();
      types[name] = type == "number" ? "float" : type;
    }
    size_t index = 0;
    for (auto &[name, type] : types)
      slots[name] = index++;
  }
  std::string adapt(std::string s) const {
    s = std::regex_replace(s, declaration(), "");
    auto at = s.find('\n');
    if (at == std::string::npos)
      throw std::runtime_error("GLSL version header missing");
    s.replace(0, at, "#version 450 core");
    at = s.find('\n');
    std::string prefix = "\nlayout(std140,binding=0) uniform LabUniforms { "
                         "vec4 labUniformSlots[" +
                         std::to_string(std::max(size_t(1), types.size())) +
                         "]; };\n";
    for (auto &[name, type] : types) {
      auto value = "labUniformSlots[" + std::to_string(slots.at(name)) + "]";
      if (type == "bool")
        value = "(" + value + ".x>0.5)";
      else if (type == "int")
        value = "int(" + value + ".x)";
      else
        value += type == "float"  ? ".x"
                 : type == "vec2" ? ".xy"
                 : type == "vec3" ? ".xyz"
                                  : ".xyzw";
      prefix += "#define " + name + " " + value + "\n";
    }
    s.insert(at + 1, prefix);
    auto sampler = s.find("uniform sampler2D uTexture;");
    if (sampler != std::string::npos)
      s.insert(sampler, "layout(binding=0) ");
    return s;
  }
};
struct Program {
  RenderResourceHandle<ShaderProgramSpec> handle;
  RenderResourceHandle<PipelineSpec> alpha, opaque;
  RenderResourceHandle<UniformBufferSpec> uniforms;
  UniformLayout layout;
  std::vector<std::array<float, 4>> values;
  Program() = default;
  Program(const std::string &v, const std::string &f, bool spatial = false,
          bool depthWrite = true, bool copyDepth = false) {
    try {
      layout.collect(v);
      layout.collect(f);
      values.resize(std::max(size_t(1), layout.slots.size()));
      CreateShaderSourceCommand vc;
      vc.type = ShaderSourceType::Vertex;
      vc.source = layout.adapt(v);
      auto vs = backend().CreateShaderSource(vc);
      CreateShaderSourceCommand fc;
      fc.type = ShaderSourceType::Fragment;
      fc.source = layout.adapt(f);
      auto ps = backend().CreateShaderSource(fc);
      if (vs.IsValid() && ps.IsValid()) {
        CreateGraphicShaderProgramCommand pc;
        pc.createDesc.vertexShaderSource = vs;
        pc.createDesc.fragmentShaderSource = ps;
        handle = backend().CreateGraphicShaderProgram(pc);
      }
      if (vs.IsValid())
        backend().DeleteShaderSource({vs});
      if (ps.IsValid())
        backend().DeleteShaderSource({ps});
      if (!handle.IsValid())
        throw std::runtime_error("Render module GLSL compilation/link failed "
                                 "(see console diagnostics)");
      CreatePipelineCommand pc;
      pc.desc.spec.shaderProgram = handle;
      pc.desc.spec.cullMode = CullMode::None;
      pc.desc.spec.depthTest = spatial || copyDepth;
      pc.desc.spec.depthWrite = (spatial && depthWrite) || copyDepth;
      if(copyDepth)pc.desc.spec.depthCompare = CompareOp::Always;
      pc.desc.spec.blendEnable = true;
      pc.desc.spec.blendMode = BlendMode::AlphaComposite;
      pc.desc.spec.expectVertexLayout.typeSlots = {VertexFieldType::Vec2,
                                                   VertexFieldType::Vec2};
      if (spatial)
        pc.desc.spec.expectVertexLayout.typeSlots = {VertexFieldType::Vec3,
            VertexFieldType::Vec2, VertexFieldType::Vec3, VertexFieldType::Vec4};
      alpha = backend().CreatePipeline(pc);
      pc.desc.spec.blendEnable = false;
      pc.desc.spec.blendMode = BlendMode::Opaque;
      opaque = backend().CreatePipeline(pc);
      CreateUniformBufferCommand uc;
      uc.byteSize = uint32_t(values.size() * 16);
      uniforms = backend().CreateUniformBuffer(uc);
      if (!uniforms.IsValid())
        throw std::runtime_error("RHI uniform buffer allocation failed");
    } catch (...) {
      release();
      throw;
    }
  }
  void release() {
    if (alpha.IsValid())
      backend().DeletePipeline({alpha});
    if (opaque.IsValid())
      backend().DeletePipeline({opaque});
    if (uniforms.IsValid())
      backend().DeleteUniformBuffer({uniforms});
    if (handle.IsValid())
      backend().DeleteShaderProgram({handle});
    alpha = {};
    opaque = {};
    uniforms = {};
    handle = {};
  }
  void use() { backend().SetPipeline({blend ? alpha : opaque}); }
  void useOpaque() { backend().SetPipeline({opaque}); }
  void set(const char *n, float x, float y = 0, float z = 0, float w = 0) {
    auto i = layout.slots.find(n);
    if (i != layout.slots.end())
      values[i->second] = {x, y, z, w};
  }
  void f(const char *n, float v) { set(n, v); }
  void i(const char *n, int v) { set(n, float(v)); }
  void v2(const char *n, float x, float y) { set(n, x, y); }
  void v4(const char *n, float x, float y, float z, float w = 1) {
    set(n, x, y, z, w);
  }
  void flush() {
    auto *p = reinterpret_cast<const std::byte *>(values.data());
    size_t bytes = values.size() * 16;
    for (size_t offset = 0; offset < bytes;
         offset += RHICommand::MaxInlineUniformBytes) {
      RHICommand::UpdateUniformBuffer c;
      c.buffer = uniforms;
      c.offset = uint32_t(offset);
      c.size =
          uint32_t(std::min(RHICommand::MaxInlineUniformBytes, bytes - offset));
      std::memcpy(c.data.data(), p + offset, c.size);
      backend().UpdateUniformBuffer(c);
    }
    backend().BindUniformBuffer({uniforms, 0});
  }
};
struct Texture {
  RenderResourceHandle<RHITextureSpec> handle;
  unsigned id = 0;
  int w = 0, h = 0; // Native id is exposed only to ImGui's UI renderer.
};
inline Texture createTexture(int w, int h, const void *data = nullptr,
                             RHIFilterMode filter = RHIFilterMode::Nearest) {
  CreateTextureCommand c;
  c.spec.width = w;
  c.spec.height = h;
  c.spec.filterMode = filter;
  c.spec.addressMode = RHIAddressMode::ClampToEdge;
  c.spec.mipmaps = false;
  if (data) {
    c.data.resize(size_t(w) * h * 4);
    std::memcpy(c.data.data(), data, c.data.size());
  }
  auto handle = backend().CreateTexture(c);
  if (!handle.IsValid())
    throw std::runtime_error("RHI texture allocation failed");
  return {handle, context->resources.TextureTable.Get(handle)->rhi_id, w, h};
}
struct Target {
  RenderResourceHandle<RenderTargetSpec> handle;
  Texture tex;
  ~Target() {
    if (handle.IsValid())
      backend().DeleteRenderTarget({handle});
    if (tex.handle.IsValid())
      backend().DeleteTexture({tex.handle});
  }
  void resize(int w, int h, RHIFilterMode filter = RHIFilterMode::Nearest) {
    if (w == tex.w && h == tex.h)
      return;
    if (handle.IsValid())
      backend().DeleteRenderTarget({handle});
    if (tex.handle.IsValid())
      backend().DeleteTexture({tex.handle});
    tex = createTexture(w, h, nullptr, filter);
    CreateRenderTargetCommand c;
    c.desc.color = tex.handle;
    handle = backend().CreateRenderTarget(c);
    if (!handle.IsValid())
      throw std::runtime_error("RHI render target creation failed");
  }
  void bind(bool clear = false) {
    RHICommand::SetRenderTarget c;
    c.target = handle;
    c.width = tex.w;
    c.height = tex.h;
    c.clearFlags = clear ? RHICommand::ClearColor : RHICommand::ClearNone;
    c.clearColor = {0, 0, 0, 1};
    backend().SetRenderTarget(c);
  }
};
inline std::vector<std::byte> bytes(const void *p, size_t size) {
  std::vector<std::byte> b(size);
  std::memcpy(b.data(), p, size);
  return b;
}
} // namespace LabRhi
