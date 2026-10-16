// Independent Surface Cache stage. No dependency on the older hybrid GI,
// world-radiance cache or final-image antialiasing. Adaptive surface probes are
// the normal gather; the per-pixel path remains an explicit reference.
FrameSettings MakeSurfaceCacheGiFrame(const Options& options) {
    auto frame=MakeProbeSdfTraceFrame(options);
#ifdef MATERIAL_LAB_HAS_MODEL
    if(options.sdfBanquet)frame=options.sdfClipmaps?MakeSdfClipmapFrame(options):MakeSdfBanquetFrame(options);
#endif
    frame.surfaceCacheStudy=true;
    auto& e=frame.effects;auto& placement=e.screenProbePlacement;
    auto& gi=e.surfaceCacheGi;gi.useScreenProbes=!options.surfaceCacheReference;
    gi.temporal.enabled=!options.surfaceCacheNoTemporal;gi.temporal.maxFrames=options.surfaceCacheHistoryFrames;
    gi.temporal.directionPhases=options.surfaceCacheHistoryDirections;
    placement.enabled=gi.useScreenProbes;placement.adaptive=true;placement.baseSpacing=options.surfaceCacheProbeSpacing;
    placement.frameIndex=0;placement.freeze=false;placement.debugView=0;placement.markerRadiusPixels=3;
    placement.trace.enabled=true;placement.trace.showPanel=false;placement.trace.cosineHemisphere=gi.useScreenProbes;
    placement.trace.rayResolution=options.surfaceCacheProbeRays;placement.trace.maxDistance=30;
    placement.trace.sdf.enabled=true;placement.trace.sdf.visualMode=0;placement.trace.sdf.sliceView=0;
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
    return frame;
}

