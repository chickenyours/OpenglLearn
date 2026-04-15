#pragma once

#include "engine/ECS/Component/component.h"
#include "ResourceManager/Public/resource_handle.h"


namespace Render::RHI{
    class TextureAsset;
}



namespace Render2D::Component{
    class Sprite : public ECS::Component::Component<Sprite> {
        Resource::ResourceHandle<Render::RHI::TextureAsset> texture;
    };
};