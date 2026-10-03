#include "ForestFire/Public/forest_renderer.h"
#include "ForestFire/Public/forest_module.h"
#include "Render/module.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ForestFire {
namespace {

constexpr float CanvasWidth = 1280.0f;
constexpr float CanvasHeight = 800.0f;
const glm::vec4 Background{0.025f, 0.045f, 0.052f, 1.0f};
const glm::vec4 Panel{0.046f, 0.080f, 0.085f, 1.0f};
const glm::vec4 Ink{0.86f, 0.93f, 0.88f, 1.0f};
const glm::vec4 Muted{0.38f, 0.54f, 0.51f, 1.0f};
const glm::vec4 Green{0.24f, 0.76f, 0.48f, 1.0f};
const glm::vec4 Orange{1.0f, 0.47f, 0.16f, 1.0f};

struct Rectangle { float x, y, width, height; };

Rectangle GridBounds(std::uint32_t columns, std::uint32_t rows) {
    if (columns == 0 || rows == 0) return {32, 130, 0, 0};
    const float cell = std::min(880.0f / columns, 584.0f / rows);
    const float width = cell * columns, height = cell * rows;
    return {32.0f + (880.0f - width) * 0.5f,
            130.0f + (584.0f - height) * 0.5f, width, height};
}

struct CanvasViewport { int x, y, width, height; };

CanvasViewport Viewport(int width, int height) {
    const double scale = std::min(width / double(CanvasWidth), height / double(CanvasHeight));
    const int viewWidth = std::max(1, static_cast<int>(CanvasWidth * scale));
    const int viewHeight = std::max(1, static_cast<int>(CanvasHeight * scale));
    return {(width - viewWidth) / 2, (height - viewHeight) / 2, viewWidth, viewHeight};
}

// Small built-in 5x7 font: the demo has no filesystem or font-library dependency.
std::array<unsigned char, 7> Glyph(char ch) {
    switch (ch) {
    case 'A': return {14,17,17,31,17,17,17};
    case 'B': return {30,17,17,30,17,17,30};
    case 'C': return {14,17,16,16,16,17,14};
    case 'D': return {30,17,17,17,17,17,30};
    case 'E': return {31,16,16,30,16,16,31};
    case 'F': return {31,16,16,30,16,16,16};
    case 'G': return {14,17,16,23,17,17,15};
    case 'H': return {17,17,17,31,17,17,17};
    case 'I': return {14,4,4,4,4,4,14};
    case 'J': return {7,2,2,2,18,18,12};
    case 'K': return {17,18,20,24,20,18,17};
    case 'L': return {16,16,16,16,16,16,31};
    case 'M': return {17,27,21,21,17,17,17};
    case 'N': return {17,25,21,19,17,17,17};
    case 'O': return {14,17,17,17,17,17,14};
    case 'P': return {30,17,17,30,16,16,16};
    case 'Q': return {14,17,17,17,21,18,13};
    case 'R': return {30,17,17,30,20,18,17};
    case 'S': return {15,16,16,14,1,1,30};
    case 'T': return {31,4,4,4,4,4,4};
    case 'U': return {17,17,17,17,17,17,14};
    case 'V': return {17,17,17,17,17,10,4};
    case 'W': return {17,17,17,21,21,21,10};
    case 'X': return {17,17,10,4,10,17,17};
    case 'Y': return {17,17,10,4,4,4,4};
    case 'Z': return {31,1,2,4,8,16,31};
    case '0': return {14,17,19,21,25,17,14};
    case '1': return {4,12,4,4,4,4,14};
    case '2': return {14,17,1,2,4,8,31};
    case '3': return {30,1,1,14,1,1,30};
    case '4': return {2,6,10,18,31,2,2};
    case '5': return {31,16,16,30,1,1,30};
    case '6': return {14,16,16,30,17,17,14};
    case '7': return {31,1,2,4,8,8,8};
    case '8': return {14,17,17,14,17,17,14};
    case '9': return {14,17,17,15,1,1,14};
    case '.': return {0,0,0,0,0,12,12};
    case ':': return {0,12,12,0,12,12,0};
    case '/': return {1,1,2,4,8,16,16};
    case '-': return {0,0,0,31,0,0,0};
    case '+': return {0,4,4,31,4,4,0};
    case '%': return {25,25,2,4,8,19,19};
    case '[': return {14,8,8,8,8,8,14};
    case ']': return {14,2,2,2,2,2,14};
    default: return {};
    }
}

std::string Decimal(double value, int precision = 1) {
    char buffer[80];
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
    return buffer;
}

Render::VertexLayout Layout() {
    Render::VertexLayout result;
    result.typeSlots = {Render::VertexFieldType::Vec2, Render::VertexFieldType::Vec4};
    return result;
}

constexpr char VertexSource[] = R"GLSL(#version 450 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
out vec4 vColor;
void main() {
    gl_Position = vec4(position.x / 640.0 - 1.0, 1.0 - position.y / 400.0, 0.0, 1.0);
    vColor = color;
}
)GLSL";

