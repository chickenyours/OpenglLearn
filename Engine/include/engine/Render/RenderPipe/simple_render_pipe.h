#pragma once

#include "code/Render/RenderPipe/renderpipe.h"

namespace Render{

class SimpleRenderPipe:public RenderPipe{
    public:
        virtual void LoadEntity(const std::vector<ECS::EntityID>& seq) override;
        virtual void Draw() override;
    private:
        ECS::EntityID cameraEntity = ECS::INVALID_ENTITY;
};


} // namespace Render