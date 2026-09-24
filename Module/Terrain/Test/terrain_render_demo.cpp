// Dynamic terrain demo for chickenyours/OpenglLearn.
//
// Terrain generation now lives in the ECS: chunk entities own ChunkBlocks /
// ChunkMesh / ChunkRender, the streaming / generation / meshing systems fill
// them (generation and meshing run on the engine JobSystem), and rendering goes
// through the RenderWorld extraction pipeline. This file is only the host:
// window, camera, GPU pipelines and the main loop.

#define _CRT_SECURE_NO_WARNINGS

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include <glad/glad.h>
#include <glfw/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/ECS/Core/Kernel/kernel.h"
#include "engine/ECS/Scene/scene.h"
#include "engine/ECS/System/system_pipeline.h"

#include "ApplicationWindow/module.h"
#include "Render/module.h"
#include "Render/Private/rhi_mesh_upload_queue.h"
#include "Render/Public/Pipeline/render_pipeline.h"
#include "Render/Public/Pipeline/render_world.h"
#include "Render/Public/RHIResourceType/Buffer/uniform_buffer.h"
#include "Render/Public/RHIResourceType/Pipeline/pipeline.h"
#include "Render/Public/RHIResourceType/Shader/shader.h"
#include "Render/Public/RHIResourceType/Shader/shader_source.h"
#include "Render/Systems/callback_system.h"
#include "Render/Systems/render_pipeline_system.h"

#include "Terrain/Public/block_atlas.h"
#include "Terrain/Public/terrain_atlas_texture.h"
#include "Terrain/Public/terrain_components.h"
#include "Terrain/Public/terrain_edit.h"
#include "Terrain/Public/terrain_generator.h"
#include "Terrain/Public/terrain_raycast.h"
#include "Terrain/Systems/terrain_systems.h"

namespace {

constexpr glm::vec4 kSkyColor{0.44f, 0.68f, 0.90f, 1.0f};

// Voxel break reach (blocks). The crosshair ray is short on purpose: it never
// scans the whole world.
constexpr float kBreakReach = 6.0f;

constexpr char kVertexShader[] = R"GLSL(
#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec2 aTexCoord;

layout(std140, binding = 0) uniform ViewData {
    mat4 uViewProjection;
    vec4 uCameraPosition;
};

layout(std140, binding = 1) uniform ObjectData {
    mat4 uModel;
};

out vec3 vWorldPosition;
out vec3 vNormal;
out vec3 vColor;
out vec2 vTexCoord;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPosition = world.xyz;
    vNormal = normalize(mat3(uModel) * aNormal);
    vColor = aColor;
    vTexCoord = aTexCoord;
    gl_Position = uViewProjection * world;
}
)GLSL";

constexpr char kFragmentShader[] = R"GLSL(
#version 450 core

layout(std140, binding = 0) uniform ViewData {
    mat4 uViewProjection;
    vec4 uCameraPosition;
};

layout(binding = 0) uniform sampler2D uAtlas;

in vec3 vWorldPosition;
in vec3 vNormal;
in vec3 vColor;
in vec2 vTexCoord;

layout(location = 0) out vec4 FragColor;

void main() {
    const vec3 sky = vec3(0.44, 0.68, 0.90);
    vec3 n = normalize(vNormal);
    vec3 lightDirection = normalize(vec3(0.42, 0.86, 0.28));
    float light = 0.34 + 0.66 * max(dot(n, lightDirection), 0.0);

    // Atlas sample, tinted by the per-face vertex color (future biome tint).
    vec3 albedo = texture(uAtlas, vTexCoord).rgb * vColor;
    vec3 color = albedo * light;

    float d = distance(vWorldPosition, uCameraPosition.xyz);
    float fog = smoothstep(250.0, 400.0, d);
    color = mix(color, sky, fog);

    FragColor = vec4(color, 1.0);
}
)GLSL";

constexpr char kWaterFragmentShader[] = R"GLSL(
#version 450 core

layout(std140, binding = 0) uniform ViewData {
    mat4 uViewProjection;
    vec4 uCameraPosition;
};

