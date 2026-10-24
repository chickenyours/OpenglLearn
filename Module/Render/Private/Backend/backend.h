#pragma once

#include <optional>
#include <string>
#include <stdexcept>
#include <vector>
#include <unordered_map>

#include "Render/Public/RHICommand/rhi_command.h"
#include "Render/Public/RHICommand/rhi_pipeline_command.h"
#include "Render/Public/RHIResourceType/rhi_resource_type.h"
#include "Render/Private/Backend/rhi_backend_context.h"
#include "Render/Public/Compute/gpu_compute.h"


namespace Render{
    struct BackendUniformBlock {
        uint32_t bytes=0,binding=0;
        std::unordered_map<std::string,uint32_t> offsets;
    };
    struct TextureUploadStatistics {
        // Copies recorded during preparation for the last BeginFrame; initial
        // creations count too. Cancelled deferred updates do not count.
        std::uint64_t regions=0,bytes=0,deferredRegions=0,deferredBytes=0;
        // Dedicated upload submissions/waits, excluding the ordinary frame
        // completion fence shared by every deferred copy in that frame.
        std::uint64_t immediateSubmissions=0,immediateFenceWaits=0;
        std::uint64_t stagingAllocations=0,stagingCapacityBytes=0;
    };
    struct HostFrameStatistics {
        std::uint64_t frameIndex=0;
        double frameBeginMilliseconds=0,commandEncodeMilliseconds=0,frameEndMilliseconds=0;
        double deferredUploadRecordMilliseconds=0,textureUpdateMilliseconds=0;
        double accelerationPrepareMilliseconds=0,accelerationGeometryMilliseconds=0,accelerationRetirementMilliseconds=0;
        double queueSubmitMilliseconds=0,fenceWaitMilliseconds=0,acquireMilliseconds=0,presentMilliseconds=0;
        double timestampReadMilliseconds=0,pipelineCreateMilliseconds=0;
        std::uint64_t drawCalls=0,descriptorAllocations=0,uniformSnapshotCopies=0;
        std::uint64_t accelerationBuilds=0,accelerationStorageReuses=0,accelerationBufferAllocations=0,accelerationStructureCreations=0;
        // accelerationBuilds counts refreshed native scenes (one BLAS + one
        // TLAS). These disjoint counters distinguish full BUILD from UPDATE.
        std::uint64_t accelerationFullBuilds=0,accelerationUpdates=0;
    };
    struct UniformSnapshotStatistics {
        bool enabled=false;
        std::uint64_t contentLookups=0,contentReuses=0,contentComparisons=0,contentHashCollisions=0;
        // Graphics snapshot writes (including zero fallback); ringUsedBytes
        // additionally includes alignment and any in-frame compute snapshots.
        std::uint64_t copiedBytes=0,ringUsedBytes=0,descriptorCacheHits=0,descriptorWrites=0;
    };
    // 后端类方法必须要只在同一个线程内调用
    class IBackend{
        friend class RHIDevice;
        protected:
            RHIBackContext rhiContext_;
            IBackend(const RHIBackContext& rhiContext):rhiContext_(rhiContext){}
            virtual void Init(void* specData) = 0;
            virtual void Shutdown() = 0;
            // buffer
            virtual RenderResourceHandle<VertexBufferSpec> CreateVertexBuffer(const CreateVertexBufferCommand& command) = 0;
            virtual bool UpdateVertexBuffer(const UpdateVertexBufferCommand& command) = 0;
            virtual void DeleteVertexBuffer(const DeleteVertexBufferCommand& command) = 0;
            virtual RenderResourceHandle<UniformBufferSpec> CreateUniformBuffer(const CreateUniformBufferCommand& command) = 0;
            virtual void DeleteUniformBuffer(const DeleteUniformBufferCommand& command) = 0;
            // shader
            virtual RenderResourceHandle<ShaderProgramSpec> CreateGraphicShaderProgram(const CreateGraphicShaderProgramCommand& command) = 0;
            virtual RenderResourceHandle<ShaderSourceSpec> CreateShaderSource(const CreateShaderSourceCommand& command) = 0;
            // Defaults preserve source compatibility for external backends.
            virtual void DeleteShaderSource(const DeleteShaderSourceCommand&) {}
            virtual void DeleteShaderProgram(const DeleteShaderProgramCommand&) {}
            // pipeline
            virtual RenderResourceHandle<PipelineSpec> CreatePipeline(const CreatePipelineCommand&) = 0;
            virtual void DeletePipeline(const DeletePipelineCommand&) = 0;
            // texture
            virtual RenderResourceHandle<RHITextureSpec> CreateTexture(const CreateTextureCommand&) = 0;
            virtual void DeleteTexture(const DeleteTextureCommand&) = 0;
            virtual bool UpdateTexture(const UpdateTextureCommand&) { return false; }
            virtual RenderResourceHandle<RenderTargetSpec> CreateRenderTarget(const CreateRenderTargetCommand&) { return {}; }
            virtual void DeleteRenderTarget(const DeleteRenderTargetCommand&) {}
            // frame
            virtual void SetBackgroundColor(const RHICommand::SetBackgroundColor& command) = 0;
            virtual void Flip(const RHICommand::Flip& command) = 0;
            virtual void SetVertexBuffer(const RHICommand::SetVertexBuffer& command) = 0;
            virtual void SetPipeline(const RHICommand::SetPipeline& command) = 0;
            virtual void Draw(const RHICommand::Draw& command) = 0;
            virtual void DrawInstance(const RHICommand::DrawInstance& command) = 0;
            virtual void DrawRect(const RHICommand::DrawRect& command) = 0;
            virtual void BeginFrame(const RHICommand::BeginFrame& command) = 0;
            virtual void SetRenderTarget(const RHICommand::SetRenderTarget&) {}
            virtual void ResolveRenderTarget(const RHICommand::ResolveRenderTarget&) {}
            virtual void BeginGpuTiming(const RHICommand::BeginGpuTiming&) {}
            virtual void EndGpuTiming(const RHICommand::EndGpuTiming&) {}
            virtual void DispatchFrameCompute(const RHICommand::DispatchFrameCompute&) { throw std::runtime_error("Frame compute unavailable on this backend"); }
            virtual void EndFrame(const RHICommand::EndFrame& command) = 0;
            virtual void SetViewport(const RHICommand::SetViewport& command) = 0;
            virtual void SetScissor(const RHICommand::SetScissor& command) = 0;
            virtual void BindTexture(const RHICommand::BindTexture& command) = 0;
            virtual void BindUniformBuffer(const RHICommand::BindUniformBuffer& command) = 0;
            virtual void UpdateUniformBuffer(const RHICommand::UpdateUniformBuffer& command) = 0;
            virtual void DrawIndexed(const RHICommand::DrawIndexed& command) = 0;
        public:
            virtual ~IBackend() = default;
            // Compute methods are executed on the RHIDevice render thread.
            virtual Compute::BufferHandle ComputeCreateBuffer(size_t, const void*, const std::string&) { throw std::runtime_error("GPU compute unavailable on this backend"); }
            virtual void ComputeUpload(Compute::BufferHandle, const void*, size_t, size_t) { throw std::runtime_error("GPU compute unavailable on this backend"); }
            virtual Compute::PipelineHandle ComputeCreatePipeline(const std::string&, uint32_t, uint32_t, const std::string&) { throw std::runtime_error("GPU compute unavailable on this backend"); }
            virtual Compute::PipelineHandle ComputeCreateFramePipeline(const Compute::FramePipelineDesc&) { throw std::runtime_error("Frame compute unavailable on this backend"); }
            virtual std::vector<uint32_t> CompletedComputeWords(Compute::BufferHandle, uint32_t) const { return {}; }
            virtual void ComputeSubmit(const Compute::Batch&) { throw std::runtime_error("GPU compute unavailable on this backend"); }
            virtual std::vector<std::byte> ComputeReadback(Compute::BufferHandle, size_t, size_t) { throw std::runtime_error("GPU compute unavailable on this backend"); }
            virtual void ComputeCopyToVertexBuffer(Compute::BufferHandle, RenderResourceHandle<VertexBufferSpec>, size_t, size_t, size_t) { throw std::runtime_error("GPU compute unavailable on this backend"); }
            virtual void ComputeDestroy(Compute::BufferHandle) {}
            virtual void ComputeDestroy(Compute::PipelineHandle) {}
            virtual Compute::Statistics ComputeDiagnostics() const { return {}; }

