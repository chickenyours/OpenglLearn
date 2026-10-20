#pragma once

// Runner-owned panels edit existing laboratory settings. DebugUI owns the
// platform integration and RHI renderer; no UI types enter the Render module.
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <GLFW/glfw3.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include "DebugUI/Public/debug_ui_module.h"
#include "MaterialLab/Public/material_lab_module.h"
#include "material_lab_debug_input_policy.h"
#include "material_lab_sdf_controls.h"

namespace MaterialLab {

inline std::unordered_map<GLFWwindow*, DebugUI::DebugUIModule*>& DebugUiWindows() {
    static std::unordered_map<GLFWwindow*, DebugUI::DebugUIModule*> windows;
    return windows;
}
inline void DebugUiScroll(GLFWwindow* window, double x, double y) {
    // Runners install their camera callback after DebugUI initializes GLFW.
    // Forward once to this window's context, rather than whichever ImGui
    // context another tool may have most recently made current.
    const auto found = DebugUiWindows().find(window);
    if (found == DebugUiWindows().end()) return;
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(found->second->GetContext());
    ImGui_ImplGlfw_ScrollCallback(window, x, y);
    ImGui::SetCurrentContext(previous);
}

class DebugPanelHost {
public:
    struct Context {
        MaterialLabModule& lab;
        FrameSettings& frame;
        std::size_t& material;
        bool* isolate = nullptr;
        bool* animate = nullptr;
    };
    using PanelCallback = std::function<void(Context&)>;

    DebugPanelHost(MaterialLabModule& lab, GLFWwindow* window, bool visible = true)
        : lab_(lab), window_(window), visible_(visible) {
        if (lab.GetDebugUI()) DebugUiWindows()[window] = lab.GetDebugUI();
        AddPanel("material", "Material", [this](Context& c) { DrawMaterial(c); });
        AddPanel("lighting", "Lighting / shadows", [this](Context& c) { DrawLighting(c); });
        AddPanel("gi", "Global illumination / probes", [this](Context& c) { DrawGi(c); });
        AddPanel("post", "Post processing / antialiasing", [this](Context& c) { DrawPost(c); });
        AddPanel("statistics", "Statistics", [](Context& c) { DrawStatistics(c); });
        AddPanel("help", "Help / scene controls", [](Context& c) { DrawHelp(c); });
    }

    ~DebugPanelHost() { DebugUiWindows().erase(window_); }
    DebugPanelHost(const DebugPanelHost&) = delete;
    DebugPanelHost& operator=(const DebugPanelHost&) = delete;

    // Stable IDs allow a scene to replace a panel without duplicating it.
    void AddPanel(std::string id, std::string title, PanelCallback draw) {
        const auto found = std::find_if(panels_.begin(), panels_.end(), [&](const Panel& p) { return p.id == id; });
        Panel panel{std::move(id), std::move(title), std::move(draw)};
        if (found == panels_.end()) panels_.push_back(std::move(panel));
        else *found = std::move(panel);
    }

    bool Begin() {
        auto* ui = lab_.GetDebugUI();
        if (!ui) return false;
        const bool f1 = glfwGetKey(window_, GLFW_KEY_F1) == GLFW_PRESS;
        if (f1 && !previousF1_) SetVisible(!visible_);
        previousF1_ = f1;
        if (!ui->BeginFrame()) throw std::runtime_error(ui->LastError());
        return true;
    }
    void SetVisible(bool visible) {
        if (visible_ == visible) return;
        visible_ = visible;
        if (!visible_) if (auto* ui=lab_.GetDebugUI()) {
            auto* previous=ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(ui->GetContext());
            ImGui::SetWindowFocus(nullptr);
            ImGui::SetCurrentContext(previous);
        }
    }
    bool IsVisible() const { return visible_; }
    bool WantsMouse() const {
        auto* ui=lab_.GetDebugUI();if(!ui||!visible_)return false;
        auto* previous=ImGui::GetCurrentContext();ImGui::SetCurrentContext(ui->GetContext());
        const bool capture=DebugPanelInput::WantsMouse(visible_,ImGui::GetIO());
        ImGui::SetCurrentContext(previous);return capture;
    }
    bool WantsKeyboard() const {
        auto* ui=lab_.GetDebugUI();if(!ui||!visible_)return false;
        auto* previous=ImGui::GetCurrentContext();ImGui::SetCurrentContext(ui->GetContext());
        const bool popup=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
        const bool capture=DebugPanelInput::WantsKeyboard(visible_,ImGui::GetIO(),ImGui::IsAnyItemActive(),popup);
        ImGui::SetCurrentContext(previous);return capture;
    }
    bool End() { return lab_.GetDebugUI()->EndFrame(); }
    void Cancel() noexcept { if (auto* ui=lab_.GetDebugUI()) ui->CancelFrame(); }
    const std::string& LastError() const { return lab_.GetDebugUI()->LastError(); }

