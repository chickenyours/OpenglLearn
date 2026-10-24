// Included inside MaterialRenderPipeline::Impl. The owning translation unit
// includes ue_lumen_direct_lighting_schedule_state.h and its shaders header.
void PrepareUeLumenDirectLightingSchedule(bool nativeVisibility,bool diagnostics) {
    auto& s=ueLumenDirectLightingSchedule;
    if(!s.constantsOwner){PipelineDetail::ResourceBuilder b(device);
        const auto buffer=b.Uniform(sizeof(PipelineDetail::UeLumenDirectLightingRectConstants));
        auto owner=b.Finish();s.constantsBuffer=buffer;s.constantsOwner=std::move(owner);}
    const auto prepare=[&](bool collect){const unsigned key=(nativeVisibility?1u:0u)|(collect?2u:0u);
        if(!s.programOwners[key]){PipelineDetail::ResourceBuilder b(device);
            const auto program=b.FullscreenPipeline(SurfaceCacheDetail::UeLumenDirectLightingTileVertexShader(),
                SurfaceCacheDetail::UeLumenDirectLightingTileFragmentShader(nativeVisibility,collect));
            auto owner=b.Finish();s.programs[key]=program;s.programOwners[key]=std::move(owner);}};
    prepare(false);if(diagnostics)prepare(true);
    const unsigned fullKey=nativeVisibility?1u:0u;
    if(!s.fullProgramOwners[fullKey]){PipelineDetail::ResourceBuilder b(device);
        const auto program=b.FullscreenPipeline(FullscreenVertexShader(),
            SurfaceCacheDetail::LightingShader(nativeVisibility,false,true));
        auto owner=b.Finish();s.fullPrograms[fullKey]=program;s.fullProgramOwners[fullKey]=std::move(owner);}

}

