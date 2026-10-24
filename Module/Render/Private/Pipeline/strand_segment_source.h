#pragma once
#include "Render/Public/Pipeline/strand_rendering.h"
#include <limits>

namespace Render {
// std430 records. The BRDF is never evaluated on the CPU. The importer retains
// authored source arc/random; topology supplies boundary extrapolation flags.
struct alignas(16) StrandSegmentPoint {
    glm::vec4 positionArc{0},previousPositionArc{0},colorCoordinates{-1,-1,0,0};
};
static_assert(sizeof(StrandSegmentPoint)==48);
struct StrandSegmentSourceData {
    std::vector<StrandSegmentPoint> points;
    // Preserve physical radii separately from the shading producer's 48B
    // records. Curve raster consumers must not infer them from arc/random.
    std::vector<float> radii,previousRadii;
    // start, end, predecessor (or start when extrapolated), isChainRoot.
    std::vector<glm::uvec4> segments;
    bool directedChains=true;
};
inline std::shared_ptr<const StrandSegmentSourceData> MakeStrandSegmentSource(
    std::span<const StrandVertex> vertices,std::span<const std::uint32_t> lines,
    std::span<const StrandColorCoordinate> coordinates,std::span<const StrandVertex> previous,bool validPrevious) {
    auto result=std::make_shared<StrandSegmentSourceData>();
    result->points.resize(vertices.size());result->segments.reserve(lines.size()/2);
    result->radii.resize(vertices.size());result->previousRadii.resize(vertices.size());
    constexpr auto absent=std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> incoming(vertices.size(),absent),outgoing(vertices.size(),absent);
    for(std::size_t i=0;i<vertices.size();++i) {
        const auto& point=vertices[i];const auto& old=validPrevious?previous[i]:point;
        result->radii[i]=point.positionRadius.w;result->previousRadii[i]=old.positionRadius.w;
        result->points[i]={glm::vec4(glm::vec3(point.positionRadius),point.tangentArc.w),
            glm::vec4(glm::vec3(old.positionRadius),old.tangentArc.w),
            coordinates.empty()?glm::vec4(-1,-1,0,0):glm::vec4(coordinates[i].gradientRandom,0,0)};
    }
    for(std::size_t i=0;i<lines.size();i+=2) {
        const auto a=lines[i],b=lines[i+1];
        if(incoming[b]!=absent||outgoing[a]!=absent)result->directedChains=false;
        incoming[b]=a;outgoing[a]=b;
    }
    // Every component of an acyclic, nonbranching directed chain has a root.
    // Count its edges, so a disconnected cycle cannot silently race stores.
    std::size_t visited=0;
    if(result->directedChains)for(std::size_t i=0;i<vertices.size();++i)if(incoming[i]==absent&&outgoing[i]!=absent) {
        auto point=static_cast<std::uint32_t>(i);
        for(std::size_t budget=0;outgoing[point]!=absent&&budget<vertices.size();++budget) {
            ++visited;point=outgoing[point];
        }
    }
    result->directedChains=result->directedChains&&visited==lines.size()/2;
    for(std::size_t i=0;i<lines.size();i+=2) {
        const auto a=lines[i],b=lines[i+1];
        result->segments.emplace_back(a,b,incoming[a]==absent?a:incoming[a],incoming[a]==absent?1u:0u);
    }
    return result;
}
} // namespace Render
