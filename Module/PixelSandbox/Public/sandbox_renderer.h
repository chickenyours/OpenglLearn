#pragma once
#include "materials.h"
#include "sandbox_module.h"
#include "sandbox_palette.h"
#include "object_ptr.h"
#include <cstdint>
#include <memory>
#include <string>

namespace Render { class RHIDevice; }
namespace PixelSandbox {
struct ViewState {
    Material selected = Material::Sand;
    BrushTool tool = BrushTool::Paint;
    int radius = 5;
    PaletteGroup paletteGroup = PaletteGroup::All;
    int palettePage = 0;
    bool heatMap = false, replace = false;
    int hoverX = -1, hoverY = -1;
    std::string status;
};
struct UiHit {
    enum class Kind { None, Cell, Material, Pause, Step, Clear, Preset, Save, Load, HeatMap, Tool, Category, Page };
    Kind kind = Kind::None;
    int x = -1, y = -1, value = 0;
};
// Host-thread adapter: pump device.returnSystem while active. The persistent
// canvas texture uploads only on revision/view changes; HUD uses SpriteBatch2D.
class SandboxRenderer {
public:
    explicit SandboxRenderer(ObjectWeakPtr<Render::RHIDevice> device);
    ~SandboxRenderer();
    SandboxRenderer(const SandboxRenderer&) = delete;
    SandboxRenderer& operator=(const SandboxRenderer&) = delete;
    void Initialize(int columns, int rows);
    bool Ready() const;
    bool Busy() const;
    const std::string& Error() const;
    std::uint64_t CompletedFrames() const;
    bool Draw(const SandboxModule&, const ViewState&, int framebufferWidth, int framebufferHeight, bool present = true);
    void Shutdown();
    static UiHit HitTest(double x, double y, int framebufferWidth, int framebufferHeight, int columns, int rows,
                         PaletteGroup group = PaletteGroup::All, int page = 0);
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
