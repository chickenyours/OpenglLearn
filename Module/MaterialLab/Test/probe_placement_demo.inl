FrameSettings MakeProbePlacementFrame(const Options& options) {
    FrameSettings frame;frame.screenProbeStudy=true;
    frame.effects.screenProbePlacement.enabled=true;
    frame.effects.screenProbePlacement.frameIndex=0;
    frame.effects.screenProbePlacement.markerRadiusPixels=4;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    frame.effects.temporal.msaaSamples=1;frame.effects.temporal.motionBlurEnabled=false;
    frame.effects.reflection.enabled=false;frame.effects.shadows.enabled=false;
    frame.effects.shadows.sunIntensity=1.1f;frame.effects.shadows.sunDirection={-.35f,-1,-.6f};
    frame.effects.post.bloomEnabled=false;frame.effects.post.ssaoEnabled=false;
    frame.effects.post.vignette=0;frame.effects.post.toneMap=Render::ToneMapMode::ACES;
    frame.effects.lighting.ambientAndExposure={.16f,.16f,.16f,1};
    for(auto& color:frame.effects.lighting.lightColors)color=glm::vec4(0);
    SetShowcaseResolution(frame,options.studySizeSet?options.benchmarkWidth:1280,options.studySizeSet?options.benchmarkHeight:800,1);
    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());return frame;
}
void ProbePlacementSave(MaterialLabModule& lab,const Options& options,const char* label) {
    if(options.screenshot.empty())return;
    auto path=std::filesystem::path(options.screenshot);
    path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(lab.Readback(),path.string());
}
FrameSettings MakeScreenProbeTraceFrame(const Options& options) {
    auto frame=MakeProbePlacementFrame(options);frame.effects.screenProbePlacement.trace.enabled=true;
    frame.effects.screenProbePlacement.markerRadiusPixels=7;return frame;
}
FrameSettings MakeProbeSdfTraceFrame(const Options& options) {
    auto frame=MakeScreenProbeTraceFrame(options);frame.sdfProbeStudy=true;
    auto& tracing=frame.effects.screenProbePlacement.trace;tracing.rayResolution=4;tracing.displayMode=1;
    tracing.sdf.enabled=true;tracing.sdf.sliceView=0;tracing.sdf.visualMode=options.sdfView;
    tracing.sdf.meshQuality=options.sdfQuality;tracing.sdf.automaticQuality=options.sdfAutomatic;
    tracing.sdf.showVoxelGrid=options.sdfVoxelGrid;tracing.sdf.visualizeSelected=options.sdfIsolate;tracing.sdf.selectedObject=options.sdfObject;return frame;
}
#ifdef MATERIAL_LAB_HAS_MODEL
FrameSettings MakeSdfBanquetFrame(const Options& options) {
    auto frame=MakeProbeSdfTraceFrame(options);frame.sdfModelStudy=true;
    auto& placement=frame.effects.screenProbePlacement;placement.baseSpacing=64;placement.markerRadiusPixels=4;
    auto& df=placement.trace.sdf;df.visualMode=options.sdfViewSet?options.sdfView:1;
    df.automaticQuality=!options.sdfQualitySet||options.sdfAutomatic;
    df.selectedObject=options.sdfIsolate?options.sdfObject:1;
    frame.camera=SdfModelCameraPreset(0,frame.OutputAspect());return frame;
}
Render::PipelineCamera SdfClipmapCameraPreset(unsigned preset,float aspect) {
    auto camera=SdfModelCameraPreset(preset,aspect);
    if(preset==1){camera.position={-2.6875f,3.3125f,5.6875f};
        camera.view=glm::lookAt(camera.position,glm::vec3(-2.75f,1.8f,4.f),glm::vec3(0,1,0));}
    return camera;
}
FrameSettings MakeSdfClipmapFrame(const Options& options) {
    auto frame=MakeSdfBanquetFrame(options);frame.sdfClipmapStudy=true;
    auto& df=frame.effects.screenProbePlacement.trace.sdf;
    df.clipmapsEnabled=true;df.showClipmapLevels=true;df.showClipmapBounds=true;
    df.visualMode=options.sdfViewSet?options.sdfView:2;
    frame.camera=SdfClipmapCameraPreset(0,frame.OutputAspect());return frame;
}
#endif
void ProbePlacementInteractive(MaterialLabModule& lab,ApplicationWindow::Window& window,const Options& options) {
    auto frame=options.sdfProbeTracing?MakeProbeSdfTraceFrame(options):options.screenProbeTracing?MakeScreenProbeTraceFrame(options):MakeProbePlacementFrame(options);
#ifdef MATERIAL_LAB_HAS_MODEL
    if(options.sdfBanquet)frame=MakeSdfBanquetFrame(options);
    if(options.sdfClipmaps)frame=MakeSdfClipmapFrame(options);
#endif
    const bool sdfViewer=options.sdfProbeTracing||options.sdfBanquet||options.sdfClipmaps;
    const auto presetCamera=[&](unsigned preset) {
#ifdef MATERIAL_LAB_HAS_MODEL
        if(options.sdfClipmaps)return SdfClipmapCameraPreset(preset,frame.OutputAspect());
        if(options.sdfBanquet)return SdfModelCameraPreset(preset,frame.OutputAspect());
#endif
        return ProbePlacementCameraPreset(preset,frame.OutputAspect());
    };
    auto* native=static_cast<GLFWwindow*>(window.GetNativeWindow());
    struct Input {float scroll=0;} input;
    glfwSetWindowUserPointer(native,&input);
    glfwSetScrollCallback(native,[](GLFWwindow* w,double x,double y){DebugUiScroll(w,x,y);static_cast<Input*>(glfwGetWindowUserPointer(w))->scroll+=float(y);});
    glm::vec3 position;float yaw=0,pitch=0,speed=2.5f;
    const auto pose=[&](const Render::PipelineCamera& camera){
        position=camera.position;const auto direction=-glm::vec3(glm::inverse(camera.view)[2]);
        yaw=std::atan2(direction.x,-direction.z);pitch=std::asin(glm::clamp(direction.y,-1.f,1.f));frame.camera=camera;
    };
    pose(*frame.camera);std::array<bool,GLFW_KEY_LAST+1> previous{};
    bool looking=false,saved=false;double lastX=0,lastY=0,lastTime=glfwGetTime(),lastSample=lastTime-1,smoothMs=1000./90;
    FramePacer pacer;
    DebugPanelHost debugPanels(lab, native, false);
    ScreenProbePlacementReadback selectable;bool previousClick=false;
    std::cout<<"UE5.7 Screen Probe Placement: BLUE uniform / ORANGE adaptive\n"
        "F2 adaptive | F3 spacing16/32/64 | F4 markers/candidates/coverage | Space freeze\n"
        "J temporal phases | 1/2/3 cameras | Left/Right move block | R reset\n"
        "RMB look | WASD move | Q/E down/up | Shift fast | Wheel speed | F1 controls | Esc exit\n";
    if(options.screenProbeTracing)std::cout<<"HZB screen traces: F5 enabled | F6 radiance/status | F7 rays 4x4/8x8 | Click select probe\n";
    while(!window.ShouldClose()) {
        const auto start=std::chrono::steady_clock::now();window.PollEvents();
        DebugPanelFrame uiFrame(debugPanels);
        const double now=glfwGetTime(),elapsed=now-lastTime;lastTime=now;
        const float dt=float(std::clamp(elapsed,.0001,.1));smoothMs=.9*smoothMs+.1*std::clamp(elapsed,.0001,1.)*1000;
        const auto down=[&](int key){return !uiFrame.WantsKeyboard() && glfwGetKey(native,key)==GLFW_PRESS;};
        const auto pressed=[&](int key){const bool value=glfwGetKey(native,key)==GLFW_PRESS,edge=value&&!previous[key];previous[key]=value;return edge && !uiFrame.WantsKeyboard();};
        if(pressed(GLFW_KEY_ESCAPE))break;
        int width=0,height=0;glfwGetFramebufferSize(native,&width,&height);
        if(width<=0||height<=0){glfwWaitEventsTimeout(.05);continue;}
        SetShowcaseResolution(frame,unsigned(width),unsigned(height),1);
        if(options.renderSizeSet) {
            frame.width=options.benchmarkWidth;frame.height=options.benchmarkHeight;
            frame.effects.outputSize=frame.width==unsigned(width)&&frame.height==unsigned(height)?glm::uvec2(0):glm::uvec2(width,height);
        }
        auto& placement=frame.effects.screenProbePlacement;placement.reset=false;
        if(!lab.GetDebugUI()&&pressed(GLFW_KEY_F1))frame.screenProbeStudyHud=!frame.screenProbeStudyHud;
        if(pressed(GLFW_KEY_F2)){placement.adaptive=!placement.adaptive;placement.reset=true;placement.trace.selectedProbe=std::numeric_limits<std::uint32_t>::max();}
        if(pressed(GLFW_KEY_F3)){placement.baseSpacing=placement.baseSpacing==16?32:placement.baseSpacing==32?64:16;placement.reset=true;placement.trace.selectedProbe=std::numeric_limits<std::uint32_t>::max();}
        if(pressed(GLFW_KEY_F4))placement.debugView=(placement.debugView+1)%(placement.freeze?2:3);
        if(options.screenProbeTracing||sdfViewer) {
            if(pressed(GLFW_KEY_F5))placement.trace.enabled=!placement.trace.enabled;
            if(pressed(GLFW_KEY_F6))placement.trace.displayMode=1-placement.trace.displayMode;
            if(pressed(GLFW_KEY_F7))placement.trace.rayResolution=placement.trace.rayResolution==4?8:4;
        }
        if(sdfViewer) {
            auto& df=placement.trace.sdf;
            if(pressed(GLFW_KEY_F8)){df.visualMode=0;df.sliceView=(df.sliceView+1)%3;}
            if(pressed(GLFW_KEY_F9))df.selectedObject=(df.selectedObject+1)%(options.sdfBanquet?16u:unsigned(ProbeSdfObjects().size()));
            if(pressed(GLFW_KEY_F10))df.enabled=!df.enabled;
            if(pressed(GLFW_KEY_F11))df.showHitPoints=!df.showHitPoints;
            if(pressed(GLFW_KEY_F12)){df.automaticQuality=false;df.meshQuality=(df.meshQuality+1)%3;}
            if(pressed(GLFW_KEY_L))df.automaticQuality=!df.automaticQuality;
            if(pressed(GLFW_KEY_V))df.visualMode=(df.visualMode+1)%4;
            if(pressed(GLFW_KEY_B))df.showVoxelGrid=!df.showVoxelGrid;
            if(pressed(GLFW_KEY_O))df.visualizeSelected=!df.visualizeSelected;
            if(options.sdfClipmaps) {
                if(pressed(GLFW_KEY_K))df.clipmapLevel=df.clipmapLevel==3?-1:df.clipmapLevel+1;
                if(pressed(GLFW_KEY_C))df.showClipmapLevels=!df.showClipmapLevels;
                if(pressed(GLFW_KEY_H))df.showClipmapBounds=!df.showClipmapBounds;
            }
            if(options.sdfBanquet&&pressed(GLFW_KEY_T))frame.sdfModelRasterLod=!frame.sdfModelRasterLod;
            if(pressed(GLFW_KEY_M))df.useMesh=!df.useMesh;
            if(pressed(GLFW_KEY_G))df.useGlobal=!df.useGlobal;
            if(pressed(GLFW_KEY_X))df.sliceAxis=0;if(pressed(GLFW_KEY_Y))df.sliceAxis=1;if(pressed(GLFW_KEY_Z))df.sliceAxis=2;
            if(pressed(GLFW_KEY_LEFT_BRACKET))df.slicePosition=std::max(0.f,df.slicePosition-.025f);
            if(pressed(GLFW_KEY_RIGHT_BRACKET))df.slicePosition=std::min(1.f,df.slicePosition+.025f);
        }
        if(pressed(GLFW_KEY_SPACE)){placement.freeze=!placement.freeze;if(placement.freeze)placement.debugView=0;}
        if(pressed(GLFW_KEY_J)){placement.frameIndex=placement.frameIndex==0?std::numeric_limits<std::uint32_t>::max():0;placement.reset=true;}
        for(unsigned preset=0;preset<3;++preset)if(pressed(GLFW_KEY_1+int(preset)))pose(presetCamera(preset));
        if(pressed(GLFW_KEY_R)){frame.screenProbeStudyState={};pose(presetCamera(0));placement.reset=true;}
        double x=0,y=0;glfwGetCursorPos(native,&x,&y);
        const bool focused=glfwGetWindowAttrib(native,GLFW_FOCUSED),rmb=!uiFrame.WantsMouse()&&focused&&glfwGetMouseButton(native,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS;
        if(rmb!=looking){
            glfwSetInputMode(native,GLFW_CURSOR,rmb?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL);
            if(glfwRawMouseMotionSupported())glfwSetInputMode(native,GLFW_RAW_MOUSE_MOTION,rmb?GLFW_TRUE:GLFW_FALSE);
            glfwGetCursorPos(native,&x,&y);
        }
        if(rmb&&looking){yaw+=float(x-lastX)*.0025f;pitch=std::clamp(pitch-float(y-lastY)*.0025f,-1.5f,1.5f);}
        lastX=x;lastY=y;looking=rmb;if(uiFrame.WantsMouse())input.scroll=0;
        speed=std::clamp(speed*std::pow(1.15f,input.scroll),.2f,10.f);input.scroll=0;
        if(focused){
            const auto forward=ShowcaseForward(yaw,0),right=glm::normalize(glm::cross(forward,glm::vec3(0,1,0)));
            const auto movement=forward*(float(down(GLFW_KEY_W))-float(down(GLFW_KEY_S)))+right*(float(down(GLFW_KEY_D))-float(down(GLFW_KEY_A)))+
                glm::vec3(0,float(down(GLFW_KEY_E))-float(down(GLFW_KEY_Q)),0);
            if(glm::dot(movement,movement)>0)position+=glm::normalize(movement)*speed*dt*(down(GLFW_KEY_LEFT_SHIFT)?3.f:1.f);
            frame.screenProbeStudyState.occluderOffset=std::clamp(frame.screenProbeStudyState.occluderOffset+
                (float(down(GLFW_KEY_RIGHT))-float(down(GLFW_KEY_LEFT)))*dt,-1.2f,2.5f);
        }
        auto camera=frame.camera.value();camera.position=position;
        camera.view=glm::lookAt(position,position+ShowcaseForward(yaw,pitch),glm::vec3(0,1,0));
        camera.projection=glm::perspective(glm::radians(55.f),frame.OutputAspect(),camera.nearPlane,camera.farPlane);frame.camera=camera;
        const bool sdfVolume=sdfViewer&&placement.trace.enabled&&placement.trace.sdf.enabled&&placement.trace.sdf.visualMode!=0;
        if(sdfVolume)selectable={};
        const bool click=!uiFrame.WantsMouse()&&focused&&!rmb&&glfwGetMouseButton(native,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
        if((options.screenProbeTracing||sdfViewer)&&click&&!previousClick&&!selectable.uniform.empty()) {
            int windowWidth=0,windowHeight=0;glfwGetWindowSize(native,&windowWidth,&windowHeight);
            glm::vec2 cursor(float(x/windowWidth*width),float((1-y/windowHeight)*height));float nearest=24*24;
            auto selected=std::numeric_limits<std::uint32_t>::max();
            const auto choose=[&](const auto& positions,unsigned offset){for(unsigned index=0;index<positions.size();++index){
                const auto& p=positions[index];if(p.w<.5f)continue;
                const auto clip=camera.projection*camera.view*glm::vec4(glm::vec3(p),1);if(clip.w<=0)continue;
                const auto pixel=(glm::vec2(clip)/clip.w*.5f+.5f)*glm::vec2(width,height);const auto delta=pixel-cursor;
                if(glm::dot(delta,delta)<nearest){nearest=glm::dot(delta,delta);selected=offset+index;}}};
            choose(selectable.uniform,0);if(placement.adaptive)choose(selectable.spawned,unsigned(selectable.uniform.size()));
            if(selected!=std::numeric_limits<std::uint32_t>::max())placement.trace.selectedProbe=selected;
        }
        previousClick=click;
        uiFrame.Finish(frame);
        frame.present=true;Check(lab.Render(frame),lab.LastError());
        if(now-lastSample>=1){
            frame.displayFps=float(1000/smoothMs);lastSample=now;
            // Volume inspection does not consume probe positions or rays.
            // Avoid synchronizing the GPU for unrelated diagnostic readbacks.
            if(!sdfVolume) {
            const auto probes=lab.ReadbackScreenProbePlacement();Check(!probes.uniform.empty(),lab.LastError());selectable=probes;
            frame.placementUniformCount=unsigned(std::count_if(probes.uniform.begin(),probes.uniform.end(),[](const auto& p){return p.w>0;}));
            frame.placementAdaptiveCount=unsigned(std::count_if(probes.spawned.begin(),probes.spawned.end(),[](const auto& p){return p.w>0;}));
            frame.placementMarkedCount=unsigned(std::count_if(probes.marked.begin(),probes.marked.end(),[](const auto& p){return p.w>1.5f;}));
            if(placement.trace.enabled){const auto trace=lab.ReadbackScreenProbeTrace(false);Check(!trace.radianceStatus.empty(),lab.LastError());
                frame.traceActiveCount=unsigned(std::count_if(trace.radianceStatus.begin(),trace.radianceStatus.end(),[](const auto& r){return r.w>.5f;}));
                frame.traceHitCount=unsigned(std::count_if(trace.radianceStatus.begin(),trace.radianceStatus.end(),[](const auto& r){return r.w>.5f&&r.w<1.5f;}));
                frame.sdfMeshHitCount=unsigned(std::count_if(trace.radianceStatus.begin(),trace.radianceStatus.end(),[](const auto& r){return r.w==9||r.w==11;}));
                frame.sdfGlobalHitCount=unsigned(std::count_if(trace.radianceStatus.begin(),trace.radianceStatus.end(),[](const auto& r){return r.w==10||r.w==12;}));
                frame.sdfUnreadableCount=unsigned(std::count_if(trace.radianceStatus.begin(),trace.radianceStatus.end(),[](const auto& r){return r.w==11||r.w==12;}));
                if(placement.trace.selectedProbe==std::numeric_limits<std::uint32_t>::max()) {
                    float nearest=std::numeric_limits<float>::infinity();const auto rays=trace.rayResolution*trace.rayResolution;
                    for(unsigned index=0;index<probes.uniform.size();++index){const auto& p=probes.uniform[index];if(p.w<.5f)continue;
                        unsigned hits=0;for(unsigned ray=0;ray<rays;++ray){const auto status=trace.radianceStatus[index*rays+ray].w;if(status==1||status==9||status==10||status==11||status==12)++hits;}
                        const auto clip=camera.projection*camera.view*glm::vec4(glm::vec3(p),1);if(clip.w<=0)continue;
                        const auto offset=glm::vec2(clip)/clip.w;const auto score=glm::dot(offset,offset)+(hits>=2?0.f:10.f);
                        if(score<nearest){nearest=score;placement.trace.selectedProbe=index;}}
                }
            }
            std::cout<<"Placement active uniform="<<frame.placementUniformCount<<" adaptive="<<frame.placementAdaptiveCount
                <<" uncovered="<<frame.placementMarkedCount<<" phase="<<probes.frameIndex<<'\n';
            }
        }
        if(!saved&&!options.screenshot.empty()&&(!sdfViewer||!lab.ProbeSdfQualityStatistics().pending)){
            frame.present=false;Check(lab.Render(frame),lab.LastError());Save(lab.Readback(),options.screenshot);saved=true;}
        std::ostringstream title;title<<(options.sdfClipmaps?"Global SDF Clipmaps | ":options.sdfBanquet?"Imported teapot SDF / model LOD | ":options.sdfProbeTracing?"Lumen Mesh / Global SDF | ":options.screenProbeTracing?"Lumen HZB Screen Traces | ":"UE5.7 Probe Placement | ")<<(placement.freeze?"FROZEN":"LIVE")<<" | "<<placement.baseSpacing
            <<"px | base "<<frame.placementUniformCount<<" adaptive "<<frame.placementAdaptiveCount<<" | Space freeze F2 adaptive F3 spacing F4 view";
        glfwSetWindowTitle(native,title.str().c_str());
        if(options.fpsLimit>0)pacer.WaitUntil(start+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0/options.fpsLimit)));
    }
    glfwSetInputMode(native,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
    if(glfwRawMouseMotionSupported())glfwSetInputMode(native,GLFW_RAW_MOUSE_MOTION,GLFW_FALSE);
    glfwSetScrollCallback(native,nullptr);glfwSetWindowUserPointer(native,nullptr);
}
