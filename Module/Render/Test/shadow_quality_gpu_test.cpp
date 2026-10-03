#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Public/Material/pbr_material.h"

namespace {
using namespace Render;
using namespace std::chrono_literals;

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template <typename T> struct Completion { T value{}; bool done = false; };

// Test geometry is deliberately independent of shadow sampling. The depth
// pass rasterizes actual plane/box triangles; the diagnostic pass calls the
// same GLSL function used by PBR, with no lighting or display transform.
struct alignas(16) GeometryConstants {
    glm::mat4 rasterViewProjection{1};
    glm::mat4 model{1};
    glm::vec4 normal{0, 1, 0, 0};
    // x: explicit cascade, or -1 for the complete directional lookup.
    // y: remove derivatives for the receiver-plane negative control.
    glm::vec4 options{0};
};
static_assert(sizeof(GeometryConstants) == 160);

std::string GeometryBlock() {
    return R"GLSL(
layout(std140, binding = 7) uniform TestGeometryData {
    mat4 rasterViewProjection;
    mat4 geometryModel;
    vec4 geometryNormal;
    vec4 testOptions;
};
)GLSL";
}
std::string GeometryVertexShader() {
    return std::string(R"GLSL(#version 450 core
layout(location = 0) in vec3 position;
out vec3 worldPosition;
)GLSL") + GeometryBlock() + R"GLSL(
void main() {
    worldPosition = (geometryModel * vec4(position, 1)).xyz;
    gl_Position = rasterViewProjection * vec4(worldPosition, 1);
}
)GLSL";
}
std::string VisibilityShader() {
    return std::string(R"GLSL(#version 450 core
layout(binding = 12) uniform sampler2D shadowAtlas;
in vec3 worldPosition;
layout(location = 0) out vec4 outColor;
)GLSL") + GeometryBlock() + Material::PbrSceneEffectsGLSL() +
        Material::PbrDirectionalShadowGLSL() + R"GLSL(
void main() {
    vec3 dx = dFdx(worldPosition), dy = dFdy(worldPosition);
    if (testOptions.y > 0.5) { dx = vec3(0); dy = vec3(0); }
    vec3 N = normalize(geometryNormal.xyz);
    float visibility = testOptions.x < 0.0
        ? DirectionalVisibility(worldPosition, N, dx, dy)
        : CascadeVisibility(int(testOptions.x), worldPosition, N, dx, dy);
    outColor = vec4(vec3(visibility), 1);
}
)GLSL";
}

glm::mat4 PlaneModel(glm::vec3 center, glm::vec3 halfU, glm::vec3 halfV) {
    glm::mat4 result(1);
    result[0] = glm::vec4(halfU, 0);
    result[1] = glm::vec4(halfV, 0);
    result[2] = glm::vec4(glm::normalize(glm::cross(halfU, halfV)), 0);
    result[3] = glm::vec4(center, 1);
    return result;
}
GeometryConstants Receiver(glm::mat4 model, glm::vec3 normal, int cascade = 0) {
    GeometryConstants result;
    result.model = model;
    // Orthographic camera aligned to this receiver patch. Its two triangles
    // really draw the visibility image and provide world-space derivatives.
    result.rasterViewProjection = glm::inverse(model);
    result.normal = glm::vec4(glm::normalize(normal), 0);
    result.options.x = static_cast<float>(cascade);
    return result;
}

struct Images {
    std::shared_ptr<const void> owner;
    RenderResourceHandle<RHITextureSpec> atlas, color;
    RenderResourceHandle<RenderTargetSpec> atlasTarget, colorTarget;
    std::uint32_t width = 0, height = 0, atlasSize = 0;
};
struct DepthDraw { glm::mat4 model{1}; bool box = false; };
struct Image {
    std::uint32_t width = 0, height = 0;
    std::vector<float> rgba;
    float At(std::uint32_t x, std::uint32_t y) const { return rgba[(y * width + x) * 4]; }
};

