#pragma once

#include "engine/ECS/System/system.h"
#include "Render/Public/Pipeline/render_pipeline.h"

namespace Render::System {

class RenderExtractBeginSystem final : public ECS::System::System {
public:
    RenderExtractBeginSystem()
        : ECS::System::System("RenderExtractBeginSystem", ECS::System::Phase::RenderExtract) {}
    void OnTick() override {
        auto* context = GetContext();
        auto* world = context ? context->GetService<RenderWorld>() : nullptr;
        if(world) world->BeginExtract(context->frameIndex);
    }
};

class RenderPublishSystem final : public ECS::System::System {
public:
    RenderPublishSystem()
        : ECS::System::System("RenderPublishSystem", ECS::System::Phase::RenderExtract) {}
    void OnTick() override {
        auto* context = GetContext();
        auto* world = context ? context->GetService<RenderWorld>() : nullptr;
        if(world) world->Publish();
    }
};

class RenderPipelineSystem final : public ECS::System::System {
public:
    RenderPipelineSystem()
        : ECS::System::System("RenderPipelineSystem", ECS::System::Phase::RenderSubmit) {}

    void OnTick() override {
        auto* context = GetContext();
        if(context == nullptr) return;
        auto* world = context->GetService<RenderWorld>();
        auto* service = context->GetService<RenderFrameService>();
        if(world == nullptr || service == nullptr || service->encoder == nullptr) return;

        RenderFrame frame;
        if(world->Consume(frame)) pipeline_.Record(*service->encoder, frame, service->view);
    }

private:
    ForwardRenderPipeline pipeline_;
};

} // namespace Render::System