layout(binding = 0) uniform sampler2D uAtlas;

in vec3 vWorldPosition;
in vec3 vNormal;
in vec3 vColor;
in vec2 vTexCoord;

layout(location = 0) out vec4 FragColor;

void main() {
    const vec3 sky = vec3(0.44, 0.68, 0.90);
    vec3 n = normalize(vNormal);
    vec3 lightDirection = normalize(vec3(0.42, 0.86, 0.28));
    float light = 0.40 + 0.60 * max(dot(n, lightDirection), 0.0);

    vec3 viewDirection = normalize(uCameraPosition.xyz - vWorldPosition);
    float fresnel = pow(1.0 - max(dot(n, viewDirection), 0.0), 3.0);

    vec3 albedo = texture(uAtlas, vTexCoord).rgb * vColor;
    vec3 color = mix(albedo * light, sky, fresnel * 0.6);

    float d = distance(vWorldPosition, uCameraPosition.xyz);
    float fog = smoothstep(62.0, 88.0, d);
    color = mix(color, sky, fog);

    float alpha = mix(0.60, 0.92, fresnel);
    alpha = mix(alpha, 1.0, fog);

    FragColor = vec4(color, alpha);
}
)GLSL";

// Screen-space crosshair. The vertex shader writes clip-space positions directly
// and ignores the camera view/projection, so the crosshair is always dead centre
// regardless of resolution; the pipeline disables depth test/write so world
// geometry never occludes it.
constexpr char kCrosshairVertexShader[] = R"GLSL(
#version 450 core

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec3 aColor;

out vec3 vColor;

void main() {
    vColor = aColor;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)GLSL";

constexpr char kCrosshairFragmentShader[] = R"GLSL(
#version 450 core

in vec3 vColor;
layout(location = 0) out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
)GLSL";

struct CrosshairVertex {
    glm::vec2 position{0.0f};
    glm::vec3 color{1.0f};
};

Render::VertexLayout CrosshairVertexLayout() {
    Render::VertexLayout layout;
    layout.typeSlots = {Render::VertexFieldType::Vec2, Render::VertexFieldType::Vec3};
    return layout;
}

struct CrosshairMeshData {
    std::vector<CrosshairVertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Four rectangles (a plus) with a dark outline, sized in pixels using the live
// framebuffer dimensions so it never depends on a hard-coded resolution.
CrosshairMeshData BuildCrosshair(float width, float height) {
    CrosshairMeshData data;
    const float px = 2.0f / std::max(width, 1.0f);
    const float py = 2.0f / std::max(height, 1.0f);
    const float arm = 6.0f;     // half length (px)
    const float thick = 1.0f;   // half thickness (px)
    const float edge = 1.0f;    // outline thickness (px)
    const glm::vec3 white(0.95f, 0.96f, 0.98f);
    const glm::vec3 black(0.03f, 0.03f, 0.05f);

    const auto addRect = [&](float x0, float x1, float y0, float y1, glm::vec3 color) {
        const std::uint32_t base = static_cast<std::uint32_t>(data.vertices.size());
        data.vertices.push_back(CrosshairVertex{{x0 * px, y0 * py}, color});
        data.vertices.push_back(CrosshairVertex{{x1 * px, y0 * py}, color});
        data.vertices.push_back(CrosshairVertex{{x1 * px, y1 * py}, color});
        data.vertices.push_back(CrosshairVertex{{x0 * px, y1 * py}, color});
        constexpr std::uint32_t pattern[6] = {0, 1, 2, 0, 2, 3};
        for (std::uint32_t index : pattern) data.indices.push_back(base + index);
    };

    // Outline first, then the bright arms on top.
    addRect(-arm - edge, arm + edge, -thick - edge, thick + edge, black);
    addRect(-thick - edge, thick + edge, -arm - edge, arm + edge, black);
    addRect(-arm, arm, -thick, thick, white);
    addRect(-thick, thick, -arm, arm, white);
    return data;
}

// Service read by CrosshairExtractSystem each frame.
struct HudSettings {
    Render::RenderResourceHandle<Render::PipelineSpec> pipeline;
    Render::RenderResourceHandle<Render::VertexBufferSpec> mesh;
    std::uint32_t indexCount = 0;
};

// Adds the crosshair as an Overlay render item, after terrain extraction and
// before Publish, so it is not cleared by RenderExtractBeginSystem.
class CrosshairExtractSystem final : public ECS::System::System {
public:
    CrosshairExtractSystem()
        : ECS::System::System("CrosshairExtractSystem", ECS::System::Phase::RenderExtract) {}

