// Actual receiver-origin prepass versus the fused Gather's unchanged source
// query, including the real optional Gather texture load. This is a low-level
// contract replay, never part of a timed renderer frame.
#include "Render/Private/Pipeline/surface_cache_contact_shaders.h"
#include "Render/Public/Pipeline/screen_probe_placement.h"
#include "Render/Public/Pipeline/surface_cache_gi.h"
#include <glm/gtc/matrix_transform.hpp>

void SurfaceCacheReceiverTracePackingContract(RHIDevice& device) {
  ResourceBuilder build(device);const auto mesh=build.FullscreenMesh();
  struct Case {std::string name;std::array<glm::vec4,7> pages{};};
  Case zero;for(auto& page:zero.pages)page=glm::vec4(0);
  zero.name="zero";zero.pages[0]={1,128,32,127};std::vector<Case> cases{zero};
  for(float value:std::array<float,8>{0,1,2,127,128,134,4094,4095}) {
    Case c=zero;c.name="pair_boundary_"+std::to_string(int(value));
    for(unsigned page=1;page<7;++page)c.pages[page]=glm::vec4(value);cases.push_back(c);
  }
  Case c=zero;c.name="independent_page_component_mapping";
  for(unsigned page=1;page<7;++page)for(unsigned component=0;component<4;++component)
    c.pages[page][component]=float((page-1)*4+component+1);
  cases.push_back(c);
  for(unsigned high=0;high<2;++high) {
    c=zero;c.name=high?"highest_high_digit":"highest_low_digit";
    for(unsigned page=1;page<7;++page)for(unsigned component=0;component<4;++component)
      c.pages[page][component]=(component%2==high)?4095.f:0.f;
    cases.push_back(c);
  }
  constexpr std::array<const char*,5> invalidNames{"negative","fractional","overflow","infinite","nan"};
  const std::array<float,5> invalidValues{-1.f,.5f,4096.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
  // Exercise every source counter independently. A large low digit must not
  // silently carry into the high digit, and a nonfinite value must not be
  // converted to an integer or encoded as a misleading finite counter pair.
  for(unsigned page=1;page<7;++page)for(unsigned component=0;component<4;++component)for(unsigned invalid=0;invalid<5;++invalid) {
    c=zero;c.name=std::string(invalidNames[invalid])+"_page"+std::to_string(page)+"_component"+std::to_string(component);
    c.pages[page][component]=invalidValues[invalid];cases.push_back(c);
  }
  std::array<RenderResourceHandle<RHITextureSpec>,4> outputs;
  for(auto& output:outputs)output=build.Texture(unsigned(cases.size()),1,RHITextureFormat::RGBA32F);
  const auto target=build.TargetColors(outputs);const auto input=build.Uniform(sizeof(c.pages));
  const auto program=build.FullscreenPipeline(R"(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);})",
      "#version 450 core\n"+SurfaceCacheDetail::ContactTraceCounterPackingGLSL()+R"(
layout(std140,binding=0) uniform TracePackingCase {vec4 tpPages[7];};
layout(location=0) out vec4 sampling;
layout(location=1) out vec4 packed12;
layout(location=2) out vec4 packed34;
layout(location=3) out vec4 packed56;
void main(){sampling=tpPages[0];packed12=CTTracePackTwoPages(tpPages[1],tpPages[2]);
  packed34=CTTracePackTwoPages(tpPages[3],tpPages[4]);packed56=CTTracePackTwoPages(tpPages[5],tpPages[6]);})");
  auto encoder=device.BeginFrame({3120,64,32});
  Require(encoder.SetRenderTarget({target,unsigned(cases.size()),1,RHICommand::ClearColor,glm::vec4(0)})&&
      encoder.BindPipeline(program)&&encoder.BindMesh(mesh)&&encoder.BindUniformBuffer(input,0),"Receiver trace packing bindings");
  for(std::size_t index=0;index<cases.size();++index)
    Require(encoder.UpdateUniformBufferBytes(input,cases[index].pages.data(),sizeof(cases[index].pages))&&
        encoder.SetViewport({int(index),0,1,1})&&encoder.DrawIndexed({3}),"Receiver trace packing boundary draw");
  Require(encoder.End(false),"Receiver trace packing frame end");bool done=false;
  device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[&]{done=true;});
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
  while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Receiver trace packing timeout");
    if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
  std::array<std::vector<float>,4> actual;std::uint64_t errors=0;
  OnThread(device,[&]{for(unsigned part=0;part<4;++part)actual[part]=device.BackendDiagnostics()->ReadTexture(outputs[part]);
    errors=device.BackendDiagnostics()->ValidationErrorCount();});
  Require(!errors,"Receiver trace packing Vulkan validation error");
  for(const auto& output:actual)Require(output.size()==cases.size()*4,"Receiver trace packing readback extent");
  std::size_t packedChecks=0,decodedChecks=0,invalidPairs=0;
  for(std::size_t index=0;index<cases.size();++index) {
    const auto& item=cases[index];
    for(unsigned component=0;component<4;++component)
      Require(std::bit_cast<std::uint32_t>(actual[0][index*4+component])==std::bit_cast<std::uint32_t>(item.pages[0][component]),
          "Receiver trace MRT changed the original page-zero FP32 payload");
    for(unsigned attachment=1;attachment<4;++attachment)for(unsigned component=0;component<4;++component) {
      const unsigned page=attachment*2-1+component/2,lowComponent=2*(component%2);
      const double low=item.pages[page][lowComponent],high=item.pages[page][lowComponent+1];
      const bool valid=std::isfinite(low)&&std::isfinite(high)&&low>=0&&high>=0&&low<4096&&high<4096&&low==std::floor(low)&&high==std::floor(high);
      const float expected=valid?float(low+4096.0*high):-1.f,value=actual[attachment][index*4+component];
      if(std::bit_cast<std::uint32_t>(value)!=std::bit_cast<std::uint32_t>(expected))
        std::cout<<"Receiver trace packing mismatch case="<<item.name<<" attachment="<<attachment<<" component="<<component
            <<" expected="<<expected<<" actual="<<value<<std::endl;
      Require(std::bit_cast<std::uint32_t>(value)==std::bit_cast<std::uint32_t>(expected),"Receiver trace packing boundary/overflow contract changed");
      ++packedChecks;
      if(valid) {
        Require(std::isfinite(value)&&value>=0&&value<16777216.f&&value==std::floor(value),"Receiver trace packed value is not an exact finite integer");
        const auto encoded=std::uint32_t(value);
        Require(encoded%4096==std::uint32_t(low)&&encoded/4096==std::uint32_t(high),"Receiver trace page counter did not decode exactly");
        decodedChecks+=2;
      } else {Require(value==-1.f,"Receiver trace invalid counter did not produce a strict failure sentinel");++invalidPairs;}
    }
  }
  std::cout<<"Surface cache receiver trace packing contract passed: cases="<<cases.size()<<" packed_fp32_channels="<<packedChecks
      <<" decoded_integer_channels="<<decodedChecks<<" invalid_pairs="<<invalidPairs
      <<" mrt_attachments=4 page0_exact=1 radix=4096 overflow_nonfinite_fraction_negative_rejected=1 maximum_24bit_integer_exact=1 timed=0\n";
}

