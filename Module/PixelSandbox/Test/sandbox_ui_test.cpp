#include "PixelSandbox/Public/sandbox_renderer.h"
#include <iostream>
#include <stdexcept>

using namespace PixelSandbox;
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

int main() {
    try {
        // Exercise the same coordinates and catalog-derived entries as the HUD.
        for (int group = 0; group < int(PaletteGroup::Count); ++group) {
            const auto category = PaletteGroup(group);
            for (int page = 0; page < PalettePages(category); ++page) {
                const auto entries = PaletteMaterials(category, page);
                for (std::size_t i = 0; i < entries.size(); ++i) {
                    const double x = 1000 + (i % 2) * 124, y = 168 + (i / 2) * 34;
                    const auto hit = SandboxRenderer::HitTest(x, y, 1280, 800, 320, 200, category, page);
                    Check(hit.kind == UiHit::Kind::Material && hit.value == int(entries[i]), "material click selects visible item");
                    const auto scaled = SandboxRenderer::HitTest(x * 2, y * 2, 2560, 1600, 320, 200, category, page);
                    Check(scaled.kind == hit.kind && scaled.value == hit.value, "high DPI click preserves selection");
                    const auto letterbox = SandboxRenderer::HitTest(x + 160, y, 1600, 800, 320, 200, category, page);
                    Check(letterbox.kind == hit.kind && letterbox.value == hit.value, "wide window letterbox preserves selection");
                }
            }
            const auto tab = SandboxRenderer::HitTest(1000 + (group % 3) * 82, 108 + (group / 3) * 27, 1280, 800, 320, 200);
            Check(tab.kind == UiHit::Kind::Category && tab.value == group, "category tab hit");
        }
        auto hit = SandboxRenderer::HitTest(1180, 520, 1280, 800, 320, 200);
        Check(hit.kind == UiHit::Kind::Page && hit.value == 1, "next-page control hit");
        hit = SandboxRenderer::HitTest(510, 88, 1280, 800, 320, 200);
        Check(hit.kind == UiHit::Kind::Preset && hit.value == int(Preset::Elements), "elements preset control hit");
        hit = SandboxRenderer::HitTest(28, 126, 1280, 800, 320, 200);
        Check(hit.kind == UiHit::Kind::Cell, "palette additions preserve canvas selection");
        std::cout << "Sandbox palette and input layout PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
