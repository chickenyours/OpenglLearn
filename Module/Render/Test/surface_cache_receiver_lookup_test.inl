// Receiver-only immutable anchor reuse and guarded cache atlas addressing.
// This contract does not change the ray budget or claim physical fetch counts.
#include "Render/Private/Pipeline/surface_cache_shaders.h"
void SurfaceCacheReceiverLookupContract(RHIDevice& device) {
  const auto addressOnlySource=SurfaceCacheDetail::LookupGLSL(false,true);
  Require(SurfaceCacheDetail::LookupGLSL(true,true)==SurfaceCacheDetail::LookupGLSL(true,false),
      "Receiver context-plus-address prototype lost priority");
  Require(addressOnlySource.find("SCLookupObjectContext")==std::string::npos&&
      addressOnlySource.find("bool SCLookupObject(vec3 p,vec3 n,int object,out int result)")!=std::string::npos&&
      addressOnlySource.find("vec4 anchorNormal=SCAttribute(scAtlas.y+anchor),anchorPosition=SCAttribute(anchor);")!=std::string::npos,
      "Receiver address-only variant changed anchor context/fetch policy");
  ResourceBuilder build(device);const auto mesh=build.FullscreenMesh();
  struct Case {std::string name;glm::vec3 position{0},normal{0,0,1};int object=0,flags=3,mutation=0;};
  std::vector<Case> cases;
  const auto add=[&](const char* name,int mutation=0,glm::vec3 position=glm::vec3(0),int flags=3,int object=0) {
    Case c;c.name=name;c.mutation=mutation;c.position=position;c.flags=flags;c.object=object;cases.push_back(c);
  };
  add("filtered_uniform");add("nearest_uniform",0,glm::vec3(0),1);add("filtered_black",1);
  add("filtered_gradient",2,{.2f,-.125f,0});add("nearest_gradient",2,{.2f,-.125f,0},1);
  add("clamped_edge",2,{1,0,0});add("clamped_corner",2,{1,1,0});
  add("negative_edge",2,{-1,-1,0});add("outside_projection",0,{1.01f,0,0});
  add("cache_disabled",0,glm::vec3(0),0);add("object_missing",0,glm::vec3(0),3,1);
  add("object_search_all",2,{.2f,0,0},3,-1);add("one_position_hole",3);add("all_position_holes",4);
  add("anchor_light_unknown",5);add("other_light_unknown",6);add("anchor_light_nonfinite",7);
  add("invalid_anchor_card",8);add("normal_rejected",9);add("depth_equal_tolerance",10);
  add("nearby_layer",11);add("zero_bilinear_weight_unknown",12,{.25f,.25f,0});
  add("lighting_nonanchor_nan",13);add("world_bounds_rejected",0,{3,0,0});
  const std::array<int,3> widths{1024,17,257};
  struct Resources {RenderResourceHandle<RHITextureSpec> attributes,metadata,lighting;};
  std::array<std::vector<Resources>,3> resources;
  const auto texture=[&](std::vector<glm::vec4> pixels,int width) {
    pixels.resize((pixels.size()+size_t(width)-1)/size_t(width)*size_t(width),glm::vec4(0));
    CreateRHITextureSpec desc;desc.width=uint32_t(width);desc.height=uint32_t(pixels.size()/size_t(width));
    desc.mipmaps=false;desc.data=pixels.data();desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;
    desc.filterMode=RHIFilterMode::Nearest;
    return build.Create<RHITextureSpec>([&](auto callback){device.async_CreateTexture(desc,callback);});
  };
  constexpr int resolution=4,plane=6*resolution*resolution,anchor=4*resolution*resolution+5;
  for(size_t w=0;w<widths.size();++w)for(const auto& c:cases) {
    std::vector<glm::vec4> attributes(plane*3,glm::vec4(0)),metadata(6*12+1,glm::vec4(0));
    std::vector<glm::vec4> lighting(plane,glm::vec4(.25f,.5f,.75f,1));
    for(int card=0;card<6;++card) {
      for(int column=0;column<4;++column){glm::vec4 v(0);v[column]=1;metadata[card*12+column]=v;}
      metadata[card*12+4]={-1,-1,-1,0};metadata[card*12+5]={1,1,1,float(card*16)};
      glm::vec3 facing(0);facing[card/2]=(card&1)?-1.f:1.f;
      metadata[card*12+6]=glm::vec4(facing,.5f);metadata[card*12+9]={-2,-2,-2,float(resolution)};
      metadata[card*12+10]={2,2,2,0};
      const int axis=card/2,u=(axis+1)%3,v=(axis+2)%3;
      for(int y=0;y<resolution;++y)for(int x=0;x<resolution;++x) {
        const int index=card*16+y*4+x;glm::vec3 p(0);
        p[u]=(float(x)+.5f)*.5f-1;p[v]=(float(y)+.5f)*.5f-1;
        attributes[index]=glm::vec4(p,1);attributes[plane+index]=glm::vec4(facing,float(card+1));
        if(c.mutation==2)lighting[index]={float(x+1)*.125f,float(y+1)*.125f,float(card+1)*.0625f,1};
      }
    }
    if(c.mutation==1)for(auto& value:lighting)value=glm::vec4(0,0,0,1);
    if(c.mutation==3)attributes[anchor].w=0;
    if(c.mutation==4)for(int i=0;i<plane;++i)attributes[i].w=0;
    if(c.mutation==5)lighting[anchor].w=0;
    if(c.mutation==6)lighting[anchor+1].w=0;
    if(c.mutation==7)lighting[anchor].x=std::numeric_limits<float>::quiet_NaN();
    if(c.mutation==8)for(int i=4*16;i<5*16;++i)attributes[plane+i].w=0;
    if(c.mutation==9)for(int i=0;i<plane;++i)attributes[plane+i].z=-attributes[plane+i].z;
    if(c.mutation==10)for(int i=4*16;i<5*16;++i)attributes[i].z=.5f;
    if(c.mutation==11)attributes[anchor+1].z=.3f;
    if(c.mutation==12)lighting[anchor].w=0;
    if(c.mutation==13)lighting[anchor+1].y=std::numeric_limits<float>::quiet_NaN();
    resources[w].push_back({texture(attributes,widths[w]),texture(metadata,widths[w]),texture(lighting,widths[w])});
  }
  struct alignas(16) Input {glm::vec4 position,normal;glm::ivec4 options;};
  const auto input=build.Uniform(sizeof(Input)),cache=build.Uniform(sizeof(SurfaceCacheGiConstants));
  std::array<RenderResourceHandle<RHITextureSpec>,4> outputs;
  for(auto& output:outputs)output=build.Texture(uint32_t(cases.size()),1,RHITextureFormat::RGBA32F);
  const auto target=build.TargetColors(outputs);
  const std::string vertex=R"(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);})";
  const std::string body=R"(
layout(std140,binding=0) uniform ReceiverLookupCase {vec4 rcPosition,rcNormal;ivec4 rcOptions;};
layout(location=0) out vec4 value;layout(location=1) out vec4 selected;
layout(location=2) out vec4 anchorPosition;layout(location=3) out vec4 anchorNormal;
void main(){vec3 radiance;float coverage;int anchor;
  bool found=SCSampleLightingObject(rcPosition.xyz,rcNormal.xyz,rcOptions.x,radiance,coverage);
  bool known=SCLookupObject(rcPosition.xyz,rcNormal.xyz,rcOptions.x,anchor);
  value=vec4(radiance,found?coverage:-1);selected=vec4(found?1:0,known?1:0,float(anchor),float(SCTriangleOwner(0)));
  anchorPosition=known?SCAttribute(anchor):vec4(0);anchorNormal=known?SCAttribute(scAtlas.y+anchor):vec4(0);
})";
  std::array<RenderResourceHandle<PipelineSpec>,3> programs;
  for(unsigned mode=0;mode<3;++mode)programs[mode]=build.FullscreenPipeline(vertex,
      std::string("#version 450 core\n")+SurfaceCacheDetail::LookupGLSL(mode==1,mode==2)+body);
  uint64_t frameIndex=7800;size_t channels=0,mismatches=0,simpleChecks=0;
  size_t addressOnlyChannels=0,addressOnlyMismatches=0,addressOnlySimpleChecks=0;
  for(size_t w=0;w<widths.size();++w) {
    std::array<std::vector<float>,4> baseline;
    for(unsigned mode=0;mode<3;++mode) {
      auto e=device.BeginFrame({frameIndex++,64,32});
      Require(e.SetRenderTarget({target,uint32_t(cases.size()),1,RHICommand::ClearColor,glm::vec4(0)})&&
          e.BindMesh(mesh)&&e.BindPipeline(programs[mode])&&e.BindUniformBuffer(input,0)&&
          e.BindUniformBuffer(cache,SurfaceCacheGiBinding),"Receiver lookup contract bindings");
      for(size_t i=0;i<cases.size();++i) {
        const auto& c=cases[i];SurfaceCacheGiConstants sc;sc.atlas={widths[w],plane,plane,6};
        sc.geometry={0,1024,0,1};sc.parameters.w=float(c.flags);
        Input values{glm::vec4(c.position,0),glm::vec4(c.normal,0),glm::ivec4(c.object,0,0,0)};
        const auto* bytes=reinterpret_cast<const std::byte*>(&sc);
        for(uint32_t offset=0;offset<sizeof(sc);offset+=RHICommand::MaxInlineUniformBytes) {
          const auto count=uint32_t(std::min<size_t>(RHICommand::MaxInlineUniformBytes,sizeof(sc)-offset));
          Require(e.UpdateUniformBufferBytes(cache,bytes+offset,count,offset),"Receiver lookup constants");
        }
        Require(e.UpdateUniformBuffer(input,values)&&e.BindTexture(resources[w][i].attributes,12)&&
            e.BindTexture(resources[w][i].metadata,13)&&e.BindTexture(resources[w][i].lighting,14)&&
            e.SetViewport({int32_t(i),0,1,1})&&e.DrawIndexed({3}),"Receiver lookup case draw");
      }
      Require(e.End(false),"Receiver lookup contract frame end");bool done=false;
      device.async_SubmitFrameCommands(e.GetCommandBuffer(),[&]{done=true;});
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
      while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Receiver lookup contract timeout");
        if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
      std::array<std::vector<float>,4> actual;uint64_t errors=0;
      OnThread(device,[&]{for(size_t part=0;part<actual.size();++part)actual[part]=device.BackendDiagnostics()->ReadTexture(outputs[part]);
        errors=device.BackendDiagnostics()->ValidationErrorCount();});
      Require(!errors,"Receiver lookup contract Vulkan validation error");
      for(const auto& part:actual)Require(part.size()==cases.size()*4,"Receiver lookup contract extent");
      for(size_t i=0;i<3;++i) {
        const size_t at=i*4;const glm::vec3 color=i==2?glm::vec3(0):glm::vec3(.25f,.5f,.75f);
        Require(actual[0][at]==color.x&&actual[0][at+1]==color.y&&actual[0][at+2]==color.z&&
            actual[0][at+3]==1&&actual[1][at]==1&&actual[1][at+1]==1&&actual[1][at+2]==float(anchor),
            "Receiver lookup independent uniform/black/anchor expectation");
        if(mode==2)++addressOnlySimpleChecks;else ++simpleChecks;
      }
      Require(actual[0][9*4+3]==-1&&actual[0][10*4+3]==-1&&actual[0][13*4+3]==-1&&
          actual[0][14*4+3]==-1&&actual[0][16*4+3]==-1,"Receiver lookup independent unavailable expectations");
      if(mode==2)addressOnlySimpleChecks+=5;else simpleChecks+=5;
      if(!mode)baseline=actual;
      else for(size_t part=0;part<actual.size();++part)for(size_t i=0;i<actual[part].size();++i) {
        if(mode==2)++addressOnlyChannels;else ++channels;
        if(std::bit_cast<uint32_t>(actual[part][i])!=std::bit_cast<uint32_t>(baseline[part][i])) {
          if(mode==2)++addressOnlyMismatches;else ++mismatches;
          std::cout<<"Receiver lookup mismatch mode="<<mode<<" width="<<widths[w]<<" case="<<cases[i/4].name
              <<" part="<<part<<" component="<<i%4<<" expected="<<baseline[part][i]<<" actual="<<actual[part][i]<<'\n';
        }
      }
    }
  }
  // Do not texture-fetch negative diagnostic indices. Split signed address
  // bits into exact 16-bit channels; negative modulo uses legacy GPU results.
  const std::array<int,15> indices{std::numeric_limits<int>::min(),-1025,-1024,-1023,-18,-17,-1,0,1,16,17,1023,1024,1025,std::numeric_limits<int>::max()};
  const std::string addressBody=R"(
