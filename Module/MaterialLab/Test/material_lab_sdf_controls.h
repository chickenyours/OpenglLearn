#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <imgui.h>
#include "MaterialLab/Public/material_lab_module.h"

namespace MaterialLab {

// The SDF experiments share the probe runner, but their volume inspection
// controls must remain available when the probe overlay itself is hidden.
inline void DrawSdfStudyControls(FrameSettings& frame, const FrameSettings& baseline) {
    auto& placement = frame.effects.screenProbePlacement;
    auto& sdf = placement.trace.sdf;
    bool changed = ImGui::Checkbox("SDF fallback / volume (F10)", &sdf.enabled);
    int view = int(sdf.visualMode);
    if (ImGui::Combo("SDF view (V)", &view, "Scene and traced hits\0Mesh SDF\0Global SDF\0Mesh / Global split\0")) {
        sdf.visualMode = std::uint32_t(std::clamp(view, 0, 3));
        sdf.sliceView = 0;
        changed = true;
    }
    changed |= ImGui::Checkbox("Automatic MDF quality (L)", &sdf.automaticQuality);
    ImGui::BeginDisabled(sdf.automaticQuality);
    int quality = int(sdf.meshQuality);
    if (ImGui::Combo("Manual MDF quality (F12)", &quality, "Low\0Medium\0High\0")) {
        sdf.meshQuality = std::uint32_t(std::clamp(quality, 0, 2));
        changed = true;
    }
    ImGui::EndDisabled();
    if (sdf.automaticQuality)
        ImGui::TextDisabled("MDF tiers follow the camera; Global Clipmaps use their own source tiers.");
    changed |= ImGui::Checkbox("Voxel grid (B)", &sdf.showVoxelGrid);
    changed |= ImGui::Checkbox("Inspect selected object only (O)", &sdf.visualizeSelected);
    int selected = int(sdf.selectedObject);
    const int objectCount = frame.sdfModelStudy ? 16 : int(ProbeSdfObjects().size());
    if (ImGui::SliderInt("Selected object (F9)", &selected, 0, std::max(0, objectCount - 1), "%d", ImGuiSliderFlags_AlwaysClamp)) {
        sdf.selectedObject = std::uint32_t(std::clamp(selected, 0, std::max(0, objectCount - 1)));
        changed = true;
    }
    if (frame.sdfModelStudy)
        changed |= ImGui::Checkbox("Raster model LOD (T)", &frame.sdfModelRasterLod);
    if (frame.sdfClipmapStudy) {
        int level = sdf.clipmapLevel + 1;
        if (ImGui::Combo("Inspect Clipmap level (K)", &level, "Automatic\0L0 - nearest\0L1\0L2\0L3 - farthest\0")) {
            sdf.clipmapLevel = std::clamp(level, 0, 4) - 1;
            changed = true;
        }
        changed |= ImGui::Checkbox("Clipmap level colors (C)", &sdf.showClipmapLevels);
        changed |= ImGui::Checkbox("Clipmap bounds (H)", &sdf.showClipmapBounds);
        ImGui::TextWrapped("Level selection changes volume inspection. Fallback rays continue to choose the finest covered level automatically.");
    }
    if (ImGui::TreeNode("SDF trace and slice settings")) {
        changed |= ImGui::Checkbox("Mesh SDF fallback (M)", &sdf.useMesh);
        changed |= ImGui::Checkbox("Global SDF fallback (G)", &sdf.useGlobal);
        changed |= ImGui::Checkbox("Show trace hit points (F11)", &sdf.showHitPoints);
        changed |= ImGui::SliderFloat("Mesh trace distance", &sdf.meshTraceDistance, 0, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        int steps = int(sdf.maxSteps);
        if (ImGui::SliderInt("SDF trace step limit", &steps, 8, 512, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            sdf.maxSteps = std::uint32_t(std::clamp(steps, 8, 512));
            changed = true;
        }
        int slice = int(sdf.sliceView);
        if (ImGui::Combo("Distance field slice (F8)", &slice, "Off\0Global SDF\0Mesh SDF\0")) {
            sdf.sliceView = std::uint32_t(std::clamp(slice, 0, 2));
            if (sdf.sliceView != 0) sdf.visualMode = 0;
            changed = true;
        }
        int axis = int(sdf.sliceAxis);
        if (ImGui::Combo("Slice axis", &axis, "X\0Y\0Z\0")) {
            sdf.sliceAxis = std::uint32_t(std::clamp(axis, 0, 2));
            changed = true;
        }
        changed |= ImGui::SliderFloat("Slice position ([ / ])", &sdf.slicePosition, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::TreePop();
    }
    if (ImGui::Button("Reset SDF inspection")) {
        sdf = baseline.effects.screenProbePlacement.trace.sdf;
        frame.sdfModelRasterLod = baseline.sdfModelRasterLod;
        changed = true;
    }
    if (changed) {
        placement.reset = true;
        placement.trace.selectedProbe = std::numeric_limits<std::uint32_t>::max();
    }
}

inline void DrawSdfStudyHelp(const FrameSettings& frame) {
    ImGui::TextWrapped("SDF: V scene / Mesh / Global / split; F12 manual low / medium / high; L automatic MDF quality; B voxel grid; O selected object only; F9 next object.");
    ImGui::TextWrapped("M / G toggle Mesh / Global fallback rays; F10 SDF enable; F11 hit points; F8 off / Global / Mesh slice; X / Y / Z slice axis; [ / ] slice position.");
    if (frame.sdfClipmapStudy)
        ImGui::TextWrapped("Clipmaps: K automatic / L0 / L1 / L2 / L3; C level colors; H bounds. Walk with WASD to inspect local entering-brick updates.");
    if (frame.sdfModelStudy)
        ImGui::TextWrapped("Tea banquet: T toggles raster model LOD. Mesh SDF and raster model selection are independent.");
}

} // namespace MaterialLab
