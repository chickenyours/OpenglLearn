#include "material_lab_debug_input_policy.h"

#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Capture {
    bool rawKeyboard, text, active, navigation, keyboard, mouse;
};

class Fixture {
public:
    explicit Fixture(ImGuiWindowFlags flags = 0) : windowFlags(flags) {
        context = ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {640, 480};
        io.DeltaTime = 1.f / 60;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        io.AddMousePosEvent(600, 450);
    }
    ~Fixture() { ImGui::DestroyContext(context); }

    Capture Frame(bool visible = true, bool focusInput = false,
                  bool showPopup = false, bool focusButton = false) {
        ImGui::NewFrame();
        if (visible) {
            ImGui::SetNextWindowPos({10, 10});
            ImGui::SetNextWindowSize({300, 250});
            ImGui::Begin("Scene controls", nullptr, windowFlags);
            if (focusButton) ImGui::SetKeyboardFocusHere();
            ImGui::Button("Scene preset");
            if (focusInput) ImGui::SetKeyboardFocusHere();
            ImGui::InputText("Find panel", text, sizeof(text));
            if (showPopup) ImGui::OpenPopup("Settings");
            if (ImGui::BeginPopup("Settings")) {
                ImGui::TextUnformatted("Popup owns scene keys");
                ImGui::EndPopup();
            }
            ImGui::End();
        }
        const auto& io = ImGui::GetIO();
        const bool active = ImGui::IsAnyItemActive();
        const bool popup = ImGui::IsPopupOpen(nullptr,
            ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        const Capture result{io.WantCaptureKeyboard, io.WantTextInput, active,
            io.NavVisible, MaterialLab::DebugPanelInput::WantsKeyboard(visible, io, active, popup),
            MaterialLab::DebugPanelInput::WantsMouse(visible, io)};
        ImGui::Render();
        return result;
    }
private:
    ImGuiContext* context = nullptr;
    ImGuiWindowFlags windowFlags = 0;
    char text[64]{};
};
} // namespace

int main() {
    try {
        {
            Fixture fixture;
            fixture.Frame();
            const auto passive = fixture.Frame();
            Check(passive.rawKeyboard && !passive.text && !passive.active && !passive.navigation,
                "Fixture did not reproduce ImGui passive-focus keyboard capture");
            Check(!passive.keyboard, "A merely focused panel swallowed scene hotkeys");
            const auto hidden = fixture.Frame(false);
            Check(!hidden.keyboard && !hidden.mouse, "A hidden panel captured scene input");
        }
        {
            Fixture fixture;
            fixture.Frame(true, true);
            fixture.Frame();
            const auto editing = fixture.Frame();
            Check(editing.rawKeyboard && editing.text && editing.active && editing.keyboard,
                "An active text field leaked shortcuts or movement to the scene");
            const auto hidden = fixture.Frame(false);
            Check(!hidden.keyboard, "Hiding an active text field retained scene keyboard capture");
        }
        {
            Fixture fixture(ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus |
                            ImGuiWindowFlags_NoFocusOnAppearing);
            fixture.Frame();
            ImGui::SetWindowFocus("Scene controls");
            ImGui::GetIO().AddKeyEvent(ImGuiKey_LeftArrow, true);
            const auto arrow = fixture.Frame();
            Check(!arrow.rawKeyboard && !arrow.keyboard && !arrow.navigation,
                "A scene arrow key activated keyboard navigation in the tool panel");
            ImGui::GetIO().AddKeyEvent(ImGuiKey_LeftArrow, false);
            fixture.Frame(true, true);
            fixture.Frame();
            const auto editing = fixture.Frame();
            Check(editing.rawKeyboard && editing.text && editing.active && editing.keyboard,
                "Disabling panel navigation broke active text editing");
        }
        {
            Fixture fixture;
            fixture.Frame(true, false, false, true);
            fixture.Frame();
            ImGui::SetNavCursorVisible(true);
            const auto navigation = fixture.Frame();
            Check(navigation.navigation && navigation.keyboard,
                "Explicit keyboard navigation leaked arrows to the scene");
            ImGui::SetWindowFocus(nullptr);
            const auto released = fixture.Frame();
            Check(!released.keyboard, "Returning focus to the scene did not restore hotkeys");
        }
        {
            Fixture fixture;
            fixture.Frame();
            fixture.Frame(true, false, true);
            Check(fixture.Frame().keyboard, "An open popup leaked shortcuts to the scene");
        }
        std::cout << "MaterialLab input routing passed: passive focus, hidden panels, text editing, navigation, popups\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