class ShadowFixture {
public:
    explicit ShadowFixture(OpenglBackendContext context) {
        device_.Run(BackendType::Opengl, std::move(context));
    }
    ~ShadowFixture() { Shutdown(); }
    void Initialize() {
        PipelineDetail::ResourceBuilder builder(device_);
        const auto vertex = builder.Source(ShaderSourceType::Vertex, GeometryVertexShader());
        const auto pipeline = [&](const std::string& fragment, bool depth) {
            CreateGraphicShaderDesc desc;
            desc.vertexShaderSource = vertex;
            desc.fragmentShaderSource = builder.Source(ShaderSourceType::Fragment, fragment);
            auto program = builder.Create<ShaderProgramSpec>([&](auto cb) { device_.async_CreateGraphicShader(desc, cb); });
            PipelineSpec spec;
            spec.shaderProgram = program;
            spec.expectVertexLayout = {{VertexFieldType::Vec3}};
            spec.cullMode = CullMode::None;
            spec.depthTest = spec.depthWrite = depth;
            spec.depthCompare = CompareOp::Less;
            return builder.Create<PipelineSpec>([&](auto cb) { device_.async_CreatePipeline({spec}, cb); });
        };
        depthPipeline_ = pipeline("#version 450 core\nvoid main() {}\n", true);
        visibilityPipeline_ = pipeline(VisibilityShader(), false);
        const std::vector<glm::vec3> plane{{-1,-1,0}, {1,-1,0}, {1,1,0}, {-1,1,0}};
        const std::vector<std::uint32_t> planeIndices{0,1,2, 0,2,3};
        const std::vector<glm::vec3> box{
            {-.5f,0,-.5f}, {.5f,0,-.5f}, {.5f,1,-.5f}, {-.5f,1,-.5f},
            {-.5f,0,.5f}, {.5f,0,.5f}, {.5f,1,.5f}, {-.5f,1,.5f}};
        const std::vector<std::uint32_t> boxIndices{
            0,2,1, 0,3,2, 4,5,6, 4,6,7, 0,4,7, 0,7,3,
            1,2,6, 1,6,5, 0,1,5, 0,5,4, 3,7,6, 3,6,2};
        const auto mesh = [&](const auto& vertices, const auto& indices) {
            CreateMeshBufferDesc desc;
            desc.vertexLayout = {{VertexFieldType::Vec3}};
            desc.vertexData = vertices.data(); desc.vertexByteSize = vertices.size() * sizeof(glm::vec3);
            desc.vertexCount = static_cast<std::uint32_t>(vertices.size());
            desc.indexData = indices.data(); desc.indexByteSize = indices.size() * sizeof(std::uint32_t);
            desc.indexCount = static_cast<std::uint32_t>(indices.size());
            return builder.Create<VertexBufferSpec>([&](auto cb) { device_.async_CreateMeshBuffer(desc, cb); });
        };
        plane_ = mesh(plane, planeIndices);
        box_ = mesh(box, boxIndices);
        sceneUniform_ = builder.Uniform(sizeof(SceneEffectsConstants));
        geometryUniform_ = builder.Uniform(sizeof(GeometryConstants));
        owner_ = builder.Finish();
    }
    void Shutdown() {
        if (!device_.IsRunning()) return;
        owner_.reset();
        device_.StopAndRelease();
    }
    Images Allocate(std::uint32_t width, std::uint32_t height, std::uint32_t atlasSize,
                    const std::vector<float>& depth = {}) {
        Check(depth.empty() || depth.size() == std::size_t(atlasSize) * atlasSize, "Invalid synthetic atlas size");
        PipelineDetail::ResourceBuilder builder(device_);
        Images result;
        result.width = width; result.height = height; result.atlasSize = atlasSize;
        CreateRHITextureSpec desc;
        desc.width = desc.height = atlasSize;
        desc.textureDataStoreType = desc.textureUseType = RHITextureFormat::Depth32F;
        desc.filterMode = RHIFilterMode::Nearest;
        desc.addressMode = RHIAddressMode::ClampToEdge;
        desc.mipmaps = false;
        desc.data = depth.empty() ? nullptr : depth.data();
        result.atlas = builder.Create<RHITextureSpec>([&](auto cb) { device_.async_CreateTexture(desc, cb); });
        result.atlasTarget = builder.Target({}, result.atlas);
        result.color = builder.Texture(width, height, RHITextureFormat::RGBA16F);
        result.colorTarget = builder.Target(result.color, {});
        result.owner = builder.Finish();
        return result;
    }
    Image Render(const Images& images, const SceneEffectsConstants& scene, const GeometryConstants& receiver,
                 const std::vector<DepthDraw>& draws = {}) {
        RHICommand::BeginFrame begin;
        begin.frameIndex = ++frameIndex_;
        begin.framebufferWidth = begin.framebufferHeight = 64;
        auto encoder = device_.BeginFrame(begin);
        try {
            Check(encoder.KeepAlive(owner_) && encoder.KeepAlive(images.owner), "Cannot retain shadow resources");
            if (!draws.empty()) {
                RHICommand::SetRenderTarget target;
                target.target = images.atlasTarget; target.width = target.height = images.atlasSize;
                target.clearFlags = RHICommand::ClearDepth;
                Check(encoder.SetRenderTarget(target) && encoder.BindPipeline(depthPipeline_), "Cannot select shadow atlas");
                for (std::uint32_t cascade = 0; cascade < 4; ++cascade) {
                    RHICommand::SetViewport viewport;
                    viewport.width = viewport.height = images.atlasSize / 2;
                    viewport.x = static_cast<int>(cascade % 2 * viewport.width);
                    viewport.y = static_cast<int>(cascade / 2 * viewport.height);
                    Check(encoder.SetViewport(viewport), "Cannot select atlas tile");
                    for (const auto& draw : draws) {
                        GeometryConstants geometry;
                        geometry.model = draw.model;
                        geometry.rasterViewProjection = scene.lightViewProjection[cascade];
                        Check(encoder.UpdateUniformBuffer(geometryUniform_, geometry) &&
                              encoder.BindUniformBuffer(geometryUniform_, 7) &&
                              encoder.BindMesh(draw.box ? box_ : plane_) &&
                              encoder.DrawIndexed(RHICommand::DrawIndexed{draw.box ? 36u : 6u}),
                              "Cannot draw shadow caster/receiver geometry");
                    }
                }
            }
            RHICommand::SetRenderTarget target;
            target.target = images.colorTarget; target.width = images.width; target.height = images.height;
            target.clearFlags = RHICommand::ClearColor;
            target.clearColor = glm::vec4(-1);
            Check(encoder.SetRenderTarget(target), "Cannot select visibility image");
            const auto* bytes = reinterpret_cast<const std::byte*>(&scene);
            for (std::uint32_t offset = 0; offset < sizeof(scene); offset += RHICommand::MaxInlineUniformBytes) {
                const auto count = std::min<std::uint32_t>(RHICommand::MaxInlineUniformBytes, sizeof(scene) - offset);
                Check(encoder.UpdateUniformBufferBytes(sceneUniform_, bytes + offset, count, offset), "Cannot upload scene effects");
            }
            Check(encoder.BindUniformBuffer(sceneUniform_, SceneEffectsBinding) &&
                  encoder.UpdateUniformBuffer(geometryUniform_, receiver) && encoder.BindUniformBuffer(geometryUniform_, 7) &&
                  encoder.BindTexture(images.atlas, ShadowAtlasTextureSlot) && encoder.BindPipeline(visibilityPipeline_) &&
                  encoder.BindMesh(plane_) && encoder.DrawIndexed(RHICommand::DrawIndexed{6}), "Cannot draw production shadow visibility");
            Check(encoder.End(false), "Cannot end shadow frame");
            auto completed = std::make_shared<Completion<bool>>();
            Check(device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(), [completed] { completed->done = true; }),
                  "Cannot submit shadow frame");
            Wait(completed, "shadow frame");
        } catch (...) { encoder.Cancel(); throw; }

