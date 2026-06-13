#pragma once
#include <string>
#include "texture_data.h"

namespace Render::Resource
{
    class TextureLoader
    {
    public:
        static TextureData LoadFromFile(const std::string& path);
        static TextureData LoadFromMemory(const void* data, size_t size);
    };
}