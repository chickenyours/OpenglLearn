#include <cassert>
#include <cstdint>

#include <glm/glm.hpp>

#include "Render/Public/RHICommand/FrameCommand/rhi_frame_encoder.h"

namespace {

struct FrameConstants {
    glm::mat4 viewProjection{1.0f};
    glm::vec4 cameraPosition{0.0f};
};

} // namespace

int main() {
    Render::RHIFrameCommandBufferPool pool;
    Render::RHIFrameEncoder frame(pool.threadAny_GetBuffer());

    Render::RHICommand::BeginFrame begin{};
    begin.frameIndex = 42;
    begin.framebufferWidth = 1920;
    begin.framebufferHeight = 1080;

    assert(frame.Begin(begin));
    assert(frame.SetViewport({0, 0, 1920, 1080, 0.0f, 1.0f}));

    Render::RenderResourceHandle<Render::UniformBufferSpec> frameUbo{1, 0};
    Render::RenderResourceHandle<Render::PipelineSpec> pipeline{2, 0};
    Render::RenderResourceHandle<Render::VertexBufferSpec> mesh{3, 0};

    assert(frame.UpdateUniformBuffer(frameUbo, FrameConstants{}));
    assert(frame.BindUniformBuffer(frameUbo, 0));
    assert(frame.BindPipeline(pipeline));
    assert(frame.BindMesh(mesh));
    assert(frame.DrawIndexed({.indexCount = 36, .instanceCount = 4}));
    assert(frame.End());
    assert(frame.IsEnded());
    assert(frame.GetCommandBuffer()->GetCommandCount() == 8);
    assert(!frame.DrawIndexed());
    return 0;
}
