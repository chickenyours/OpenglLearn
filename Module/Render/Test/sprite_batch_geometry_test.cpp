#include "Render/Public/Sprite/sprite_batch_geometry.h"
#include <iostream>
#include <stdexcept>

using namespace Render::SpriteDetail;
namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }
}

int main() {
    try {
        // Distinct corner colors expose orientation errors and edge extrusion.
        AtlasTile corners{2, 2, {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255}};
        AtlasTile tall{1, 9, std::vector<unsigned char>(9 * 4, 128)};
        const auto packed = PackAtlas({corners, tall}, 32, 2);
        const auto uv = packed.regions[0];
        const int x = int(std::round(uv.x * packed.width)), y = int(std::round(uv.y * packed.height));
        auto channel = [&](int px, int py, int c) { return packed.rgba[(size_t(py) * packed.width + px) * 4 + c]; };
        Require(channel(x, y, 0) == 255 && channel(x + 1, y, 1) == 255, "packing changed image row orientation");
        Require(channel(x - 2, y - 2, 0) == 255, "top-left border was not extruded");
        Require(channel(x + 3, y + 3, 0) == 255 && channel(x + 3, y + 3, 1) == 255, "bottom-right border was not extruded");
        Require(Near(packed.regions[1].w * packed.height, 9), "sorting changed input region order");
        const auto again = PackAtlas({corners, tall}, 32, 2);
        Require(packed.rgba == again.rgba, "atlas packing is not deterministic");
        bool rejected = false;
        try { PackAtlas({AtlasTile{1, 1, {255}}}); } catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "malformed pixel count accepted");
        rejected = false;
        try { PackAtlas({AtlasTile{29, 1, std::vector<unsigned char>(29 * 4)}}, 32, 2); }
        catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "oversized tile accepted");
        rejected = false;
        try { PackAtlas(std::vector<AtlasTile>(64, corners), 16, 2); }
        catch (const std::runtime_error&) { rejected = true; }
        Require(rejected, "atlas capacity overflow accepted");

        const glm::vec4 region{.1f, .2f, .3f, .4f}, tint{1, .5f, .25f, .75f};
        const auto quad = MakeQuad({12, 23}, {4, 2}, 0, region, tint, {10, 5}, {10, 20});
        Require(Near(quad[0].position.x, 0) && Near(quad[0].position.y, .4f), "camera translation/projection incorrect");
        Require(Near(quad[2].position.x, .4f) && Near(quad[2].position.y, .8f), "quad dimensions incorrect");
        Require(Near(quad[0].uv.y, .6f) && Near(quad[3].uv.y, .2f), "top-down PNG is upside down in y-up world");
        Require(quad[2].color == tint, "tint not preserved");
        const auto flipped = MakeQuad({0, 0}, {4, 2}, 0, region, tint, {1, 1}, {}, true, true, true);
        Require(Near(flipped[0].uv.x, .4f) && Near(flipped[0].uv.y, .2f), "flips did not reverse UVs");
        Require(flipped[0].nearest == 1, "bitmap sampling mode missing");
        const auto rotated = MakeQuad({0, 0}, {4, 2}, 1.57079632679f, region, tint, {1, 1});
        Require(Near(rotated[0].position.x, 1) && Near(rotated[0].position.y, -2), "rotation is not counterclockwise radians");
        rejected = false;
        try { MakeQuad({0, 0}, {1, 1}, 0, region, tint, {0, 1}); }
        catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "zero view extent accepted");
        std::cout << "Sprite atlas and geometry tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
