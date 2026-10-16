// Included inside MaterialRenderPipeline::Impl. Local assets remain resident;
// transformed object metadata and dirty Global atlas rectangles are uploaded.
void PrepareProbeSdfTrace(const ScreenProbePlacementSettings& placement) {
    const auto& s=placement.trace.sdf;if(!placement.enabled||!placement.trace.enabled||!s.enabled)return;
    Require(bool(s.scene),"SDF tracing needs a prepared distance-field scene");
    if(!sdfProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);
        sdfFallbackProgram=b.FullscreenPipeline(FullscreenVertexShader(),ProbeSdfDetail::FallbackShader());
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
    if(s.visualMode&&!sdfVolumeOwners[s.visualMode-1]) {
        // Compile only the requested view. A mesh-only shader need not reserve
        // registers for the clipmap polynomial solver, and vice versa.
        PipelineDetail::ResourceBuilder b(device);
        sdfVolumePrograms[s.visualMode-1]=b.FullscreenPipeline(FullscreenVertexShader(),ProbeSdfVisualizationDetail::VolumeShader(s.visualMode));
        sdfVolumeOwners[s.visualMode-1]=b.Finish();
    }
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
    if(s.visualMode==0&&(!sdfViewOwner||sdfRays[0].width!=traceRays[0].width||sdfRays[0].height!=traceRays[0].height)) {
        PipelineDetail::ResourceBuilder b(device);std::array<Target,2> rays;
        for(auto& ray:rays){ray.width=traceRays[0].width;ray.height=traceRays[0].height;
            ray.color=b.Texture(ray.width,ray.height,RHITextureFormat::RGBA32F);ray.framebuffer=b.Target(ray.color,{});}
        auto owner=b.Finish();sdfRays=rays;sdfViewOwner=std::move(owner);
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
    auto c=ProbeSdfConstants(s.trace.sdf);c.atlas.w=surfaceCachePending.valid?3:1;
    Upload(encoder,sdfBuffer,&c,sizeof(c),ProbeSdfTracingBinding);
    if(surfaceCachePending.valid) {
        Require(encoder.BindUniformBuffer(surfaceCacheBuffer,SurfaceCacheGiBinding),"Cannot bind Surface Cache lookup constants");
        BindSurfaceCache(encoder);
    } else {
        SurfaceCacheGiConstants disabled;Upload(encoder,surfaceCacheBuffer,&disabled,sizeof(disabled),SurfaceCacheGiBinding);
        Require(encoder.BindTexture({},12)&&encoder.BindTexture({},13)&&encoder.BindTexture({},14),"Cannot clear Surface Cache lookup textures");
    }
    BindTarget(encoder,sdfRays[rayImage]);
    Require(encoder.BindPipeline(sdfFallbackProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(traceRays[rayImage].color,0)&&encoder.BindTexture(traceSource.color,1)&&
        encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
        encoder.BindTexture(placementUniform[probeImage].color,4)&&encoder.BindTexture(placementSpawned[probeImage].color,5)&&
        encoder.BindTexture(sdfMeshTexture,6)&&encoder.BindTexture(sdfObjectTexture,7)&&encoder.BindTexture(sdfGlobalTexture,8)&&
        encoder.BindTexture(sdfGeometryTexture,9)&&encoder.BindTexture(sdfRangeTexture,11)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot record SDF fallback");
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
