#pragma once

#include "engine/ECS/Component/component.h"
#include "engine/ECS/Core/Resource/resource_manager.h"
#include "engine/Resource/Texture/texture.h"

namespace ECS::Component{
    
    struct Renderer2D : Component<Renderer2D>
    {
        Resource::ResourceHandle<Resource::Texture2D> picture;
        bool LoadFromMetaDataImpl(const Json::Value& data, Log::StackLogErrorHandle errHandle = nullptr) {
            std::string texturePath;
            if(!Tool::JsonHelper::TryGetString(data,"texturePath",texturePath)){
                REPORT_STACK_ERROR(errHandle, "Renderer2D->LoadFromMetaData", "Failed to load texture file path.");
                return false;
            }
            picture = std::move(ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Texture2D>(
                FromConfig<Texture2D>(texturePath)
            ));
            return picture.get() != nullptr;
        }
    };
}