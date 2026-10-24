#include "HairSimulation/Public/hair_simulation.h"
#include "HairSimulation/Public/hair_asset.h"
#include "Render/Public/Compute/gpu_compute.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace HairSimulation {
namespace {
using Clock = std::chrono::steady_clock;
using namespace Render::Compute;
struct alignas(16) RestNode { glm::vec4 positionArc, orientation; };
struct alignas(16) Node { glm::vec4 position, orientation, velocity, angularVelocity; };
struct alignas(16) GpuParameters {
    glm::vec4 gravityDt, windDrag;
    glm::uvec4 solver;
    glm::vec4 complianceDamping, shapeCollision, limitsTime, rootTranslation;
    glm::vec4 headA, headB, headA1, headB1;
    glm::uvec4 counts;
};
static_assert(sizeof(RestNode)==32 && sizeof(Node)==64 && sizeof(GpuParameters)==192);
static_assert(sizeof(RenderVertex)==32);
glm::vec4 vec(const HairFloat4& p) { return {p.x,p.y,p.z,p.w}; }
glm::vec3 xyz(const HairFloat4& p) { return {p.x,p.y,p.z}; }
bool finite(glm::vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
float safe(float value, float lo, float hi, float fallback) {
    return std::isfinite(value)?std::clamp(value,lo,hi):fallback;
}
double elapsed(Clock::time_point begin) {
    return std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
}
std::string shaderSource(const char* name) {
#ifdef HAIR_SIMULATION_SHADER_DIR
    auto path=std::filesystem::path(HAIR_SIMULATION_SHADER_DIR)/name;
#else
    auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/"Shaders"/name;
#endif
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error("Cannot open hair compute shader: "+path.string());
    std::ostringstream source; source<<file.rdbuf();
    return source.str();
}
}

struct Simulator::Impl {
    Parameters settings;
    Statistics stats;
    std::unique_ptr<HairAsset> asset;
    std::unique_ptr<Device> gpu;
    std::array<BufferHandle,8> buffers{};
    PipelineHandle simulate{}, interpolate{}, renderTangents{};
    bool lastTangentMode=false;
    std::vector<RenderVertex> vertices;
    std::vector<glm::vec4> guidePositions;
    glm::vec3 translation{0};
    std::string error;
    double accumulator=0;

