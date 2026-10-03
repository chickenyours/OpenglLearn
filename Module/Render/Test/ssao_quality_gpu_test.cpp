#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"

namespace {
using namespace Render;
using namespace std::chrono_literals;
using Pixels = std::vector<float>;
using Texture = RenderResourceHandle<RHITextureSpec>;
using Pipeline = RenderResourceHandle<PipelineSpec>;

void Check(bool result, const std::string& message) {
    if (!result) throw std::runtime_error(message);
}
template <class T> struct Completion { T value{}; bool done = false; };

// Negative control for reproducing the regression on the same GPU. Its error
// metrics are diagnostic only: exact boundary rounding can vary by driver.
std::string ShaderForTest(bool legacyNeighbours) {
    auto source = SsaoShader();
    if (!legacyNeighbours) return source;
    const auto begin = source.find("vec3 DepthNormal(");
    const auto end = source.find("void main() {", begin);
    Check(begin != std::string::npos && end != std::string::npos, "SSAO normal function missing");
    source.replace(begin, end - begin, R"GLSL(vec3 DepthNormal(vec2 uv, vec3 center) {
    vec2 texel = 1.0 / vec2(textureSize(sceneDepth, 0));
    vec2 leftUV = SafeUV(sceneDepth, uv - vec2(texel.x, 0.0));
    vec2 rightUV = SafeUV(sceneDepth, uv + vec2(texel.x, 0.0));
    vec2 downUV = SafeUV(sceneDepth, uv - vec2(0.0, texel.y));
    vec2 upUV = SafeUV(sceneDepth, uv + vec2(0.0, texel.y));
    vec3 left = ViewPosition(leftUV, ReadDepth(leftUV));
    vec3 right = ViewPosition(rightUV, ReadDepth(rightUV));
    vec3 down = ViewPosition(downUV, ReadDepth(downUV));
    vec3 up = ViewPosition(upUV, ReadDepth(upUV));
    vec3 dx = abs(center.z - left.z) < abs(right.z - center.z) ? center - left : right - center;
    vec3 dy = abs(center.z - down.z) < abs(up.z - center.z) ? center - down : up - center;
    vec3 towardsCamera = SafeNormalize(-center, vec3(0.0, 0.0, 1.0));
    vec3 normal = SafeNormalize(cross(dx, dy), towardsCamera);
    return dot(normal, towardsCamera) < 0.0 ? -normal : normal;
}
)GLSL");
    return source;
}

// Keep the production depth-normal functions verbatim. Only replace the final
// entry point so an analytic plane can validate their numerical result on GPU.
std::string NormalDiagnosticShader(std::string source) {
    const auto main = source.find("void main() {");
    Check(main != std::string::npos, "SSAO shader entry point missing");
    source.erase(main);
    source += R"GLSL(void main() {
    vec3 position = ViewPosition(vUV, ReadDepth(vUV));
    outColor = vec4(DepthNormal(vUV, position), 1.0);
}
)GLSL";
    return source;
}

