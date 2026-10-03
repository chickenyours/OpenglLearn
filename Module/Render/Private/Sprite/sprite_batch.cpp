#include "Render/Public/Sprite/sprite_batch.h"
#include "Render/Public/Sprite/sprite_batch_geometry.h"
#include "Render/Private/rhi_device.h"
#include <stb_image.h>
#include <array>
#include <fstream>
#include <map>
#include <utility>

namespace Render {
namespace {
using SpriteDetail::AtlasTile;
using SpriteDetail::SpriteVertex;

AtlasTile ReadImage(const std::filesystem::path& path) {
    // Reading via filesystem::path avoids Windows ANSI filename limitations.
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("Cannot open sprite image: " + path.filename().string());
    const auto byteSize = stream.tellg();
    if (byteSize <= 0 || byteSize > 128 * 1024 * 1024)
        throw std::runtime_error("Invalid sprite image file size");
    std::vector<unsigned char> encoded(static_cast<size_t>(byteSize));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(encoded.data()), byteSize))
        throw std::runtime_error("Cannot read sprite image");
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(encoded.data(), int(encoded.size()), &width, &height, &channels) ||
        width < 1 || height < 1 || width > 16384 || height > 16384 || uint64_t(width) * height > 64 * 1024 * 1024)
        throw std::runtime_error("Invalid sprite image dimensions");
    auto* decoded = stbi_load_from_memory(encoded.data(), int(encoded.size()), &width, &height, &channels, 4);
    if (!decoded) throw std::runtime_error("Sprite image decoding failed");
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> owner(decoded, stbi_image_free);
    return {width, height, std::vector<unsigned char>(decoded, decoded + size_t(width) * height * 4)};
}

// Original compact bitmap glyphs, one byte per row (five low bits).
constexpr std::string_view GlyphCharacters = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.-,:/!?+()[]=%<>_#*$";
constexpr std::array<unsigned char, 7> GlyphRows[] = {
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},
    {31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},
    {16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},
    {30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,27,17},
    {17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
    {0,0,0,0,0,6,6},{0,0,0,31,0,0,0},{0,0,0,0,6,4,8},
    {0,6,6,0,6,6,0},{1,2,2,4,8,8,16},{4,4,4,4,4,0,4},
    {14,17,1,2,4,0,4},{0,4,4,31,4,4,0},{2,4,8,8,8,4,2},
    {8,4,2,2,2,4,8},{14,8,8,8,8,8,14},{14,2,2,2,2,2,14},
    {0,0,31,0,31,0,0},{25,25,2,4,8,19,19},{2,4,8,16,8,4,2},
    {8,4,2,1,2,4,8},{0,0,0,0,0,0,31},{10,10,31,10,31,10,10},
    {0,21,14,31,14,21,0},{4,15,20,14,5,30,4}
};
static_assert(std::size(GlyphRows) == GlyphCharacters.size());

AtlasTile MakeGlyph(size_t glyph) {
    AtlasTile tile{5, 7, std::vector<unsigned char>(5 * 7 * 4, 255)};
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 5; ++x)
            tile.rgba[(y * 5 + x) * 4 + 3] = (GlyphRows[glyph][y] & (1 << (4 - x))) ? 255 : 0;
    return tile;
}

constexpr const char* VertexShader = R"(#version 450 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in vec4 color;
layout(location=3) in float nearest;
out vec2 vUV;out vec4 vColor;flat out float vNearest;
void main(){gl_Position=vec4(position,0,1);vUV=texcoord;vColor=color;vNearest=nearest;})";
constexpr const char* FragmentShader = R"(#version 450 core
layout(binding=0) uniform sampler2D atlas;
in vec2 vUV;in vec4 vColor;flat in float vNearest;out vec4 color;
void main(){
vec4 texel=vNearest>0.5?texelFetch(atlas,ivec2(vUV*textureSize(atlas,0)),0):textureLod(atlas,vUV,0.0);
color=texel*vColor;})";
}

