#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

namespace DebugUI::Detail {

// Packed for the RHI's Vec2 / Vec2 / Vec4 layout. Independent of GLM alignment.
struct Vertex {
    float position[2];
    float uv[2];
    float color[4];
};
static_assert(sizeof(Vertex) == 8 * sizeof(float));

struct DrawCommand {
    bool resetState = false;
    ImTextureID texture = 0;
    std::int32_t scissorX = 0, scissorY = 0;
    std::uint32_t scissorWidth = 0, scissorHeight = 0;
    std::uint32_t firstIndex = 0, indexCount = 0;
    std::int32_t baseVertex = 0;
};

struct DrawSnapshot {
    std::uint32_t width = 0, height = 0;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<DrawCommand> commands;
};

// Copies everything needed after ImGui::NewFrame invalidates ImDrawData.
// Coordinates and scissors follow the RHI's logical bottom-left convention on
// both OpenGL and Vulkan. Ordinary callbacks are rejected (never run on the GPU
// thread); ImDrawCallback_ResetRenderState becomes an explicit reset marker.
bool BuildDrawSnapshot(const ImDrawData& source, DrawSnapshot& result, std::string& error);

} // namespace DebugUI::Detail