class SsaoGpuTest {
public:
    explicit SsaoGpuTest(OpenglBackendContext context) {
        device_.Run(BackendType::Opengl, std::move(context));
    }
    ~SsaoGpuTest() { Shutdown(); }
    void Shutdown() {
        if (stopped_) return;
        owner_.reset();
        device_.StopAndRelease();
        device_.returnSystem.DrainCallbacks();
        stopped_ = true;
    }
    void Run() {
        PipelineDetail::ResourceBuilder builder(device_);
        const auto shader = ShaderForTest(false);
        ao_ = builder.FullscreenPipeline(FullscreenVertexShader(), shader);
        normals_ = builder.FullscreenPipeline(FullscreenVertexShader(), NormalDiagnosticShader(shader));
        const auto legacy = ShaderForTest(true);
        legacyAo_ = builder.FullscreenPipeline(FullscreenVertexShader(), legacy);
        legacyNormals_ = builder.FullscreenPipeline(FullscreenVertexShader(), NormalDiagnosticShader(legacy));
        mesh_ = builder.FullscreenMesh();
        uniform_ = builder.Uniform(sizeof(PostProcessConstants));
        owner_ = builder.Finish();

        // Non-power-of-two even widths put half-size centers on depth texel
        // boundaries. Odd sizes exercise a different mapping in both axes.
        CheckPlane(640, 192, {0.65f, 0.35f}, true);
        CheckPlane(1280, 196, {0.65f, 0.35f}, false);
        CheckPlane(1536, 192, {-0.50f, 0.70f}, false);
        CheckPlane(641, 193, {0.65f, 0.35f}, false);
        CheckPlane(1279, 195, {-0.50f, 0.70f}, false);
        std::cout << "SSAO quality GPU test passed: integer depth neighbours, plane normals, "
                     "stripe-free half-size AO, odd dimensions and contact occlusion\n";
    }

private:
    template <class Predicate> void Wait(Predicate done, const char* label) {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!done()) {
            device_.returnSystem.DrainCallbacks();
            if (done()) break;
            Check(std::chrono::steady_clock::now() < deadline, std::string("Timeout: ") + label);
            glfwPollEvents();
            device_.returnSystem.WaitForCallbacks(5ms);
        }
    }
    Texture Depth(PipelineDetail::ResourceBuilder& builder, std::uint32_t width,
                  std::uint32_t height, const Pixels& pixels) {
        CreateRHITextureSpec desc;
        desc.width = width; desc.height = height;
        desc.textureDataStoreType = desc.textureUseType = RHITextureFormat::Depth32F;
        desc.filterMode = RHIFilterMode::Nearest;
        desc.addressMode = RHIAddressMode::ClampToEdge;
        desc.mipmaps = false;
        desc.data = pixels.data();
        return builder.Create<RHITextureSpec>([&](auto callback) { device_.async_CreateTexture(desc, callback); });
    }
    Pixels Draw(Pipeline pipeline, Texture depth, Texture output,
                RenderResourceHandle<RenderTargetSpec> target, std::uint32_t width, std::uint32_t height,
                const PostProcessConstants& constants, const std::shared_ptr<const void>& imageOwner) {
        RHICommand::BeginFrame begin;
        begin.frameIndex = ++frameIndex_; begin.framebufferWidth = width; begin.framebufferHeight = height;
        auto encoder = device_.BeginFrame(begin);
        const auto abort = [&] { encoder.Cancel(); throw std::runtime_error("Could not record SSAO GPU test pass"); };
        RHICommand::SetRenderTarget bind;
        bind.target = target; bind.width = width; bind.height = height;
        bind.clearFlags = RHICommand::ClearColor;
        if (!encoder.KeepAlive(owner_) || !encoder.KeepAlive(imageOwner) || !encoder.SetRenderTarget(bind) ||
            !encoder.UpdateUniformBufferBytes(uniform_, reinterpret_cast<const std::byte*>(&constants), sizeof(constants)) ||
            !encoder.BindUniformBuffer(uniform_, PostProcessBinding) || !encoder.BindPipeline(pipeline) ||
            !encoder.BindMesh(mesh_) || !encoder.BindTexture(depth, 2) ||
            !encoder.DrawIndexed(RHICommand::DrawIndexed{3}) || !encoder.End(false)) abort();
        auto completed = std::make_shared<Completion<bool>>();
        if (!device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(), [completed] { completed->done = true; })) abort();
        Wait([completed] { return completed->done; }, "SSAO frame");
        struct Readback { Pixels pixels; GLenum error = GL_NO_ERROR; bool found = false; };
        auto result = std::make_shared<Completion<Readback>>();
        result->value.pixels.resize(std::size_t(width) * height * 4);
        device_.async_ExecuteCode([this, output, result] {
            const auto* texture = device_.GetResourcePool().TextureTable.Get(output);
            if (texture) {
                result->value.found = true;
                glGetTextureImage(texture->rhi_id, 0, GL_RGBA, GL_FLOAT,
                    static_cast<GLsizei>(result->value.pixels.size() * sizeof(float)), result->value.pixels.data());
            }
            result->value.error = glGetError();
        }, [result] { result->done = true; });
        Wait([result] { return result->done; }, "SSAO readback");
        Check(result->value.found && result->value.error == GL_NO_ERROR, "GL error during SSAO draw/readback");
        for (float value : result->value.pixels) Check(std::isfinite(value), "SSAO produced a nonfinite pixel");
        return result->value.pixels;
    }
    void CheckPlane(std::uint32_t width, std::uint32_t height, glm::vec2 slope, bool checkContact) {
        const auto halfWidth = (width + 1) / 2, halfHeight = (height + 1) / 2;
        PostProcessConstants constants;
        constants.projection = glm::ortho(-4.0f, 4.0f, -3.0f, 3.0f, 0.1f, 20.0f);
        constants.inverseProjection = glm::inverse(constants.projection);
        constants.screen = {1.0f / width, 1.0f / height, 0, 0};
        constants.misc = {0.1f, 20.0f, 0, 1};
        const auto depthOf = [&](float x, float y, float z) {
            const auto clip = constants.projection * glm::vec4(x, y, z, 1);
            return clip.z / clip.w * 0.5f + 0.5f;
        };
        Pixels plane(std::size_t(width) * height);
        Pixels contact(plane.size());
        for (std::uint32_t y = 0; y < height; ++y) for (std::uint32_t x = 0; x < width; ++x) {
            const float px = ((x + 0.5f) / width * 2.0f - 1.0f) * 4.0f;
            const float py = ((y + 0.5f) / height * 2.0f - 1.0f) * 3.0f;
            plane[std::size_t(y) * width + x] = depthOf(px, py, -8.0f + slope.x * px + slope.y * py);
            // A shallow raised plate must still occlude its surrounding plane.
            const float z = std::abs(px) < 1.0f && std::abs(py) < 1.0f ? -7.8f : -8.0f;
            contact[std::size_t(y) * width + x] = depthOf(px, py, z);
        }
        PipelineDetail::ResourceBuilder builder(device_);
        const auto depth = Depth(builder, width, height, plane);
        const auto output = builder.Texture(halfWidth, halfHeight, RHITextureFormat::RGBA16F);
        const auto target = builder.Target(output, {});
        const auto contactDepth = checkContact ? Depth(builder, width, height, contact) : Texture{};
        const auto clearDepth = checkContact ? Depth(builder, width, height, Pixels(plane.size(), 1.0f)) : Texture{};
        const auto images = builder.Finish();
        const auto normals = Draw(normals_, depth, output, target, halfWidth, halfHeight, constants, images);
        const auto ao = Draw(ao_, depth, output, target, halfWidth, halfHeight, constants, images);
        const auto expectedNormal = glm::normalize(glm::vec3(-slope, 1.0f));
        float maximumNormalError = 0, minimumAo = 1;
        double meanAo = 0;
        std::size_t count = 0;
        float minimumColumn = 1, maximumColumn = 0;
        for (std::uint32_t x = 3; x + 3 < halfWidth; ++x) {
            double column = 0;
            std::size_t columnCount = 0;
            for (std::uint32_t y = 3; y + 3 < halfHeight; ++y) {
                const auto index = (std::size_t(y) * halfWidth + x) * 4;
                const glm::vec3 measured(normals[index], normals[index + 1], normals[index + 2]);
                maximumNormalError = std::max(maximumNormalError, glm::length(measured - expectedNormal));
                minimumAo = std::min(minimumAo, ao[index]);
                column += ao[index]; ++columnCount;
                meanAo += ao[index]; ++count;
            }
            minimumColumn = std::min(minimumColumn, float(column / columnCount));
            maximumColumn = std::max(maximumColumn, float(column / columnCount));
        }
        meanAo /= count;
        const auto label = std::to_string(width) + "x" + std::to_string(height);
        std::cout << "SSAO " << label << " normal max error=" << maximumNormalError
                  << " AO min/mean=" << minimumAo << '/' << meanAo
                  << " column range=" << maximumColumn - minimumColumn << '\n';
        Check(maximumNormalError < 0.005f, label + ": depth neighbours changed the analytic plane normal");
        Check(minimumAo > 0.97f && meanAo > 0.998 && maximumColumn - minimumColumn < 0.002f,
              label + ": an unoccluded sloped plane contains AO stripes");
        if (checkContact) {
            const auto oldNormals = Draw(legacyNormals_, depth, output, target, halfWidth, halfHeight, constants, images);
            const auto oldAo = Draw(legacyAo_, depth, output, target, halfWidth, halfHeight, constants, images);
            float oldMaximumError = 0, oldMinimumAo = 1;
            double oldMeanAo = 0;
            std::size_t oldBadNormals = 0, oldCount = 0;
            for (std::uint32_t y = 3; y + 3 < halfHeight; ++y)
                for (std::uint32_t x = 3; x + 3 < halfWidth; ++x) {
                    const auto index = (std::size_t(y) * halfWidth + x) * 4;
                    const glm::vec3 measured(oldNormals[index], oldNormals[index + 1], oldNormals[index + 2]);
                    const float error = glm::length(measured - expectedNormal);
                    oldMaximumError = std::max(oldMaximumError, error);
                    if (error >= 0.005f) ++oldBadNormals;
                    oldMinimumAo = std::min(oldMinimumAo, oldAo[index]);
                    oldMeanAo += oldAo[index]; ++oldCount;
                }
            std::cout << "Legacy UV-neighbour control: bad normals=" << oldBadNormals << '/' << oldCount
                      << " max error=" << oldMaximumError << " AO min/mean=" << oldMinimumAo
                      << '/' << oldMeanAo / oldCount << '\n';
            const auto occluded = Draw(ao_, contactDepth, output, target, halfWidth, halfHeight, constants, images);
            std::size_t dark = 0;
            for (std::size_t index = 0; index < occluded.size(); index += 4)
                if (occluded[index] < 0.95f) ++dark;
            Check(dark > 20, "SSAO no longer detects a raised plate's contact occlusion");
            const auto clear = Draw(ao_, clearDepth, output, target, halfWidth, halfHeight, constants, images);
            constants.ssao.w = 0;
            const auto disabled = Draw(ao_, contactDepth, output, target, halfWidth, halfHeight, constants, images);
            for (std::size_t index = 0; index < clear.size(); index += 4)
                Check(clear[index] == 1.0f && disabled[index] == 1.0f, "Clear/disabled SSAO must be white");
            std::cout << "SSAO contact dark pixels=" << dark << "; clear/disabled output white\n";
        }
    }

    RHIDevice device_;
    std::shared_ptr<const void> owner_;
    Pipeline ao_, normals_, legacyAo_, legacyNormals_;
    RenderResourceHandle<VertexBufferSpec> mesh_;
    RenderResourceHandle<UniformBufferSpec> uniform_;
    std::uint64_t frameIndex_ = 0;
    bool stopped_ = false;
};
} // namespace

int main() {
    int result = 0;
    try {
        Check(glfwInit() == GLFW_TRUE, "glfwInit failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        ApplicationWindow::Window window({64, 64, "SSAO quality GPU test", false});
        Check(window.Activate(), "Could not activate hidden OpenGL 4.5 window");
        auto context = window.GetRenderContextAsOpengl();
        Check(context.has_value(), "No OpenGL context");
        SsaoGpuTest test(std::move(*context));
        test.Run();
        test.Shutdown();
    } catch (const std::exception& error) {
        std::cerr << "SSAO quality GPU test failed: " << error.what() << '\n';
        result = 1;
    }
    glfwTerminate();
    return result;
}