struct SpriteBatch2D::State {
    ObjectWeakPtr<RHIDevice> device;
    RenderResourceHandle<RHITextureSpec> atlas;
    RenderResourceHandle<VertexBufferSpec> mesh;
    RenderResourceHandle<PipelineSpec> pipeline;
    RenderResourceHandle<ShaderProgramSpec> program;
    RenderResourceHandle<ShaderSourceSpec> vertexSource, fragmentSource;
    std::map<std::string, std::vector<glm::vec4>, std::less<>> frames;
    glm::vec4 whiteRegion{};
    std::array<glm::vec4, GlyphCharacters.size()> glyphRegions{};
    std::vector<SpriteVertex> vertices;
    std::vector<uint32_t> indices;
    glm::vec2 halfExtent{1, 1}, cameraCenter{};
    ObjectWeakPtr<RHIFrameCommandBuffer> flushedFrame;
    bool closed = false, inFlight = false, begun = false, flushed = false;
    std::string error;

    explicit State(ObjectWeakPtr<RHIDevice> value) : device(std::move(value)) {}
    ~State() {
        // Async callbacks and recorded/submitted frames retain this state. No
        // callback captures the SpriteBatch2D object; Shutdown is safe mid-init.
        if (!device || !device->IsRunning()) return;
        if (pipeline.IsValid()) device->async_DeletePipeline(pipeline);
        if (mesh.IsValid()) device->async_DeleteVertexBuffer(mesh);
        if (atlas.IsValid()) device->async_DeleteTexture(atlas);
        if (program.IsValid()) device->async_DeleteShaderProgram(program);
        if (vertexSource.IsValid()) device->async_DeleteShaderSource(vertexSource);
        if (fragmentSource.IsValid()) device->async_DeleteShaderSource(fragmentSource);
    }
    bool Active() const { return !closed && device && device->IsRunning(); }
    void Fail(std::string message) { if (error.empty()) error = std::move(message); }

    static void CreateResources(const std::shared_ptr<State>& state, const SpriteDetail::AtlasPixels& pixels) {
        CreateRHITextureSpec texture;
        texture.width = pixels.width;
        texture.height = pixels.height;
        texture.data = pixels.rgba.data();
        texture.filterMode = RHIFilterMode::Linear;
        texture.addressMode = RHIAddressMode::ClampToEdge;
        state->device->async_CreateTexture(texture, [state](auto handle) {
            state->atlas = handle;
            if (!handle.IsValid()) state->Fail("Sprite atlas GPU creation failed");
        });
        const SpriteVertex initial[3]{};
        CreateMeshBufferDesc mesh;
        mesh.vertexLayout.typeSlots = {VertexFieldType::Vec2, VertexFieldType::Vec2, VertexFieldType::Vec4, VertexFieldType::Float};
        mesh.vertexCount = 3;
        mesh.vertexData = initial;
        mesh.vertexByteSize = sizeof(initial);
        mesh.usage = BufferUsage::Stream;
        state->device->async_CreateMeshBuffer(mesh, [state](auto handle) {
            state->mesh = handle;
            if (!handle.IsValid()) state->Fail("Sprite mesh GPU creation failed");
        });
        state->device->async_CreateShaderSource({ShaderSourceType::Vertex, VertexShader, std::strlen(VertexShader)}, [state](auto vertex) {
            state->vertexSource = vertex;
            if (!vertex.IsValid()) { state->Fail("Sprite vertex shader compilation failed"); return; }
            if (!state->Active()) return;
            state->device->async_CreateShaderSource({ShaderSourceType::Fragment, FragmentShader, std::strlen(FragmentShader)}, [state](auto fragment) {
                state->fragmentSource = fragment;
                if (!fragment.IsValid()) { state->Fail("Sprite fragment shader compilation failed"); return; }
                if (!state->Active()) return;
                CreateGraphicShaderDesc shader{};
                shader.vertexShaderSource = state->vertexSource;
                shader.fragmentShaderSource = state->fragmentSource;
                state->device->async_CreateGraphicShader(shader, [state](auto program) {
                    state->program = program;
                    if (!program.IsValid()) { state->Fail("Sprite shader linking failed"); return; }
                    if (!state->Active()) return;
                    CreatePipelineDesc pipeline;
                    pipeline.spec.shaderProgram = program;
                    pipeline.spec.cullMode = CullMode::None;
                    pipeline.spec.depthTest = false;
                    pipeline.spec.depthWrite = false;
                    pipeline.spec.blendEnable = true;
                    pipeline.spec.blendMode = BlendMode::Alpha;
                    state->device->async_CreatePipeline(pipeline, [state](auto handle) {
                        state->pipeline = handle;
                        if (!handle.IsValid()) state->Fail("Sprite pipeline creation failed");
                    });
                });
            });
        });
    }
};