        struct Readback { Image image; GLenum error = GL_NO_ERROR; };
        auto completed = std::make_shared<Completion<Readback>>();
        device_.async_ExecuteCode([this, completed, images] {
            const auto* texture = device_.GetResourcePool().TextureTable.Get(images.color);
            if (!texture) { completed->value.error = GL_INVALID_VALUE; return; }
            auto& image = completed->value.image;
            image.width = images.width; image.height = images.height;
            image.rgba.resize(std::size_t(image.width) * image.height * 4);
            glGetTextureImage(texture->rhi_id, 0, GL_RGBA, GL_FLOAT,
                              static_cast<GLsizei>(image.rgba.size() * sizeof(float)), image.rgba.data());
            completed->value.error = glGetError();
        }, [completed] { completed->done = true; });
        Wait(completed, "shadow visibility readback");
        Check(completed->value.error == GL_NO_ERROR, "OpenGL shadow draw/readback error: " + std::to_string(completed->value.error));
        for (std::size_t i = 0; i < completed->value.image.rgba.size(); ++i) {
            const float value = completed->value.image.rgba[i];
            Check(std::isfinite(value) && value >= 0 && value <= 1, "Non-finite/uncovered/out-of-range shadow visibility");
            if (i % 4 == 3) Check(value == 1, "Visibility rasterization missed pixels");
        }
        return std::move(completed->value.image);
    }