    void OnTick() override {
        auto* context = GetContext();
        if(context == nullptr) return;
        auto* world = context->GetService<Render::RenderWorld>();
        auto* hud = context->GetService<HudSettings>();
        if(world == nullptr || hud == nullptr) return;
        if(!hud->pipeline.IsValid() || !hud->mesh.IsValid() || hud->indexCount == 0) return;

        Render::RenderItem item{};
        item.mesh = hud->mesh;
        item.pipeline = hud->pipeline;
        item.model = glm::mat4(1.0f);
        item.draw.indexCount = hud->indexCount;
        item.layer = Render::RenderLayer::Overlay;
        world->Add(std::move(item));
    }
};

// Creates the crosshair pipeline and keeps its (resolution dependent) mesh up to
// date. Handles are written from the upload-queue callbacks, which the demo pumps
// on the main thread via CallBackSystem.
class CrosshairGpu {
public:
    CrosshairGpu(Render::RHIDevice* device, Render::IMeshUploadQueue* uploader)
        : device_(device), uploader_(uploader) {}

    void Start() {
        Render::CreateShaderSourceDesc vs{};
        vs.type = Render::ShaderSourceType::Vertex;
        vs.codeSource = kCrosshairVertexShader;
        vs.size = std::strlen(kCrosshairVertexShader);
        device_->async_CreateShaderSource(
            vs, [this](Render::RenderResourceHandle<Render::ShaderSourceSpec> handle) {
                vertexShader_ = handle; TryCreateProgram();
            });

        Render::CreateShaderSourceDesc fs{};
        fs.type = Render::ShaderSourceType::Fragment;
        fs.codeSource = kCrosshairFragmentShader;
        fs.size = std::strlen(kCrosshairFragmentShader);
        device_->async_CreateShaderSource(
            fs, [this](Render::RenderResourceHandle<Render::ShaderSourceSpec> handle) {
                fragmentShader_ = handle; TryCreateProgram();
            });
    }

    void EnsureMesh(float width, float height) {
        if(width == width_ && height == height_ && (meshValid_ || createRequested_)) return;
        width_ = width;
        height_ = height;

        const CrosshairMeshData data = BuildCrosshair(width, height);
        settings_.indexCount = static_cast<std::uint32_t>(data.indices.size());

        if(settings_.mesh.IsValid()) {
            Render::UpdateMeshBufferDesc desc{};
            desc.vertexCount = static_cast<std::uint32_t>(data.vertices.size());
            desc.vertexData = data.vertices.data();
            desc.vertexByteSize = data.vertices.size() * sizeof(CrosshairVertex);
            desc.indexCount = static_cast<std::uint32_t>(data.indices.size());
            desc.indexData = data.indices.data();
            desc.indexByteSize = data.indices.size() * sizeof(std::uint32_t);
            const auto handle = settings_.mesh;
            uploader_->Update(handle, desc, [](bool) {});
        } else if(!createRequested_) {
            createRequested_ = true;
            Render::CreateMeshBufferDesc desc{};
            desc.vertexLayout = CrosshairVertexLayout();
            desc.vertexCount = static_cast<std::uint32_t>(data.vertices.size());
            desc.vertexData = data.vertices.data();
            desc.vertexByteSize = data.vertices.size() * sizeof(CrosshairVertex);
            desc.indexCount = static_cast<std::uint32_t>(data.indices.size());
            desc.indexData = data.indices.data();
            desc.indexByteSize = data.indices.size() * sizeof(std::uint32_t);
            desc.usage = Render::BufferUsage::Dynamic;
            uploader_->Create(desc,
                [this](Render::RenderResourceHandle<Render::VertexBufferSpec> handle) {
                    settings_.mesh = handle;
                    meshValid_ = handle.IsValid();
                });
        }
    }