    void Draw(FrameSettings& frame, std::size_t* selected = nullptr, bool* isolate = nullptr, bool* animate = nullptr) {
        // The first frame is the scene's reset baseline, including command-line
        // settings. Presets deliberately copy only effects/exposure, never pose.
        const unsigned sceneKey = unsigned(frame.showcase) | (unsigned(frame.giRoom) << 1) |
            (unsigned(frame.screenProbeStudy) << 2) | (unsigned(frame.lightingStudy) << 3) | (unsigned(frame.shadowStudy) << 4) | (unsigned(frame.surfaceCacheStudy) << 5) |
            (unsigned(frame.sdfProbeStudy) << 6) | (unsigned(frame.sdfModelStudy) << 7) | (unsigned(frame.sdfClipmapStudy) << 8);
        if (!baseline_ || sceneKey != baselineScene_) { baseline_ = frame; baselineScene_ = sceneKey; preset_.reset(); }
        if (!visible_) return;
        auto& material = selected ? *selected : material_;
        Context context{lab_, frame, material, isolate, animate};
        const auto display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(14, 14), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(405, std::min(720.f, std::max(220.f, display.y - 28))), ImGuiCond_FirstUseEver);
        // Scene arrows and mode keys must not activate ImGui navigation merely
        // because this tool window was clicked. Text edits/active widgets and
        // popups still reserve their normal keyboard and mouse input.
        constexpr auto windowFlags=ImGuiWindowFlags_NoNavInputs|ImGuiWindowFlags_NoNavFocus|ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin("MaterialLab controls (F1)", &visible_, windowFlags)) {
            ImGui::TextDisabled("Live settings | F1 show / hide | changes are runtime only");
            if (ImGui::Button("Store preset")) preset_ = frame;
            ImGui::SameLine();
            ImGui::BeginDisabled(!preset_.has_value());
            if (ImGui::Button("Restore preset")) {
                const auto outputSize=frame.effects.outputSize;
                const auto deltaSeconds=frame.effects.deltaSeconds;
                frame.effects = preset_->effects; frame.exposure = preset_->exposure; frame.effects.cameraCut = true;
                frame.effects.outputSize=outputSize; frame.effects.deltaSeconds=deltaSeconds;
            }
            ImGui::EndDisabled();
            filter_.Draw("Find panel", std::max(80.f, ImGui::GetContentRegionAvail().x - 90));
            ImGui::Separator();
            for (auto& panel : panels_) {
                if (!filter_.PassFilter(panel.title.c_str()) && !filter_.PassFilter(panel.id.c_str())) continue;
                ImGui::PushID(panel.id.c_str());
                if (ImGui::CollapsingHeader(panel.title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .5f);
                    panel.draw(context);
                    ImGui::PopItemWidth();
                }
                ImGui::PopID();
            }
        }
        ImGui::End();
    }