private:
    template <typename T> void Wait(const std::shared_ptr<Completion<T>>& completion, const char* label) {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!completion->done) {
            device_.returnSystem.DrainCallbacks();
            if (completion->done) break;
            Check(std::chrono::steady_clock::now() < deadline, std::string("Timed out waiting for ") + label);
            glfwPollEvents();
            device_.returnSystem.WaitForCallbacks(2ms);
        }
    }
    RHIDevice device_;
    std::shared_ptr<const void> owner_;
    RenderResourceHandle<PipelineSpec> depthPipeline_, visibilityPipeline_;
    RenderResourceHandle<VertexBufferSpec> plane_, box_;
    RenderResourceHandle<UniformBufferSpec> sceneUniform_, geometryUniform_;
    std::uint64_t frameIndex_ = 0;
};

SceneEffectsConstants SyntheticScene(std::uint32_t atlasSize) {
    SceneEffectsConstants result;
    result.effects = {1, 0, 0, float(atlasSize)};
    result.sunDirectionIntensity = {0, 0, -1, 1};
    result.cascadeSplits = {10, 20, 30, 40};
    result.cascadeWorldTexelSize = glm::vec4(4.0f / atlasSize);
    result.cascadeInverseDepthRange = glm::vec4(0.5f);
    return result;
}
SceneEffectsConstants OrthographicScene(std::uint32_t atlasSize, glm::vec3 direction, float phase = 0) {
    auto result = SyntheticScene(atlasSize);
    const glm::mat4 view = glm::lookAt(-glm::normalize(direction) * 6.0f, glm::vec3(0), glm::vec3(0, 0, 1));
    glm::mat4 projection = glm::ortho(-2.0f, 2.0f, -2.0f, 2.0f, 0.1f, 12.0f);
    projection[3].x += phase * 2.0f / (atlasSize / 2);
    for (auto& matrix : result.lightViewProjection) matrix = projection * view;
    result.sunDirectionIntensity = glm::vec4(glm::normalize(direction), 1);
    result.cascadeWorldTexelSize = glm::vec4(8.0f / atlasSize);
    result.cascadeInverseDepthRange = glm::vec4(1.0f / 11.9f);
    return result;
}
std::vector<float> TileDepths(std::uint32_t atlasSize, const std::array<float, 4>& values) {
    std::vector<float> result(std::size_t(atlasSize) * atlasSize);
    for (std::uint32_t y = 0; y < atlasSize; ++y)
        for (std::uint32_t x = 0; x < atlasSize; ++x)
            result[y * atlasSize + x] = values[(y / (atlasSize / 2)) * 2 + x / (atlasSize / 2)];
    return result;
}
double Mean(const Image& image) {
    double sum = 0;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) sum += image.rgba[i];
    return sum / (image.width * image.height);
}
double ContactMean(const Image& image) {
    double sum = 0;
    std::size_t count = 0;
    for (std::uint32_t y = 0; y < image.height; ++y) for (std::uint32_t x = 0; x < image.width; ++x) {
        const float worldX = -.45f + (x + .5f) / image.width * 1.2f;
        const float worldZ = -.8f + (y + .5f) / image.height * 1.6f;
        if (worldX > .03f && worldX < .075f && std::abs(worldZ) < .2f) {
            sum += image.At(x, y); ++count;
        }
    }
    Check(count > 0, "Empty contact sample region");
    return sum / count;
}

void TestContact(ShadowFixture& fixture) {
    auto images = fixture.Allocate(512, 128, 1024);
    const auto scene = OrthographicScene(images.atlasSize, {1, -2, 0});
    const auto receiver = Receiver(PlaneModel({.15f,0,0}, {.6f,0,0}, {0,0,.8f}), {0,1,0});
    const auto ground = PlaneModel({0,0,0}, {3,0,0}, {0,0,3});
    const auto box = glm::scale(glm::mat4(1), {.05f, 1, 1});
    const auto touching = fixture.Render(images, scene, receiver, {{ground, false}, {box, true}});
    const auto floating = fixture.Render(images, scene, receiver,
        {{ground, false}, {glm::translate(glm::mat4(1), {0,.3f,0}) * box, true}});
    const double contact = ContactMean(touching), separated = ContactMean(floating);
    std::cout << "Raster contact visibility=" << contact << ", raised-caster control=" << separated << '\n';
    Check(contact < .1, "Shadow detached from a grounded thin caster");
    Check(separated > .9, "Contact fixture failed to detect the deliberately raised caster");
    Check(touching.At(500, 64) > .99f, "Contact scene has self-shadowing outside the caster shadow");
}