constexpr char FragmentSource[] = R"GLSL(#version 450 core
in vec4 vColor;
layout(location = 0) out vec4 color;
void main() { color = vColor; }
)GLSL";

} // namespace

struct ForestRenderer::Impl {
    struct Vertex { glm::vec2 position; glm::vec4 color; };
    static_assert(sizeof(Vertex) == sizeof(float) * 6, "RHI vertex layout must remain packed");

    explicit Impl(ObjectWeakPtr<Render::RHIDevice> value) : device(std::move(value)) {}
    ObjectWeakPtr<Render::RHIDevice> device;
    Render::RenderResourceHandle<Render::VertexBufferSpec> mesh;
    Render::RenderResourceHandle<Render::PipelineSpec> pipeline;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::string error;
    bool initialized = false, busy = false, stopped = false;
    std::uint64_t completed = 0;

    void Rect(float x, float y, float width, float height, glm::vec4 color) {
        if (width <= 0 || height <= 0) return;
        const auto first = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back({{x, y}, color});
        vertices.push_back({{x + width, y}, color});
        vertices.push_back({{x + width, y + height}, color});
        vertices.push_back({{x, y + height}, color});
        for (std::uint32_t index : {0, 1, 2, 0, 2, 3}) indices.push_back(first + index);
    }

    void Text(const std::string& value, float x, float y, float pixel, glm::vec4 color) {
        for (char ch : value) {
            const auto glyph = Glyph(ch);
            for (unsigned row = 0; row < 7; ++row) {
                for (unsigned column = 0; column < 5; ++column) {
                    if ((glyph[row] & (1u << (4 - column))) != 0)
                        Rect(x + column * pixel, y + row * pixel, pixel, pixel, color);
                }
            }
            x += pixel * 6.0f;
        }
    }

