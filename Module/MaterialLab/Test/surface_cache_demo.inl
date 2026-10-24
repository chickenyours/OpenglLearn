#include "sealed_room_interaction.h"

// CPU-only regression of the helpers used by F, arrows and H. No window,
// renderer, mesh bake, GPU resource or image readback is needed.
void SurfaceCacheInteractionCpuTest() {
    using namespace SealedRoomMotion;
    auto objects=SealedGiRoomObjects();
    for(auto& object:objects)object.position+=glm::vec3(100);
    const auto sphere=[&](unsigned index,glm::vec3 position,glm::vec3 scale) {
        objects[index].mesh=ShowcaseMesh::Sphere;objects[index].position=position;
        objects[index].scale=scale;objects[index].yaw=objects[index].tilt=0;objects[index].movable=true;
    };
    sphere(FirstProp,{0,1,0},glm::vec3(.5f));sphere(FirstProp+1,{0,1,-2},glm::vec3(.5f));
    Check(Pick(objects,{0,1,3},{0,0,-1},30)==FirstProp,"F picking failed to choose the nearest actual prop");
    Check(Pick(objects,{0,1,3},{0,0,1},30)==NoSelection,"F picking accepted a hit behind the ray origin");
    objects[0].mesh=ShowcaseMesh::Box;objects[0].position={0,1,1.5f};objects[0].scale={.6f,.6f,.1f};
    objects[0].yaw=objects[0].tilt=0;
    Check(Pick(objects,{0,1,3},{0,0,-1},30)==NoSelection,"F picking selected a prop through an opaque wall");
    objects[0].position=glm::vec3(100);
    sphere(FirstProp,{0,0,0},{2,.2f,.5f});sphere(FirstProp+1,{1.9f,.19f,-2},glm::vec3(.1f));
    Check(Pick(objects,{1.9,.19,3},{0,0,-1},30)==FirstProp+1,
        "F picking used an ellipsoid's empty bounding-box corner instead of visible triangles");
    const auto camera=SealedGiRoomCameraPreset(0,1.6f);
    Check(Pick(objects,camera,-.1,.5)==NoSelection,"F picking accepted a cursor outside the viewport");
    const auto movement=GroundDisplacement(1,-1,.25f,false);
    Check(std::abs(glm::length(movement)-.35f)<1e-6f&&movement.y==0&&movement.z<0,
        "Arrow movement must normalize diagonals and remain on the XZ ground plane");
    Check(glm::length(GroundDisplacement(1,-1,.125f,false)*2.f-movement)<1e-6f&&
        glm::length(GroundDisplacement(1,-1,.25f,true)-movement*3.f)<1e-6f,
        "Arrow movement speed changed with timestep or Shift did not multiply it consistently");
    const auto initial=SealedGiRoomObjects();auto oneStep=initial,segmented=initial;
    Advance(oneStep,0,1);Advance(segmented,0,.35);
    const auto paused=segmented;
    // Pausing advances no simulation time; resuming uses the retained phase.
    Advance(segmented,.35,.35);Check(segmented[FirstProp].position==paused[FirstProp].position,
        "Pausing H changed an object's position");
    Advance(segmented,.35,1);
    unsigned moved=0;
    for(unsigned index=0;index<initial.size();++index) {
        Check(glm::length(oneStep[index].position-segmented[index].position)<1e-6f,
            "H pause/resume or a split timestep introduced an animation jump");
        if(IsProp(index)) {
            Check(glm::length(oneStep[index].position-initial[index].position)>1e-5f&&
                oneStep[index].position.y==initial[index].position.y,"H failed to move a prop on the ground plane");++moved;
        } else Check(oneStep[index].position==initial[index].position,"H moved room architecture or its lamp");
    }
    Check(moved==6,"H animation must include all six porcelain props");
    segmented=initial;segmented[FirstProp].position.x+=.7f;const auto manual=segmented[FirstProp].position;
    Advance(segmented,1,1.01);
    Check(glm::length(segmented[FirstProp].position-manual)<.01f,
        "Resuming H after manual movement snapped the prop back to its initial path");
    auto edge=initial[FirstProp];edge.position={100,edge.position.y,-100};KeepInsideRoom(edge);
    Check(edge.position.x<SealedGiRoomMaximum.x&&edge.position.z>SealedGiRoomMinimum.z&&edge.position.y==initial[FirstProp].position.y,
        "Ground movement escaped the room or changed object height");
    const auto verifyPalette=[&](const std::vector<ProbePlacementObject>& poses) {
        FrameSettings palette;palette.screenProbeStudyState.occluderOffset=.61f;
        Sync(palette,poses);
        Check(palette.sealedGiRoomPropMotion&&palette.screenProbeStudyState.occluderOffset==0,
            "Interactive palette lost stable dynamic identity or retained the legacy cube offset");
        for(unsigned index=0;index<poses.size();++index) {
            const auto visible=ProbePlacementTransform(poses[index],{});
            const auto published=SealedGiRoomObjectTransform(index,palette.screenProbeStudyState,palette.sealedGiRoomPropOffsets);
            for(unsigned column=0;column<4;++column)for(unsigned row=0;row<4;++row)
                Check(std::abs(visible[column][row]-published[column][row])<1e-6f,
                    "F picking pose differs from the public raster/SDF/card instance transform");
        }
    };
    verifyPalette(initial);verifyPalette(oneStep);verifyPalette(paused);verifyPalette(segmented);
    auto manySteps=initial;
    for(unsigned step=0;step<120;++step)Advance(manySteps,double(step)/60.,double(step+1)/60.);
    manySteps[FirstProp+2].position+=GroundDisplacement(-1,1,.3f,true);
    KeepInsideRoom(manySteps[FirstProp+2]);verifyPalette(manySteps);
    Advance(manySteps,2,2.016);verifyPalette(manySteps);
    manySteps=initial;verifyPalette(manySteps); // R resets every prop and all six offset slots.
    FrameSettings untouched;untouched.screenProbeStudyState.occluderOffset=.4f;
    Check(!untouched.sealedGiRoomPropMotion,"Default settings unexpectedly enabled six-prop animation");
    for(unsigned index=0;index<initial.size();++index) {
        const auto legacy=ProbePlacementTransform(initial[index],untouched.screenProbeStudyState);
        const auto preserved=SealedGiRoomObjectTransform(index,untouched.screenProbeStudyState,untouched.sealedGiRoomPropOffsets);
        for(unsigned column=0;column<4;++column)Check(legacy[column]==preserved[column],
            "The default palette changed the legacy sealed-room transform");
    }
    std::cout<<"GI95 interaction CPU passed: actual mesh picking, nearest/occlusion, XZ dt/Shift, six-prop H pause/resume, 25 shared transforms/reset/legacy\n";
}

