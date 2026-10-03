#include <iostream>
#include <stdexcept>
#include "Render/Private/rhi_device.h"
#include "Render/Public/Pipeline/material_render_pipeline.h"

int main() {
    Render::RHIDevice device;
    Render::MaterialRenderPipeline pipeline(device);
    if (pipeline.Resize(640, 480) || pipeline.LastError().empty())
        throw std::runtime_error("Resize must reject an uninitialized pipeline");
    if (pipeline.Initialize() || pipeline.LastError().find("Start RHIDevice") == std::string::npos)
        throw std::runtime_error("Initialize must reject a stopped device without waiting for callbacks");
    pipeline.Shutdown();
    pipeline.Shutdown();
    if (pipeline.DebugTextures().sceneColor.IsValid())
        throw std::runtime_error("Shutdown must invalidate borrowed targets");
    std::cout << "Material pipeline lifecycle tests passed\n";
}
