#pragma once

#include "code/ECS/Component/component.h"

namespace ECS::Component{
    struct Mesh : Component<Mesh>{
        bool LoadFromMetaDataImpl(const Json::Value& data, Log::StackLogErrorHandle errHandle = nullptr) {
            
        }
    };
}