    HudSettings& Settings() { return settings_; }

private:
    void TryCreateProgram() {
        if(programRequested_ || !vertexShader_.IsValid() || !fragmentShader_.IsValid()) return;
        programRequested_ = true;

        Render::CreateGraphicShaderDesc shader{};
        shader.vertexShaderSource = vertexShader_;
        shader.fragmentShaderSource = fragmentShader_;
        device_->async_CreateGraphicShader(
            shader, [this](Render::RenderResourceHandle<Render::ShaderProgramSpec> handle) {
                shaderProgram_ = handle;
                if(!handle.IsValid()) return;

                Render::CreatePipelineDesc desc{};
                desc.spec.shaderProgram = shaderProgram_;
                desc.spec.topology = Render::PrimitiveTopology::TriangleList;
                desc.spec.expectVertexLayout = CrosshairVertexLayout();
                desc.spec.cullMode = Render::CullMode::None;
                desc.spec.depthTest = false;
                desc.spec.depthWrite = false;
                desc.spec.blendEnable = false;
                device_->async_CreatePipeline(
                    desc, [this](Render::RenderResourceHandle<Render::PipelineSpec> created) {
                        settings_.pipeline = created;
                    });
            });
    }

    Render::RHIDevice* device_ = nullptr;
    Render::IMeshUploadQueue* uploader_ = nullptr;
    HudSettings settings_{};
    Render::RenderResourceHandle<Render::ShaderSourceSpec> vertexShader_{};
    Render::RenderResourceHandle<Render::ShaderSourceSpec> fragmentShader_{};
    Render::RenderResourceHandle<Render::ShaderProgramSpec> shaderProgram_{};
    float width_ = 0.0f;
    float height_ = 0.0f;
    bool createRequested_ = false;
    bool meshValid_ = false;
    bool programRequested_ = false;
};

// Owns the uniforms and the two terrain pipelines (opaque + translucent water).
class TerrainGpu {
public:
    TerrainGpu(Render::RHIDevice* device,
               const char* vertexSource,
               const char* fragmentSource,
               bool translucent)
        : device_(device),
          vertexSource_(vertexSource),
          fragmentSource_(fragmentSource),
          translucent_(translucent) {}

    void Start() {
        Render::CreateUniformBufferDesc view{};
        view.byteSize = static_cast<std::uint32_t>(sizeof(Render::ViewConstants));
        view.usage = Render::BufferUsage::Dynamic;
        device_->async_CreateUniformBuffer(
            view,
            [this](Render::RenderResourceHandle<Render::UniformBufferSpec> handle) {
                viewUniform_ = handle;
                if (!handle.IsValid()) failed_ = true;
            });

        Render::CreateUniformBufferDesc object{};
        object.byteSize = static_cast<std::uint32_t>(sizeof(Render::ObjectConstants));
        object.usage = Render::BufferUsage::Dynamic;
        device_->async_CreateUniformBuffer(
            object,
            [this](Render::RenderResourceHandle<Render::UniformBufferSpec> handle) {
                objectUniform_ = handle;
                if (!handle.IsValid()) failed_ = true;
            });

        Render::CreateShaderSourceDesc vs{};
        vs.type = Render::ShaderSourceType::Vertex;
        vs.codeSource = vertexSource_;
        vs.size = std::strlen(vertexSource_);
        device_->async_CreateShaderSource(
            vs,
            [this](Render::RenderResourceHandle<Render::ShaderSourceSpec> handle) {
                vertexShader_ = handle;
                if (!handle.IsValid()) {
                    failed_ = true;
                    return;
                }
                TryCreateProgram();
            });

        Render::CreateShaderSourceDesc fs{};
        fs.type = Render::ShaderSourceType::Fragment;
        fs.codeSource = fragmentSource_;
        fs.size = std::strlen(fragmentSource_);
        device_->async_CreateShaderSource(
            fs,
            [this](Render::RenderResourceHandle<Render::ShaderSourceSpec> handle) {
                fragmentShader_ = handle;
                if (!handle.IsValid()) {
                    failed_ = true;
                    return;
                }
                TryCreateProgram();
            });
    }

