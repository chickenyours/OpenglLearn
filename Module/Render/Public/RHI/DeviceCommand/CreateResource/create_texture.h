#pragma once

namespace Render::Device::Command{

    enum class Wrap{
        
    };

    enum class SampleMode{

    };

    struct RHICreateTextureCommand
    {
        size_t width;
        size_t height;
        Wrap wrapS;
        Wrap wrapT;
        SampleMode sampleMode;
        void* data = nullptr;
    };
    
}