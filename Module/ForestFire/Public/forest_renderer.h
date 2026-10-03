#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "object_ptr.h"

namespace Render { class RHIDevice; }

namespace ForestFire {

class ForestModule;

// Optional presentation adapter. Simulation components never own GPU handles.
// Pump RHIDevice::returnSystem on the host thread while this adapter is active.
class ForestRenderer {
public:
    explicit ForestRenderer(ObjectWeakPtr<Render::RHIDevice> device);
    ~ForestRenderer();
    ForestRenderer(const ForestRenderer&) = delete;
    ForestRenderer& operator=(const ForestRenderer&) = delete;

    void Initialize();
    bool Ready() const;
    bool Busy() const;
    const std::string& Error() const;
    std::uint64_t CompletedFrames() const;

    // One dynamic mesh and one RHI draw. A frame is submitted only after its
    // mesh upload has completed; Busy() bounds the queue to one frame.
    bool Draw(const ForestModule& forest, int framebufferWidth, int framebufferHeight, bool present = true);
    void Shutdown();

    // Coordinates are framebuffer pixels, with the origin at the top left.
    // Returns no cell for the HUD or the aspect-preserving letterbox margins.
    static std::optional<std::pair<std::uint32_t, std::uint32_t>> PickCell(
        double framebufferX, double framebufferY,
        int framebufferWidth, int framebufferHeight,
        std::uint32_t columns, std::uint32_t rows);

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace ForestFire
