// Production native probe helper contracts. Included by vulkan_backend_gpu_test.
#include "Render/Public/Pipeline/surface_cache_gi.h"
void ProbeNativeIntersectionContract(RHIDevice& device) {
  bool supported=false;
  OnThread(device,[&]{supported=device.BackendDiagnostics()->HardwareRayTracingAvailable();});
  if(!supported){std::cout<<"Native probe intersection contract skipped: hardware unavailable\n";return;}
  ResourceBuilder build(device);const auto mesh=build.FullscreenMesh();
  struct Case {
    std::string name;std::vector<glm::vec4> geometry;int nodes=1,triangles=1;
    glm::vec3 origin{0},direction{0,0,1};float minimum=0,maximum=8;
  };
  const auto setTriangle=[](std::vector<glm::vec4>& geometry,int offset,float x,float z) {
    geometry[offset]={x-1,-1,z,0};geometry[offset+1]={x+1,-1,z,0};geometry[offset+2]={x,1,z,0};
  };
  const auto leaf=[&](const char* name,float z=2.f) {
    Case c;c.name=name;c.geometry.resize(7);c.geometry[0]={-1,-1,z,0};c.geometry[1]={1,1,z,-1};
    setTriangle(c.geometry,2,0,z);return c;
  };
  const auto pair=[&](const char* name,float leftX,float leftZ,float rightX,float rightZ) {
    Case c;c.name=name;c.nodes=3;c.triangles=2;c.geometry.resize(16);
    c.geometry[0]={std::min(leftX,rightX)-1,-1,std::min(leftZ,rightZ),1};
    c.geometry[1]={std::max(leftX,rightX)+1,1,std::max(leftZ,rightZ),2};
    c.geometry[2]={leftX-1,-1,leftZ,0};c.geometry[3]={leftX+1,1,leftZ,-1};
    c.geometry[4]={rightX-1,-1,rightZ,1};c.geometry[5]={rightX+1,1,rightZ,-1};
    setTriangle(c.geometry,6,leftX,leftZ);setTriangle(c.geometry,11,rightX,rightZ);return c;
  };
  std::vector<Case> cases;
  cases.push_back(leaf("interior"));
  auto c=leaf("box_miss");c.origin.x=4;cases.push_back(c);
  c=leaf("triangle_miss_inside_box");c.origin={.75f,.75f,0};cases.push_back(c);
  c=pair("left_only",0,2,4,4);cases.push_back(c);
  c=pair("right_only",-4,4,0,2);cases.push_back(c);
  c=pair("near_left",0,2,0,4);cases.push_back(c);
  c=pair("near_right",0,4,0,2);cases.push_back(c);
  c=pair("interior_coplanar_tie",0,2,0,2);cases.push_back(c);
  c=leaf("minimum_clips");c.minimum=3;cases.push_back(c);
  c=leaf("closed_minimum");c.minimum=2;cases.push_back(c);
  c=leaf("closed_maximum");c.maximum=2;cases.push_back(c);
  c=leaf("maximum_clips");c.maximum=1.999f;cases.push_back(c);
  c=leaf("zero_interval");c.minimum=2;c.maximum=2;cases.push_back(c);
  c=leaf("parallel");c.origin={0,-2,2};c.direction={0,1,0};cases.push_back(c);
  c=leaf("edge");c.origin={0,-1,0};cases.push_back(c);
  c=leaf("vertex");c.origin={-1,-1,0};cases.push_back(c);
  c=leaf("expanded_edge_rejected");c.origin={0,-1.000001f,0};cases.push_back(c);
  c=leaf("backface");c.origin={0,0,4};c.direction={0,0,-1};cases.push_back(c);
  c=leaf("near_parallel");c.direction=glm::normalize(glm::vec3(.000001f,0,1));cases.push_back(c);
  c=leaf("self_tmin");c.origin={0,0,2};cases.push_back(c);
  // Non-axis planes exercise the independently rounded conservative native
  // candidate vertices. Vary local shape, world position, ray incidence and
  // barycentric proximity; these are legal triangles, not synthetic BVHs.
  for(unsigned plane=0;plane<32;++plane) {
    const float angle=.173f*float(plane+1);
    const glm::vec3 n=glm::normalize(glm::vec3(std::sin(angle),std::cos(angle*.7f),.25f+std::sin(angle*.37f)));
    const glm::vec3 tangent=glm::normalize(glm::cross(n,glm::vec3(.31f,.53f,.79f)));
    const glm::vec3 bitangent=glm::normalize(glm::cross(n,tangent));
    const glm::vec3 center(float(int(plane%7)-3)*13.25f,float(int(plane%5)-2)*9.125f,float(int(plane%3)-1)*17.5f);
    const glm::vec3 a=center-tangent*.7f-bitangent*.45f;
    const glm::vec3 b=center+tangent*1.1f-bitangent*.2f;
    const glm::vec3 d=center-tangent*.2f+bitangent*.95f;
    const glm::vec3 low=glm::min(a,glm::min(b,d)),high=glm::max(a,glm::max(b,d));
    for(unsigned incidence=0;incidence<4;++incidence) {
      Case rotated;rotated.name="rotated_"+std::to_string(plane)+"_incidence_"+std::to_string(incidence);
      rotated.geometry.resize(7);rotated.geometry[0]=glm::vec4(low,0);rotated.geometry[1]=glm::vec4(high,-1);
      rotated.geometry[2]=glm::vec4(a,0);rotated.geometry[3]=glm::vec4(b,0);rotated.geometry[4]=glm::vec4(d,0);
      const float normalWeight=incidence==0?1.f:incidence==1?.05f:incidence==2?.001f:.0001f;
      rotated.direction=glm::normalize(n*normalWeight+tangent);
      // The final incidence approaches an expanded edge; predicate/bounds
      // rounding can legitimately select miss, but native must match it.
      const float v=incidence==3?1e-5f:.3f;
      const glm::vec3 point=a+(b-a)*.25f+(d-a)*v;
      rotated.origin=point-rotated.direction*2.f;cases.push_back(std::move(rotated));
    }
  }
  // A successful far candidate must not mask a native miss of a closer grazing
  // triangle. In particular rotated_18_incidence_3 is a measured native miss
  // that the all-miss fallback recovers only when no far plane is present.
  for(unsigned plane=0;plane<32;++plane) {
    const auto& nearCase=cases[20+plane*4+3];Case masked=nearCase;
    masked.name="masked_near_"+nearCase.name;masked.nodes=3;masked.triangles=2;masked.geometry.assign(16,glm::vec4(0));
    const glm::vec3 center=nearCase.origin+nearCase.direction*6.f;
    const glm::vec3 tangent=glm::normalize(glm::cross(nearCase.direction,glm::vec3(.31f,.53f,.79f)));
    const glm::vec3 bitangent=glm::normalize(glm::cross(nearCase.direction,tangent));
    const glm::vec3 a=center-tangent*4.f-bitangent*4.f,b=center+tangent*4.f-bitangent*4.f,d=center+bitangent*4.f;
    const glm::vec3 farLow=glm::min(a,glm::min(b,d)),farHigh=glm::max(a,glm::max(b,d));
    masked.geometry[0]=glm::vec4(glm::min(glm::vec3(nearCase.geometry[0]),farLow),1);
    masked.geometry[1]=glm::vec4(glm::max(glm::vec3(nearCase.geometry[1]),farHigh),2);
    masked.geometry[2]=nearCase.geometry[0];masked.geometry[2].w=0;
    masked.geometry[3]=nearCase.geometry[1];masked.geometry[3].w=-1;
    masked.geometry[4]=glm::vec4(farLow,1);masked.geometry[5]=glm::vec4(farHigh,-1);
    std::copy_n(nearCase.geometry.begin()+2,3,masked.geometry.begin()+6);
    masked.geometry[11]=glm::vec4(a,0);masked.geometry[12]=glm::vec4(b,0);masked.geometry[13]=glm::vec4(d,0);
    cases.push_back(std::move(masked));
  }
  const size_t opposedFirst=cases.size();
  c=pair("origin_opposed_coplanar_right_first",0,2,0,2);std::swap(c.geometry[12],c.geometry[13]);cases.push_back(c);
  c.name="origin_opposed_coplanar_swapped_children";c.geometry[0].w=2;c.geometry[1].w=1;cases.push_back(c);
  const size_t partialFirst=cases.size();
  // Deliberately synthetic legal-index graphs isolate partial nearest-hit
  // exhaustion. Every leaf/branch read remains inside the packed allocation.
  c=leaf("partial_hit_then_visit_exhaustion");c.nodes=3;c.geometry.assign(11,glm::vec4(0));c.maximum=2;
  c.geometry[0]={-1,-1,0,1};c.geometry[1]={1,1,3,2};
  c.geometry[2]={-1,-1,0,0};c.geometry[3]={1,1,3,-1};
  c.geometry[4]={-1,-1,1,1};c.geometry[5]={1,1,3,2};setTriangle(c.geometry,6,0,2);cases.push_back(c);
  // Reverse children to reach the same partial hit even in the unsafe variant
  // whose accepted BVH uses stack order instead of near-first child ordering.
  c.name="partial_hit_then_visit_exhaustion_both_orders";c.maximum=8;
  c.geometry[0].w=2;c.geometry[1].w=1;c.geometry[4].w=2;c.geometry[5].w=1;cases.push_back(c);

  const auto geometryTexture=[&](std::vector<glm::vec4> pixels) {
    pixels.resize(1024,glm::vec4(0));CreateRHITextureSpec desc;
    desc.width=1024;desc.height=1;desc.mipmaps=false;desc.data=pixels.data();
    desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;
    desc.filterMode=RHIFilterMode::Nearest;
    return build.Create<RHITextureSpec>([&](auto callback){device.async_CreateTexture(desc,callback);});
  };
  std::vector<RenderResourceHandle<RHITextureSpec>> geometry;
  for(const auto& item:cases)geometry.push_back(geometryTexture(item.geometry));
  struct alignas(16) Input {glm::vec4 origin,direction,interval;};
  const auto input=build.Uniform(sizeof(Input)),sdf=build.Uniform(sizeof(ProbeSdfTraceConstants));
  const auto cache=build.Uniform(sizeof(SurfaceCacheGiConstants));
  std::array<RenderResourceHandle<RHITextureSpec>,4> outputs;
  for(auto& output:outputs)output=build.Texture(uint32_t(cases.size()),1,RHITextureFormat::RGBA32F);
  const std::array<RenderResourceHandle<RHITextureSpec>,4> attachments{outputs[0],outputs[1],outputs[2],outputs[3]};
  const auto target=build.TargetColors(attachments);
  const std::string vertex=R"(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);})";
  const std::string body=R"(
layout(std140,binding=0) uniform NativeProbeCase {vec4 ncOrigin,ncDirection,ncInterval;};
layout(location=0) out vec4 result;layout(location=1) out vec4 normalData;layout(location=2) out vec4 marched;
layout(location=3) out vec4 logicalFetches;
void main(){float maximum=ncInterval.y,fetches=0;int triangle;vec3 normal;bool exhausted;
  // Keep the real reflected AS-header UBO active in this isolated helper main.
  if(any(notEqual(scGeometry,psGeometry))){result=vec4(-9);normalData=vec4(-9);marched=vec4(-9);return;}
  bool found=PSRefine(ncOrigin.xyz,ncDirection.xyz,ncInterval.x,maximum,triangle,normal,fetches,exhausted);
  result=vec4(found?1:0,maximum,float(triangle),exhausted?1:0);normalData=vec4(normal,0);logicalFetches=vec4(fetches,0,0,0);
#if defined(SC_NATIVE_GEOMETRY_SLOT) || defined(PS_DIRECT_SOFTWARE_REFINEMENT)
  float distance=0;int steps=0;fetches=0;
  found=PSMarch(ncOrigin.xyz,ncDirection.xyz,ncInterval.x,ncInterval.y,false,distance,steps,fetches,triangle,normal,exhausted);
  marched=vec4(found?1:0,distance,float(triangle),exhausted?1:0);logicalFetches.y=fetches;
#else
  marched=result;
#endif
})";
  std::array<std::array<RenderResourceHandle<PipelineSpec>,4>,2> programs;
  for(unsigned safe=0;safe<2;++safe)for(unsigned native=0;native<4;++native) {
    const auto source=std::string("#version 450 core\n")+
        ProbeSdfDetail::FallbackCommonGLSL(safe!=0,true,true,native!=0,native==2,native==3)+body;
    if(native==3)Require(source.find("#define SC_NATIVE_GEOMETRY_SLOT")==std::string::npos&&
        source.find("accelerationStructureEXT")==std::string::npos&&source.find("rayQueryEXT")==std::string::npos,
        "Direct software triangle profile unexpectedly requests a hardware query");
    programs[safe][native]=build.FullscreenPipeline(vertex,source);
  }
  size_t channels=0,mismatches=0;uint64_t frameIndex=1500;
  size_t candidateChannels=0,candidateMismatches=0,candidateLegacyChannels=0,candidateLegacyMismatches=0;
  size_t candidateFetchChannels=0,candidateFetchMismatches=0;
  size_t directChannels=0,directMismatches=0,directLegacyChannels=0,directLegacyMismatches=0;
  size_t directFetchChannels=0,directFetchMismatches=0;
  for(unsigned safe=0;safe<2;++safe) {
    std::array<std::vector<float>,3> baseline,acceptedNative;std::vector<float> baselineFetches,acceptedNativeFetches;
    for(unsigned native=0;native<4;++native) {
      auto e=device.BeginFrame({frameIndex++,64,32});
      Require(e.SetRenderTarget({target,uint32_t(cases.size()),1,RHICommand::ClearColor,glm::vec4(0)})&&
          e.BindMesh(mesh)&&e.BindPipeline(programs[safe][native])&&e.BindUniformBuffer(input,0)&&
          e.BindUniformBuffer(sdf,ProbeSdfTracingBinding)&&e.BindUniformBuffer(cache,SurfaceCacheGiBinding),
          "Native probe contract initial bindings");
      for(size_t i=0;i<cases.size();++i) {
        const auto& item=cases[i];ProbeSdfTraceConstants constants;
        constants.atlas={1024,1024,0,1};constants.geometry={item.nodes*2,1024,item.nodes,item.triangles};
        const auto* bytes=reinterpret_cast<const std::byte*>(&constants);
        for(uint32_t offset=0;offset<sizeof(constants);offset+=RHICommand::MaxInlineUniformBytes) {
          const auto count=uint32_t(std::min<size_t>(RHICommand::MaxInlineUniformBytes,sizeof(constants)-offset));
          Require(e.UpdateUniformBufferBytes(sdf,bytes+offset,count,offset),"Native probe contract SDF constants");
        }
        SurfaceCacheGiConstants sc;sc.geometry=constants.geometry;
        const auto* cacheBytes=reinterpret_cast<const std::byte*>(&sc);
        for(uint32_t offset=0;offset<sizeof(sc);offset+=RHICommand::MaxInlineUniformBytes) {
          const auto count=uint32_t(std::min<size_t>(RHICommand::MaxInlineUniformBytes,sizeof(sc)-offset));
          Require(e.UpdateUniformBufferBytes(cache,cacheBytes+offset,count,offset),"Native probe contract AS constants");
        }
        Input values{glm::vec4(item.origin,0),glm::vec4(item.direction,0),glm::vec4(item.minimum,item.maximum,0,0)};
        Require(e.UpdateUniformBuffer(input,values)&&e.BindTexture(geometry[i],9)&&e.SetViewport({int32_t(i),0,1,1})&&
            e.DrawIndexed({3}),"Native probe contract case draw");
      }
      Require(e.End(false),"Native probe contract frame end");bool done=false;
      device.async_SubmitFrameCommands(e.GetCommandBuffer(),[&]{done=true;});
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
      while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Native probe contract timeout");
        if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
      std::array<std::vector<float>,3> actual;std::vector<float> actualFetches;uint64_t errors=0;
      OnThread(device,[&]{for(size_t part=0;part<actual.size();++part)actual[part]=device.BackendDiagnostics()->ReadTexture(outputs[part]);
        actualFetches=device.BackendDiagnostics()->ReadTexture(outputs[3]);
        errors=device.BackendDiagnostics()->ValidationErrorCount();});
      Require(!errors,"Native probe contract Vulkan validation error");
      for(const auto& part:actual)Require(part.size()==cases.size()*4,"Native probe contract readback extent");
      Require(actualFetches.size()==cases.size()*4,"Native probe contract logical-fetch readback extent");
      // Independent expected values for the twenty simple geometric cases.
      // Comparing two GPU paths alone would accept two identically wrong or
      // accidentally unbound empty outputs.
      const std::array<int,20> expectedTriangle{0,-1,-1,0,1,0,1,0,-1,0,0,-1,-1,-1,0,0,-1,0,0,-1};
      for(size_t i=0;i<expectedTriangle.size();++i) {
        const size_t at=i*4;const bool found=expectedTriangle[i]>=0;
        const float expectedDistance=found?2.f:cases[i].maximum;
        Require(actual[0][at]==float(found)&&std::abs(actual[0][at+1]-expectedDistance)<1e-6f&&
            actual[0][at+2]==float(expectedTriangle[i])&&actual[0][at+3]==0,
            "Native probe simple geometric CPU expected result");
        Require(actual[1][at]==0&&actual[1][at+1]==0&&actual[1][at+2]==float(found)&&actual[1][at+3]==0,
            "Native probe simple geometric CPU expected normal");
      }
      if(!native){
        baseline=actual;baselineFetches=actualFetches;
        const auto first=partialFirst*4,second=(partialFirst+1)*4;
        Require(actual[0][first]==float(safe!=0)&&actual[0][first+1]==2&&actual[0][first+3]==1&&
            actual[0][second]==1&&actual[0][second+1]==2&&actual[0][second+2]==0&&actual[0][second+3]==1,
            "Software partial-hit exhaustion cases did not reach the intended guards");
        continue;
      }
      if(native==1){acceptedNative=actual;acceptedNativeFetches=actualFetches;}
      for(size_t i=0;i<cases.size();++i)for(size_t part=0;part<actual.size();++part)for(size_t component=0;component<4;++component) {
        const size_t at=i*4+component;float expected=baseline[part==2?0:part][at];const float value=actual[part][at];
        // The helper retains the accepted partial result, but a march may not
        // turn a partial nearest query into readable/certified radiance.
        if(part==2&&component==0&&baseline[0][i*4+3]!=0)expected=0;
        if(std::bit_cast<uint32_t>(expected)!=std::bit_cast<uint32_t>(value))
          std::cout<<(native==3?"Direct-software probe versus software mismatch case=":native==2?"Candidate-reuse probe versus software mismatch case=":"Native probe mismatch case=")<<cases[i].name<<" safe="<<safe<<" part="<<part<<" component="<<component
              <<" expected="<<expected<<" actual="<<value<<std::endl;
        const bool mismatch=std::bit_cast<uint32_t>(expected)!=std::bit_cast<uint32_t>(value);
        if(native==1){mismatches+=mismatch;++channels;}
        else if(native==2) {
          candidateMismatches+=mismatch;++candidateChannels;
          const float accepted=acceptedNative[part][at];
          if(std::bit_cast<uint32_t>(accepted)!=std::bit_cast<uint32_t>(value))
            std::cout<<"Candidate-reuse probe versus original native mismatch case="<<cases[i].name<<" safe="<<safe<<" part="<<part<<" component="<<component
                <<" expected="<<accepted<<" actual="<<value<<std::endl;
          candidateLegacyMismatches+=std::bit_cast<uint32_t>(accepted)!=std::bit_cast<uint32_t>(value);++candidateLegacyChannels;
        } else {
          directMismatches+=mismatch;++directChannels;
          const float accepted=acceptedNative[part][at];
          if(std::bit_cast<uint32_t>(accepted)!=std::bit_cast<uint32_t>(value))
            std::cout<<"Direct-software probe versus original native mismatch case="<<cases[i].name<<" safe="<<safe<<" part="<<part<<" component="<<component
                <<" expected="<<accepted<<" actual="<<value<<std::endl;
          directLegacyMismatches+=std::bit_cast<uint32_t>(accepted)!=std::bit_cast<uint32_t>(value);++directLegacyChannels;
        }
      }
      if(native==2)for(size_t i=0;i<cases.size();++i)for(unsigned component=0;component<2;++component) {
        const size_t at=i*4+component;const float expected=acceptedNativeFetches[at],value=actualFetches[at];
        if(std::bit_cast<uint32_t>(expected)!=std::bit_cast<uint32_t>(value))
          std::cout<<"Candidate-reuse probe logical-fetch mismatch case="<<cases[i].name<<" safe="<<safe<<" component="<<component
              <<" expected="<<expected<<" actual="<<value<<std::endl;
        candidateFetchMismatches+=std::bit_cast<uint32_t>(expected)!=std::bit_cast<uint32_t>(value);++candidateFetchChannels;
      }
      if(native==3)for(size_t i=0;i<cases.size();++i)for(unsigned component=0;component<2;++component) {
        // Both the direct helper and its single PSMarch call must consume the
        // original full-interval software query's requests, not the native
        // candidate's extra reads or its shortened certification interval.
        const size_t at=i*4+component;const float expected=baselineFetches[i*4],value=actualFetches[at];
        if(std::bit_cast<uint32_t>(expected)!=std::bit_cast<uint32_t>(value))
          std::cout<<"Direct-software probe logical-fetch mismatch case="<<cases[i].name<<" safe="<<safe<<" component="<<component
              <<" expected="<<expected<<" actual="<<value<<std::endl;
        directFetchMismatches+=std::bit_cast<uint32_t>(expected)!=std::bit_cast<uint32_t>(value);++directFetchChannels;
      }
    }
  }
  std::cout<<"Native probe intersection contract "<<(mismatches?"FAILED":"passed")<<": cases="<<cases.size()<<" safe_variants=2 compared_fp32_channels="<<channels<<" mismatched_channels="<<mismatches
      <<" cpu_expected_simple_cases=20 rotated_grazing_cases=128 native_miss_regression=rotated_18_incidence_3 "
      <<"masked_near_far_cases=32 opposed_interior_ties=2 partial_exhaustion_cases=2 strict_predicate,closed_intervals,edge_rejection,coplanar_ties,nearest,backface,no_sdf_sampling,conservative_native_miss_fallback\n";
  std::cout<<"Native probe candidate predicate reuse contract "<<((candidateMismatches||candidateLegacyMismatches||candidateFetchMismatches)?"FAILED":"passed")
      <<": cases="<<cases.size()<<" safe_variants=2 compared_software_fp32_channels="<<candidateChannels<<" software_mismatched_channels="<<candidateMismatches
      <<" compared_original_native_fp32_channels="<<candidateLegacyChannels<<" original_native_mismatched_channels="<<candidateLegacyMismatches
      <<" logical_fetch_channels="<<candidateFetchChannels<<" logical_fetch_mismatches="<<candidateFetchMismatches
      <<" candidate_hint_only_after_original_leaf_reached=1 no_preseed=1 original_software_fallback_preserved=1 physical_fetch_claim=0\n";
  std::cout<<"Direct software probe refinement contract "<<((directMismatches||directLegacyMismatches||directFetchMismatches)?"FAILED":"passed")
      <<": cases="<<cases.size()<<" safe_variants=2 compared_software_fp32_channels="<<directChannels<<" software_mismatched_channels="<<directMismatches
      <<" compared_original_native_fp32_channels="<<directLegacyChannels<<" original_native_mismatched_channels="<<directLegacyMismatches
      <<" logical_fetch_channels="<<directFetchChannels<<" logical_fetch_mismatches="<<directFetchMismatches
      <<" full_interval_software_calls_per_refine=1 hardware_query_declaration=0 sdf_gdf_march=0 original_software_predicate_preserved=1\n";

  // Exercise the actual shared helper now used by both receiver and probe
  // safety origins, separately from the stricter PSRefine implementation.
  const std::string originBody=R"(
