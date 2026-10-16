#include "ApplicationWindow/public/window.h"
#include "MaterialLab/Public/material_lab_module.h"
#include "DebugUI/Public/debug_ui_module.h"
#include "material_lab_debug_panel.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {
void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::array<int, 4> Pixel(const MaterialLab::FramePixels& image, float x, float y) {
    const auto column = std::min(image.width - 1, unsigned(x * image.width));
    const auto row = image.height - 1 - std::min(image.height - 1, unsigned(y * image.height));
    const auto offset = (std::size_t(row) * image.width + column) * 4;
    return {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2], image.rgba[offset + 3]};
}
void Save(const MaterialLab::FramePixels& image, const std::string& path) {
    std::vector<unsigned char> topDown(image.rgba.size());
    const auto stride = std::size_t(image.width) * 4;
    for (unsigned row = 0; row < image.height; ++row)
        std::copy_n(image.rgba.data() + (image.height - row - 1) * stride, stride,
                    topDown.data() + row * stride);
    Check(stbi_write_png(path.c_str(), int(image.width), int(image.height), 4,
                         topDown.data(), int(stride)) != 0, "Cannot save GPU test screenshot");
}
void DrawControls(MaterialLab::DebugPanelHost& panels, MaterialLab::FrameSettings& frame) {
    const auto size = ImGui::GetIO().DisplaySize;
    panels.Draw(frame);
    auto* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect({size.x * .78f, size.y * .10f}, {size.x * .92f, size.y * .20f}, true);
    draw->AddRectFilled({size.x * .72f, size.y * .06f}, {size.x * .96f, size.y * .24f}, IM_COL32(255, 0, 0, 255));
    draw->PopClipRect();
    draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    draw->AddRectFilled({size.x * .78f, size.y * .65f}, {size.x * .92f, size.y * .75f}, IM_COL32(0, 0, 255, 128));
}
}