layout(std140,binding=0) uniform ReceiverLookupAddressCase {vec4 rcPosition,rcNormal;ivec4 rcOptions;};
layout(location=0) out vec4 result;
void main(){
#if RC_LOOKUP_REUSED
  ivec2 address=SCAtlasAddress(rcOptions.z);
#else
  ivec2 address=ivec2(rcOptions.z%scAtlas.x,rcOptions.z/scAtlas.x);
#endif
  uvec2 bits=uvec2(address);result=vec4(bits.x&65535u,bits.x>>16,bits.y&65535u,bits.y>>16);
})";
  for(unsigned mode=0;mode<3;++mode)programs[mode]=build.FullscreenPipeline(vertex,
      std::string("#version 450 core\n#define RC_LOOKUP_REUSED ")+std::to_string(mode)+"\n"+
      SurfaceCacheDetail::LookupGLSL(mode==1,mode==2)+addressBody);
  size_t addressChannels=0,addressOnlyAddressChannels=0;
  for(int width:widths) {
    std::vector<float> baseline;
    for(unsigned mode=0;mode<3;++mode) {
      auto e=device.BeginFrame({frameIndex++,64,32});
      Require(e.SetRenderTarget({target,uint32_t(cases.size()),1,RHICommand::ClearColor,glm::vec4(0)})&&e.BindMesh(mesh)&&
          e.BindPipeline(programs[mode])&&e.BindUniformBuffer(input,0)&&e.BindUniformBuffer(cache,SurfaceCacheGiBinding),"Lookup address bindings");
      SurfaceCacheGiConstants sc;sc.atlas.x=width;
      Require(e.UpdateUniformBuffer(cache,sc),"Lookup address width");
      for(size_t i=0;i<indices.size();++i) {
        Input values{};values.options.z=indices[i];
        Require(e.UpdateUniformBuffer(input,values)&&e.SetViewport({int32_t(i),0,1,1})&&e.DrawIndexed({3}),"Lookup address case");
      }
      Require(e.End(false),"Lookup address frame end");bool done=false;
      device.async_SubmitFrameCommands(e.GetCommandBuffer(),[&]{done=true;});
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
      while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Lookup address timeout");
        if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
      std::vector<float> actual;uint64_t errors=0;
      OnThread(device,[&]{actual=device.BackendDiagnostics()->ReadTexture(outputs[0]);errors=device.BackendDiagnostics()->ValidationErrorCount();});
      Require(!errors&&actual.size()==cases.size()*4,"Lookup address readback/validation");
      if(!mode)baseline=actual;
      else for(size_t i=0;i<indices.size()*4;++i) {
        if(mode==2)++addressOnlyAddressChannels;else ++addressChannels;
        Require(actual[i]==baseline[i],"Guarded receiver atlas address changed legacy GPU expression");
      }
      for(size_t i=0;i<indices.size();++i)if(indices[i]>=0) {
        const uint32_t x=uint32_t(actual[i*4])|(uint32_t(actual[i*4+1])<<16),y=uint32_t(actual[i*4+2])|(uint32_t(actual[i*4+3])<<16);
        Require(x==uint32_t(indices[i]%width)&&y==uint32_t(indices[i]/width),"Receiver atlas independent nonnegative address");
      }
    }
  }
  std::cout<<"Surface cache receiver lookup contract "<<(mismatches?"FAILED":"passed")<<": cases="<<cases.size()
      <<" widths=1024,17,257 compared_fp32_channels="<<channels<<" mismatched_channels="<<mismatches
      <<" exact_address_channels="<<addressChannels<<" independent_expectations="<<simpleChecks
      <<" nearest,filtered,black,unknown,holes,clamped_edges,anchor_metadata,nonfinite_lighting unchanged_tap_order=1 negative_fetches=0 physical_fetch_claim=0\n";
  Require(!mismatches,"Receiver immutable lookup reuse changed FP32 output");
  std::cout<<"Surface cache receiver address-only lookup contract "<<(addressOnlyMismatches?"FAILED":"passed")<<": cases="<<cases.size()
      <<" widths=1024,17,257 compared_fp32_channels="<<addressOnlyChannels<<" mismatched_channels="<<addressOnlyMismatches
      <<" exact_address_channels="<<addressOnlyAddressChannels<<" independent_expectations="<<addressOnlySimpleChecks
      <<" original_anchor_function_and_fetches=1 context_outputs=0 unchanged_tap_order=1 negative_fetches=0 physical_fetch_claim=0\n";
  Require(!addressOnlyMismatches,"Receiver address-only lookup changed FP32 output");
}