void TestReceiverPlane(ShadowFixture& fixture) {
    double worstMean = 1, worstMinimum = 1, controlMean = 1;
    for (const auto resolution : {256u, 1024u}) {
        auto images = fixture.Allocate(96, 80, resolution);
        // The last two normals have N.L approximately .30 and .15. Their
        // filter footprint needs substantially more depth correction than a
        // gently tilted receiver, and catches an undersized RPDB clamp.
        for (const auto slope : {glm::vec2(-.65f,.35f), glm::vec2(.5f,-.4f), glm::vec2(1.15f,.2f),
                                 glm::vec2(-.95f,.35f), glm::vec2(-1.25f,.2f)}) {
            const glm::vec3 u(1,slope.x,0), v(0,slope.y,1);
            const glm::vec3 normal = glm::normalize(glm::cross(v, u));
            const auto receiver = Receiver(PlaneModel({0,0,0}, u * .55f, v * .55f), normal);
            const auto depthPlane = PlaneModel({0,0,0}, u * 3.0f, v * 3.0f);
            for (const float phase : {0.0f, .31f, .73f}) {
                const auto scene = OrthographicScene(resolution, {.6f,-1,.2f}, phase);
                const auto output = fixture.Render(images, scene, receiver, {{depthPlane, false}});
                worstMean = std::min(worstMean, Mean(output));
                for (std::size_t i = 0; i < output.rgba.size(); i += 4)
                    worstMinimum = std::min(worstMinimum, double(output.rgba[i]));
                if (resolution == 256 && phase == 0) {
                    auto control = receiver;
                    control.options.y = 1;
                    controlMean = std::min(controlMean, Mean(fixture.Render(images, scene, control, {{depthPlane, false}})));
                    if (slope.x < -.9f) {
                        std::cout << "Grazing N.L=" << glm::dot(normal, -glm::vec3(scene.sunDirectionIntensity));
                        for (const float limit : {4.0f, 8.0f, 16.0f}) {
                            auto comparison = scene;
                            comparison.shadowFilter.w = limit;
                            std::cout << ", clamp " << limit << " mean="
                                      << Mean(fixture.Render(images, comparison, receiver, {{depthPlane, false}}));
                        }
                        std::cout << '\n';
                    }
                }
            }
        }
    }
    std::cout << "Raster sloped receiver: worst mean=" << worstMean << ", minimum=" << worstMinimum
              << ", without receiver-plane derivatives=" << controlMean << '\n';
    Check(worstMean > .995 && worstMinimum > .95, "Sloped receiver developed shadow acne");
    Check(controlMean < .9, "Slope fixture did not exercise receiver-plane depth correction");
}