    bool Ready() const {
        return pipeline_.IsValid()
            && viewUniform_.IsValid()
            && objectUniform_.IsValid();
    }

    bool Failed() const { return failed_; }

    Render::RenderResourceHandle<Render::PipelineSpec> Pipeline() const {
        return pipeline_;
    }
    Render::RenderResourceHandle<Render::UniformBufferSpec> ViewUniform() const {
        return viewUniform_;
    }
    Render::RenderResourceHandle<Render::UniformBufferSpec> ObjectUniform() const {
        return objectUniform_;
    }

    void Shutdown() {
        if (pipeline_.IsValid()) {
            device_->async_DeletePipeline(pipeline_);
            pipeline_ = {};
        }
        if (viewUniform_.IsValid()) {
            device_->async_DeleteUniformBuffer(viewUniform_);
            viewUniform_ = {};
        }
        if (objectUniform_.IsValid()) {
            device_->async_DeleteUniformBuffer(objectUniform_);
            objectUniform_ = {};
        }
    }

private:
    void TryCreateProgram() {
        if (programRequested_
            || !vertexShader_.IsValid()
            || !fragmentShader_.IsValid()) {
            return;
        }

        programRequested_ = true;

        Render::CreateGraphicShaderDesc shader{};
        shader.vertexShaderSource = vertexShader_;
        shader.fragmentShaderSource = fragmentShader_;

        device_->async_CreateGraphicShader(
            shader,
            [this](Render::RenderResourceHandle<Render::ShaderProgramSpec> handle) {
                shaderProgram_ = handle;
                if (!handle.IsValid()) {
                    failed_ = true;
                    return;
                }

                Render::CreatePipelineDesc desc{};
                desc.spec.shaderProgram = shaderProgram_;
                desc.spec.topology = Render::PrimitiveTopology::TriangleList;
                desc.spec.expectVertexLayout = Terrain::TerrainVertexLayout();
                desc.spec.cullMode = Render::CullMode::None;
                desc.spec.depthTest = true;
                desc.spec.depthWrite = !translucent_;
                desc.spec.depthCompare = Render::CompareOp::LessEqual;
                desc.spec.blendEnable = translucent_;
                desc.spec.blendMode = translucent_
                    ? Render::BlendMode::Alpha
                    : Render::BlendMode::Opaque;

                device_->async_CreatePipeline(
                    desc,
                    [this](Render::RenderResourceHandle<Render::PipelineSpec> created) {
                        pipeline_ = created;
                        if (!created.IsValid()) failed_ = true;
                    });
            });
    }

    Render::RHIDevice* device_ = nullptr;
    const char* vertexSource_ = nullptr;
    const char* fragmentSource_ = nullptr;
    bool translucent_ = false;
    Render::RenderResourceHandle<Render::ShaderSourceSpec> vertexShader_{};
    Render::RenderResourceHandle<Render::ShaderSourceSpec> fragmentShader_{};
    Render::RenderResourceHandle<Render::ShaderProgramSpec> shaderProgram_{};
    Render::RenderResourceHandle<Render::PipelineSpec> pipeline_{};
    Render::RenderResourceHandle<Render::UniformBufferSpec> viewUniform_{};
    Render::RenderResourceHandle<Render::UniformBufferSpec> objectUniform_{};
    bool programRequested_ = false;
    bool failed_ = false;
};

class FlyCamera {
public:
    glm::vec3 position{8.0f, 21.0f, 28.0f};
    float yaw = -90.0f;
    float pitch = -29.0f;

    void Start(GLFWwindow* window) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        firstMouse_ = true;
    }