int main(int argc, char** argv) {
    int result = 0;
    try {
        bool vulkan = false, validation = false;
        std::string screenshot;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--vulkan") vulkan = true;
            else if (argument == "--validation") validation = true;
            else if (argument == "--screenshot" && index + 1 < argc) screenshot = argv[++index];
            else throw std::runtime_error("Unknown GPU test argument: " + argument);
        }
        Check(glfwInit() == GLFW_TRUE, "GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        const auto backend = vulkan ? Render::BackendType::Vulkan : Render::BackendType::Opengl;
        ApplicationWindow::Window window({640, 480, "DebugUI RHI regression", false, backend});
        Check(window.Activate(), "GPU test window creation failed");
        std::unique_ptr<MaterialLab::MaterialLabModule> lab;
        if (vulkan) {
            auto context = *window.GetRenderContextAsVulkan();
            context.validation = validation;
            context.hardwareRayTracing = false;
            lab = std::make_unique<MaterialLab::MaterialLabModule>(context);
        } else lab = std::make_unique<MaterialLab::MaterialLabModule>(*window.GetRenderContextAsOpengl());
        Check(lab->Startup(), lab->LastError());
        Check(lab->EnableDebugUI(window.GetNativeWindow(), backend), lab->LastError());
        auto* ui = lab->GetDebugUI();
        MaterialLab::DebugPanelHost panels(*lab, window.GetNativeWindow());
        MaterialLab::FrameSettings frame;
        frame.present = false;
        frame.effects.shadows.enabled = false;
        frame.effects.reflection.enabled = false;
        frame.effects.post.bloomEnabled = false;
        frame.effects.post.ssaoEnabled = false;
        frame.effects.temporal.antialiasing = Render::AntialiasingMode::None;
        frame.effects.temporal.msaaSamples = 1;
        MaterialLab::FramePixels image;
        for (const auto extent : {glm::ivec2(640, 480), glm::ivec2(800, 600)}) {
            glfwSetWindowSize(window.GetNativeWindow(), extent.x, extent.y);
            window.PollEvents();
            int width = 0, height = 0;
            glfwGetFramebufferSize(window.GetNativeWindow(), &width, &height);
            MaterialLab::SetShowcaseResolution(frame, unsigned(width), unsigned(height), .5f);
            Check(lab->Render(frame), lab->LastError());
            const auto baseline = lab->Readback();
            Check(!baseline.rgba.empty(), lab->LastError());
            // Repeat across fresh UI snapshots and native-size presentation to
            // catch stale draw pointers, prematurely retired resources and DPI mistakes.
            for (int iteration = 0; iteration < 3; ++iteration) {
                Check(ui->BeginFrame(), ui->LastError());
                DrawControls(panels, frame);
                Check(ui->EndFrame(), ui->LastError());
                Check(ui->LastFrameStatistics().draws > 0 && ui->LastFrameStatistics().vertices > 0,
                    "Control window produced no RHI draw snapshot");
                Check(lab->Render(frame), lab->LastError());
                image = lab->Readback();
                Check(!image.rgba.empty(), lab->LastError());
                const auto red = Pixel(image, .85f, .15f);
                Check(red[0] > 250 && red[1] < 4 && red[2] < 4,
                    "UI overlay is absent, flipped, tone-mapped or at the internal render extent");
                Check(Pixel(image, .74f, .15f) == Pixel(baseline, .74f, .15f),
                    "ImGui clip rectangle leaked outside its RHI scissor");
                Check(Pixel(image, .85f, .85f) == Pixel(baseline, .85f, .85f),
                    "UI overlay/scissor is vertically inverted");
                const auto original = Pixel(baseline, .85f, .70f);
                const auto blue = Pixel(image, .85f, .70f);
                for (int channel = 0; channel < 3; ++channel) {
                    const auto expected = original[channel] * (127.f / 255.f) + (channel == 2 ? 128.f : 0.f);
                    Check(std::abs(blue[channel] - expected) < 3.f, "UI straight-alpha RGB blend is incorrect");
                }
                Check(blue[3] >= 253, "UI must preserve opaque destination alpha");
                std::size_t changed = 0;
                for (unsigned y = 24; y < std::min(image.height, 220u); ++y)
                    for (unsigned x = 24; x < std::min(image.width, 300u); ++x) {
                        const auto offset = (std::size_t(image.height - y - 1) * image.width + x) * 4;
                        changed += !std::equal(image.rgba.begin() + offset, image.rgba.begin() + offset + 3,
                                               baseline.rgba.begin() + offset);
                    }
                Check(changed > 1000, "ImGui control window/font atlas was not rendered");
            }
            Check(lab->Render(frame), lab->LastError());
            const auto noUi = lab->Readback();
            Check(Pixel(noUi, .85f, .15f) == Pixel(baseline, .85f, .15f),
                "Prepared UI was drawn again without a new UI frame");
            Check(ui->BeginFrame(), ui->LastError());
            ImGui::GetForegroundDrawList()->AddRectFilled({0, 0}, ImGui::GetIO().DisplaySize, IM_COL32(255, 0, 0, 255));
            ui->CancelFrame();
            Check(lab->Render(frame), lab->LastError());
            Check(Pixel(lab->Readback(), .85f, .15f) == Pixel(baseline, .85f, .15f),
                "Cancelled UI frame leaked into scene rendering");
        }
        if (!screenshot.empty()) Save(image, screenshot);
        // Exercise a real registered panel and mouse capture, not just static
        // rasterization. A hidden GLFW window does not overwrite injected input.
        ImVec2 sliderPosition;
        panels.AddPanel("material", "Interactive exposure", [&](MaterialLab::DebugPanelHost::Context& context) {
            ImGui::SetNextItemWidth(220);
            ImGui::SliderFloat("Test exposure", &context.frame.exposure, .05f, 8.f);
            const auto minimum = ImGui::GetItemRectMin();
            const auto maximum = ImGui::GetItemRectMax();
            sliderPosition = {minimum.x + 175, (minimum.y + maximum.y) * .5f};
        });
        const auto controlFrame = [&] {
            Check(ui->BeginFrame(), ui->LastError());
            panels.Draw(frame);
            Check(ui->EndFrame(), ui->LastError());
            Check(lab->Render(frame), lab->LastError());
        };
        controlFrame();
        const float exposureBeforeClick = frame.exposure;
        ImGui::GetIO().AddMousePosEvent(sliderPosition.x, sliderPosition.y);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        Check(ui->BeginFrame(), ui->LastError());
        Check(ui->WantsMouse(), "UI did not capture a click over its controls");
        panels.Draw(frame);
        Check(ui->EndFrame(), ui->LastError());
        Check(lab->Render(frame), lab->LastError());
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        controlFrame();
        Check(frame.exposure > exposureBeforeClick + .5f, "Dragging a registered panel slider did not edit FrameSettings");
        Check(!panels.WantsKeyboard(), "A released slider left the scene keyboard captured");
        char editText[32]{};ImVec2 textPosition;
        panels.AddPanel("material", "Keyboard editing", [&](MaterialLab::DebugPanelHost::Context&) {
            ImGui::InputText("Test text",editText,sizeof(editText));
            const auto low=ImGui::GetItemRectMin(),high=ImGui::GetItemRectMax();
            textPosition={(low.x+high.x)*.5f,(low.y+high.y)*.5f};
        });
        controlFrame();
        ImGui::GetIO().AddMousePosEvent(textPosition.x,textPosition.y);
        ImGui::GetIO().AddMouseButtonEvent(0,true);controlFrame();
        ImGui::GetIO().AddMouseButtonEvent(0,false);controlFrame();
        Check(panels.WantsKeyboard(), "A focused text field leaked scene shortcuts");
        panels.SetVisible(false);
        Check(!panels.WantsKeyboard()&&!panels.WantsMouse(), "Closing the panel did not immediately return scene input");
        controlFrame();
        const auto closedPanel=lab->Readback();
        Check(lab->Render(frame),lab->LastError());
        Check(lab->Readback().rgba==closedPanel.rgba,
            "A closed panel changed the scene or left visible overlay pixels");
        panels.SetVisible(true);
        // Drawing the shared SDF panel must never replace the scene, camera,
        // object pose, selected volume or quality just because it was opened.
        auto sdfFrame=frame;sdfFrame.screenProbeStudy=true;sdfFrame.sdfProbeStudy=true;
        sdfFrame.sdfModelStudy=true;sdfFrame.sdfClipmapStudy=true;
        sdfFrame.screenProbeStudyState.occluderOffset=.6f;
        auto& sdf=sdfFrame.effects.screenProbePlacement.trace.sdf;
        sdf.enabled=true;sdf.visualMode=3;sdf.meshQuality=2;sdf.automaticQuality=true;sdf.clipmapLevel=2;
        for(unsigned n=0;n<2;++n) {
            Check(ui->BeginFrame(),ui->LastError());panels.Draw(sdfFrame);ui->CancelFrame();
            Check(sdfFrame.screenProbeStudy&&sdfFrame.sdfProbeStudy&&sdfFrame.sdfModelStudy&&sdfFrame.sdfClipmapStudy&&
                sdfFrame.screenProbeStudyState.occluderOffset==.6f&&sdf.visualMode==3&&sdf.meshQuality==2&&
                sdf.automaticQuality&&sdf.clipmapLevel==2&&Render::ValidateProbeSdfTracingSettings(sdf),
                "Opening the SDF panel changed scene or inspection settings");
        }
        panels.AddPanel("material", "Throwing panel", [](MaterialLab::DebugPanelHost::Context&) {
            throw std::runtime_error("Expected panel failure");
        });
        bool recovered = false;
        try {
            MaterialLab::DebugPanelFrame uiFrame(panels);
            uiFrame.Finish(frame);
        } catch (const std::runtime_error& error) {
            recovered = std::string(error.what()) == "Expected panel failure";
        }
        Check(recovered, "Panel failure did not reach its caller");
        panels.AddPanel("material", "Recovered panel", [](MaterialLab::DebugPanelHost::Context&) {
            ImGui::TextUnformatted("Rendering continues after a cancelled panel");
        });
        controlFrame();
        lab->Shutdown();
        Check(!lab->IsStarted(), "DebugUI/MaterialLab shutdown failed");
        std::cout << "DebugUI RHI GPU checks passed (" << (vulkan ? "Vulkan" : "OpenGL") << ")\n";
    } catch (const std::exception& error) {
        std::cerr << "DebugUI RHI GPU check failed: " << error.what() << '\n';
        result = 1;
    }
    glfwTerminate();
    return result;
}
