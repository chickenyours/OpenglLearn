#include "Render/Public/Sprite/sprite_batch_geometry.h"
#include "Render/Public/Sprite/sprite_nine_slice.h"
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace Render::SpriteDetail;
namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }

void TestAffineHierarchy() {
    const glm::vec2 center{12,23}, camera{10,20}, view{10,5};
    const glm::vec4 region{.1f,.2f,.3f,.4f}, tint{1,.5f,.25f,.75f};
    // Hand-computed parallelogram: axisX=(4,0), axisY=(2,2). Its top
    // edge is shifted two world units right, which size/angle cannot express.
    const auto shear=MakeAffineQuad(center,{4,0},{2,2},region,tint,view,camera);
    const std::array<glm::vec2,4> expected={glm::vec2(-.1f,.4f),{.3f,.4f},{.5f,.8f},{.1f,.8f}};
    for(std::size_t i=0;i<4;++i) {
        Require(Near(shear[i].position.x,expected[i].x) && Near(shear[i].position.y,expected[i].y),
                "affine parallelogram lost shear or camera projection");
        Require(shear[i].color==tint,"affine tint changed");
    }
    auto rotate=[](glm::vec2 value,float angle) {
        return glm::vec2(value.x*std::cos(angle)-value.y*std::sin(angle),
                         value.x*std::sin(angle)+value.y*std::cos(angle));
    };
    const glm::vec2 parentScale{-1.4f,.7f}, childSize{.4f,.9f};
    constexpr float parentRotation=-.2f, childRotation=.7f;
    const auto axisX=rotate(parentScale*rotate({childSize.x,0},childRotation),parentRotation);
    const auto axisY=rotate(parentScale*rotate({0,childSize.y},childRotation),parentRotation);
    Require(std::abs(glm::dot(axisX,axisY))>.05f,"hierarchy test must exercise nonorthogonal axes");
    const auto quad=MakeAffineQuad(center,axisX,axisY,region,tint,view,camera,true,true,true);
    const std::array<glm::vec2,4> corners={glm::vec2(-.5f,-.5f),{.5f,-.5f},{.5f,.5f},{-.5f,.5f}};
    for(std::size_t i=0;i<4;++i) {
        const auto local=rotate(corners[i]*childSize,childRotation);
        const auto world=center+rotate(parentScale*local,parentRotation);
        const auto projected=(world-camera)/view;
        Require(Near(quad[i].position.x,projected.x) && Near(quad[i].position.y,projected.y),
                "affine quad differs from nested child rotation and nonuniform reflected parent scale");
        Require(quad[i].nearest==1,"affine nearest sampling flag lost");
    }
    Require(Near(quad[0].uv.x,.4f) && Near(quad[0].uv.y,.2f) &&
            Near(quad[2].uv.x,.1f) && Near(quad[2].uv.y,.6f),"reflection and UV flips became coupled");
    const auto a=quad[1].position-quad[0].position, b=quad[3].position-quad[0].position;
    Require(a.x*b.y-a.y*b.x<0,"reflected affine basis lost its winding");
    // With an orthogonal basis, the new helper must match ordinary sprites.
    const auto regular=MakeQuad(center,{4,2},.3f,region,tint,view,camera);
    const auto affine=MakeAffineQuad(center,rotate({4,0},.3f),rotate({0,2},.3f),region,tint,view,camera);
    for(std::size_t i=0;i<4;++i)
        Require(regular[i].position==affine[i].position && regular[i].uv==affine[i].uv,
                "ordinary sprite projection changed when routed through affine geometry");
}

void TestAffineValidation() {
    const float nan=std::numeric_limits<float>::quiet_NaN(), inf=std::numeric_limits<float>::infinity();
    for(const auto& axes:std::array<std::pair<glm::vec2,glm::vec2>,5>{
        std::pair{glm::vec2(0),glm::vec2(0,1)},{{1,0},{2,0}},{{nan,0},{0,1}},{{1,0},{inf,1}},{{1,0},{0,0}}}) {
        bool rejected=false;
        try { MakeAffineQuad({},axes.first,axes.second,{0,0,1,1},glm::vec4(1),{1,1}); }
        catch(const std::invalid_argument&) { rejected=true; }
        Require(rejected,"singular or nonfinite affine basis accepted");
    }
    auto rejected=[](glm::vec2 center,glm::vec2 view,glm::vec4 tint) {
        try { MakeAffineQuad(center,{1,0},{0,1},{0,0,1,1},tint,view); }
        catch(const std::invalid_argument&) { return true; }
        return false;
    };
    Require(rejected({inf,0},{1,1},glm::vec4(1)) && rejected({0,0},{1,1},{1,1,nan,1}),
            "nonfinite affine center or tint accepted");
    Require(rejected({0,0},{0,1},glm::vec4(1)),"invalid affine projection accepted");
    Require(rejected({std::numeric_limits<float>::max(),0},{.01f,1},glm::vec4(1)),
            "affine projection overflow produced nonfinite vertices");
    Require(ValidAffineBasis({1e30f,0},{0,1e30f}) && ValidAffineBasis({1e-30f,0},{0,1e-30f}),
            "finite nonzero determinant overflowed or underflowed during validation");
}