SpriteBatch2D::SpriteBatch2D(ObjectWeakPtr<RHIDevice> device) : device_(std::move(device)) {}
SpriteBatch2D::~SpriteBatch2D() { Shutdown(); }

bool SpriteBatch2D::Initialize(const std::vector<SpriteImage>& images) {
    Shutdown();
    state_ = std::make_shared<State>(device_);
    auto& state = *state_;
    if (!state.Active()) { state.Fail("Sprite batch requires a running RHI device"); return false; }
    try {
        std::vector<AtlasTile> tiles;
        std::vector<std::pair<std::string, size_t>> starts;
        for (const auto& image : images) {
            if (image.name.empty() || state.frames.contains(image.name) || image.columns < 1 || image.rows < 1 ||
                image.columns > 256 || image.rows > 256 || image.columns * image.rows > 4096)
                throw std::runtime_error("Invalid or duplicate sprite image definition: " + image.name);
            auto source = ReadImage(image.path);
            if (source.width % image.columns || source.height % image.rows)
                throw std::runtime_error("Sprite sheet dimensions must divide into whole cells: " + image.name);
            const int width = source.width / image.columns, height = source.height / image.rows;
            if (width < 1 || height < 1 || width > 4092 || height > 4092)
                throw std::runtime_error("Sprite sheet cell exceeds atlas dimensions: " + image.name);
            starts.emplace_back(image.name, tiles.size());
            state.frames[image.name].resize(size_t(image.columns) * image.rows);
            for (int row = 0; row < image.rows; ++row) {
                for (int column = 0; column < image.columns; ++column) {
                    AtlasTile tile{width, height, std::vector<unsigned char>(size_t(width) * height * 4)};
                    for (int y = 0; y < height; ++y) {
                        const auto* src = source.rgba.data() + (size_t(row * height + y) * source.width + column * width) * 4;
                        std::memcpy(tile.rgba.data() + size_t(y) * width * 4, src, size_t(width) * 4);
                    }
                    tiles.push_back(std::move(tile));
                }
            }
        }
        const size_t whiteIndex = tiles.size();
        tiles.push_back({1, 1, {255, 255, 255, 255}});
        const size_t fontIndex = tiles.size();
        for (size_t i = 0; i < GlyphCharacters.size(); ++i) tiles.push_back(MakeGlyph(i));
        auto atlas = SpriteDetail::PackAtlas(tiles);
        for (const auto& [name, start] : starts) {
            auto& frames = state.frames.at(name);
            std::copy_n(atlas.regions.begin() + start, frames.size(), frames.begin());
        }
        state.whiteRegion = atlas.regions[whiteIndex];
        std::copy_n(atlas.regions.begin() + fontIndex, GlyphCharacters.size(), state.glyphRegions.begin());
        state.vertices.reserve(4096);
        state.indices.reserve(6144);
        State::CreateResources(state_, atlas);
        return true;
    } catch (const std::exception& exception) {
        state.Fail(exception.what());
        return false;
    }
}

bool SpriteBatch2D::Ready() const {
    return state_ && state_->Active() && state_->error.empty() && !state_->inFlight &&
        state_->atlas.IsValid() && state_->mesh.IsValid() && state_->pipeline.IsValid();
}

bool SpriteBatch2D::Begin(float halfWidth, float halfHeight, glm::vec2 cameraCenter) {
    if (!Ready() || state_->flushed) return false;
    if (!(halfWidth > 0 && halfHeight > 0) || !std::isfinite(halfWidth) || !std::isfinite(halfHeight) ||
        !std::isfinite(cameraCenter.x) || !std::isfinite(cameraCenter.y)) {
        state_->Fail("Sprite view must have positive finite extents and a finite center");
        return false;
    }
    state_->halfExtent = {halfWidth, halfHeight};
    state_->cameraCenter = cameraCenter;
    state_->vertices.clear();
    state_->indices.clear();
    state_->begun = true;
    return true;
}