    void Update(GLFWwindow* window, float dt) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window, &x, &y);

        if (firstMouse_) {
            lastX_ = x;
            lastY_ = y;
            firstMouse_ = false;
        }

        constexpr float sensitivity = 0.10f;
        yaw += static_cast<float>(x - lastX_) * sensitivity;
        pitch -= static_cast<float>(y - lastY_) * sensitivity;
        pitch = std::clamp(pitch, -89.0f, 89.0f);
        lastX_ = x;
        lastY_ = y;

        glm::vec3 horizontal = Forward();
        horizontal.y = 0.0f;
        if (glm::dot(horizontal, horizontal) < 0.0001f) {
            horizontal = glm::vec3(0.0f, 0.0f, -1.0f);
        } else {
            horizontal = glm::normalize(horizontal);
        }

        const glm::vec3 right = glm::normalize(
            glm::cross(horizontal, glm::vec3(0.0f, 1.0f, 0.0f)));

        float speed = 12.0f;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) speed = 34.0f;
        speed *= dt;

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) position += horizontal * speed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) position -= horizontal * speed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) position -= right * speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) position += right * speed;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) position.y += speed;
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) position.y -= speed;
    }

    glm::vec3 Forward() const {
        const float y = glm::radians(yaw);
        const float p = glm::radians(pitch);
        return glm::normalize(glm::vec3(
            std::cos(y) * std::cos(p),
            std::sin(p),
            std::sin(y) * std::cos(p)));
    }

    glm::mat4 View() const {
        return glm::lookAt(position, position + Forward(),
                           glm::vec3(0.0f, 1.0f, 0.0f));
    }

private:
    bool firstMouse_ = true;
    double lastX_ = 0.0;
    double lastY_ = 0.0;
};

void UpdateTitle(GLFWwindow* window) {
    static double previous = -1.0;
    const double now = glfwGetTime();
    if (previous >= 0.0 && now - previous < 0.25) return;
    previous = now;
    glfwSetWindowTitle(window, "ECS Terrain | WASD + mouse, LMB break block, Shift fast, Esc quit");
}

} // namespace

