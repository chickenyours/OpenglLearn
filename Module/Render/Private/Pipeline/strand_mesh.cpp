#include "Render/Public/Pipeline/strand_rendering.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/strand_segment_source.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Render {
namespace {
struct StrandRasterVertex { StrandVertex strand;glm::vec2 colorCoordinate{-1,-1};StrandVertex previous; };
static_assert(sizeof(StrandRasterVertex)==72);
static_assert(offsetof(StrandRasterVertex,previous)==40);
}
struct StrandMesh::Impl {
    RHIDevice& device;
    std::string error;
    std::vector<std::shared_ptr<StrandGeometry>> slots;
    size_t current = std::numeric_limits<size_t>::max();
    std::vector<StrandRasterVertex> ribbonVertices;
    std::vector<std::uint32_t> sourceLines, ribbonIndices;
    std::uint64_t revision = 0;
    explicit Impl(RHIDevice& d) : device(d) {}
};
StrandMesh::StrandMesh(RHIDevice& device) : impl_(std::make_unique<Impl>(device)) {}
StrandMesh::~StrandMesh() = default;
bool StrandMesh::Update(std::span<const StrandVertex> vertices, std::span<const std::uint32_t> lines,
    std::span<const StrandColorCoordinate> colors,std::span<const StrandVertex> previousVertices) {
    impl_->error.clear();
    try {
        if (!impl_->device.IsRunning()) throw std::runtime_error("Strand upload requires a running RHIDevice");
        if (vertices.empty() || lines.empty() || lines.size() % 2 ||(!colors.empty()&&colors.size()!=vertices.size())||
            vertices.size() > std::numeric_limits<uint32_t>::max() / (2 * sizeof(StrandRasterVertex)) ||
            lines.size() > std::numeric_limits<uint32_t>::max() / (3 * sizeof(uint32_t)))
            throw std::invalid_argument("Invalid strand vertex count or line-list topology");
        const bool topologyChanged=impl_->sourceLines.size()!=lines.size()||
            !std::equal(lines.begin(),lines.end(),impl_->sourceLines.begin());
        const bool previousInputMatched=previousVertices.size()==vertices.size()&&
            impl_->current<impl_->slots.size()&&impl_->slots[impl_->current]->pointCount_==vertices.size()&&!topologyChanged;
        // Explicit previous input is validated independently from history
        // acceptance. A count/topology mismatch falls back to current vertices
        // with an invalid flag; it must never create synthetic zero motion.
        if(previousVertices.size()==vertices.size())for(const auto& previous:previousVertices) {
            for(int c=0;c<4;++c)if(!std::isfinite(previous.positionRadius[c])||!std::isfinite(previous.tangentArc[c]))
                throw std::invalid_argument("Previous strand vertices must be finite");
            if(previous.positionRadius.w<0)throw std::invalid_argument("Previous strand radius cannot be negative");
        }
        std::vector<std::uint32_t> newRibbonIndices;
        if(topologyChanged)newRibbonIndices.reserve(lines.size()*3);
        for(size_t i=0;i<lines.size();i+=2) {
            const auto a=lines[i],b=lines[i+1];
            if(a>=vertices.size()||b>=vertices.size()||a==b)
                throw std::invalid_argument("Strand segment references an invalid point");
            if(topologyChanged)newRibbonIndices.insert(newRibbonIndices.end(),{2*a,2*a+1,2*b,2*b,2*a+1,2*b+1});
        }
        const auto& ribbonIndices=topologyChanged?newRibbonIndices:impl_->ribbonIndices;
        impl_->ribbonVertices.resize(vertices.size() * 2);
        glm::vec3 boundsMin(std::numeric_limits<float>::max()),boundsMax(-std::numeric_limits<float>::max());
        double radiusSum=0;
        bool hasDeformationMotion=false;
        for (size_t i = 0; i < vertices.size(); ++i) {
            const auto& point = vertices[i];
            for (int c = 0; c < 4; ++c) {
                if (!std::isfinite(point.positionRadius[c]) || !std::isfinite(point.tangentArc[c]))
                    throw std::invalid_argument("Strand vertices must contain finite positions, radii and tangents");
                if(previousInputMatched&&(point.positionRadius[c]!=previousVertices[i].positionRadius[c]||
                    (c<3&&point.tangentArc[c]!=previousVertices[i].tangentArc[c])))hasDeformationMotion=true;
            }
            if (point.positionRadius.w < 0) throw std::invalid_argument("Strand radius cannot be negative");
            glm::vec2 colorCoordinate(-1,-1);
            if(!colors.empty()){
                colorCoordinate=colors[i].gradientRandom;
                if(!std::isfinite(colorCoordinate.x)||!std::isfinite(colorCoordinate.y)||
                    colorCoordinate.x<0||colorCoordinate.x>1||colorCoordinate.y<0||colorCoordinate.y>1)
                    throw std::invalid_argument("Invalid source strand color coordinates");
            }
            radiusSum+=point.positionRadius.w;
            boundsMin=glm::min(boundsMin,glm::vec3(point.positionRadius)-glm::vec3(point.positionRadius.w));
            boundsMax=glm::max(boundsMax,glm::vec3(point.positionRadius)+glm::vec3(point.positionRadius.w));
            auto left = point, right = point;
            // A signed physical radius encodes each side without an additional
            // vertex attribute; the simulation's input layout remains 32 bytes.
            left.positionRadius.w = -std::max(point.positionRadius.w, 1e-10f);
            right.positionRadius.w = std::max(point.positionRadius.w, 1e-10f);
            auto previousLeft=previousInputMatched?previousVertices[i]:point,previousRight=previousLeft;
            previousLeft.positionRadius.w=-std::max(previousLeft.positionRadius.w,1e-10f);
            previousRight.positionRadius.w=std::max(previousRight.positionRadius.w,1e-10f);
            impl_->ribbonVertices[i*2]={left,colorCoordinate,previousLeft};
            impl_->ribbonVertices[i*2+1]={right,colorCoordinate,previousRight};
        }
        // A queued frame retains its StrandGeometry through encoder.KeepAlive.
        // Only slots with no external references are safe to update in place.
        size_t slot = 0;
        for (; slot < impl_->slots.size(); ++slot)
            if (impl_->slots[slot].use_count() == 1) break;
        const auto vertexCount = uint32_t(impl_->ribbonVertices.size());
        const auto indexCount = uint32_t(ribbonIndices.size());
        if (slot == impl_->slots.size()) {
            PipelineDetail::ResourceBuilder builder(impl_->device);
            CreateMeshBufferDesc desc;
            desc.vertexLayout = {{VertexFieldType::Vec4, VertexFieldType::Vec4, VertexFieldType::Vec2,VertexFieldType::Vec4,VertexFieldType::Vec4}};
            desc.vertexCount = vertexCount; desc.vertexData = impl_->ribbonVertices.data();
            desc.vertexByteSize = impl_->ribbonVertices.size() * sizeof(StrandRasterVertex);
            desc.indexCount = indexCount; desc.indexData = ribbonIndices.data();
            desc.indexByteSize = ribbonIndices.size() * sizeof(uint32_t);
            desc.usage = BufferUsage::Stream;
            auto geometry = std::shared_ptr<StrandGeometry>(new StrandGeometry);
            geometry->mesh_ = builder.Create<VertexBufferSpec>([&](auto cb) { impl_->device.async_CreateMeshBuffer(desc, cb); });
            geometry->lifetime_ = builder.Finish();
            impl_->slots.push_back(std::move(geometry));
        } else {
            UpdateMeshBufferDesc desc;
            desc.vertexCount = vertexCount; desc.vertexData = impl_->ribbonVertices.data();
            desc.vertexByteSize = impl_->ribbonVertices.size() * sizeof(StrandRasterVertex);
            desc.indexCount = indexCount; desc.indexData = ribbonIndices.data();
            desc.indexByteSize = ribbonIndices.size() * sizeof(uint32_t);
            struct Completion { bool done = false, success = false; };
            auto completion = std::make_shared<Completion>();
            impl_->device.async_UpdateMeshBuffer(impl_->slots[slot]->mesh_, desc,
                [completion](bool success) { completion->success = success; completion->done = true; });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!completion->done) {
                impl_->device.returnSystem.DrainCallbacks();
                if (completion->done) break;
                if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Strand RHI upload timed out");
                impl_->device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
            }
            if (!completion->success) throw std::runtime_error("Strand RHI mesh update failed");
        }
        impl_->slots[slot]->pointCount_ = uint32_t(vertices.size());
        impl_->slots[slot]->indexCount_ = indexCount;
        impl_->slots[slot]->boundsMin_=boundsMin;impl_->slots[slot]->boundsMax_=boundsMax;
        impl_->slots[slot]->meanRadius_=float(radiusSum/double(vertices.size()));
        impl_->slots[slot]->previousInputMatched_=previousInputMatched;
        impl_->slots[slot]->hasDeformationMotion_=hasDeformationMotion;
        impl_->slots[slot]->revision_=++impl_->revision;
        impl_->slots[slot]->segmentSource_=MakeStrandSegmentSource(vertices,lines,colors,previousVertices,previousInputMatched);
        if(topologyChanged){impl_->ribbonIndices=std::move(newRibbonIndices);impl_->sourceLines.assign(lines.begin(),lines.end());}
        impl_->current = slot;
        return true;
    } catch (const std::exception& error) { impl_->error = error.what(); return false; }
}
std::shared_ptr<const StrandGeometry> StrandMesh::Snapshot() const {
    return impl_->current < impl_->slots.size() ? impl_->slots[impl_->current] : nullptr;
}
const std::string& StrandMesh::LastError() const { return impl_->error; }
void StrandMesh::Clear() {
    impl_->current = std::numeric_limits<size_t>::max();
    impl_->slots.clear(); impl_->ribbonVertices.clear();
    impl_->sourceLines.clear(); impl_->ribbonIndices.clear(); impl_->error.clear();
}
} // namespace Render