const PipelineDetail::UeLumenDirectLightingPlan& PlanUeLumenDirectLightingSchedule(
    const SurfaceCacheScene& cache,const PipelineCamera& camera,std::uint64_t globalLightingKey,bool forceFullRefresh) {
    auto& s=ueLumenDirectLightingSchedule;s.pendingValid=false;
    auto& plan=s.plan;plan={};
    const auto pageResolution=cache.Settings().pageResolution;
    plan.cardCount=cache.CardCount();plan.pageTexels=pageResolution*pageResolution;
    const auto& previous=s.completed;
    const bool layoutChanged=!previous.valid||previous.cache!=&cache||previous.width!=cache.Width()||
        previous.planeTexels!=cache.PlaneTexels()||previous.cardCount!=plan.cardCount||previous.pageResolution!=pageResolution;
    plan.fullRefresh=forceFullRefresh||layoutChanged||previous.globalLightingKey!=globalLightingKey;
    s.pending=previous;
    auto& pending=s.pending;pending.valid=true;pending.cache=&cache;pending.globalLightingKey=globalLightingKey;
    pending.width=cache.Width();pending.planeTexels=cache.PlaneTexels();pending.cardCount=plan.cardCount;pending.pageResolution=pageResolution;
    pending.frameIndex=previous.valid?previous.frameIndex+1u:1u;
    if(layoutChanged)pending.lastUpdate.assign(plan.cardCount,0);

    // UE quality override, not default High: LumenSceneLighting.cpp:554
    // clamps LightingUpdateSpeed to [.5,16], line572 RoundToInt(High64/speed),
    // then line101 clamps the factor to [1,1024]. Fixed speed16 gives factor4.
    // Lines110-118 retain per-axis uint conversion, CardTileSize8 rounding and
    // the whole-page lower bound. Representation adapter: UE PhysicalPageSize128
    // maps to this cache's actual logical page resolution; the atlas is flat.
    constexpr float lightingUpdateSpeed=16.f;
    const auto effectiveFactor=std::clamp(int(std::round(64.f/lightingUpdateSpeed)),1,1024);
    const auto updateAxis=[&](std::uint32_t extent){
        const auto scaled=std::uint32_t(float(extent)*(1.f/std::sqrt(float(effectiveFactor)))+.5f);
        return std::max(pageResolution,((scaled+7u)/8u)*8u);};
    const auto width=cache.Width(),height=cache.PlaneTexels()/width;
    plan.budgetTexels=std::uint32_t(std::min<std::uint64_t>(cache.TexelCount(),
        std::uint64_t(updateAxis(width))*updateAxis(height)));
    const auto makeFull=[&]{
        plan.fullRefresh=true;plan.rects.clear();plan.selectedCards.resize(plan.cardCount);
        for(std::uint32_t card=0;card<plan.cardCount;++card)plan.selectedCards[card]=card;
        plan.scheduledTexels=cache.TexelCount();plan.writtenTexels=cache.PlaneTexels();
    };
    if(plan.fullRefresh){makeFull();}
    else {
        const auto& metadata=cache.Metadata();
        Require(metadata.size()>=std::size_t(plan.cardCount)*SurfaceCacheCardTexels,"Direct lighting schedule lacks card metadata");
        const auto inverseView=glm::inverse(camera.view);
        const auto viewOrigin=inverseView[3];
        Require(std::isfinite(viewOrigin.x)&&std::isfinite(viewOrigin.y)&&std::isfinite(viewOrigin.z)&&
            std::isfinite(viewOrigin.w)&&viewOrigin.w!=0,"Invalid direct lighting view origin");
        const auto cameraOrigin=glm::vec3(viewOrigin)/viewOrigin.w;
        // UE uses five outward camera planes (no far plane), with the original
        // 500cm near-frustum allowance, converted to this renderer's meters.
        const auto vp=camera.projection*camera.view;
        const auto row=[&](unsigned i){return glm::vec4(vp[0][i],vp[1][i],vp[2][i],vp[3][i]);};
        const auto r3=row(3);
        std::array<glm::vec4,5> planes{{-(r3+row(2)),-(r3+row(0)),-(r3-row(0)),-(r3+row(1)),-(r3-row(1))}};
        for(auto& plane:planes){const float length=glm::length(glm::vec3(plane));
            Require(std::isfinite(length)&&length>0,"Invalid direct lighting camera frustum");plane/=length;}
        std::vector<unsigned> buckets(plan.cardCount);
        for(std::uint32_t card=0;card<plan.cardCount;++card){const auto base=std::size_t(card)*SurfaceCacheCardTexels;
            // The project has no separate card-page OBB: all six fixed pages
            // expose their captured instance's current world bounds at9/10.
            // Use those real bounds in UE's box-distance/frustum scorer.
            const auto low=glm::vec3(metadata[base+9]),high=glm::vec3(metadata[base+10]);
            const auto center=(low+high)*.5f,extent=(high-low)*.5f;
            for(unsigned axis=0;axis<3;++axis)Require(std::isfinite(low[axis])&&std::isfinite(high[axis])&&
                std::isfinite(center[axis])&&std::isfinite(extent[axis]),"Nonfinite direct lighting page bounds");
            const auto delta=glm::max(glm::max(low-cameraOrigin,cameraOrigin-high),glm::vec3(0));
            const float distance=glm::length(delta);
            Require(std::isfinite(distance)&&glm::all(glm::greaterThanEqual(extent,glm::vec3(0))),"Invalid direct lighting page bounds");
            bool nearFrustum=true;
            for(const auto& plane:planes){const float radius=glm::dot(extent,glm::abs(glm::vec3(plane)));
                nearFrustum=nearFrustum&&glm::dot(plane,glm::vec4(center,1))<=radius+5.f;}
            // UE FirstClipmapWorldExtentRcp is GlobalSDF extent2500cm, not a
            // RadianceCache probe budget. No nonexistent feedback is invented.
            float speed=1.f/(1.f+std::max(distance/25.f,0.f));if(nearFrustum)speed*=2.f;
            const auto last=pending.lastUpdate[card];
            const auto age=last?pending.frameIndex-last:2048u;
            const float priority=15.f-std::clamp(std::log2(std::max(4.f*float(age)*speed,1.f)),0.f,15.f);
            const unsigned bucket=unsigned(priority); // original final float-to-uint truncation
            buckets[card]=bucket;plan.histogram[bucket]+=plan.pageTexels;
        }
        // UE LumenSceneLighting.usf:210-232 /254-302: admit entire pages from
        // ascending priority buckets, then a bounded append in the last bucket.
        std::uint32_t prefix=0,lastBucketBudget=plan.budgetTexels;
        for(unsigned bucket=0;bucket<16;++bucket){plan.maximumBucket=bucket;
            if(prefix+plan.histogram[bucket]>=plan.budgetTexels){lastBucketBudget=plan.budgetTexels-prefix;break;}
            prefix+=plan.histogram[bucket];}
        std::uint32_t lastBucketCost=0;
        for(std::uint32_t card=0;card<plan.cardCount;++card){const auto bucket=buckets[card];
            if(bucket>plan.maximumBucket)continue;
            if(bucket==plan.maximumBucket){const auto before=lastBucketCost;lastBucketCost+=plan.pageTexels;
                if(before+plan.pageTexels>lastBucketBudget)continue;}
            if(plan.scheduledTexels+plan.pageTexels>plan.budgetTexels)continue;
            plan.selectedCards.push_back(card);plan.scheduledTexels+=plan.pageTexels;
            const auto base=std::size_t(card)*SurfaceCacheCardTexels;
            const float firstValue=metadata[base+5].w,resolutionValue=metadata[base+9].w;
            Require(std::isfinite(firstValue)&&firstValue>=0&&firstValue==std::floor(firstValue)&&resolutionValue==float(pageResolution),"Invalid direct lighting physical page mapping");
            std::uint32_t first=std::uint32_t(firstValue),remaining=plan.pageTexels;
            Require(std::uint64_t(first)+remaining<=cache.TexelCount(),"Direct lighting page exceeds live atlas");
            while(remaining){const auto x=first%width,y=first/width;
                const auto rows=x==0?remaining/width:0u;
                const auto rectWidth=rows?width:std::min(remaining,width-x),rectHeight=rows?rows:1u;
                plan.rects.emplace_back(int(x),int(y),int(rectWidth),int(rectHeight));
                const auto area=rectWidth*rectHeight;plan.writtenTexels+=area;first+=area;remaining-=area;
            }
        }
        // No extra draw batches or truncated work if the fixed RHI UBO cannot
        // represent the selected set. Fall back to the original full producer.
        if(plan.rects.size()>PipelineDetail::UeLumenDirectLightingMaxRects)makeFull();
    }
    for(const auto card:plan.selectedCards)pending.lastUpdate[card]=pending.frameIndex;
    s.pendingValid=true;return plan;
}