void CheckCoverage(const std::vector<NineSlicePatch>& patches, glm::vec2 target) {
    double area = 0;
    for (const auto& patch : patches) {
        Require(patch.size.x > 0 && patch.size.y > 0 && ValidRegion(patch.region), "nine-slice produced invalid geometry or UVs");
        const auto low = patch.center - patch.size * .5f, high = patch.center + patch.size * .5f;
        Require(low.x >= -target.x * .5f - .0001f && low.y >= -target.y * .5f - .0001f &&
                high.x <= target.x * .5f + .0001f && high.y <= target.y * .5f + .0001f,
                "nine-slice patch escaped target bounds");
        area += double(patch.size.x) * patch.size.y;
    }
    Require(std::abs(area - double(target.x) * target.y) < .0001, "nine-slice failed to cover target area exactly");
    for (size_t a = 0; a < patches.size(); ++a) for (size_t b = a + 1; b < patches.size(); ++b) {
        const auto overlap = (patches[a].size + patches[b].size) * .5f - glm::abs(patches[a].center - patches[b].center);
        Require(overlap.x < .0001f || overlap.y < .0001f, "nine-slice patches overlap");
    }
}

void TestRegionComposition() {
    const glm::vec4 parent{.125f, .25f, .5f, .5f}, child{.25f, .5f, .5f, .25f};
    const auto region = ComposeRegion(parent, child);
    Require(Near(region.x, .25f) && Near(region.y, .5f) && Near(region.z, .25f) && Near(region.w, .125f),
            "child UVs were not composed inside their atlas frame");
    Require(ComposeRegion(parent, {0, 0, 1, 1}) == parent, "full-frame region changed sprite UVs");
    const auto quad = MakeQuad({0, 0}, {4, 2}, 1.57079632679f, region, {1, 1, 1, 1}, {1, 1}, {}, true, true);
    Require(Near(quad[0].position.x, 1) && Near(quad[0].position.y, -2), "region changed geometric rotation");
    Require(Near(quad[0].uv.x, .5f) && Near(quad[0].uv.y, .5f) &&
            Near(quad[2].uv.x, .25f) && Near(quad[2].uv.y, .625f), "flipped subregion UVs escaped the selected region");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (glm::vec4 bad : {glm::vec4(-.01f, 0, 1, 1), {0, 0, 0, 1}, {0, 0, 1, -1},
                         {.75f, 0, .5f, 1}, {0, .75f, 1, .5f}, {nan, 0, 1, 1}, {0, 0, infinity, 1}}) {
        bool rejected = false;
        try { ComposeRegion(parent, bad); } catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "invalid subregion was accepted");
    }
}

void TestTiledNineSlice() {
    const auto patches = BuildNineSlice({9, 7}, {4, 4}, {1, 1, 1, 1});
    Require(patches.size() == 30, "tiled slice did not repeat center rows and columns");
    CheckCoverage(patches, {9, 7});
    const auto& bottomLeft = patches.front();
    Require(bottomLeft.size == glm::vec2(1) && bottomLeft.center == glm::vec2(-4, -3),
            "native corner dimensions were stretched");
    Require(bottomLeft.region == glm::vec4(0, .75f, .25f, .25f), "bottom border did not use bottom source pixels");
    const auto& topRight = patches.back();
    Require(topRight.size == glm::vec2(1) && topRight.region == glm::vec4(.75f, 0, .25f, .25f),
            "top-right corner orientation or dimensions changed");
    // Six columns: border, three full repeats, one partial repeat, border.
    const auto& bottomPartial = patches[4];
    Require(bottomPartial.size == glm::vec2(1) && Near(bottomPartial.region.x, .25f) && Near(bottomPartial.region.z, .25f),
            "last horizontal repeat stretched instead of cropping source UVs");
    const auto& centerPartial = patches[3 * 6 + 4];
    Require(centerPartial.size == glm::vec2(1) && centerPartial.region == glm::vec4(.25f, .5f, .25f, .25f),
            "partial center tile did not crop both axes from bottom-left repeat origin");
    Require(patches[1].size == glm::vec2(2, 1), "horizontal edge repeat lost native border thickness");
    Require(patches[6].size == glm::vec2(1, 2), "vertical edge repeat lost native border thickness");
}

