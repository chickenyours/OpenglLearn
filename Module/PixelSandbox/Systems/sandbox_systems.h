#pragma once
#include "PixelSandbox/Public/sandbox_module.h"
#include "PixelSandbox/Public/sandbox_components.h"
namespace PixelSandbox {
class FieldSystem final : public ECS::System::System {
public:
    FieldSystem() : System("PixelSandbox.Fields", ECS::System::Phase::PreUpdate) { Writes<GridStorage>(); Writes<ChunkRegion>(); }
    void OnTick() override { GetContext()->GetService<SandboxModule>()->StepFields(); }
};
class HeatSystem final : public ECS::System::System {
public:
    HeatSystem() : System("PixelSandbox.Heat", ECS::System::Phase::Update) { Writes<GridStorage>(); Writes<ChunkRegion>(); }
    void OnTick() override { GetContext()->GetService<SandboxModule>()->StepHeat(); }
};
class MaterialSystem final : public ECS::System::System {
public:
    MaterialSystem() : System("PixelSandbox.Materials", ECS::System::Phase::Update) { Writes<GridStorage>(); Writes<ChunkRegion>(); }
    void OnTick() override { GetContext()->GetService<SandboxModule>()->StepMaterials(); }
};
} // namespace PixelSandbox