private:
    struct Panel { std::string id, title; PanelCallback draw; };
    static bool FloatSlider(const char* label, float* value, float minimum, float maximum,
                            const char* format="%.3f", ImGuiSliderFlags flags=0) {
        return ImGui::SliderFloat(label,value,minimum,maximum,format,flags|ImGuiSliderFlags_AlwaysClamp);
    }
    static bool Float3Slider(const char* label, float* value, float minimum, float maximum) {
        return ImGui::SliderFloat3(label,value,minimum,maximum,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    }
    static bool AngleSlider(const char* label, float* value, float minimum, float maximum) {
        return ImGui::SliderAngle(label,value,minimum,maximum,"%.0f deg",ImGuiSliderFlags_AlwaysClamp);
    }
    static bool UIntSlider(const char* label, std::uint32_t& value, int minimum, int maximum) {
        int edited = int(value);
        if (!ImGui::SliderInt(label, &edited, minimum, maximum, "%d", ImGuiSliderFlags_AlwaysClamp)) return false;
        value = std::uint32_t(edited); return true;
    }
    template<class Enum> static bool Combo(const char* label, Enum& value, const char* items) {
        int edited = int(value);
        if (!ImGui::Combo(label, &edited, items)) return false;
        value = Enum(edited); return true;
    }
    void DrawMaterial(Context& c) {
        if (c.frame.showcase || c.frame.giRoom || c.frame.screenProbeStudy) {
            ImGui::TextWrapped("This scene uses dedicated materials. The material library controls are available in the gallery and lighting study.");
            return;
        }
        const auto count = c.lab.MaterialCount();
        if (!count) return;
        c.material = std::min(c.material, count - 1);
        auto info = c.lab.InspectMaterial(c.material);
        if (ImGui::BeginCombo("Selected material", info.name.c_str())) {
            for (std::size_t i = 0; i < count; ++i) {
                const auto item = c.lab.InspectMaterial(i);
                ImGui::PushID(int(i));
                if (ImGui::Selectable(item.name.c_str(), i == c.material)) c.material = i;
                ImGui::PopID();
            }
            ImGui::EndCombo(); info = c.lab.InspectMaterial(c.material);
        }
        if (c.isolate) ImGui::Checkbox("Isolate selected", c.isolate);
        const auto edit = [&](const char* name, const Render::Material::ParameterValue& value) {
            if (!c.lab.SetMaterialParameter(c.material, name, value)) throw std::runtime_error(c.lab.LastError());
            c.frame.effects.cameraCut = true;
        };
        if (FloatSlider("Roughness", &info.roughness, .045f, 1)) { edit("useRoughnessMap", false); edit("roughness", info.roughness); info.roughnessMap=false; }
        if (FloatSlider("Metallic", &info.metallic, 0, 1)) { edit("useMetallicMap", false); edit("metallic", info.metallic); info.metallicMap=false; }
        if (ImGui::Checkbox("Normal map", &info.normalMap)) edit("useNormalMap", info.normalMap);
        if (ImGui::Checkbox("Metallic map", &info.metallicMap)) edit("useMetallicMap", info.metallicMap);
        if (ImGui::Checkbox("Roughness map", &info.roughnessMap)) edit("useRoughnessMap", info.roughnessMap);
        if (ImGui::Checkbox("Occlusion map", &info.aoMap)) edit("useAoMap", info.aoMap);
        ImGui::TextWrapped("Dragging a scalar disables its corresponding texture map.");
        if (ImGui::Button("Reset material")) {
            if (!c.lab.ResetMaterial(c.material)) throw std::runtime_error(c.lab.LastError());
            c.frame.effects.cameraCut = true;
        }
    }
    void DrawLighting(Context& c) {
        auto& e = c.frame.effects;
        FloatSlider("Exposure", &c.frame.exposure, .05f, 8, "%.2f", ImGuiSliderFlags_Logarithmic);
        if (c.animate) ImGui::Checkbox("Animate lights", c.animate);
        if (c.frame.showcase) {
            auto& s=c.frame.showcaseState;
            ImGui::Checkbox("Sunlight", &s.sunlight); ImGui::Checkbox("Point lights", &s.pointLights);
            ImGui::Checkbox("Area lights", &s.areaLights); ImGui::Checkbox("Emission", &s.emission);
            ImGui::Checkbox("Animate scene lights", &s.animateLights);
            ImGui::TextDisabled("Scene toggles control the fixed lighting setup.");
        } else if (c.frame.giRoom) {
            ImGui::Checkbox("Point lamp", &c.frame.giRoomState.pointLight);
            ImGui::Checkbox("Camera flashlight", &c.frame.giRoomState.flashlight);
            Float3Slider("Lamp radiance (RGB)", &c.frame.giRoomState.lightRadiance.x, 0, 100);
            FloatSlider("Lamp source radius", &c.frame.giRoomState.lightRadius, .02f, .5f);
            Float3Slider("Flashlight intensity (RGB)", &e.spotLight.intensity.x, 0, 1000);
            ImGui::TextDisabled("The sealed room has no sunlight or sky fill.");
        } else if (c.frame.lightingStudy) {
            bool area=e.areaLights.count!=0;
            if(ImGui::Checkbox("Rectangle area light",&area)) e.areaLights.count=area?1:0;
            Float3Slider("Area radiance (RGB)",&e.areaLights.lights[0].radiance.x,0,30);
            ImGui::TextDisabled("The lighting study uses an area light and sky; sun is off.");
        } else {
            ImGui::Checkbox("Shadows", &e.shadows.enabled);
            FloatSlider("Sun intensity", &e.shadows.sunIntensity, 0, 12);
            ImGui::ColorEdit3("Sun color", &e.shadows.sunColor.x);
            Float3Slider("Sun direction", &e.shadows.sunDirection.x, -1, 1);
            if (glm::dot(e.shadows.sunDirection,e.shadows.sunDirection)<.0001f) e.shadows.sunDirection={0,-1,0};
            FloatSlider("Shadow distance", &e.shadows.distance, 1, 100);
            FloatSlider("Normal bias (texels)", &e.shadows.normalBiasTexels, 0, 3);
            Combo("Shadow view", e.shadows.debugView, "Shaded\0Visibility\0Cascades\0");
            int atlas=e.shadows.atlasResolution==2048?0:e.shadows.atlasResolution==4096?1:2;
            if (ImGui::Combo("Shadow atlas", &atlas, "2048\0 4096\0 8192\0")) e.shadows.atlasResolution=2048u<<atlas;
        }
        if (!c.frame.giRoom && !c.frame.surfaceCacheStudy) {
            ImGui::Checkbox("Sky light", &e.sky.enabled); ImGui::Checkbox("Sky background", &e.sky.background);
            FloatSlider("Sky intensity", &e.sky.intensity, 0, 10);
            AngleSlider("Sky rotation", &e.sky.rotation, -180, 180);
            ImGui::Checkbox("Planar reflection", &e.reflection.enabled);
            FloatSlider("Reflection strength", &e.reflection.strength, 0, 1);
        }
        if (ImGui::Button("Reset lighting")) {
            e.shadows=baseline_->effects.shadows;e.sky=baseline_->effects.sky;e.reflection=baseline_->effects.reflection;
            e.areaLights=baseline_->effects.areaLights;e.spotLight=baseline_->effects.spotLight;
            c.frame.exposure=baseline_->exposure;e.cameraCut=true;
            c.frame.showcaseState.sunlight=baseline_->showcaseState.sunlight;
            c.frame.showcaseState.pointLights=baseline_->showcaseState.pointLights;
            c.frame.showcaseState.areaLights=baseline_->showcaseState.areaLights;
            c.frame.showcaseState.emission=baseline_->showcaseState.emission;
            c.frame.giRoomState.pointLight=baseline_->giRoomState.pointLight;
            c.frame.giRoomState.flashlight=baseline_->giRoomState.flashlight;
            c.frame.giRoomState.lightRadiance=baseline_->giRoomState.lightRadiance;
            c.frame.giRoomState.lightRadius=baseline_->giRoomState.lightRadius;
        }
    }
    void DrawGi(Context& c) {
        auto& e=c.frame.effects; auto& g=e.lumenGi;
        if(c.frame.surfaceCacheStudy) {
            auto& gi=e.surfaceCacheGi;
            if(c.frame.sealedGiRoomStudy) {
                ImGui::SeparatorText("Recessed ceiling fixture");
                auto& lamp=e.areaLights.lights[0];
                ImGui::Checkbox("Ceiling lamp (L)",&lamp.enabled);
                ImGui::Checkbox("Stratified emitter samples",&e.areaLights.stratifiedSamples);
                ImGui::Checkbox("Direct area shadows enabled",&gi.areaShadows.enabled);
                float radiance=lamp.radiance.r;
                if(ImGui::SliderFloat("Ceiling emitted radiance",&radiance,0,80,"%.1f"))lamp.radiance=radiance*glm::vec3(1,.96f,.89f);
                ImGui::TextWrapped("Sealed room, rectangular area emitter with cached soft shadows. High-albedo dielectric ceramic. F5 compares direct lighting with GI; V selects indirect GI. No sun or ambient fill.");
                ImGui::Separator();
            }
            ImGui::Checkbox("Surface Cache GI",&gi.enabled);
            Combo("GI inspection",gi.view,"Total diffuse GI\0Surface Cache lighting\0Indirect GI\0Cache coverage\0Card atlas\0History length\0Probe geometric coverage\0");
            ImGui::Checkbox("Geometry-aware cache bilinear",&gi.cacheBilinear);
            if(gi.view==2&&gi.useScreenProbes)
                Combo("Probe reconstruction check",gi.reconstructionDebug,"Real lighting\0Constant illumination\0Base / adaptive weights\0Actual-position affine\0Selected probe support\0");
            auto& placement=e.screenProbePlacement;
            if(ImGui::Checkbox("Gather from surface probes",&gi.useScreenProbes))placement.reset=true;
            if(gi.useScreenProbes) {
                ImGui::Checkbox("Directional probe reconstruction",&gi.directionalReconstruction);
                ImGui::Checkbox("Constrain probe ray origins",&gi.safeProbeOrigins);
                if(gi.safeProbeOrigins) {
                    FloatSlider("Origin clearance fraction",&gi.originSafetyFraction,.05f,.95f);
                    FloatSlider("Origin numeric clearance",&gi.originSafetyEpsilon,.000001f,.001f,"%.6f");
                    ImGui::Checkbox("Show actual ray origins",&gi.probeOriginDiagnostics);
                    if(ImGui::Button("Inspect origin validity")) {
                        const auto origins=c.lab.ReadbackSurfaceCacheProbeOrigins();
                        auto& counts=c.frame.probeOriginInspection;counts.fill(0);
                        for(const auto& origin:origins)if(origin.w!=0) {
                            ++counts[0];const int status=int(std::lround(origin.w));
                            if(status>0&&status<=3)++counts[unsigned(status)];
                            else if(status<0&&status>=-5)++counts[unsigned(3-status)];
                        }
                    }
                    const auto& origins=c.frame.probeOriginInspection;
                    ImGui::Text("Last inspected: %u active / %u full / %u clipped / %u uncertain",origins[0],origins[1],origins[2],origins[3]);
                    ImGui::Text("Invalid: data %u / source %u / precision %u / facing %u / clearance %u",origins[4],origins[5],origins[6],origins[7],origins[8]);
                    if(ImGui::Button("Inspect ray status counts")) {
                        const auto rays=c.lab.ReadbackScreenProbeTrace(false);
                        auto& counts=c.frame.probeRayInspection;counts.fill(0);
                        for(const auto& value:rays.radianceStatus) {
                            const int status=int(std::lround(value.w));
                            if(status>=0&&status<int(counts.size()))++counts[unsigned(status)];
                        }
                    }
                    const auto& rays=c.frame.probeRayInspection;
                    ImGui::Text("Last rays: source %llu / launch %llu / outside field %llu / budget %llu",
                        static_cast<unsigned long long>(rays[6]),static_cast<unsigned long long>(rays[16]),
                        static_cast<unsigned long long>(rays[14]),static_cast<unsigned long long>(rays[15]));
                    ImGui::TextWrapped("P shows the status of a ray on each probe marker. Actual origins: cyan full bias, orange clipped, magenta near/backface uncertainty, red invalid. Origin overlay has no depth test. Invalid launch points do not provide black lighting samples; valid black and partially unreadable ray integrals keep their weights.");
                }
                if(ImGui::Checkbox("Probe segment visibility",&placement.reconstructionVisibility))placement.reset=true;
                if(ImGui::Checkbox("Continuous probe interpolation",&placement.continuousInterpolation))placement.reset=true;
                if(ImGui::Checkbox("Curved surface lighting weights",&placement.curvatureAwareWeights))placement.reset=true;
                bool placementJitter=placement.frameIndex==std::numeric_limits<std::uint32_t>::max();
                if(ImGui::Checkbox("Temporal placement jitter",&placementJitter)){placement.frameIndex=placementJitter?std::numeric_limits<std::uint32_t>::max():0;placement.reset=true;}
                if(gi.reconstructionDebug==4) {
                    auto& selected=placement.trace.selectedProbe;
                    const auto maximum=std::max(c.lab.PipelineStatistics().surfaceCacheProbeSlots,1u)-1u;
                    if(selected==std::numeric_limits<std::uint32_t>::max())selected=0;
                    selected=std::min(selected,maximum);
                    UIntSlider("Support diagnostic probe",selected,0,maximum);
                }
                if(ImGui::Checkbox("Adaptive placement",&placement.adaptive))placement.reset=true;
                if(ImGui::Checkbox("Refine geometric intersections",&placement.cornerRefinement))placement.reset=true;
                if(ImGui::Checkbox("Refine surface curvature",&placement.curvatureRefinement))placement.reset=true;
                if(FloatSlider("Curvature normal budget (degrees)",&placement.curvatureNormalAngleDegrees,1,60))placement.reset=true;
                if(FloatSlider("Curvature plane budget (pixels)",&placement.curvaturePlaneErrorPixels,.05f,8))placement.reset=true;
                ImGui::TextWrapped("Lower curvature budgets activate more existing representative slots on curved surfaces. Capacity and rays per probe stay fixed; active work can increase. Requires geometric intersection refinement.");
                if(FloatSlider("Intersection detection (probe tiles)",&placement.cornerDetectionRadius,.25f,2))placement.reset=true;
                if(FloatSlider("Intersection refinement band (tiles)",&placement.cornerProbeSupport,.25f,1.5f))placement.reset=true;
                if(FloatSlider("Intersection normal angle",&placement.cornerNormalAngleDegrees,15,75))placement.reset=true;
                if(UIntSlider("Probe spacing (pixels)",placement.baseSpacing,4,128))placement.reset=true;
                int candidates=placement.candidateCount==4?0:placement.candidateCount==8?1:2;
                if(ImGui::Combo("Adaptive candidates / tile",&candidates,"4\0 8\0 16\0")){placement.candidateCount=4u<<candidates;placement.reset=true;}
                int resolution=placement.trace.rayResolution==4?0:placement.trace.rayResolution==8?1:2;
                if(ImGui::Combo("Rays / probe",&resolution,"16 (4 x 4)\0 64 (8 x 8)\0 256 (16 x 16)\0"))placement.trace.rayResolution=4u<<resolution;
                UIntSlider("Sampling seed",placement.trace.samplingSeed,0,4096);
                ImGui::Checkbox("Freeze probe positions",&placement.freeze);
                if(ImGui::Button("New ray realization"))++placement.trace.samplingSeed;
                if(ImGui::Button("Rebuild probe positions"))placement.reset=true;
                if(ImGui::Button("Inspect active probe counts")) {
                    const auto probes=c.lab.ReadbackScreenProbePlacement();
                    c.frame.placementUniformCount=unsigned(std::count_if(probes.uniform.begin(),probes.uniform.end(),[](const auto& p){return p.w>.5f;}));
                    c.frame.placementAdaptiveCount=unsigned(std::count_if(probes.spawned.begin(),probes.spawned.end(),[](const auto& p){return p.w>.5f;}));
                }
                ImGui::Text("Last inspected active probes: %u base / %u adaptive",c.frame.placementUniformCount,c.frame.placementAdaptiveCount);
            } else UIntSlider("Reference rays / pixel",gi.raysPerPixel,1,32);
            FloatSlider("Indirect intensity",&gi.intensity,0,4);
            FloatSlider("Ray distance",&gi.maxDistance,.1f,100);
            ImGui::Checkbox("Show probe / ray overlay",&gi.probeDiagnostics);
            if(gi.useScreenProbes) {
                auto& spatial=gi.spatial;
                ImGui::SeparatorText("GI spatial filtering");
                ImGui::Checkbox("Spatial filtering (H)",&spatial.enabled);
                UIntSlider("Spatial passes",spatial.passes,1,4);
                UIntSlider("Spatial half kernel (probe tiles)",spatial.halfKernelSize,1,2);
                FloatSlider("Spatial strength",&spatial.strength,0,1);
                FloatSlider("Spatial position weight scale",&spatial.positionWeightScale,100,10000);
                FloatSlider("Spatial hit angle (degrees)",&spatial.maxHitAngleDegrees,1,60);
                FloatSlider("Spatial normal threshold (degrees)",&spatial.normalThresholdDegrees,5,60);
                FloatSlider("Spatial plane / view depth",&spatial.planeDistanceThreshold,.001f,.05f);
                ImGui::Checkbox("Spatial pair visibility test",&spatial.visibilityCheck);
                ImGui::Checkbox("Local adaptive probe neighbors",&spatial.localAdaptiveNeighbors);
                ImGui::TextWrapped("Filter probe ray radiance before integration, then accumulate in time. Larger kernels and more passes cost more and smooth a wider area. Larger position weights and tighter angle / plane thresholds preserve edges more strictly. Visibility rejects probe pairs separated by geometry. Unknown rays remain unknown. B shows geometric coverage: green base probes, orange adaptive, magenta unsupported; colored ray markers alone do not mean the pixel has no probe.");
                auto& contact=gi.contact;
                ImGui::SeparatorText("GI contact reconstruction");
                ImGui::Checkbox("Actual receiver contact rays",&contact.enabled);
                if(contact.enabled) {
                    ImGui::Checkbox("Contact parallel layer validation",&contact.layerValidation);
                    UIntSlider("Contact rays per pixel",contact.raysPerPixel,8,32);
                    FloatSlider("Contact risk distance",&contact.maxDistance,.01f,1.f);
                    FloatSlider("Contact parallax angle (degrees)",&contact.parallaxAngleDegrees,1,45);
                    FloatSlider("Contact risky weight threshold",&contact.weightThreshold,.001f,.5f);
                    UIntSlider("Contact current filter radius",contact.filterRadius,1,2);
                    UIntSlider("Contact current filter passes",contact.filterPasses,1,3);
                    FloatSlider("Contact current filter strength",&contact.filterStrength,0,1);
                    ImGui::Checkbox("Independent contact ray sequence",&contact.temporalDirections);
                    ImGui::Checkbox("Contact coverage-weighted filter",&contact.coverageWeightedFilter);
                    ImGui::Checkbox("Contact hit-distance / curved filter",&contact.geometryGuidedFilter);
                    ImGui::Checkbox("Receiver screen contact detection",&contact.screenContactDetection);
                    ImGui::Checkbox("Coherent near-contact probe risk",&contact.coherentProbeRisk);
                    if(contact.geometryGuidedFilter) {
                        UIntSlider("Contact guided max radius",contact.guideMaxRadius,1,4);
                        FloatSlider("Contact filter hit angle (degrees)",&contact.guideHitAngleDegrees,1,45);
                    }
                }
                ImGui::TextWrapped("A narrow contact shadow at a probe may not represent another pixel on the same plane. Geometry-only risk selects receivers for real local rays before GI history. Higher ray counts cost more; larger risk distance, smaller angle or smaller weight threshold retrace a wider area. Actual rays retain black occlusion and unknown samples in the full ray count. Raw and spatial diagnostics keep their original probe reconstruction.");
                auto& temporal=gi.temporal;
                ImGui::SeparatorText("GI temporal accumulation");
                ImGui::Checkbox("Temporal accumulation (T)",&temporal.enabled);
                ImGui::Checkbox("Static probe convergence (64 frames)",&temporal.staticConvergence);
                UIntSlider("Max accumulated frames",temporal.maxFrames,1,64);
                ImGui::BeginDisabled(temporal.staticConvergence&&temporal.maxFrames>1);
                UIntSlider("Temporal direction phases",temporal.directionPhases,1,64);
                ImGui::EndDisabled();
                if(temporal.staticConvergence&&temporal.maxFrames>1)
                    ImGui::TextWrapped("Static views accumulate up to 64 frames with fresh samples. Moving views keep the shorter history limit above.");
                FloatSlider("History distance / view depth",&temporal.distanceThreshold,.001f,.05f);
                FloatSlider("History normal threshold (degrees)",&temporal.normalThresholdDegrees,5,90);
                FloatSlider("Moving history frames",&temporal.movingMaxFrames,1,float(temporal.maxFrames));
                temporal.movingMaxFrames=std::min(temporal.movingMaxFrames,float(temporal.maxFrames));
                FloatSlider("Motion threshold (pixels)",&temporal.motionThresholdPixels,.1f,32);
                ImGui::Checkbox("History neighborhood clamp",&temporal.neighborhoodClamp);
                if(temporal.neighborhoodClamp){
                    ImGui::Checkbox("Clamp slow camera motion",&temporal.clampSlowMotion);
                    FloatSlider("History clamp sigma",&temporal.clampSigma,1,8);
                }
                ImGui::Checkbox("Geometry edge protection",&temporal.edgeProtection);
                if(temporal.edgeProtection)FloatSlider("Edge history frames",&temporal.edgeMaxFrames,1,
                    std::min(float(temporal.maxFrames),temporal.movingMaxFrames));
                temporal.edgeMaxFrames=std::min(temporal.edgeMaxFrames,temporal.movingMaxFrames);
                ImGui::Checkbox("Young history spatial recovery",&temporal.youngFilter);
                if(temporal.youngFilter) {
                    UIntSlider("Recovery radius (pixels)",temporal.youngFilterRadius,1,4);
                    FloatSlider("Recovery below frames",&temporal.youngFilterMaxFrames,2,16);
                    FloatSlider("Recovery strength",&temporal.youngFilterStrength,0,1);
                }
                ImGui::Checkbox("Retain history on movable transforms",&temporal.retainHistoryOnObjectMotion);
                if(temporal.retainHistoryOnObjectMotion) {
                    FloatSlider("Dynamic lighting history frames",&temporal.dynamicMaxFrames,1,16);
                    FloatSlider("Dynamic lighting clamp sigma",&temporal.dynamicClampSigma,.5f,6);
                    UIntSlider("Dynamic settling frames",temporal.dynamicSettleFrames,0,16);
                }
                if(ImGui::Button("Reset GI history (G)"))temporal.reset=true;
                ImGui::TextWrapped("Camera motion reuses geometrically compatible history. Moving history frames caps accumulation above the pixel-motion threshold. Geometry edge protection keeps moving silhouettes responsive. Young history recovery filters only compatible current-frame pixels until the history matures. Certified movable transforms retain history with a short dynamic lighting cap and clamp, including a settling period; this is conservative global reactivity, not exact per-light dependency tracking. Larger caps reduce noise but respond more slowly. Light, material, topology and unsupported object edits clear history.");
            }
            ImGui::TextWrapped("One diffuse bounce from a two-dimensional stratified cosine hemisphere. Pixels reconstruct irradiance from compatible surface probes, then apply their own albedo. Cache coverage and probe geometric coverage are separate diagnostics. GI history is separate from final-image TAA.");
            return;
        }
        if (c.frame.screenProbeStudy) {
            auto& p=e.screenProbePlacement;
            bool changed=ImGui::Checkbox("Adaptive placement", &p.adaptive);
            changed|=UIntSlider("Probe spacing (pixels)",p.baseSpacing,4,128);
            changed|=FloatSlider("Relative plane threshold", &p.relativeDepthThreshold,.001f,.15f);
            ImGui::Checkbox("Freeze placement", &p.freeze);
            Combo("Placement view",p.debugView,"Active markers\0All candidates\0Coverage\0");
            ImGui::Checkbox("Trace probes",&p.trace.enabled);
            if (ImGui::Button("Rebuild placement")) changed=true;
            ImGui::SameLine();
            if (ImGui::Button("Reset placement settings")) {p=baseline_->effects.screenProbePlacement;changed=true;}
            p.reset|=changed;
            if(c.frame.sdfProbeStudy)DrawSdfStudyControls(c.frame,*baseline_);
            return;
        }
        if (!c.frame.lightingStudy && !c.frame.showcase && !c.frame.giRoom) {
            ImGui::TextWrapped("World-space GI controls need a lighting scene. Press Q outside the UI to open the lighting study, or launch --showcase / --gi-room.");
            return;
        }
        bool changed=false;
        if (!c.frame.giRoom) changed=ImGui::Checkbox("Hybrid GI", &g.enabled);
        else ImGui::TextDisabled("Sealed room: traced world GI is always enabled.");
        if (g.enabled) { e.realtimeGi.enabled=false;e.probes.enabled=false; }
        if (c.frame.giRoom) {
            int view=std::max(1,int(g.lightingView))-1;
            if (ImGui::Combo("Lighting view",&view,"Surface cache / full GI\0Indirect only\0Direct only\0Coverage\0Probe diffuse\0")) {
                g.lightingView=Render::LumenLightingView(view+1);
                c.frame.giRoomState.indirectOnly=g.lightingView==Render::LumenLightingView::IndirectOnly;
                changed=true;
            }
        } else changed|=Combo("Lighting view", g.lightingView,"Material\0Surface cache / full GI\0Indirect only\0Direct only\0Coverage\0Probe diffuse\0");
        changed|=FloatSlider("GI intensity", &g.intensity,0,8);
        if (!c.frame.giRoom) {
            changed|=ImGui::Checkbox("Screen traces", &g.screenTraces);
            changed|=ImGui::Checkbox("GI reflections", &g.reflections);
        }
        changed|=ImGui::Checkbox("Adaptive GI probes", &g.adaptiveProbes);
        changed|=UIntSlider("Adaptive subdivision",g.adaptiveSubdivision,1,4);
        changed|=FloatSlider("Adaptive depth threshold",&g.adaptiveDepthThreshold,.001f,.2f);
        changed|=UIntSlider("Surface updates / frame",g.surfaceUpdatesPerFrame,1,4096);
        changed|=UIntSlider("Surface rays",g.surfaceRays,1,32);
        changed|=UIntSlider("Gather rays",g.gatherRays,1,32);
        changed|=FloatSlider("Surface history",&g.surfaceHistory,0,.98f);
        changed|=FloatSlider("Gather history",&g.gatherHistory,0,.98f);
        if (ImGui::Button("Reset GI settings")) {
            e.lumenGi=baseline_->effects.lumenGi; e.realtimeGi=baseline_->effects.realtimeGi;
            e.probes=baseline_->effects.probes;e.indirect=baseline_->effects.indirect;changed=true;
        }
        c.frame.effects.cameraCut|=changed;
        ImGui::TextWrapped("More rays and surface updates increase GPU work. History reduces noise but may respond more slowly to edits.");
    }
    void DrawPost(Context& c) {
        auto& e=c.frame.effects; auto& p=e.post; auto& t=e.temporal;
        if(c.frame.surfaceCacheStudy) {
            Combo("Tone map",p.toneMap,"Reinhard\0ACES\0None\0");
            ImGui::TextWrapped("GI temporal accumulation is controlled in the GI group. Final-image antialiasing, motion blur, SSAO, Bloom and image filters remain disabled.");
            t.antialiasing=Render::AntialiasingMode::None;t.motionBlurEnabled=false;
            p.ssaoEnabled=false;p.bloomEnabled=false;p.filter=Render::PostFilter::None;
            return;
        }
        bool changed=Combo("Antialiasing",t.antialiasing,"None\0FXAA\0TAA\0");
        int msaa=t.msaaSamples==1?0:t.msaaSamples==2?1:t.msaaSamples==4?2:3;
        if (ImGui::Combo("MSAA",&msaa,"Off (1x)\0 2x\0 4x\0 8x\0")) {t.msaaSamples=1u<<msaa;changed=true;}
        FloatSlider("TAA history weight",&t.historyWeight,0,.98f);
        ImGui::Checkbox("Camera motion blur",&t.motionBlurEnabled);
        FloatSlider("Blur strength",&t.motionBlurStrength,0,4);
        Combo("Tone map",p.toneMap,"Reinhard\0ACES\0None\0");
        Combo("Filter",p.filter,"None\0Grayscale\0Invert\0Sharpen\0Emboss\0Edge\0Gaussian\0Ripple\0");
        ImGui::Checkbox("Bloom",&p.bloomEnabled);
        FloatSlider("Bloom threshold",&p.bloomThreshold,0,8);
        FloatSlider("Bloom strength",&p.bloomStrength,0,4);
        ImGui::Checkbox("SSAO",&p.ssaoEnabled);
        FloatSlider("SSAO radius",&p.ssaoRadius,.01f,3);
        p.ssaoBias=std::min(p.ssaoBias,p.ssaoRadius);
        FloatSlider("SSAO power",&p.ssaoPower,.01f,4);
        FloatSlider("Gamma",&p.gamma,.5f,4);
        FloatSlider("Saturation",&p.saturation,0,2);
        FloatSlider("Contrast",&p.contrast,0,2);
        FloatSlider("Vignette",&p.vignette,0,1);
        if (ImGui::Button("Reset post / AA")) {p=baseline_->effects.post;t=baseline_->effects.temporal;changed=true;}
        e.cameraCut|=changed;
    }
    static void DrawStatistics(Context& c) {
        const auto& io=ImGui::GetIO();const auto s=c.lab.PipelineStatistics();const auto output=c.frame.OutputExtent();
        ImGui::Text("CPU frame %.2f ms | %.1f FPS",io.Framerate>0?1000/io.Framerate:0,io.Framerate);
        ImGui::Text("Render %u x %u | output %u x %u",c.frame.width,c.frame.height,output.x,output.y);
        ImGui::Text("Passes scene %u / shadows %u / post %u",s.scenePasses,s.shadowPasses,s.postPasses);
        ImGui::Text("AA history %s | MSAA %ux",s.historyUsed?"valid":"reset",s.msaaSamples);
        if(c.frame.surfaceCacheStudy) {
            ImGui::Text("Cards: %u texels | updated %u",s.surfaceCacheTexels,s.surfaceCacheUpdatedTexels);
            ImGui::Text("Software GI rays %llu | lighting %s",static_cast<unsigned long long>(s.surfaceCacheRays),s.surfaceCacheLightingUpdated?"updated":"reused");
            ImGui::Text("Probe slots %u | gather passes %u",s.surfaceCacheProbeSlots,s.surfaceCacheProbePasses);
            ImGui::Text("Last inspected active: %u base / %u adaptive",c.frame.placementUniformCount,c.frame.placementAdaptiveCount);
            ImGui::Text("GI spatial passes %u",s.surfaceCacheSpatialPasses);
            ImGui::Text("GI temporal passes %u | history %s",s.surfaceCacheTemporalPasses,s.surfaceCacheHistoryUsed?"reused":"new / disabled");
            ImGui::TextDisabled("Ray count is a capacity budget; inactive probes exit early.");
            return;
        }
        ImGui::Text("GI surface texels %u | updated %u",s.lumenSurfaceTexels,s.lumenUpdatedSurfels);
        ImGui::Text("GI screen probes %u | baked probes %u",s.lumenScreenProbes,s.validDiffuseProbes);
        if(c.frame.screenProbeStudy)ImGui::Text("Placement active %u uniform / %u adaptive",c.frame.placementUniformCount,c.frame.placementAdaptiveCount);
        ImGui::TextDisabled("Counters describe the last submitted frame; no GPU readback.");
    }
    static void DrawHelp(Context& c) {
        ImGui::TextWrapped("F1 toggles this window. Drag sliders or Ctrl+click to type a value. Find panel filters group names; each group has its own reset. Store / restore preset keeps one effects snapshot for this session.");
        ImGui::TextWrapped("Mouse input over this panel edits controls. Scene shortcuts remain available unless editing a field, dragging a control or using a popup. F1 closes the panel and returns input to the scene. Hold RMB to look; WASD / Q / E move. Wheel zooms the gallery or changes viewer speed.");
        if(c.frame.showcase)ImGui::Checkbox("Scene HUD help",&c.frame.showcaseState.help);
        if(c.frame.giRoom)ImGui::Checkbox("Room HUD help",&c.frame.giRoomState.help);
        if(c.frame.screenProbeStudy)ImGui::Checkbox("Probe scene HUD",&c.frame.screenProbeStudyHud);
        if(c.frame.sdfProbeStudy&&!c.frame.surfaceCacheStudy)DrawSdfStudyHelp(c.frame);
    }
    MaterialLabModule& lab_;
    GLFWwindow* window_;
    std::vector<Panel> panels_;
    ImGuiTextFilter filter_;
    std::optional<FrameSettings> baseline_,preset_;
    std::size_t material_=0;
    unsigned baselineScene_=0;
    bool visible_=true,previousF1_=false;
};

// Balanced even when a minimized window skips rendering, a runner switches
// scene, or a capture throws. EndFrame is deferred until after scene captures.
class DebugPanelFrame {
public:
    explicit DebugPanelFrame(DebugPanelHost& host):host_(host),active_(host.Begin()){}
    ~DebugPanelFrame(){if(active_)host_.Cancel();}
    DebugPanelFrame(const DebugPanelFrame&)=delete;
    DebugPanelFrame& operator=(const DebugPanelFrame&)=delete;
    bool WantsMouse() const{return active_&&host_.WantsMouse();}
    bool WantsKeyboard() const{return active_&&host_.WantsKeyboard();}
    void Finish(FrameSettings& frame,std::size_t* selected=nullptr,bool* isolate=nullptr,bool* animate=nullptr) {
        if(!active_)return;
        host_.Draw(frame,selected,isolate,animate);
        const bool ended=host_.End();active_=false;
        if(!ended)throw std::runtime_error(host_.LastError());
    }
private:
    DebugPanelHost& host_;
    bool active_;
};

} // namespace MaterialLab