            // Render-thread diagnostics, outside the frontend command protocol.
            virtual bool HardwareRayTracingAvailable() const { return false; }
            virtual bool GeometryShadersAvailable() const { return false; }
            virtual std::string DeviceName() const { return "OpenGL"; }
            virtual std::vector<float> ReadTexture(RenderResourceHandle<RHITextureSpec>) { return {}; }
            virtual std::vector<uint8_t> ReadWindow() { return {}; }
            virtual double LastGpuMilliseconds() const { return 0; }
            virtual RHICommand::GpuTimingResult LastGpuTiming(RHICommand::GpuTimingScope) const { return {}; }
            virtual TextureUploadStatistics LastTextureUploadStatistics() const { return {}; }
            virtual HostFrameStatistics LastHostFrameStatistics() const { return {}; }
            virtual UniformSnapshotStatistics LastUniformSnapshotStatistics() const { return {}; }
            virtual void AbortFrame() {}
            virtual uint32_t ValidationErrorCount() const { return 0; }
            virtual std::optional<BackendUniformBlock> ReflectUniformBlock(RenderResourceHandle<ShaderProgramSpec>, const std::string&) { return {}; }


    };
    // Registered explicitly, so standalone header-only OpenGL modules do not
    // acquire a link dependency on the Vulkan implementation.
    inline IBackend* (*VulkanBackendFactory)(const RHIBackContext&) = nullptr;
}