void SpriteBatch2D::Quad(glm::vec2 center, glm::vec2 size, float radians, glm::vec4 region,
    glm::vec4 tint, bool flipX, bool flipY, bool nearest) {
    if (!state_ || !state_->begun || state_->flushed || state_->inFlight) return;
    if (!(size.x > 0 && size.y > 0) || !std::isfinite(size.x) || !std::isfinite(size.y) ||
        !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(radians)) return;
    const float cosine = std::cos(radians), sine = std::sin(radians);
    AffineQuad(center,{cosine*size.x,sine*size.x},{-sine*size.y,cosine*size.y},region,tint,flipX,flipY,nearest);
}

bool SpriteBatch2D::AffineQuad(glm::vec2 center, glm::vec2 axisX, glm::vec2 axisY, glm::vec4 region,
    glm::vec4 tint, bool flipX, bool flipY, bool nearest) {
    if (!state_ || !state_->begun || state_->flushed || state_->inFlight) return false;
    if (!SpriteDetail::Finite(center) || !SpriteDetail::Finite(tint) || !SpriteDetail::ValidAffineBasis(axisX,axisY)) {
        state_->Fail("Sprite affine transform and tint must be finite with a nonsingular basis");
        return false;
    }
    // Exact AABB of the parallelogram. A circle derived from the original size
    // would incorrectly cull sheared children near a view edge.
    const glm::dvec2 extent=(glm::abs(glm::dvec2(axisX))+glm::abs(glm::dvec2(axisY)))*.5;
    const auto relative=glm::dvec2(center)-glm::dvec2(state_->cameraCenter);
    if (relative.x+extent.x < -state_->halfExtent.x || relative.x-extent.x > state_->halfExtent.x ||
        relative.y+extent.y < -state_->halfExtent.y || relative.y-extent.y > state_->halfExtent.y) return true;
    if (state_->vertices.size() > std::numeric_limits<uint32_t>::max() - 4 ||
        state_->indices.size() > std::numeric_limits<uint32_t>::max() - 6) {
        state_->Fail("Sprite batch exceeded the RHI index range");
        return false;
    }
    try {
        const auto vertices = SpriteDetail::MakeAffineQuad(center,axisX,axisY,region,tint,
            state_->halfExtent,state_->cameraCenter,flipX,flipY,nearest);
        const auto first = uint32_t(state_->vertices.size());
        state_->vertices.insert(state_->vertices.end(), vertices.begin(), vertices.end());
        for (uint32_t index : {0, 1, 2, 0, 2, 3}) state_->indices.push_back(first + index);
        return true;
    } catch (const std::invalid_argument& error) {
        state_->Fail(error.what());
        return false;
    }
}

bool SpriteBatch2D::Sprite(std::string_view name, glm::vec2 center, glm::vec2 size, float radians,
    glm::vec4 tint, bool flipX, bool flipY, int frame) {
    return SpriteRegion(name, center, size, {0, 0, 1, 1}, radians, tint, flipX, flipY, frame);
}

bool SpriteBatch2D::SpriteAffine(std::string_view name, glm::vec2 center, glm::vec2 axisX, glm::vec2 axisY,
    glm::vec4 tint, bool flipX, bool flipY, int frame) {
    if (!state_ || !state_->begun || state_->flushed || state_->inFlight) return false;
    const auto found=state_->frames.find(name);
    if (found==state_->frames.end() || frame<0 || size_t(frame)>=found->second.size()) {
        state_->Fail("Unknown sprite or frame: " + std::string(name));
        return false;
    }
    return AffineQuad(center,axisX,axisY,found->second[frame],tint,flipX,flipY);
}

bool SpriteBatch2D::SpriteRegion(std::string_view name, glm::vec2 center, glm::vec2 size,
    glm::vec4 normalizedRegion, float radians, glm::vec4 tint, bool flipX, bool flipY, int frame) {
    if (!state_ || !state_->begun || state_->flushed || state_->inFlight) return false;
    const auto found = state_->frames.find(name);
    if (found == state_->frames.end() || frame < 0 || size_t(frame) >= found->second.size()) {
        state_->Fail("Unknown sprite or frame: " + std::string(name));
        return false;
    }
    if (!SpriteDetail::ValidRegion(normalizedRegion)) {
        state_->Fail("Sprite region must be positive, finite and inside its frame");
        return false;
    }
    Quad(center, size, radians, SpriteDetail::ComposeRegion(found->second[frame], normalizedRegion), tint, flipX, flipY);
    return true;
}

