#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include "ApplicationWindow/public/window.h"
#include "Render/Private/Material/rhi_material_resources.h"
#include "Render/Public/Material/unlit_material.h"
#include "Render/Public/Pipeline/render_pipeline.h"

namespace {

using namespace Render;
using namespace Render::Material;
using namespace std::chrono_literals;

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename T>
struct Completion {
    T value{};
    bool done = false;
};

// All callback outputs have shared lifetime. In particular, timing out cannot
// leave a callback referring to an Await() stack variable. The fixture and its
// device stay alive while shutdown joins the render thread and drains callbacks.
class MaterialSmoke {
public:
    explicit MaterialSmoke(OpenglBackendContext context) {
        device_.Run(BackendType::Opengl, std::move(context));
    }
    ~MaterialSmoke() { Shutdown(); }

    void Run() {
        const auto surfaceAsset = MaterialAsset::Create(MakeUnlitTemplate(Domain::Surface));
        const auto spriteAsset = MaterialAsset::Create(MakeUnlitTemplate(Domain::Sprite));
        CreateSharedTexture();
        resources_[0] = CreateMaterialResources(Domain::Surface, surfaceAsset->Parameters().GetTemplate());
        resources_[1] = CreateMaterialResources(Domain::Sprite, spriteAsset->Parameters().GetTemplate());
        meshes_[0] = CreateMesh(Domain::Surface);
        meshes_[1] = CreateMesh(Domain::Sprite);
        view_.viewUniform = CreateUniform(sizeof(ViewConstants));
        view_.objectUniform = CreateUniform(sizeof(ObjectConstants));

        std::string error;
        const auto surface = materials_.Register(surfaceAsset, resources_[0], &error);
        Check(surface.IsValid(), "Register Surface: " + error);
        const auto sprite = materials_.Register(spriteAsset, resources_[1], &error);
        Check(sprite.IsValid(), "Register Sprite: " + error);

        Check(materials_.SetParameter(surface, "baseColor", glm::vec4(1, 0, 0, 1), &error), error);
        CheckColor(RenderAndRead({Item(meshes_[0], Capture(surface))}), {255, 0, 0}, "red Surface");

        // A second frame uses the same GPU UBO and material handle. Updating its
        // CPU instance must upload the new complete parameter block.
        Check(materials_.SetParameter(surface, "baseColor", glm::vec4(0, 0, 1, 1), &error), error);
        CheckColor(RenderAndRead({Item(meshes_[0], Capture(surface))}), {0, 0, 255}, "edited blue Surface");

        Check(materials_.SetParameter(sprite, "baseColor", glm::vec4(0, 1, 0, 1), &error), error);
        CheckColor(RenderAndRead({Item(meshes_[1], Capture(sprite))}), {0, 255, 0}, "green Sprite");

        // Deliberately conflicting sort keys must not reorder Sprite A/B/A.
        // The third draw also reuses A's snapshot and the very same material UBO.
        // Straight alpha over black: R(.5), G(.5), R(.5) -> (.625, .25, 0).
        auto red = materials_.Capture(sprite, {{"baseColor", glm::vec4(1, 0, 0, 0.5f)}}, &error);
        Check(red != nullptr, error);
        auto green = materials_.Capture(sprite, {{"baseColor", glm::vec4(0, 1, 0, 0.5f)}}, &error);
        Check(green != nullptr, error);
        auto a = Item(meshes_[1], red);
        auto b = Item(meshes_[1], green);
        a.sortKey = 100;
        b.sortKey = 0;
        CheckColor(RenderAndRead({a, b, a}), {159, 64, 0}, "transparent Sprite A/B/A");
        std::cout << "Material OpenGL smoke passed: Surface red/blue, Sprite green, transparent A/B/A\n";
    }

