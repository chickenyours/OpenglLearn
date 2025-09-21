#pragma once

#include "code/ECS/Component/component.h"

namespace ECS::Component{
    struct AABB_3D : public Component<AABB_3D>{
        float length = .0f; // x
        float height = .0f; // y
        float width  = .0f; // z

        bool LoadFromMetaDataImpl(const Json::Value& data, Log::StackLogErrorHandle errHandle = nullptr) {
            if(!Tool::JsonHelper::TryGetFloat(data, "length", length)){
                REPORT_STACK_ERROR(errHandle,"Component:AABB_3D->LoadFromMetaDataImpl","Cannot load length");
                return false;
            }
            if(!Tool::JsonHelper::TryGetFloat(data, "height", height)){
                REPORT_STACK_ERROR(errHandle,"Component:AABB_3D->LoadFromMetaDataImpl","Cannot load height");
                return false;
            }
            if(!Tool::JsonHelper::TryGetFloat(data, "width", width)){
                REPORT_STACK_ERROR(errHandle,"Component:AABB_3D->LoadFromMetaDataImpl","Cannot load width");
                return false;
            }
            return true;
        }

    };
}