void SpriteBatch2D::Rect(glm::vec2 center, glm::vec2 size, glm::vec4 tint, float radians) {
    if (state_) Quad(center, size, radians, state_->whiteRegion, tint);
}

void SpriteBatch2D::Text(std::string_view text, glm::vec2 topLeft, float pixel, glm::vec4 tint) {
    if (!state_ || !(pixel > 0) || !std::isfinite(pixel)) return;
    const float left = topLeft.x;
    for (char character : text) {
        if (character == '\n') { topLeft.x = left; topLeft.y -= 9 * pixel; continue; }
        if (character >= 'a' && character <= 'z') character = char(character - 'a' + 'A');
        if (character != ' ') {
            auto glyph = GlyphCharacters.find(character);
            if (glyph == std::string_view::npos) glyph = GlyphCharacters.find('?');
            Quad(topLeft + glm::vec2(2.5f, -3.5f) * pixel, glm::vec2(5, 7) * pixel, 0,
                state_->glyphRegions[glyph], tint, false, false, true);
        }
        topLeft.x += 6 * pixel;
    }
}

bool SpriteBatch2D::Flush(RHIFrameEncoder& encoder) {
    if (!Ready() || !state_->begun || state_->flushed || !encoder.IsRecording()) return false;
    if (!encoder.KeepAlive(state_)) return false;
    if (!state_->indices.empty()) {
        RHICommand::DrawIndexed draw;
        draw.indexCount = uint32_t(state_->indices.size());
        if (!encoder.BindPipeline(state_->pipeline) || !encoder.BindMesh(state_->mesh) ||
            !encoder.BindTexture(state_->atlas, 0) || !encoder.DrawIndexed(draw)) {
            state_->Fail("Sprite draw command recording failed");
            return false;
        }
    }
    state_->flushed = true;
    state_->flushedFrame = encoder.GetCommandBuffer();
    return true;
}

bool SpriteBatch2D::Submit(ObjectWeakPtr<RHIFrameCommandBuffer> frame, std::function<void()> completed) {
    if (!Ready() || !state_->flushed || !frame || frame != state_->flushedFrame || frame->IsSubmitted()) return false;
    auto state = state_;
    state->inFlight = true;
    // Retain the actual recording lease for cancellation if upload fails.
    auto cancellation = std::make_shared<RHIFrameEncoder>(frame);
    auto finish = [state, completed = std::move(completed)]() mutable {
        state->inFlight = false;
        state->begun = false;
        state->flushed = false;
        state->flushedFrame = nullptr;
        if (completed) completed();
    };
    auto afterUpload = [state, frame, cancellation, finish = std::move(finish)](bool ok) mutable {
        if (!ok || !state->Active()) {
            if (!ok) state->Fail("Sprite mesh upload failed");
            cancellation->Cancel();
            finish();
            return;
        }
        // Upload completion is the explicit mesh -> draw dependency. An upload
        // being enqueued alone does not guarantee it precedes frame execution.
        if (!state->device->async_SubmitFrameCommands(frame, finish)) {
            state->Fail("Sprite frame submission failed");
            cancellation->Cancel();
            finish();
        }
    };
    if (state->indices.empty()) {
        afterUpload(true);
    } else {
        UpdateMeshBufferDesc upload;
        upload.vertexCount = uint32_t(state->vertices.size());
        upload.vertexData = state->vertices.data();
        upload.vertexByteSize = state->vertices.size() * sizeof(SpriteVertex);
        upload.indexCount = uint32_t(state->indices.size());
        upload.indexData = state->indices.data();
        upload.indexByteSize = state->indices.size() * sizeof(uint32_t);
        state->device->async_UpdateMeshBuffer(state->mesh, upload, std::move(afterUpload));
    }
    return true;
}

const std::string& SpriteBatch2D::Error() const {
    static const std::string empty;
    return state_ ? state_->error : empty;
}

void SpriteBatch2D::Shutdown() {
    if (state_) state_->closed = true;
    state_.reset();
}

} // namespace Render