    std::vector<Binding> bindings(std::uint32_t count=7) const {
        std::vector<Binding> result;
        for(std::uint32_t i=0;i<count;++i) result.push_back({i,buffers[i]});
        return result;
    }
    GpuParameters parameters() const {
        const auto& h=asset->header;
        GpuParameters p{};
        p.gravityDt={settings.gravity,settings.fixedTimeStep};
        p.windDrag={settings.wind,settings.airDrag};
        p.solver={settings.substeps,settings.iterations,h.guideCount,h.renderVertexCount};
        p.complianceDamping={settings.stretchCompliance,settings.bendCompliance,
                             settings.linearDamping,settings.angularDamping};
        p.shapeCollision={settings.shapeMemory,settings.friction,settings.collisionMargin,
                          settings.collisionRadiusScale};
        p.limitsTime={settings.maxLinearSpeed,settings.maxAngularSpeed,
                      static_cast<float>(stats.simulationSeconds),settings.windPulse};
        p.rootTranslation={translation,0};
        // The two original tapered CLSP capsules, transformed by the extracted
        // Head bone's bind matrix into the strand asset's metre coordinate space.
        // Source values and the exact derivation are recorded with asset export.
        p.headA={1.95595e-8f,0.96886315f,-0.00821461f,0.06687821f};
        p.headB={-1.68346e-7f,0.92563673f,0.01308093f,0.05000000f};
        p.headA1={-4.58696e-7f,0.90708285f,0.04598658f,0.04706905f};
        p.headB1={-0.00035261555f,0.96938082f,0.02377333f,0.05984419f};
        p.counts={h.nodeCount,h.strandCount,settings.collision?1u:0u,0};
        return p;
    }
    Dispatch dispatch(PipelineHandle pipeline,std::uint32_t groups,std::uint32_t bindingCount=7) const {
        Dispatch d; d.pipeline=pipeline; d.bindings=bindings(bindingCount); d.groupsX=groups;
        return d;
    }
    void appendRenderDispatches(Batch& batch) const {
        const auto groups=(asset->header.renderVertexCount+127)/128;
        batch.dispatches.push_back(dispatch(interpolate,groups));
        if(settings.reconstructRenderTangents)batch.dispatches.push_back(dispatch(renderTangents,groups,8));
    }
    void finishRenderReadback() {
        readback();lastTangentMode=settings.reconstructRenderTangents;++stats.renderRevision;
    }
    void readback() {
        const auto begin=Clock::now();
        auto output=gpu->Readback(buffers[5]);
        auto state=gpu->Readback(buffers[2]);
        if(output.size()!=vertices.size()*sizeof(RenderVertex) ||
           state.size()!=guidePositions.size()*sizeof(Node))
            throw std::runtime_error("Hair GPU readback size mismatch");
        std::memcpy(vertices.data(),output.data(),output.size());
        stats.readbackBytes=output.size()+state.size();
        stats.maxGuideDisplacement=stats.maxSegmentRelativeError=stats.maxRootError=0;
        stats.nonFiniteNodes=0; stats.gpuRecoveredNodes=0; stats.maxOrientationError=0;
        for(std::size_t i=0;i<guidePositions.size();++i) {
            Node n;
            std::memcpy(&n,state.data()+i*sizeof(Node),sizeof(Node));
            guidePositions[i]=n.position;
            const auto position=glm::vec3(n.position);
            if(!finite(position)||!finite(glm::vec3(n.velocity))||
               !finite(glm::vec3(n.angularVelocity))||!finite(glm::vec3(n.orientation))||
               !std::isfinite(n.orientation.w)) ++stats.nonFiniteNodes;
            if(n.angularVelocity.w>0) ++stats.gpuRecoveredNodes;
            stats.maxOrientationError=std::max(stats.maxOrientationError,
                std::abs(glm::length(n.orientation)-1.0f));
            stats.maxGuideDisplacement=std::max(stats.maxGuideDisplacement,
                glm::length(position-xyz(asset->guideRest[i])-translation));
        }
        for(std::uint32_t guide=0;guide<asset->header.guideCount;++guide) {
            const auto first=asset->guideOffsets[guide], end=asset->guideOffsets[guide+1];
            stats.maxRootError=std::max(stats.maxRootError,
                glm::length(glm::vec3(guidePositions[first])-xyz(asset->guideRest[first])-translation));
            for(auto i=first;i+1<end;++i) {
                const float rest=glm::length(xyz(asset->guideRest[i+1])-xyz(asset->guideRest[i]));
                const float current=glm::length(glm::vec3(guidePositions[i+1]-guidePositions[i]));
                if(rest>1e-8f) stats.maxSegmentRelativeError=std::max(stats.maxSegmentRelativeError,
                    std::abs(current/rest-1.0f));
            }
        }
        for(const auto& vertex:vertices)
            if(!finite(glm::vec3(vertex.positionRadius))||!finite(glm::vec3(vertex.tangentArc)))
                throw std::runtime_error("Hair GPU produced a non-finite render vertex");
        stats.readbackMilliseconds=elapsed(begin);
    }
    bool fail(const std::exception& failure,std::string* out) {
        error=failure.what(); if(out) *out=error; return false;
    }
    bool succeed(std::string* out) { error.clear(); if(out) out->clear(); return true; }
};

Simulator::Simulator():impl_(std::make_unique<Impl>()) {}
Simulator::~Simulator()=default;
Simulator::Simulator(Simulator&&) noexcept=default;
Simulator& Simulator::operator=(Simulator&&) noexcept=default;

bool Simulator::LoadAsset(const std::filesystem::path& path,std::string* error) {
    try {
        auto candidate=std::make_unique<HairAsset>(); std::string reason;
        if(!loadHairAsset(path,*candidate,reason)) throw std::runtime_error(reason+": "+path.string());
        for(std::size_t g=1;g<candidate->guideOffsets.size();++g)
            if(candidate->guideOffsets[g]-candidate->guideOffsets[g-1]>64)
                throw std::runtime_error("Hair guide exceeds GPU solver's 64-node limit");
        impl_->gpu.reset(); impl_->buffers={}; impl_->simulate={}; impl_->interpolate={}; impl_->renderTangents={};
        impl_->lastTangentMode=false;
        impl_->asset=std::move(candidate); impl_->stats={}; impl_->accumulator=0;
        const auto& h=impl_->asset->header;
        impl_->stats.guideCount=h.guideCount; impl_->stats.guideNodes=h.nodeCount;
        impl_->stats.strands=h.strandCount; impl_->stats.renderVertices=h.renderVertexCount;
        impl_->vertices.resize(h.renderVertexCount); impl_->guidePositions.resize(h.nodeCount);
        return impl_->succeed(error);
    } catch(const std::exception& failure) { return impl_->fail(failure,error); }
}

bool Simulator::Initialize(std::string* error) {
    try {
        if(!impl_->asset) throw std::runtime_error("Load a hair asset before initializing Vulkan compute");
        if(impl_->gpu && impl_->stats.gpuReady) return impl_->succeed(error);
        impl_->gpu=Device::CreateVulkan(false);
        if(!impl_->gpu) throw std::runtime_error("Vulkan compute device unavailable");
        const auto& a=*impl_->asset;
        std::vector<RestNode> rest(a.header.nodeCount);
        for(std::size_t i=0;i<rest.size();++i) rest[i]={vec(a.guideRest[i]),vec(a.guideRestOrientation[i])};
        auto& b=impl_->buffers;
        b[0]=impl_->gpu->CreateStorageBuffer(a.guideOffsets.size()*sizeof(std::uint32_t),a.guideOffsets.data(),"Hair guide offsets");
        b[1]=impl_->gpu->CreateStorageBuffer(rest.size()*sizeof(RestNode),rest.data(),"Hair rest rods");
        b[2]=impl_->gpu->CreateStorageBuffer(a.header.nodeCount*sizeof(Node),nullptr,"Hair dynamic rods");
        b[3]=impl_->gpu->CreateStorageBuffer(a.renderRest.size()*sizeof(HairFloat4),a.renderRest.data(),"Hair render rest");
        b[4]=impl_->gpu->CreateStorageBuffer(a.renderBindings.size()*sizeof(HairRenderBinding),a.renderBindings.data(),"Hair three-guide bindings");
        b[5]=impl_->gpu->CreateStorageBuffer(a.header.renderVertexCount*sizeof(RenderVertex),nullptr,"Hair interpolated vertices");
        b[6]=impl_->gpu->CreateStorageBuffer(sizeof(GpuParameters),nullptr,"Hair solver parameters");
        std::vector<glm::uvec2> neighbors(a.header.renderVertexCount);
        for(std::uint32_t strand=0;strand<a.header.strandCount;++strand){
            const auto first=a.strandOffsets[strand],end=a.strandOffsets[strand+1];
            for(auto i=first;i<end;++i)neighbors[i]={i==first?i:i-1,i+1==end?i:i+1};
        }
        static_assert(sizeof(glm::uvec2)==8);
        b[7]=impl_->gpu->CreateStorageBuffer(neighbors.size()*sizeof(glm::uvec2),neighbors.data(),"Hair render-curve neighbors");
        impl_->simulate=impl_->gpu->CreatePipeline(shaderSource("guide_simulate.comp"),7,16,"Cosserat XPBD guides");
        impl_->interpolate=impl_->gpu->CreatePipeline(shaderSource("guide_interpolate.comp"),7,0,"Three-guide interpolation");
        impl_->renderTangents=impl_->gpu->CreatePipeline(shaderSource("render_tangents.comp"),8,0,"Actual render-curve tangents");
        impl_->stats.gpuReady=true;
        impl_->stats.deviceName=impl_->gpu->Diagnostics().deviceName;
        return Reset(error);
    } catch(const std::exception& failure) {
        impl_->stats.gpuReady=false; impl_->gpu.reset();
        return impl_->fail(failure,error);
    }
}

bool Simulator::Reset(std::string* error) {
    try {
        if(!impl_->gpu || !impl_->stats.gpuReady) throw std::runtime_error("Hair Vulkan compute is not initialized");
        SanitizeParameters();
        const auto& a=*impl_->asset;
        std::vector<Node> initial(a.header.nodeCount);
        for(std::size_t i=0;i<initial.size();++i)
            initial[i]={glm::vec4(xyz(a.guideRest[i])+impl_->translation,1),vec(a.guideRestOrientation[i]),glm::vec4(0),glm::vec4(0)};
        impl_->gpu->Upload(impl_->buffers[2],initial.data(),initial.size()*sizeof(Node));
        impl_->accumulator=0; impl_->stats.fixedSteps=0; impl_->stats.simulationSeconds=0;
        impl_->stats.lastFrameSteps=0; impl_->stats.droppedSeconds=0; ++impl_->stats.resets;
        const auto p=impl_->parameters(); impl_->gpu->Upload(impl_->buffers[6],&p,sizeof(p));
        Batch batch; impl_->appendRenderDispatches(batch);
        const auto begin=Clock::now(); impl_->gpu->Submit(batch);
        impl_->stats.submitMilliseconds=elapsed(begin); impl_->stats.dispatches+=batch.dispatches.size();
        impl_->finishRenderReadback();
        return impl_->succeed(error);
    } catch(const std::exception& failure) { return impl_->fail(failure,error); }
}

bool Simulator::RefreshRenderTangents(std::string* error) {
    try {
        if(!impl_->gpu || !impl_->stats.gpuReady) throw std::runtime_error("Hair Vulkan compute is not initialized");
        SanitizeParameters();
        const auto p=impl_->parameters();impl_->gpu->Upload(impl_->buffers[6],&p,sizeof(p));
        Batch batch;impl_->appendRenderDispatches(batch);
        const auto begin=Clock::now();impl_->gpu->Submit(batch);
        impl_->stats.submitMilliseconds=elapsed(begin);impl_->stats.dispatches+=batch.dispatches.size();
        impl_->stats.lastFrameSteps=0;impl_->finishRenderReadback();
        return impl_->succeed(error);
    } catch(const std::exception& failure) { return impl_->fail(failure,error); }
}

bool Simulator::Step(double deltaSeconds,std::string* error) {
    try {
        if(!impl_->gpu || !impl_->stats.gpuReady) throw std::runtime_error("Hair Vulkan compute is not initialized");
        SanitizeParameters();
        if(!std::isfinite(deltaSeconds)||deltaSeconds<0) throw std::runtime_error("Invalid hair frame delta");
        impl_->stats.lastFrameSteps=0;
        // Avoid unbounded work after a debugger pause. Every discarded second
        // is visible in Statistics; the solver's fixed tick is never enlarged.
        impl_->stats.droppedSeconds+=std::max(0.0,deltaSeconds-0.25);
        impl_->accumulator+=std::min(deltaSeconds,0.25);
        const double tick=impl_->settings.fixedTimeStep;
        auto available=static_cast<std::uint32_t>(std::floor((impl_->accumulator+1e-9)/tick));
        const auto steps=std::min(available,impl_->settings.maxFrameSteps);
        if(available>steps) {
            const double drop=(available-steps)*tick;
            impl_->accumulator-=drop; impl_->stats.droppedSeconds+=drop;
        }
        if(!steps) {
            if(impl_->lastTangentMode!=impl_->settings.reconstructRenderTangents)return RefreshRenderTangents(error);
            return impl_->succeed(error);
        }
        auto p=impl_->parameters();
        impl_->gpu->Upload(impl_->buffers[6],&p,sizeof(p));
        Batch batch;
        for(std::uint32_t i=0;i<steps;++i) {
            auto d=impl_->dispatch(impl_->simulate,(impl_->asset->header.guideCount+31)/32);
            const glm::vec4 time(static_cast<float>(impl_->stats.simulationSeconds+i*tick),0,0,0);
            d.pushConstants.resize(sizeof(time));
            std::memcpy(d.pushConstants.data(),&time,sizeof(time));
            batch.dispatches.push_back(std::move(d));
        }
        impl_->appendRenderDispatches(batch);
        const auto begin=Clock::now(); impl_->gpu->Submit(batch);
        impl_->stats.submitMilliseconds=elapsed(begin);
        impl_->stats.dispatches+=batch.dispatches.size(); impl_->stats.fixedSteps+=steps;
        impl_->stats.simulationSeconds+=steps*tick; impl_->stats.lastFrameSteps=steps;
        impl_->accumulator=std::max(0.0,impl_->accumulator-steps*tick);
        impl_->finishRenderReadback();
        return impl_->succeed(error);
    } catch(const std::exception& failure) { return impl_->fail(failure,error); }
}

void Simulator::SetGravityMode(GravityMode mode) {
    switch(mode) {
    case GravityMode::Normal: impl_->settings.gravity={0,-30,0}; break;
    case GravityMode::Low: impl_->settings.gravity={0,-1.6f,0}; break;
    case GravityMode::Zero: impl_->settings.gravity={0,0,0}; break;
    case GravityMode::Custom: break;
    }
}
GravityMode Simulator::GetGravityMode() const {
    auto g=impl_->settings.gravity;
    if(glm::length(g-glm::vec3(0,-30,0))<1e-5f) return GravityMode::Normal;
    if(glm::length(g-glm::vec3(0,-1.6f,0))<1e-5f) return GravityMode::Low;
    if(glm::length(g)<1e-5f) return GravityMode::Zero;
    return GravityMode::Custom;
}
Parameters& Simulator::Settings() { return impl_->settings; }
const Parameters& Simulator::Settings() const { return impl_->settings; }
void Simulator::SanitizeParameters() {
    auto& p=impl_->settings;
    p.fixedTimeStep=safe(p.fixedTimeStep,1.0f/240,1.0f/20,1.0f/60);
    p.substeps=std::clamp(p.substeps,1u,32u); p.iterations=std::clamp(p.iterations,1u,16u);
    for(int i=0;i<3;++i) { p.gravity[i]=safe(p.gravity[i],-100,100,0); p.wind[i]=safe(p.wind[i],-30,30,0); }
    p.windPulse=safe(p.windPulse,0,1,.3f); p.airDrag=safe(p.airDrag,0,20,.3f);
    p.linearDamping=safe(p.linearDamping,0,20,.001f); p.angularDamping=safe(p.angularDamping,0,20,.001f);
    p.stretchCompliance=safe(p.stretchCompliance,1e-12f,.01f,1e-8f);
    p.bendCompliance=safe(p.bendCompliance,1e-10f,.1f,1e-5f);
    p.shapeMemory=safe(p.shapeMemory,0,1,.25f); p.friction=safe(p.friction,0,1,.4f);
    p.collisionMargin=safe(p.collisionMargin,0,.02f,.0002f);
    p.collisionRadiusScale=safe(p.collisionRadiusScale,.5f,1.5f,1);
    p.maxLinearSpeed=safe(p.maxLinearSpeed,.1f,30,3); p.maxAngularSpeed=safe(p.maxAngularSpeed,1,200,30);
    p.maxFrameSteps=std::clamp(p.maxFrameSteps,1u,16u);
}
void Simulator::SetRootTranslation(const glm::vec3& translation) {
    if(finite(translation)) impl_->translation=glm::clamp(translation,glm::vec3(-100),glm::vec3(100));
}
const Statistics& Simulator::Stats() const { return impl_->stats; }
const HairAsset* Simulator::Asset() const { return impl_->asset.get(); }
std::span<const RenderVertex> Simulator::GetRenderVertices() const { return impl_->vertices; }
std::span<const glm::vec4> Simulator::GetGuidePositions() const { return impl_->guidePositions; }
const std::string& Simulator::LastError() const { return impl_->error; }
} // namespace HairSimulation