    void Shutdown() {
        if (stopped_) return;
        // Every material GPU handle is adopted exactly once. Both pass owners
        // retain the one shared texture owner through dependencies.
        materials_.Clear();
        resources_ = {};
        textureOwner_.reset();
        RetainMaterialResources(device_, std::exchange(pendingOwned_, {})).reset();
        for (auto& mesh : meshes_) {
            if (mesh.IsValid()) device_.async_DeleteVertexBuffer(mesh);
            mesh = {};
        }
        if (view_.viewUniform.IsValid()) device_.async_DeleteUniformBuffer(view_.viewUniform);
        if (view_.objectUniform.IsValid()) device_.async_DeleteUniformBuffer(view_.objectUniform);
        view_ = {};
        // Stop also discards unsubmitted encoders and drains their resource
        // leases before joining. Late completion callbacks only hold value data.
        device_.StopAndRelease();
        while (!device_.returnSystem.callbacks.empty()) device_.returnSystem.DrainCallbacks();
        stopped_ = true;
    }

private:
    template <typename Predicate>
    void Wait(Predicate complete, const char* operation) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!complete()) {
            device_.returnSystem.DrainCallbacks();
            if (complete()) break;
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error(std::string("Timed out waiting for ") + operation);
            glfwPollEvents();
            device_.returnSystem.WaitForCallbacks(10ms);
        }
    }

    template <typename Spec, typename Submit>
    RenderResourceHandle<Spec> AwaitHandle(Submit submit, const char* operation) {
        auto completion = std::make_shared<Completion<RenderResourceHandle<Spec>>>();
        submit([completion](RenderResourceHandle<Spec> handle) {
            completion->value = handle;
            completion->done = true;
        });
        Wait([completion] { return completion->done; }, operation);
        Check(completion->value.IsValid(), std::string("RHI creation failed: ") + operation);
        return completion->value;
    }

    RenderResourceHandle<ShaderSourceSpec> Compile(ShaderSourceType type, const std::string& source) {
        CreateShaderSourceDesc desc{type, source.data(), source.size()};
        auto handle = AwaitHandle<ShaderSourceSpec>([&](auto callback) {
            device_.async_CreateShaderSource(desc, std::move(callback));
        }, "shader source");
        pendingOwned_.sources.push_back(handle);
        return handle;
    }

    RenderResourceHandle<UniformBufferSpec> CreateUniform(std::uint32_t bytes) {
        CreateUniformBufferDesc desc;
        desc.byteSize = bytes;
        return AwaitHandle<UniformBufferSpec>([&](auto callback) {
            device_.async_CreateUniformBuffer(desc, std::move(callback));
        }, "uniform buffer");
    }

    void CreateSharedTexture() {
        const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
        CreateRHITextureSpec desc;
        desc.width = desc.height = 1;
        desc.data = white.data();
        texture_ = AwaitHandle<RHITextureSpec>([&](auto callback) {
            device_.async_CreateTexture(desc, std::move(callback));
        }, "white texture");
        pendingOwned_.textures.push_back(texture_);
        textureOwner_ = RetainMaterialResources(device_, std::exchange(pendingOwned_, {}));
    }

    MaterialPassResources CreateMaterialResources(Domain domain,
                                                   std::shared_ptr<const MaterialTemplate> schema) {
        const auto vertex = Compile(ShaderSourceType::Vertex, UnlitVertexShader(domain));
        const auto fragment = Compile(ShaderSourceType::Fragment, UnlitFragmentShader());
        CreateGraphicShaderDesc shader;
        shader.vertexShaderSource = vertex;
        shader.fragmentShaderSource = fragment;
        const auto program = AwaitHandle<ShaderProgramSpec>([&](auto callback) {
            device_.async_CreateGraphicShader(shader, std::move(callback));
        }, "unlit shader program");
        pendingOwned_.programs.push_back(program);

        InspectMaterialBlock(program, static_cast<GLint>(schema->ByteSize()));
        CreatePipelineDesc pipelineDesc{UnlitPipelineSpec(program, domain)};
        const auto pipeline = AwaitHandle<PipelineSpec>([&](auto callback) {
            device_.async_CreatePipeline(pipelineDesc, std::move(callback));
        }, "unlit pipeline");
        pendingOwned_.pipelines.push_back(pipeline);
        const auto parameters = CreateUniform(static_cast<std::uint32_t>(schema->ByteSize()));
        pendingOwned_.uniformBuffers.push_back(parameters);
        pendingOwned_.dependencies.push_back(textureOwner_);

        MaterialPassResources result;
        result.expectedTemplate = std::move(schema);
        result.pipeline = pipeline;
        result.parameterBuffer = parameters;
        result.parameterBufferBytes = static_cast<std::uint32_t>(result.expectedTemplate->ByteSize());
        result.textures = {{0, texture_}};
        result.lifetime = RetainMaterialResources(device_, std::exchange(pendingOwned_, {}));
        return result;
    }

    void InspectMaterialBlock(RenderResourceHandle<ShaderProgramSpec> handle, GLint expectedBytes) {
        struct Inspection { GLint bytes = -1; GLenum error = GL_NO_ERROR; };
        auto completion = std::make_shared<Completion<Inspection>>();
        device_.async_ExecuteCode([this, handle, completion] {
            const auto* program = device_.GetResourcePool().shaderProgramTable.Get(handle);
            if (program) {
                const GLuint block = glGetUniformBlockIndex(program->rhi_id, "MaterialData");
                if (block != GL_INVALID_INDEX)
                    glGetActiveUniformBlockiv(program->rhi_id, block, GL_UNIFORM_BLOCK_DATA_SIZE,
                                              &completion->value.bytes);
            }
            completion->value.error = glGetError();
        }, [completion] { completion->done = true; });
        Wait([completion] { return completion->done; }, "MaterialData reflection");
        Check(completion->value.error == GL_NO_ERROR, "GL error during material reflection");
        Check(completion->value.bytes == expectedBytes, "GLSL MaterialData block size differs from CPU schema");
    }

    RenderResourceHandle<VertexBufferSpec> CreateMesh(Domain domain) {
        const std::vector<float> surface = {
            -0.8f, -0.8f, 0, 0, 0, 1, 1, 1, 1,
             0.8f, -0.8f, 0, 1, 0, 1, 1, 1, 1,
             0.0f,  0.8f, 0, 0.5f, 1, 1, 1, 1, 1
        };
        const std::vector<float> sprite = {
            -0.8f, -0.8f, 0, 0, 1, 1, 1, 1,
             0.8f, -0.8f, 1, 0, 1, 1, 1, 1,
             0.0f,  0.8f, 0.5f, 1, 1, 1, 1, 1
        };
        const auto& vertices = domain == Domain::Surface ? surface : sprite;
        const std::array<std::uint32_t, 3> indices{0, 1, 2};
        CreateMeshBufferDesc desc;
        desc.vertexLayout = UnlitVertexLayout(domain);
        desc.vertexCount = 3;
        desc.vertexData = vertices.data();
        desc.vertexByteSize = static_cast<std::uint32_t>(vertices.size() * sizeof(float));
        desc.indexCount = 3;
        desc.indexData = indices.data();
        desc.indexByteSize = sizeof(indices);
        return AwaitHandle<VertexBufferSpec>([&](auto callback) {
            device_.async_CreateMeshBuffer(desc, std::move(callback));
        }, "triangle mesh");
    }

    std::shared_ptr<const MaterialSnapshot> Capture(MaterialHandle handle) {
        std::string error;
        auto snapshot = materials_.Capture(handle, {}, &error);
        Check(snapshot != nullptr, "Capture material: " + error);
        return snapshot;
    }

    static RenderItem Item(RenderResourceHandle<VertexBufferSpec> mesh,
                           std::shared_ptr<const MaterialSnapshot> material) {
        RenderItem item;
        item.mesh = mesh;
        item.layer = material->GetDomain() == Domain::Sprite ? RenderLayer::Transparent : RenderLayer::Opaque;
        item.material = std::move(material);
        item.draw.indexCount = 3;
        return item;
    }

    std::array<std::uint8_t, 4> RenderAndRead(std::vector<RenderItem> items) {
        RHICommand::BeginFrame begin;
        begin.frameIndex = ++frameIndex_;
        begin.framebufferWidth = begin.framebufferHeight = 64;
        begin.clearColor = glm::vec4(0, 0, 0, 1);
        auto encoder = device_.BeginFrame(begin);
        RenderFrame frame{frameIndex_, std::move(items)};
        ForwardRenderPipeline pipeline;
        if (!pipeline.Record(encoder, frame, view_) || !encoder.End(false)) {
            encoder.Cancel();
            throw std::runtime_error("Could not record material frame");
        }
        auto submitted = std::make_shared<Completion<bool>>();
        if (!device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(), [submitted] { submitted->done = true; })) {
            encoder.Cancel();
            throw std::runtime_error("Could not submit material frame");
        }
        Wait([submitted] { return submitted->done; }, "material frame");

        struct Pixels { std::array<std::uint8_t, 4> rgba{}; GLenum error = GL_NO_ERROR; };
        auto readback = std::make_shared<Completion<Pixels>>();
        device_.async_ExecuteCode([readback] {
            glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, readback->value.rgba.data());
            readback->value.error = glGetError();
        }, [readback] { readback->done = true; });
        Wait([readback] { return readback->done; }, "center pixel readback");
        Check(readback->value.error == GL_NO_ERROR, "GL error during material draw/readback");
        return readback->value.rgba;
    }

    static void CheckColor(const std::array<std::uint8_t, 4>& actual,
                           const std::array<int, 3>& expected, const char* label) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            if (std::abs(static_cast<int>(actual[channel]) - expected[channel]) > 4) {
                throw std::runtime_error(std::string(label) + ": expected RGB(" +
                    std::to_string(expected[0]) + "," + std::to_string(expected[1]) + "," +
                    std::to_string(expected[2]) + "), got (" + std::to_string(actual[0]) + "," +
                    std::to_string(actual[1]) + "," + std::to_string(actual[2]) + ")");
            }
        }
    }

    RHIDevice device_;
    MaterialService materials_;
    std::array<MaterialPassResources, 2> resources_;
    OwnedMaterialResources pendingOwned_;
    std::shared_ptr<const void> textureOwner_;
    RenderResourceHandle<RHITextureSpec> texture_;
    std::array<RenderResourceHandle<VertexBufferSpec>, 2> meshes_;
    RenderView view_;
    std::uint64_t frameIndex_ = 0;
    bool stopped_ = false;
};

} // namespace

int main() {
    if (!glfwInit()) {
        std::cerr << "Material OpenGL smoke failed: glfwInit\n";
        return 1;
    }
    int result = 0;
    {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        ApplicationWindow::Window window(ApplicationWindow::WindowSpec{64, 64, "Material smoke", false});
        try {
            Check(window.Activate(), "Could not activate hidden OpenGL window");
            auto context = window.GetRenderContextAsOpengl();
            Check(context.has_value(), "No OpenGL window context");
            MaterialSmoke smoke(std::move(*context));
            try {
                smoke.Run();
            } catch (...) {
                smoke.Shutdown();
                throw;
            }
            smoke.Shutdown();
        } catch (const std::exception& error) {
            std::cerr << "Material OpenGL smoke failed: " << error.what() << '\n';
            result = 1;
        }
    }
    glfwTerminate();
    return result;
}
