// Included inside MaterialRenderPipeline::Impl. Local assets remain resident;
// transformed object metadata and dirty Global atlas rectangles are uploaded.
bool ProbeSdfFastAtlasAddressingEligible(const ProbeSdfTracingSettings& settings) const {
    // The specialized shader has no per-fetch width branch. Object and mesh
    // metadata share the scene width, while triangles have their own atlas.
    return settings.fastAtlasAddressing&&settings.visualMode==0&&settings.scene&&settings.scene->Width()==1024&&
        settings.scene->PackedGeometry()&&settings.scene->PackedGeometry()->Width()==1024;
}
unsigned ProbeSdfTraceVariant(const ProbeSdfTracingSettings& settings) const {
    // Reuse is independent of atlas width; only the integer specialization
    // needs the fixed resident layout. Visualizers keep their original path.
    return (ProbeSdfFastAtlasAddressingEligible(settings)?1u:0u)|
        (settings.reuseTraceSamples&&settings.visualMode==0?2u:0u);
}
unsigned ProbeSdfNativeVariant(const ProbeSdfTracingSettings& settings) const {
    return ProbeSdfTraceVariant(settings)|
        (settings.reuseNativeCandidate&&!settings.directSoftwareRefinement?4u:0u)|
        (settings.directSoftwareRefinement?8u:0u)|(settings.collectRayDiagnostics?16u:0u)|
        (settings.nativeInteriorTracing&&!settings.directSoftwareRefinement?32u:0u);
}
bool ProbeSdfTriangleProfileRequested(const ProbeSdfTracingSettings& settings) const {
    return settings.hardwareRayTracing||settings.directSoftwareRefinement;
}
bool PrepareProbeSdfCompaction(const ScreenProbePlacementSettings& placement) {
    const auto& trace=placement.trace;
    if(!trace.compactSdfRays||!trace.singlePassOutputs||!trace.sdf.enabled||trace.sdf.visualMode!=0)return false;
    if(!sdfCompactCapabilityChecked) {
        struct Result {bool done=false;Compute::Statistics diagnostics;};auto result=std::make_shared<Result>();
        device.async_ExecuteCode([this,result]{result->diagnostics=device.BackendDiagnostics()->ComputeDiagnostics();},[result]{result->done=true;});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!result->done){device.returnSystem.DrainCallbacks();
            Require(std::chrono::steady_clock::now()<deadline,"Compact SDF capability query timed out");
            if(!result->done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(1));}
        sdfCompactSupported=result->diagnostics.frameAvailable;
        sdfCompactMaxBufferBytes=result->diagnostics.maxStorageBufferBytes;sdfCompactCapabilityChecked=true;
    }
    const auto capacity=std::uint64_t(traceRays[0].width)*traceRays[0].height/4;
    // Use the core workgroup-count minimum instead of assuming the current
    // GPU's larger limit. Every queued item is three std430 vec4 records.
    if(!sdfCompactSupported||!capacity||capacity>65535u*64u||capacity>sdfCompactMaxBufferBytes/48u)return false;
    if(!sdfCompactOwner) {
        sdfCompactOwner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
        Compute::FramePipelineDesc reset;reset.source=ProbeSdfDetail::CompactResetShader();reset.label="probe-sdf-compact-reset";
        reset.storageBuffers=1;reset.pushConstantBytes=4;sdfCompactReset=sdfCompactOwner->CreateFramePipeline(reset);
        for(unsigned safe=0;safe<2;++safe){
            Compute::FramePipelineDesc desc;desc.uniformMask=(1u<<9)|(1u<<10)|(1u<<12);
            desc.sampledMask=((1u<<15)-1)&(safe?~0u:~(1u<<10));
            desc.storageBuffers=2;desc.storageImages=1;
            desc.source=ProbeSdfDetail::CompactClassifyShader(safe!=0);desc.label=safe?"probe-sdf-compact-classify-safe":"probe-sdf-compact-classify";
            sdfCompactClassify[safe]=sdfCompactOwner->CreateFramePipeline(desc);
            desc.source=ProbeSdfDetail::CompactTraceShader(safe!=0);desc.label=safe?"probe-sdf-compact-trace-safe":"probe-sdf-compact-trace";
            sdfCompactTrace[safe]=sdfCompactOwner->CreateFramePipeline(desc);
        }
    }
    const unsigned variant=ProbeSdfTraceVariant(trace.sdf),variantIndex=variant?variant-1:0;
    if(variant&&!sdfTraceVariantCompactOwner[variantIndex]) {
        auto owner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
        std::array<Compute::PipelineHandle,2> classify,consume;
        const std::string suffix=variant==1?"fast-address":variant==2?"reuse":"fast-address-reuse";
        for(unsigned safe=0;safe<2;++safe){
            Compute::FramePipelineDesc desc;desc.uniformMask=(1u<<9)|(1u<<10)|(1u<<12);
            desc.sampledMask=((1u<<15)-1)&(safe?~0u:~(1u<<10));desc.storageBuffers=2;desc.storageImages=1;
            desc.source=ProbeSdfDetail::CompactClassifyShader(safe!=0,(variant&1u)!=0,(variant&2u)!=0);
            desc.label=std::string("probe-sdf-compact-classify")+(safe?"-safe-":"-")+suffix;
            classify[safe]=owner->CreateFramePipeline(desc);
            desc.source=ProbeSdfDetail::CompactTraceShader(safe!=0,(variant&1u)!=0,(variant&2u)!=0);
            desc.label=std::string("probe-sdf-compact-trace")+(safe?"-safe-":"-")+suffix;
            consume[safe]=owner->CreateFramePipeline(desc);
        }
        sdfTraceVariantCompactClassify[variantIndex]=classify;sdfTraceVariantCompactTrace[variantIndex]=consume;
        sdfTraceVariantCompactOwner[variantIndex]=std::move(owner);
    }
    if(sdfCompactCapacity!=capacity||!sdfCompactDataOwner) {
        // Recorded but unsubmitted frames can still pin the previous capacity.
        // Give each buffer generation its own facade/lease; replacing this
        // pointer retires old handles only after their last frame pin releases.
        auto dataOwner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
        const auto control=dataOwner->CreateStorageBuffer(16*sizeof(std::uint32_t),nullptr,"probe-sdf-compact-control");
        const auto queue=dataOwner->CreateStorageBuffer(std::size_t(capacity)*48,nullptr,"probe-sdf-compact-queue");
        sdfCompactControl=control;sdfCompactQueue=queue;sdfCompactCapacity=std::uint32_t(capacity);
        sdfCompactDataOwner=std::move(dataOwner);
    }
    return true;
}
void PrepareProbeSdfUploads(const ProbeSdfTracingSettings& s,bool deferUploads) {
    const auto& sceneData=*s.scene;const bool replaced=sdfScene!=s.scene;
    const auto texture=[&](PipelineDetail::ResourceBuilder& b,const auto& pixels,unsigned w) {
        CreateRHITextureSpec d;d.width=w;d.height=unsigned(pixels.size()/w);d.data=pixels.data();
        d.textureDataStoreType=d.textureUseType=RHITextureFormat::RGBA32F;d.mipmaps=false;
        d.filterMode=RHIFilterMode::Nearest;d.addressMode=RHIAddressMode::ClampToEdge;
        return b.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(d,cb);});
    };
    if(!sdfMeshOwner||replaced||sdfMeshGeneration!=sceneData.MeshGeneration()) {
        std::vector<std::uintptr_t> key;
        std::vector<std::shared_ptr<const ProbeMeshSdf>> assets;
        for(const auto& instance:sceneData.Instances()){const auto id=reinterpret_cast<std::uintptr_t>(instance.mesh.get());
            if(std::find(key.begin(),key.end(),id)==key.end()){key.push_back(id);assets.push_back(instance.mesh);}}
        if(const auto found=sdfMeshUploads.find(key);found!=sdfMeshUploads.end()) {
            sdfMeshTexture=found->second.texture;sdfMeshOwner=found->second.owner;statistics.probeSdfMeshCacheHit=true;
        } else {
            PipelineDetail::ResourceBuilder b(device);auto mesh=texture(b,sceneData.MeshPixels(),sceneData.Width());
            auto owner=b.Finish();sdfMeshTexture=mesh;sdfMeshOwner=owner;sdfMeshUploads[key]={mesh,std::move(owner),std::move(assets)};
            statistics.probeSdfMeshUploadedTexels=sceneData.MeshPixels().size();
        }
        sdfMeshGeneration=sceneData.MeshGeneration();
        if(sdfMeshUploads.size()>24){auto old=std::find_if(sdfMeshUploads.begin(),sdfMeshUploads.end(),[&](const auto& item){return item.first!=key;});
            if(old!=sdfMeshUploads.end())sdfMeshUploads.erase(old);}
    }
    const auto& geometry=*sceneData.PackedGeometry();const auto& clipmaps=sceneData.Clipmaps();
    const auto& globalPixels=clipmaps?clipmaps->Pixels():sceneData.GlobalPixels();
    const auto& globalRanges=clipmaps?clipmaps->DirtyRanges():sceneData.DirtyRanges();
    const auto globalGeneration=clipmaps?clipmaps->Generation():sceneData.GlobalGeneration();
    const auto globalWidth=clipmaps?clipmaps->Width():sceneData.Width();
    const auto rangeHeight=clipmaps?unsigned(clipmaps->RangePixels().size()/clipmaps->Width()):0u;
    const bool recreate=!sdfWorldOwner||replaced||sdfGlobalHeight!=globalPixels.size()/globalWidth||
        sdfObjectHeight!=sceneData.ObjectPixels().size()/sceneData.Width()||sdfGeometryHeight!=geometry.Height()||sdfRangeHeight!=rangeHeight;
    if(recreate) {
        PipelineDetail::ResourceBuilder b(device);
        auto objects=texture(b,sceneData.ObjectPixels(),sceneData.Width());
        auto global=texture(b,globalPixels,globalWidth);auto geo=texture(b,geometry.Pixels(),geometry.Width());
        auto ranges=clipmaps?texture(b,clipmaps->RangePixels(),clipmaps->Width()):objects;
        auto owner=b.Finish();sdfObjectTexture=objects;sdfGlobalTexture=global;sdfGeometryTexture=geo;sdfRangeTexture=ranges;sdfWorldOwner=std::move(owner);
        sdfObjectHeight=unsigned(sceneData.ObjectPixels().size()/sceneData.Width());
        sdfGlobalHeight=unsigned(globalPixels.size()/globalWidth);sdfGeometryHeight=geometry.Height();sdfRangeHeight=rangeHeight;
        sdfUploadedGeneration=sceneData.Statistics().generation;
        sdfUploadedGlobalGeneration=globalGeneration;sdfUploadedGeometryGeneration=sceneData.GeometryGeneration();
        sdfUploadedRangeGeneration=clipmaps?globalGeneration:0;
        statistics.probeSdfUploadedTexels=globalPixels.size();statistics.probeSdfUploadRegions=1;
    } else if(sdfUploadedGeneration!=sceneData.Statistics().generation||sdfUploadedGlobalGeneration!=globalGeneration||
              sdfUploadedGeometryGeneration!=sceneData.GeometryGeneration()||(clipmaps&&sdfUploadedRangeGeneration!=globalGeneration)) {
        struct Completion {unsigned remaining=0;bool success=true;};auto done=std::make_shared<Completion>();
        const auto update=[&](RenderResourceHandle<RHITextureSpec> target,unsigned x,unsigned y,unsigned w,unsigned h,const glm::vec4* data,
                              std::shared_ptr<std::vector<glm::vec4>> keepAlive={}) {
            UpdateRHITextureDesc d;d.x=x;d.y=y;d.width=w;d.height=h;d.format=RHITextureFormat::RGBA32F;
            d.deferUntilFrame=deferUploads;
            d.data=data;d.byteSize=std::size_t(w)*h*sizeof(glm::vec4);++done->remaining;
            device.async_UpdateTexture(target,d,[done,keepAlive](bool ok){done->success&=ok;--done->remaining;});
        };
        if(sdfUploadedGeneration!=sceneData.Statistics().generation)
            update(sdfObjectTexture,0,0,sceneData.Width(),sdfObjectHeight,sceneData.ObjectPixels().data());
        if(sdfUploadedGeometryGeneration!=sceneData.GeometryGeneration())
            update(sdfGeometryTexture,0,0,geometry.Width(),geometry.Height(),geometry.Pixels().data());
        // The entire four-level interval atlas is only 256 KiB for the demo.
        // One bounded transfer is cheaper than many tiny brick submissions and
        // independently catches up after frames that did not prepare SDFs.
        if(clipmaps&&sdfUploadedRangeGeneration!=globalGeneration)
            update(sdfRangeTexture,0,0,clipmaps->Width(),rangeHeight,clipmaps->RangePixels().data());
        const bool globalChanged=sdfUploadedGlobalGeneration!=globalGeneration;
        const bool globalCatchup=globalChanged&&(sdfUploadedGlobalGeneration+1!=globalGeneration||globalRanges.empty());
        if(clipmaps&&globalChanged) {
            if(globalCatchup) {
                update(sdfGlobalTexture,0,0,globalWidth,sdfGlobalHeight,globalPixels.data());
                statistics.probeSdfUploadedTexels+=globalPixels.size();++statistics.probeSdfUploadRegions;
            } else {
                // Preserve narrow X slabs: split linear dirty ranges at atlas
                // rows, then merge only identical spans on adjacent rows.
                struct Span {unsigned x,y,width;};std::vector<Span> spans;
                for(const auto& range:globalRanges) {
                    Require(range.offset<=globalPixels.size()&&range.count<=globalPixels.size()-range.offset,"Invalid clipmap dirty range");
                    auto offset=range.offset,count=range.count;
                    while(count) {const auto x=offset%globalWidth,width=std::min(count,globalWidth-x);
                        spans.push_back({x,offset/globalWidth,width});offset+=width;count-=width;}
                }
                std::sort(spans.begin(),spans.end(),[](const Span& a,const Span& b){return a.y!=b.y?a.y<b.y:a.x<b.x;});
                std::vector<Span> merged;merged.reserve(spans.size());
                for(const auto& span:spans) {
                    if(!merged.empty()&&merged.back().y==span.y&&span.x<=merged.back().x+merged.back().width)
                        merged.back().width=std::max(merged.back().x+merged.back().width,span.x+span.width)-merged.back().x;
                    else merged.push_back(span);
                }
                std::sort(merged.begin(),merged.end(),[](const Span& a,const Span& b){
                    if(a.x!=b.x)return a.x<b.x;if(a.width!=b.width)return a.width<b.width;return a.y<b.y;});
                struct Rectangle {unsigned x,y,width,height;};std::vector<Rectangle> rectangles;
                for(std::size_t first=0;first<merged.size();) {
                    const auto span=merged[first];std::size_t end=first+1;
                    while(end<merged.size()&&merged[end].x==span.x&&merged[end].width==span.width&&
                          merged[end].y==span.y+unsigned(end-first))++end;
                    rectangles.push_back({span.x,span.y,span.width,unsigned(end-first)});first=end;
                }
                constexpr std::size_t maximumRegions=128;
                if(rectangles.size()>maximumRegions) {
                    // An XYZ scroll cuts otherwise long X strips into many
                    // fragments. Reconnect matching strips across the cheapest
                    // vertical gaps before considering a coarser tile cover.
                    // Each joined gap's cost is exact, including any padding.
                    struct Gap {std::size_t a,b;std::uint64_t cost;};std::vector<Gap> gaps;
                    std::vector<std::size_t> parent(rectangles.size());
                    for(std::size_t i=0;i<rectangles.size();++i) {
                        parent[i]=i;if(!i)continue;const auto& a=rectangles[i-1];const auto& b=rectangles[i];
                        if(a.x==b.x&&a.width==b.width)
                            gaps.push_back({i-1,i,std::uint64_t(b.y-a.y-a.height)*a.width});
                    }
                    std::sort(gaps.begin(),gaps.end(),[](const Gap& a,const Gap& b){return a.cost<b.cost;});
                    const auto root=[&](std::size_t value) {while(parent[value]!=value){parent[value]=parent[parent[value]];value=parent[value];}return value;};
                    auto remaining=rectangles.size();
                    for(const auto& gap:gaps) {
                        if(remaining<=maximumRegions)break;const auto a=root(gap.a),b=root(gap.b);if(a==b)continue;
                        const auto bottom=std::max(rectangles[a].y+rectangles[a].height,rectangles[b].y+rectangles[b].height);
                        rectangles[a].y=std::min(rectangles[a].y,rectangles[b].y);rectangles[a].height=bottom-rectangles[a].y;
                        parent[b]=a;--remaining;
                    }
                    std::vector<Rectangle> compact;compact.reserve(remaining);
                    for(std::size_t i=0;i<rectangles.size();++i)if(root(i)==i)compact.push_back(rectangles[i]);
                    rectangles=std::move(compact);
                }
                if(rectangles.size()>maximumRegions) {
                    // General fragmented updates get an adaptive conservative
                    // tile cover. This bounds GPU submissions instead of
                    // extending the timeout or uploading the complete atlas.
                    unsigned tileWidth=64,tileHeight=16;
                    do {
                        const auto columns=(globalWidth+tileWidth-1)/tileWidth,rows=(sdfGlobalHeight+tileHeight-1)/tileHeight;
                        std::vector<bool> tiles(std::size_t(columns)*rows,false);
                        for(const auto& span:merged)for(unsigned x=span.x/tileWidth;x<=(span.x+span.width-1)/tileWidth;++x)
                            tiles[std::size_t(span.y/tileHeight)*columns+x]=true;
                        std::vector<Rectangle> tiled;
                        // Greedily consume maximal equal-width row runs.
                        for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<columns;) {
                            if(!tiles[std::size_t(y)*columns+x]){++x;continue;}const auto first=x;
                            while(x<columns&&tiles[std::size_t(y)*columns+x])++x;
                            unsigned height=1;
                            while(y+height<rows) {
                                bool complete=true;for(unsigned column=first;column<x;++column)
                                    complete&=tiles[std::size_t(y+height)*columns+column];
                                if(!complete)break;++height;
                            }
                            for(unsigned row=y;row<y+height;++row)for(unsigned column=first;column<x;++column)
                                tiles[std::size_t(row)*columns+column]=false;
                            const auto left=first*tileWidth,top=y*tileHeight;
                            tiled.push_back({left,top,std::min(globalWidth,x*tileWidth)-left,
                                std::min(sdfGlobalHeight,(y+height)*tileHeight)-top});
                        }
                        rectangles=std::move(tiled);
                        tileWidth=std::min(globalWidth,tileWidth*2);tileHeight=std::min(sdfGlobalHeight,tileHeight*2);
                    } while(rectangles.size()>maximumRegions);
                }
                for(const auto& rectangle:rectangles) {
                    auto transfer=std::make_shared<std::vector<glm::vec4>>(std::size_t(rectangle.width)*rectangle.height);
                    for(unsigned row=0;row<rectangle.height;++row)
                        std::copy_n(globalPixels.data()+std::size_t(rectangle.y+row)*globalWidth+rectangle.x,rectangle.width,
                                    transfer->data()+std::size_t(row)*rectangle.width);
                    update(sdfGlobalTexture,rectangle.x,rectangle.y,rectangle.width,rectangle.height,transfer->data(),transfer);
                    statistics.probeSdfUploadedTexels+=std::uint64_t(rectangle.width)*rectangle.height;++statistics.probeSdfUploadRegions;
                }
            }
        } else if(!clipmaps&&globalChanged) {
            // Keep legacy fixed-volume row uploads and missed-generation catchup.
            std::vector<bool> dirtyRows(sdfGlobalHeight,globalCatchup);
            if(!globalCatchup)for(const auto& range:globalRanges)if(range.count) {
                const auto first=range.offset/globalWidth,last=(range.offset+range.count-1)/globalWidth;
                for(unsigned row=first;row<=last;++row)dirtyRows[row]=true;
            }
            for(unsigned row=0;row<sdfGlobalHeight;) {
                if(!dirtyRows[row]){++row;continue;}const auto first=row;
                while(row<sdfGlobalHeight&&dirtyRows[row])++row;
                update(sdfGlobalTexture,0,first,globalWidth,row-first,globalPixels.data()+std::size_t(first)*globalWidth);
                statistics.probeSdfUploadedTexels+=std::uint64_t(row-first)*globalWidth;++statistics.probeSdfUploadRegions;
            }
        }
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(done->remaining){device.returnSystem.DrainCallbacks();
            Require(std::chrono::steady_clock::now()<deadline,"SDF dirty uploads timed out");
            if(done->remaining)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(1));}
        Require(done->success,"Cannot upload SDF dirty regions");sdfUploadedGeneration=sceneData.Statistics().generation;
        sdfUploadedGlobalGeneration=globalGeneration;sdfUploadedGeometryGeneration=sceneData.GeometryGeneration();
        sdfUploadedRangeGeneration=clipmaps?globalGeneration:0;
    }
    sdfScene=s.scene;
}
bool ProbeSdfNativeUploadPruningEligible(const ScreenProbePlacementSettings& placement,const SurfaceCacheGiSettings& cache) const {
    const auto& trace=placement.trace;const auto& s=trace.sdf;
    if(!s.nativeUploadPruning||!ProbeSdfTriangleProfileRequested(s)||s.visualMode||s.sliceView||s.showHitPoints||trace.compactSdfRays||
        (trace.singleInvocation&&!trace.singlePassOutputs)||
        !cache.enabled||!cache.useScreenProbes||!cache.cache||cache.scene!=s.scene||!surfaceCacheHardwareSupported)return false;
    const bool gather=cache.view==0||cache.view==2||cache.view==6||(cache.view==5&&cache.temporal.enabled)||
        cache.probeDiagnostics||cache.probeOriginDiagnostics;
    if(!gather)return false;
    // The SDF targets allocated below have the exact extent of traceRays. Use
    // the same already-prepared output and GS predicates as recording, without
    // probing capability or changing which native variant is selected.
    const bool outputs=trace.singlePassOutputs&&sdfOutputProgramsOwner&&traceOutputTargetsOwner&&SingleInvocationTraceTarget(traceRays[0]);
    const bool single=!outputs&&trace.singleInvocation&&sdfSingleProgramsOwner&&SingleInvocationTraceTarget(traceRays[0]);
    const unsigned mode=outputs?1u:(single?2u:0u),variant=ProbeSdfNativeVariant(s);
    return mode<2&&bool(sdfNativeProgramsOwner[variant][mode]);
}
void PrepareProbeSdfTrace(const ScreenProbePlacementSettings& placement,const SurfaceCacheGiSettings& cache,bool deferUploads=false) {
    sdfNativeUploadsPruned=false;
    const auto& s=placement.trace.sdf;if(!placement.enabled||!placement.trace.enabled||!s.enabled)return;
    Require(bool(s.scene),"SDF tracing needs a prepared distance-field scene");
    if(!sdfProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);
        sdfFallbackProgram=b.FullscreenPipeline(FullscreenVertexShader(),ProbeSdfDetail::FallbackShader());
        sdfSafeFallbackProgram=b.FullscreenPipeline(FullscreenVertexShader(),ProbeSdfDetail::FallbackShader(true));
        sdfSliceProgram=b.FullscreenPipeline(FullscreenVertexShader(),ProbeSdfDetail::SliceDisplayShader());
        CreateGraphicShaderDesc desc;
        desc.vertexShaderSource=b.Source(ShaderSourceType::Vertex,ProbeSdfDetail::HitVertexShader());
        desc.fragmentShaderSource=b.Source(ShaderSourceType::Fragment,ProbeSdfDetail::HitFragmentShader());
        auto program=b.Create<ShaderProgramSpec>([&](auto cb){device.async_CreateGraphicShader(desc,cb);});
        PipelineSpec marker;marker.shaderProgram=program;marker.expectVertexLayout={{VertexFieldType::Vec2,VertexFieldType::Vec2}};
        marker.cullMode=CullMode::None;marker.depthTest=true;marker.depthWrite=false;marker.depthCompare=CompareOp::LessEqual;
        sdfHitMarkerProgram=b.Create<PipelineSpec>([&](auto cb){device.async_CreatePipeline({marker},cb);});
        sdfBuffer=b.Uniform(sizeof(ProbeSdfTraceConstants));
        if(!surfaceCacheBuffer.IsValid())surfaceCacheBuffer=b.Uniform(sizeof(SurfaceCacheGiConstants));
        sdfProgramsOwner=b.Finish();
    }
    if(placement.trace.singleInvocation&&s.visualMode==0&&!sdfSingleProgramsOwner&&PrepareSingleInvocationTraceCapability()) {
        PipelineDetail::ResourceBuilder b(device);
        const auto vertex=ScreenProbeTraceDetail::RayQuadVertexShader(),fragment=ScreenProbeTraceDetail::RayQuadFragmentShader();
        sdfSingleFallbackProgram=b.RayQuadPipeline(vertex,ProbeSdfDetail::FallbackGeometryShader(),fragment);
        sdfSingleSafeFallbackProgram=b.RayQuadPipeline(vertex,ProbeSdfDetail::FallbackGeometryShader(true),fragment);
        sdfSingleProgramsOwner=b.Finish();
    }
    if(placement.trace.singlePassOutputs&&s.visualMode==0&&!sdfOutputProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        sdfOutputFallbackProgram=b.FullscreenPipeline(vertex,ProbeSdfDetail::FallbackOutputsShader());
        sdfOutputSafeFallbackProgram=b.FullscreenPipeline(vertex,ProbeSdfDetail::FallbackOutputsShader(true));
        sdfOutputProgramsOwner=b.Finish();
    }
    const unsigned variant=ProbeSdfTraceVariant(s),variantIndex=variant?variant-1:0;
    if(variant) {
        const unsigned mode=placement.trace.singlePassOutputs?1u:
            (placement.trace.singleInvocation&&traceGeometrySupported?2u:0u);
        if(!sdfTraceVariantProgramsOwner[variantIndex][mode]) {
            PipelineDetail::ResourceBuilder builder(device);std::array<RenderResourceHandle<PipelineSpec>,2> programs;
            for(unsigned safe=0;safe<2;++safe) {
                if(mode==2)programs[safe]=builder.RayQuadPipeline(ScreenProbeTraceDetail::RayQuadVertexShader(),
                    ProbeSdfDetail::FallbackGeometryShader(safe!=0,(variant&1u)!=0,(variant&2u)!=0),ScreenProbeTraceDetail::RayQuadFragmentShader());
                else programs[safe]=builder.FullscreenPipeline(FullscreenVertexShader(),mode==1?
                    ProbeSdfDetail::FallbackOutputsShader(safe!=0,(variant&1u)!=0,(variant&2u)!=0):
                    ProbeSdfDetail::FallbackShader(safe!=0,(variant&1u)!=0,(variant&2u)!=0));
            }
            auto owner=builder.Finish();sdfTraceVariantPrograms[variantIndex][mode]=programs;
            sdfTraceVariantProgramsOwner[variantIndex][mode]=std::move(owner);
        }
    }
    // Compute compaction uses set 2 for its work queue and the GS backend does
    // not bind a ray-query AS. Retain their accepted software variants rather
    // than silently selecting an unsupported shader stage or descriptor set.
    if(ProbeSdfTriangleProfileRequested(s)&&s.visualMode==0&&!placement.trace.compactSdfRays&&
        (placement.trace.singlePassOutputs||!placement.trace.singleInvocation)&&PrepareSurfaceCacheHardwareCapability()) {
        const unsigned mode=placement.trace.singlePassOutputs?1u:0u;
        const unsigned nativeVariant=ProbeSdfNativeVariant(s);
        if(!sdfNativeProgramsOwner[nativeVariant][mode]) {
            PipelineDetail::ResourceBuilder builder(device);std::array<RenderResourceHandle<PipelineSpec>,2> programs;
            for(unsigned safe=0;safe<2;++safe)programs[safe]=builder.FullscreenPipeline(FullscreenVertexShader(),mode==1?
                ProbeSdfDetail::FallbackOutputsShader(safe!=0,(variant&1u)!=0,(variant&2u)!=0,true,s.reuseNativeCandidate,s.directSoftwareRefinement,s.collectRayDiagnostics,s.nativeInteriorTracing):
                ProbeSdfDetail::FallbackShader(safe!=0,(variant&1u)!=0,(variant&2u)!=0,true,s.reuseNativeCandidate,s.directSoftwareRefinement,s.collectRayDiagnostics,s.nativeInteriorTracing));
            auto owner=builder.Finish();sdfNativePrograms[nativeVariant][mode]=programs;
            sdfNativeProgramsOwner[nativeVariant][mode]=std::move(owner);
        }
    }
    if(s.visualMode&&!sdfVolumeOwners[s.visualMode-1]) {
        // Compile only the requested view. A mesh-only shader need not reserve
        // registers for the clipmap polynomial solver, and vice versa.
        PipelineDetail::ResourceBuilder b(device);
        sdfVolumePrograms[s.visualMode-1]=b.FullscreenPipeline(FullscreenVertexShader(),ProbeSdfVisualizationDetail::VolumeShader(s.visualMode));
        sdfVolumeOwners[s.visualMode-1]=b.Finish();
    }
    const auto& sceneData=*s.scene;const auto& clipmaps=sceneData.Clipmaps();
    sdfNativeUploadsPruned=ProbeSdfNativeUploadPruningEligible(placement,cache);
    // Keep the last uploaded scene/generations unchanged when pruning. A later
    // software/debug frame then takes the original replacement/full catch-up
    // path, including a scene swap whose generation numbers happen to match.
    if(!sdfNativeUploadsPruned)PrepareProbeSdfUploads(s,deferUploads);
    const bool compact=PrepareProbeSdfCompaction(placement);
    if(s.visualMode==0&&(!sdfViewOwner||sdfRays[0].width!=traceRays[0].width||sdfRays[0].height!=traceRays[0].height||(compact&&!sdfRaysStorage))) {
        PipelineDetail::ResourceBuilder b(device);std::array<Target,2> rays;
        for(auto& ray:rays){ray.width=traceRays[0].width;ray.height=traceRays[0].height;
            ray.color=compact?b.StorageTexture(ray.width,ray.height,RHITextureFormat::RGBA32F):b.Texture(ray.width,ray.height,RHITextureFormat::RGBA32F);
            ray.framebuffer=b.Target(ray.color,{});}
        auto owner=b.Finish();sdfRays=rays;sdfViewOwner=std::move(owner);sdfRaysStorage=compact;
    }
    statistics.probeSdfUpdatedVoxels=clipmaps?clipmaps->Statistics().updatedVoxels:sceneData.Statistics().updatedVoxels;
    statistics.probeSdfUpdatedBricks=clipmaps?clipmaps->Statistics().updatedBricks:sceneData.Statistics().updatedBricks;
}
ProbeSdfTraceConstants ProbeSdfConstants(const ProbeSdfTracingSettings& s) const {
    ProbeSdfTraceConstants c;const auto& data=*s.scene;const auto& grid=data.Settings();const auto& geometry=*data.PackedGeometry();
    c.globalOrigin=glm::vec4(grid.origin,grid.maxDistance);c.globalSpacing=glm::vec4(grid.spacing,data.MaximumWorldMeshError());
    c.globalCounts=glm::ivec4(glm::ivec3(grid.counts),int(data.Width()));
    c.atlas={int(data.Width()),int(data.Width()),int(data.Instances().size()),1};
    c.geometry={int(geometry.NodeCount()*2),int(geometry.Width()),int(geometry.NodeCount()),int(geometry.TriangleCount())};
    c.trace={s.meshTraceDistance,float(s.maxSteps),s.hitEpsilon,.5f*glm::length(grid.spacing)+data.MaximumWorldMeshError()};
    c.display={int(s.sliceView),int(s.selectedObject),int(s.sliceAxis),int(s.visualMode)};c.slice={s.slicePosition,0,s.visualEpsilon,s.showVoxelGrid?1.f:0.f};
    c.flags={s.useMesh?1:0,s.useGlobal?1:0,int(ProbeSdfObjectTexels),s.visualizeSelected?1:0};
    if(const auto& clipmaps=data.Clipmaps()) {
        const auto& levels=clipmaps->Levels();const auto count=std::min(levels.size(),c.clipOrigins.size());
        c.globalCounts.w=int(clipmaps->Width());
        c.clipmap={int(count),s.clipmapLevel,s.showClipmapLevels?1:0,s.showClipmapBounds?1:0};
        c.rangeAtlas={int(clipmaps->Width()),int(clipmaps->Settings().brickSize),1,0};
        for(std::size_t i=0;i<count;++i) {const auto& level=levels[i];
            c.clipOrigins[i]=glm::vec4(level.origin,level.narrowBand);
            c.clipSpacing[i]=glm::vec4(level.spacing,level.sourceInterpolationError+.5f*glm::length(level.spacing));
            c.clipCounts[i]=glm::ivec4(glm::ivec3(level.counts),int(level.atlasOffset));
            c.clipScroll[i]=glm::ivec4(glm::ivec3(level.scroll),level.generation?1:0);
            c.clipRanges[i]=glm::ivec4(glm::ivec3(level.rangeCounts),int(level.rangeAtlasOffset));
        }
    }
    return c;
}
void RecordProbeSdfFallback(RHIFrameEncoder& encoder,const ScreenProbePlacementSettings& s,unsigned probeImage,unsigned rayImage) {
    const bool safeOrigins=surfaceCachePending.valid&&surfaceCacheSafeOriginsActive&&s.trace.cosineHemisphere;
    auto c=ProbeSdfConstants(s.trace.sdf);c.atlas.w=surfaceCachePending.valid?3:1;
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::ProbeSdfTrace),"Cannot begin probe SDF trace GPU timing");
    Upload(encoder,sdfBuffer,&c,sizeof(c),ProbeSdfTracingBinding);
    if(surfaceCachePending.valid) {
        Require(encoder.BindUniformBuffer(surfaceCacheBuffer,SurfaceCacheGiBinding),"Cannot bind Surface Cache lookup constants");
        BindSurfaceCache(encoder);
    } else {
        SurfaceCacheGiConstants disabled;Upload(encoder,surfaceCacheBuffer,&disabled,sizeof(disabled),SurfaceCacheGiBinding);
        Require(encoder.BindTexture({},12)&&encoder.BindTexture({},13)&&encoder.BindTexture({},14),"Cannot clear Surface Cache lookup textures");
    }
    const bool outputs=s.trace.singlePassOutputs&&sdfOutputProgramsOwner&&traceOutputTargetsOwner&&
        SingleInvocationTraceTarget(sdfRays[rayImage]);
    const auto rayCapacity=std::uint32_t(std::uint64_t(sdfRays[rayImage].width)*sdfRays[rayImage].height/4);
    const bool compact=outputs&&s.trace.compactSdfRays&&sdfCompactOwner&&sdfCompactDataOwner&&sdfRaysStorage&&sdfCompactCapacity==rayCapacity;
    const unsigned variant=ProbeSdfTraceVariant(s.trace.sdf),variantIndex=variant?variant-1:0;
    if(compact) {
        const bool extra=variant&&sdfTraceVariantCompactOwner[variantIndex];
        if(extra)Require(encoder.KeepAlive(sdfTraceVariantCompactOwner[variantIndex]),"Cannot pin compact SDF execution programs");
        Require(encoder.BindTexture(traceRays[rayImage].color,0)&&encoder.BindTexture(traceSource.color,1)&&
            encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
            encoder.BindTexture(placementUniform[probeImage].color,4)&&encoder.BindTexture(placementSpawned[probeImage].color,5)&&
            encoder.BindTexture(sdfMeshTexture,6)&&encoder.BindTexture(sdfObjectTexture,7)&&encoder.BindTexture(sdfGlobalTexture,8)&&
            encoder.BindTexture(sdfGeometryTexture,9)&&encoder.BindTexture(sdfRangeTexture,11)&&
            encoder.BindTexture(safeOrigins?surfaceCacheProbeOrigins.color:RenderResourceHandle<RHITextureSpec>{},10),"Cannot bind compact SDF sources");
        RHICommand::DispatchFrameCompute reset;reset.pipeline=sdfCompactReset;reset.buffers[0]=sdfCompactControl;reset.pushBytes=4;
        std::memcpy(reset.pushConstants.data(),&rayCapacity,sizeof(rayCapacity));
        Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::ProbeSdfKernel),"Cannot begin compact SDF kernel timing");
        Require(encoder.DispatchCompute(reset),"Cannot reset compact SDF work queue");
        RHICommand::DispatchFrameCompute classify;classify.pipeline=(extra?sdfTraceVariantCompactClassify[variantIndex]:sdfCompactClassify)[safeOrigins?1:0];
        classify.buffers[0]=sdfCompactControl;classify.buffers[1]=sdfCompactQueue;classify.images[0]=sdfRays[rayImage].color;
        classify.groupsX=(rayCapacity+63)/64;
        Require(encoder.DispatchCompute(classify),"Cannot classify compact SDF work queue");
        auto trace=classify;trace.pipeline=(extra?sdfTraceVariantCompactTrace[variantIndex]:sdfCompactTrace)[safeOrigins?1:0];trace.indirect=sdfCompactControl;trace.indirectOffset=4;
        Require(encoder.DispatchCompute(trace),"Cannot consume compact SDF work queue");
        Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::ProbeSdfKernel),"Cannot end compact SDF kernel timing");
        statistics.probeSdfFastAtlasAddressing=extra&&(variant&1u)!=0;
        statistics.probeSdfReuseTraceSamples=extra&&(variant&2u)!=0;
        statistics.probeSdfSinglePassOutputs=true;statistics.probeSdfCompactRays=true;statistics.probeSdfCompactCapacity=rayCapacity;
        statistics.screenProbeTracePasses+=3;
        Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::ProbeSdfTrace),"Cannot end compact probe SDF trace GPU timing");
        return;
    }
    BindTarget(encoder,outputs?traceOutputTarget:sdfRays[rayImage]);
    const bool single=!outputs&&s.trace.singleInvocation&&sdfSingleProgramsOwner&&SingleInvocationTraceTarget(sdfRays[rayImage]);
    const unsigned mode=outputs?1u:(single?2u:0u);
    // Reuse the card/contact AS by binding that exact geometry texture handle.
    // The separate SDF geometry mirror has identical packed IDs but a distinct
    // backend resource key and would otherwise cause a duplicate AS build.
    const unsigned nativeVariant=ProbeSdfNativeVariant(s.trace.sdf);
    const bool triangleProfile=ProbeSdfTriangleProfileRequested(s.trace.sdf)&&mode<2&&surfaceCacheHardwareSupported&&
        surfaceCachePending.valid&&surfaceCacheGeometryScene==s.trace.sdf.scene&&surfaceCacheGeometry.IsValid()&&
        bool(sdfNativeProgramsOwner[nativeVariant][mode]);
    Require(!sdfNativeUploadsPruned||triangleProfile,"Pruned SDF uploads require the matching triangle trace profile");
    if(triangleProfile)Require(encoder.KeepAlive(sdfNativeProgramsOwner[nativeVariant][mode]),"Cannot pin triangle probe trace programs");
    const bool extra=variant&&sdfTraceVariantProgramsOwner[variantIndex][mode];
    if(extra)Require(encoder.KeepAlive(sdfTraceVariantProgramsOwner[variantIndex][mode]),"Cannot pin SDF execution programs");
    const auto program=triangleProfile?sdfNativePrograms[nativeVariant][mode][safeOrigins?1:0]:extra?sdfTraceVariantPrograms[variantIndex][mode][safeOrigins?1:0]:outputs?(safeOrigins?sdfOutputSafeFallbackProgram:sdfOutputFallbackProgram):
        (single?(safeOrigins?sdfSingleSafeFallbackProgram:sdfSingleFallbackProgram):(safeOrigins?sdfSafeFallbackProgram:sdfFallbackProgram));
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::ProbeSdfKernel),"Cannot begin SDF kernel timing");
    Require(encoder.BindPipeline(program)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(traceRays[rayImage].color,0)&&encoder.BindTexture(traceSource.color,1)&&
        encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
        encoder.BindTexture(placementUniform[probeImage].color,4)&&encoder.BindTexture(placementSpawned[probeImage].color,5)&&
        encoder.BindTexture(sdfNativeUploadsPruned?RenderResourceHandle<RHITextureSpec>{}:sdfMeshTexture,6)&&
        encoder.BindTexture(sdfNativeUploadsPruned?RenderResourceHandle<RHITextureSpec>{}:sdfObjectTexture,7)&&
        encoder.BindTexture(sdfNativeUploadsPruned?RenderResourceHandle<RHITextureSpec>{}:sdfGlobalTexture,8)&&
        encoder.BindTexture(triangleProfile?surfaceCacheGeometry:sdfGeometryTexture,9)&&
        encoder.BindTexture(sdfNativeUploadsPruned?RenderResourceHandle<RHITextureSpec>{}:sdfRangeTexture,11)&&
        encoder.BindTexture(safeOrigins?surfaceCacheProbeOrigins.color:RenderResourceHandle<RHITextureSpec>{},10)&&
        encoder.DrawIndexed(single?RHICommand::DrawIndexed{1,0,0,rayCapacity,0}:RHICommand::DrawIndexed{3}),"Cannot record SDF fallback");
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::ProbeSdfKernel),"Cannot end SDF kernel timing");
    statistics.probeSdfSingleInvocation=single;
    statistics.probeSdfFastAtlasAddressing=extra&&(variant&1u)!=0;
    statistics.probeSdfReuseTraceSamples=extra&&(variant&2u)!=0;
    const bool directSoftware=triangleProfile&&s.trace.sdf.directSoftwareRefinement;
    statistics.probeSdfHardwareRayTracing=triangleProfile&&!directSoftware;
    statistics.probeSdfNativeUploadsPruned=sdfNativeUploadsPruned;
    statistics.probeSdfNativeCandidateReused=triangleProfile&&!directSoftware&&s.trace.sdf.reuseNativeCandidate;
    statistics.probeSdfDirectSoftwareRefinement=directSoftware;
    if(outputs){
        Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::ProbeSdfPack),"Cannot begin SDF pack timing");
        PackProbeTraceOutputs(encoder,sdfRays[rayImage]);
        Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::ProbeSdfPack),"Cannot end SDF pack timing");
    }
    statistics.probeSdfSinglePassOutputs=outputs;
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::ProbeSdfTrace),"Cannot end probe SDF trace GPU timing");
    ++statistics.screenProbeTracePasses;
}
void RecordProbeSdfSlice(RHIFrameEncoder& encoder,const ScreenProbePlacementSettings& s,const PostProcessConstants& post) {
    if(!s.trace.sdf.enabled||!s.trace.sdf.sliceView)return;
    const auto c=ProbeSdfConstants(s.trace.sdf);Upload(encoder,sdfBuffer,&c,sizeof(c),ProbeSdfTracingBinding);
    BindTarget(encoder,placementCoverage);
    Require(encoder.BindPipeline(sdfSliceProgram)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(scene.color,0)&&
        encoder.BindTexture(sdfMeshTexture,6)&&encoder.BindTexture(sdfObjectTexture,7)&&encoder.BindTexture(sdfGlobalTexture,8)&&
        encoder.BindTexture(sdfRangeTexture,11)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot show SDF slice");
    Fullscreen(encoder,scene,copy,placementCoverage.color,{},{},post);++statistics.screenProbeTracePasses;
}
void RecordProbeSdfHitMarkers(RHIFrameEncoder& encoder,const ScreenProbePlacementSettings& s,unsigned rayImage) {
    if(!s.trace.sdf.enabled||!s.trace.sdf.showHitPoints)return;
    const auto c=ProbeSdfConstants(s.trace.sdf);Upload(encoder,sdfBuffer,&c,sizeof(c),ProbeSdfTracingBinding);
    BindTarget(encoder,scene);
    Require(encoder.BindPipeline(sdfHitMarkerProgram)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(sdfRays[rayImage].color,7)&&
        encoder.BindTexture(normalCapture.color,1)&&encoder.BindTexture(sdfGeometryTexture,9)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3,0,0,s.trace.rayResolution*s.trace.rayResolution,0}),"Cannot show SDF hit points");
    ++statistics.screenProbeTracePasses;
}
void RecordProbeSdfVolume(RHIFrameEncoder& encoder,const ScreenProbePlacementSettings& s,const PostProcessConstants& post,
    const PipelineCamera& camera) {
    if(!s.trace.sdf.enabled||!s.trace.sdf.visualMode)return;
    auto c=ProbeSdfConstants(s.trace.sdf);
    c.boundLines.fill(glm::vec4(1e20f));
    if(c.clipmap.x>0&&c.clipmap.w!=0) {
        const auto viewProjection=camera.projection*camera.view;
        for(int level=0;level<c.clipmap.x;++level) {
            if(c.clipScroll[level].w==0)continue;
            const auto spacing=glm::vec3(c.clipSpacing[level]);
            const auto low=glm::vec3(c.clipOrigins[level])+spacing;
            const auto high=glm::vec3(c.clipOrigins[level])+spacing*(glm::vec3(c.clipCounts[level])-glm::vec3(2));
            for(int axis=0;axis<3;++axis)for(int edge=0;edge<4;++edge) {
                const int a=(axis+1)%3,b=(axis+2)%3;
                auto first=low,last=low;last[axis]=high[axis];
                first[a]=last[a]=(edge&1)?high[a]:low[a];first[b]=last[b]=(edge&2)?high[b]:low[b];
                auto from=viewProjection*glm::vec4(first,1),to=viewProjection*glm::vec4(last,1);
                constexpr float nearW=.001f;
                if(from.w<=nearW&&to.w<=nearW)continue;
                if(from.w<=nearW)from=glm::mix(from,to,(nearW-from.w)/(to.w-from.w));
                if(to.w<=nearW)to=glm::mix(to,from,(nearW-to.w)/(from.w-to.w));
                c.boundLines[std::size_t(level)*12+axis*4+edge]=glm::vec4(glm::vec2(from)/from.w,glm::vec2(to)/to.w);
            }
        }
    }
    Upload(encoder,sdfBuffer,&c,sizeof(c),ProbeSdfTracingBinding);
    BindTarget(encoder,placementCoverage);
    Require(encoder.BindPipeline(sdfVolumePrograms[s.trace.sdf.visualMode-1])&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(scene.color,0)&&
        encoder.BindTexture(sdfMeshTexture,6)&&encoder.BindTexture(sdfObjectTexture,7)&&encoder.BindTexture(sdfGlobalTexture,8)&&
        encoder.BindTexture(sdfRangeTexture,11)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot visualize SDF volumes");
    Fullscreen(encoder,scene,copy,placementCoverage.color,{},{},post);++statistics.screenProbeTracePasses;
}
