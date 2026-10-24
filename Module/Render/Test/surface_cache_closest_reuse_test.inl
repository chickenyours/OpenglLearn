// Software closest/any-hit equivalence for the optional child-bounds cache
// and guarded atlas addressing. No native search or lighting oracle is used.
void SurfaceCacheClosestReuseContract(RHIDevice& device) {
  ResourceBuilder build(device);const auto mesh=build.FullscreenMesh();
  struct Case {
    std::string name;std::vector<glm::vec4> pixels;
    glm::vec3 origin{0},direction{0,0,1};float maximum=8;
    int nodes=1,triangles=1;
  };
  const auto triangle=[](std::vector<glm::vec4>& pixels,int offset,float x,float z,bool opposed=false) {
    pixels[offset]={x-1,-1,z,0};pixels[offset+1]={x+1,-1,z,0};pixels[offset+2]={x,1,z,0};
    if(opposed)std::swap(pixels[offset+1],pixels[offset+2]);
  };
  const auto leaf=[&](const std::string& name) {
    Case c;c.name=name;c.pixels.resize(7);c.pixels[0]={-1,-1,2,0};c.pixels[1]={1,1,2,-1};triangle(c.pixels,2,0,2);return c;
  };
  const auto pair=[&](const std::string& name,float leftX,float leftZ,float rightX,float rightZ,bool opposed=false) {
    Case c;c.name=name;c.nodes=3;c.triangles=2;c.pixels.resize(16);
    c.pixels[0]={std::min(leftX,rightX)-1,-1,std::min(leftZ,rightZ),1};
    c.pixels[1]={std::max(leftX,rightX)+1,1,std::max(leftZ,rightZ),2};
    c.pixels[2]={leftX-1,-1,leftZ,0};c.pixels[3]={leftX+1,1,leftZ,-1};
    c.pixels[4]={rightX-1,-1,rightZ,1};c.pixels[5]={rightX+1,1,rightZ,-1};
    triangle(c.pixels,6,leftX,leftZ);triangle(c.pixels,11,rightX,rightZ,opposed);return c;
  };
  std::vector<Case> cases;
  auto c=leaf("empty");c.nodes=0;c.triangles=0;cases.push_back(c);
  cases.push_back(leaf("interior"));
  c=leaf("root_miss");c.origin.x=4;cases.push_back(c);
  c=leaf("triangle_miss_inside_box");c.origin={.75f,.75f,0};cases.push_back(c);
  cases.push_back(pair("left_only",0,2,4,4));
  cases.push_back(pair("right_only",-4,2,0,4));
  cases.push_back(pair("near_left_shrinks_far",0,2,0,4));
  cases.push_back(pair("near_right_shrinks_far",0,4,0,2));
  cases.push_back(pair("opposed_coplanar_tie_right_first",0,2,0,2,true));
  c=leaf("open_upper_endpoint");c.maximum=2;cases.push_back(c);
  c=leaf("self_tmin");c.origin.z=2;cases.push_back(c);
  c=leaf("vertex");c.origin={-1,-1,0};cases.push_back(c);
  c=leaf("edge");c.origin.y=-1;cases.push_back(c);
  c=leaf("expanded_edge_rejected_by_bounds");c.origin.y=-1.0000005f;cases.push_back(c);
  c=leaf("coplanar_parallel");c.origin={0,-2,2};c.direction={0,1,0};cases.push_back(c);
  c=leaf("near_parallel_axis");c.direction=glm::normalize(glm::vec3(.0000000001f,0,1));cases.push_back(c);
  c=leaf("reverse_direction");c.origin.z=4;c.direction.z=-1;cases.push_back(c);
  // A complete balanced tree exercises sibling pruning, changing closest IDs,
  // tie order, multi-level near-child reuse and both directions of traversal.
  for(unsigned pattern=0;pattern<64;++pattern) {
    Case item;item.name="balanced_"+std::to_string(pattern);item.nodes=15;item.triangles=8;item.pixels.resize(70);
    for(unsigned i=0;i<8;++i) {
      const float x=pattern%4==0?0.f:float(int((i+pattern)%3)-1)*1.25f;
      const float z=1.f+float((i*3+pattern)%7)*.5f;
      const unsigned node=7+i;item.pixels[node*2]={x-1,-1,z,float(i)};item.pixels[node*2+1]={x+1,1,z,-1};
      triangle(item.pixels,30+i*5,x,z,bool((i+pattern)&1));
    }
    for(int node=6;node>=0;--node) {
      const int left=node*2+1,right=left+1;
      item.pixels[node*2]=glm::vec4(glm::min(glm::vec3(item.pixels[left*2]),glm::vec3(item.pixels[right*2])),float(left));
      item.pixels[node*2+1]=glm::vec4(glm::max(glm::vec3(item.pixels[left*2+1]),glm::vec3(item.pixels[right*2+1])),float(right));
    }
    if(pattern&8){item.origin.z=6;item.direction.z=-1;}
    item.origin.x=float(int(pattern%7)-3)*.25f;cases.push_back(std::move(item));
  }
  // Non-axis planes preserve the exact float intersection/normal expressions
  // while approaching a grazing hit and original leaf bounds.
  for(unsigned rotation=0;rotation<32;++rotation) {
    const float angle=.173f*float(rotation+1);
    const glm::vec3 n=glm::normalize(glm::vec3(std::sin(angle),std::cos(angle*.7f),.25f+std::sin(angle*.37f)));
    const auto tangent=glm::normalize(glm::cross(n,glm::vec3(.31f,.53f,.79f))),bitangent=glm::normalize(glm::cross(n,tangent));
    const glm::vec3 center(float(int(rotation%7)-3)*13.25f,float(int(rotation%5)-2)*9.125f,float(int(rotation%3)-1)*17.5f);
    Case item;item.name="grazing_"+std::to_string(rotation);item.pixels.resize(7);
    const glm::vec3 a=center-tangent*.7f-bitangent*.45f,b=center+tangent*1.1f-bitangent*.2f,d=center-tangent*.2f+bitangent*.95f;
    item.pixels[0]=glm::vec4(glm::min(a,glm::min(b,d)),0);item.pixels[1]=glm::vec4(glm::max(a,glm::max(b,d)),-1);
    item.pixels[2]=glm::vec4(a,0);item.pixels[3]=glm::vec4(b,0);item.pixels[4]=glm::vec4(d,0);
    item.direction=glm::normalize(n*.0001f+tangent);item.origin=a+(b-a)*.25f+(d-a)*1e-5f-item.direction*2.f;
    cases.push_back(std::move(item));
  }
  const auto cyclic=[&](const std::string& name,bool overflow) {
    Case item;item.name=name;item.nodes=4;item.triangles=2;item.pixels.resize(18);
    item.pixels[0]={-1,-1,0,1};item.pixels[1]={1,1,4,2};
    item.pixels[2]={-1,-1,0,0};item.pixels[3]={1,1,2,-1};triangle(item.pixels,8,0,2);
    // First visit the near leaf and retain its result. A later malformed
    // subtree then reaches the original stack or 16384-visit exhaustion guard.
    item.pixels[4]={-1,-1,1,float(overflow?3:2)};item.pixels[5]={1,1,4,float(overflow?2:3)};
    item.pixels[6]={overflow?-1.f:3.f,-1,1,1};item.pixels[7]={overflow?1.f:5.f,1,4,-1};
    triangle(item.pixels,13,overflow?0.f:4.f,4);return item;
  };
  const size_t visitExhaustion=cases.size();cases.push_back(cyclic("partial_visit_exhaustion",false));
  const size_t stackExhaustion=cases.size();cases.push_back(cyclic("partial_stack_exhaustion",true));
  const auto input=build.Uniform(3*sizeof(glm::vec4)),cache=build.Uniform(sizeof(SurfaceCacheGiConstants));
  const auto result=build.Texture(uint32_t(cases.size()*2),1,RHITextureFormat::RGBA32F);
  const auto normal=build.Texture(uint32_t(cases.size()*2),1,RHITextureFormat::RGBA32F);
  const auto diagnostic=build.Texture(uint32_t(cases.size()*2),1,RHITextureFormat::RGBA32F);
  const auto target=build.TargetColors({result,normal,diagnostic,{}});
  const std::string vertex=R"(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);})";
  const std::string body=R"(
layout(std140,binding=0) uniform ClosestReuseCase {vec4 testOrigin,testDirection,testInterval;};
layout(location=0) out vec4 result;layout(location=1) out vec4 normalData;layout(location=2) out vec4 diagnostics;
void main(){float maximum=testInterval.x;int triangle;vec3 normal;bool exhausted;
  bool found=SCIntersect(testOrigin.xyz,testDirection.xyz,maximum,normal,triangle,exhausted,testInterval.y>.5);
  result=vec4(found?1:0,maximum,float(triangle),exhausted?1:0);normalData=vec4(normal,0);
  diagnostics=vec4(float(fixtureGeometryFetches),0,0,0);
})";
  std::array<RenderResourceHandle<PipelineSpec>,4> programs;
  for(unsigned mask=0;mask<4;++mask) {
    auto source=SoftwareBvhVisibilityGLSL(9,bool(mask&1),bool(mask&2));
    const std::string token="vec4 SCGeometry(int index){";
    const auto at=source.find(token);Require(at!=std::string::npos,"Closest reuse fixture lost geometry helper");
    source.insert(at+token.size(),"++fixtureGeometryFetches;");
    programs[mask]=build.FullscreenPipeline(vertex,"#version 450 core\nint fixtureGeometryFetches=0;\n"+SurfaceCacheDataGLSL()+source+body);
  }
  const auto wait=[&](const bool& done) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Closest reuse GPU timeout");
      if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
  };
  std::size_t channels=0,reducedDraws=0,addressChannels=0;std::uint64_t frame=2400;
  for(unsigned width:std::array<unsigned,2>{1024,17}) {
    std::vector<RenderResourceHandle<RHITextureSpec>> geometry;
    for(const auto& item:cases) {
      const unsigned height=unsigned((item.pixels.size()+width-1)/width);
      std::vector<glm::vec4> pixels(width*height,glm::vec4(0));std::copy(item.pixels.begin(),item.pixels.end(),pixels.begin());
      CreateRHITextureSpec desc;desc.width=width;desc.height=height;desc.mipmaps=false;
      desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;desc.filterMode=RHIFilterMode::Nearest;desc.data=pixels.data();
      geometry.push_back(build.Create<RHITextureSpec>([&](auto callback){device.async_CreateTexture(desc,callback);}));
    }
    std::array<std::vector<float>,3> baseline;
    for(unsigned mask=0;mask<4;++mask) {
      auto encoder=device.BeginFrame({frame++,64,32});
      Require(encoder.SetRenderTarget({target,uint32_t(cases.size()*2),1,RHICommand::ClearColor,glm::vec4(0)})&&
          encoder.BindPipeline(programs[mask])&&encoder.BindMesh(mesh)&&encoder.BindUniformBuffer(input,0)&&
          encoder.BindUniformBuffer(cache,SurfaceCacheGiBinding),"Closest reuse bindings");
      for(size_t i=0;i<cases.size();++i)for(unsigned anyHit=0;anyHit<2;++anyHit) {
        const auto& item=cases[i];SurfaceCacheGiConstants constants;constants.geometry={item.nodes*2,int(width),item.nodes,item.triangles};
        const std::array<glm::vec4,3> values{glm::vec4(item.origin,0),glm::vec4(item.direction,0),glm::vec4(item.maximum,float(anyHit),0,0)};
        Require(encoder.UpdateUniformBuffer(cache,constants)&&encoder.UpdateUniformBufferBytes(input,values.data(),sizeof(values))&&
            encoder.BindTexture(geometry[i],9)&&encoder.SetViewport({int32_t(i*2+anyHit),0,1,1})&&encoder.DrawIndexed({3}),"Closest reuse case draw");
      }
      Require(encoder.End(false),"Closest reuse frame end");bool done=false;
      device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[&]{done=true;});wait(done);
      std::array<std::vector<float>,3> actual;std::uint64_t errors=0;
      OnThread(device,[&]{actual[0]=device.BackendDiagnostics()->ReadTexture(result);actual[1]=device.BackendDiagnostics()->ReadTexture(normal);
        actual[2]=device.BackendDiagnostics()->ReadTexture(diagnostic);errors=device.BackendDiagnostics()->ValidationErrorCount();});
      Require(!errors,"Closest reuse Vulkan validation error");
      for(const auto& part:actual)Require(part.size()==cases.size()*8,"Closest reuse readback extent");
      if(!mask) {
        baseline=actual;
        const std::array<int,17> expectedTriangle{-1,0,-1,-1,0,1,0,1,1,-1,-1,0,0,-1,-1,0,0};
        for(size_t i=0;i<expectedTriangle.size();++i) {
          const size_t at=i*8;const int expected=expectedTriangle[i];
          const float expectedDistance=expected<0?cases[i].maximum:i==5?4.f:2.f;
          Require(actual[0][at]==float(expected>=0)&&actual[0][at+2]==float(expected)&&actual[0][at+3]==0,
              "Closest reuse independent simple geometry expectation");
          Require(std::abs(actual[0][at+1]-expectedDistance)<1e-6f&&actual[1][at]==0&&actual[1][at+1]==0&&
              actual[1][at+2]==float(expected<0?0:i==8?-1:1)&&actual[1][at+3]==0,
              "Closest reuse independent distance/normal expectation");
        }
        Require(actual[0][visitExhaustion*8]==1&&actual[0][visitExhaustion*8+3]==1&&
            actual[0][stackExhaustion*8]==1&&actual[0][stackExhaustion*8+3]==1,
            "Closest reuse partial-hit exhaustion fixtures did not exercise guards");
      } else {
        for(size_t part=0;part<2;++part)for(size_t at=0;at<actual[part].size();++at) {
          if(std::bit_cast<std::uint32_t>(baseline[part][at])!=std::bit_cast<std::uint32_t>(actual[part][at]))
            std::cout<<"Closest reuse mismatch width="<<width<<" mask="<<mask<<" case="<<cases[at/8].name<<" any="<<((at/4)%2)
                <<" part="<<part<<" component="<<(at%4)<<" before="<<baseline[part][at]<<" after="<<actual[part][at]<<'\n';
          Require(std::bit_cast<std::uint32_t>(baseline[part][at])==std::bit_cast<std::uint32_t>(actual[part][at]),
              "Closest reuse changed FP32 nearest/normal/tie/exhaustion result");++channels;
        }
        for(size_t at=0;at<actual[2].size();at+=4) {
          Require(actual[2][at]<=baseline[2][at],"Closest reuse increased logical geometry fetches in fixture");
          if(mask&1)reducedDraws+=actual[2][at]<baseline[2][at];
          else Require(actual[2][at]==baseline[2][at],"Fast addressing changed traversal work");
        }
      }
    }
    // Test addresses as integers, including negative indices, without issuing
    // out-of-bounds texture reads. Negative GLSL '%' is undefined: gate its
    // quotient/branch and record the remainder, never use C++ '%' as its oracle.
    constexpr std::array<int,16> indices{-2051,-1025,-1024,-1023,-17,-1,0,1,16,17,1023,1024,1025,2048,2049,16385};
    const auto addressTexture=build.Texture(uint32_t(indices.size()),1,RHITextureFormat::RGBA32F);
    const auto addressTarget=build.Target(addressTexture,{});
    const auto addressProgram=build.FullscreenPipeline(vertex,"#version 450 core\n"+SurfaceCacheDataGLSL()+SoftwareBvhVisibilityGLSL(9,false,true)+R"(
layout(std140,binding=0) uniform AddressCase {ivec4 indexData;};layout(location=0) out vec4 result;
void main(){result=vec4(vec2(SCGeometryAddress(indexData.x)),0,1);})");
    auto encoder=device.BeginFrame({frame++,64,32});SurfaceCacheGiConstants constants;constants.geometry.y=int(width);
    Require(encoder.SetRenderTarget({addressTarget,uint32_t(indices.size()),1,RHICommand::ClearColor,glm::vec4(0)})&&
        encoder.BindPipeline(addressProgram)&&encoder.BindMesh(mesh)&&encoder.UpdateUniformBuffer(cache,constants)&&
        encoder.BindUniformBuffer(cache,SurfaceCacheGiBinding)&&encoder.BindUniformBuffer(input,0)&&encoder.BindTexture(geometry.front(),9),"Address fixture bindings");
    for(size_t i=0;i<indices.size();++i) {
      const glm::ivec4 values(indices[i],0,0,0);
      Require(encoder.UpdateUniformBuffer(input,values)&&encoder.SetViewport({int32_t(i),0,1,1})&&encoder.DrawIndexed({3}),"Address fixture draw");
    }
    Require(encoder.End(false),"Address fixture frame end");bool done=false;device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[&]{done=true;});wait(done);
    std::vector<float> addresses;OnThread(device,[&]{addresses=device.BackendDiagnostics()->ReadTexture(addressTexture);
      Require(!device.BackendDiagnostics()->ValidationErrorCount(),"Address fixture validation error");});
    Require(addresses.size()==indices.size()*4,"Address fixture readback size");
    for(size_t i=0;i<indices.size();++i) {
      Require(addresses[i*4+1]==float(indices[i]/int(width))&&addresses[i*4+2]==0&&addresses[i*4+3]==1,
          "Guarded geometry addressing changed quotient or nonnegative guard");addressChannels+=3;
      if(indices[i]>=0) {
        Require(addresses[i*4]==float(indices[i]%int(width)),"Guarded nonnegative geometry remainder differs from CPU");++addressChannels;
      } else std::cout<<"SC GEOMETRY ADDRESS negative index="<<indices[i]<<" width="<<width<<" observed_remainder="<<addresses[i*4]
          <<" negative_texel_fetch=0 mathematical_remainder_oracle=0\n";
    }
  }
  Require(reducedDraws>0,"Closest reuse fixture did not exercise a cached child");
  std::cout<<"Surface cache closest reuse contract passed: cases="<<cases.size()<<" any_hit_modes=2 widths=1024,17 variants=4 compared_fp32_channels="<<channels
      <<" guarded_address_channels="<<addressChannels<<" reduced_logical_fetch_draws="<<reducedDraws
      <<" partial_visit_and_stack_exhaustion=2 original_helper_preserved=1 timed=0 logical_fetches_not_physical_reads=1\n";
}