// Independent Surface Cache stage. No dependency on the older hybrid GI,
// world-radiance cache or final-image antialiasing. Adaptive surface probes are
// the normal gather; the per-pixel path remains an explicit reference.
void ApplySurfaceCacheBudget(FrameSettings& frame,unsigned profile) {
    if(profile==21){
        // Fixed UE 5.7.1 GlobalIlluminationQuality@2, not a tunable trial matrix.
        // BaseScalability.ini 281-306; LumenScreenProbeFiltering.cpp 18-76.
        ApplySurfaceCacheBudget(frame,16);
        auto& gi=frame.effects.surfaceCacheGi;auto& p=frame.effects.screenProbePlacement;
        gi.ueLumenHigh=true;gi.nativeAdaptiveFreshSampling=false;
        gi.fuseNativeReconstruction=false;gi.nativeTemporalContactMetadata=false;
        gi.contact.enabled=false;gi.compactReceiverTracing=false;gi.sparseReceiverSampling=false;
        p.baseSpacing=32;p.candidateCount=16;p.trace.rayResolution=8;p.trace.cosineHemisphere=false;
        p.cornerRefinement=false;p.curvatureRefinement=false;
        gi.spatial.passes=3;gi.spatial.halfKernelSize=1;
        gi.spatial.positionWeightScale=1000;gi.spatial.maxHitAngleDegrees=10;
        gi.temporal.maxFrames=10;gi.temporal.distanceThreshold=.01f;
        return;
    }
    if(!profile)return;
    auto& gi=frame.effects.surfaceCacheGi;auto& p=frame.effects.screenProbePlacement;
    gi.halfReceiverRadiance=profile>=9;gi.deferredReceiverMix=profile>=10;
    gi.mrtReceiverCapture=profile>=11;
    gi.nativeScreenProbeTransport=profile>=12;
    gi.nativeAdaptiveFreshSampling=profile>=13;
    gi.reuseStaticReceiverMotionCapture=profile>=13;
    gi.fuseNativeReconstruction=profile>=14;
    gi.nativeTemporalContactMetadata=profile>=14;
    gi.nativeComputePlacement=profile>=15;
    p.trace.sdf.snapshotRowCatchup=profile>=16;
    gi.nativeForceRasterCore=profile>=17;
    gi.nativeSyntheticReceiverFilterSupport=profile>=18;
    gi.nativeReceiverFilterRiskWeights=profile>=19;
    gi.nativeReceiverFilterSupportHalo=profile>=20;
    gi.hardwareContactTracing=true;gi.hardwareCacheVisibility=true;gi.hardwareOriginTracing=true;
    gi.updateAccelerationStructures=true;gi.deferResourceUploads=true;gi.fastGeometryAddressing=true;
    gi.areaShadows.reuseCasterPreparation=true;gi.areaShadows.cullCasters=true;
    gi.areaShadows.reuseAtlasStorage=true;gi.areaShadows.reuseUniformSnapshotContents=true;
    gi.spatial.radianceOnlyFiltering=true;
    p.trace.singleInvocation=false;p.trace.singlePassOutputs=true;p.trace.compactSdfRays=false;
    p.trace.sdf.fastAtlasAddressing=true;p.trace.sdf.reuseTraceSamples=true;p.trace.sdf.nativeUploadPruning=true;
    p.trace.sdf.reuseNativeCandidate=false;p.trace.sdf.directSoftwareRefinement=profile==1;
    // High-frequency contact geometry keeps full-resolution receivers. Broad
    // illumination uses sparse probes and bounded, geometry-rejected history.
    p.baseSpacing=32;p.candidateCount=4;p.trace.rayResolution=8;
    gi.contact.budgetedSampling=true;gi.contact.raysPerPixel=4;
    gi.cacheSettledGather=true;
    gi.omitUnusedRawGather=true;
    gi.cachedAreaSamplesPerAxis=8;
    p.trace.sdf.deferUnusedGlobalField=true;
    gi.contact.halfResolutionScreenRisk=true;
    gi.contact.prioritizeContactCore=true;
    gi.contact.disocclusionRayMultiplier=2;
    gi.contact.movingReceiverRayMultiplier=2;gi.contact.movingReceiverFullCurrent=true;
    gi.spatial.passes=2;
    gi.temporal.motionConsistentSpatialReuse=true;
    gi.temporal.dynamicMaxFrames=4;gi.temporal.edgeMaxFrames=2;
    if(profile>=2){frame.effects.areaLights.samplesPerAxis=4;frame.effects.areaLights.stratifiedSamples=true;
        p.trace.sdf.directSoftwareRefinement=false;p.trace.sdf.hardwareRayTracing=true;p.trace.sdf.nativeInteriorTracing=true;}
    if(profile==2||profile>=4){
        // Keep the curve/junction obligation and original contact strata.
        p.candidateCount=8;gi.contact.raysPerPixel=8;gi.contact.prioritizeContactCore=false;
        gi.contact.halfResolutionScreenRisk=false;
        gi.contact.disocclusionRayMultiplier=4;
        gi.spatial.passes=3;gi.temporal.motionConsistentSpatialReuse=false;
    }
    if(profile==3){p.baseSpacing=48;p.trace.rayResolution=4;gi.contact.raysPerPixel=1;}
    if(profile>=4){gi.splitReceiverGather=true;gi.cachedDirectDiffuse=true;
        gi.sparseReceiverSampling=profile==4||profile>=6;
        gi.coherentReceiverTracing=profile>=6;
        if(profile>=7){gi.receiverLatticeMax=2;gi.geometryReceiverRisk=true;}
        gi.compactReceiverTracing=profile==7||profile>=9;
        gi.transportTexelStride=1;gi.receiverRiskDistance=.35f;
        gi.temporal.edgeMaxFrames=4;
        gi.sparseNativeVertexUpdates=true;p.trace.sdf.incrementalGeometry=true;
        gi.spatial.enabled=false;gi.contact.enabled=false;gi.cacheSettledGather=false;
        gi.areaShadows.enabled=false;frame.effects.areaLights.samplesPerAxis=8;frame.effects.areaLights.stratifiedSamples=false;
        if(profile>=6){gi.cachedAreaSamplesPerAxis=0;frame.effects.areaLights.stratifiedSamples=true;}}
    if(profile>=12){gi.contact.enabled=true;gi.spatial.enabled=true;gi.spatial.passes=3;}
}
FrameSettings MakeSurfaceCacheGiFrame(const Options& options) {
    auto frame=MakeProbeSdfTraceFrame(options);
#ifdef MATERIAL_LAB_HAS_MODEL
    if(options.sdfBanquet)frame=options.sdfClipmaps?MakeSdfClipmapFrame(options):MakeSdfBanquetFrame(options);
#endif
    frame.surfaceCacheStudy=true;
    auto& e=frame.effects;auto& placement=e.screenProbePlacement;
    auto& gi=e.surfaceCacheGi;gi.useScreenProbes=!options.surfaceCacheReference;
    gi.hardwareContactTracing=options.surfaceCacheHardwareContact;
    gi.hardwareCacheVisibility=options.surfaceCacheHardwareVisibility;
    gi.hardwareOriginTracing=options.surfaceCacheNativeProbes;
    gi.receiverSoftwareOrigins=options.surfaceCacheSoftwareReceiverOrigins;
    gi.reuseOriginSoftwareCertification=options.surfaceCacheReuseOriginCertification;
    gi.updateAccelerationStructures=options.surfaceCacheUpdateAcceleration;
    placement.trace.sdf.nativeUploadPruning=options.surfaceCachePruneNativeUploads;
    placement.trace.sdf.reuseNativeCandidate=options.surfaceCacheReuseNativeCandidate;
    placement.trace.sdf.directSoftwareRefinement=options.surfaceCacheDirectSoftwareRefinement;
    gi.contact.splitReceiverOrigins=options.surfaceCacheSplitReceiverOrigins;
    gi.contact.reuseLightingLookup=options.surfaceCacheReceiverLookupReuse;
    gi.contact.fastLightingAddressing=options.surfaceCacheReceiverFastLightingAddressing;
    gi.reuseClosestTraversal=options.surfaceCacheReuseClosestTraversal;
    gi.fastGeometryAddressing=options.surfaceCacheFastGeometryAddressing;
    gi.spatial.radianceOnlyFiltering=options.surfaceCacheRadianceFilter;
    gi.deferResourceUploads=options.surfaceCacheDeferredUploads;
    gi.reuseAccelerationStorage=options.surfaceCacheReuseAcceleration;
    gi.areaShadows.reuseCasterPreparation=options.surfaceCacheReuseShadowCasters;
    gi.areaShadows.cullCasters=options.surfaceCacheCullShadowCasters;
    gi.areaShadows.reuseAtlasStorage=options.surfaceCacheReuseShadowAtlas;
    gi.areaShadows.reuseUniformSnapshotContents=options.surfaceCacheReuseUniformSnapshots;
    gi.safeProbeOrigins=!options.surfaceCacheNoSafeOrigins;
    gi.rasterSourcePositions=!options.surfaceCacheDepthPositions;
    gi.probeOriginDiagnostics=options.surfaceCacheOriginOverlay;
    placement.curvatureRefinement=!options.surfaceCacheNoCurvature;
    gi.temporal.enabled=!options.surfaceCacheNoTemporal;gi.temporal.maxFrames=options.surfaceCacheHistoryFrames;
    gi.temporal.directionPhases=options.surfaceCacheHistoryDirections;
    gi.temporal.motionConsistentSpatialReuse=options.surfaceCacheDynamicConsistent;
    gi.temporal.localCameraHistory=options.surfaceCacheLocalCameraHistory;
    gi.temporal.motionStability=!options.surfaceCacheNoMotionStability&&
        (options.sealedGiRoom||options.surfaceCacheMotionStability);
    gi.temporal.motionSpatialFilter=options.surfaceCacheMotionSpatialFilter;
    gi.temporal.motionDisocclusionRecovery=options.surfaceCacheMotionDisocclusionRecovery;
    gi.temporal.objectMotionReprojection=!options.surfaceCacheNoObjectMotionReprojection&&
        (options.sealedGiRoom||options.surfaceCacheObjectMotionReprojection);
    gi.contact.disocclusionSampling=!options.surfaceCacheNoFreshSampling&&(options.sealedGiRoom||options.surfaceCacheFreshSampling);
    gi.contact.disocclusionRayMultiplier=options.surfaceCacheFreshSamplingMultiplier;
    gi.contact.movingReceiverRayMultiplier=options.surfaceCacheMovingReceiverRayMultiplier;
    gi.contact.movingReceiverFullCurrent=options.surfaceCacheMovingReceiverFullCurrent;
    gi.temporal.reactiveShadows=!options.surfaceCacheNoReactiveShadows&&(options.sealedGiRoom||options.surfaceCacheReactiveShadows);
    gi.temporal.disocclusionFilter=options.surfaceCacheDisocclusionFilter;
    gi.reuseAnyHitBounds=!options.surfaceCacheLegacyAnyHitBounds;
    gi.temporal.youngFilter=!options.surfaceCacheNoYoungRecovery;
    gi.temporal.retainHistoryOnObjectMotion=!options.surfaceCacheResetOnObjectMotion;
    gi.spatial.enabled=!options.surfaceCacheNoSpatial;gi.spatial.passes=options.surfaceCacheSpatialPasses;
    gi.spatial.halfKernelSize=options.surfaceCacheSpatialRadius;
    gi.spatial.localAdaptiveNeighbors=!options.surfaceCacheNoLocalNeighbors;
    placement.enabled=gi.useScreenProbes;placement.adaptive=true;placement.baseSpacing=options.surfaceCacheProbeSpacing;
    placement.frameIndex=0;placement.freeze=false;placement.debugView=0;placement.markerRadiusPixels=3;
    placement.trace.enabled=true;placement.trace.showPanel=false;placement.trace.cosineHemisphere=gi.useScreenProbes;
    placement.trace.rayResolution=options.surfaceCacheProbeRays;placement.trace.maxDistance=30;
    placement.trace.singleInvocation=options.surfaceCacheSingleInvocation;
    placement.trace.singlePassOutputs=options.surfaceCacheSinglePassOutputs;
    placement.trace.compactSdfRays=options.surfaceCacheCompactSdfRays;
    placement.trace.sdf.enabled=true;placement.trace.sdf.visualMode=0;placement.trace.sdf.sliceView=0;
    placement.trace.sdf.fastAtlasAddressing=options.surfaceCacheFastSdfAddressing;
    placement.trace.sdf.reuseTraceSamples=options.surfaceCacheReuseSdfSamples;
    placement.trace.sdf.hardwareRayTracing=options.surfaceCacheNativeProbes;
    placement.trace.sdf.showHitPoints=false;placement.trace.sdf.showClipmapBounds=false;
    placement.trace.sdf.automaticQuality=frame.sdfModelStudy;
    e.surfaceCacheGi.enabled=true;e.surfaceCacheGi.raysPerPixel=options.surfaceCacheRays;
    e.surfaceCacheGi.view=options.surfaceCacheView;e.surfaceCacheGi.maxDistance=30;
    e.lighting.ambientAndExposure={0,0,0,1};for(auto& color:e.lighting.lightColors)color=glm::vec4(0);
    e.shadows.enabled=true;e.shadows.sunDirection={.1f,-.3f,-1};
    e.shadows.sunColor={1,.93f,.80f};e.shadows.sunIntensity=3;
    e.shadows.atlasResolution=2048;e.shadows.distance=32;
    e.sky.enabled=false;e.probes.enabled=false;e.realtimeGi.enabled=false;e.lumenGi.enabled=false;e.indirect.enabled=false;
    e.temporal.antialiasing=Render::AntialiasingMode::None;e.temporal.motionBlurEnabled=false;
    e.post.bloomEnabled=false;e.post.ssaoEnabled=false;e.post.filter=Render::PostFilter::None;
    if(options.sealedGiRoom) {
        frame.sealedGiRoomStudy=true;
        frame.screenProbeStudyHud=false;
        ConfigureSealedGiRoomLighting(e);
        gi.areaLightShadows=true;
        frame.camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    }
    ApplySurfaceCacheBudget(frame,options.gi90Profile);
    return frame;
}

