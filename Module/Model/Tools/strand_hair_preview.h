#pragma once

// The Model editor retains its OpenGL viewport. Physics and guide interpolation
// are Vulkan compute work; the explicit readback/upload bridge is measured in
// the inspector. No CPU physics fallback is hidden here.
#include "HairSimulation/Public/hair_simulation.h"
#include "HairSimulation/Public/hair_asset.h"
#include "Render/Public/Pipeline/strand_rendering.h"
#include "Render/Public/Pipeline/render_world.h"
#include "strand_submission_history.h"

namespace Model::EditorPreview {
class StrandHairPreview {
    std::unique_ptr<Render::StrandMesh> mesh_;
    std::shared_ptr<const std::vector<Render::StrandVertex>> vertices_,uploadedVertices_,uploadedPrevious_;
    std::vector<float> arc_;
    std::vector<Render::StrandColorCoordinate> colorCoordinates_;
    std::uint64_t sampledStep_=~0ull,sampledReset_=~0ull,sampledRenderRevision_=~0ull,appearanceRevision_=0;
    std::shared_ptr<const std::vector<std::uint32_t>> indices_,uploadedIndices_;
    int strandStride_=1,topologyStride_=0;
    StrandSubmissionHistory history_;
    std::string error_;
    std::shared_ptr<const std::vector<Render::StrandOpaqueCapsule>> opaqueCapsules_;
    static void Help(const char* description) { ImGui::TextDisabled("(?)");if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",description); }
    template<class F> static void Control(const char* help,F&& draw) {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*.45f);draw();ImGui::PopItemWidth();ImGui::SameLine();Help(help);
    }
public:
    HairSimulation::Simulator simulation;
    Render::StrandMaterial material;
    bool loaded=false,visible=true,paused=false;
private:
    bool sampledTangentMode_=true;
public:
    bool Load(const std::filesystem::path& path,HairSimulation::GravityMode mode) {
        loaded=simulation.LoadAsset(path,&error_)&&simulation.Initialize(&error_);
        history_.Clear();vertices_.reset();indices_.reset();uploadedVertices_.reset();uploadedPrevious_.reset();uploadedIndices_.reset();
        topologyStride_=0;sampledStep_=sampledReset_=~0ull;++appearanceRevision_;
        if(loaded){simulation.SetGravityMode(mode);arc_.clear();colorCoordinates_.clear();sampledTangentMode_=simulation.Settings().reconstructRenderTangents;}return loaded;
    }
    bool SetColorCoordinates(std::vector<Render::StrandColorCoordinate> coordinates){
        if(!loaded||coordinates.size()!=simulation.Asset()->header.renderVertexCount){error_="Source appearance does not match loaded hair topology";return false;}
        colorCoordinates_=std::move(coordinates);uploadedVertices_.reset();++appearanceRevision_;return true;
    }
    const std::string& Error()const{return error_;}
    void SetOpaqueCapsules(std::shared_ptr<const std::vector<Render::StrandOpaqueCapsule>> capsules){opaqueCapsules_=std::move(capsules);}
    bool SetRenderTangents(bool enabled) {
        if(simulation.Settings().reconstructRenderTangents==enabled)return true;
        simulation.Settings().reconstructRenderTangents=enabled;
        if(loaded&&!simulation.RefreshRenderTangents(&error_))return false;
        return true;
    }
    bool Step(double delta){return !loaded||simulation.Step(paused?0:delta,&error_);}
    void Controls() {
        using H=HairSimulation::ParameterHelp;
        if(!loaded){ImGui::TextWrapped("%s",error_.empty()?"Launch StartPragmataHair.bat to load Diana strand data.":error_.c_str());return;}
        ImGui::TextColored(ImVec4(.4f,.8f,1,1),"DIANA / VULKAN STRAND HAIR");
        const auto& s=simulation.Stats();
        ImGui::TextWrapped("%s",s.deviceName.c_str());
        ImGui::Text("%zu guides / %zu nodes",s.guideCount,s.guideNodes);
        ImGui::Text("%zu strands / %zu vertices",s.strands,s.renderVertices);
        ImGui::Text("Compute submit %.2f ms",s.submitMilliseconds);
        ImGui::Text("Readback %.2f ms / %.1f MiB",s.readbackMilliseconds,double(s.readbackBytes)/(1024*1024));
        ImGui::Text("Steps %llu | time %.2f s",static_cast<unsigned long long>(s.fixedSteps),s.simulationSeconds);
        ImGui::Text("Root error %.3f mm | strain %.2f%%",s.maxRootError*1000,s.maxSegmentRelativeError*100);
        ImGui::Text("Invalid %u | recovered %u",s.nonFiniteNodes,s.gpuRecoveredNodes);
        ImGui::Text("Dropped time %.3f s",s.droppedSeconds);
        ImGui::TextWrapped("Vulkan simulation -> Render/RHI ribbons, layered coverage, DSVolume and strand temporal accumulation -> display transform.");
        ImGui::Checkbox("显示发丝 / Visible",&visible);ImGui::SameLine();Help("只控制发丝显示；隐藏后仍可模拟，便于观察头部。");
        ImGui::Checkbox("暂停模拟 / Pause",&paused);ImGui::SameLine();Help("暂停物理时间，保留当前状态；相机仍可操作。");
        if(ImGui::Button("重置 / Reset"))simulation.Reset(&error_);ImGui::SameLine();Help("恢复提取的静止发型，并清除速度和物理时钟。");
        int mode=int(simulation.GetGravityMode());const char* modes[]={"正常 (-30)","低重力 (-1.6)","无重力 (0)","自定义"};
        Control(H::Gravity,[&]{if(ImGui::Combo("环境",&mode,modes,4))simulation.SetGravityMode(HairSimulation::GravityMode(mode));});
        auto& p=simulation.Settings();
        if(ImGui::CollapsingHeader("力与阻尼 / Forces",ImGuiTreeNodeFlags_DefaultOpen)) {
            Control(H::Gravity,[&]{if(ImGui::DragFloat3("Gravity",&p.gravity.x,.05f,-50,50))simulation.SetGravityMode(HairSimulation::GravityMode::Custom);});
            Control(H::Wind,[&]{ImGui::DragFloat3("Wind m/s",&p.wind.x,.02f,-10,10);});
            Control(H::WindPulse,[&]{ImGui::SliderFloat("Wind pulse",&p.windPulse,0,1);});
            Control(H::AirDrag,[&]{ImGui::SliderFloat("Air drag",&p.airDrag,0,5);});
            Control(H::LinearDamping,[&]{ImGui::DragFloat("Linear damping",&p.linearDamping,.001f,0,10,"%.3f");});
            Control(H::AngularDamping,[&]{ImGui::DragFloat("Angular damping",&p.angularDamping,.001f,0,10,"%.3f");});
        }
        if(ImGui::CollapsingHeader("杆约束 / Cosserat XPBD")) {
            Control(H::StretchCompliance,[&]{ImGui::SliderFloat("Stretch compliance",&p.stretchCompliance,1e-10f,1e-4f,"%.1e",ImGuiSliderFlags_Logarithmic);});
            Control(H::BendCompliance,[&]{ImGui::SliderFloat("Bend/twist compliance",&p.bendCompliance,1e-9f,1e-1f,"%.1e",ImGuiSliderFlags_Logarithmic);});
            Control(H::ShapeMemory,[&]{ImGui::SliderFloat("Shape memory",&p.shapeMemory,0,1);});
        }
        if(ImGui::CollapsingHeader("碰撞 / Collision")) {
            Control(H::Collision,[&]{ImGui::Checkbox("Collision enabled",&p.collision);});
            Control(H::CollisionRadiusScale,[&]{ImGui::SliderFloat("Proxy radius",&p.collisionRadiusScale,.5f,1.5f);});
            Control(H::CollisionMargin,[&]{ImGui::SliderFloat("Margin m",&p.collisionMargin,0,.01f,"%.4f");});
            Control(H::Friction,[&]{ImGui::SliderFloat("Friction",&p.friction,0,1);});
        }
        if(ImGui::CollapsingHeader("求解质量 / Quality")) {
            int sub=int(p.substeps),it=int(p.iterations),catchup=int(p.maxFrameSteps);
            Control(H::Substeps,[&]{if(ImGui::SliderInt("Substeps",&sub,1,32))p.substeps=sub;});
            Control(H::Iterations,[&]{if(ImGui::SliderInt("Iterations",&it,1,16))p.iterations=it;});
            Control(H::FixedTimeStep,[&]{ImGui::SliderFloat("Fixed dt",&p.fixedTimeStep,1.f/240,1.f/30,"%.5f s");});
            Control(H::MaxFrameSteps,[&]{if(ImGui::SliderInt("Max catch-up",&catchup,1,8))p.maxFrameSteps=catchup;});
            Control(H::MaxLinearSpeed,[&]{ImGui::SliderFloat("Max speed",&p.maxLinearSpeed,.1f,20);});
            Control(H::MaxAngularSpeed,[&]{ImGui::SliderFloat("Max angular",&p.maxAngularSpeed,1,100);});
        }
        if(ImGui::CollapsingHeader("发丝显示 / Rendering")) {
            Control("发丝抽样间隔；1显示全部17,500条。减少数量时按源CPU的倒数比例补偿可见全宽，模拟保持完整。",[&]{ImGui::SliderInt("Strand stride",&strandStride_,1,16);});
            Control("发丝几何全宽倍率，源HairWidthScale为1；像素滤波支撑域与体积归一化宽度各自独立。",[&]{ImGui::SliderFloat("Width scale",&material.widthScale,.3f,3);});
            Control("保留发丝投影宽度的积分覆盖；关闭可对照旧内缩软边造成的覆盖损失，不修改模拟半径和体积密度。",[&]{ImGui::Checkbox("Area preserving coverage",&material.areaPreservingCoverage);});
            Control("发丝基础颜色；预览各向异性双向切线高光，不含游戏动态发光。",[&]{ImGui::ColorEdit3("Hair color",&material.tipColor.x);});
            Control("切线高光粗糙度；较大时高光更宽。",[&]{ImGui::SliderFloat("Roughness",&material.roughness,.05f,1);});
        }
        simulation.SanitizeParameters();
        if(!error_.empty())ImGui::TextWrapped("%s",error_.c_str());
    }
    bool AppendTo(Render::RenderFrame& frame,Render::RHIDevice& device,const glm::dmat4& placement) {
        // An executed hidden/empty frame breaks visibility continuity. Merely
        // recording one still cannot replace the last executed history.
        history_.Stage(frame.frameIndex,{});
        if(!loaded||!visible)return true;
        const auto source=simulation.GetRenderVertices();const auto* asset=simulation.Asset();if(source.empty()||!asset)return true;
        if(!mesh_)mesh_=std::make_unique<Render::StrandMesh>(device);
        if(arc_.size()!=source.size()) {
            arc_.assign(source.size(),0);
            for(std::uint32_t s=0;s<asset->header.strandCount;++s){
                const auto begin=asset->strandOffsets[s],end=asset->strandOffsets[s+1];
                for(auto n=begin+1;n<end;++n){const auto& a=asset->renderRest[n-1];const auto& b=asset->renderRest[n];arc_[n]=arc_[n-1]+glm::length(glm::vec3(b.x-a.x,b.y-a.y,b.z-a.z));}
                const float length=std::max(arc_[end-1],1e-7f);for(auto n=begin;n<end;++n)arc_[n]/=length;
            }
        }
        const auto& stats=simulation.Stats();
        if(sampledTangentMode_!=simulation.Settings().reconstructRenderTangents){
            sampledTangentMode_=simulation.Settings().reconstructRenderTangents;++appearanceRevision_;
        }
        if(!indices_||topologyStride_!=strandStride_) {
            auto topology=std::make_shared<std::vector<std::uint32_t>>();
            for(std::size_t s=0;s<asset->header.strandCount;s+=strandStride_)for(auto n=asset->strandOffsets[s];n+1<asset->strandOffsets[s+1];++n){topology->push_back(n);topology->push_back(n+1);}
            indices_=std::move(topology);topologyStride_=strandStride_;
        }
        if(!vertices_||vertices_->size()!=source.size()||sampledStep_!=stats.fixedSteps||sampledReset_!=stats.resets||sampledRenderRevision_!=stats.renderRevision) {
            auto points=std::make_shared<std::vector<Render::StrandVertex>>(source.size());
            for(std::size_t i=0;i<source.size();++i){(*points)[i]={source[i].positionRadius,source[i].tangentArc};(*points)[i].tangentArc.w=arc_[i];}
            vertices_=std::move(points);sampledStep_=stats.fixedSteps;sampledReset_=stats.resets;sampledRenderRevision_=stats.renderRevision;
        }
        StrandSubmissionState current;current.vertices=vertices_;current.topology=indices_;current.model=glm::mat4(placement);
        current.reset=stats.resets;current.appearance=appearanceRevision_;
        current.raster={material.widthScale,material.minWidthMetres,material.maxWidthMetres,material.minPixelWidth,
            material.maxPixelWidth,material.opacity,material.areaPreservingCoverage?1.f:0.f,1.f/float(strandStride_),
            material.sourceCurveGeometry?1.f:0.f};
        const bool historyValid=history_.CanReproject(current);
        const auto previous=historyValid?history_.Executed().vertices:nullptr;
        // When simulation stops, executed vertices advance from the moving
        // baseline to current once. Only that transition needs another upload.
        if(uploadedVertices_!=vertices_||uploadedPrevious_!=previous||uploadedIndices_!=indices_||!mesh_->Snapshot()) {
            const auto previousSpan=previous?std::span<const Render::StrandVertex>(*previous):std::span<const Render::StrandVertex>{};
            if(!mesh_->Update(*vertices_,*indices_,colorCoordinates_,previousSpan)){error_=mesh_->LastError();return false;}
            uploadedVertices_=vertices_;uploadedPrevious_=previous;uploadedIndices_=indices_;
        }
        frame.strands.push_back({mesh_->Snapshot(),glm::mat4(placement),material});
        frame.strands.back().strandCountFraction=1.f/float(strandStride_);
        frame.strands.back().previousModel=historyValid?history_.Executed().model:current.model;
        frame.strands.back().previousTransformValid=historyValid;
        frame.strands.back().opaqueCapsules=opaqueCapsules_;
        // The editor normalizes the character for display. Preserve the source
        // world-distance shell in that same scale, like skin diffusion's units.
        const float displayScale=float(std::max({glm::length(glm::dvec3(placement[0])),glm::length(glm::dvec3(placement[1])),glm::length(glm::dvec3(placement[2]))}));
        frame.strands.back().material.volumeDepthThreshold*=displayScale;
        history_.Stage(frame.frameIndex,current);
        return true;
    }
    void CommitRenderedFrame(std::uint64_t frameIndex,bool hairExecuted=true){history_.Commit(frameIndex,hairExecuted);}
    void Release(){mesh_.reset();history_.Clear();uploadedVertices_.reset();uploadedPrevious_.reset();uploadedIndices_.reset();}
};
} // namespace Model::EditorPreview
