#pragma once

#include "engine/Resource/RenderPipe/renderPipe.h"

class IParticleSystem{
    protected:

    public:
        virtual void Update() = 0;
        virtual void Init() = 0;
};