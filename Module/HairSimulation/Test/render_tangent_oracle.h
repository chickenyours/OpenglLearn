#pragma once
#include "HairSimulation/Public/hair_asset.h"
#include "HairSimulation/Public/hair_simulation.h"
#include "Render/Public/Compute/gpu_compute.h"
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace HairTangentOracle {
using HairSimulation::RenderVertex;
using HairSimulation::Simulator;
inline void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
inline glm::dvec3 Position(const RenderVertex& vertex){return glm::dvec3(vertex.positionRadius);}
inline glm::dvec3 SourceDerivative(glm::dvec3 incoming,glm::dvec3 outgoing){
    const double a=glm::length(incoming),b=glm::length(outgoing);
    if(a<=1e-8||b<=1e-8)return glm::dvec3(0);
    // Independently retain the unsimplified source centripetal derivative.
    // The common positive interval denominator disappears on normalization.
    const auto derivative=incoming*std::sqrt(b/a)+outgoing*std::sqrt(a/b);
    const double norm=glm::length(derivative);
    return norm>1e-14?derivative/norm:glm::dvec3(0);
}
inline double ActualCurves(const Simulator& simulator){
    const auto points=simulator.GetRenderVertices();const auto& offsets=simulator.Asset()->strandOffsets;
    double maximumError=0;std::size_t checked=0,endpoints=0;
    for(std::size_t strand=0;strand+1<offsets.size();++strand){
        const auto first=offsets[strand],end=offsets[strand+1];
        for(auto i=first;i<end;++i){
            const auto p=Position(points[i]);
            const auto incoming=i==first?Position(points[i+1])-p:p-Position(points[i-1]);
            const auto outgoing=i+1==end?incoming:Position(points[i+1])-p;
            const auto expected=SourceDerivative(incoming,outgoing),actual=glm::dvec3(points[i].tangentArc);
            Check(std::isfinite(actual.x)&&std::isfinite(actual.y)&&std::isfinite(actual.z),"Actual-curve tangent is nonfinite");
            Check(std::abs(glm::length(actual)-1.)<2e-5,"Actual-curve tangent is not unit length");
            if(glm::dot(expected,expected)<.5)continue; // Native coincident/reversal expression is undefined.
            const double error=glm::length(expected-actual);maximumError=std::max(maximumError,error);++checked;
            endpoints+=i==first||i+1==end;
            Check(error<5e-4,"GPU tangent differs from independent actual-render centripetal derivative");
        }
    }
    Check(checked>points.size()*9/10&&endpoints==2*(offsets.size()-1),"Actual-curve oracle did not cover strand endpoints/topology");
    return maximumError;
}
inline void SameGeometry(const std::vector<RenderVertex>& before,std::span<const RenderVertex> after){
    Check(before.size()==after.size(),"Tangent mode changed the render topology");
    for(std::size_t i=0;i<before.size();++i){
        Check(std::memcmp(&before[i].positionRadius,&after[i].positionRadius,sizeof(glm::vec4))==0,
            "Tangent refresh altered render positions or physical radii");
        Check(before[i].tangentArc.w==after[i].tangentArc.w,"Tangent refresh altered arc coordinates");
    }
}
inline void GuideDirections(const Simulator& simulator){
    const auto guides=simulator.GetGuidePositions();
    const auto points=simulator.GetRenderVertices();
    const auto& asset=*simulator.Asset();double maximumError=0;
    for(std::size_t i=0;i<points.size();++i){
        glm::dvec3 expected(0);bool supported=true;
        for(unsigned slot=0;slot<3;++slot){
            const auto index=asset.renderBindings[i].guideNodes[slot];
            const auto delta=glm::dvec3(guides[index+1]-guides[index]);const double length=glm::length(delta);
            if(length<=1e-7){supported=false;break;}
            expected+=double(asset.renderBindings[i].weights[slot])*delta/length;
        }
        if(!supported||glm::length(expected)<1e-8)continue;
        expected=glm::normalize(expected);maximumError=std::max(maximumError,glm::length(expected-glm::dvec3(points[i].tangentArc)));
    }
    Check(maximumError<5e-5,"Disabled reconstruction no longer uses the legacy guide tangent");
}
inline void SyntheticGpu(){
    using namespace Render::Compute;
    auto device=Device::CreateVulkan(false);Check(bool(device),"Create tangent oracle Vulkan compute device");
    std::vector<RenderVertex> input;
    const std::array<glm::vec3,10> positions{{{0,0,0},{1,0,0},{1,2,0},{1,2,3},
        {100,3,-20},{101,3,-20},{-1,0,0},{-1,0,0},{0,0,0},{-1,0,0}}};
    for(std::size_t i=0;i<positions.size();++i)input.push_back({glm::vec4(positions[i],.00001f),glm::vec4(0,1,0,float(i)*.125f)});
    const std::array<glm::uvec2,10> neighbors{{{0,1},{0,2},{1,3},{2,3},{4,5},{4,5},{6,7},{6,8},{7,9},{8,9}}};
    std::array<std::uint32_t,48> parameters{};parameters[11]=std::uint32_t(input.size());
    const std::array<std::uint32_t,4> zero{};
    std::array<BufferHandle,8> buffers;buffers.fill(device->CreateStorageBuffer(sizeof(zero),zero.data(),"Tangent unused bindings"));
    buffers[5]=device->CreateStorageBuffer(input.size()*sizeof(RenderVertex),input.data(),"Tangent test vertices");
    buffers[6]=device->CreateStorageBuffer(sizeof(parameters),parameters.data(),"Tangent parameters192B");
    buffers[7]=device->CreateStorageBuffer(sizeof(neighbors),neighbors.data(),"Tangent test neighbors");
    const auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/"Shaders"/"render_tangents.comp";
    std::ifstream file(path,std::ios::binary);Check(bool(file),"Read production render tangent shader");std::ostringstream source;source<<file.rdbuf();
    Dispatch dispatch;dispatch.pipeline=device->CreatePipeline(source.str(),8,0,"Actual tangent synthetic oracle");
    for(unsigned i=0;i<buffers.size();++i)dispatch.bindings.push_back({i,buffers[i]});
    Batch batch;batch.dispatches.push_back(std::move(dispatch));device->Submit(batch);
    const auto bytes=device->Readback(buffers[5]);Check(bytes.size()==input.size()*sizeof(RenderVertex),"Tangent test readback size");
    std::vector<RenderVertex> output(input.size());std::memcpy(output.data(),bytes.data(),bytes.size());SameGeometry(input,output);
    const auto match=[&](unsigned i,glm::dvec3 expected){Check(glm::length(glm::dvec3(output[i].tangentArc)-expected)<1e-5,"Synthetic source derivative/strand endpoint mismatch");};
    match(0,{1,0,0});match(1,glm::normalize(glm::dvec3(1,1,0)));match(2,glm::normalize(glm::dvec3(0,1,1)));match(3,{0,0,1});
    match(4,{1,0,0});match(5,{1,0,0});
    for(const auto& point:output){const double length=glm::length(glm::dvec3(point.tangentArc));Check(std::isfinite(length)&&std::abs(length-1.)<2e-5,"Degenerate tangent extension is nonfinite/nonunit");}
    Check(device->Diagnostics().validationErrors==0,"Tangent fixture RHI validation errors");
    std::cout<<"Actual render tangents: unequal intervals, 2/4-point endpoints, isolated strands, coincident/reversing guards and immutable positions/radii/arc passed\n";
}
} // namespace HairTangentOracle
