#include "DebugUI/Public/debug_ui_draw_data.h"
#include "DebugUI/Public/debug_ui_texture_registry.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool Near(float a, float b) { return std::abs(a - b) < .0001f; }
void CustomCallback(const ImDrawList*, const ImDrawCmd*) {}

ImDrawVert MakeVertex(float x, float y) {
    ImDrawVert result;
    result.pos = {x, y};
    result.uv = {.25f, .75f};
    result.col = IM_COL32(255, 128, 64, 32);
    return result;
}
void AddTriangle(ImDrawList& list, unsigned offset = 0) {
    for (int i = 0; i < 3; ++i) list.VtxBuffer.push_back(MakeVertex(10 + i * 10.0f, 20 + i * 10.0f));
    list.IdxBuffer.push_back(ImDrawIdx(offset));
    list.IdxBuffer.push_back(ImDrawIdx(offset + 1));
    list.IdxBuffer.push_back(ImDrawIdx(offset + 2));
}
}

int main() {
    using namespace DebugUI::Detail;
    auto* context = ImGui::CreateContext();
    try {
        ImDrawList first(ImGui::GetDrawListSharedData());
        ImDrawList second(ImGui::GetDrawListSharedData());
        AddTriangle(first);
        first.VtxBuffer.push_back(MakeVertex(0, 0)); // exercise VtxOffset + nonzero IdxOffset
        first.VtxBuffer.push_back(MakeVertex(0, 0));
        first.VtxBuffer.push_back(MakeVertex(0, 0));
        first.IdxBuffer.push_back(0);
        first.IdxBuffer.push_back(1);
        first.IdxBuffer.push_back(2);
        ImDrawCmd command;
        command.TextureId = 7;
        command.ClipRect = {-20, 24.25f, 40.25f, 100};
        command.IdxOffset = 3;
        command.VtxOffset = 3;
        command.ElemCount = 3;
        first.CmdBuffer.push_back(command);
        ImDrawCmd reset;
        reset.UserCallback = ImDrawCallback_ResetRenderState;
        first.CmdBuffer.push_back(reset);
        AddTriangle(second);
        command.IdxOffset = command.VtxOffset = 0;
        command.ClipRect = {10, 20, 110, 70};
        second.CmdBuffer.push_back(command);
        ImDrawData data;
        data.Valid = true;
        data.DisplayPos = {10, 20};
        data.DisplaySize = {100, 50};
        data.FramebufferScale = {2, 2};
        data.CmdLists.push_back(&first);
        data.CmdLists.push_back(&second);
        data.CmdListsCount = 2;
        data.TotalVtxCount = 9;
        data.TotalIdxCount = 9;
        DrawSnapshot snapshot;
        std::string error;
        Check(BuildDrawSnapshot(data, snapshot, error), error.c_str());
        Check(snapshot.width == 200 && snapshot.height == 100, "DPI framebuffer dimensions");
        Check(snapshot.vertices.size() == 9 && snapshot.indices.size() == 9, "combined geometry");
        Check(snapshot.commands.size() == 3 && snapshot.commands[1].resetState, "reset marker ordering");
        const auto& a = snapshot.commands[0];
        Check(a.firstIndex == 3 && a.baseVertex == 3 && a.indexCount == 3, "ImGui local offsets");
        Check(a.scissorX == 0 && a.scissorY == 0 && a.scissorWidth == 61 && a.scissorHeight == 92,
              "DPI clip translation, outward rounding and clamp");
        Check(snapshot.commands[2].firstIndex == 6 && snapshot.commands[2].baseVertex == 6,
              "multiple draw list global offsets");
        Check(snapshot.commands[2].scissorY == 0 && snapshot.commands[2].scissorHeight == 100,
              "full frame scissor");
        const auto& v = snapshot.vertices[0];
        Check(Near(v.position[0], -1) && Near(v.position[1], 1), "DisplayPos NDC top-left conversion");
        Check(Near(v.color[0], 1) && Near(v.color[1], 128.0f / 255) &&
              Near(v.color[2], 64.0f / 255) && Near(v.color[3], 32.0f / 255), "packed RGBA conversion");
        Check(Near(v.uv[0], .25f) && Near(v.uv[1], .75f), "texture coordinates preserved");
        first.VtxBuffer[0].pos = {999, 999};
        first.IdxBuffer[0] = 2;
        first.CmdBuffer[0].TextureId = 99;
        Check(Near(snapshot.vertices[0].position[0], -1) && snapshot.indices[0] == 0 && a.texture == 7,
              "snapshot owns geometry and draw metadata");

        // Top-only clipping must convert to bottom-left RHI coordinates.
        first.CmdBuffer[0].ClipRect = {10, 20, 110, 30};
        Check(BuildDrawSnapshot(data, snapshot, error), "top clipping conversion");
        Check(snapshot.commands[0].scissorY == 80 && snapshot.commands[0].scissorHeight == 20,
              "top-left ImGui clip becomes bottom-left RHI scissor");
        first.CmdBuffer[0].ClipRect = {200, 200, 250, 250};
        Check(BuildDrawSnapshot(data, snapshot, error) && snapshot.commands.size() == 2,
              "completely clipped draw omitted");

        first.CmdBuffer[0].UserCallback = CustomCallback;
        Check(!BuildDrawSnapshot(data, snapshot, error) && snapshot.vertices.empty() &&
              error.find("custom ImDrawCallback") != std::string::npos, "custom callbacks rejected atomically");
        first.CmdBuffer[0].UserCallback = nullptr;
        first.CmdBuffer[0].IdxOffset = 7;
        Check(!BuildDrawSnapshot(data, snapshot, error), "out-of-range index offset rejected");
        first.CmdBuffer[0].IdxOffset = 3;
        first.IdxBuffer[3] = 42;
        Check(!BuildDrawSnapshot(data, snapshot, error), "missing vertex rejected");
        first.IdxBuffer[3] = 0;
        data.DisplaySize = {0, 0};
        Check(BuildDrawSnapshot(data, snapshot, error) && snapshot.vertices.empty() && snapshot.commands.empty(),
              "minimized frame drops previous data");
        data.DisplaySize = {1000, 500};
        data.FramebufferScale = {.79999995f, .79999995f};
        Check(BuildDrawSnapshot(data, snapshot, error) && snapshot.width == 800 && snapshot.height == 400,
              "fractional DPI round trip preserves integer framebuffer extent");
        data.DisplaySize = {100, 50};
        data.FramebufferScale.x = std::numeric_limits<float>::quiet_NaN();
        Check(!BuildDrawSnapshot(data, snapshot, error), "non-finite DPI rejected");

        // Dear ImGui uses 16-bit indices with VtxOffset when a list grows past
        // 64K vertices. The RHI baseVertex must retain the high vertex segment.
        ImDrawList large(ImGui::GetDrawListSharedData());
        large.VtxBuffer.resize(65539);
        for (auto& vertex : large.VtxBuffer) vertex = MakeVertex(10, 20);
        large.IdxBuffer.push_back(0);
        large.IdxBuffer.push_back(1);
        large.IdxBuffer.push_back(2);
        command.IdxOffset = 0;
        command.VtxOffset = 65536;
        command.ClipRect = {10, 20, 110, 70};
        large.CmdBuffer.push_back(command);
        data.CmdLists.clear();
        data.CmdLists.push_back(&large);
        data.CmdListsCount = 1;
        data.FramebufferScale = {1, 1};
        Check(BuildDrawSnapshot(data, snapshot, error) && snapshot.commands.size() == 1 &&
              snapshot.commands[0].baseVertex == 65536 && snapshot.indices[2] == 2,
              "large 16-bit ImGui mesh preserves VtxOffset");

        TextureRegistry registry;
        auto owner = std::make_shared<int>(42);
        std::weak_ptr<int> weak = owner;
        Check(!registry.Register({}, owner), "invalid RHI handle rejected");
        Check(!registry.Register({1, 3}, {}), "unowned texture rejected");
        const auto texture = registry.Register({1, 3}, owner);
        Check(texture && !registry.Find(texture + 1), "unknown texture lookup rejected");
        auto captured = registry.Find(texture);
        Check(captured && captured->texture.id == 1 && captured->texture.version == 3,
              "texture handle preserves RHI generation");
        owner.reset();
        Check(registry.Unregister(texture) && !registry.Find(texture) && !weak.expired(),
              "prepared frame keeps texture owner after unregister");
        captured.reset();
        Check(weak.expired(), "last frame retirement releases texture owner");
        const auto next = registry.Register({1, 4}, std::make_shared<int>(1));
        registry.Clear();
        const auto newest = registry.Register({1, 5}, std::make_shared<int>(2));
        Check(next != texture && newest != next && !registry.Find(next), "texture IDs never alias retired registrations");
        std::cout << "DebugUI CPU draw conversion and texture lifetime tests passed\n";
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        ImGui::DestroyContext(context);
        return 1;
    }
    ImGui::DestroyContext(context);
    return 0;
}
