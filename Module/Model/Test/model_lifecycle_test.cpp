#include <iostream>
#include <stdexcept>
#include "Model/Public/model_module.h"
#include "Render/Private/rhi_device.h"

int main() {
    Render::RHIDevice device;
    Model::ModelModule module(device);
    const auto check = [](bool value, const char* message) {
        if (!value) throw std::runtime_error(message);
    };
    check(!module.Startup(), "Starting a model module with a stopped device must fail");
    check(!module.LastError().empty(), "Failed startup needs diagnostics");
    check(!module.IsStarted(), "Failed startup must not publish readiness");
    check(!module.Upload({}), "Upload must reject a stopped module without waiting for callbacks");
    check(!module.Load("Asset/Model/Rikka_Uniform"), "Load must reject a stopped module");
    module.Shutdown();
    module.Shutdown();
    std::cout << "Model lifecycle guards passed\n";
}