void SurfaceCacheInteractive(MaterialLabModule& lab,ApplicationWindow::Window& window,const Options& options) {
    auto frame=MakeSurfaceCacheGiFrame(options);
    frame.effects.surfaceCacheGi.collectProbeCoverage=false;
    auto* native=static_cast<GLFWwindow*>(window.GetNativeWindow());
    // Preserve a short F/H press until the next rendered frame consumes it.
    if(options.sealedGiRoom)glfwSetInputMode(native,GLFW_STICKY_KEYS,GLFW_TRUE);
    DebugPanelHost panels(lab,native,false);FramePacer pacer;
    std::array<bool,GLFW_KEY_LAST+1> previous{};
    std::vector<ProbePlacementObject> roomObjects;
    if(options.sealedGiRoom) {
        roomObjects=SealedGiRoomObjects();
        for(unsigned index=SealedRoomMotion::FirstProp;index<roomObjects.size();++index)roomObjects[index].movable=true;
    }
    const auto initialRoomObjects=roomObjects;
    unsigned selectedObject=SealedRoomMotion::NoSelection;
    bool animateObjects=false;double animationSeconds=0;
    glm::vec3 position;float yaw=0,pitch=0;
    const auto pose=[&](const Render::PipelineCamera& camera){frame.camera=camera;position=camera.position;
        const auto direction=-glm::vec3(glm::inverse(camera.view)[2]);yaw=std::atan2(direction.x,-direction.z);pitch=std::asin(glm::clamp(direction.y,-1.f,1.f));};
    const auto preset=[&](unsigned n){
        if(options.sealedGiRoom)return SealedGiRoomCameraPreset(n,frame.OutputAspect());
#ifdef MATERIAL_LAB_HAS_MODEL
        if(options.sdfBanquet)return SdfModelCameraPreset(n,frame.OutputAspect());
#endif
        return ProbePlacementCameraPreset(n,frame.OutputAspect());};
    pose(preset(0));bool looking=false,saved=false;unsigned screenshotWarmup=0;double lastX=0,lastY=0,last=glfwGetTime(),sample=last;
    double smoothMs=16.7;
    std::cout<<"Surface Cache GI: adaptive probes | V views | F5 GI | F6 pixel reference | F7 rays | P probe overlay\n"
        "F2 adaptive | F3 spacing | J sampling seed | Space freeze placement | T temporal | G reset history | B probe coverage\n"
        "1/2/3 cameras | RMB look | WASD move | Q/E height | Shift fast | R reset | Esc exit\n";
    if(options.sealedGiRoom)std::cout<<"F selects visible prop under mouse (RMB: viewport center) | Arrows move selected prop on XZ ground\n"
        "H toggles all six props' motion | Arrow movement pauses H | Shift+H toggles spatial filtering | R restores all props and camera\n";
    else std::cout<<"Left/Right move block | H spatial filtering\n";
    std::cout<<"Camera movement stability: "<<(frame.effects.surfaceCacheGi.temporal.motionStability?"enabled":"disabled")<<'\n';
    std::cout<<"Moving-object GI reprojection: "<<(frame.effects.surfaceCacheGi.temporal.objectMotionReprojection?"enabled":"disabled")<<'\n';
    while(!window.ShouldClose()) {
        const auto started=std::chrono::steady_clock::now();window.PollEvents();DebugPanelFrame ui(panels);
        const double now=glfwGetTime();const float dt=float(std::clamp(now-last,.0001,.1));
        smoothMs=.9*smoothMs+.1*std::clamp(now-last,.0001,1.)*1000;last=now;
        const bool focused=glfwGetWindowAttrib(native,GLFW_FOCUSED)!=0;
        const auto down=[&](int key){return focused&&!ui.WantsKeyboard()&&glfwGetKey(native,key)==GLFW_PRESS;};
        const auto pressed=[&](int key){const bool value=glfwGetKey(native,key)==GLFW_PRESS,edge=value&&!previous[key];previous[key]=value;return focused&&edge&&!ui.WantsKeyboard();};
        if(pressed(GLFW_KEY_ESCAPE))break;
        int width=0,height=0;glfwGetFramebufferSize(native,&width,&height);if(width<=0||height<=0){glfwWaitEventsTimeout(.05);continue;}
        SetShowcaseResolution(frame,unsigned(width),unsigned(height),1);
        if(options.renderSizeSet) {
            frame.width=options.benchmarkWidth;frame.height=options.benchmarkHeight;
            frame.effects.outputSize={unsigned(width),unsigned(height)};
        }
        auto& gi=frame.effects.surfaceCacheGi;
        if(options.sealedGiRoom&&pressed(GLFW_KEY_L))frame.effects.areaLights.lights[0].enabled=!frame.effects.areaLights.lights[0].enabled;
        if(!lab.GetDebugUI()&&pressed(GLFW_KEY_F1))frame.screenProbeStudyHud=!frame.screenProbeStudyHud;
        if(pressed(GLFW_KEY_V))gi.view=(gi.view+1)%7;
        if(pressed(GLFW_KEY_B))gi.view=gi.view==6?0:6;
        if(pressed(GLFW_KEY_F5))gi.enabled=!gi.enabled;
        if(pressed(GLFW_KEY_T))gi.temporal.enabled=!gi.temporal.enabled;
        const bool fast=down(GLFW_KEY_LEFT_SHIFT)||down(GLFW_KEY_RIGHT_SHIFT);
        if(pressed(GLFW_KEY_H)) {
            if(options.sealedGiRoom&&!fast)animateObjects=!animateObjects;
            else gi.spatial.enabled=!gi.spatial.enabled;
        }
        if(pressed(GLFW_KEY_G))gi.temporal.reset=true;
        auto& placement=frame.effects.screenProbePlacement;
        if(pressed(GLFW_KEY_F6)){gi.useScreenProbes=!gi.useScreenProbes;placement.reset=true;}
        if(pressed(GLFW_KEY_F7)){if(gi.useScreenProbes)placement.trace.rayResolution=placement.trace.rayResolution==4?8:placement.trace.rayResolution==8?16:4;
            else gi.raysPerPixel=gi.raysPerPixel==1?4:gi.raysPerPixel==4?8:gi.raysPerPixel==8?16:1;}
        if(pressed(GLFW_KEY_F2)){placement.adaptive=!placement.adaptive;placement.reset=true;}
        if(pressed(GLFW_KEY_F3)){placement.baseSpacing=placement.baseSpacing==16?32:placement.baseSpacing==32?64:16;placement.reset=true;}
        if(pressed(GLFW_KEY_J))placement.trace.samplingSeed=(placement.trace.samplingSeed+1)%0x20000000u;
        if(pressed(GLFW_KEY_SPACE))placement.freeze=!placement.freeze;
        if(pressed(GLFW_KEY_P)){gi.probeDiagnostics=!gi.probeDiagnostics;frame.effects.screenProbePlacement.reset=true;}
        for(unsigned n=0;n<3;++n)if(pressed(GLFW_KEY_1+int(n)))pose(preset(n));
        if(pressed(GLFW_KEY_R)) {
            frame.screenProbeStudyState={};pose(preset(0));
            if(options.sealedGiRoom) {
                roomObjects=initialRoomObjects;selectedObject=SealedRoomMotion::NoSelection;
                animateObjects=false;animationSeconds=0;gi.temporal.reset=true;placement.reset=true;
            }
        }
        if(!options.sealedGiRoom)frame.screenProbeStudyState.occluderOffset=std::clamp(frame.screenProbeStudyState.occluderOffset+
            (float(down(GLFW_KEY_RIGHT))-float(down(GLFW_KEY_LEFT)))*dt,-1.2f,2.5f);
        double x=0,y=0;glfwGetCursorPos(native,&x,&y);
        const bool rmb=!ui.WantsMouse()&&glfwGetWindowAttrib(native,GLFW_FOCUSED)&&glfwGetMouseButton(native,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS;
        if(rmb!=looking){
            glfwSetInputMode(native,GLFW_CURSOR,rmb?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL);
            if(glfwRawMouseMotionSupported())glfwSetInputMode(native,GLFW_RAW_MOUSE_MOTION,rmb?GLFW_TRUE:GLFW_FALSE);
            glfwGetCursorPos(native,&x,&y);
        }
        if(rmb&&looking){yaw+=float(x-lastX)*.0025f;pitch=std::clamp(pitch-float(y-lastY)*.0025f,-1.5f,1.5f);}lastX=x;lastY=y;looking=rmb;
        const auto forward=ShowcaseForward(yaw,0),right=glm::normalize(glm::cross(forward,glm::vec3(0,1,0)));
        const auto motion=forward*(float(down(GLFW_KEY_W))-float(down(GLFW_KEY_S)))+right*(float(down(GLFW_KEY_D))-float(down(GLFW_KEY_A)))+
            glm::vec3(0,float(down(GLFW_KEY_E))-float(down(GLFW_KEY_Q)),0);
        if(glm::dot(motion,motion)>0)position+=glm::normalize(motion)*2.5f*dt*(fast?3.f:1.f);
        if(options.sealedGiRoom)position=glm::clamp(position,glm::vec3(-3.75f,.2f,-3.25f),glm::vec3(3.75f,4.25f,3.25f));
        auto camera=frame.camera.value();camera.position=position;camera.view=glm::lookAt(position,position+ShowcaseForward(yaw,pitch),glm::vec3(0,1,0));
        camera.projection=glm::perspective(glm::radians(55.f),frame.OutputAspect(),camera.nearPlane,camera.farPlane);frame.camera=camera;
        if(options.sealedGiRoom) {
            if(pressed(GLFW_KEY_F)&&!ui.WantsMouse()) {
                int cursorWidth=0,cursorHeight=0;glfwGetWindowSize(native,&cursorWidth,&cursorHeight);
                const double u=looking?.5:cursorWidth>0?x/double(cursorWidth):-1;
                const double v=looking?.5:cursorHeight>0?y/double(cursorHeight):-1;
                selectedObject=SealedRoomMotion::Pick(roomObjects,camera,u,v);
                std::cout<<"Selected prop: "<<(selectedObject==SealedRoomMotion::NoSelection?"none (aim at a visible prop)":roomObjects[selectedObject].name)<<'\n';
            }
            const auto displacement=SealedRoomMotion::GroundDisplacement(
                float(down(GLFW_KEY_RIGHT))-float(down(GLFW_KEY_LEFT)),
                float(down(GLFW_KEY_DOWN))-float(down(GLFW_KEY_UP)),dt,fast);
            if(selectedObject!=SealedRoomMotion::NoSelection&&glm::dot(displacement,displacement)>0) {
                animateObjects=false;roomObjects[selectedObject].position+=displacement;
                SealedRoomMotion::KeepInsideRoom(roomObjects[selectedObject]);
            }
            if(animateObjects) {
                SealedRoomMotion::Advance(roomObjects,animationSeconds,animationSeconds+double(dt));
                animationSeconds+=double(dt);
                for(unsigned index=SealedRoomMotion::FirstProp;index<roomObjects.size();++index)SealedRoomMotion::KeepInsideRoom(roomObjects[index]);
            }
            // One fixed six-prop palette drives raster, SDF and card instances.
            // Geometry and materials retain their immutable scene identity.
            SealedRoomMotion::Sync(frame,roomObjects);
        }
        ui.Finish(frame);
        placement.enabled=gi.enabled&&(gi.useScreenProbes||gi.probeDiagnostics);
        placement.trace.cosineHemisphere=gi.useScreenProbes;
        frame.present=true;Check(lab.Render(frame),lab.LastError());
        frame.effects.cameraCut=false;frame.effects.screenProbePlacement.reset=false;gi.temporal.reset=false;
        if(now-sample>=1){frame.displayFps=float(1000/smoothMs);sample=now;}
        ++screenshotWarmup;
        if(!saved&&!options.screenshot.empty()&&!lab.ProbeSdfQualityStatistics().pending&&(!options.sealedGiRoom||screenshotWarmup>=32)){
            frame.present=false;Check(lab.Render(frame),lab.LastError());Save(lab.Readback(),options.screenshot);saved=true;}
        const auto rayLabel=gi.useScreenProbes?std::to_string(placement.trace.rayResolution*placement.trace.rayResolution)+" rays / probe":std::to_string(gi.raysPerPixel)+" rays / pixel";
        const auto movingHistoryLimit=gi.temporal.motionStability?std::min(64u,std::max(32u,gi.temporal.maxFrames)):gi.temporal.maxFrames;
        const auto historyLabel=gi.temporal.enabled&&gi.useScreenProbes?
            (gi.temporal.staticConvergence&&gi.temporal.maxFrames>1?"static 64 / camera up to "+std::to_string(movingHistoryLimit):std::to_string(gi.temporal.maxFrames)+" frames"):"off";
        const auto interactionLabel=options.sealedGiRoom?std::string("Selected: ")+
            (selectedObject==SealedRoomMotion::NoSelection?"none (F to pick)":roomObjects[selectedObject].name)+
            " | H motion: "+(animateObjects?"running":"paused")+" | F mouse / RMB center / arrows XZ / Shift+H spatial | ":std::string();
        glfwSetWindowTitle(native,((options.sealedGiRoom?"Sealed ceramic GI | ":"Surface Cache GI | ")+interactionLabel+rayLabel+" | Spatial "+(gi.spatial.enabled&&gi.useScreenProbes?"on":"off")+" | Temporal "+historyLabel+" | V views").c_str());
        if(options.fpsLimit>0)pacer.WaitUntil(started+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1./options.fpsLimit)));
    }
    if(glfwRawMouseMotionSupported())glfwSetInputMode(native,GLFW_RAW_MOUSE_MOTION,GLFW_FALSE);
    glfwSetInputMode(native,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
}