int main() {
    ApplicationWindow::ApplicationWindowModule windowModule;
    Render::RenderModule renderModule;
    Render::System::CallBackSystem callbackSystem;

    if (!windowModule.Startup()) {
        std::cerr << "ApplicationWindow startup failed.\n";
        return 1;
    }

    if (!renderModule.Startup()) {
        std::cerr << "Render startup failed.\n";
        windowModule.Shutdown();
        return 2;
    }

    callbackSystem.OnStart();

    auto windowWeak = ApplicationWindow::ApplicationWindowModule::GetCurrentWindow();
    auto deviceWeak = Render::RenderModule::GetRHIDevice();
    ApplicationWindow::Window* window = windowWeak.Get();
    Render::RHIDevice* device = deviceWeak.Get();

    if (window == nullptr || device == nullptr) {
        std::cerr << "Window or RHIDevice unavailable.\n";
        callbackSystem.OnEnd();
        renderModule.Shutdown();
        windowModule.Shutdown();
        return 3;
    }

    GLFWwindow* nativeWindow = window->GetNativeWindow();

    FlyCamera camera;
    camera.Start(nativeWindow);

    TerrainGpu gpu(device, kVertexShader, kFragmentShader, false);
    TerrainGpu waterGpu(device, kVertexShader, kWaterFragmentShader, true);
    Terrain::TerrainAtlasTexture atlas(device);
    gpu.Start();
    waterGpu.Start();
    atlas.Start();

    while (!window->ShouldClose()
           && !(gpu.Ready() && waterGpu.Ready() && atlas.Ready())
           && !gpu.Failed()
           && !waterGpu.Failed()
           && !atlas.Failed()) {
        window->PollEvents();
        callbackSystem.OnTick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!gpu.Ready() || !waterGpu.Ready() || !atlas.Ready()) {
        std::cerr << "Failed to initialize terrain GPU resources.\n";
        gpu.Shutdown();
        waterGpu.Shutdown();
        atlas.Shutdown();
        callbackSystem.OnEnd();
        renderModule.Shutdown();
        windowModule.Shutdown();
        return 4;
    }

    // The job pool must exist before any Scene is constructed.
    ECS::Core::ECSKernel kernel;
    kernel.Init();

    // Optional host-level generator selection (defaults to DensityField).
    //   TERRAIN_MODE=density|heightmap  TERRAIN_SEED=12345
    if (const char* mode = std::getenv("TERRAIN_MODE")) {
        if (std::strcmp(mode, "heightmap") == 0 || std::strcmp(mode, "height") == 0) {
            Terrain::SetTerrainGeneratorMode(Terrain::TerrainGeneratorMode::Heightmap);
        } else if (std::strcmp(mode, "density") == 0) {
            Terrain::SetTerrainGeneratorMode(Terrain::TerrainGeneratorMode::DensityField);
        }
    }
    // Standard name; keep accepting the historical misspelling for a while.
    const char* seedEnv = std::getenv("TERRAIN_SEED");
    if (seedEnv == nullptr) seedEnv = std::getenv("TERRAIN_SEEDD");
    if (seedEnv != nullptr) {
        Terrain::SetWorldSeed(std::strtoull(seedEnv, nullptr, 10));
    }

    Terrain::RegisterTerrainComponents();

    ECS::Core::Scene scene;

    // Editable world state: chunk registry + user edit overlay. Owned by the
    // main thread; systems receive it as a service.
    Terrain::TerrainWorld terrainWorld;
    terrainWorld.SetScene(&scene);

    auto chunkDescription = scene.CreateArchTypeDescription();
    chunkDescription->AddComponentArray<Terrain::ChunkLocation>();
    chunkDescription->AddComponentArray<Terrain::ChunkBlocks>();
    chunkDescription->AddComponentArray<Terrain::ChunkMesh>();
    chunkDescription->AddComponentArray<Terrain::ChunkRender>();
    auto chunkArchetype = scene.CreateArchType(chunkDescription, 16);

    auto viewerDescription = scene.CreateArchTypeDescription();
    viewerDescription->AddComponentArray<Terrain::TerrainViewer>();
    auto viewerArchetype = scene.CreateArchType(viewerDescription, 1);
    const auto viewerEntity = scene.CreateEntity(viewerArchetype);

    Terrain::BlockRenderRegistry blockRegistry;
    Terrain::RegisterDefaultTerrainBlocks(blockRegistry);
    Terrain::ApplyDefaultBlockAtlas(blockRegistry); // top/side/bottom atlas tiles

    Render::RHIMeshUploadQueue uploader(*device);
    Render::RenderWorld renderWorld;

    CrosshairGpu crosshair(device, &uploader);
    crosshair.Start();

    Terrain::System::RenderSettings terrainSettings{};
    terrainSettings.pipelines[0] = gpu.Pipeline();
    terrainSettings.pipelines[2] = waterGpu.Pipeline();
    terrainSettings.atlas = atlas.Handle();

    ECS::System::Pipeline pipeline;
    pipeline.Add<Terrain::System::StreamingSystem>(chunkArchetype);
    pipeline.Add<Terrain::System::GenerationSystem>();
    pipeline.Add<Terrain::System::MeshingSystem>();
    pipeline.Add<Terrain::System::MeshUploadSystem>();
    pipeline.Add<Render::System::RenderExtractBeginSystem>();
    pipeline.Add<Terrain::System::RenderExtractSystem>();
    pipeline.Add<CrosshairExtractSystem>();
    pipeline.Add<Render::System::RenderPublishSystem>();
    pipeline.Add<Render::System::RenderPipelineSystem>();
    pipeline.RunBefore<Terrain::System::StreamingSystem, Terrain::System::GenerationSystem>();
    pipeline.RunBefore<Terrain::System::GenerationSystem, Terrain::System::MeshingSystem>();
    pipeline.RunBefore<Render::System::RenderExtractBeginSystem, Terrain::System::RenderExtractSystem>();
    pipeline.RunBefore<Terrain::System::RenderExtractSystem, Render::System::RenderPublishSystem>();
    pipeline.RunBefore<Render::System::RenderExtractBeginSystem, CrosshairExtractSystem>();
    pipeline.RunBefore<CrosshairExtractSystem, Render::System::RenderPublishSystem>();

    ECS::System::Context context;
    context.scene = &scene;
    context.SetService(&blockRegistry);
    context.SetService<Render::IMeshUploadQueue>(&uploader);
    context.SetService(&renderWorld);
    context.SetService(&terrainSettings);
    context.SetService(&terrainWorld);
    context.SetService(&crosshair.Settings());

    std::uint64_t frameIndex = 1;
    double previousTime = glfwGetTime();
    bool previousLeftClick = false;

    while (!window->ShouldClose()) {
        window->PollEvents();
        callbackSystem.OnTick();

        const double now = glfwGetTime();
        const float deltaSeconds = std::clamp(
            static_cast<float>(now - previousTime), 0.0f, 0.05f);
        previousTime = now;

        camera.Update(nativeWindow, deltaSeconds);

        // Crosshair pick: cast the camera ray into the live ChunkBlocks every
        // frame (the DDA touches ~6-10 voxels). The crosshair is always the
        // screen centre, so origin/direction come straight from the camera.
        const auto provider = [&terrainWorld](int x, int y, int z) {
            return terrainWorld.GetBlockWorld(x, y, z);
        };
        const Terrain::VoxelRaycastHit hit = Terrain::RaycastBlocks(
            camera.position, camera.Forward(), kBreakReach, provider);

        // Left-click edge breaks the selected block (never repeats while held).
        const bool leftDown =
            glfwGetMouseButton(nativeWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const bool leftPressed = leftDown && !previousLeftClick;
        previousLeftClick = leftDown;
        if (leftPressed && hit.hit) {
            // SetBlockWorld records the override, updates the live chunk and
            // marks the chunk (+ boundary neighbour) for async remeshing.
            terrainWorld.SetBlockWorld(
                hit.block, static_cast<Terrain::BlockId>(Terrain::Block::Air));
        }

        auto viewer = scene.GetActiveComponent<Terrain::TerrainViewer>(viewerEntity.GetID());
        if (auto* component = viewer.Get()) component->position = camera.position;
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(nativeWindow, &width, &height);
        if (width <= 0 || height <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            continue;
        }

        crosshair.EnsureMesh(static_cast<float>(width), static_cast<float>(height));

        Render::RHICommand::BeginFrame begin{};
        begin.frameIndex = frameIndex;
        begin.framebufferWidth = static_cast<std::uint32_t>(width);
        begin.framebufferHeight = static_cast<std::uint32_t>(height);
        begin.clearColor = kSkyColor;

        auto frame = device->BeginFrame(begin);

        Render::RHICommand::SetViewport viewport{};
        viewport.width = static_cast<std::uint32_t>(width);
        viewport.height = static_cast<std::uint32_t>(height);
        frame.SetViewport(viewport);

        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const glm::mat4 projection =
            glm::perspective(glm::radians(67.0f), aspect, 0.1f, 500.0f);

        Render::RenderView renderView{};
        renderView.viewProjection = projection * camera.View();
        renderView.cameraPosition = glm::vec4(camera.position, 1.0f);
        renderView.viewUniform = gpu.ViewUniform();
        renderView.objectUniform = gpu.ObjectUniform();

        Render::RenderFrameService frameService{&frame, renderView};

        context.frameIndex = frameIndex;
        context.deltaSeconds = deltaSeconds;
        context.SetService(&frameService);

        pipeline.Tick(context);

        if (!frame.End(true)) {
            std::cerr << "Failed to end frame.\n";
            break;
        }

        if (!device->async_SubmitFrameCommands(frame.GetCommandBuffer())) {
            std::cerr << "Failed to submit frame.\n";
            break;
        }

        UpdateTitle(nativeWindow);
        ++frameIndex;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    pipeline.Stop(context);
    callbackSystem.OnTick();
    callbackSystem.OnEnd();

    gpu.Shutdown();
    waterGpu.Shutdown();
    atlas.Shutdown();

    renderModule.Shutdown();
    windowModule.Shutdown();
    return 0;
}