void TestStretchedSmallAndBorderlessNineSlice() {
    const auto stretched = BuildNineSlice({9, 7}, {4, 4}, {1, 1, 1, 1}, false);
    Require(stretched.size() == 9, "stretched nine-slice should contain nine patches");
    CheckCoverage(stretched, {9, 7});
    Require(stretched[4].center == glm::vec2(0) && stretched[4].size == glm::vec2(7, 5) &&
            stretched[4].region == glm::vec4(.25f, .25f, .5f, .5f), "stretched center did not use its full source region");
    const auto small = BuildNineSlice({1, .75f}, {4, 4}, {1, .5f, 2, 1.5f});
    Require(small.size() == 4, "small target should omit collapsed middle rows and columns");
    CheckCoverage(small, {1, .75f});
    Require(Near(small.front().size.x, 1.f / 3) && Near(small.front().size.y, .1875f),
            "small target did not compress borders proportionally");
    Require(small.front().region == glm::vec4(0, .875f, .25f, .125f), "compressed border cropped native corner pixels");
    const auto borderless = BuildNineSlice({5, 3}, {2, 2}, {0, 0, 0, 0});
    Require(borderless.size() == 6, "zero borders should repeat the entire image");
    CheckCoverage(borderless, {5, 3});
    Require(borderless.back().size == glm::vec2(1, 1) && borderless.back().region == glm::vec4(0, .5f, .5f, .5f),
            "borderless partial tile was stretched");
    const auto borderlessStretch = BuildNineSlice({5, 3}, {2, 2}, {0, 0, 0, 0}, false);
    Require(borderlessStretch.size() == 1 && borderlessStretch.front().region == glm::vec4(0, 0, 1, 1),
            "borderless stretched sprite should be one full region");
    CheckCoverage(BuildNineSlice({2, 2}, {2, 2}, {1, 1, 1, 1}), {2, 2});
}

void TestNineSliceValidationAndCapacity() {
    auto rejected = [](glm::vec2 target, glm::vec2 native, glm::vec4 borders) {
        try { BuildNineSlice(target, native, borders); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    Require(rejected({0, 1}, {1, 1}, {}) && rejected({1, -1}, {1, 1}, {}), "invalid target size accepted");
    Require(rejected({1, 1}, {0, 1}, {}) && rejected({1, 1}, {1, infinity}, {}), "invalid native size accepted");
    Require(rejected({nan, 1}, {1, 1}, {}) && rejected({1, 1}, {1, 1}, {0, nan, 0, 0}), "nonfinite input accepted");
    Require(rejected({1, 1}, {1, 1}, {-1, 0, 0, 0}) && rejected({1, 1}, {1, 1}, {.8f, 0, .8f, 0}),
            "negative or oversized borders accepted");
    Require(rejected({3, 3}, {2, 2}, {1, 1, 1, 1}), "zero source center cannot fill an expanded target");
    Require(BuildNineSlice({64, 64}, {1, 1}, {}).size() == 4096, "exact patch capacity should be accepted");
    for (glm::vec2 target : {glm::vec2(65, 64), {4097, 1}, {std::numeric_limits<float>::max(), 1}}) {
        bool exceeded = false;
        try { BuildNineSlice(target, {1, 1}, {}); } catch (const std::length_error&) { exceeded = true; }
        Require(exceeded, "oversized repeat count did not fail before unbounded allocation");
    }
    Require(BuildNineSlice({100000, 100000}, {1, 1}, {}, false).size() == 1,
            "non-tiled slices should not be restricted by hypothetical repeat count");
}
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
        TestRegionComposition();
        TestAffineHierarchy();
        TestAffineValidation();
        TestTiledNineSlice();
        TestStretchedSmallAndBorderlessNineSlice();
        TestNineSliceValidationAndCapacity();
        std::cout << "Sprite atlas, affine hierarchy, region and nine-slice geometry tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
