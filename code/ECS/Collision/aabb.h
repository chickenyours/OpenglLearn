#pragma once

#include "code/ECS/Component/component.h"

namespace ECS::Component{
    struct AABB_3D : public Component<AABB_3D>{
        float center[3];
        float axisLength[3];
    };
}