bool SurfaceCacheReferenceOccluded(glm::dvec3 origin,glm::dvec3 direction,double maximum,
    const std::vector<Render::ProbeTriangle>& triangles) {
    // Independent double-precision brute force. This deliberately does not
    // reuse the software BVH, SDF, card lookup or GPU visibility traversal.
    for(const auto& triangle:triangles) {
        const glm::dvec3 a(triangle.a),ab=glm::dvec3(triangle.b)-a,ac=glm::dvec3(triangle.c)-a;
        const auto q=glm::cross(direction,ac);const double determinant=glm::dot(ab,q);
        if(std::abs(determinant)<1e-12)continue;
        const auto relative=origin-a;const double u=glm::dot(relative,q)/determinant;if(u<0||u>1)continue;
        const auto r=glm::cross(relative,ab);const double v=glm::dot(direction,r)/determinant;if(v<0||u+v>1)continue;
        const double distance=glm::dot(ac,r)/determinant;
        if(distance>1e-5&&distance<maximum)return true;
    }return false;
}

void SurfaceCacheQuality(MaterialLabModule& lab,const Options& options) {
    auto fixtureOptions=options;fixtureOptions.sdfBanquet=false;fixtureOptions.sdfClipmaps=false;
    auto frame=MakeSurfaceCacheGiFrame(fixtureOptions);frame.width=160;frame.height=120;frame.effects.outputSize={0,0};
    frame.effects.surfaceCacheGi.temporal.enabled=false;
    frame.effects.surfaceCacheGi.spatial.enabled=false;
    frame.effects.surfaceCacheGi.useScreenProbes=false;frame.effects.screenProbePlacement.enabled=false;
    frame.effects.screenProbePlacement.trace.cosineHemisphere=false;
    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.surfaceCacheGi.raysPerPixel=8;
    frame.effects.surfaceCacheGi.view=0;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;};
    const auto finite=[&](const std::vector<glm::vec4>& data,const char* what){Check(!data.empty(),std::string("Missing ")+what+": "+lab.LastError());
        for(const auto& p:data)for(unsigned c=0;c<4;++c)Check(std::isfinite(p[c]),std::string("Nonfinite ")+what);};
    const auto energy=[](const std::vector<glm::vec4>& data){double sum=0;for(const auto& p:data)sum+=p.x+p.y+p.z;return sum;};
    render();auto cache=lab.PreparedSurfaceCache();Check(bool(cache),"Surface Cache was not prepared");
    Check(cache->CardCount()==ProbeSdfObjects().size()*6,"Surface Cache must contain six cards per room object");
    const auto generation=cache->Generation();const auto lit=lab.ReadbackSurfaceCacheRawGi();finite(lit,"raw GI");
    const auto atlas=lab.ReadbackSurfaceCacheAtlas();finite(atlas,"cache atlas");
    const auto coverage=lab.ReadbackSurfaceCacheCoverage();finite(coverage,"coverage");
    const auto triangles=ProbeSdfTriangles();const auto& attributes=cache->Attributes();
    const auto lightDirection=-glm::normalize(glm::dvec3(frame.effects.shadows.sunDirection));
    const auto vp=glm::dmat4(frame.camera->projection)*glm::dmat4(frame.camera->view);
    unsigned referenceClear=0,referenceShadow=0,referenceOffscreen=0;double referenceError=0;
    const auto floorTexels=6u*cache->Settings().pageResolution*cache->Settings().pageResolution;
    for(unsigned index=0;index<floorTexels;++index) {
        const auto p=attributes[index],n=attributes[cache->PlaneTexels()+index];if(p.w<.5f||n.y<.999f)continue;
        const bool blocked=SurfaceCacheReferenceOccluded(glm::dvec3(p)+glm::dvec3(n)*double(frame.effects.surfaceCacheGi.rayBias),
            lightDirection,frame.effects.surfaceCacheGi.maxDistance-2.*frame.effects.surfaceCacheGi.rayBias,triangles);
        if((blocked&&referenceShadow>=32)||(!blocked&&referenceClear>=32))continue;
        const auto expected=blocked?glm::dvec3(0):glm::dvec3(.65)*glm::dvec3(frame.effects.shadows.sunColor)*
            (double(frame.effects.shadows.sunIntensity)*std::max(glm::dot(glm::dvec3(n),lightDirection),0.)/3.141592653589793);
        const double error=glm::length(glm::dvec3(atlas[index])-expected);referenceError=std::max(referenceError,error);
        Check(error<2e-5,"Surface Cache direct sunlight disagrees with independent Lambert/triangle-shadow reference");
        if(blocked)++referenceShadow;else ++referenceClear;
        const auto clip=vp*glm::dvec4(glm::dvec3(p),1);
        if(clip.w<=0||std::abs(clip.x)>clip.w||std::abs(clip.y)>clip.w)++referenceOffscreen;
    }
    Check(referenceClear>=8&&referenceShadow>=8&&referenceOffscreen>0,
        "Analytic card-lighting reference did not cover lit, shadowed and offscreen floor samples");
    Check(lit.size()==std::size_t(frame.width)*frame.height,"Raw GI dimensions differ from scene");
    Check(energy(lit)>1e-3&&energy(atlas)>1e-3,"Software rays did not read directly lit offscreen cache radiance");
    Check(lab.PipelineStatistics().screenProbeTracePasses==0,"Raw per-pixel GI unexpectedly depends on screen radiance");
    Check(lab.PipelineStatistics().temporalPasses==0&&!lab.PipelineStatistics().historyUsed,"Basic GI used temporal filtering/history");
    frame.effects.shadows.sunIntensity=0;render();auto black=lab.ReadbackSurfaceCacheRawGi();auto blackAtlas=lab.ReadbackSurfaceCacheAtlas();finite(black,"unlit GI");
    Check(std::abs(energy(black))<1e-5&&std::abs(energy(blackAtlas))<1e-5,"No-light cache or GI contains fabricated ambient energy");
    Check(std::count_if(blackAtlas.begin(),blackAtlas.end(),[](const auto& p){return p.w>.5f;})>10,
        "Legal black cache texels were classified as unreadable");
    frame.effects.shadows.sunIntensity=6;render();const auto twice=lab.ReadbackSurfaceCacheRawGi();finite(twice,"scaled GI");
    double scaleError=0;for(std::size_t i=0;i<lit.size();++i)for(unsigned c=0;c<3;++c)scaleError+=std::abs(double(twice[i][c])-2.*lit[i][c]);
    Check(scaleError<energy(lit)*.002+1e-4,"Raw GI is not linear in light intensity");
    frame.effects.shadows.sunIntensity=3;frame.camera=ProbePlacementCameraPreset(1,frame.OutputAspect());render();
    Check(lab.PreparedSurfaceCache()->Generation()==generation,"Camera movement invalidated the Surface Cache");
    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());render();const auto again=lab.ReadbackSurfaceCacheRawGi();
    double historyError=0;for(std::size_t i=0;i<lit.size();++i)historyError+=glm::length(glm::dvec3(again[i])-glm::dvec3(lit[i]));
    Check(historyError<1e-5,"A-B-A camera/light sequence depends on frame history");
    render();Check(!lab.PipelineStatistics().surfaceCacheLightingUpdated&&lab.PipelineStatistics().surfaceCacheUpdatedTexels==0,
        "Static cache repeated its direct-lighting update");
    frame.screenProbeStudyState.occluderOffset=.6f;render();
    Check(lab.PreparedSurfaceCache()->Generation()>generation,"Moving object failed to invalidate its cards");
    Check(cache->Statistics().updatedCards==6,"Moving one object recaptured unrelated instance cards");
    const auto moved=lab.ReadbackSurfaceCacheRawGi();finite(moved,"moved GI");
    if(!options.screenshot.empty())Save(lab.Readback(),options.screenshot);
    // Valid geometry paired with deliberately unrelated card captures must
    // report missing coverage; the diagnostic color must not become light.
    auto shifted=lab.PreparedProbeSdfScene()->Instances();
    for(auto& instance:shifted)instance.localToWorld[3].x+=50;
    Render::ProbeGlobalSdfSettings tiny;tiny.counts=glm::uvec3(2);
    const auto unrelated=Render::BuildProbeSdfScene(shifted,tiny);
    frame.effects.surfaceCacheGi.cache=Render::BuildSurfaceCache(*unrelated);
    frame.effects.surfaceCacheGi.view=3;render();const auto missing=lab.ReadbackSurfaceCacheCoverage();finite(missing,"missing coverage");
    Check(std::count_if(missing.begin(),missing.end(),[](const auto& p){return p.x>.9f&&p.y<.1f&&p.z>.5f;})>10,
        "Uncovered surfaces were not explicitly marked magenta");
    frame.effects.surfaceCacheGi.view=2;render();
    const auto missingGi=lab.ReadbackSurfaceCacheRawGi();finite(missingGi,"uncovered GI");
    Check(std::abs(energy(missingGi))<1e-5,"Missing cache diagnostic color leaked into GI energy");
    frame.effects.surfaceCacheGi.cache.reset();frame.effects.surfaceCacheGi.view=0;
    auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;auto& sdf=trace.sdf;
    placement.enabled=true;placement.adaptive=false;placement.baseSpacing=16;placement.frameIndex=0;
    placement.freeze=false;trace.enabled=true;trace.showPanel=false;trace.rayResolution=4;trace.displayMode=0;
    sdf.visualMode=0;sdf.sliceView=0;sdf.showHitPoints=false;sdf.meshTraceDistance=trace.maxDistance;
    unsigned cachedProbeHits=0,cachedOffscreenHits=0;
    for(unsigned phase=0;phase<2;++phase) {
        sdf.useMesh=phase==0;sdf.useGlobal=phase==1;
        frame.effects.surfaceCacheGi.enabled=false;frame.effects.surfaceCacheGi.probeDiagnostics=false;placement.reset=true;
        render();const auto before=lab.ReadbackScreenProbeTrace(false);Check(!before.radianceStatus.empty(),"Missing baseline staged rays");
        frame.effects.surfaceCacheGi.enabled=true;frame.effects.surfaceCacheGi.probeDiagnostics=true;placement.reset=false;
        render();const auto after=lab.ReadbackScreenProbeTrace(false);
        Check(after.radianceStatus.size()==before.radianceStatus.size(),"Cache fallback changed probe ray count");
        unsigned filled=0;std::array<unsigned,17> statuses{};
        for(std::size_t ray=0;ray<after.radianceStatus.size();++ray) {
            const auto old=before.radianceStatus[ray].w,current=after.radianceStatus[ray].w;
            const auto& metadata=after.hitPixelStepsMip[ray];const auto& hit=after.hitWorldDistance[ray];
            Check(current>=0&&current<=16,"Cache fallback emitted an unknown status");++statuses[unsigned(current)];
            if(old>=13&&old<=16)Check(current==old,"Cache fallback fabricated a hit for an unresolved ray");
            if(old!=float(phase==0?11:12)||current!=float(phase==0?9:10)||metadata.x!=-1||metadata.y!=-1)continue;
            Check(glm::length(hit-before.hitWorldDistance[ray])<1e-5f,"Cache fallback moved the SDF geometry hit");
            ++filled;const auto clip=vp*glm::dvec4(glm::dvec3(hit),1);
            if(clip.w<=0||std::abs(clip.x)>clip.w||std::abs(clip.y)>clip.w)++cachedOffscreenHits;
        }
        Check(filled>0,phase==0?"Mesh SDF unreadable hits did not become readable through cards":"Global SDF unreadable hits did not become readable through cards");
        cachedProbeHits+=filled;std::cout<<"Surface Cache staged "<<(phase==0?"Mesh":"Global")<<" newly_readable="<<filled<<" statuses=";
        for(unsigned s=0;s<statuses.size();++s)std::cout<<s<<':'<<statuses[s]<<' ';std::cout<<'\n';
    }
    Check(cachedOffscreenHits>0,"Staged cache fallback test did not cover any actual offscreen hit");
    std::cout<<"Surface Cache GI passed: cards="<<cache->CardCount()<<" valid_texels="<<cache->Statistics().validTexels
        <<" raw_energy="<<energy(lit)<<" light_scale_error="<<scaleError<<" history_error="<<historyError
        <<" direct_ref_lit="<<referenceClear<<" shadow="<<referenceShadow<<" offscreen="<<referenceOffscreen<<" max_error="<<referenceError
        <<" staged_cache_hits="<<cachedProbeHits<<" staged_offscreen="<<cachedOffscreenHits<<'\n';
}

