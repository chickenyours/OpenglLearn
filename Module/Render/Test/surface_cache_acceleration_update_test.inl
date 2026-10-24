// Fixed-topology BUILD/retained-BUILD/UPDATE equivalence and lifetime contract.
// Included by vulkan_backend_gpu_test; uses its test-only RHI helpers.
void SurfaceCacheAccelerationUpdateContract(RHIDevice& device) {
  bool supported=false;
  OnThread(device,[&]{supported=device.BackendDiagnostics()->HardwareRayTracingAvailable();});
  if(!supported){std::cout<<"Surface cache acceleration update skipped: hardware unavailable\n";return;}
  ResourceBuilder build(device);const auto mesh=build.FullscreenMesh();
  struct Case {
    std::string name;std::vector<glm::vec4> pixels;
    glm::vec3 origin{0},direction{0,0,1};int nodes=3,triangles=2;
  };
  using Triangle=std::array<glm::vec3,3>;
  const auto plane=[](float x,float z){return Triangle{glm::vec3(x-1,-1,z),glm::vec3(x+1,-1,z),glm::vec3(x,1,z)};};
  const auto pair=[&](const std::string& name,const Triangle& a,const Triangle& b) {
    Case c;c.name=name;c.pixels.assign(512,glm::vec4(0));
    const auto low=[](const Triangle& t){return glm::min(t[0],glm::min(t[1],t[2]));};
    const auto high=[](const Triangle& t){return glm::max(t[0],glm::max(t[1],t[2]));};
    c.pixels[0]=glm::vec4(glm::min(low(a),low(b)),1);c.pixels[1]=glm::vec4(glm::max(high(a),high(b)),2);
    c.pixels[2]=glm::vec4(low(a),0);c.pixels[3]=glm::vec4(high(a),-1);
    c.pixels[4]=glm::vec4(low(b),1);c.pixels[5]=glm::vec4(high(b),-1);
    for(unsigned v=0;v<3;++v){c.pixels[6+v]=glm::vec4(a[v],0);c.pixels[11+v]=glm::vec4(b[v],0);}
    return c;
  };
  std::vector<Case> cases;
  cases.push_back(pair("initial",plane(0,2),plane(0,6)));
  cases.push_back(pair("translated",plane(0,3),plane(0,6)));
  cases.push_back(pair("nearest_changes_id",plane(0,4),plane(0,1.5f)));
  auto opposed=plane(0,2);std::swap(opposed[1],opposed[2]);
  cases.push_back(pair("opposed_coplanar",plane(0,2),opposed));
  cases.push_back(pair("near_leaf_miss",plane(4,2),plane(0,6)));
  // A near grazing triangle may be missed by hardware while the far triangle
  // is hit. The production closest-hit certificate must still return the
  // original packed-geometry result after many in-place UPDATE operations.
  for(unsigned rotation=0;rotation<32;++rotation) {
    const float angle=.173f*float(rotation+1);
    const glm::vec3 n=glm::normalize(glm::vec3(std::sin(angle),std::cos(angle*.7f),.25f+std::sin(angle*.37f)));
    const glm::vec3 tangent=glm::normalize(glm::cross(n,glm::vec3(.31f,.53f,.79f)));
    const glm::vec3 bitangent=glm::normalize(glm::cross(n,tangent));
    const glm::vec3 center(float(int(rotation%7)-3)*13.25f,float(int(rotation%5)-2)*9.125f,float(int(rotation%3)-1)*17.5f);
    const Triangle nearTriangle{center-tangent*.7f-bitangent*.45f,center+tangent*1.1f-bitangent*.2f,center-tangent*.2f+bitangent*.95f};
    const auto direction=glm::normalize(n*.0001f+tangent);
    const auto origin=nearTriangle[0]+(nearTriangle[1]-nearTriangle[0])*.25f+(nearTriangle[2]-nearTriangle[0])*1e-5f-direction*2.f;
    const auto farCenter=origin+direction*6.f;
    const auto farTangent=glm::normalize(glm::cross(direction,glm::vec3(.31f,.53f,.79f)));
    const auto farBitangent=glm::normalize(glm::cross(direction,farTangent));
    const Triangle farTriangle{farCenter-farTangent*4.f-farBitangent*4.f,farCenter+farTangent*4.f-farBitangent*4.f,farCenter+farBitangent*4.f};
    auto c=pair("masked_grazing_"+std::to_string(rotation),nearTriangle,farTriangle);c.origin=origin;c.direction=direction;cases.push_back(std::move(c));
  }
  const auto texture=[&](bool tracked) {
    CreateRHITextureSpec desc;desc.width=512;desc.height=1;desc.mipmaps=false;
    desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;
    desc.filterMode=RHIFilterMode::Nearest;desc.data=cases[0].pixels.data();
    if(tracked)return build.Create<RHITextureSpec>([&](auto callback){device.async_CreateTexture(desc,callback);});
    RenderResourceHandle<RHITextureSpec> result;
    bool done=false;device.async_CreateTexture(desc,[&](auto handle){result=handle;done=true;});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Acceleration temporary texture callback timeout");
      if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
    Require(result.IsValid(),"Acceleration update temporary texture creation");return result;
  };
  const auto geometry=texture(true);
  const auto input=build.Uniform(3*sizeof(glm::vec4)),cache=build.Uniform(sizeof(SurfaceCacheGiConstants));
  const auto result=build.Texture(4,1,RHITextureFormat::RGBA32F),normal=build.Texture(4,1,RHITextureFormat::RGBA32F);
  const auto target=build.TargetColors({result,normal,{},{}});
  const std::string vertex=R"(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);})";
  const std::string fragment=R"(
layout(std140,binding=0) uniform AccelerationCase {vec4 acOrigin,acDirection,acInterval;};
layout(location=0) out vec4 result;layout(location=1) out vec4 normalData;
void main(){float maximum=acInterval.y;int triangle;vec3 normal;bool exhausted;
#if ACCELERATION_NATIVE==2
  bool found=SCNativeIntersect(acOrigin.xyz,acDirection.xyz,maximum,normal,triangle,exhausted,false);
#elif ACCELERATION_NATIVE==1
  bool found=SCNativeOriginIntersect(acOrigin.xyz,acDirection.xyz,maximum,normal,triangle,exhausted,false);
#else
  bool found=SCIntersect(acOrigin.xyz,acDirection.xyz,maximum,normal,triangle,exhausted,false);
#endif
  result=vec4(found?1:0,maximum,float(triangle),exhausted?1:0);normalData=vec4(normal,0);
})";
  std::array<RenderResourceHandle<PipelineSpec>,3> programs;
  for(unsigned native=0;native<3;++native)programs[native]=build.FullscreenPipeline(vertex,
      std::string("#version 450 core\n#define ACCELERATION_NATIVE ")+std::to_string(native)+"\n"+
      SurfaceCacheDataGLSL()+SoftwareBvhVisibilityGLSL(9)+(native?NativeSurfaceCacheIntersectionGLSL():std::string{})+fragment);
  const auto wait=[&](const bool& done) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Acceleration update callback timeout");
      if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
  };
  const auto upload=[&](auto texture,const Case& c,bool deferred) {
    UpdateRHITextureDesc desc;desc.width=512;desc.height=1;desc.format=RHITextureFormat::RGBA32F;
    desc.data=c.pixels.data();desc.byteSize=c.pixels.size()*sizeof(glm::vec4);desc.deferUntilFrame=deferred;
    bool done=false,accepted=false;device.async_UpdateTexture(texture,desc,[&](bool ok){accepted=ok;done=true;});wait(done);
    Require(accepted,"Acceleration update geometry upload rejected");
  };
  struct Capture {std::array<std::vector<float>,2> pixels;HostFrameStatistics statistics;};
  uint64_t frame=1900;size_t compared=0,refreshes=0,updates=0,fullBuilds=0,legacyModeDifferences=0;
  const auto draw=[&](auto texture,const Case& c,unsigned mode) {
    RHICommand::BeginFrame begin{frame++,64,32};begin.reuseSurfaceCacheAccelerationStorage=mode==1;
    begin.updateSurfaceCacheAccelerationStructures=mode==2;
    auto e=device.BeginFrame(begin);
    Require(e.SetRenderTarget({target,4,1,RHICommand::ClearColor,glm::vec4(0)})&&e.BindMesh(mesh)&&
        e.BindUniformBuffer(input,0)&&e.BindUniformBuffer(cache,SurfaceCacheGiBinding)&&e.BindTexture(texture,9),
        "Acceleration update frame binding");
    const std::array<glm::vec4,3> values{glm::vec4(c.origin,0),glm::vec4(c.direction,0),glm::vec4(0,8,0,0)};
    Require(e.UpdateUniformBuffer(input,values),"Acceleration update query uniform");
    SurfaceCacheGiConstants sc;sc.geometry={c.nodes*2,512,c.nodes,c.triangles};
    Require(e.UpdateUniformBuffer(cache,sc),"Acceleration update geometry uniform");
    for(int x=0;x<4;++x)Require(e.BindPipeline(programs[x==0?0:x==3?2:1])&&e.SetViewport({x,0,1,1})&&e.DrawIndexed({3}),
        "Acceleration update query draw");
    Require(e.End(false),"Acceleration update frame end");bool done=false;
    device.async_SubmitFrameCommands(e.GetCommandBuffer(),[&]{done=true;});wait(done);
    Capture capture;OnThread(device,[&]{capture.pixels[0]=device.BackendDiagnostics()->ReadTexture(result);
      capture.pixels[1]=device.BackendDiagnostics()->ReadTexture(normal);capture.statistics=device.BackendDiagnostics()->LastHostFrameStatistics();
      Require(!device.BackendDiagnostics()->ValidationErrorCount(),"Acceleration update Vulkan validation");});
    for(const auto& output:capture.pixels) {
      Require(output.size()==16,"Acceleration update readback extent");
      for(size_t channel=0;channel<4;++channel)for(size_t query=1;query<3;++query) {
        if(std::bit_cast<uint32_t>(output[channel])!=std::bit_cast<uint32_t>(output[query*4+channel]))
          std::cout<<"Acceleration update mismatch case="<<c.name<<" mode="<<mode<<" channel="<<channel
              <<" expected="<<output[channel]<<" actual="<<output[query*4+channel]<<std::endl;
        Require(std::bit_cast<uint32_t>(output[channel])==std::bit_cast<uint32_t>(output[query*4+channel]),
            "Acceleration UPDATE changed closest distance/normal/id/flags");++compared;
      }
    }
    refreshes+=capture.statistics.accelerationBuilds;updates+=capture.statistics.accelerationUpdates;fullBuilds+=capture.statistics.accelerationFullBuilds;
    return capture;
  };
  const auto counters=[](const Capture& c,uint64_t refresh,uint64_t full,uint64_t update,uint64_t reuse,uint64_t allocate,uint64_t handles) {
    const auto& s=c.statistics;
    Require(s.accelerationBuilds==refresh&&s.accelerationFullBuilds==full&&s.accelerationUpdates==update&&
        s.accelerationStorageReuses==reuse&&s.accelerationBufferAllocations==allocate&&s.accelerationStructureCreations==handles,
        "Acceleration UPDATE build/reuse/allocation work contract");
  };
  std::vector<Capture> baseline;
  for(unsigned mode=0;mode<3;++mode)for(size_t i=0;i<cases.size();++i) {
    upload(geometry,cases[i],(i&1)==0);const auto capture=draw(geometry,cases[i],mode);
    const bool reuse=mode!=0&&i!=0;
    counters(capture,1,mode==2&&reuse?0:1,mode==2&&reuse?1:0,reuse?1:0,reuse?0:6,reuse?0:2);
    if(!mode) {
      if(i<5) {
        const std::array<float,5> distances{2,3,1.5f,2,6};
        const std::array<int,5> triangles{0,0,1,1,1};const auto& p=capture.pixels[0];const auto& n=capture.pixels[1];
        Require(p[0]==1&&p[1]==distances[i]&&p[2]==float(triangles[i])&&p[3]==0&&
            n[0]==0&&n[1]==0&&n[2]==(i==3?-1.f:1.f),"Acceleration simple geometry independent CPU expectation");
      }
      baseline.push_back(capture);
    }
    else for(size_t part=0;part<2;++part) {
      Require(capture.pixels[part].size()==baseline[i].pixels[part].size()&&
          std::memcmp(capture.pixels[part].data(),baseline[i].pixels[part].data(),12*sizeof(float))==0,
          "Acceleration UPDATE or retained BUILD changed certified original full BUILD output");compared+=12;
      for(size_t channel=12;channel<16;++channel)
        if(std::bit_cast<uint32_t>(capture.pixels[part][channel])!=std::bit_cast<uint32_t>(baseline[i].pixels[part][channel])) {
          ++legacyModeDifferences;
          std::cout<<"Acceleration legacy native diagnostic case="<<cases[i].name<<" mode="<<mode<<" part="<<part
              <<" channel="<<channel-12<<" full_build="<<baseline[i].pixels[part][channel]<<" actual="<<capture.pixels[part][channel]<<'\n';
        }
    }
  }
  counters(draw(geometry,cases.back(),2),0,0,0,0,0,0); // Cached consumers must not rebuild twice or on an unchanged frame.
  upload(geometry,cases[0],true);counters(draw(geometry,cases[0],1),1,1,0,0,6,2); // UPDATE -> retained BUILD changes flags.
  upload(geometry,cases[1],true);counters(draw(geometry,cases[1],1),1,1,0,1,0,0);
  upload(geometry,cases[0],true);counters(draw(geometry,cases[0],0),1,1,0,0,6,2); // Original fresh BUILD restored.
  upload(geometry,cases[0],true);counters(draw(geometry,cases[0],2),1,1,0,0,6,2);
  auto single=cases[0];single.name="primitive_count_change";single.nodes=1;single.triangles=1;
  single.pixels.assign(512,glm::vec4(0));single.pixels[0]={-1,-1,3,0};single.pixels[1]={1,1,3,-1};
  const auto one=plane(0,3);for(unsigned v=0;v<3;++v)single.pixels[2+v]=glm::vec4(one[v],0);
  upload(geometry,single,true);counters(draw(geometry,single,2),1,1,0,0,6,2);
  single.pixels[2].z=single.pixels[3].z=single.pixels[4].z=3.5f;single.pixels[0].z=single.pixels[1].z=3.5f;
  upload(geometry,single,true);counters(draw(geometry,single,2),1,0,1,1,0,0);
  upload(geometry,cases[0],true);counters(draw(geometry,cases[0],2),1,1,0,0,6,2);
  // Inactive/NaN primitives cannot become active in UPDATE. Keep this leaf off
  // the query ray, verify the finite far hit, and require ordinary BUILD when
  // entering/leaving the invalid geometry rather than refitting its activity.
  auto inactive=pair("inactive_primitive_guard",plane(50,2),plane(0,6));
  inactive.pixels[6].x=std::numeric_limits<float>::quiet_NaN();
  upload(geometry,inactive,true);counters(draw(geometry,inactive,2),1,1,0,0,6,2);
  upload(geometry,inactive,true);counters(draw(geometry,inactive,2),1,1,0,1,0,0);
  upload(geometry,cases[0],true);counters(draw(geometry,cases[0],2),1,1,0,0,6,2);
  // Delete a dirty AS while its accepted deferred copy is still queued. The
  // copy must be cancelled, handles retired once, and no stale AS resurrected.
  const auto temporary=texture(false);counters(draw(temporary,cases[0],2),1,1,0,0,6,2);
  upload(temporary,cases[1],true);bool deleted=false;device.async_DeleteTexture(temporary,[&]{deleted=true;});wait(deleted);
  RHICommand::BeginFrame empty{frame++,64,32};empty.updateSurfaceCacheAccelerationStructures=true;
  auto e=device.BeginFrame(empty);e.End(false);bool done=false;device.async_SubmitFrameCommands(e.GetCommandBuffer(),[&]{done=true;});wait(done);
  OnThread(device,[&]{const auto uploadStats=device.BackendDiagnostics()->LastTextureUploadStatistics();
    Require(device.BackendDiagnostics()->ReadTexture(temporary).empty()&&uploadStats.deferredRegions==0,
        "Acceleration deferred geometry deletion was not cancelled");
    Require(!device.BackendDiagnostics()->ValidationErrorCount(),"Acceleration deletion lifecycle validation");});
  std::cout<<"Surface cache acceleration UPDATE contract passed: cases="<<cases.size()<<" modes=3 compared_fp32_channels="<<compared
      <<" refreshes="<<refreshes<<" full_builds="<<fullBuilds<<" updates="<<updates
      <<" positions,opposed_ties,masked_grazing=32,primitive_count,mode_toggles,inactive_guard,no_duplicate_AS,cache,deferred_delete"
      <<" legacy_native_mode_diagnostic_differences="<<legacyModeDifferences<<" legacy_helper_not_gated=1 untimed=1\n";
}
