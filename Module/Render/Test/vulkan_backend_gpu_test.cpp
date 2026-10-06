#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/realtime_gi_shaders.h"
#include <array>
#include <glad/glad.h>
#include <iostream>
using namespace Render;
using namespace Render::PipelineDetail;
void Require(bool x, const char *s) {
  if (!x)
    throw std::runtime_error(s);
}
template <class F> void OnThread(RHIDevice &d, F f) {
  bool done = false;
  d.async_ExecuteCode(f, [&] { done = true; });
  while (!done) {
    d.returnSystem.DrainCallbacks();
    if (!done)
      d.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
  }
}
int main(int argc, char **argv) {
  try {
    Require(RegisterVulkanBackend(), "Vulkan backend was not built");
    Require(glfwInit(), "GLFW initialization");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    ApplicationWindow::Window window(
        {64, 32, "Vulkan RHI contract", false, BackendType::Vulkan});
    Require(window.Activate(), "Vulkan window");
    auto context = *window.GetRenderContextAsVulkan();
    context.validation = argc > 1 && std::string(argv[1]) == "--validation";
    RHIDevice device;
    device.Run(BackendType::Vulkan, context);
    {
      ResourceBuilder build(device);
      auto mesh = build.FullscreenMesh();
      auto u = build.Uniform(16);
      auto shader = build.FullscreenPipeline(R"(#version 450 core
layout(location=0) in vec2 p;layout(location=1) in vec2 uv;
void main(){gl_Position=vec4(p,-.5,1);}
)",
                                             R"(#version 450 core
layout(std140,binding=0) uniform Color {vec4 color;};layout(location=0) out vec4 o;
void main(){o=color;}
)",
                                             true);
      auto color = build.Texture(64, 32, RHITextureFormat::RGBA8);
      auto depth = build.Texture(64, 32, RHITextureFormat::Depth32F);
      auto target = build.Target(color, depth);
      auto mc = build.Texture(64, 32, RHITextureFormat::RGBA8, 4);
      auto md = build.Texture(64, 32, RHITextureFormat::Depth32F, 4);
      auto mt = build.Target(mc, md);
      float drawAlpha = 1;
      glm::vec4 clearColor(0);
      auto draw = [&](bool msaa, bool present) {
        auto e = device.BeginFrame({0, 64, 32});
        e.SetRenderTarget({msaa ? mt : target, 64, 32,
                           RHICommand::ClearColor | RHICommand::ClearDepth,
                           clearColor});
        e.BindMesh(mesh);
        e.BindPipeline(shader);
        e.BindUniformBuffer(u, 0);
        e.SetViewport({0, 0, 32, 32});
        e.UpdateUniformBuffer(u, glm::vec4(1, 0, 0, drawAlpha));
        e.DrawIndexed({3});
        e.SetViewport({32, 0, 32, 32});
        e.UpdateUniformBuffer(u, glm::vec4(0, 1, 0, drawAlpha));
        e.DrawIndexed({3});
        if (msaa)
          e.ResolveRenderTarget({mt, target, true, true});
        e.End(present);
        bool done = false;
        device.async_SubmitFrameCommands(e.GetCommandBuffer(),
                                         [&] { done = true; });
        while (!done) {
          device.returnSystem.DrainCallbacks();
          if (!done)
            device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
        }
      };
      for (bool msaa : {false, true}) {
        draw(msaa, false);
        OnThread(device, [&] {
          auto c = device.BackendDiagnostics()->ReadTexture(color);
          auto z = device.BackendDiagnostics()->ReadTexture(depth);
          Require(c.size() == 64 * 32 * 4 && z.size() == 64 * 32,
                  "readback dimensions");
          Require(c[(16 * 64 + 16) * 4] > .99 &&
                      c[(16 * 64 + 48) * 4 + 1] > .99,
                  "per-draw uniform snapshots");
          Require(c[(16 * 64 + 16) * 4 + 1] < .01 &&
                      c[(16 * 64 + 48) * 4] < .01,
                  "color/viewport contracts");
          Require(std::abs(z[16 * 64 + 32] - .25) < 1e-5,
                  "OpenGL projection depth conversion / MSAA resolve");
        });
      }
      // Repeated bindings/identical updates can share descriptor and UBO
      // snapshots, but offsets, later mutations and texture changes cannot.
      auto rangedUniform = build.Uniform(512);
      auto whiteUniform = build.Uniform(16);
      auto sampledShader = build.FullscreenPipeline(
          R"(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);}
)",
          R"(#version 450 core
layout(std140,binding=0) uniform Color {vec4 color;};
layout(binding=1) uniform sampler2D source;
layout(location=0) out vec4 o;void main(){o=color*texelFetch(source,ivec2(0),0);}
)");
      const auto texel = [&](std::array<uint8_t, 4> rgba) {
        CreateRHITextureSpec spec;
        spec.width = spec.height = 1;
        spec.mipmaps = false;
        spec.textureDataStoreType = spec.textureUseType = RHITextureFormat::RGBA8;
        spec.data = rgba.data();
        return build.Create<RHITextureSpec>(
            [&](auto cb) { device.async_CreateTexture(spec, cb); });
      };
      auto white = texel({255, 255, 255, 255});
      auto cyan = texel({0, 255, 255, 255});
      auto magenta = texel({255, 0, 255, 255});
      for (unsigned frame = 0; frame < 2; ++frame) {
        const glm::vec4 first = frame == 0 ? glm::vec4(1, 0, 0, 1)
                                           : glm::vec4(0, 1, 0, 1);
        const glm::vec4 offset = frame == 0 ? glm::vec4(0, 0, 1, 1)
                                            : glm::vec4(1, 0, 0, 1);
        const glm::vec4 changed = frame == 0 ? glm::vec4(0, 1, 0, 1)
                                             : glm::vec4(0, 0, 1, 1);
        const std::array<glm::vec4, 8> expected{
            first, offset, first, first, changed, offset,
            glm::vec4(0, 1, 1, 1), glm::vec4(1, 0, 1, 1)};
        auto e = device.BeginFrame({frame, 64, 32});
        e.SetRenderTarget({target, 64, 32, RHICommand::ClearColor,
                           glm::vec4(0)});
        e.BindMesh(mesh);
        e.BindPipeline(sampledShader);
        e.BindTexture(white, 1);
        e.UpdateUniformBuffer(rangedUniform, first);
        e.UpdateUniformBuffer(rangedUniform, offset, 256);
        e.UpdateUniformBuffer(whiteUniform, glm::vec4(1));
        for (unsigned slice = 0; slice < expected.size(); ++slice) {
          e.SetViewport({int32_t(slice * 8), 0, 8, 32});
          if (slice == 3)
            e.UpdateUniformBuffer(rangedUniform, first); // unchanged bytes
          if (slice == 4)
            e.UpdateUniformBuffer(rangedUniform, changed);
          e.BindUniformBuffer(slice < 6 ? rangedUniform : whiteUniform, 0,
                              slice == 1 || slice == 5 ? 256 : 0, 16);
          if (slice >= 6)
            e.BindTexture(slice == 6 ? cyan : magenta, 1);
          e.DrawIndexed({3});
        }
        e.End(false);
        bool done = false;
        device.async_SubmitFrameCommands(e.GetCommandBuffer(),
                                         [&] { done = true; });
        while (!done) {
          device.returnSystem.DrainCallbacks();
          if (!done)
            device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
        }
        OnThread(device, [&] {
          auto pixels = device.BackendDiagnostics()->ReadTexture(color);
          for (size_t slice = 0; slice < expected.size(); ++slice)
            for (size_t channel = 0; channel < 4; ++channel)
              Require(std::abs(pixels[(16 * 64 + slice * 8 + 4) * 4 + channel] -
                                   expected[slice][channel]) < .01f,
                      "Reusable descriptors preserve UBO versions/ranges and texture bindings");
        });
      }
      PipelineSpec blendSpec;
      OnThread(device, [&] {
        blendSpec = *device.GetResourcePool().PipelineTable.Get(shader);
      });
      blendSpec.blendEnable = true;
      blendSpec.blendMode = BlendMode::Alpha;
      auto opaqueShader = shader;
      shader = build.Create<PipelineSpec>(
          [&](auto cb) { device.async_CreatePipeline({blendSpec}, cb); });
      drawAlpha = .5f;
      clearColor = {0, 0, 1, 1};
      draw(false, false);
      OnThread(device, [&] {
        auto c = device.BackendDiagnostics()->ReadTexture(color);
        auto at = (16 * 64 + 16) * 4;
        Require(std::abs(c[at] - .5f) < .01f &&
                    std::abs(c[at + 2] - .5f) < .01f &&
                    std::abs(c[at + 3] - .75f) < .01f,
                "OpenGL-compatible color/alpha blending");
      });
      drawAlpha = 1;
      clearColor = glm::vec4(0);
      shader = opaqueShader;
      CreateGraphicShaderDesc stages;
      stages.vertexShaderSource =
          build.Source(ShaderSourceType::Vertex, R"(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,-.5,1);})");
      stages.fragmentShaderSource =
          build.Source(ShaderSourceType::Fragment, R"(#version 450 core
layout(std140,binding=0) uniform Color {vec4 color;};layout(location=0) out vec4 o;void main(){o=color;})");
      stages.geometryShaderSource =
          build.Source(ShaderSourceType::Geometry, R"(#version 450 core
layout(triangles) in;layout(triangle_strip,max_vertices=3) out;
void main(){for(int i=0;i<3;++i){gl_Position=gl_in[i].gl_Position;EmitVertex();}EndPrimitive();})");
      auto program = build.Create<ShaderProgramSpec>(
          [&](auto cb) { device.async_CreateGraphicShader(stages, cb); });
      blendSpec.shaderProgram = program;
      blendSpec.blendEnable = false;
      shader = build.Create<PipelineSpec>(
          [&](auto cb) { device.async_CreatePipeline({blendSpec}, cb); });
      draw(false, false);
      OnThread(device, [&] {
        auto z = device.BackendDiagnostics()->ReadTexture(depth);
        Require(std::abs(z[16 * 64 + 32] - .25) < 1e-5,
                "Geometry output must convert projection depth exactly once");
      });
      shader = opaqueShader;
      for(unsigned frame=0;frame<200;++frame){glfwPollEvents();draw(frame%2==0,true);}
      OnThread(device,[&]{Require(!device.BackendDiagnostics()->ValidationErrorCount(),"Continuous presentation semaphore reuse failed");});
      ProbeTriangle t;
      t.a = {-1, -1, 0};
      t.b = {1, -1, 0};
      t.c = {0, 1, 0};
      auto scene = BuildRealtimeGiScene(std::span(&t, 1));
      Require(bool(scene), "ray scene");
      CreateRHITextureSpec td;
      td.width = scene->Width();
      td.height = scene->Height();
      td.mipmaps = false;
      td.textureDataStoreType = td.textureUseType = RHITextureFormat::RGBA32F;
      td.data = scene->Pixels().data();
      auto geom = build.Create<RHITextureSpec>(
          [&](auto cb) { device.async_CreateTexture(td, cb); });
      auto ru = build.Uniform(sizeof(RealtimeGiConstants));
      RealtimeGiConstants rc;
      rc.geometry = {int(scene->NodeCount() * 2), 1024, int(scene->NodeCount()),
                     1};
      auto rays = build.FullscreenPipeline(R"(#version 450 core
layout(location=0) in vec2 p;layout(location=1) in vec2 uv;void main(){gl_Position=vec4(p,0,1);}
)",
                                           std::string("#version 450 core\n") +
                                               RealtimeGiQueryGLSL() +
                                               RealtimeGiGeometryGLSL() + R"(