void SurfaceCachePerformance(MaterialLabModule& lab,const Options& options) {
    auto frame=MakeSurfaceCacheGiFrame(options);frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.surfaceCacheGi.temporal.enabled=false;
    frame.effects.surfaceCacheGi.spatial.enabled=false;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;frame.effects.screenProbePlacement.reset=false;};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    do{render();Check(std::chrono::steady_clock::now()<deadline,"Surface Cache source bake timed out");}while(lab.ProbeSdfQualityStatistics().pending);
    const auto measure=[&](const char* name){render();const auto t=lab.MeasureFrames(frame,12);const auto p=lab.PipelineStatistics();
        std::cout<<"SURFACE CACHE PERF "<<name<<" size="<<frame.width<<'x'<<frame.height<<" gpu_ms="<<t.gpuMilliseconds
            <<" elapsed_ms="<<t.elapsedMilliseconds<<" rays="<<p.surfaceCacheRays<<" cache_texels="<<p.surfaceCacheTexels
            <<" updated="<<p.surfaceCacheUpdatedTexels<<" lighting_update="<<p.surfaceCacheLightingUpdated
            <<" probe_slots="<<p.surfaceCacheProbeSlots<<" probe_passes="<<p.surfaceCacheProbePasses
            <<" spatial_passes="<<p.surfaceCacheSpatialPasses
            <<" temporal_passes="<<p.surfaceCacheTemporalPasses<<" history_used="<<p.surfaceCacheHistoryUsed<<std::endl;};
    frame.effects.surfaceCacheGi.enabled=false;frame.effects.screenProbePlacement.enabled=false;measure("direct_only");
    frame.effects.surfaceCacheGi.enabled=true;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    gi.useScreenProbes=true;placement.enabled=true;placement.baseSpacing=32;placement.trace.cosineHemisphere=true;
    placement.trace.rayResolution=4;measure("probe_16rays_spacing32");
    placement.trace.rayResolution=8;measure("probe_64rays_spacing32");
    gi.spatial.enabled=true;measure("probe_64rays_spacing32_spatial");
    gi.temporal.enabled=true;measure("probe_64rays_spacing32_spatial_temporal");gi.temporal.enabled=false;gi.spatial.enabled=false;
    gi.temporal.enabled=true;measure("probe_64rays_spacing32_temporal");gi.temporal.enabled=false;
    placement.trace.rayResolution=16;measure("probe_256rays_spacing32");
    placement.trace.rayResolution=8;placement.baseSpacing=16;placement.reset=true;measure("probe_64rays_spacing16");
    placement.baseSpacing=32;placement.reset=true;gi.useScreenProbes=false;placement.enabled=false;
    frame.effects.surfaceCacheGi.raysPerPixel=1;measure("raw_gi_1ray");
    frame.effects.surfaceCacheGi.raysPerPixel=4;measure("raw_gi_4rays");
    frame.effects.surfaceCacheGi.raysPerPixel=8;measure("raw_gi_8rays");
    frame.effects.surfaceCacheGi.view=1;measure("cache_view");
    gi.useScreenProbes=true;placement.enabled=true;placement.trace.rayResolution=8;
    frame.effects.surfaceCacheGi.view=0;frame.screenProbeStudyState.occluderOffset=.6f;
    const auto t=lab.MeasureFrames(frame,1);const auto p=lab.PipelineStatistics();
    std::cout<<"SURFACE CACHE PERF dynamic gpu_ms="<<t.gpuMilliseconds<<" elapsed_ms="<<t.elapsedMilliseconds
        <<" updated="<<p.surfaceCacheUpdatedTexels<<" cache_cpu_ms="<<lab.PreparedSurfaceCache()->Statistics().updateMilliseconds<<std::endl;
    if(!options.screenshot.empty()){
        gi.spatial.enabled=!options.surfaceCacheNoSpatial;
        gi.temporal.enabled=!options.surfaceCacheNoTemporal;frame.screenProbeStudyHud=true;
        for(unsigned i=0;i<std::max(12u,gi.temporal.maxFrames);++i)render();
        Save(lab.Readback(),options.screenshot);
    }
    std::cout<<"Surface Cache performance complete (12 timed frames/case; no present, pacing or readback in batches).\n";
}