void SurfaceCacheReceiverOriginContract(RHIDevice& device) {
  bool nativeAvailable=false;
  OnThread(device,[&]{nativeAvailable=device.BackendDiagnostics()->HardwareRayTracingAvailable();});
  ResourceBuilder build(device);const auto mesh=build.FullscreenMesh();
  struct Case {
    std::string name;std::vector<glm::vec4> geometry;int nodes=1,triangles=1;
    glm::vec4 position{0,0,-.5f,1},normal{0,0,-1,1},fresh{0,8,0,0};
    float mask=1,bias=.02f;bool perspective=false;glm::vec4 safety{.5f,.00002f,1,0};
    std::array<bool,2> expected{true,true};bool clipped=false;
  };
  const auto plane=[](float z,bool positive=false,float shift=0.f) {
    const std::array<glm::vec3,3> p{glm::vec3(shift-1,-1,z),glm::vec3(shift,1,z),glm::vec3(shift+1,-1,z)};
    return std::array<glm::vec4,3>{glm::vec4(p[0],0),glm::vec4(p[positive?2:1],0),glm::vec4(p[positive?1:2],0)};
  };
  const auto single=[&](const char* name,float z=-.5f,bool positive=false) {
    Case c;c.name=name;c.geometry.assign(7,glm::vec4(0));
    c.geometry[0]={-1,-1,z,0};c.geometry[1]={1,1,z,-1};const auto p=plane(z,positive);
    std::copy(p.begin(),p.end(),c.geometry.begin()+2);return c;
  };
  const auto blocked=[&](const char* name,float gap) {
    Case c=single(name);c.triangles=2;c.geometry.resize(12,glm::vec4(0));
    c.geometry[0].z=-.5f-gap;c.geometry[1].w=-2;
    const auto p=plane(-.5f-gap,true);std::copy(p.begin(),p.end(),c.geometry.begin()+7);return c;
  };
  std::vector<Case> cases;cases.push_back(single("orthographic_full_bias"));
  auto c=single("mask_inactive");c.mask=0;c.expected={false,false};cases.push_back(c);
  c=single("fresh_only_selected");c.mask=0;c.fresh.x=1;c.expected={false,true};cases.push_back(c);
  c=single("moving_full_current_selected");c.mask=0;c.fresh.z=24;c.expected={false,true};cases.push_back(c);
  c=single("malformed_fresh_x");c.mask=0;c.fresh.x=std::numeric_limits<float>::quiet_NaN();c.expected={false,false};cases.push_back(c);
  c=single("malformed_moving_tag");c.mask=0;c.fresh.z=31;c.expected={false,false};cases.push_back(c);
  c=single("invalid_position_coverage");c.position.w=0;c.expected={false,false};cases.push_back(c);
  c=single("invalid_normal_coverage");c.normal.w=0;c.expected={false,false};cases.push_back(c);
  c=single("zero_normal");c.normal={0,0,0,1};c.expected={false,false};cases.push_back(c);
  c=single("nan_position");c.position.x=std::numeric_limits<float>::quiet_NaN();c.expected={false,false};cases.push_back(c);
  c=single("nan_normal");c.normal.x=std::numeric_limits<float>::quiet_NaN();c.expected={false,false};cases.push_back(c);
  c=single("empty_geometry");c.nodes=0;c.triangles=0;c.expected={false,false};cases.push_back(c);
  c=single("source_box_miss");c.position.x=4;c.expected={false,false};cases.push_back(c);
  c=single("source_triangle_miss");c.position.x=.75f;c.position.y=.75f;c.expected={false,false};cases.push_back(c);
  c=single("source_backface",-.5f,true);c.expected={false,false};cases.push_back(c);
  c=single("source_normal_mismatch");c.normal={1,0,0,1};c.expected={false,false};cases.push_back(c);
  c=single("source_outside_anchor_interval",-.498f);c.expected={false,false};cases.push_back(c);
  c=single("source_inside_plane_tolerance",-.4999f);cases.push_back(c);
  c=blocked("clearance_clipped",.005f);c.clipped=true;cases.push_back(c);
  c=single("zero_requested_clearance");c.bias=0;c.expected={false,false};cases.push_back(c);
  c=blocked("clearance_upper_endpoint",.02002f);cases.push_back(c);
  c=blocked("clearance_inside_upper_endpoint",.0199f);c.clipped=true;cases.push_back(c);
  c=single("source_edge");c.position.y=-1;cases.push_back(c);
  c=single("source_vertex");c.position.x=-1;c.position.y=-1;cases.push_back(c);
  c=single("source_expanded_edge_outside_bounds");c.position.y=-1.000001f;c.expected={false,false};cases.push_back(c);
  c=single("depth_quantization_bounds");c.safety.z=0;cases.push_back(c);
  c=single("perspective_full_bias",-2,true);c.position.z=-2;c.normal.z=1;c.perspective=true;cases.push_back(c);
  c=single("perspective_quantization",-2,true);c.position.z=-2;c.normal.z=1;c.perspective=true;c.safety.z=0;cases.push_back(c);
  c=single("perspective_behind_camera",2,true);c.position.z=2;c.normal.z=1;c.perspective=true;c.expected={false,false};cases.push_back(c);
  // Synthetic bounded-index cyclic graph: native can find the packed source
  // triangle, but exact source certification must reject traversal exhaustion.
  c=single("source_traversal_exhaustion");c.nodes=2;c.geometry.assign(9,glm::vec4(0));
  c.geometry[0]={-1,-1,-1,1};c.geometry[1]={1,1,0,1};
  c.geometry[2]=c.geometry[0];c.geometry[3]=c.geometry[1];
  const auto exhaustionTriangle=plane(-.5f);std::copy(exhaustionTriangle.begin(),exhaustionTriangle.end(),c.geometry.begin()+4);
  c.expected={false,false};cases.push_back(c);

  const auto texture=[&](unsigned width,const std::vector<glm::vec4>& pixels) {
    CreateRHITextureSpec desc;desc.width=width;desc.height=1;desc.mipmaps=false;
    desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;
    desc.filterMode=RHIFilterMode::Nearest;desc.data=pixels.data();
    return build.Create<RHITextureSpec>([&](auto callback){device.async_CreateTexture(desc,callback);});
  };
  std::vector<RenderResourceHandle<RHITextureSpec>> geometries;
  std::vector<glm::vec4> positions,normals,masks,fresh;
  for(const auto& item:cases) {
    auto pixels=item.geometry;pixels.resize(1024,glm::vec4(0));geometries.push_back(texture(1024,pixels));
    positions.push_back(item.position);normals.push_back(item.normal);masks.emplace_back(item.mask,0,0,0);fresh.push_back(item.fresh);
  }
  const auto positionTexture=texture(unsigned(cases.size()),positions),normalTexture=texture(unsigned(cases.size()),normals);
  const auto maskTexture=texture(unsigned(cases.size()),masks),freshTexture=texture(unsigned(cases.size()),fresh);
  const auto zeroTexture=texture(unsigned(cases.size()),std::vector<glm::vec4>(cases.size(),glm::vec4(0)));
  const auto placement=build.Uniform(sizeof(ScreenProbePlacementConstants)),cache=build.Uniform(sizeof(SurfaceCacheGiConstants));
  const auto contact=build.Uniform(sizeof(SurfaceCacheContactConstants)),safety=build.Uniform(sizeof(SurfaceCacheOriginSafetyConstants));
  std::array<RenderResourceHandle<RHITextureSpec>,3> outputs;
  std::array<RenderResourceHandle<RenderTargetSpec>,3> targets;
  for(unsigned part=0;part<3;++part) {
    outputs[part]=build.Texture(unsigned(cases.size()),1,RHITextureFormat::RGBA32F);
    targets[part]=build.Target(outputs[part],{});
  }
  const std::string vertex=R"(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);})";
  const auto originOutput=[](std::string source) {
    // Keep the complete production Gather main through source validation and
    // its normal unknown/skip branches. Only successful launches return their
    // origin before any lighting rays; no replacement source-safety algorithm.
    const std::string token="vec3 axis=abs(n.z)<.99?";const auto at=source.find(token);
    Require(at!=std::string::npos,"Receiver-origin fixture lost Gather launch boundary");
    source.insert(at,"outColor=vec4(origin,1);return;\n    ");return source;
  };
  const auto wait=[&](const bool& done) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Receiver-origin contract timeout");
      if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
  };
  std::uint64_t frame=3100;std::size_t comparedChannels=0,validLaunches=0,unknownLaunches=0,variants=0;
  // Old/fast software addressing, accepted native+fast addressing, and the
  // retained optional native certification reuse. Hardware absence skips only
  // the native variants; the complete software contract always runs.
  for(unsigned helper=0;helper<4;++helper) {
    if(helper>=2&&!nativeAvailable)continue;
    const bool native=helper>=2,reuseCertification=helper==3,fastAddress=helper!=0;
    for(unsigned freshEnabled=0;freshEnabled<2;++freshEnabled) {
      std::array<RenderResourceHandle<PipelineSpec>,3> programs;
      programs[0]=build.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactOriginShader(freshEnabled!=0,native,reuseCertification,false,fastAddress));
      programs[1]=build.FullscreenPipeline(vertex,originOutput(SurfaceCacheDetail::ContactGatherShader(false,freshEnabled!=0,false,native,reuseCertification,false,fastAddress)));
      programs[2]=build.FullscreenPipeline(vertex,originOutput(SurfaceCacheDetail::ContactGatherShader(false,freshEnabled!=0,false,native,reuseCertification,false,fastAddress,true)));
      auto encoder=device.BeginFrame({frame++,64,32});
      const auto update=[&](auto buffer,const auto& value) {
        const auto* bytes=reinterpret_cast<const std::byte*>(&value);
        for(unsigned offset=0;offset<sizeof(value);offset+=RHICommand::MaxInlineUniformBytes) {
          const auto length=std::uint32_t(std::min<std::size_t>(RHICommand::MaxInlineUniformBytes,sizeof(value)-offset));
          Require(encoder.UpdateUniformBufferBytes(buffer,bytes+offset,length,offset),"Receiver-origin fixture uniform upload");
        }
      };
      for(unsigned part=0;part<3;++part) {
        Require(encoder.SetRenderTarget({targets[part],unsigned(cases.size()),1,RHICommand::ClearColor,glm::vec4(0)})&&
            encoder.BindPipeline(programs[part])&&encoder.BindMesh(mesh)&&encoder.BindUniformBuffer(placement,ScreenProbePlacementBinding)&&
            encoder.BindUniformBuffer(cache,SurfaceCacheGiBinding)&&encoder.BindUniformBuffer(contact,SurfaceCacheContactBinding)&&
            encoder.BindUniformBuffer(safety,SurfaceCacheOriginSafetyBinding)&&encoder.BindTexture(zeroTexture,0)&&
            encoder.BindTexture(maskTexture,1)&&encoder.BindTexture(positionTexture,2)&&encoder.BindTexture(normalTexture,3)&&
            encoder.BindTexture(zeroTexture,4)&&encoder.BindTexture(part==2?outputs[0]:zeroTexture,5)&&encoder.BindTexture(freshTexture,15),
            "Receiver-origin fixture bindings");
        for(std::size_t index=0;index<cases.size();++index) {
          const auto& item=cases[index];ScreenProbePlacementConstants sp;sp.screen={64,32,0,0};sp.traceParams.z=item.bias;
          if(item.perspective) {
            sp.viewProjection=glm::perspective(glm::radians(60.f),2.f,.1f,10.f);sp.inverseProjection=glm::inverse(sp.viewProjection);
          }
          SurfaceCacheGiConstants sc;sc.geometry={item.nodes*2,1024,item.nodes,item.triangles};
          SurfaceCacheContactConstants ct;ct.options.w=freshEnabled?16:0;
          SurfaceCacheOriginSafetyConstants os;os.safety=item.safety;
          update(placement,sp);update(cache,sc);update(contact,ct);update(safety,os);
          Require(encoder.BindTexture(geometries[index],9)&&encoder.SetViewport({int(index),0,1,1})&&encoder.DrawIndexed({3}),
              "Receiver-origin fixture case draw");
        }
      }
      Require(encoder.End(false),"Receiver-origin fixture frame end");bool done=false;
      device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[&]{done=true;});wait(done);
      std::array<std::vector<float>,3> actual;std::uint64_t errors=0;
      OnThread(device,[&]{for(unsigned part=0;part<3;++part)actual[part]=device.BackendDiagnostics()->ReadTexture(outputs[part]);
        errors=device.BackendDiagnostics()->ValidationErrorCount();});
      Require(!errors,"Receiver-origin fixture Vulkan validation error");
      for(const auto& result:actual)Require(result.size()==cases.size()*4,"Receiver-origin fixture readback extent");
      for(std::size_t index=0;index<cases.size();++index) {
        const auto& item=cases[index];const auto at=index*4;const bool valid=item.expected[freshEnabled];
        if(actual[0][at+3]!=float(valid))std::cout<<"Receiver-origin independent validity mismatch helper="<<helper<<" fresh="<<freshEnabled
            <<" case="<<item.name<<" expected="<<valid<<" actual="<<actual[0][at+3]<<std::endl;
        Require(actual[0][at+3]==float(valid),"Receiver-origin independent valid/unknown expectation");
        if(valid) {
          ++validLaunches;for(unsigned component=0;component<3;++component)Require(std::isfinite(actual[0][at+component]),"Receiver-origin valid position nonfinite");
          if(index==0||item.name=="perspective_full_bias") {
            const auto expected=glm::vec3(item.position)+glm::vec3(item.normal)*item.bias;
            for(unsigned component=0;component<3;++component)Require(std::abs(actual[0][at+component]-expected[component])<1e-6f,
                "Receiver-origin independent full-bias position expectation");
          }
          if(item.clipped)Require(actual[0][at+2]<item.position.z&&actual[0][at+2]>item.position.z-.02f,
              "Receiver-origin clearance fixture did not clip the requested bias");
        } else {++unknownLaunches;for(unsigned component=0;component<4;++component)Require(actual[0][at+component]==0,
            "Receiver-origin failed/skipped prepass pixel must be zero");}
        for(unsigned part=1;part<3;++part)for(unsigned component=0;component<4;++component) {
          const auto before=std::bit_cast<std::uint32_t>(actual[1][at+component]),after=std::bit_cast<std::uint32_t>(actual[part==1?0:2][at+component]);
          if(before!=after)std::cout<<"Receiver-origin FP32 mismatch helper="<<helper<<" fresh="<<freshEnabled<<" case="<<item.name
              <<" part="<<part<<" component="<<component<<" fused="<<actual[1][at+component]<<" optional="<<actual[part==1?0:2][at+component]<<std::endl;
          Require(before==after,"Receiver-origin prepass/load changed fused FP32 origin/validity");++comparedChannels;
        }
      }
      ++variants;
    }
  }
  Require(validLaunches>0&&unknownLaunches>0,"Receiver-origin fixture lacks valid or unknown launches");
  std::cout<<"Surface cache receiver origin contract passed: cases="<<cases.size()<<" helper_fresh_variants="<<variants
      <<" compared_fp32_channels="<<comparedChannels<<" valid="<<validLaunches<<" unknown="<<unknownLaunches
      <<" prepass_rgba32f=1 actual_optional_gather_load=1 original_fused_path_preserved=1 timed=0\n";
  SurfaceCacheReceiverTracePackingContract(device);
}