layout(location=0) out vec4 o;void main(){float d=10;vec3 n;int t;bool front;
bool hit=RtIntersect(vec3(0,0,2),vec3(0,0,-1),d,n,t,front);o=vec4(hit?d:0,float(t),front?1:0,1);}
)");
      auto rayColor = build.Texture(8, 8, RHITextureFormat::RGBA32F);
      auto rayTarget = build.Target(rayColor, {});
      auto e = device.BeginFrame({0, 64, 32});
      e.SetRenderTarget({rayTarget, 8, 8});
      e.BindPipeline(rays);
      e.BindMesh(mesh);
      e.BindTexture(geom, 0);
      e.UpdateUniformBuffer(ru, rc);
      e.BindUniformBuffer(ru, 15);
      e.DrawIndexed({3});
      e.End(true);
      bool done = false;
      device.async_SubmitFrameCommands(e.GetCommandBuffer(),
                                       [&] { done = true; });
      while (!done) {
        device.returnSystem.DrainCallbacks();
        if (!done)
          device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
      }
      OnThread(device, [&] {
        auto pixels = device.BackendDiagnostics()->ReadTexture(rayColor);
        Require(std::abs(pixels[0] - 2) < 1e-5 && pixels[1] == 0 &&
                    pixels[2] == 1,
                "native ray query closest triangle/distance/front");
        Require(!device.BackendDiagnostics()->ValidationErrorCount(),
                "Vulkan validation error");
        std::cout << "Native ray query available="
                  << device.BackendDiagnostics()->HardwareRayTracingAvailable()
                  << "\n";
      });
      glfwSetWindowSize(window.GetNativeWindow(), 96, 48);
      glfwPollEvents();
      auto resized = device.BeginFrame({1, 96, 48});
      resized.End(true);
      bool resizedDone = false;
      device.async_SubmitFrameCommands(resized.GetCommandBuffer(),
                                       [&] { resizedDone = true; });
      while (!resizedDone) {
        device.returnSystem.DrainCallbacks();
        if (!resizedDone)
          device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
      }
      OnThread(device, [&] {
        Require(device.BackendDiagnostics()->ReadWindow().size() == 96 * 48 * 4,
                "Window resize/readback contract");
        Require(!device.BackendDiagnostics()->ValidationErrorCount(),
                "Vulkan validation error after resize");
      });
      std::cout << "Vulkan RHI contract passed: indexed draw, UBO snapshots, "
                   "target/depth, alpha blend, geometry shader, resize, 4xMSAA "
                   "resolve, presentation, ray queries\n";
    }
    device.StopAndRelease();
    window.Shutdown();
    glfwTerminate();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
