// Included in main.cpp's anonymous namespace; shares CLI/capture/frame pacing.
FrameSettings MakeShowcaseFrame(const Options& options) {
    FrameSettings frame;frame.showcase=true;frame.effects.sky=LoadSky(options);
    frame.effects.lumenGi.enabled=!options.realtimeGi;
    frame.effects.realtimeGi.enabled=options.realtimeGi;
    frame.effects.lumenGi.reflectionResolutionScale=options.lumenReflectionScale;
    frame.effects.lumenGi.reflectionSourceResolutionScale=options.lumenSourceScale;
    frame.effects.lumenGi.reflectionTraceMaxDimension=options.lumenReflectionDetail;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::TAA;
    frame.effects.temporal.msaaSamples=options.msaaSamples;
    frame.effects.reflection.enabled=true;frame.effects.reflection.plane={0,1,0,-.12f};
    frame.effects.reflection.resolutionScale=.5f;
    frame.effects.post.toneMap=Render::ToneMapMode::ACES;
    frame.effects.post.bloomStrength=.16f;frame.effects.post.bloomThreshold=1.2f;
    frame.effects.post.ssaoRadius=.35f;frame.exposure=1.05f;
    return frame;
}
void ShowcaseMotion(MaterialLabModule& lab,const Options& options) {
    auto frame=MakeShowcaseFrame(options);frame.showcaseHud=false;frame.present=false;
    SetShowcaseResolution(frame,options.studySizeSet?options.benchmarkWidth:960,
                          options.studySizeSet?options.benchmarkHeight:600,options.renderScale);
    frame.lightTime=3; // Fixed lights and water phase isolate camera-dependent transport.
    const float aspect=frame.OutputAspect();const auto output=frame.OutputExtent();
    std::cout<<"Showcase resolution: render "<<frame.width<<'x'<<frame.height<<", output "<<output.x<<'x'<<output.y<<'\n';
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;};
    const auto settle=[&](unsigned count){for(unsigned n=0;n<count;++n){render();if(!options.vulkan&&n%6==5)lab.Readback();}};
    const auto save=[&](const char* label){if(options.screenshot.empty())return;auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(lab.Readback(),path.string());};
    const auto sweep=[&](const char* label,glm::vec3 eye,glm::vec3 target,std::span<const glm::vec3> receivers) {
        const auto d=glm::normalize(target-eye);const float yaw=std::atan2(d.x,-d.z),pitch=std::asin(d.y);
        frame.camera=ShowcaseCamera(eye,yaw,pitch,aspect);frame.effects.cameraCut=true;settle(256);
        const auto presented=lab.Readback();
        Check(presented.width==output.x&&presented.height==output.y&&presented.rgba.size()==std::size_t(output.x)*output.y*4,
              "Render scale changed the showcase output extent");
        if(options.showcaseMeasureOnly) {
            frame.present=options.benchmarkPresent;
            // Warm the actual presentation path as well as the offscreen
            // lighting. Initial window/compositor work must not distort the
            // first short batch relative to later camera presets.
            if(frame.present)for(unsigned warm=0;warm<32;++warm)render();
            const auto timing=lab.MeasureFrames(frame,60,0);Check(timing.gpuMilliseconds>0,lab.LastError());
            std::cout<<"Showcase all-lights benchmark "<<label<<(frame.present?" with presentation":" offscreen")<<": GPU="<<timing.gpuMilliseconds<<" ms, wall="<<timing.elapsedMilliseconds<<" ms\n";
            frame.present=false;render();
            save(label);return;
        }
        const auto asset=lab.PreparedLumenGiScene();Check(bool(asset),"Motion regression requires a prepared GI scene");
        const auto& geometry=*asset->Geometry();const auto& triangles=geometry.Pixels();
        const auto visible=[&](glm::vec3 origin,glm::vec3 receiver) {
            const auto direction=glm::normalize(receiver-origin);const float limit=glm::length(receiver-origin)-.025f;
            for(unsigned i=0;i<geometry.TriangleCount();++i) {
                const unsigned at=geometry.NodeCount()*2+i*5;const auto a=glm::vec3(triangles[at]);
                const auto ab=glm::vec3(triangles[at+1])-a,ac=glm::vec3(triangles[at+2])-a;
                const auto h=glm::cross(direction,ac);const float determinant=glm::dot(ab,h);
                if(std::abs(determinant)<1e-8f)continue;const float inverse=1/determinant;
                const auto offset=origin-a;const float u=glm::dot(offset,h)*inverse;if(u<0||u>1)continue;
                const auto q=glm::cross(offset,ab);const float v=glm::dot(direction,q)*inverse;if(v<0||u+v>1)continue;
                const float distance=glm::dot(ac,q)*inverse;if(distance>.08f&&distance<limit)return false;
            }
            return true;
        };
        std::vector<double> sum(receivers.size()),square(receivers.size()),previous(receivers.size());double maxStep=0;
        // More than one tracing-grid pixel per frame: normal turning/walking,
        // deliberately outside the old micro-motion history special case.
        for(unsigned n=0;n<72;++n) {
            const float t=float(n)*6.283185307f/72;
            const auto p=eye+glm::vec3(.35f*std::sin(t),0,.10f*std::cos(t));
            const auto camera=ShowcaseCamera(p,yaw+.18f*std::sin(t),pitch+.045f*std::sin(t*.5f),aspect);
            frame.camera=camera;render();auto image=lab.ReadbackLumenGather();
            Check(image.size()==std::size_t(frame.width)*frame.height,"Missing GI diagnostic at the internal rendering extent");
            for(unsigned k=0;k<receivers.size();++k) {
                Check(visible(p,receivers[k]),"GI sweep receiver is occluded by an opaque prop");
                const auto q=camera.projection*camera.view*glm::vec4(receivers[k],1);
                const auto uv=(glm::vec2(q)/q.w*.5f+.5f)*glm::vec2(frame.width,frame.height)-.5f;
                const auto lo=glm::ivec2(glm::floor(uv));const auto f=glm::fract(uv);
                Check(q.w>0&&lo.x>=0&&lo.y>=0&&lo.x+1<int(frame.width)&&lo.y+1<int(frame.height),"GI sweep receiver outside view");
                glm::vec3 E(0);for(int y=0;y<2;++y)for(int x=0;x<2;++x)
                    E+=glm::vec3(image[std::size_t(lo.y+y)*frame.width+lo.x+x])*(x?f.x:1-f.x)*(y?f.y:1-f.y);
                const double value=glm::dot(E,glm::vec3(.2126f,.7152f,.0722f));
                Check(std::isfinite(value)&&value>.0001,"GI receiver lost incident illumination");
                sum[k]+=value;square[k]+=value*value;
                if(n)maxStep=std::max(maxStep,std::abs(value-previous[k])/std::max(value,.02));previous[k]=value;
            }
        }
        double maxCv=0;for(unsigned k=0;k<receivers.size();++k){const double mean=sum[k]/72;
            const double cv=std::sqrt(std::max(0.,square[k]/72-mean*mean))/std::max(mean,.02);maxCv=std::max(maxCv,cv);
            std::cout<<" "<<label<<" receiver"<<k<<" CV="<<cv*100<<"%";}
        std::cout<<"\nShowcase camera sweep "<<label<<": GI peak CV="<<maxCv*100<<"%, relative step="<<maxStep*100<<"%\n";
        save(label);
        Check(maxCv<.03&&maxStep<.06,"Ordinary camera movement changes stationary world illumination");
        const auto timing=lab.MeasureFrames(frame,60,0);Check(timing.gpuMilliseconds>0,lab.LastError());
        std::cout<<"Showcase all-lights benchmark "<<label<<": GPU="<<timing.gpuMilliseconds<<" ms, wall="<<timing.elapsedMilliseconds<<" ms\n";
    };
    const std::array<glm::vec3,4> walls{{{-2.8f,2.7f,-8.85f},{-.4f,3.3f,-8.85f},{2.7f,4.05f,-8.85f},{4.2f,3.8f,-8.85f}}};
    sweep("wall_turn",{0,2,-2.5f},{0,2.8f,-8.85f},walls);
    const std::array<glm::vec3,4> roof{{{-2,4.27f,-5.8f},{2,4.27f,-5.8f},{-2,4.27f,-7},{2,4.27f,-7}}};
    sweep("ceiling_turn",{0,2,-2.5f},{0,4.27f,-6.2f},roof);
    const std::array<glm::vec3,3> floor{{{-.6f,.05f,-4},{.4f,.05f,-4},{-1.3f,.05f,-4.7f}}};
    sweep("floor_grazing",{0,.65f,-2.4f},{0,.05f,-5.5f},floor);
    if(options.showcaseMeasureOnly)std::cout<<"Showcase all-lights performance batch completed.\n";
    else std::cout<<"Showcase ordinary camera movement passed: stationary GI receivers under walking and turning.\n";
}
void ShowcaseInteractive(MaterialLabModule& lab,ApplicationWindow::Window& window,const Options& options) {
    auto frame=MakeShowcaseFrame(options);
    float renderScale=options.renderScale;
    auto* native=window.GetNativeWindow();Input input;
    glfwSetWindowUserPointer(native,&input);
    glfwSetScrollCallback(native,[](GLFWwindow* w,double,double y){static_cast<Input*>(glfwGetWindowUserPointer(w))->scroll+=float(y);});
    glm::vec3 position;float yaw=0,pitch=0,speed=4;
    const auto pose=[&](const Render::PipelineCamera& camera) {
        position=camera.position;const auto forward=-glm::vec3(glm::inverse(camera.view)[2]);
        yaw=std::atan2(forward.x,-forward.z);pitch=std::asin(glm::clamp(forward.y,-1.f,1.f));
    };
    pose(ShowcaseCameraPreset(0,1));
    std::array<bool,GLFW_KEY_LAST+1> previous{};
    bool looking=false,clicked=false,saved=false;double lastX=0,lastY=0,lastTime=glfwGetTime();
    double smoothFrameMs=1000./90;FramePacer pacer;
    std::cout<<"Showcase: connected courtyard/gallery/pool. RMB look, WASD move, Q/E vertical, Shift fast.\n"
        "Click or Tab selects a prop. Arrows/PgUp/PgDn move, Z/X rotate, R resets.\n"
        "1/2/3 camera views; F focus. F5/F6/F7/F8/F9 sun/point/area/emission/sky. F1 help.\n"
        "P cycles render scale 100/75/62.5/50 percent; output size and AA stay unchanged.\n";
    while(!window.ShouldClose()) {
        const auto start=std::chrono::steady_clock::now();window.PollEvents();
        const double now=glfwGetTime(),elapsed=now-lastTime;lastTime=now;
        const float dt=float(std::clamp(elapsed,.0001,.1));
        frame.effects.deltaSeconds=float(std::clamp(elapsed,.0001,1.));frame.effects.cameraCut=elapsed>.25;
        smoothFrameMs=.9*smoothFrameMs+.1*std::clamp(elapsed,.0001,1.)*1000;
        const auto down=[&](int key){return glfwGetKey(native,key)==GLFW_PRESS;};
        const auto pressed=[&](int key){bool value=down(key),edge=value&&!previous[key];previous[key]=value;return edge;};
        if(pressed(GLFW_KEY_ESCAPE))break;
        int width=0,height=0;glfwGetFramebufferSize(native,&width,&height);
        if(width<=0||height<=0){glfwWaitEventsTimeout(.05);continue;}
        if(pressed(GLFW_KEY_P)) {
            constexpr std::array<float,4> scales{{1,.75f,.625f,.5f}};
            const auto at=std::find_if(scales.begin(),scales.end(),[&](float s){return std::abs(s-renderScale)<.0001f;});
            renderScale=at==scales.end()?1:scales[(unsigned(at-scales.begin())+1)%scales.size()];
            frame.effects.cameraCut=true;
        }
        SetShowcaseResolution(frame,options.renderSizeSet?options.benchmarkWidth:unsigned(width),
                              options.renderSizeSet?options.benchmarkHeight:unsigned(height),renderScale);
        const float aspect=frame.OutputAspect();
        for(unsigned i=0;i<3;++i)if(pressed(GLFW_KEY_1+int(i))){pose(ShowcaseCameraPreset(i,aspect));frame.effects.cameraCut=true;}
        auto& state=frame.showcaseState;
        if(pressed(GLFW_KEY_TAB))state.selected=(state.selected+(down(GLFW_KEY_LEFT_SHIFT)?int(ShowcaseMovableCount)-1:1))%int(ShowcaseMovableCount);
        if(pressed(GLFW_KEY_F1))state.help=!state.help;
        if(pressed(GLFW_KEY_F5))state.sunlight=!state.sunlight;
        if(pressed(GLFW_KEY_F6))state.pointLights=!state.pointLights;
        if(pressed(GLFW_KEY_F7))state.areaLights=!state.areaLights;
        if(pressed(GLFW_KEY_F8))state.emission=!state.emission;
        if(pressed(GLFW_KEY_F9))frame.effects.sky.enabled=!frame.effects.sky.enabled;
        if(pressed(GLFW_KEY_L)){frame.effects.realtimeGi.enabled=false;frame.effects.lumenGi.enabled=!frame.effects.lumenGi.enabled;frame.effects.cameraCut=true;}
        if(pressed(GLFW_KEY_B))frame.effects.post.bloomEnabled=!frame.effects.post.bloomEnabled;
        if(pressed(GLFW_KEY_O))frame.effects.post.ssaoEnabled=!frame.effects.post.ssaoEnabled;
        if(pressed(GLFW_KEY_N))frame.effects.temporal.antialiasing=Render::AntialiasingMode((unsigned(frame.effects.temporal.antialiasing)+1)%3);
        if(pressed(GLFW_KEY_M))frame.effects.temporal.motionBlurEnabled=!frame.effects.temporal.motionBlurEnabled;
        if(pressed(GLFW_KEY_SPACE))state.animateLights=!state.animateLights;
        if(pressed(GLFW_KEY_R)){state.offsets[state.selected]={};state.rotations[state.selected]=0;}
        if(pressed(GLFW_KEY_BACKSPACE)){state=ShowcaseSettings{};pose(ShowcaseCameraPreset(0,aspect));frame.effects.cameraCut=true;}
        double x=0,y=0;glfwGetCursorPos(native,&x,&y);
        const bool rmb=glfwGetWindowAttrib(native,GLFW_FOCUSED)&&glfwGetMouseButton(native,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS;
        if(rmb!=looking) {
            glfwSetInputMode(native,GLFW_CURSOR,rmb?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL);
            if(glfwRawMouseMotionSupported())glfwSetInputMode(native,GLFW_RAW_MOUSE_MOTION,rmb?GLFW_TRUE:GLFW_FALSE);
            glfwGetCursorPos(native,&x,&y);
        }
        if(rmb&&looking){yaw+=float(x-lastX)*.0025f;pitch=std::clamp(pitch-float(y-lastY)*.0025f,-1.5f,1.5f);}
        lastX=x;lastY=y;looking=rmb;
        speed=std::clamp(speed*std::pow(1.15f,input.scroll),.5f,15.f);input.scroll=0;
        if(glfwGetWindowAttrib(native,GLFW_FOCUSED)) {
            auto forward=ShowcaseForward(yaw,0);auto right=glm::normalize(glm::cross(forward,glm::vec3(0,1,0)));
            auto move=forward*(float(down(GLFW_KEY_W))-float(down(GLFW_KEY_S)))+right*(float(down(GLFW_KEY_D))-float(down(GLFW_KEY_A)))+
                glm::vec3(0,float(down(GLFW_KEY_E))-float(down(GLFW_KEY_Q)),0);
            if(glm::dot(move,move)>0)position+=glm::normalize(move)*speed*dt*(down(GLFW_KEY_LEFT_SHIFT)?3.f:1.f);
            position=glm::clamp(position,glm::vec3(-25,.18f,-25),glm::vec3(25,20,25));
            auto& offset=state.offsets[state.selected];
            offset+=glm::vec3(float(down(GLFW_KEY_RIGHT))-float(down(GLFW_KEY_LEFT)),
                float(down(GLFW_KEY_PAGE_UP))-float(down(GLFW_KEY_PAGE_DOWN)),float(down(GLFW_KEY_DOWN))-float(down(GLFW_KEY_UP)))*dt*2.f;
            offset=glm::clamp(offset,glm::vec3(-12,-.5f,-12),glm::vec3(12,8,12));
            state.rotations[state.selected]+=(float(down(GLFW_KEY_X))-float(down(GLFW_KEY_Z)))*dt;
        }
        if(pressed(GLFW_KEY_F)) {
            const auto target=ShowcaseMovable(state.selected).position+state.offsets[state.selected];
            position=target-ShowcaseForward(yaw,pitch)*4.f;frame.effects.cameraCut=true;
        }
        const auto camera=ShowcaseCamera(position,yaw,pitch,aspect);frame.camera=camera;
        const bool lmb=glfwGetMouseButton(native,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
        if(lmb&&!clicked&&!rmb) {
            int logicalW=1,logicalH=1;glfwGetWindowSize(native,&logicalW,&logicalH);
            const glm::vec2 ndc{float(x/logicalW)*2-1,1-float(y/logicalH)*2};
            auto farPoint=glm::inverse(camera.projection*camera.view)*glm::vec4(ndc,1,1);
            auto ray=glm::normalize(glm::vec3(farPoint)/farPoint.w-position);
            const auto selected=PickShowcaseObject(position,ray,state);if(selected>=0)state.selected=selected;
        }
        clicked=lmb;frame.lightTime+=dt;
        if(!saved&&!options.screenshot.empty()) {
            frame.present=false;for(int i=0;i<48;++i)Check(lab.Render(frame),lab.LastError());
            Save(Capture(lab,frame),options.screenshot);saved=true;
        }
        frame.present=true;Check(lab.Render(frame),lab.LastError());
        std::ostringstream title;title<<"Lighting Showcase | "<<ShowcaseMovable(state.selected).name<<" | GI "
            <<(lab.ShowcaseGiUpdating()?"updating":"ready")<<" | "<<std::fixed<<std::setprecision(1)<<1000/smoothFrameMs
            <<" FPS | "<<frame.width<<'x'<<frame.height<<" -> "<<frame.OutputExtent().x<<'x'<<frame.OutputExtent().y<<" | P scale | F1 help";
        glfwSetWindowTitle(native,title.str().c_str());
        if(options.fpsLimit>0)pacer.WaitUntil(start+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1/options.fpsLimit)));
    }
    glfwSetInputMode(native,GLFW_CURSOR,GLFW_CURSOR_NORMAL);glfwSetScrollCallback(native,nullptr);glfwSetWindowUserPointer(native,nullptr);
}
void ShowcaseStability(MaterialLabModule& lab,const Options& options) {
    auto frame=MakeShowcaseFrame(options);frame.showcaseHud=false;frame.present=false;
    SetShowcaseResolution(frame,options.studySizeSet?options.benchmarkWidth:960,
                          options.studySizeSet?options.benchmarkHeight:600,options.renderScale);
    const auto aim=[&](glm::vec3 eye,glm::vec3 target) {
        auto d=glm::normalize(target-eye);
        return ShowcaseCamera(eye,std::atan2(d.x,-d.z),std::asin(d.y),frame.OutputAspect());
    };
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;};
    const auto measure=[&](const char* label,Render::PipelineCamera camera,std::span<const glm::vec3> points,bool moving,float time,bool flowing=true) {
        frame.camera=camera;frame.effects.cameraCut=true;frame.lightTime=time;
        for(int n=0;n<256;++n){render();if(!options.vulkan&&n%6==5)lab.Readback();}
        std::vector<double> sum(points.size()),square(points.size()),previous(points.size());double step=0,pixelStep=0;
        std::vector<std::array<double,49>> last(points.size());FramePixels pixels;
        for(int n=0;n<48;++n) {
            auto c=camera;auto inv=glm::inverse(camera.view);
            if(moving){c.position.x+=.008f*std::sin(n*.392699f);c.view=glm::lookAt(c.position,c.position-glm::vec3(inv[2]),glm::vec3(0,1,0));}
            frame.camera=c;frame.lightTime=time+(flowing?n/60.f:0);render();pixels=lab.Readback();Check(!pixels.rgba.empty(),lab.LastError());
            for(unsigned k=0;k<points.size();++k) {
                auto q=c.projection*c.view*glm::vec4(points[k],1);auto at=glm::ivec2((glm::vec2(q)/q.w*.5f+.5f)*glm::vec2(pixels.width,pixels.height));
                Check(at.x>4&&at.y>4&&at.x<int(pixels.width)-4&&at.y<int(pixels.height)-4,"Showcase patch outside view");
                double luma=0,delta=0;unsigned j=0;
                for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x,++j) {
                    auto p=(std::size_t(at.y+y)*pixels.width+at.x+x)*4;
                    double value=.2126*pixels.rgba[p]+.7152*pixels.rgba[p+1]+.0722*pixels.rgba[p+2];
                    luma+=value/49;delta+=std::abs(value-last[k][j])/49;last[k][j]=value;
                }
                Check(luma>2,"Showcase lighting patch became black");
                sum[k]+=luma;square[k]+=luma*luma;if(n){step=std::max(step,std::abs(luma-previous[k]));pixelStep=std::max(pixelStep,delta);}previous[k]=luma;
            }
        }
        double sigma=0;for(unsigned k=0;k<points.size();++k){auto v=std::sqrt(std::max(0.,square[k]/48-std::pow(sum[k]/48,2)));sigma=std::max(sigma,v);std::cout<<" patch"<<k<<"="<<v;}
        std::cout<<'\n';
        std::cout<<"Showcase stability "<<label<<": sigma="<<sigma<<", patch/pixel step="<<step<<'/'<<pixelStep<<" /255\n";
        if(!options.screenshot.empty()){auto path=std::filesystem::path(options.screenshot);path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(pixels,path.string());}
        return std::array{sigma,step,pixelStep};
    };
    const std::array<glm::vec3,6> walls{{{-3,2.7f,-8.84f},{0,2.7f,-8.84f},{3.8f,3.3f,-8.84f},
        {-4.84f,2.5f,-5.5f},{-.5f,.052f,-5},{3,.052f,-5.3f}}};
    if(options.showcaseWaterDiagnostic) {
        const std::array<glm::vec3,4> pool{{{6,.12f,.1f},{8,.12f,.1f},{6,.12f,2},{8,.12f,2}}};
        const auto water=aim({7,4,5.5f},{7,.12f,1});
        frame.effects.post.ssaoEnabled=frame.effects.post.bloomEnabled=false;
        measure("water_no_post",water,pool,true,1800,false);
        frame.effects.lumenGi.enabled=false;measure("water_direct",water,pool,true,1800,false);
        frame.showcaseState.sunlight=frame.showcaseState.pointLights=frame.showcaseState.areaLights=frame.showcaseState.emission=false;
        measure("water_sky",water,pool,true,1800,false);
        return;
    }
    const auto interior=aim({0,2,0},{0,1.8f,-6});
    const auto a=measure("room_static",interior,walls,false,2),b=measure("room_motion",interior,walls,true,3);
    frame.effects.lumenGi.reflections=false;
    measure("room_diffuse_motion",interior,walls,true,3);
    frame.effects.lumenGi.reflections=true;
    const std::array<glm::vec3,4> pool{{{6,.12f,.1f},{8,.12f,.1f},{6,.12f,2},{8,.12f,2}}};
    const auto water=aim({7,4,5.5f},{7,.12f,1});
    const auto c=measure("water_motion",water,pool,true,2),d=measure("water_long_run",water,pool,true,1799.7f);
    frame.effects.reflection.enabled=false;measure("water_no_planar",water,pool,true,1800);frame.effects.reflection.enabled=true;
    measure("water_frozen_motion",water,pool,true,1800,false);
    measure("water_fixed_camera",water,pool,false,1800,false);
    frame.showcaseState.sunlight=false;measure("water_no_sun",water,pool,true,1800,false);frame.showcaseState.sunlight=true;
    // Every submitted pose must be in the tracing asset immediately, including
    // repeated moves before any background rebuild would have completed.
    frame.camera=interior;
    const auto indirectMean=[&] {
        auto cache=lab.ReadbackLumenSurfaceCache(),direct=lab.ReadbackLumenSurfaceCache(true);double sum=0;
        for(unsigned i=0;i<lab.PreparedLumenGiScene()->SurfelCount();++i)sum+=glm::length(glm::max(glm::vec3(cache[i]-direct[i]),glm::vec3(0)));
        return sum/std::max(1u,lab.PreparedLumenGiScene()->SurfelCount());
    };
    const double beforeIndirect=indirectMean();double minimumIndirect=beforeIndirect;
    auto oldTopology=lab.PreparedLumenGiScene()->Geometry()->TopologyKey();
    for(int n=0;n<12;++n) {
        frame.showcaseState.offsets[0]={n*.13f,0,0};render();
        const auto asset=lab.PreparedLumenGiScene();const auto& g=*asset->Geometry();const auto& data=g.Pixels();
        glm::vec3 lo(1000),hi(-1000);unsigned found=0;
        for(unsigned i=0;i<g.TriangleCount();++i){auto at=g.NodeCount()*2+i*5;
            if(std::abs(data[at].w-.88f)>.001f||std::abs(data[at+1].w-.59f)>.001f||std::abs(data[at+2].w-.19f)>.001f)continue;
            for(int j=0;j<3;++j){lo=glm::min(lo,glm::vec3(data[at+j]));hi=glm::max(hi,glm::vec3(data[at+j]));}++found;
        }
        Check(found&&glm::length((lo+hi)*.5f-ShowcaseMovable(0).position-frame.showcaseState.offsets[0])<.001f,"Moving prop GI geometry lags its rendered pose");
        Check(asset->Geometry()->TopologyKey()==oldTopology,"Moving prop discarded Surface Cache topology");
        if(n)Check(lab.PipelineStatistics().realtimeGiUpdatedProbes==60,"Moved geometry did not update probe visibility in the same frame");
        minimumIndirect=std::min(minimumIndirect,indirectMean());
    }
    std::cout<<"Moving prop retained indirect radiance="<<minimumIndirect/beforeIndirect*100<<"%, tracing geometry latency=0 frames\n";
    Check(beforeIndirect>0&&minimumIndirect>beforeIndirect*.9,"Moving prop cleared converged indirect illumination");
    Check(a[0]<.25&&a[1]<.5,"Static showcase illumination flickers");
    Check(b[1]<1&&b[2]<2,"Moving camera causes showcase lighting discontinuities");
    Check(c[1]<1.5&&d[1]<1.5&&d[2]<3,"Water flow produces discontinuities or grows unstable over time");
    std::cout<<"Showcase stability passed: local temporal patches, long-running water and same-frame moved geometry.\n";
}
void ShowcaseQuality(MaterialLabModule& lab,const Options& options) {
    auto frame=MakeShowcaseFrame(options);frame.present=false;
    SetShowcaseResolution(frame,options.studySizeSet?options.benchmarkWidth:960,
                          options.studySizeSet?options.benchmarkHeight:600,options.renderScale);
    const auto settle=[&](unsigned count) {
        for(unsigned i=0;i<count;++i) {
            Check(lab.Render(frame),lab.LastError());
            // GL submission completion does not fence GPU execution. Drain
            // short software-tracing batches instead of queuing 72 heavy frames
            // ahead of the bounded diagnostic readback.
            if(!options.vulkan&&i%6==5)Check(!lab.Readback().rgba.empty(),lab.LastError());
        }
    };
    settle(1);frame.showcaseHud=false; // Verify HUD once, then measure only scene pixels.
    const auto saveView=[&](unsigned view,const FramePixels& pixels) {
        if(options.screenshot.empty())return;auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+std::array<const char*,3>{"_courtyard","_interior","_pool"}[view]+path.extension().string());Save(pixels,path.string());
    };
    for(unsigned view=0;view<3;++view) {
        frame.camera=ShowcaseCameraPreset(view,frame.OutputAspect());frame.effects.cameraCut=true;settle(1);
        frame.effects.cameraCut=false;settle(72);auto pixels=Capture(lab,frame);saveView(view,pixels);
        std::size_t lit=0;for(std::size_t i=0;i<pixels.rgba.size();i+=4)if(pixels.rgba[i]>24||pixels.rgba[i+1]>24||pixels.rgba[i+2]>24)++lit;
        Check(lit>pixels.width*pixels.height/5,"Showcase view is blank");
    }
    // Changing internal resolution retires view histories, while the solved
    // world lighting and scene asset retain their identity and update budget.
    {
        const auto output=frame.OutputExtent();const auto scene=lab.PreparedLumenGiScene();
        const auto steady=lab.PipelineStatistics(); // Includes ConfigureShowcaseLighting's effective budget.
        SetShowcaseResolution(frame,output.x,output.y,1);settle(2);
        for(const float scale:{.625f,1.f}) {
            SetShowcaseResolution(frame,output.x,output.y,scale);frame.effects.cameraCut=false;settle(1);
            auto stats=lab.PipelineStatistics();
            Check(!stats.historyUsed,"Render scale retained TAA history from a different extent");
            if(frame.effects.lumenGi.enabled) {
                Check(lab.PreparedLumenGiScene()==scene,"Render scale rebuilt showcase world geometry");
                Check(stats.lumenSurfaceTexels==steady.lumenSurfaceTexels&&stats.lumenUpdatedSurfels==steady.lumenUpdatedSurfels,
                      "Render scale reset the solved world lighting cache");
            }
            settle(1);stats=lab.PipelineStatistics();
            Check(stats.historyUsed,"TAA did not resume at the new internal extent");
            if(frame.effects.lumenGi.enabled) {
                Check(stats.lumenHistoryUsed,"GI view history did not resume at the new internal extent");
                Check(lab.ReadbackLumenGather().size()==std::size_t(frame.width)*frame.height,
                      "GI diagnostic retained the old internal extent");
            }
            frame.present=true;settle(2); // Exercise scaled backbuffer -> native swapchain presentation.
            frame.present=false;settle(1);
            const auto pixels=lab.Readback();
            Check(pixels.width==output.x&&pixels.height==output.y,"Render scale changed the presentation extent");
        }
        SetShowcaseResolution(frame,output.x,output.y,options.renderScale);settle(2);
    }
    frame.camera=ShowcaseCameraPreset(1,frame.OutputAspect());settle(48);
    auto interior=Capture(lab,frame);frame.showcaseState.pointLights=false;frame.showcaseState.areaLights=false;settle(48);
    Check(Difference(interior,Capture(lab,frame))>.15,"Showcase local lights have no contribution");
    frame.showcaseState.pointLights=frame.showcaseState.areaLights=true;
    frame.camera=ShowcaseCameraPreset(0,frame.OutputAspect());settle(32);
    const auto before=Capture(lab,frame);const auto oldGeometry=lab.PreparedLumenGiScene();
    frame.showcaseState.offsets[0]={1.8f,.2f,.3f};frame.showcaseState.rotations[1]=.7f;settle(1);
    Check(Difference(before,Capture(lab,frame))>.05,"Moving a showcase prop has no visible effect");
    // Consecutive changes must reach tracing immediately, preserving the
    // same cache layout rather than publishing an older extraction result.
    frame.showcaseState.offsets[0]={2.1f,.3f,.6f};settle(1);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    Check(!lab.ShowcaseGiUpdating(),"Showcase transform did not reach GI in the submitted frame");
    if(frame.effects.lumenGi.enabled) {
        const auto asset=lab.PreparedLumenGiScene();Check(asset!=oldGeometry,"Moved props retained stale GI geometry");
        const auto& geometry=*asset->Geometry();const auto& data=geometry.Pixels();
        glm::vec3 lo(1000),hi(-1000);unsigned found=0;
        for(unsigned i=0;i<geometry.TriangleCount();++i) {
            const auto base=geometry.NodeCount()*2+i*5;
            if(std::abs(data[base].w-.88f)>.001f||std::abs(data[base+1].w-.59f)>.001f||std::abs(data[base+2].w-.19f)>.001f)continue;
            for(int j=0;j<3;++j){lo=glm::min(lo,glm::vec3(data[base+j]));hi=glm::max(hi,glm::vec3(data[base+j]));}++found;
        }
        const auto center=ShowcaseMovable(0).position+frame.showcaseState.offsets[0];
        Check(found>0&&glm::length((lo+hi)*.5f-center)<.001f,"GI refit retained a stale prop destination");
    }
    auto geometry=lab.PreparedLumenGiScene();frame.camera=ShowcaseCameraPreset(2,frame.OutputAspect());settle(3);
    Check(!frame.effects.lumenGi.enabled||lab.PreparedLumenGiScene()==geometry,"Camera movement rebuilt showcase geometry");
    frame.showcaseState.emission=false;settle(1);
    while(lab.ShowcaseGiUpdating()){Check(std::chrono::steady_clock::now()<deadline,"Emission update did not finish");settle(1);}
    auto stats=lab.PipelineStatistics();Check(stats.transparentPasses>0&&stats.shadowPasses==4&&stats.skyPasses>0,"Showcase is missing major lighting passes");
    if(frame.effects.lumenGi.enabled)Check(stats.lumenGiPasses>0,"Showcase omitted GI");
    // A visible camera override must change pixels without extracting geometry.
    const auto original=Capture(lab,frame);frame.camera=ShowcaseCameraPreset(0,frame.OutputAspect());
    settle(8);Check(Difference(original,Capture(lab,frame))>1,"Free camera override was ignored");
    const auto timing=lab.MeasureFrames(frame,24,1.f/60);Check(lab.LastError().empty(),lab.LastError());
    std::cout<<"Showcase steady frames: GPU="<<timing.gpuMilliseconds<<" ms, wall="<<timing.elapsedMilliseconds<<" ms\n";
    std::cout<<"Showcase passed: three views, local lights, moving props, same-frame GI, camera reuse, emission and transparency.\n";
}