    void Build(const ForestModule& forest) {
        vertices.clear();
        indices.clear();
        const auto& config = forest.Config();
        const auto& stats = forest.Stats();
        const auto cells = forest.Snapshot();
        const double count = std::max(1.0, double(config.width) * config.height);
        const auto bounds = GridBounds(config.width, config.height);
        const float cellSize = bounds.width / config.width;

        Rect(0, 0, CanvasWidth, CanvasHeight, Background);
        Rect(32, 24, 4, 55, Green);
        Text("FOREST / FIRE", 52, 27, 4.5f, Ink);
        Text("CELLULAR AUTOMATON", 54, 69, 1.7f, Muted);
        Rect(1100, 35, 148, 33, Panel);
        Rect(1112, 46, 10, 10, forest.IsPaused() ? Orange : Green);
        Text(forest.IsPaused() ? "PAUSED" : "RUNNING", 1135, 46, 1.7f, Ink);
        Text(std::to_string(config.width) + " X " + std::to_string(config.height) + " CELLS", 32, 107, 1.6f, Muted);
        Text("GENERATION " + std::to_string(stats.tick), 624, 107, 1.6f, Muted);
        Rect(30, 128, 884, 588, Panel);
        Rect(bounds.x, bounds.y, bounds.width, bounds.height, {0.057f, 0.090f, 0.081f, 1});

        const float gap = cellSize >= 4.0f ? 0.25f : 0.0f;
        for (const auto& cell : cells) {
            if (cell.state == CellState::Empty) continue;
            const std::uint32_t hash = cell.x * 73856093u ^ cell.y * 19349663u;
            const float shade = float(hash % 100u) / 100.0f;
            glm::vec4 color;
            if (cell.state == CellState::Tree) {
                color = {0.085f + shade * 0.07f, 0.31f + shade * 0.20f, 0.20f + shade * 0.08f, 1};
            } else {
                const float life = float(cell.burnTicksRemaining) / std::max(1u, config.burnTicks);
                color = {1.0f, 0.20f + 0.58f * life, 0.045f + 0.10f * life, 1};
            }
            Rect(bounds.x + cell.x * cellSize + gap,
                 bounds.y + cell.y * cellSize + gap,
                 cellSize - gap * 2, cellSize - gap * 2, color);
        }

        Rect(940, 128, 308, 588, Panel);
        Text("LIVE ECOSYSTEM", 960, 150, 2, Ink);
        Rect(960, 180, 268, 1, Muted * glm::vec4(0.4f, 0.4f, 0.4f, 1));
        Text("TREES", 960, 202, 1.7f, Green);
        Text(std::to_string(stats.trees), 960, 225, 3.4f, Ink);
        Text(Decimal(stats.trees * 100.0 / count) + "%", 1140, 233, 1.6f, Green);
        Text("BURNING", 960, 269, 1.7f, Orange);
        Text(std::to_string(stats.burning), 960, 292, 3.4f, Ink);
        Text(Decimal(stats.burning * 100.0 / count) + "%", 1140, 300, 1.6f, Orange);
        Text("EMPTY", 960, 336, 1.7f, Muted);
        Text(std::to_string(stats.empty), 960, 359, 3.4f, Ink);

        Rect(960, 409, 268, 8, Background);
        const float treeWidth = float(stats.trees / count) * 268.0f;
        const float fireWidth = float(stats.burning / count) * 268.0f;
        Rect(960, 409, treeWidth, 8, Green);
        Rect(960 + treeWidth, 409, fireWidth, 8, Orange);
        Text("MODEL / PER TICK", 960, 447, 1.7f, Ink);
        Text("GROW", 960, 478, 1.6f, Muted);
        Text(Decimal(config.growthProbability * 100, 3) + "%", 1122, 478, 1.6f, Ink);
        Text("LIGHTNING", 960, 502, 1.6f, Muted);
        Text(Decimal(config.lightningProbability * 100, 3) + "%", 1122, 502, 1.6f, Ink);
        Text("SPREAD", 960, 526, 1.6f, Muted);
        Text(Decimal(config.spreadProbability * 100, 1) + "%", 1122, 526, 1.6f, Ink);
        Text("TICKS / SEC", 960, 561, 1.6f, Muted);
        Text(Decimal(1.0 / config.stepSeconds), 1141, 561, 1.6f, Ink);
        Text("SEED " + std::to_string(config.seed), 960, 596, 1.6f, Muted);
        Text(config.neighborhood == Neighborhood::Eight ? "8 NEIGHBORS" : "4 NEIGHBORS", 960, 622, 1.6f, Muted);
        Text(config.boundary == BoundaryMode::Wrap ? "WRAPPED EDGES" : "FINITE EDGES", 960, 646, 1.6f, Muted);
        Text("SHIFT: WIDE BRUSH", 960, 686, 1.5f, Ink);

        Text("LMB IGNITE   RMB PLANT   MMB ERASE", 32, 742, 1.8f, Ink);
        Text("SPACE PAUSE   N STEP   R RESET   +/- SPEED   ESC EXIT", 32, 773, 1.45f, Muted);
        Rect(960, 741, 10, 10, Green); Text("TREE", 980, 741, 1.5f, Muted);
        Rect(1045, 741, 10, 10, Orange); Text("FIRE", 1065, 741, 1.5f, Muted);
        Rect(1130, 741, 10, 10, {0.13f, 0.19f, 0.16f, 1}); Text("EMPTY", 1150, 741, 1.5f, Muted);
    }
};

ForestRenderer::ForestRenderer(ObjectWeakPtr<Render::RHIDevice> device)
    : impl_(std::make_shared<Impl>(std::move(device))) {}

ForestRenderer::~ForestRenderer() { Shutdown(); }

void ForestRenderer::Initialize() {
    const auto self = impl_;
    if (self->initialized || self->stopped) return;
    self->initialized = true;
    if (!self->device || !self->device->IsRunning()) {
        self->error = "Forest renderer requires a running Render module";
        return;
    }
    const Impl::Vertex initial[3]{};
    Render::CreateMeshBufferDesc mesh;
    mesh.vertexLayout = Layout();
    mesh.vertexCount = 3;
    mesh.vertexData = initial;
    mesh.vertexByteSize = sizeof(initial);
    mesh.usage = Render::BufferUsage::Stream;
    self->device->async_CreateMeshBuffer(mesh, [self](auto handle) {
        if (self->stopped) {
            if (handle.IsValid() && self->device) self->device->async_DeleteVertexBuffer(handle);
            return;
        }
        self->mesh = handle;
        if (!handle.IsValid()) self->error = "Forest mesh creation failed";
    });
    self->device->async_CreateShaderSource(
        {Render::ShaderSourceType::Vertex, VertexSource, std::strlen(VertexSource)}, [self](auto vertex) {
        if (self->stopped) return;
        if (!vertex.IsValid()) { self->error = "Forest vertex shader compilation failed"; return; }
        self->device->async_CreateShaderSource(
            {Render::ShaderSourceType::Fragment, FragmentSource, std::strlen(FragmentSource)}, [self, vertex](auto fragment) {
            if (self->stopped) return;
            if (!fragment.IsValid()) { self->error = "Forest fragment shader compilation failed"; return; }
            Render::CreateGraphicShaderDesc shader;
            shader.vertexShaderSource = vertex;
            shader.fragmentShaderSource = fragment;
            self->device->async_CreateGraphicShader(shader, [self](auto program) {
                if (self->stopped) return;
                if (!program.IsValid()) { self->error = "Forest shader link failed"; return; }
                Render::CreatePipelineDesc pipeline;
                pipeline.spec.shaderProgram = program;
                pipeline.spec.expectVertexLayout = Layout();
                pipeline.spec.cullMode = Render::CullMode::None;
                pipeline.spec.depthTest = false;
                pipeline.spec.depthWrite = false;
                pipeline.spec.blendEnable = false;
                self->device->async_CreatePipeline(pipeline, [self](auto handle) {
                    if (self->stopped) {
                        if (handle.IsValid() && self->device) self->device->async_DeletePipeline(handle);
                        return;
                    }
                    self->pipeline = handle;
                    if (!handle.IsValid()) self->error = "Forest pipeline creation failed";
                });
            });
        });
    });
}

