#pragma once

#include "engine/Resource/RenderPipe/RenderItems/effect_render_item.h"


class IParticleSystem{
    protected:

    public:
        virtual void UpdateLogic() = 0;
        virtual void Init() = 0;
        virtual void CollectComputeTasks(std::vector<ParticleComputeTask>& out) = 0;
};