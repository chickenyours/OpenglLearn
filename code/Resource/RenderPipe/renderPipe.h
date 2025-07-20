#pragma once

#include "code/Resource/RenderPipe/render_contex.h"

namespace Render{
    class RenderPipe{
        virtual int Init(const RenderPipeContex& cfg) = 0;
        virtual void SetConfig(const RenderPipeContex& cfg) = 0;
        virtual void RenderCall() = 0;
    };
}