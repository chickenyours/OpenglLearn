#include "DebugUI/Public/debug_ui_draw_data.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace DebugUI::Detail {
namespace {
bool Finite(float value) { return std::isfinite(value); }
bool Fail(DrawSnapshot& result, std::string& error, const char* message) {
    result = {};
    error = message;
    return false;
}
}

bool BuildDrawSnapshot(const ImDrawData& source, DrawSnapshot& result, std::string& error) {
    result = {};
    error.clear();
    if (!source.Valid) return Fail(result, error, "ImGui draw data is not rendered");
    if (!Finite(source.DisplayPos.x) || !Finite(source.DisplayPos.y) ||
        !Finite(source.DisplaySize.x) || !Finite(source.DisplaySize.y) ||
        !Finite(source.FramebufferScale.x) || !Finite(source.FramebufferScale.y))
        return Fail(result, error, "ImGui viewport contains non-finite coordinates");
    // A minimized window is a successful empty frame, not a stale snapshot.
    if (source.DisplaySize.x <= 0 || source.DisplaySize.y <= 0 ||
        source.FramebufferScale.x <= 0 || source.FramebufferScale.y <= 0) return true;
    const double width = double(source.DisplaySize.x) * source.FramebufferScale.x;
    const double height = double(source.DisplaySize.y) * source.FramebufferScale.y;
    if (width > std::numeric_limits<std::int32_t>::max() ||
        height > std::numeric_limits<std::int32_t>::max())
        return Fail(result, error, "ImGui framebuffer exceeds RHI coordinate range");
    DrawSnapshot next;
    // GLFW supplies integer framebuffer dimensions divided by logical size as
    // a float scale. Multiplying back can be just below the original integer.
    next.width = static_cast<std::uint32_t>(std::lround(width));
    next.height = static_cast<std::uint32_t>(std::lround(height));
    if (!next.width || !next.height) return true;
    if (source.CmdListsCount < 0 || source.CmdListsCount != source.CmdLists.Size)
        return Fail(result, error, "Invalid ImGui draw list count");
    for (const ImDrawList* list : source.CmdLists) {
        if (!list || list->VtxBuffer.Size < 0 || list->IdxBuffer.Size < 0 ||
            next.vertices.size() + std::size_t(list->VtxBuffer.Size) >
                std::size_t(std::numeric_limits<std::int32_t>::max()) ||
            next.indices.size() + std::size_t(list->IdxBuffer.Size) >
                std::size_t(std::numeric_limits<std::uint32_t>::max()))
            return Fail(result, error, "ImGui geometry exceeds RHI draw ranges");
        const auto vertexBase = static_cast<std::int32_t>(next.vertices.size());
        const auto indexBase = static_cast<std::uint32_t>(next.indices.size());
        for (const ImDrawVert& vertex : list->VtxBuffer) {
            if (!Finite(vertex.pos.x) || !Finite(vertex.pos.y) ||
                !Finite(vertex.uv.x) || !Finite(vertex.uv.y))
                return Fail(result, error, "ImGui vertex contains non-finite coordinates");
            Vertex converted{};
            converted.position[0] = (vertex.pos.x - source.DisplayPos.x) * 2 / source.DisplaySize.x - 1;
            converted.position[1] = 1 - (vertex.pos.y - source.DisplayPos.y) * 2 / source.DisplaySize.y;
            converted.uv[0] = vertex.uv.x;
            converted.uv[1] = vertex.uv.y;
            converted.color[0] = float((vertex.col >> IM_COL32_R_SHIFT) & 255u) / 255;
            converted.color[1] = float((vertex.col >> IM_COL32_G_SHIFT) & 255u) / 255;
            converted.color[2] = float((vertex.col >> IM_COL32_B_SHIFT) & 255u) / 255;
            converted.color[3] = float((vertex.col >> IM_COL32_A_SHIFT) & 255u) / 255;
            next.vertices.push_back(converted);
        }
        for (ImDrawIdx index : list->IdxBuffer) next.indices.push_back(index);
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback) {
                if (command.UserCallback != ImDrawCallback_ResetRenderState)
                    return Fail(result, error, "DebugUI rejects custom ImDrawCallback; submit rendering through RHI instead");
                DrawCommand reset;
                reset.resetState = true;
                next.commands.push_back(reset);
                continue;
            }
            if (!command.ElemCount) continue;
            if (std::uint64_t(command.IdxOffset) + command.ElemCount > std::uint64_t(list->IdxBuffer.Size) ||
                command.VtxOffset > std::uint32_t(list->VtxBuffer.Size))
                return Fail(result, error, "ImGui draw offsets exceed their draw list");
            for (std::uint32_t i = 0; i < command.ElemCount; ++i)
                if (std::uint64_t(list->IdxBuffer[int(command.IdxOffset + i)]) + command.VtxOffset >=
                    std::uint64_t(list->VtxBuffer.Size))
                    return Fail(result, error, "ImGui index references a missing vertex");
            const ImVec4 clip = command.ClipRect;
            if (!Finite(clip.x) || !Finite(clip.y) || !Finite(clip.z) || !Finite(clip.w))
                return Fail(result, error, "ImGui clip rectangle is non-finite");
            const auto clipX = [&](float x) { return std::clamp((double(x) - source.DisplayPos.x) * source.FramebufferScale.x, 0.0, double(next.width)); };
            const auto clipY = [&](float y) { return std::clamp((double(y) - source.DisplayPos.y) * source.FramebufferScale.y, 0.0, double(next.height)); };
            const double left = clipX(clip.x), right = clipX(clip.z);
            const double top = clipY(clip.y), bottom = clipY(clip.w);
            if (right <= left || bottom <= top) continue;
            DrawCommand draw;
            draw.texture = command.GetTexID();
            draw.scissorX = static_cast<std::int32_t>(std::floor(left));
            const auto topPixel = static_cast<std::uint32_t>(std::floor(top));
            const auto bottomPixel = static_cast<std::uint32_t>(std::ceil(bottom));
            draw.scissorY = static_cast<std::int32_t>(next.height - bottomPixel);
            draw.scissorWidth = static_cast<std::uint32_t>(std::ceil(right)) - draw.scissorX;
            draw.scissorHeight = bottomPixel - topPixel;
            draw.firstIndex = indexBase + command.IdxOffset;
            draw.indexCount = command.ElemCount;
            draw.baseVertex = vertexBase + static_cast<std::int32_t>(command.VtxOffset);
            next.commands.push_back(draw);
        }
    }
    result = std::move(next);
    return true;
}

} // namespace DebugUI::Detail
