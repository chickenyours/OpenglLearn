// Included inside MaterialRenderPipeline::Impl, after CaptureSurface().
// Fullscreen temporal reuses object binding one for its normal-transform palette.
static constexpr unsigned SurfaceCacheObjectMotionCapacity=64;
struct alignas(16) SurfaceCacheObjectMotionConstants {
    std::array<glm::mat4,SurfaceCacheObjectMotionCapacity> normalTransforms;
};
struct alignas(16) SurfaceCacheStaticMotionConstants {
    glm::mat4 inverseView{1};glm::uvec4 staticOwners{0};
};
static_assert(sizeof(SurfaceCacheStaticMotionConstants)==80);
bool RecordSurfaceCacheObjectMotion(RHIFrameEncoder& encoder,const RenderFrame& frame,
    const PipelineCamera& camera,const SurfacePassConstants& sourceSurface,
    const SurfaceCacheTemporalMotionState& motion,bool historyValid,bool enabled,bool reuseStatic=false) {
    surfaceCacheObjectMotionStates={};
    const auto& previous=surfaceCacheTemporalCompleted.motionSnapshot;
    const auto& current=motion.snapshot;
    if(!enabled||!current||!current->certified||current->instances.empty()||
        current->instances.size()>SurfaceCacheObjectMotionCapacity)return false;
    const bool reproject=historyValid&&motion.compatible&&previous&&previous->certified&&
        previous->instances.size()==current->instances.size();
    std::array<glm::mat4,SurfaceCacheObjectMotionCapacity> transforms;
    SurfaceCacheObjectMotionConstants constants;
    std::array<bool,SurfaceCacheObjectMotionCapacity> valid{};
    for(unsigned i=0;i<SurfaceCacheObjectMotionCapacity;++i) {
        transforms[i]=glm::mat4(1);constants.normalTransforms[i]=glm::mat4(1);
    }
    const auto affine=[](const glm::mat4& matrix) {
        return Finite(matrix)&&matrix[0].w==0&&matrix[1].w==0&&matrix[2].w==0&&matrix[3].w==1;
    };
    for(unsigned i=0;i<current->instances.size();++i) {
        const auto& now=current->instances[i];
        if(!affine(now.transform))continue;
        const float currentDeterminant=glm::determinant(glm::mat3(now.transform));
        if(!std::isfinite(currentDeterminant)||std::abs(currentDeterminant)<1e-8f)continue;
        valid[i]=true; // Cold/static owner capture uses the current world point.
        if(!reproject)continue;
        const auto& old=previous->instances[i];
        if(old.source!=now.source||old.userId!=now.userId||old.movable!=now.movable||!affine(old.transform))continue;
        const float oldDeterminant=glm::determinant(glm::mat3(old.transform));
        if(!std::isfinite(oldDeterminant)||std::abs(oldDeterminant)<1e-8f)continue;
        transforms[i]=old.transform*glm::inverse(now.transform);
        constants.normalTransforms[i]=glm::mat4(glm::transpose(glm::inverse(glm::mat3(transforms[i]))));
        // The unused homogeneous lane certifies actual instance motion; world
        // round-trip float error must not classify a static receiver as moving.
        constants.normalTransforms[i][3].w=old.transform!=now.transform?2.f:1.f;
        valid[i]=Finite(transforms[i])&&Finite(constants.normalTransforms[i]);
    }
    // The snapshot's order counts every non-overlay item, including a material
    // that cannot supply this capture mode. Never compact its owner indices.
    unsigned instance=0,draws=0,staticOwners=0;
    SurfaceCacheStaticMotionConstants staticConstants;
    staticConstants.staticOwners.z=unsigned(current->instances.size());
    bool useStatic=reuseStatic&&sourceSurface.options.w<0&&
        surfaceCacheStaticMotionProgramsOwner&&surfaceCacheStaticMotionProgram.IsValid();
    if(useStatic){staticConstants.inverseView=glm::inverse(camera.view);useStatic=Finite(staticConstants.inverseView);}
    for(const auto& item:frame.items) {
        if(item.layer==RenderLayer::Overlay)continue;
        if(instance>=current->instances.size())return false;
        const auto index=instance++;
        if(item.model!=current->instances[index].transform)return false;
        const bool eligible=valid[index]&&item.material&&
            (item.EffectiveLayer()==RenderLayer::Opaque||item.EffectiveLayer()==RenderLayer::Cutout);
        valid[index]=false;
        if(!eligible)continue;
        const auto& resources=item.material->Resources();
        // Only the built-in opaque PBR normal variant implements mode four.
        // Other material families leave clear-zero validity and use old rejection.
        valid[index]=resources.normalPipeline.IsValid()&&resources.previousWorldCapture;
        draws+=valid[index];
        // Current positions carry surfaceCacheObject, whereas the old motion
        // pass writes extraction index. They are interchangeable only when
        // every item explicitly certifies the same rigid geometry and owner.
        useStatic=useStatic&&valid[index]&&item.EffectiveLayer()==RenderLayer::Opaque&&
            item.surfaceCacheObject==std::int32_t(index)&&resources.staticWorldCapture&&
            resources.ownerGatedPreviousWorldPipeline.IsValid();
        if(valid[index]&&constants.normalTransforms[index][3].w<=1.5f) {
            staticConstants.staticOwners[index/32]|=std::uint32_t(1u<<(index%32));++staticOwners;
        }
    }
    if(instance!=current->instances.size()||!draws)return false;
    if(!surfaceCacheObjectMotionOwner||surfaceCacheObjectPreviousPositions.width!=width||
        surfaceCacheObjectPreviousPositions.height!=height||surfaceCacheObjectPreviousPositions.depth!=diffuseCapture.depth) {
        PipelineDetail::ResourceBuilder builder(device);Target target;
        target.width=width;target.height=height;target.depth=diffuseCapture.depth;
        target.color=builder.Texture(width,height,RHITextureFormat::RGBA32F);
        target.framebuffer=builder.Target(target.color,target.depth);
        auto buffer=builder.Uniform(sizeof(SurfaceCacheObjectMotionConstants));auto owner=builder.Finish();
        surfaceCacheObjectPreviousPositions=target;surfaceCacheObjectMotionBuffer=buffer;
        surfaceCacheObjectMotionOwner=std::move(owner);
    }
    Require(encoder.KeepAlive(surfaceCacheObjectMotionOwner),"Cannot pin GI receiver-motion resources");
    // An ineligible item skipped above must also prevent the fullscreen path;
    // retaining the old raster pass preserves its partial-validity semantics.
    useStatic=useStatic&&draws==current->instances.size()&&staticOwners>0;
    for(unsigned i=0;i<current->instances.size();++i)if(valid[i])surfaceCacheObjectMotionStates[i]=constants.normalTransforms[i][3].w;
    statistics.surfaceCacheReceiverMotionStaticOwners=staticOwners;
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheReceiverMotion),"Cannot begin GI receiver-motion GPU timing");
    // Temporal binds this image at nine. The shared depth is read/compared by
    // the normal capture pipeline; clearing only color preserves source visibility.
    Require(encoder.BindTexture({},9),"Cannot unbind GI receiver-motion output");
    BindTarget(encoder,surfaceCacheObjectPreviousPositions,RHICommand::ClearColor);
    if(useStatic) {
        Require(encoder.KeepAlive(surfaceCacheStaticMotionProgramsOwner),"Cannot pin static receiver-motion resources");
        Upload(encoder,surfaceCacheStaticMotionBuffer,&staticConstants,sizeof(staticConstants),1);
        Require(encoder.BindPipeline(surfaceCacheStaticMotionProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(indirectPositions.color,0)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot seed static previous receiver positions");
        statistics.surfaceCacheStaticReceiverMotionCapture=true;
    }
    const ViewConstants view{camera.projection*camera.view,glm::vec4(camera.position,1)};
    Upload(encoder,viewBuffer,&view,sizeof(view),0);instance=0;
    for(const auto& item:frame.items) {
        if(item.layer==RenderLayer::Overlay)continue;
        const auto index=instance++;
        if(!valid[index]||(useStatic&&constants.normalTransforms[index][3].w<=1.5f))continue;
        const auto& resources=item.material->Resources();
        auto capture=sourceSurface;capture.view=transforms[index];capture.options.z=useStatic?15.f:4.f;capture.options.w=float(index+1);
        Upload(encoder,surfaceBuffer,&capture,sizeof(capture),SurfacePassBinding);
        Require(encoder.KeepAlive(item.material)&&encoder.BindPipeline(useStatic?resources.ownerGatedPreviousWorldPipeline:resources.normalPipeline),"Cannot bind GI receiver-motion variant");
        for(const auto& binding:resources.textures)
            Require(encoder.BindTexture(binding.texture,binding.slot),"Cannot bind GI receiver-motion material texture");
        if(useStatic)Require(encoder.BindTexture(indirectPositions.color,15),"Cannot bind current owner for receiver-motion overlay");
        const auto& bytes=item.material->Parameters().Bytes();
        if(!bytes.empty())Upload(encoder,resources.parameterBuffer,bytes.data(),static_cast<std::uint32_t>(bytes.size()),2);
        Upload(encoder,objectBuffer,&item.model,sizeof(item.model),1);
        Require(encoder.BindMesh(item.mesh)&&encoder.DrawIndexed(item.draw),"Cannot capture GI previous receiver position");
        ++statistics.surfaceCacheReceiverMotionDraws;
    }
    Upload(encoder,surfaceBuffer,&sourceSurface,sizeof(sourceSurface),SurfacePassBinding);
    // Raster draws used binding one for ObjectData. Bind the palette only after
    // their final draw; the following fullscreen temporal pass has no ObjectData.
    Upload(encoder,surfaceCacheObjectMotionBuffer,&constants,sizeof(constants),1);
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheReceiverMotion),"Cannot end GI receiver-motion GPU timing");
    return true;
}