// Caller binds the selected lighting or diagnostic MRT framebuffer (ClearNone),
// existing lighting/cache/area uniforms and textures12/13/9 before this call.
// Diagnostic callers first clear the query-only target, never the lighting MRT.
void RecordUeLumenDirectLightingSchedule(RHIFrameEncoder& encoder,bool nativeVisibility,bool diagnostics) {
    auto& s=ueLumenDirectLightingSchedule;const auto& plan=s.plan;
    Require(s.pendingValid&&!plan.fullRefresh,"Partial direct lighting record lacks a partial plan");
    if(plan.rects.empty())return;
    const unsigned key=(nativeVisibility?1u:0u)|(diagnostics?2u:0u);
    Require(encoder.KeepAlive(s.constantsOwner)&&encoder.KeepAlive(s.programOwners[key]),"Cannot retain selected direct lighting resources");
    PipelineDetail::UeLumenDirectLightingRectConstants constants;
    constants.atlas={int(s.pending.width),int(s.pending.planeTexels/s.pending.width),int(plan.rects.size()),0};
    for(auto& rect:constants.rects)rect=glm::ivec4(0);
    std::copy(plan.rects.begin(),plan.rects.end(),constants.rects.begin());
    Upload(encoder,s.constantsBuffer,&constants,sizeof(constants),PipelineDetail::UeLumenDirectLightingRectBinding);
    Require(encoder.BindPipeline(s.programs[key])&&encoder.BindMesh(fullscreen)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3,0,0,std::uint32_t(plan.rects.size()),0}),"Cannot shade selected direct lighting pages");
}

void CommitUeLumenDirectLightingSchedule(bool success) {
    auto& s=ueLumenDirectLightingSchedule;
    if(success&&s.pendingValid)s.completed=std::move(s.pending);
    if(!success)s.completed={};
    s.pending={};s.pendingValid=false;
}

void ResetUeLumenDirectLightingSchedule() {
    auto& s=ueLumenDirectLightingSchedule;
    s.completed={};s.pending={};s.plan={};s.pendingValid=false;
}