struct CurveSummary { double first = 0, last = 0, maxStep = 0, backwards = 0; };
CurveSummary Curve(const Image& image, bool increasing = true) {
    CurveSummary result;
    const auto y = image.height / 2;
    result.first = image.At(0, y); result.last = image.At(image.width - 1, y);
    for (std::uint32_t x = 1; x < image.width; ++x) {
        const double delta = image.At(x, y) - image.At(x - 1, y);
        result.maxStep = std::max(result.maxStep, std::abs(delta));
        result.backwards = std::max(result.backwards, increasing ? -delta : delta);
    }
    return result;
}
void TestTentContinuity(ShadowFixture& fixture) {
    constexpr std::uint32_t size = 128, tileSize = size / 2;
    auto depth = TileDepths(size, {.75f,.75f,.75f,.75f});
    for (std::uint32_t y = 0; y < tileSize; ++y)
        for (std::uint32_t x = 0; x < tileSize / 2; ++x) depth[y * size + x] = .25f;
    auto images = fixture.Allocate(1024, 8, size, depth);
    const auto scene = SyntheticScene(size);
    // Across eight shadow texels, every output pixel advances 1/128 of a
    // shadow texel. A hard nearest-depth 3x3 PCF creates 1/3 jumps here.
    const auto receiver = Receiver(PlaneModel({0,0,0}, {8.0f/tileSize,0,0}, {0,.2f,0}), {0,0,1});
    const auto summary = Curve(fixture.Render(images, scene, receiver));
    std::cout << "PCF phase: endpoints=" << summary.first << ',' << summary.last << ", max step=" << summary.maxStep << '\n';
    Check(summary.first < .001 && summary.last > .999, "PCF test did not cross the shadow edge");
    Check(summary.backwards < .001 && summary.maxStep < .015, "PCF has a discontinuity under subtexel receiver motion");
}
void TestAtlasIsolation(ShadowFixture& fixture) {
    constexpr std::uint32_t size = 64;
    auto images = fixture.Allocate(96, 96, size, TileDepths(size, {.25f,.75f,.75f,.25f}));
    const auto scene = SyntheticScene(size);
    for (int cascade = 0; cascade < 4; ++cascade) {
        const auto receiver = Receiver(PlaneModel({0,0,0}, {.99999f,0,0}, {0,.99999f,0}), {0,0,1}, cascade);
        const auto output = fixture.Render(images, scene, receiver);
        const float expected = cascade == 0 || cascade == 3 ? 0.0f : 1.0f;
        for (std::size_t i = 0; i < output.rgba.size(); i += 4)
            Check(std::abs(output.rgba[i] - expected) < .001f, "Shadow filter sampled an adjacent atlas tile");
    }
    std::cout << "Atlas isolation: all four tile interiors, edges and corners passed.\n";
}
void TestCascadeTransitions(ShadowFixture& fixture) {
    constexpr std::uint32_t size = 64;
    auto images = fixture.Allocate(512, 8, size, TileDepths(size, {.25f,.75f,.25f,.75f}));
    auto scene = SyntheticScene(size);
    scene.shadowParams.z = .2f;
    for (auto& matrix : scene.lightViewProjection) matrix = glm::scale(glm::mat4(1), {1,1,.01f});
    for (int boundary = 0; boundary < 3; ++boundary) {
        const float split = scene.cascadeSplits[boundary];
        const auto receiver = Receiver(PlaneModel({0,0,-split}, {0,0,-2}, {0,.2f,0}), {0,0,1}, -1);
        const bool increasing = boundary != 1;
        const auto summary = Curve(fixture.Render(images, scene, receiver), increasing);
        std::cout << "Cascade boundary " << boundary << ": endpoints=" << summary.first << ',' << summary.last
                  << ", max step=" << summary.maxStep << '\n';
        Check(std::abs(summary.last - summary.first) > .99, "Cascade blend fixture did not cross opposite shadow results");
        Check(summary.backwards < .001 && summary.maxStep < .015, "Cascade transition is discontinuous");
    }
    auto fadeImages = fixture.Allocate(512, 8, size, TileDepths(size, {.25f,.25f,.25f,.25f}));
    const auto receiver = Receiver(PlaneModel({0,0,-38}, {0,0,-3}, {0,.2f,0}), {0,0,1}, -1);
    const auto summary = Curve(fixture.Render(fadeImages, scene, receiver));
    std::cout << "Last-cascade fade: endpoints=" << summary.first << ',' << summary.last << ", max step=" << summary.maxStep << '\n';
    Check(summary.first < .001 && summary.last > .999 && summary.backwards < .001 && summary.maxStep < .015,
          "Last cascade disappears abruptly at shadow distance");
}
} // namespace

int main() {
    int result = 0;
    try {
        Check(glfwInit() == GLFW_TRUE, "glfwInit failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        ApplicationWindow::Window window({64, 64, "Shadow quality GPU test", false});
        Check(window.Activate(), "Hidden OpenGL 4.5 window creation failed");
        auto context = window.GetRenderContextAsOpengl();
        Check(context.has_value(), "Missing OpenGL context");
        ShadowFixture fixture(std::move(*context));
        fixture.Initialize();
        std::cout << std::fixed << std::setprecision(6);
        int failedCases = 0;
        const auto run = [&](const char* name, auto test) {
            try { test(fixture); }
            catch (const std::exception& error) {
                ++failedCases;
                std::cerr << name << ": " << error.what() << '\n';
            }
        };
        run("Contact", TestContact);
        run("Receiver plane", TestReceiverPlane);
        run("Tent continuity", TestTentContinuity);
        run("Atlas isolation", TestAtlasIsolation);
        run("Cascade transitions", TestCascadeTransitions);
        fixture.Shutdown();
        Check(failedCases == 0, std::to_string(failedCases) + " shadow quality case(s) failed");
        std::cout << "Shadow GPU quality passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "Shadow GPU quality failed: " << error.what() << '\n';
        result = 1;
    }
    glfwTerminate();
    return result;
}
