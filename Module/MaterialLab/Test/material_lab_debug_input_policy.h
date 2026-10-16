#pragma once

#include <imgui.h>

namespace MaterialLab::DebugPanelInput {

// Dear ImGui requests keyboard capture for a passively focused navigation
// window too. A scene viewer should reserve its keys only while the user is
// editing, navigating controls, or interacting with a popup. The host disables
// passive panel navigation and releases focus when the panel is hidden.
inline bool WantsKeyboard(bool visible, const ImGuiIO& io,
                          bool anyItemActive, bool popupOpen = false) {
    return visible && io.WantCaptureKeyboard &&
        (io.WantTextInput || anyItemActive || popupOpen || io.NavVisible);
}

inline bool WantsMouse(bool visible, const ImGuiIO& io) {
    return visible && io.WantCaptureMouse;
}

} // namespace MaterialLab::DebugPanelInput