layout(std140,binding=0) uniform NativeOriginCase {vec4 ncOrigin,ncDirection,ncInterval;};
layout(location=0) out vec4 result;layout(location=1) out vec4 normalData;layout(location=2) out vec4 unused;
layout(location=3) out vec4 traceWork;
void main(){float maximum=ncInterval.y;int triangle;vec3 normal;bool exhausted;
#if ORIGIN_TRACE_DIAGNOSTICS
  SCTraceDiagReset();
#endif
#if ORIGIN_NATIVE_CONTRACT==3
  bool found=SCNativeOriginIntersectReusingCertification(ncOrigin.xyz,ncDirection.xyz,maximum,normal,triangle,exhausted,false);
#elif ORIGIN_NATIVE_CONTRACT==2
  bool found=SCNativeOriginIntersect(ncOrigin.xyz,ncDirection.xyz,maximum,normal,triangle,exhausted,false);
#elif ORIGIN_NATIVE_CONTRACT==1
  bool found=SCNativeIntersect(ncOrigin.xyz,ncDirection.xyz,maximum,normal,triangle,exhausted,false);
#else
  bool found=SCIntersect(ncOrigin.xyz,ncDirection.xyz,maximum,normal,triangle,exhausted,false);
#endif
  result=vec4(found?1:0,maximum,float(triangle),exhausted?1:0);normalData=vec4(normal,0);unused=vec4(0);traceWork=vec4(0);
#if ORIGIN_TRACE_DIAGNOSTICS
  unused=vec4(scTraceDiag[0]);traceWork=vec4(scTraceDiag[1]);
#endif
})";
  std::array<RenderResourceHandle<PipelineSpec>,4> originPrograms;
  for(unsigned native=0;native<4;++native)
    originPrograms[native]=build.FullscreenPipeline(vertex,std::string("#version 450 core\n#define ORIGIN_NATIVE_CONTRACT ")+std::to_string(native)+"\n"+
        SurfaceCacheDataGLSL()+SoftwareBvhVisibilityGLSL(9)+(native?NativeSurfaceCacheIntersectionGLSL():std::string{})+originBody);
  std::array<std::vector<float>,2> originBaseline,certifiedOriginBaseline;
  std::array<std::array<std::vector<float>,2>,4> originalOriginOutputs;
  size_t originChannels=0,originMismatches=0,legacyOriginMismatches=0;
  size_t reusedOriginChannels=0,reusedOriginMismatches=0,reusedLegacyChannels=0,reusedLegacyMismatches=0;
  for(unsigned native=0;native<4;++native) {
    auto e=device.BeginFrame({frameIndex++,64,32});
    Require(e.SetRenderTarget({target,uint32_t(cases.size()),1,RHICommand::ClearColor,glm::vec4(0)})&&
        e.BindMesh(mesh)&&e.BindPipeline(originPrograms[native])&&e.BindUniformBuffer(input,0)&&
        e.BindUniformBuffer(cache,SurfaceCacheGiBinding),"Native origin contract bindings");
    for(size_t i=0;i<cases.size();++i) {
      const auto& item=cases[i];SurfaceCacheGiConstants sc;sc.geometry={item.nodes*2,1024,item.nodes,item.triangles};
      Input values{glm::vec4(item.origin,0),glm::vec4(item.direction,0),glm::vec4(item.minimum,item.maximum,0,0)};
      Require(e.UpdateUniformBuffer(cache,sc)&&e.UpdateUniformBuffer(input,values)&&e.BindTexture(geometry[i],9)&&
          e.SetViewport({int32_t(i),0,1,1})&&e.DrawIndexed({3}),"Native origin contract case draw");
    }
    Require(e.End(false),"Native origin contract frame end");bool done=false;
    device.async_SubmitFrameCommands(e.GetCommandBuffer(),[&]{done=true;});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Native origin contract timeout");
      if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
    std::array<std::vector<float>,2> actual;uint64_t errors=0;
    OnThread(device,[&]{for(size_t part=0;part<actual.size();++part)actual[part]=device.BackendDiagnostics()->ReadTexture(outputs[part]);
      errors=device.BackendDiagnostics()->ValidationErrorCount();});
    Require(!errors,"Native origin contract Vulkan validation error");
    for(const auto& part:actual)Require(part.size()==cases.size()*4,"Native origin contract extent");
    originalOriginOutputs[native]=actual;
    if(!native) {
      originBaseline=actual;
      for(unsigned order=0;order<2;++order) {
        const size_t at=(opposedFirst+order)*4;
        Require(actual[0][at]==1&&actual[0][at+1]==2&&actual[0][at+2]==float(order?0:1)&&actual[0][at+3]==0&&
            actual[1][at]==0&&actual[1][at+1]==0&&actual[1][at+2]==float(order?1:-1),
            "Software origin opposed winding tie independent CPU expectation");
      }
      continue;
    }
    if(native==2)certifiedOriginBaseline=actual;
    for(size_t i=0;i<cases.size();++i)for(size_t part=0;part<actual.size();++part)for(size_t component=0;component<4;++component) {
      const size_t at=i*4+component;const float expected=originBaseline[part][at],value=actual[part][at];
      if(std::bit_cast<uint32_t>(expected)!=std::bit_cast<uint32_t>(value))
        std::cout<<(native==1?"Legacy native origin diagnostic mismatch case=":native==2?"Native origin mismatch case=":"Reused native origin mismatch case=")<<cases[i].name<<" part="<<part<<" component="<<component
            <<" expected="<<expected<<" actual="<<value<<std::endl;
      if(native==1)legacyOriginMismatches+=std::bit_cast<uint32_t>(expected)!=std::bit_cast<uint32_t>(value);
      else if(native==2){originMismatches+=std::bit_cast<uint32_t>(expected)!=std::bit_cast<uint32_t>(value);++originChannels;}
      else {
        reusedOriginMismatches+=std::bit_cast<uint32_t>(expected)!=std::bit_cast<uint32_t>(value);++reusedOriginChannels;
        const float accepted=certifiedOriginBaseline[part][at];
        if(std::bit_cast<uint32_t>(accepted)!=std::bit_cast<uint32_t>(value))
          std::cout<<"Reused versus original certified origin mismatch case="<<cases[i].name<<" part="<<part<<" component="<<component
              <<" expected="<<accepted<<" actual="<<value<<std::endl;
        reusedLegacyMismatches+=std::bit_cast<uint32_t>(accepted)!=std::bit_cast<uint32_t>(value);++reusedLegacyChannels;
      }
    }
  }
  // Replay-only local query counters must not alter any original helper's
  // result, including legacy native diagnostics, ties and partial exhaustion.
  size_t diagnosticChannels=0,diagnosticMismatches=0,diagnosticCounterChannels=0;
  for(unsigned native=0;native<4;++native) {
    const auto helper=InstrumentSurfaceCacheTraceDiagnosticsGLSL(SurfaceCacheDataGLSL()+SoftwareBvhVisibilityGLSL(9)+
        (native?NativeSurfaceCacheIntersectionGLSL():std::string{}));
    const auto program=build.FullscreenPipeline(vertex,std::string("#version 450 core\n#define ORIGIN_TRACE_DIAGNOSTICS 1\n#define ORIGIN_NATIVE_CONTRACT ")+
        std::to_string(native)+"\n"+helper+originBody);
    auto e=device.BeginFrame({frameIndex++,64,32});
    Require(e.SetRenderTarget({target,uint32_t(cases.size()),1,RHICommand::ClearColor,glm::vec4(0)})&&e.BindMesh(mesh)&&
        e.BindPipeline(program)&&e.BindUniformBuffer(input,0)&&e.BindUniformBuffer(cache,SurfaceCacheGiBinding),"Query trace diagnostic bindings");
    for(size_t i=0;i<cases.size();++i) {
      const auto& item=cases[i];SurfaceCacheGiConstants sc;sc.geometry={item.nodes*2,1024,item.nodes,item.triangles};
      Input values{glm::vec4(item.origin,0),glm::vec4(item.direction,0),glm::vec4(item.minimum,item.maximum,0,0)};
      Require(e.UpdateUniformBuffer(cache,sc)&&e.UpdateUniformBuffer(input,values)&&e.BindTexture(geometry[i],9)&&
          e.SetViewport({int32_t(i),0,1,1})&&e.DrawIndexed({3}),"Query trace diagnostic case draw");
    }
    Require(e.End(false),"Query trace diagnostic frame end");bool done=false;
    device.async_SubmitFrameCommands(e.GetCommandBuffer(),[&]{done=true;});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Query trace diagnostic timeout");
      if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
    std::array<std::vector<float>,4> actual;uint64_t errors=0;
    OnThread(device,[&]{for(size_t part=0;part<actual.size();++part)actual[part]=device.BackendDiagnostics()->ReadTexture(outputs[part]);
      errors=device.BackendDiagnostics()->ValidationErrorCount();});
    Require(!errors,"Query trace diagnostic Vulkan validation error");
    for(const auto& part:actual)Require(part.size()==cases.size()*4,"Query trace diagnostic extent");
    for(size_t part=0;part<2;++part)for(size_t i=0;i<actual[part].size();++i) {
      ++diagnosticChannels;if(std::bit_cast<uint32_t>(actual[part][i])!=std::bit_cast<uint32_t>(originalOriginOutputs[native][part][i])) {
        ++diagnosticMismatches;std::cout<<"Query trace diagnostic output mismatch mode="<<native<<" case="<<cases[i/4].name
            <<" part="<<part<<" component="<<i%4<<'\n';
      }
    }
    const std::array<float,4> simpleLow=native?std::array<float,4>{1,1,0,0}:std::array<float,4>{0,0,0,0};
    const std::array<float,4> simpleHigh=native>=2?std::array<float,4>{0,1,1,0}:
        native?std::array<float,4>{0,0,0,0}:std::array<float,4>{0,1,0,0};
    for(size_t k=0;k<4;++k)Require(actual[2][k]==simpleLow[k]&&actual[3][k]==simpleHigh[k],"Query trace diagnostic interior expected counts");
    // Case one is the independent root-box miss. Native closest misses do one
    // software fallback; certification is only required after an actual hit.
    for(size_t k=0;k<4;++k)Require(actual[2][4+k]==(native?(k==0||k==2?1.f:0.f):0.f)&&
        actual[3][4+k]==(k==1?1.f:0.f),"Query trace diagnostic miss expected counts");
    for(size_t i=0;i<cases.size();++i) {
      const size_t at=i*4;
      for(size_t part=2;part<4;++part)for(size_t k=0;k<4;++k) {
        const float value=actual[part][at+k];++diagnosticCounterChannels;
        Require(std::isfinite(value)&&value>=0&&std::floor(value)==value,"Query trace diagnostic counter is not a finite integer");
      }
      const float nativeQueries=actual[2][at],classified=actual[2][at+1]+actual[2][at+2]+actual[2][at+3]+actual[3][at];
      Require(classified<=nativeQueries&&actual[3][at+3]<=actual[3][at+2]&&actual[3][at+2]<=actual[3][at+1],
          "Query trace diagnostic native/certification accounting");
      if(!native)Require(nativeQueries==0&&actual[3][at+1]==1,"Query trace diagnostic software-only one call");
    }
  }
  std::cout<<"Surface cache query trace diagnostic contract "<<(diagnosticMismatches?"FAILED":"passed")<<": cases="<<cases.size()
      <<" helpers=4 compared_fp32_channels="<<diagnosticChannels<<" mismatched_channels="<<diagnosticMismatches
      <<" counter_channels="<<diagnosticCounterChannels<<" independent_interior_and_miss_counts=1 local_uint_counters=1 atomics=0 timed=0\n";
  Require(!diagnosticMismatches,"Query trace instrumentation changed original helper output");
  std::cout<<"Native origin intersection contract "<<(originMismatches?"FAILED":"passed")<<": cases="<<cases.size()
      <<" compared_fp32_channels="<<originChannels<<" mismatched_channels="<<originMismatches
      <<" opposed_interior_ties=2 rotated_grazing=128 masked_near_far=32 partial_exhaustion=2 any_hit=0 legacy_native_diagnostic_mismatches="<<legacyOriginMismatches
      <<" gating_helper=SCNativeOriginIntersect legacy_helper_not_gated=1\n";
  std::cout<<"Reused native origin intersection contract "<<((reusedOriginMismatches||reusedLegacyMismatches)?"FAILED":"passed")<<": cases="<<cases.size()
      <<" compared_fp32_channels="<<reusedOriginChannels<<" mismatched_channels="<<reusedOriginMismatches
      <<" legacy_certified_fp32_channels="<<reusedLegacyChannels<<" legacy_certified_mismatched_channels="<<reusedLegacyMismatches
      <<" opposed_interior_ties=2 rotated_grazing=128 masked_near_far=32 partial_exhaustion=2 any_hit=0"
      <<" gating_helper=SCNativeOriginIntersectReusingCertification original_certified_helper_preserved=1\n";
  Require(!mismatches&&!candidateMismatches&&!candidateLegacyMismatches&&!candidateFetchMismatches&&
      !directMismatches&&!directLegacyMismatches&&!directFetchMismatches&&
      !originMismatches&&!reusedOriginMismatches&&!reusedLegacyMismatches,
      "Native probe/origin strict nearest/normal/endpoint/edge/tie contract");
}
