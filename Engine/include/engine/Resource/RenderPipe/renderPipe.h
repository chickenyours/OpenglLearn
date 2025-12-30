#pragma once

#include "engine/Resource/RenderPipe/render_contex.h"
#include "engine/Resource/RenderPipe/RenderItems/effect_render_item.h"

namespace Render{
    class RenderPipe{
        public:
            virtual int Init(const RenderPipeContex& cfg) = 0;
            virtual void SetConfig(const RenderPipeContex& cfg) = 0;
            virtual void RenderCall() = 0;
            virtual void AddParticalProcessProxyItem(const ParticleRenderProxy* proxy){}
            virtual void AddParticalItem(const BaseParticleDrawItem* proxy){}
    };
}