bool ForestRenderer::Ready() const {
    return !impl_->stopped && impl_->error.empty() && impl_->mesh.IsValid() && impl_->pipeline.IsValid();
}
bool ForestRenderer::Busy() const { return impl_->busy; }
const std::string& ForestRenderer::Error() const { return impl_->error; }
std::uint64_t ForestRenderer::CompletedFrames() const { return impl_->completed; }

bool ForestRenderer::Draw(const ForestModule& forest, int width, int height, bool present) {
    if (!Ready() || Busy() || width < 1 || height < 1 || !forest.IsStarted()) return false;
    const auto self = impl_;
    self->Build(forest);
    Render::UpdateMeshBufferDesc update;
    update.vertexCount = static_cast<std::uint32_t>(self->vertices.size());
    update.vertexData = self->vertices.data();
    update.vertexByteSize = self->vertices.size() * sizeof(Impl::Vertex);
    update.indexCount = static_cast<std::uint32_t>(self->indices.size());
    update.indexData = self->indices.data();
    update.indexByteSize = self->indices.size() * sizeof(std::uint32_t);
    self->busy = true;
    self->device->async_UpdateMeshBuffer(self->mesh, update,
        [self, width, height, present, indexCount = update.indexCount](bool success) {
        if (self->stopped) { self->busy = false; return; }
        if (!success) { self->busy = false; self->error = "Forest mesh upload failed"; return; }

        Render::RHICommand::BeginFrame begin;
        begin.frameIndex = self->completed;
        begin.framebufferWidth = width;
        begin.framebufferHeight = height;
        begin.clearColor = Background;
        auto frame = self->device->BeginFrame(begin);
        const auto viewport = Viewport(width, height);
        frame.SetViewport({viewport.x, viewport.y,
            static_cast<std::uint32_t>(viewport.width), static_cast<std::uint32_t>(viewport.height)});
        frame.BindPipeline(self->pipeline);
        frame.BindMesh(self->mesh);
        Render::RHICommand::DrawIndexed draw;
        draw.indexCount = indexCount;
        frame.DrawIndexed(draw);
        frame.End(present);
        if (!self->device->async_SubmitFrameCommands(frame.GetCommandBuffer(), [self] {
            self->busy = false;
            ++self->completed;
        })) {
            self->busy = false;
            self->error = "Forest frame submission failed";
        }
    });
    return true;
}

void ForestRenderer::Shutdown() {
    const auto self = impl_;
    if (self->stopped) return;
    self->stopped = true;
    // The host should retire Busy() before shutdown. Shared callback state also
    // makes early shutdown safe while asynchronous initialization is pending.
    if (self->device && self->device->IsRunning()) {
        if (self->mesh.IsValid()) self->device->async_DeleteVertexBuffer(self->mesh);
        if (self->pipeline.IsValid()) self->device->async_DeletePipeline(self->pipeline);
    }
    self->mesh = {};
    self->pipeline = {};
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> ForestRenderer::PickCell(
    double x, double y, int width, int height, std::uint32_t columns, std::uint32_t rows) {
    if (width < 1 || height < 1 || columns == 0 || rows == 0 || !std::isfinite(x) || !std::isfinite(y)) return {};
    const auto viewport = Viewport(width, height);
    // The OpenGL viewport y offset is measured from the bottom of the framebuffer.
    const int top = height - viewport.y - viewport.height;
    x = (x - viewport.x) * CanvasWidth / viewport.width;
    y = (y - top) * CanvasHeight / viewport.height;
    const auto bounds = GridBounds(columns, rows);
    if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height) return {};
    const auto column = static_cast<std::uint32_t>((x - bounds.x) * columns / bounds.width);
    const auto row = static_cast<std::uint32_t>((y - bounds.y) * rows / bounds.height);
    if (column >= columns || row >= rows) return {};
    return std::make_pair(column, row);
}

} // namespace ForestFire
