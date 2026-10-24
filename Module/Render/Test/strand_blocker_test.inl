// Included inside the production RHI strand GPU fixture's main scope.
{
    const auto blockerSavedView=view;
    view=PipelineDetail::StrandPassView{};
    view.cameraPosition={0,0,2};view.cameraForward={0,0,-1};
    view.sunDirection={0,0,-1};view.sunIntensity=1;view.sunColor={1,1,1};
    view.lighting.lightColors.fill(glm::vec4(0));
    view.opaqueContentRevision=1001;
    StrandMesh blockerMesh(device);
    const std::array<StrandVertex,4> blockerVertices{{
        {{-.2f,-.4f,-.12f,.002f},{0,1,0,0}},{{-.2f,.4f,.12f,.002f},{0,1,0,1}},
        {{ .2f,-.4f, .12f,.002f},{0,1,0,0}},{{ .2f,.4f,-.12f,.002f},{0,1,0,1}}
    }};
    const std::array<std::uint32_t,4> blockerSegments{0,1,2,3};
    Require(blockerMesh.Update(blockerVertices,blockerSegments),blockerMesh.LastError());
    StrandRenderItem blockerItem;blockerItem.geometry=blockerMesh.Snapshot();
    blockerItem.material.spatialVolume=true;blockerItem.material.volumeOpaqueBlockers=true;
    blockerItem.material.volumeDepthThreshold=.05f;blockerItem.material.volumeVoxelSize=.01f;
    blockerItem.material.volumeResolution=32;blockerItem.material.volumeSkyTransmission=false;
    blockerItem.material.receiveOpaqueShadows=false;blockerItem.material.physicalScattering=true;
    blockerItem.material.rootColor=blockerItem.material.tipColor={.6f,.3f,.1f};
    const auto blockerRead=[&](RenderResourceHandle<RHITextureSpec> texture) {
        Require(texture.IsValid(),"Missing opaque fiber blocker diagnostic texture");
        return OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(texture);});
    };
    draw({blockerItem});
    const auto blockerFirstStats=pass.VolumeStatistics();
    const auto blockerTextures=pass.VolumeDebugTextures();
    const auto blockerDensity=blockerRead(blockerTextures.density),blockerN=blockerRead(blockerTextures.paths);
    const auto blockerShell=blockerRead(blockerTextures.opaqueBlockers),blockerV=blockerRead(blockerTextures.directVisibility);
    const auto blockerExtraV=blockerRead(blockerTextures.extraVisibility);
    Require(blockerFirstStats.passes==7&&blockerFirstStats.blockerPasses==3&&blockerFirstStats.blockerUpdated==1,
        "Initial opaque shell/density/N/visibility pass accounting");
    const float blockerH=blockerFirstStats.effectiveVoxelSize;
    const auto blockerLow=blockerItem.geometry->BoundsMin()-glm::vec3(blockerH);
    const auto blockerExtent=glm::max(blockerItem.geometry->BoundsMax()-blockerItem.geometry->BoundsMin(),glm::vec3(1e-6f));
    const auto blockerGrid=glm::min(glm::uvec3(glm::ceil(blockerExtent/blockerH))+glm::uvec3(2),glm::uvec3(32));
    const auto blockerColumns=std::uint32_t(std::ceil(std::sqrt(float(blockerGrid.z))));
    const auto blockerPixel=[&](std::uint32_t x,std::uint32_t y,std::uint32_t z) {
        return std::size_t((z/blockerColumns)*blockerGrid.y+y)*blockerTextures.width+(z%blockerColumns)*blockerGrid.x+x;
    };
    float blockerMinimumVisibility=1;std::uint32_t blockerMarked=0,blockerClear=0;
    for(std::uint32_t z=0;z<blockerGrid.z;++z)for(std::uint32_t y=0;y<blockerGrid.y;++y)for(std::uint32_t x=0;x<blockerGrid.x;++x) {
        const auto pixel=4*blockerPixel(x,y,z);
        const glm::vec3 p=blockerLow+(glm::vec3(x,y,z)+.5f)*blockerH;
        // Independent world-space plane oracle. Identity VP maps z=0 to .5;
        // source reverse-depth gate maps to z>0 here, followed by |z|<.05.
        const bool expected=p.z>0&&std::abs(p.z)<.05f;
        Require(std::abs(blockerShell[pixel]-(expected?1.f:0.f))<.001f,
            "Opaque shell differs from source behind-depth / forward-distance predicate");
        expected?++blockerMarked:++blockerClear;
        blockerMinimumVisibility=std::min(blockerMinimumVisibility,blockerV[pixel]);
        for(int channel=0;channel<4;++channel)Require(std::isfinite(blockerV[pixel+channel])&&
            blockerV[pixel+channel]>=-.001f&&blockerV[pixel+channel]<=1.001f,"Opaque visibility outside [0,1]");
        for(int channel=1;channel<4;++channel)Require(std::abs(blockerV[pixel+channel]-1)<.001f,
            "Inactive point-light opaque visibility must be one");
        for(int channel=0;channel<4;++channel)Require(std::abs(blockerExtraV[pixel+channel]-1)<.001f,
            "Inactive extra-light / environment opaque visibility must be one");
    }
    Require(blockerMarked>0&&blockerClear>0&&blockerMinimumVisibility<.75f,
        "Fixture failed to exercise both shell states and a directional occlusion path");
    draw({blockerItem});
    Require(pass.VolumeStatistics().passes==0&&pass.VolumeStatistics().blockerPasses==0,
        "Paused opaque blocker frame rebuilt cached density, N or shell");
    ++view.opaqueContentRevision;draw({blockerItem});
    Require(pass.VolumeStatistics().passes==3&&pass.VolumeStatistics().updated==0&&pass.VolumeStatistics().blockerPasses==3,
        "Opaque content revision must refresh shell/visibility without redoing density or N");
    Require(MaximumDifference(blockerDensity,blockerRead(pass.VolumeDebugTextures().density))==0&&
        MaximumDifference(blockerN,blockerRead(pass.VolumeDebugTextures().paths))==0,
        "Opaque shell update contaminated density / tangent or fiber encounters");
    view.cameraForward={0,0,1};draw({blockerItem});
    Require(pass.VolumeStatistics().passes==3&&pass.VolumeStatistics().updated==0,
        "Camera-forward change must invalidate only opaque shell/visibility in axis-independent mode");
    view.cameraForward={0,0,-1};view.viewProjection=glm::translate(glm::mat4(1),glm::vec3(.03f,0,0));draw({blockerItem});
    Require(pass.VolumeStatistics().passes==3&&pass.VolumeStatistics().updated==0,
        "Projection change must invalidate opaque shell without rebuilding fiber density/N");
    Require(MaximumDifference(blockerDensity,blockerRead(pass.VolumeDebugTextures().density))==0&&
        MaximumDifference(blockerN,blockerRead(pass.VolumeDebugTextures().paths))==0,
        "Camera-only opaque shell update changed physical fiber transport");
    view.viewProjection=glm::mat4(1);draw({blockerItem});
    auto blockerWide=blockerItem;blockerWide.material.volumeDepthThreshold=.1f;draw({blockerWide});
    const auto blockerWideShell=blockerRead(pass.VolumeDebugTextures().opaqueBlockers);
    std::uint32_t blockerWideMarked=0;for(std::size_t i=0;i<blockerWideShell.size();i+=4)blockerWideMarked+=blockerWideShell[i]>.5f;
    Require(blockerWideMarked>blockerMarked&&pass.VolumeStatistics().updated==0&&pass.VolumeStatistics().blockerPasses==3,
        "World-depth shell threshold must change G separately from N/density");
    draw({blockerItem});
    view.sunDirection={-1,0,0};draw({blockerItem});
    const auto blockerSideV=blockerRead(pass.VolumeDebugTextures().directVisibility);
    Require(pass.VolumeStatistics().passes==5&&pass.VolumeStatistics().blockerPasses==2&&pass.VolumeStatistics().updated==1,
        "Light change should retrace N and blocker visibility while retaining the shell");
    Require(MaximumDifference(blockerV,blockerSideV)>.01f&&
        MaximumDifference(blockerShell,blockerRead(pass.VolumeDebugTextures().opaqueBlockers))==0&&
        MaximumDifference(blockerDensity,blockerRead(pass.VolumeDebugTextures().density))==0,
        "Directional opaque visibility did not change independently of shell/density");
    auto blockerZero=blockerItem;blockerZero.material.volumeDepthThreshold=0;
    const auto blockerZeroImage=draw({blockerZero});
    const auto blockerZeroShell=blockerRead(pass.VolumeDebugTextures().opaqueBlockers);
    for(std::size_t i=0;i<blockerZeroShell.size();i+=4)Require(blockerZeroShell[i]==0,"Zero shell threshold must inject no G");
    auto blockerDisabled=blockerZero;blockerDisabled.material.volumeOpaqueBlockers=false;
    const auto blockerDisabledImage=draw({blockerDisabled});
    Require(MaximumDifference(blockerZeroImage,blockerDisabledImage)<.002f,
        "Disabled opaque blockers disagree with an empty blocker shell");
    Require(!pass.VolumeDebugTextures().opaqueBlockers.IsValid(),"Disabled opaque blocker debug view exposed stale data");
    auto blockerFront=blockerItem;blockerFront.model=glm::translate(glm::mat4(1),glm::vec3(0,0,-.3f));draw({blockerFront});
    const auto blockerFrontShell=blockerRead(pass.VolumeDebugTextures().opaqueBlockers);
    for(std::size_t i=0;i<blockerFrontShell.size();i+=4)Require(blockerFrontShell[i]==0,
        "Volume in front of the opaque surface must not become a blocker shell");
    pass.InvalidateVolume();draw({blockerItem});
    Require(pass.VolumeStatistics().passes==7&&pass.VolumeStatistics().blockerPasses==3,
        "Discard invalidation failed to rebuild opaque shell alongside physical transport");
    Require(MaximumDifference(blockerDensity,blockerRead(pass.VolumeDebugTextures().density))==0,
        "Rebuilt opaque blocker fixture changed immutable density");
    std::cout<<"Opaque DSVolume shell: marked="<<blockerMarked<<", wider="<<blockerWideMarked
        <<", minimum sun visibility="<<blockerMinimumVisibility<<"; density/N isolation and cache PASS\n";
    view=blockerSavedView;
}