void SurfaceCacheInteractive(MaterialLabModule& lab,ApplicationWindow::Window& window,const Options& options) {
    auto frame=MakeSurfaceCacheGiFrame(options);
    auto* native=static_cast<GLFWwindow*>(window.GetNativeWindow());
    DebugPanelHost panels(lab,native,false);FramePacer pacer;
    std::array<bool,GLFW_KEY_LAST+1> previous{};
    glm::vec3 position;float yaw=0,pitch=0;
    const auto pose=[&](const Render::PipelineCamera& camera){frame.camera=camera;position=camera.position;
        const auto direction=-glm::vec3(glm::inverse(camera.view)[2]);yaw=std::atan2(direction.x,-direction.z);pitch=std::asin(glm::clamp(direction.y,-1.f,1.f));};
    const auto preset=[&](unsigned n){
#ifdef MATERIAL_LAB_HAS_MODEL
        if(options.sdfBanquet)return SdfModelCameraPreset(n,frame.OutputAspect());
#endif
        return ProbePlacementCameraPreset(n,frame.OutputAspect());};
    pose(preset(0));bool looking=false,saved=false;double lastX=0,lastY=0,last=glfwGetTime(),sample=last;
    double smoothMs=16.7;
    std::cout<<"Surface Cache GI: adaptive probes | V views | F5 GI | F6 pixel reference | F7 rays | P probe overlay\n"
        "F2 adaptive | F3 spacing | J sampling seed | Space freeze placement | T temporal | G reset history\n"
        "1/2/3 cameras | Arrows move object | RMB look | WASD move | Q/E height | Shift fast | R reset | Esc exit\n";
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
        if(!lab.GetDebugUI()&&pressed(GLFW_KEY_F1))frame.screenProbeStudyHud=!frame.screenProbeStudyHud;
        if(pressed(GLFW_KEY_V))gi.view=(gi.view+1)%6;
        if(pressed(GLFW_KEY_F5))gi.enabled=!gi.enabled;
        if(pressed(GLFW_KEY_T))gi.temporal.enabled=!gi.temporal.enabled;
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
        if(pressed(GLFW_KEY_R)){frame.screenProbeStudyState={};pose(preset(0));}
        frame.screenProbeStudyState.occluderOffset=std::clamp(frame.screenProbeStudyState.occluderOffset+
            (float(down(GLFW_KEY_RIGHT))-float(down(GLFW_KEY_LEFT)))*dt,-1.2f,2.5f);
        double x=0,y=0;glfwGetCursorPos(native,&x,&y);
        const bool rmb=!ui.WantsMouse()&&glfwGetWindowAttrib(native,GLFW_FOCUSED)&&glfwGetMouseButton(native,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS;
        if(rmb!=looking){glfwSetInputMode(native,GLFW_CURSOR,rmb?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL);glfwGetCursorPos(native,&x,&y);}
        if(rmb&&looking){yaw+=float(x-lastX)*.0025f;pitch=std::clamp(pitch-float(y-lastY)*.0025f,-1.5f,1.5f);}lastX=x;lastY=y;looking=rmb;
        const auto forward=ShowcaseForward(yaw,0),right=glm::normalize(glm::cross(forward,glm::vec3(0,1,0)));
        const auto motion=forward*(float(down(GLFW_KEY_W))-float(down(GLFW_KEY_S)))+right*(float(down(GLFW_KEY_D))-float(down(GLFW_KEY_A)))+
            glm::vec3(0,float(down(GLFW_KEY_E))-float(down(GLFW_KEY_Q)),0);
        if(glm::dot(motion,motion)>0)position+=glm::normalize(motion)*2.5f*dt*(down(GLFW_KEY_LEFT_SHIFT)?3.f:1.f);
        auto camera=frame.camera.value();camera.position=position;camera.view=glm::lookAt(position,position+ShowcaseForward(yaw,pitch),glm::vec3(0,1,0));
        camera.projection=glm::perspective(glm::radians(55.f),frame.OutputAspect(),camera.nearPlane,camera.farPlane);frame.camera=camera;
        ui.Finish(frame);
        placement.enabled=gi.enabled&&(gi.useScreenProbes||gi.probeDiagnostics);
        placement.trace.cosineHemisphere=gi.useScreenProbes;
        frame.present=true;Check(lab.Render(frame),lab.LastError());
        frame.effects.cameraCut=false;frame.effects.screenProbePlacement.reset=false;gi.temporal.reset=false;
        if(now-sample>=1){frame.displayFps=float(1000/smoothMs);sample=now;}
        if(!saved&&!options.screenshot.empty()&&!lab.ProbeSdfQualityStatistics().pending){
            frame.present=false;Check(lab.Render(frame),lab.LastError());Save(lab.Readback(),options.screenshot);saved=true;}
        const auto rayLabel=gi.useScreenProbes?std::to_string(placement.trace.rayResolution*placement.trace.rayResolution)+" rays / probe":std::to_string(gi.raysPerPixel)+" rays / pixel";
        glfwSetWindowTitle(native,("Surface Cache GI | "+rayLabel+" | Temporal "+(gi.temporal.enabled&&gi.useScreenProbes?std::to_string(gi.temporal.maxFrames)+" frames":"off")+" | V views").c_str());
        if(options.fpsLimit>0)pacer.WaitUntil(started+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1./options.fpsLimit)));
    }
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
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;frame.effects.screenProbePlacement.reset=false;};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    do{render();Check(std::chrono::steady_clock::now()<deadline,"Surface Cache source bake timed out");}while(lab.ProbeSdfQualityStatistics().pending);
    const auto measure=[&](const char* name){render();const auto t=lab.MeasureFrames(frame,12);const auto p=lab.PipelineStatistics();
        std::cout<<"SURFACE CACHE PERF "<<name<<" size="<<frame.width<<'x'<<frame.height<<" gpu_ms="<<t.gpuMilliseconds
            <<" elapsed_ms="<<t.elapsedMilliseconds<<" rays="<<p.surfaceCacheRays<<" cache_texels="<<p.surfaceCacheTexels
            <<" updated="<<p.surfaceCacheUpdatedTexels<<" lighting_update="<<p.surfaceCacheLightingUpdated
            <<" probe_slots="<<p.surfaceCacheProbeSlots<<" probe_passes="<<p.surfaceCacheProbePasses
            <<" temporal_passes="<<p.surfaceCacheTemporalPasses<<" history_used="<<p.surfaceCacheHistoryUsed<<std::endl;};
    frame.effects.surfaceCacheGi.enabled=false;frame.effects.screenProbePlacement.enabled=false;measure("direct_only");
    frame.effects.surfaceCacheGi.enabled=true;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    gi.useScreenProbes=true;placement.enabled=true;placement.baseSpacing=32;placement.trace.cosineHemisphere=true;
    placement.trace.rayResolution=4;measure("probe_16rays_spacing32");
    placement.trace.rayResolution=8;measure("probe_64rays_spacing32");
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
        gi.temporal.enabled=!options.surfaceCacheNoTemporal;frame.screenProbeStudyHud=true;
        for(unsigned i=0;i<std::max(12u,gi.temporal.maxFrames);++i)render();
        Save(lab.Readback(),options.screenshot);
    }
    std::cout<<"Surface Cache performance complete (12 timed frames/case; no present, pacing or readback in batches).\n";
}
