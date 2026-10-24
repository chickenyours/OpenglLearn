// A walkable visual study of Graveyard Keeper 2. Scene resources and all scene
// draws go through OpenglLearn Render; ApplicationWindow owns the window.
#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>
#include "rhi_bridge.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <tuple>
namespace fs=std::filesystem;
static fs::path module=GRAVEYARD_LAB_DIR;
static std::string read(const fs::path&p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot read "+p.string());return {std::istreambuf_iterator<char>(f),{}};}
static std::string shaderSource(const fs::path&p,bool include=false){
 std::istringstream lines(read(p));std::ostringstream out;std::string line;
 while(std::getline(lines,line)){
  if(include&&line.rfind("#version",0)==0)continue;
  if(line.rfind("#include \"",0)==0){auto end=line.find('"',10);if(end==std::string::npos)throw std::runtime_error("Invalid shader include");out<<shaderSource(p.parent_path()/line.substr(10,end-10),true)<<"\n";}
  else out<<line<<"\n";
 }
 return out.str();
}
static float clamp(float x,float a,float b){return std::clamp(x,a,b);}
struct V3{float x=0,y=0,z=0;};
static float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static V3 cross(V3 a,V3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static V3 normalized(V3 a){float n=std::sqrt(dot(a,a));return {a.x/n,a.y/n,a.z/n};}
struct Vertex{V3 p;float u,v;V3 n;std::array<float,4> colour{1,1,1,1};};
static float rand01(unsigned n){n=(n^61)^(n>>16);n*=9;n^=n>>4;n*=0x27d4eb2d;n^=n>>15;return (n&0xffffff)/16777215.f;}
struct Settings{
 float time=12,day=1,wind=1,water=1,leaves=1,zoom=.78f,speed=1;
 bool pause=false,light=true,shadows=true,post=true,heightDebug=false,waterDebug=false,follow=true,showMagnifier=true;
 bool legacyShadows=false,ao=true,shadowDebug=false,aoDebug=false,manualSun=false;
 float sunAzimuth=90.8f,sunElevation=70,shadowBias=.00065f,shadowPcf=1.f,aoStrength=.72f,aoRadius=5;
 float ambientScale=1.75f,sunScale=1,backScale=1,cloudStrength=1;
 float cloudDensity=.47f,cloudCutoff=.792f,cloudOpacity=.515f,cloudEdgeRange=.096f,cloudOffset=1.92f;
 bool waterReflection=true,waterRefraction=true,waterDepth=true;
 float playerX=1040,playerZ=940,playerY=0,cameraX=915,cameraZ=590,walkPhase=0;
 int direction=0;bool moving=false;
};
static V3 sunDirection(const Settings&s){
 if(s.manualSun){float az=s.sunAzimuth*.01745329252f,el=s.sunElevation*.01745329252f;return {std::cos(az)*std::cos(el),std::sin(el),std::sin(az)*std::cos(el)};}
 V3 night{.46227304f,.8660254f,.19053514f},dusk{-.57348908f,.81915204f,.01001029f},day{-.004775365f,.93969262f,.3419868f};
 float k=clamp(s.day<.42f?(s.day-.05f)/.37f:(s.day-.42f)/.58f,0,1);V3 a=s.day<.42f?night:dusk,b=s.day<.42f?dusk:day;
 return normalized({a.x+(b.x-a.x)*k,a.y+(b.y-a.y)*k,a.z+(b.z-a.z)*k});
}
struct Asset{LabRhi::Texture tex;float pivotX=.5f,pivotY=0;fs::path file;};
struct Object{std::string asset;float x,z,width,wind,seed;float footOffset=0;};
struct Model{std::vector<Vertex> vertices;float width=1,height=1,minScreenY=1e6f,maxScreenY=-1e6f,minX=1e6f,maxX=-1e6f;};
struct DepthTarget{
 LabRhi::Texture tex,depth;Render::RenderResourceHandle<Render::RenderTargetSpec> handle;
 void init(int w,int h,Render::RHIFilterMode colorFilter=Render::RHIFilterMode::Nearest){
  tex=LabRhi::createTexture(w,h,nullptr,colorFilter);
  Render::CreateTextureCommand d;d.spec.width=w;d.spec.height=h;
  d.spec.textureDataStoreType=d.spec.textureUseType=Render::RHITextureFormat::Depth32F;
  d.spec.mipmaps=false;d.spec.filterMode=Render::RHIFilterMode::Nearest;
  depth.handle=LabRhi::backend().CreateTexture(d);depth.w=w;depth.h=h;
  Render::CreateRenderTargetCommand c;c.desc.color=tex.handle;c.desc.depth=depth.handle;
  handle=LabRhi::backend().CreateRenderTarget(c);
  if(!handle.IsValid()||!depth.handle.IsValid())throw std::runtime_error("Render RHI color/depth attachment creation failed");
 }
 void bind(bool white=false,glm::vec4 clear={.10f,.15f,.13f,1}){Render::RHICommand::SetRenderTarget c;c.target=handle;c.width=tex.w;c.height=tex.h;c.clearFlags=Render::RHICommand::ClearColor|Render::RHICommand::ClearDepth;c.clearColor=white?glm::vec4{1,1,1,1}:clear;c.clearDepth=1;LabRhi::backend().SetRenderTarget(c);}
 ~DepthTarget(){if(handle.IsValid())LabRhi::backend().DeleteRenderTarget({handle});for(auto*t:{&depth,&tex})if(t->handle.IsValid())LabRhi::backend().DeleteTexture({t->handle});}
};
struct Scene{
 std::map<std::string,Asset> assets;
 std::map<std::string,Model> models;
 std::vector<Object> objects;
 LabRhi::Program sprite,transparent,post,sunDepth,cloudProgram,copyProgram;
 Render::RenderResourceHandle<Render::VertexBufferSpec> spatialMesh,screenMesh;
 using MeshKey=std::tuple<int,unsigned,float,float,float,float,float,float>;
 struct CachedMesh{Render::RenderResourceHandle<Render::VertexBufferSpec> handle;uint32_t count=0;};
 std::map<MeshKey,CachedMesh> fixedMeshes;
 struct PlantBatch{CachedMesh mesh;std::vector<size_t> visibleIndices;};
 std::map<std::string,PlantBatch> plantBatches;
 std::map<std::string,PlantBatch> reflectionPlants;
 std::map<std::string,CachedMesh> particleMeshes;
 std::map<size_t,std::vector<Vertex>> plantVertices;
 DepthTarget scene,sunMap,opaqueScene,reflectionScene;
 LabRhi::Target output,cloudMask;
 std::array<float,4> cloudNoiseInfo{},cloudAtlasInfo{};
 std::vector<Vertex> ground,plateau,cliff,ramp,bridge,waterSurface;
 int w=1280,h=720,draws=0,streamUploads=0;bool drawPlayer=true;
 bool inSunPass=false,forceShadowUpdate=false,shadowValid=false,lastDrawPlayer=true;Settings lastShadowSettings;double lastShadowWall=-1;int shadowDraws=0;unsigned shadowUpdates=0;
 bool cloudValid=false;Settings lastCloudSettings;float lastCloudOffset=0;double lastCloudWall=-1;unsigned cloudUpdates=0;
 bool reflectionPass=false,waterPass=false,reverseOpaque=false;std::map<std::string,std::vector<Vertex>> sunBatches;std::map<std::string,bool> sunBatchWind;size_t coplanarGroundTriangles=0;
 ~Scene(){sprite.release();transparent.release();post.release();sunDepth.release();cloudProgram.release();copyProgram.release();for(auto&[k,m]:fixedMeshes)if(m.handle.IsValid())LabRhi::backend().DeleteVertexBuffer({m.handle});for(auto&[k,m]:particleMeshes)if(m.handle.IsValid())LabRhi::backend().DeleteVertexBuffer({m.handle});for(auto*groups:{&plantBatches,&reflectionPlants})for(auto&[k,b]:*groups)if(b.mesh.handle.IsValid())LabRhi::backend().DeleteVertexBuffer({b.mesh.handle});for(auto m:{spatialMesh,screenMesh})if(m.IsValid())LabRhi::backend().DeleteVertexBuffer({m});for(auto&[k,a]:assets)if(a.tex.handle.IsValid())LabRhi::backend().DeleteTexture({a.tex.handle});}
 Asset& asset(const std::string&name){auto i=assets.find(name);if(i==assets.end())throw std::runtime_error("Asset role missing: "+name);return i->second;}
 bool has(const std::string&name)const{return assets.find(name)!=assets.end();}
 static void quad(std::vector<Vertex>&v,V3 a,V3 b,V3 c,V3 d,V3 normal={0,1,0},float u0=0,float v0=0,float u1=1,float v1=1){
  v.insert(v.end(),{{a,u0,v0,normal},{b,u1,v0,normal},{c,u1,v1,normal},{a,u0,v0,normal},{c,u1,v1,normal},{d,u0,v1,normal}});
 }
 static float edge(float x){return 720+std::sin(x*.017f)*14+std::sin(x*.042f)*7;}
 static float riverX(float z){return 420+std::sin(z*.005f)*72+std::sin(z*.013f)*19;}
 static float riverBed(float offset){float a=clamp(1-std::abs(offset)/90.f,0,1);return -25*a*a*(3-2*a);}
 static float bedHeight(float x,float z){float bed=riverBed(x-riverX(z));float q=std::pow((x-440.35f)/30.f,2)+std::pow((z-1135.f)/55.f,2);float a=clamp(1-q,0,1);float blend=a*a*(3-2*a);return bed+(std::max(bed,-4.f)-bed)*blend;}
 static float altitude(float x,float z){
  if(x>=850&&x<=1580&&z>=250&&z<=edge(x))return 160;
  if(x>=1050&&x<=1180&&z>=edge(x)&&z<=970)return 160*clamp((970-z)/(970-edge(x)),0,1);
  return 0;
 }
 void move(Settings&s,float dx,float dz,float dt){
  if(dx==0&&dz==0){s.moving=false;return;}
  float len=std::sqrt(dx*dx+dz*dz);dx/=len;dz/=len;float v=130*dt;
  float nx=clamp(s.playerX+dx*v,190,1750),nz=clamp(s.playerZ+dz*v,260,1410);
  float ny=altitude(nx,nz);
  // Only basic walking support: cliff crossings are blocked; the continuous
  // slope changes support height. No inventory, quests, or interaction logic.
  if(std::abs(ny-s.playerY)<std::max(8.f,v*.8f)){s.playerX=nx;s.playerZ=nz;s.playerY=ny;}
  s.direction=std::abs(dx)>std::abs(dz)?(dx>0?2:3):(dz>0?0:1);
  s.walkPhase+=dt*8;s.moving=true;
 }
 void follow(Settings&s,float dt){if(s.follow){float a=1-std::exp(-dt*4);s.cameraX+=(s.playerX-s.cameraX)*a;s.cameraZ+=(s.playerZ-s.playerY*.75f-350-s.cameraZ)*a;}}
 void loadAssets(){
  std::ifstream f(module/"Assets/scene-assets.tsv");if(!f)throw std::runtime_error("Extracted scene assets are missing. See Assets/scene-assets.tsv");
  std::string line;while(std::getline(f,line)){
   if(line.empty()||line[0]=='#')continue;std::istringstream row(line);std::string name,file,px,py;
   std::getline(row,name,'\t');std::getline(row,file,'\t');std::getline(row,px,'\t');std::getline(row,py,'\t');
   fs::path path=module/"Assets"/file;int tw,th,comp;auto*p=stbi_load(path.string().c_str(),&tw,&th,&comp,4);
   if(!p)throw std::runtime_error("Cannot decode source texture: "+path.string());
   Asset a;a.tex=LabRhi::createTexture(tw,th,p);stbi_image_free(p);a.file=path;
   if(!px.empty())a.pivotX=std::stof(px);if(!py.empty())a.pivotY=std::stof(py);
   assets.emplace(name,a);
  }
  if(assets.size()<8)throw std::runtime_error("Need at least 8 evidence-derived scene assets");
  unsigned char white[]={255,255,255,255};Asset a;a.tex=LabRhi::createTexture(1,1,white);assets.emplace("white",a);
  std::ifstream meshes(module/"Assets/scene-meshes.tsv");
  while(std::getline(meshes,line)){
   if(line.empty()||line[0]=='#')continue;std::istringstream row(line);std::string role,file;std::getline(row,role,'\t');std::getline(row,file,'\t');
   std::ifstream binary(module/"Assets"/file,std::ios::binary);uint32_t count=0;binary.read(reinterpret_cast<char*>(&count),sizeof(count));
   if(!count||count>1000000)throw std::runtime_error("Invalid extracted mesh: "+file);
   Model model;model.vertices.resize(count);binary.read(reinterpret_cast<char*>(model.vertices.data()),count*sizeof(Vertex));if(!binary)throw std::runtime_error("Truncated extracted mesh: "+file);
   for(auto&v:model.vertices)v.colour[1]=0;
   for(size_t i=0;i<model.vertices.size();i+=3){auto&a=model.vertices[i];auto&b=model.vertices[i+1];auto&c=model.vertices[i+2];
    if(std::max({a.p.y,b.p.y,c.p.y})-std::min({a.p.y,b.p.y,c.p.y})<.00001f){
     float layer=.000004f+float(models.size())*.0000005f+float((i/3)%128)*.00000005f;for(size_t k=0;k<3;k++)model.vertices[i+k].colour[1]=layer;coplanarGroundTriangles++;
    }
   }
   float minX=1e6f,maxX=-1e6f;for(auto&v:model.vertices){minX=std::min(minX,v.p.x);maxX=std::max(maxX,v.p.x);model.height=std::max(model.height,v.p.y);}
   model.width=std::max(.001f,maxX-minX);model.minX=minX;model.maxX=maxX;
   for(auto&v:model.vertices){float sy=.8f*v.p.z-.6f*v.p.y;model.minScreenY=std::min(model.minScreenY,sy);model.maxScreenY=std::max(model.maxScreenY,sy);}
   models.emplace(role,std::move(model));
  }
  // Original Texture3D slices are exported with a one-pixel wrapped border.
  // The atlas uses bilinear filtering; two adjacent slices are mixed in GLSL.
  std::ifstream noiseMeta(module/"Assets/cloud-noise.tsv");std::string atlas;
  if(!(noiseMeta>>atlas>>cloudNoiseInfo[0]>>cloudNoiseInfo[1]>>cloudNoiseInfo[2]>>cloudNoiseInfo[3]>>cloudAtlasInfo[0]>>cloudAtlasInfo[1]>>cloudAtlasInfo[2]>>cloudAtlasInfo[3]))throw std::runtime_error("Missing original cloud atlas metadata: Assets/cloud-noise.tsv");
  fs::path atlasPath=module/"Assets"/atlas;int nw,nh,nc;auto*p=stbi_load(atlasPath.string().c_str(),&nw,&nh,&nc,4);if(!p)throw std::runtime_error("Cannot decode original cloud noise atlas");
  if(nw!=int(cloudAtlasInfo[0])||nh!=int(cloudAtlasInfo[1])){stbi_image_free(p);throw std::runtime_error("Cloud atlas size disagrees with metadata");}
  Asset noise;noise.tex=LabRhi::createTexture(nw,nh,p,Render::RHIFilterMode::Linear);noise.file=atlasPath;stbi_image_free(p);assets.emplace("cloud-noise",noise);
 }
 void init(){
  sprite=LabRhi::Program(shaderSource(module/"Assets/shaders/scene.vert"),shaderSource(module/"Assets/shaders/scene.frag"),true,true);
  transparent=LabRhi::Program(shaderSource(module/"Assets/shaders/scene.vert"),shaderSource(module/"Assets/shaders/scene.frag"),true,false);
  sunDepth=LabRhi::Program(shaderSource(module/"Assets/shaders/shadow.vert"),read(module/"Assets/shaders/shadow.frag"),true,true);
  cloudProgram=LabRhi::Program(read(module/"Assets/shaders/post.vert"),read(module/"Assets/shaders/cloud.frag"));
  copyProgram=LabRhi::Program(read(module/"Assets/shaders/post.vert"),read(module/"Assets/shaders/copy.frag"),false,true,true);
  post=LabRhi::Program(read(module/"Assets/shaders/post.vert"),read(module/"Assets/shaders/post.frag"));
  Render::CreateVertexBufferCommand c;c.vertexLayout.typeSlots={Render::VertexFieldType::Vec3,Render::VertexFieldType::Vec2,Render::VertexFieldType::Vec3,Render::VertexFieldType::Vec4};c.usage=Render::BufferUsage::Stream;c.numVertex=6000;c.vertexData.resize(6000*sizeof(Vertex));spatialMesh=LabRhi::backend().CreateVertexBuffer(c);
  const float q[]={-1,-1,0,0,1,-1,1,0,1,1,1,1,-1,-1,0,0,1,1,1,1,-1,1,0,1};
  c.vertexLayout.typeSlots={Render::VertexFieldType::Vec2,Render::VertexFieldType::Vec2};c.usage=Render::BufferUsage::Static;c.numVertex=6;c.vertexData=LabRhi::bytes(q,sizeof(q));screenMesh=LabRhi::backend().CreateVertexBuffer(c);
  if(!spatialMesh.IsValid()||!screenMesh.IsValid())throw std::runtime_error("Render mesh allocation failed");
  scene.init(w,h);opaqueScene.init(w,h);reflectionScene.init(640,360,Render::RHIFilterMode::Linear);sunMap.init(1536,1536);cloudMask.resize(256,256,Render::RHIFilterMode::Linear);output.resize(w,h);loadAssets();buildTerrain();buildObjects();buildSunBatches();
  for(size_t i=0;i<objects.size();i++){
   const auto&o=objects[i];
   if(o.asset=="grass"||o.asset=="grass2"||o.asset=="flowers"||o.asset=="reeds"){
    plantBatches.try_emplace(o.asset);reflectionPlants.try_emplace(o.asset);plantVertices.emplace(i,billVertices(asset(o.asset),o.x,o.z,altitude(o.x,o.z)+o.footOffset,o.width));
   }
  }
 }
 void buildTerrain(){
  // Narrow river cross sections give the opaque pass a real bed. Land remains
  // at Y=0 and is never replaced by a screen-sized water colour quad.
  const std::array<float,19> offsets{-90,-80,-70,-60,-50,-40,-30,-20,-10,0,10,20,30,40,50,60,70,80,90};
  for(float z=0;z<1700;z+=10){float next=std::min(z+10,1700.f),r0=riverX(z),r1=riverX(next);
   quad(ground,{0,0,z},{r0-90,0,z},{r1-90,0,next},{0,0,next});
   for(size_t k=0;k+1<offsets.size();k++){float a=offsets[k],b=offsets[k+1];quad(ground,{r0+a,bedHeight(r0+a,z),z},{r0+b,bedHeight(r0+b,z),z},{r1+b,bedHeight(r1+b,next),next},{r1+a,bedHeight(r1+a,next),next});}
   quad(ground,{r0+90,0,z},{2000,0,z},{2000,0,next},{r1+90,0,next});
   quad(waterSurface,{r0-90,0,z},{r0+90,0,z},{r1+90,0,next},{r1-90,0,next});
  }
  for(float x=850;x<1580;x+=20){float nx=std::min(x+20,1580.f),ez=edge(x),enz=edge(nx);
   quad(plateau,{x,160,250},{nx,160,250},{nx,160,enz},{x,160,ez});
   if(x<1050||x>=1180)quad(cliff,{x,160,ez},{nx,160,enz},{nx,0,enz},{x,0,ez},{0,.1f,1});
  }
  for(float x=1050;x<1180;x+=10){float nx=std::min(x+10,1180.f);quad(ramp,{x,160,edge(x)},{nx,160,edge(nx)},{nx,0,970},{x,0,970},{0,.85f,.52f});}
  // A walkable timber bridge is an actual horizontal mesh over the water.
  quad(bridge,{285,6,819},{620,6,819},{620,6,885},{285,6,885});
  for(auto*vertices:{&ground,&plateau,&cliff,&waterSurface,&bridge})for(auto&v:*vertices)v.colour[1]=0;
  for(auto&v:ramp)v.colour[1]=.000001f;
 }
 void add(std::string role,float x,float z,float width,float wind=0,float seed=0,float footOffset=0){if(has(role))objects.push_back({role,x,z,width,wind,seed,footOffset});}
 void buildObjects(){
  // Deliberately authored diorama. Positions are new; the art is extracted from
  // the inspected local build, not a screenshot painted as a flat background.
  for(int i=0;i<36;i++){
   float x=120+rand01(100+i)*190,z=280+rand01(500+i)*1120;
   if(std::abs(z-850)<110)continue;
   add(i%3==0?"tree2":"tree",x,z,125+rand01(i+71)*70,7,i*3.7f);
  }
  for(int i=0;i<16;i++){float x=550+rand01(i+1400)*1080,z=290+rand01(i+1700)*175;add(i%2?"tree2":"tree",x,z,145+rand01(i+2000)*65,5,i*2.1f);}
  for(int i=0;i<14;i++){float x=1440+rand01(i+3100)*220,z=500+rand01(i+3400)*830;add("tree",x,z,180+rand01(i+4000)*60,6,i*1.9f);}
  add("tree2",670,990,180,5,32);
  add("house",1320,465,300,0,1);
  add("arch",1000,640,145);
  for(int r=0;r<2;r++)for(int c=0;c<3;c++){float x=940+c*91+r*8,z=455+r*125;add((r+c)%3==0?"grave2":"grave",x,z,37+(c%2)*5);add("gravebase",x,z+30,43);if((r+c)%4==0)add("flowerbed",x+27,z+43,27);}
  for(int i=0;i<28;i++){
   float z=450+rand01(i+442)*920,x=650+rand01(i+663)*820;
   if(std::abs(x-(1030+std::sin(z*.005f+1)*90))<90)continue;
   add(i%3==0?"rock":"bush",x,z,i%3==0?42+rand01(i)*30:60+rand01(i)*35,i%3==0?0:4,i*1.3f);
  }
  for(int i=0;i<165;i++){
   float z=330+rand01(i+9100)*1000,x=510+rand01(i+8100)*980;
   if(std::abs(x-(1030+std::sin(z*.005f+1)*90))<56)continue;
   if(x>850&&x<1580&&std::abs(z-edge(x))<30)continue;
   add(i%7==0?"flowers":(i%2==0?"grass":"grass2"),x,z,12+rand01(i+33)*14,3.2f,i*2.7f);
  }
  for(int i=0;i<20;i++){float z=430+i*48.f,x=420+std::sin(z*.005f)*72+std::sin(z*.013f)*19;add("reeds",x+(i%2?1:-1)*103,z,8+rand01(i+29)*5,5,i*3.1f);}
  for(auto p:std::array<V3,4>{{{1160,0,590},{1410,0,450},{875,0,1030},{630,0,840}}})add("lantern",p.x,p.z,30);
  add("rock",riverX(1060)+84,1060,30,0,0,-1);
  add("rock",riverX(1220)+76,1220,22,0,0,-2.5f);
 }
 void buildSunBatches(){
  // All tree instances of one atlas share a shadow draw. The only varying
  // object data used by their wind formula are carried in metadata G/B.
  for(const auto&o:objects){auto it=models.find(o.asset);if(it==models.end())continue;
   auto&m=it->second;float scale=o.width/m.width,base=altitude(o.x,o.z)+o.footOffset;auto&batch=sunBatches[o.asset];sunBatchWind[o.asset]=o.wind>0;
   for(auto q:m.vertices){q.p={q.p.x*scale+o.x,q.p.y*scale+base,q.p.z*scale+o.z};q.colour[1]=o.x+base-o.z;q.colour[2]=scale*.5f;batch.push_back(q);}
  }
 }
 void common(LabRhi::Program&p,const Settings&s,float type,float wind=0,float seed=0){
  if(inSunPass||&p==&sprite)p.useOpaque();else p.use();p.v4("uViewport",float(reflectionPass?reflectionScene.tex.w:w),float(reflectionPass?reflectionScene.tex.h:h),0,0);
  // GK2 camera positions are pixel-snapped; our +Z is source Unity -Z.
  p.v4("uCamera",std::round(s.cameraX),std::round(s.cameraZ*.8f)/.8f,s.zoom*(reflectionPass?.5f:1.f),s.time);
  p.v4("uScenePass",reflectionPass?1.f:0.f,0,0,0);
  p.v4("uObject",0,0,0,seed);p.v4("uMaterial",type,wind,.27f,0);
  p.v4("uControls",s.wind,s.water,s.leaves,s.shadows?1.f:0.f);
  float presetShadow=.7f+.3f*clamp(s.day<.42f?(.42f-s.day)/.37f:(s.day-.42f)/.58f,0,1);
  p.v4("uLighting",s.day,s.shadows||s.legacyShadows?1.f:0.f,s.light?1.f:0.f,presetShadow);
  p.v4("uTint",1,1,1,1);p.v4("uEffects",s.heightDebug?1.f:0.f,s.waterDebug?1.f:0.f,s.shadowDebug?1.f:0.f,0);
  V3 sun=sunDirection(s);
  V3 right=normalized(cross({0,1,0},sun)),up=cross(sun,right),center{1000,180,850};
  p.v4("uLightRow0",right.x/1500,right.y/1500,right.z/1500,-dot(right,center)/1500);
  p.v4("uLightRow1",up.x/1200,up.y/1200,up.z/1200,-dot(up,center)/1200);
  p.v4("uLightRow2",-sun.x/2200,-sun.y/2200,-sun.z/2200,dot(sun,center)/2200);
  p.v4("uLightRow3",0,0,0,1);p.v4("uSunDir",sun.x,sun.y,sun.z,1);
  p.v4("uShadowParams",s.shadows&&!inSunPass?1.f:0.f,s.shadowBias,s.shadowPcf,float(sunMap.tex.w));
  // Recovered serialized LightEnvironmentPreset colours; s.day retains the
  // original lab UI range and interpolates day / sunset / night keyframes.
  float k=clamp(s.day<.42f?(s.day-.05f)/.37f:(s.day-.42f)/.58f,0,1);
  auto colour=[&](const char*name,V3 night,V3 dusk,V3 day){V3 a=s.day<.42f?night:dusk,b=s.day<.42f?dusk:day;p.v4(name,a.x+(b.x-a.x)*k,a.y+(b.y-a.y)*k,a.z+(b.z-a.z)*k,1);};
  colour("uSunColor",{.305882f*.3f,.392157f*.3f,.3f},{.886792f*.8f,.41405f*.8f,.213332f*.8f},{.6f,.891509f*.6f,.674528f*.6f});
  colour("uAmbient",{.109915f,.109915f,.358491f},{.194333f,.168667f,.319f},{.415094f,.343519f,.323069f});
  colour("uBacklight",{.363208f*.2f,.577937f*.2f,.2f},{.127656f,.046502f,.896226f},{.301887f,.301887f,.301887f});
  p.v4("uLightOptions",s.ambientScale,s.sunScale,s.backScale,s.cloudStrength);
  p.v4("uCloudWorld",0,0,2000,1700);
  p.v4("uWaterViewport",float(w),float(h),1.f/w,1.f/h);p.v4("uWaterReflectionInfo",s.waterReflection?1.f:0.f,float(reflectionScene.tex.w),float(reflectionScene.tex.h),0);
  p.v4("uWaterFeatures",s.waterDepth?1.f:0.f,s.waterRefraction?1.f:0.f,s.waterReflection?1.f:0.f,1);
 }
 void submit(LabRhi::Program&p,Asset&a,Render::RenderResourceHandle<Render::VertexBufferSpec> mesh,uint32_t count){
  p.flush();LabRhi::backend().SetVertexBuffer({mesh});LabRhi::backend().BindTexture({a.tex.handle,0});
  if(waterPass){LabRhi::backend().BindTexture({opaqueScene.tex.handle,6});LabRhi::backend().BindTexture({opaqueScene.depth.handle,7});LabRhi::backend().BindTexture({reflectionScene.tex.handle,8});}
  LabRhi::backend().Draw({count});draws++;
 }
 void bindSceneTextures(){
  // The Render backend validates device limits on each BindTexture. These
  // immutable inputs therefore bind once per view, rather than once per mesh.
  if(has("dirt"))LabRhi::backend().BindTexture({asset("dirt").tex.handle,1});
  if(has("water-noise"))LabRhi::backend().BindTexture({asset("water-noise").tex.handle,2});
  if(has("foam-noise"))LabRhi::backend().BindTexture({asset("foam-noise").tex.handle,3});
  if(has("ground-dry"))LabRhi::backend().BindTexture({asset("ground-dry").tex.handle,9});
  if(has("ground-shade"))LabRhi::backend().BindTexture({asset("ground-shade").tex.handle,10});
  LabRhi::backend().BindTexture({sunMap.tex.handle,4});LabRhi::backend().BindTexture({cloudMask.tex.handle,5});
 }
 void draw(LabRhi::Program&p,Asset&a,const std::vector<Vertex>&v){
  if(v.empty())return;
  Render::UpdateVertexBufferCommand u;u.handle=spatialMesh;u.numVertex=uint32_t(v.size());u.vertexData=LabRhi::bytes(v.data(),v.size()*sizeof(Vertex));
  if(!LabRhi::backend().UpdateVertexBuffer(u))throw std::runtime_error("Render scene mesh update failed");streamUploads++;
  submit(p,a,spatialMesh,uint32_t(v.size()));
 }
 template<class Maker> void cachedDraw(LabRhi::Program&p,Asset&a,const MeshKey&key,Maker make){
  auto found=fixedMeshes.find(key);
  if(found==fixedMeshes.end()){
   auto v=make();Render::CreateVertexBufferCommand c;
   c.vertexLayout.typeSlots={Render::VertexFieldType::Vec3,Render::VertexFieldType::Vec2,Render::VertexFieldType::Vec3,Render::VertexFieldType::Vec4};
   c.usage=Render::BufferUsage::Static;c.numVertex=uint32_t(v.size());c.vertexData=LabRhi::bytes(v.data(),v.size()*sizeof(Vertex));
   auto handle=LabRhi::backend().CreateVertexBuffer(c);if(!handle.IsValid())throw std::runtime_error("Static RHI mesh cache allocation failed");
   found=fixedMeshes.emplace(key,CachedMesh{handle,uint32_t(v.size())}).first;
  }
  submit(p,a,found->second.handle,found->second.count);
 }
 void fixedDraw(LabRhi::Program&p,Asset&a,const std::vector<Vertex>&v,float identity){
  cachedDraw(p,a,MeshKey{10,a.tex.id,identity,0,0,0,0,0},[&](){return v;});
 }
 std::vector<Vertex> billVertices(Asset&a,float x,float z,float y,float width,float rotation=0,float stretch=1){
  std::vector<Vertex> v;float height=width*a.tex.h/a.tex.w/.6f*stretch;
  float left=x-width*a.pivotX,bottom=y-height*a.pivotY;
  int slices=6;
  for(int i=0;i<slices;i++){
   float v0=float(i)/slices,v1=float(i+1)/slices;
   auto point=[&](float u,float vv){float xx=left+width*u-x,yy=bottom+height*(1-vv)-y;
    return V3{x+std::cos(rotation)*xx-std::sin(rotation)*yy*.6f,y+std::sin(rotation)*xx/.6f+std::cos(rotation)*yy,z};};
   quad(v,point(0,v0),point(1,v0),point(1,v1),point(0,v1),{0,.18f,1},0,v0,1,v1);
  }
  return v;
 }
 void bill(LabRhi::Program&p,Asset&a,float x,float z,float y,float width,float rotation=0,float stretch=1,bool fixed=false){
  auto make=[&](){return billVertices(a,x,z,y,width,rotation,stretch);};
  if(fixed)cachedDraw(p,a,MeshKey{2,a.tex.id,x,y,z,width,rotation,stretch},make);
  else{auto v=make();draw(p,a,v);}
 }
 bool visible(const Object&o,const Settings&s){
  float px=(o.x-s.cameraX)*s.zoom+w/2,py=((o.z-s.cameraZ)*.8f-(altitude(o.x,o.z)+o.footOffset)*.6f)*s.zoom+h/2;
  float minX=px-o.width*s.zoom,maxX=px+o.width*s.zoom,minY=py-o.width*asset(o.asset).tex.h/asset(o.asset).tex.w*s.zoom,maxY=py;
  if(auto it=models.find(o.asset);it!=models.end()){
   auto&m=it->second;float scale=o.width/m.width*s.zoom;
   minX=px+m.minX*scale;maxX=px+m.maxX*scale;minY=py+m.minScreenY*scale;maxY=py+m.maxScreenY*scale;
  }
  float windMargin=35*s.zoom;return !(maxX+windMargin<0||minX-windMargin>w||maxY+windMargin<0||minY-windMargin>h);
 }
 void drawPlants(const std::string&role,const std::vector<size_t>&indices,const Settings&s){
  if(indices.empty())return;auto&b=(reflectionPass?reflectionPlants:plantBatches).at(role);auto&a=asset(role);
  // These four source textures contain only alpha 0 and 255, and their branch
  // uses world position rather than object seed. Depth-writing cutout cards
  // can therefore share one draw without changing their visible composition.
  if(b.visibleIndices!=indices){
   std::vector<Vertex> v;v.reserve(indices.size()*36);
   for(size_t index:indices){auto&card=plantVertices.at(index);v.insert(v.end(),card.begin(),card.end());}
   if(!b.mesh.handle.IsValid()){
    Render::CreateVertexBufferCommand c;c.vertexLayout.typeSlots={Render::VertexFieldType::Vec3,Render::VertexFieldType::Vec2,Render::VertexFieldType::Vec3,Render::VertexFieldType::Vec4};c.usage=Render::BufferUsage::Dynamic;c.numVertex=uint32_t(v.size());c.vertexData=LabRhi::bytes(v.data(),v.size()*sizeof(Vertex));b.mesh.handle=LabRhi::backend().CreateVertexBuffer(c);
    if(!b.mesh.handle.IsValid())throw std::runtime_error("Plant batch RHI allocation failed");
   }else{
    Render::UpdateVertexBufferCommand u;u.handle=b.mesh.handle;u.numVertex=uint32_t(v.size());u.vertexData=LabRhi::bytes(v.data(),v.size()*sizeof(Vertex));if(!LabRhi::backend().UpdateVertexBuffer(u))throw std::runtime_error("Plant batch RHI update failed");streamUploads++;
   }
   b.mesh.count=uint32_t(v.size());b.visibleIndices=indices;
  }
  const auto&o=objects[indices.front()];common(sprite,s,2,o.wind,o.seed);submit(sprite,a,b.mesh.handle,b.mesh.count);
 }
 void model(const Object&o,const Settings&s,bool shadowPass=false){
  auto&m=models.at(o.asset);float scale=o.width/m.width,base=altitude(o.x,o.z)+o.footOffset;
  auto&p=inSunPass?sunDepth:(shadowPass?transparent:sprite);common(p,s,shadowPass?5.f:(o.wind>0?2.f:0.f),o.wind,o.seed);
  p.v4("uObject",o.x,base,o.z,o.seed);p.v4("uMaterial",shadowPass?5.f:(o.wind>0?2.f:0.f),scale*.5f,.12f*s.day+.06f,1);
  auto&a=asset(o.asset);
  cachedDraw(p,a,MeshKey{shadowPass?1:0,a.tex.id,o.x,base,o.z,o.width,0,0},[&](){
  std::vector<Vertex>v=m.vertices;
  for(auto&q:v){q.p.x=q.p.x*scale+o.x;q.p.y=q.p.y*scale+base;q.p.z=q.p.z*scale+o.z;
   if(shadowPass){float relativeY=q.p.y-base;q.p.x-=relativeY*.53f;q.p.z-=relativeY*.28f;q.p.y=base+.75f;}
  }
  return v;});
 }
 void shadow(const Object&o,const Settings&s,bool fixed=true){
  if(models.find(o.asset)!=models.end()){model(o,s,true);return;}
  auto&a=asset(o.asset);float yy=altitude(o.x,o.z)+o.footOffset+.7f,width=o.width,height=width*a.tex.h/a.tex.w;
  float left=o.x-width*a.pivotX;
  auto make=[&](){std::vector<Vertex> v;
  // Project the cutout along one common sun direction onto supporting ground.
  quad(v,{left-height*.53f,yy,o.z-height*.28f},{left+width-height*.53f,yy,o.z-height*.28f},{left+width,yy,o.z},{left,yy,o.z},{0,1,0});
  return v;};
  common(transparent,s,5,0,o.seed);transparent.v4("uMaterial",5,0,.12f*s.day+.06f,0);
  if(fixed)cachedDraw(transparent,a,MeshKey{3,a.tex.id,o.x,yy,o.z,width,0,0},make);
  else{auto v=make();draw(transparent,a,v);}
 }
 void cliffTiles(const Settings&s){
  if(models.find("rock")==models.end())return;
  auto&m=models.at("rock");float scale=160.f/m.width;
  float topZ=0,count=0;for(auto&q:m.vertices)if(q.p.y>m.height-.06f){topZ+=q.p.z;count++;}if(count)topZ/=count;
  for(float x=850;x<1580;x+=154){if(x+75>1050&&x-75<1180)continue;
   auto&p=inSunPass?sunDepth:sprite;common(p,s,0);p.v4("uMaterial",0,0,.27f,1);auto&a=asset("rock");cachedDraw(p,a,MeshKey{4,a.tex.id,x,0,0,0,0,0},[&](){auto v=m.vertices;
   for(auto&q:v){q.p.x=q.p.x*scale+x+50;q.p.z=(q.p.z-topZ)*scale+Scene::edge(q.p.x);q.p.y=q.p.y/m.height*160;if(q.colour[1]>0)q.colour[1]+=(x-850)/154*.0000005f;}
   return v;});
  }
 }
 void actor(const Settings&s,LabRhi::Program&p){
  if(!drawPlayer)return;static const char*dirs[]={"down","up","right","left"};int frame=s.moving?int(s.walkPhase)%4:0;
  std::string role="player_"+std::string(dirs[s.direction])+"_"+std::to_string(frame);if(!has(role))role="player_down_0";
  if(has(role)){common(p,s,0);bill(p,asset(role),std::round(s.playerX),std::round(s.playerZ*.8f)/.8f,std::round(s.playerY*.6f)/.6f,38);}
 }
 void updateSunMap(const Settings&s){
  if(!s.shadows)return;
  double wall=glfwGetTime();const auto&old=lastShadowSettings;
  bool geometryChanged=(s.wind>0&&s.time!=old.time)||s.wind!=old.wind||s.playerX!=old.playerX||s.playerY!=old.playerY||s.playerZ!=old.playerZ||s.direction!=old.direction||s.moving!=old.moving||int(s.walkPhase)%4!=int(old.walkPhase)%4||drawPlayer!=lastDrawPlayer;
  bool lightChanged=s.sunAzimuth!=old.sunAzimuth||s.sunElevation!=old.sunElevation||s.manualSun!=old.manualSun||(!s.manualSun&&s.day!=old.day);
  // Casters and receivers always use the same time. Batching removes the old
  // 12-Hz throttle that made moving crowns shadow their own previous pose.
  if(shadowValid&&!forceShadowUpdate&&!lightChanged&&!geometryChanged)return;
  int before=draws;inSunPass=true;sunMap.bind(true);
  common(sunDepth,s,1);fixedDraw(sunDepth,asset("ground"),ground,0);fixedDraw(sunDepth,asset("ground"),plateau,1);fixedDraw(sunDepth,asset("ground"),ramp,2);
  common(sunDepth,s,3);fixedDraw(sunDepth,asset("dirt"),cliff,3);cliffTiles(s);
  if(has("wood")){common(sunDepth,s,0);fixedDraw(sunDepth,asset("wood"),bridge,4);}
  else if(models.find("bridge")!=models.end()){Object b{"bridge",454,850,335,0,0};model(b,s);}
  // All scene casters are submitted, including those outside the view camera:
  // an offscreen tree can still project onto visible ground.
  for(auto&[role,vertices]:sunBatches){auto&a=asset(role);bool wind=sunBatchWind.at(role);common(sunDepth,s,wind?2.f:0.f);sunDepth.v4("uMaterial",wind?2.f:0.f,0,.5f,wind?2.f:1.f);
   cachedDraw(sunDepth,a,MeshKey{21,a.tex.id,0,0,0,0,0,0},[&](){return vertices;});}
  for(size_t i=0;i<objects.size();i++){const auto&o=objects[i];if(models.find(o.asset)!=models.end()||plantVertices.find(i)!=plantVertices.end())continue;common(sunDepth,s,o.wind>0?2.f:0.f,o.wind,o.seed);bill(sunDepth,asset(o.asset),o.x,o.z,altitude(o.x,o.z)+o.footOffset,o.width,0,1,true);}
  for(auto&[role,b]:plantBatches){auto&a=asset(role);common(sunDepth,s,2);
   cachedDraw(sunDepth,a,MeshKey{20,a.tex.id,0,0,0,0,0,0},[&](){std::vector<Vertex> v;for(auto&[i,card]:plantVertices)if(objects[i].asset==role)v.insert(v.end(),card.begin(),card.end());return v;});
  }
  actor(s,sunDepth);inSunPass=false;shadowDraws=draws-before;shadowUpdates++;lastShadowSettings=s;lastDrawPlayer=drawPlayer;lastShadowWall=wall;shadowValid=true;
 }
 float cloudOffset(const Settings&s)const{return forceShadowUpdate?(.01f+s.wind*.15f)*s.time:s.cloudOffset;}
 void updateCloudMask(const Settings&s){
  double wall=glfwGetTime();float offset=cloudOffset(s);auto&old=lastCloudSettings;
  bool settingsChanged=s.cloudDensity!=old.cloudDensity||s.cloudCutoff!=old.cloudCutoff||s.cloudOpacity!=old.cloudOpacity||s.cloudEdgeRange!=old.cloudEdgeRange;
  bool motionChanged=s.time!=old.time||offset!=lastCloudOffset;
  if(cloudValid&&!forceShadowUpdate&&!settingsChanged&&!motionChanged)return;
  cloudMask.bind(true);cloudProgram.useOpaque();
  cloudProgram.v4("uCloudWorld",0,0,2000,1700);cloudProgram.v4("uCloudSettings",s.cloudDensity,s.cloudCutoff,s.cloudOpacity,s.cloudEdgeRange);cloudProgram.v4("uCloudMotion",offset,s.time,1,0);
  cloudProgram.v4("uCloudNoiseInfo",cloudNoiseInfo[0],cloudNoiseInfo[1],cloudNoiseInfo[2],cloudNoiseInfo[3]);cloudProgram.v4("uCloudAtlasInfo",cloudAtlasInfo[0],cloudAtlasInfo[1],cloudAtlasInfo[2],cloudAtlasInfo[3]);
  cloudProgram.flush();LabRhi::backend().SetVertexBuffer({screenMesh});LabRhi::backend().BindTexture({asset("cloud-noise").tex.handle,0});LabRhi::backend().Draw({6});draws++;
  lastCloudSettings=s;lastCloudOffset=offset;lastCloudWall=wall;cloudValid=true;cloudUpdates++;
 }
 void leaves(const Settings&s){
  if(s.leaves<=0)return;
  std::map<std::string,std::vector<Vertex>> groups;
  for(unsigned i=0;i<85*s.leaves;i++){
   float life=8+rand01(i+73)*8,phase=std::fmod(s.time+rand01(i+230)*life,life)/life;
   float ox=250+rand01(i+379)*1260,oz=350+rand01(i+533)*900;
   float x=ox-phase*125*s.wind,z=oz+phase*44;
   float y=altitude(x,z)+(1-phase)*145+5;
   std::string role="white";std::vector<Vertex> v;
   if(has("leaf")){role=i%8==0?"leaf":"leaf"+std::to_string(i%8);if(!has(role))role="leaf";v=billVertices(asset(role),x,z,y,7+rand01(i)*5,std::sin(s.time*2+i)*1.5f);}
   else{
    float angle=s.time*2+i,dx=std::cos(angle)*5,dy=std::sin(angle)*3;
    quad(v,{x-dx,y+dy+4,z},{x+dx,y-dy+4,z},{x+dx+2,y-dy-4,z},{x-dx+2,y+dy-4,z},{0,1,0});
   }
   for(auto&q:v)q.colour[2]=float(i);auto&group=groups[role];group.insert(group.end(),v.begin(),v.end());
  }
  for(auto&[role,v]:groups){auto&mesh=particleMeshes[role];
   if(!mesh.handle.IsValid()){Render::CreateVertexBufferCommand c;c.vertexLayout.typeSlots={Render::VertexFieldType::Vec3,Render::VertexFieldType::Vec2,Render::VertexFieldType::Vec3,Render::VertexFieldType::Vec4};c.usage=Render::BufferUsage::Stream;c.numVertex=uint32_t(v.size());c.vertexData=LabRhi::bytes(v.data(),v.size()*sizeof(Vertex));mesh.handle=LabRhi::backend().CreateVertexBuffer(c);if(!mesh.handle.IsValid())throw std::runtime_error("Particle batch RHI allocation failed");}
   else{Render::UpdateVertexBufferCommand u;u.handle=mesh.handle;u.numVertex=uint32_t(v.size());u.vertexData=LabRhi::bytes(v.data(),v.size()*sizeof(Vertex));if(!LabRhi::backend().UpdateVertexBuffer(u))throw std::runtime_error("Particle batch RHI update failed");streamUploads++;}
   mesh.count=uint32_t(v.size());common(transparent,s,4);transparent.v4("uTint",.92f,.65f,.24f,.9f);submit(transparent,asset(role),mesh.handle,mesh.count);
  }
 }
 void terrain(const Settings&s){
  common(sprite,s,1);fixedDraw(sprite,asset("ground"),ground,0);fixedDraw(sprite,asset("ground"),plateau,1);fixedDraw(sprite,asset("ground"),ramp,2);
  common(sprite,s,3);fixedDraw(sprite,asset("dirt"),cliff,3);cliffTiles(s);
  if(has("wood")){common(sprite,s,0);fixedDraw(sprite,asset("wood"),bridge,4);}
  else if(models.find("bridge")!=models.end()){Object b{"bridge",454,850,335,0,0};model(b,s);}
 }
 void opaque(const Settings&s){
  if(!reverseOpaque)terrain(s);
  if(s.legacyShadows&&!reflectionPass)for(auto&o:objects)if(o.width>40)shadow(o,s);
  if(s.legacyShadows&&!reflectionPass&&drawPlayer){Object p{"player_down_0",s.playerX,s.playerZ,38,0,0};if(has(p.asset))shadow(p,s,false);}
  // Alpha-cutout meshes write true 3-D depth. Deliberately not sorted by Y:
  // their spatial depth still produces correct player/tree/cliff occlusion.
  std::vector<bool> visibleObjects(objects.size());std::map<std::string,std::vector<size_t>> visiblePlants;
  for(size_t i=0;i<objects.size();i++){const auto&o=objects[i];visibleObjects[i]=reflectionPass?(o.x+o.width>120&&o.x-o.width<760):visible(o,s);if(visibleObjects[i]&&plantVertices.find(i)!=plantVertices.end())visiblePlants[objects[i].asset].push_back(i);}
  for(size_t order=0;order<objects.size();order++){size_t i=reverseOpaque?objects.size()-1-order:order;
   if(!visibleObjects[i])continue;auto&o=objects[i];
   if(plantVertices.find(i)!=plantVertices.end()){auto&indices=visiblePlants[o.asset];if(!indices.empty()){drawPlants(o.asset,indices,s);indices.clear();}continue;}
   if(models.find(o.asset)!=models.end())model(o,s);
   else{common(sprite,s,o.wind>0?2.f:0.f,o.wind,o.seed);bill(sprite,asset(o.asset),o.x,o.z,altitude(o.x,o.z)+o.footOffset,o.width,0,1,true);}
  }
  if(!reflectionPass||(s.playerX>120&&s.playerX<760))actor(s,sprite);
  if(reverseOpaque)terrain(s);
 }
 void unbindWaterInputs(){for(uint32_t binding:{6u,7u,8u})LabRhi::backend().BindTexture({{},binding});}
 void render(Settings&s){
  draws=streamUploads=0;updateCloudMask(s);updateSunMap(s);
  // Reflection uses the same orthographic camera, mirrored only in projection.
  // Alpha zero identifies sky coverage to the water shader. Exclude water,
  // falling leaves and glow, and cull objects unable to reach the river strip.
  unbindWaterInputs();reflectionPass=true;reflectionScene.bind(false,{0,0,0,0});bindSceneTextures();if(s.waterReflection)opaque(s);reflectionPass=false;
  unbindWaterInputs();opaqueScene.bind();opaque(s);
  scene.bind();copyProgram.useOpaque();copyProgram.flush();LabRhi::backend().SetVertexBuffer({screenMesh});LabRhi::backend().BindTexture({opaqueScene.tex.handle,0});LabRhi::backend().BindTexture({opaqueScene.depth.handle,1});LabRhi::backend().Draw({6});draws++;
  bindSceneTextures();
  waterPass=true;common(sprite,s,7);fixedDraw(sprite,asset("ground"),waterSurface,5);waterPass=false;
  leaves(s);
  if(s.light&&s.day<.9f)for(auto p:std::array<V3,4>{{{1160,0,590},{1410,0,450},{875,0,1030},{630,0,840}}}){
   common(transparent,s,6);transparent.v4("uTint",1,.44f,.12f,(1-s.day)*.37f);bill(transparent,asset("white"),p.x,p.z,altitude(p.x,p.z)-30,145,0,1,true);
  }
  output.bind(true);post.use();post.v4("uPost",s.time,s.post?.8f:0,s.post?s.day:0,0);post.v4("uTint",1,1,1,1);
  post.v4("uAO",s.ao?1.f:0.f,s.aoStrength,s.aoRadius,s.aoDebug?1.f:0.f);post.v4("uSceneInfo",std::round(s.cameraX),std::round(s.cameraZ*.8f)/.8f,s.zoom,s.time);post.v4("uViewport",float(w),float(h),0,0);
  post.flush();LabRhi::backend().SetVertexBuffer({screenMesh});LabRhi::backend().BindTexture({scene.tex.handle,0});LabRhi::backend().BindTexture({scene.depth.handle,1});LabRhi::backend().Draw({6});draws++;
 }
 std::vector<unsigned char> pixels(){output.bind();std::vector<unsigned char> p(size_t(w)*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,p.data());for(int yy=0;yy<h/2;yy++)for(int xx=0;xx<w*4;xx++)std::swap(p[size_t(yy)*w*4+xx],p[size_t(h-1-yy)*w*4+xx]);return p;}
 void save(const fs::path&p){if(!p.parent_path().empty())fs::create_directories(p.parent_path());auto b=pixels();if(!stbi_write_png(p.string().c_str(),w,h,4,b.data(),w*4))throw std::runtime_error("PNG export failed: "+p.string());}
 void saveSunDepth(const fs::path&p){
  Render::RHICommand::SetRenderTarget c;c.target=sunMap.handle;c.width=sunMap.tex.w;c.height=sunMap.tex.h;LabRhi::backend().SetRenderTarget(c);
  int size=sunMap.tex.w;std::vector<unsigned char> b(size_t(size)*size*4);glReadPixels(0,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,b.data());
  std::vector<unsigned char> gray(size_t(size)*size);
  for(int y=0;y<size;y++)for(int x=0;x<size;x++){size_t k=(size_t(y)*size+x)*4;float d=b[k]/255.f+b[k+1]/65025.f+b[k+2]/16581375.f;gray[size_t(size-1-y)*size+x]=static_cast<unsigned char>(clamp(d,0,1)*255);}
  if(!stbi_write_png(p.string().c_str(),size,size,1,gray.data(),size))throw std::runtime_error("Sun depth readback export failed");
 }
 std::vector<unsigned char> cloudPixels(){cloudMask.bind();std::vector<unsigned char>b(256*256*4);glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,b.data());return b;}
 void saveCloudMask(const fs::path&p){
  auto b=cloudPixels();std::vector<unsigned char> a(256*256);for(int y=0;y<256;y++)for(int x=0;x<256;x++)a[size_t(255-y)*256+x]=b[(size_t(y)*256+x)*4+3];
  if(!stbi_write_png(p.string().c_str(),256,256,1,a.data(),256))throw std::runtime_error("Cloud mask readback export failed");
 }
 void saveTargetColor(const DepthTarget&t,const fs::path&p){int width=t.tex.w,height=t.tex.h;Render::RHICommand::SetRenderTarget c;c.target=t.handle;c.width=width;c.height=height;LabRhi::backend().SetRenderTarget(c);std::vector<unsigned char>b(size_t(width)*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,b.data());for(int y=0;y<height/2;y++)for(int x=0;x<width*4;x++)std::swap(b[size_t(y)*width*4+x],b[size_t(height-1-y)*width*4+x]);if(!stbi_write_png(p.string().c_str(),width,height,4,b.data(),width*4))throw std::runtime_error("RHI target color export failed");}
 void saveReflection(const fs::path&p){saveTargetColor(reflectionScene,p);}
};
static void checkGL(){auto e=glGetError();if(e!=GL_NO_ERROR)throw std::runtime_error("OpenGL/RHI diagnostic error "+std::to_string(e));if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete Render framebuffer");}
static size_t difference(const std::vector<unsigned char>&a,const std::vector<unsigned char>&b){size_t n=0;for(size_t i=0;i<a.size();i+=4)if(std::abs(int(a[i])-b[i])+std::abs(int(a[i+1])-b[i+1])+std::abs(int(a[i+2])-b[i+2])>10)n++;return n;}
static void parameters(std::ostream&f,const Settings&s){
 auto sun=sunDirection(s);
 f<<"{\"time\":"<<s.time<<",\"day\":"<<s.day<<",\"wind\":"<<s.wind<<",\"water\":"<<s.water<<",\"leaves\":"<<s.leaves;
 f<<",\"lighting\":"<<(s.light?"true":"false")<<",\"shadows\":"<<(s.shadows?"true":"false")<<",\"legacy_shadows\":"<<(s.legacyShadows?"true":"false");
 f<<",\"sun_direction_mode\":\""<<(s.manualSun?"manual":"recovered preset interpolation")<<"\",\"sun_direction\":["<<sun.x<<","<<sun.y<<","<<sun.z<<"],\"manual_sun_angles\":["<<s.sunAzimuth<<","<<s.sunElevation<<"]";
 f<<",\"shadow_bias\":"<<s.shadowBias<<",\"pcf_texels\":"<<s.shadowPcf<<",\"ao\":"<<(s.ao?"true":"false")<<",\"ao_strength\":"<<s.aoStrength<<",\"ao_radius_px\":"<<s.aoRadius;
 f<<",\"light_options\":["<<s.ambientScale<<","<<s.sunScale<<","<<s.backScale<<","<<s.cloudStrength<<"]";
 f<<",\"cloud_settings\":["<<s.cloudDensity<<","<<s.cloudCutoff<<","<<s.cloudOpacity<<","<<s.cloudEdgeRange<<"],\"cloud_wind_offset\":"<<(.01f+s.wind*.15f)*s.time<<",\"cloud_offset_mode\":\"deterministic rate(wind)*time for fixed capture\"";
 f<<",\"water_features\":{\"depth\":"<<(s.waterDepth?"true":"false")<<",\"refraction\":"<<(s.waterRefraction?"true":"false")<<",\"planar_reflection\":"<<(s.waterReflection?"true":"false")<<"}";
 f<<",\"post\":"<<(s.post?"true":"false")<<",\"height_debug\":"<<(s.heightDebug?"true":"false")<<",\"water_debug\":"<<(s.waterDebug?"true":"false")<<",\"camera\":["<<s.cameraX<<","<<s.cameraZ<<","<<s.zoom<<"],\"player\":["<<s.playerX<<","<<s.playerY<<","<<s.playerZ<<"]}";
}
static void saveUI(const fs::path&p,int w,int h){std::vector<unsigned char>b(size_t(w)*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,b.data());for(int yy=0;yy<h/2;yy++)for(int xx=0;xx<w*4;xx++)std::swap(b[size_t(yy)*w*4+xx],b[size_t(h-1-yy)*w*4+xx]);if(!p.parent_path().empty())fs::create_directories(p.parent_path());if(!stbi_write_png(p.string().c_str(),w,h,4,b.data(),w*4))throw std::runtime_error("UI capture failed");}
int main(int argc,char**argv){try{
 bool smoke=false;fs::path captureDir,captureUI,captureMotion,captureLighting,captureStability;int captureFrames=0;
 for(int i=1;i<argc;i++){std::string a=argv[i];if(a=="--smoke-test")smoke=true;else if(a=="--capture-dir"&&i+1<argc)captureDir=argv[++i];else if(a=="--capture-ui"&&i+1<argc)captureUI=argv[++i];else if(a=="--capture-motion"&&i+1<argc)captureMotion=argv[++i];else if(a=="--capture-lighting"&&i+1<argc)captureLighting=argv[++i];else if(a=="--capture-stability"&&i+1<argc)captureStability=argv[++i];else if(a=="--capture-frames"&&i+1<argc)captureFrames=std::stoi(argv[++i]);else if(a=="--assets"&&i+1<argc)module=argv[++i];else if(a=="--help"){std::cout<<"graveyard_scene_lab [--smoke-test] [--capture-dir DIR] [--capture-lighting DIR] [--capture-stability DIR] [--capture-ui PNG] [--capture-motion DIR] [--capture-frames N] [--assets MODULE_DIR]\n";return 0;}}
 ApplicationWindow::Window window({1640,940,"Graveyard Scene Lab | ApplicationWindow + Render RHI",true,Render::BackendType::Opengl});
 if(!window.Activate())throw std::runtime_error("ApplicationWindow activation failed");auto*native=window.GetNativeWindow();
 if(smoke||!captureDir.empty()||!captureUI.empty()||!captureMotion.empty()||!captureLighting.empty()||!captureStability.empty())glfwHideWindow(native);
 glfwSetWindowSizeLimits(native,1050,680,GLFW_DONT_CARE,GLFW_DONT_CARE);
 auto spec=*window.GetRenderContextAsOpengl();LabRhi::Context context(spec);LabRhi::context=&context;
 std::cout<<"Renderer: "<<glGetString(GL_RENDERER)<<"\nOpenGL: "<<glGetString(GL_VERSION)<<"\n";
 IMGUI_CHECKVERSION();ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;ImGui::StyleColorsDark();
 ImGui_ImplGlfw_InitForOpenGL(native,true);ImGui_ImplOpenGL3_Init("#version 450 core");
 Scene scene;scene.init();scene.forceShadowUpdate=smoke||!captureDir.empty()||!captureMotion.empty()||!captureLighting.empty();Settings s;
 auto validate=[&](const fs::path&p,const std::string&extra){fs::create_directories(p.parent_path());std::ofstream f(p);f<<"{\n\"window_module\":\"ApplicationWindow::Window\",\n\"render_module\":\"Render::OpenglBackend + RHIResourcePool\",\n\"scene_vertex_layout\":\"vec3 position / vec2 UV / vec3 normal / vec4 source metadata\",\n\"depth_attachment\":\"Depth32F\",\n\"uniform_transport\":\"std140 RHI UBO\",\n\"alpha_cutoff\":0.5,\n\"camera_x_degrees\":53.130104,\n\"source_coordinate_adaptation\":\"laboratory Zsouth = -Unity Znorth\",\n\"textures\":"<<scene.assets.size()<<",\n\"scene_objects\":"<<scene.objects.size()<<",\n\"gl_error\":0,\n\"renderer\":\""<<glGetString(GL_RENDERER)<<"\",\n"<<extra<<"\n}\n";};
 if(smoke){
  scene.render(s);auto baseline=scene.pixels();checkGL();
  if(baseline.empty())throw std::runtime_error("Empty GPU readback");
  auto cloudData=scene.cloudPixels();unsigned minCloud=255,maxCloud=0;for(size_t i=3;i<cloudData.size();i+=4){minCloud=std::min(minCloud,unsigned(cloudData[i]));maxCloud=std::max(maxCloud,unsigned(cloudData[i]));}if(maxCloud-minCloud<3)throw std::runtime_error("Original cloud mask lacks spatial alpha variation");
  Render::RHICommand::SetRenderTarget shadowRead;shadowRead.target=scene.sunMap.handle;shadowRead.width=scene.sunMap.tex.w;shadowRead.height=scene.sunMap.tex.h;LabRhi::backend().SetRenderTarget(shadowRead);
  std::vector<unsigned char> packedDepth(size_t(scene.sunMap.tex.w)*scene.sunMap.tex.h*4);glReadPixels(0,0,scene.sunMap.tex.w,scene.sunMap.tex.h,GL_RGBA,GL_UNSIGNED_BYTE,packedDepth.data());
  size_t shadowCoverage=0;float minShadowDepth=1,maxShadowDepth=0;
  for(size_t i=0;i<packedDepth.size();i+=4)if(packedDepth[i]!=255||packedDepth[i+1]!=255||packedDepth[i+2]!=255){float d=packedDepth[i]/255.f+packedDepth[i+1]/65025.f+packedDepth[i+2]/16581375.f;minShadowDepth=std::min(minShadowDepth,d);maxShadowDepth=std::max(maxShadowDepth,d);shadowCoverage++;}
  if(shadowCoverage<10000||maxShadowDepth-minShadowDepth<.02f)throw std::runtime_error("Sun RHI depth target has no usable caster depth range");
  float low=Scene::altitude(1115,968),high=Scene::altitude(1115,710);
  if(low>3||high<159)throw std::runtime_error("Ramp endpoints failed");
  Settings walk=s;walk.playerX=1115;walk.playerZ=985;walk.playerY=0;
  for(int i=0;i<145;i++)scene.move(walk,0,-1,1/60.f);
  if(walk.playerY<150||walk.playerZ>730)throw std::runtime_error("Walking uphill failed");
  scene.follow(walk,1);if(walk.cameraZ>s.cameraZ)throw std::runtime_error("Follow camera failed");
  Settings cliff=s;cliff.playerX=950;cliff.playerZ=Scene::edge(950)+4;cliff.playerY=0;scene.move(cliff,0,-1,.1f);if(cliff.playerY!=0)throw std::runtime_error("Cliff support guard failed");
  auto altered=[&](Settings t){scene.render(t);checkGL();return difference(baseline,scene.pixels());};
  Settings t=s;t.day=0;auto night=altered(t);t=s;t.wind=0;auto wind=altered(t);t=s;t.leaves=0;auto leaf=altered(t);t=s;t.water=0;auto water=altered(t);t=s;t.heightDebug=true;auto height=altered(t);t=s;t.shadows=false;auto sunShadow=altered(t);t=s;t.ao=false;auto contact=altered(t);t=s;t.cloudStrength=0;auto clouds=altered(t);
  if(night<2000||wind<30||leaf<30||water<100||height<2000||sunShadow<100||contact<30||clouds<100)throw std::runtime_error("A visual toggle did not change the rendered GPU image");
  // Validate visibility against a no-player render, not only two different
  // filenames: in front of this tree the actor must survive, behind its crown
  // most actor pixels must fail the actual spatial depth comparison.
  Settings occ=s;occ.cameraX=670;occ.cameraZ=900;occ.zoom=1.2f;occ.playerX=670;occ.playerZ=1040;occ.playerY=0;occ.shadows=false;occ.leaves=0;occ.wind=0;occ.water=0;occ.post=false;
  auto visiblePixels=[&](){scene.drawPlayer=true;scene.render(occ);auto actor=scene.pixels();scene.drawPlayer=false;scene.render(occ);auto empty=scene.pixels();scene.drawPlayer=true;return difference(actor,empty);};
  size_t frontVisible=visiblePixels();occ.playerZ=940;size_t behindVisible=visiblePixels();
  if(frontVisible<150||behindVisible>=frontVisible*.65)throw std::runtime_error("Tree/player occlusion visibility test failed");
  // Submit front red before rear green at the same projected rectangle. A
  // painter-order implementation would return green; true depth returns red.
  Settings depth=s;depth.cameraX=depth.cameraZ=0;depth.zoom=1;depth.light=false;depth.heightDebug=false;scene.scene.bind();
  std::vector<Vertex> face;Scene::quad(face,{-40,100+26.666667f,20},{40,100+26.666667f,20},{40,26.666667f,20},{-40,26.666667f,20},{0,0,1});
  scene.common(scene.sprite,depth,0);scene.sprite.v4("uTint",1,0,0,1);scene.draw(scene.sprite,scene.asset("white"),face);
  face.clear();Scene::quad(face,{-40,100,0},{40,100,0},{40,0,0},{-40,0,0},{0,0,1});scene.common(scene.sprite,depth,0);scene.sprite.v4("uTint",0,1,0,1);scene.draw(scene.sprite,scene.asset("white"),face);
  std::array<unsigned char,4> depthPixel{};glReadPixels(scene.w/2,scene.h/2+30,1,1,GL_RGBA,GL_UNSIGNED_BYTE,depthPixel.data());checkGL();
  if(depthPixel[0]<200||depthPixel[1]>20)throw std::runtime_error("RHI depth did not override reverse draw order");
  // Run all major state combinations through the same production renderer.
  int states=0;for(int d=0;d<3;d++)for(int effects=0;effects<8;effects++){t=s;t.day=d*.5f;t.wind=(effects&1)?1:0;t.leaves=(effects&2)?1:0;t.shadows=effects&4;t.time=12+effects*.3f;scene.render(t);checkGL();states++;}
  std::ostringstream evidence;evidence<<"\"smoke_pass\":true,\n\"rendered_states\":"<<states<<",\n\"cloud_mask\":{\"size\":256,\"volume_dimensions\":[32,32,32],\"alpha_range_u8\":["<<minCloud<<","<<maxCloud<<"],\"sampling_adaptation\":\"Linear/Repeat + two Z slices; saved Texture3D Point/Clamp; actual inline sampler unresolved\"},\n\"sun_shadow_map\":{\"size\":1536,\"rgba_packed_depth\":true,\"opaque_pipeline\":true,\"depth_attachment\":\"Depth32F\",\"covered_texels\":"<<shadowCoverage<<",\"caster_depth_range\":["<<minShadowDepth<<","<<maxShadowDepth<<"]},\n\"ramp_support\":["<<low<<","<<high<<"],\n\"walk_final_height\":"<<walk.playerY<<",\n\"reverse_draw_order_depth_pixel\":["<<int(depthPixel[0])<<","<<int(depthPixel[1])<<","<<int(depthPixel[2])<<"],\n\"actor_visible_pixels\":{\"front\":"<<frontVisible<<",\"behind\":"<<behindVisible<<"},\n\"visual_toggle_changed_pixels\":{\"night\":"<<night<<",\"wind\":"<<wind<<",\"leaves\":"<<leaf<<",\"water\":"<<water<<",\"height\":"<<height<<",\"sun_shadow\":"<<sunShadow<<",\"contact_ao\":"<<contact<<",\"cloud_mask\":"<<clouds<<"}";
  validate(module/"Evidence/gpu-validation.json",evidence.str());std::cout<<"PASS: assets, RHI resources, 24 render states, ramp walking, cliff guard, camera and GPU effect differences.\n";return 0;
 }
 if(!captureStability.empty()){
  fs::create_directories(captureStability);scene.forceShadowUpdate=false;s.follow=false;s.wind=0;s.water=0;s.leaves=0;s.cloudStrength=0;
  scene.render(s);auto frozen=scene.pixels();scene.save(captureStability/"frozen-first.png");size_t frozenMax=0;
  for(int i=1;i<=12;i++){s.time=12+i/60.f;s.cameraX=915+(i%2?.15f:0);scene.render(s);checkGL();frozenMax=std::max(frozenMax,difference(frozen,scene.pixels()));}
  scene.save(captureStability/"frozen-last.png");if(frozenMax!=0)throw std::runtime_error("Frozen wind/water and identical snapped camera produced changing pixels");
  s=Settings{};s.follow=false;s.wind=0;s.water=0;s.leaves=0;s.cloudStrength=0;scene.render(s);auto ordered=scene.pixels();scene.save(captureStability/"opaque-order-normal.png");scene.reverseOpaque=true;scene.render(s);auto reversed=scene.pixels();scene.save(captureStability/"opaque-order-reversed.png");scene.reverseOpaque=false;size_t orderChanged=difference(ordered,reversed);
  s=Settings{};s.follow=false;s.zoom=1;s.wind=0;s.water=0;s.leaves=0;s.cloudStrength=0;scene.render(s);auto previous=scene.pixels();size_t panMax=0;double panRgbError=0;
  for(int i=1;i<=12;i++){s.cameraX=915+float(i);scene.render(s);auto current=scene.pixels();size_t changed=0;double error=0;size_t samples=0;
   for(int y=20;y<scene.h-20;y++)for(int x=20;x<scene.w-20;x++){size_t a=(size_t(y)*scene.w+x+1)*4,b=(size_t(y)*scene.w+x)*4;int d=0;for(int c=0;c<3;c++)d+=std::abs(int(previous[a+c])-int(current[b+c]));error+=d;changed+=d>10;samples++;}
   panMax=std::max(panMax,changed);panRgbError=std::max(panRgbError,error/double(samples*3));previous=std::move(current);
  }
  scene.save(captureStability/"frozen-camera-pan.png");
  s=Settings{};s.follow=false;s.water=0;s.leaves=0;s.cloudStrength=0;float maxShadowLag=0,maxCloudLag=0;int maxShadowDraws=0;unsigned windShadowStart=scene.shadowUpdates,windCloudStart=scene.cloudUpdates;
  for(int i=0;i<60;i++){s.time=12+i/60.f;s.cloudOffset=(.01f+s.wind*.15f)*s.time;scene.render(s);checkGL();maxShadowLag=std::max(maxShadowLag,std::abs(s.time-scene.lastShadowSettings.time));maxCloudLag=std::max(maxCloudLag,std::abs(s.time-scene.lastCloudSettings.time));maxShadowDraws=std::max(maxShadowDraws,scene.shadowDraws);if(i%15==0||i==59){std::ostringstream name;name<<"wind-"<<std::setfill('0')<<std::setw(2)<<i<<".png";scene.save(captureStability/name.str());}}
  if(maxShadowLag!=0||maxCloudLag!=0)throw std::runtime_error("Animated shadow or cloud mask was rendered at an older time than its receiver");
  std::ofstream f(captureStability/"temporal-validation.json");f<<"{\"pass\":true,\"frozen_frames\":13,\"frozen_wind_water\":0,\"frozen_leaves_clouds\":0,\"subpixel_camera_has_identical_snapped_projection\":true,\"frozen_changed_pixels_max\":"<<frozenMax<<",\"opaque_model_and_terrain_order_reversed_changed_pixels\":"<<orderChanged<<",\"integer_camera_pan_frames\":12,\"reprojected_pan_changed_pixels_max\":"<<panMax<<",\"reprojected_pan_mean_rgb_error_max\":"<<panRgbError<<",\"pan_scope\":\"overlap of full image; includes screen vignette and legitimate raster coverage changes\",\"wind_sequence_frames\":60,\"wind_fps\":60,\"shadow_time_lag_seconds_max\":"<<maxShadowLag<<",\"cloud_time_lag_seconds_max\":"<<maxCloudLag<<",\"wind_shadow_updates\":"<<scene.shadowUpdates-windShadowStart<<",\"wind_cloud_updates\":"<<scene.cloudUpdates-windCloudStart<<",\"same_frame_shadow_draws\":"<<maxShadowDraws<<",\"horizontal_source_triangles_assigned_stable_depth_layer\":"<<scene.coplanarGroundTriangles<<",\"shadow_update_policy\":\"current frame time; atlas batches carry instance wind phase/amplitude\",\"gl_error\":0}\n";f.close();fs::copy_file(captureStability/"temporal-validation.json",module/"Evidence/stability-validation.json",fs::copy_options::overwrite_existing);
  std::cout<<"STABILITY PASS: frozen="<<frozenMax<<" pixels, reverse opaque="<<orderChanged<<", pan RGB error="<<panRgbError<<", shadow/cloud lag=0, batched shadow draws="<<maxShadowDraws<<"\n";return 0;
 }
 if(!captureLighting.empty()){
  fs::create_directories(captureLighting);std::vector<std::string> records;
  auto cap=[&](const std::string&name){scene.render(s);scene.save(captureLighting/(name+".png"));checkGL();std::ostringstream r;r<<"{\"file\":\""<<name<<".png\",\"parameters\":";parameters(r,s);r<<"}";records.push_back(r.str());std::cout<<"Captured "<<name<<"\n";};
  cap("overview-day");scene.saveSunDepth(captureLighting/"shadow-map-depth.png");s.day=.42f;cap("overview-dusk");s.day=.05f;cap("overview-night");s=Settings{};
  s.cloudStrength=0;cap("clouds-off");s.cloudStrength=1;cap("clouds-on");scene.saveCloudMask(captureLighting/"cloudmap-only.png");s.time=32;cap("clouds-later");scene.saveCloudMask(captureLighting/"cloudmap-later.png");s=Settings{};
  s.cameraX=460;s.cameraZ=1130;s.zoom=1.6f;s.wind=0;s.leaves=0;s.waterReflection=false;s.waterRefraction=false;s.waterDepth=false;cap("water-depth-off");s.waterDepth=true;cap("water-depth-on");cap("water-refraction-off");s.waterRefraction=true;cap("water-refraction-on");cap("water-reflection-off");s.waterReflection=true;cap("water-reflection-on");scene.saveReflection(captureLighting/"reflection-only.png");scene.saveTargetColor(scene.opaqueScene,captureLighting/"opaque-only.png");s.waterDebug=true;cap("water-depth-debug");s=Settings{};
  s.cameraX=1130;s.cameraZ=720;s.zoom=1.25f;s.leaves=0;s.wind=0;
  s.shadows=false;cap("sun-shadow-off");s.shadows=true;cap("sun-shadow-on");s.shadowPcf=0;cap("pcf-off");s.shadowPcf=2;cap("pcf-on");s.shadowDebug=true;cap("shadow-visibility");s.shadowDebug=false;
  s.manualSun=true;s.sunAzimuth=-35;cap("sun-direction-changed");s.sunAzimuth=90.8f;s.sunElevation=30;cap("sun-low");s.manualSun=false;s.sunElevation=70;
  s.shadows=false;s.legacyShadows=true;cap("legacy-flat-silhouette");s.legacyShadows=false;s.shadows=true;
  s.ao=false;cap("contact-ao-off");s.ao=true;cap("contact-ao-on");s.aoDebug=true;cap("contact-ao-only");s.aoDebug=false;
  s.day=.3f;s.light=false;cap("lighting-off");s.light=true;cap("lighting-on");s.post=false;cap("post-off");s.post=true;cap("post-on");
  std::ostringstream e;e<<"\"shadow_map\":{\"size\":1536,\"colour_format\":\"RGBA8 packed depth\",\"depth_attachment\":\"Depth32F\",\"alpha_cutoff\":0.5,\"capture_refresh\":\"every state\",\"interactive_update\":\"current-frame/on-dirty\"},\n\"captured_frames\":[";for(size_t i=0;i<records.size();i++){if(i)e<<",";e<<"\n"<<records[i];}e<<"\n]";validate(captureLighting/"capture-manifest.json",e.str());std::cout<<"LIGHTING CAPTURE PASS\n";return 0;
 }
 if(!captureDir.empty()){
  fs::create_directories(captureDir);std::vector<std::string> records;
  auto cap=[&](const std::string&name){scene.render(s);scene.save(captureDir/(name+".png"));checkGL();std::ostringstream r;r<<"{\"file\":\""<<name<<".png\",\"parameters\":";parameters(r,s);r<<"}";records.push_back(r.str());std::cout<<"Captured "<<name<<"\n";};
  cap("overview-day");s.day=.42f;cap("overview-dusk");s.day=.05f;cap("overview-night");s=Settings{};
  s.cameraX=480;s.cameraZ=1470;s.zoom=2.4f;s.leaves=0;s.wind=0;s.water=0;cap("water-off");s.water=1;cap("water-on");s.waterDebug=true;cap("water-flow-debug");s=Settings{};
  s.cameraX=490;s.cameraZ=870;s.zoom=1.4f;cap("bridge");s=Settings{};
  s.cameraX=1370;s.cameraZ=1010;s.zoom=1.6f;s.leaves=0;s.wind=0;cap("wind-off");s.wind=2;cap("wind-on");s.time=13.3f;cap("wind-later");s=Settings{};
  s.leaves=0;cap("leaves-off");s.leaves=1;cap("leaves-on");s.time=13;cap("leaves-later");s=Settings{};
  s.day=.22f;s.light=false;s.post=false;cap("lighting-off");s.light=true;s.post=true;cap("lighting-on");s=Settings{};
  s.shadows=false;cap("shadows-off");s.shadows=true;cap("shadows-on");s.heightDebug=true;cap("height-debug");s=Settings{};
  s.cameraX=1115;s.cameraZ=830;s.zoom=1.35f;s.playerX=1115;s.playerZ=978;s.playerY=0;cap("player-low");s.playerZ=700;s.playerY=160;cap("player-high");s=Settings{};
  s.cameraX=670;s.cameraZ=900;s.zoom=1.2f;s.playerX=670;s.playerZ=1040;s.playerY=0;cap("occlusion-front");s.playerZ=940;cap("occlusion-behind");s=Settings{};
  for(int i=0;i<captureFrames;i++){s.time=12+i/30.f;cap("frame-"+std::to_string(i));}
  std::ostringstream e;e<<"\"captured_frames\":[";for(size_t i=0;i<records.size();i++){if(i)e<<",";e<<"\n"<<records[i];}e<<"\n]";validate(captureDir/"capture-manifest.json",e.str());std::cout<<"CAPTURE PASS\n";return 0;
 }
 if(!captureMotion.empty()){
  for(auto view:{std::string("water"),std::string("scene")}){
   s=Settings{};if(view=="water"){s.cameraX=460;s.cameraZ=1130;s.zoom=1.6f;s.leaves=0;s.wind=0;}
   else{s.cameraX=850;s.cameraZ=950;s.zoom=1.05f;s.leaves=2;s.wind=1.4f;}
   fs::path dir=captureMotion/view;fs::create_directories(dir);
   std::ofstream manifest(dir/"parameters.json");manifest<<"{\"fps\":12,\"frames\":24,\"duration_seconds\":2,\"start_parameters\":";parameters(manifest,s);manifest<<"}\n";
   for(int i=0;i<24;i++){s.time=12+i/12.f;scene.render(s);std::ostringstream name;name<<"frame-"<<std::setfill('0')<<std::setw(3)<<i<<".png";scene.save(dir/name.str());checkGL();}
  }
  std::cout<<"MOTION CAPTURE PASS: water + scene, 24 frames each / 12 FPS.\n";return 0;
 }
 double last=glfwGetTime();int uiFrames=0;std::vector<double> uiFrameTimes;
 while(!window.ShouldClose()){
  window.PollEvents();double now=glfwGetTime();float dt=std::min(float(now-last),.1f);
  if(!captureUI.empty()&&uiFrames>=60)uiFrameTimes.push_back(now-last);
  last=now;
  if(!s.pause){s.time+=dt*s.speed;s.cloudOffset+=(.01f+s.wind*.15f)*dt*s.speed;}
  ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
  if(!ImGui::GetIO().WantTextInput){float dx=0,dz=0;if(glfwGetKey(native,GLFW_KEY_A)==GLFW_PRESS||glfwGetKey(native,GLFW_KEY_LEFT)==GLFW_PRESS)dx--;if(glfwGetKey(native,GLFW_KEY_D)==GLFW_PRESS||glfwGetKey(native,GLFW_KEY_RIGHT)==GLFW_PRESS)dx++;if(glfwGetKey(native,GLFW_KEY_W)==GLFW_PRESS||glfwGetKey(native,GLFW_KEY_UP)==GLFW_PRESS)dz--;if(glfwGetKey(native,GLFW_KEY_S)==GLFW_PRESS||glfwGetKey(native,GLFW_KEY_DOWN)==GLFW_PRESS)dz++;scene.move(s,dx,dz,dt);scene.follow(s,dt);}
  int ww,hh;glfwGetFramebufferSize(native,&ww,&hh);LabRhi::windowTarget(ww,hh,true);
  ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({310.f,float(hh)});
  ImGui::Begin("Scene controls",nullptr,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
  ImGui::Text("GRAVEYARD / SCENE LAB");ImGui::TextDisabled("Extracted art + spatial reconstruction");ImGui::Separator();
  ImGui::TextWrapped("WASD / arrows: walk. Follow the path up the stone ramp to the elevated cemetery.");
  ImGui::Checkbox("Pause time",&s.pause);ImGui::SameLine();if(ImGui::Button("Reset"))s=Settings{};
  ImGui::SliderFloat("Time",&s.time,0,120,"%.2f s");ImGui::SliderFloat("Time speed",&s.speed,0,3);
  ImGui::SeparatorText("Living scenery");ImGui::SliderFloat("Water flow",&s.water,0,2);ImGui::SliderFloat("Wind",&s.wind,0,3);ImGui::SliderFloat("Falling leaves",&s.leaves,0,2);
  ImGui::Checkbox("Water depth colour",&s.waterDepth);ImGui::Checkbox("Water refraction",&s.waterRefraction);ImGui::Checkbox("Water planar reflection",&s.waterReflection);
  ImGui::SliderFloat("Cloud density",&s.cloudDensity,0,1);ImGui::SliderFloat("Cloud strength",&s.cloudStrength,0,1.5f);
  ImGui::SliderFloat("Day / night",&s.day,0,1);ImGui::Checkbox("Lighting",&s.light);ImGui::Checkbox("Sun shadow map",&s.shadows);ImGui::Checkbox("Contact / screen AO",&s.ao);ImGui::Checkbox("Glow / atmosphere",&s.post);
  if(ImGui::CollapsingHeader("Light and shadow study")){
   ImGui::Checkbox("Override preset sun direction",&s.manualSun);if(s.manualSun){ImGui::SliderFloat("Sun azimuth",&s.sunAzimuth,-180,180,"%.1f deg");ImGui::SliderFloat("Sun elevation",&s.sunElevation,15,85,"%.1f deg");}ImGui::SliderFloat("PCF radius",&s.shadowPcf,0,3,"%.2f texels");ImGui::SliderFloat("Depth bias",&s.shadowBias,.0001f,.003f,"%.5f");
   ImGui::SliderFloat("Contact strength",&s.aoStrength,0,1.5f);ImGui::SliderFloat("Contact radius",&s.aoRadius,1,12,"%.1f px");ImGui::Checkbox("Shadow visibility debug",&s.shadowDebug);ImGui::Checkbox("Contact AO only",&s.aoDebug);ImGui::Checkbox("Legacy flat silhouette",&s.legacyShadows);
   ImGui::SliderFloat("Ambient scale",&s.ambientScale,0,2);ImGui::SliderFloat("Sun scale",&s.sunScale,0,2);ImGui::SliderFloat("Backlight scale",&s.backScale,0,2);
   ImGui::Text("1536 x 1536 | same-frame batched casters");ImGui::Text("Last shadow pass: %d RHI draws",scene.shadowDraws);
  }
  ImGui::SeparatorText("Space and camera");ImGui::Checkbox("Follow player",&s.follow);ImGui::SliderFloat("Zoom",&s.zoom,.65f,2.4f);ImGui::Checkbox("Height debug",&s.heightDebug);ImGui::Checkbox("Water bands debug",&s.waterDebug);ImGui::Checkbox("Pixel magnifier",&s.showMagnifier);
  if(!s.follow){ImGui::SliderFloat("Camera X",&s.cameraX,200,1700);ImGui::SliderFloat("Camera south Z",&s.cameraZ,300,1400);}
  if(ImGui::Button("Cemetery / high ground")){s.playerX=1115;s.playerZ=690;s.playerY=160;s.cameraX=1115;s.cameraZ=650;}
  if(ImGui::Button("Stream / bridge")){s.playerX=650;s.playerZ=940;s.playerY=0;s.cameraX=610;s.cameraZ=870;}
  if(ImGui::Button("Save current PNG")){scene.render(s);scene.save(module/"Captures/current.png");}
  ImGui::Separator();ImGui::Text("Support height: %.1f px",s.playerY);ImGui::Text("World: %.1f / %.1f",s.playerX,s.playerZ);ImGui::Text("Camera X angle: 53.130104 deg");ImGui::Text("Depth: actual Depth32F");ImGui::Text("Assets: %d | objects: %d",int(scene.assets.size()),int(scene.objects.size()));ImGui::TextWrapped("New diorama layout. The report distinguishes source evidence, reused art, and approximate effects.");
  ImGui::End();
  ImGui::SetNextWindowPos({310,0});ImGui::SetNextWindowSize({float(ww-310),float(hh)});
  ImGui::Begin("Walkable 2.5D scene",nullptr,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
  auto avail=ImGui::GetContentRegionAvail();float vh=std::min(avail.y-72,avail.x*9/16),vw=vh*16/9;vw=std::max(vw,1.f);vh=std::max(vh,1.f);
  auto pos=ImGui::GetCursorScreenPos();scene.render(s);LabRhi::windowTarget(ww,hh);
  ImGui::Image((ImTextureID)(intptr_t)scene.output.tex.id,{vw,vh},{0,1},{1,0});
  if(s.showMagnifier&&ImGui::IsItemHovered()){
   auto m=ImGui::GetIO().MousePos;float u=clamp((m.x-pos.x)/vw,0,1),v=clamp((m.y-pos.y)/vh,0,1);float radius=.035f;
   ImGui::BeginTooltip();ImGui::Text("Nearest filtered source pixels");ImGui::Image((ImTextureID)(intptr_t)scene.output.tex.id,{260,146},{u-radius,1-v+radius*9/16},{u+radius,1-v-radius*9/16});ImGui::EndTooltip();
  }
  ImGui::Text("%.1f FPS | %d RHI draws | WASD / arrow keys move | space remains three dimensional",ImGui::GetIO().Framerate,scene.draws);
  ImGui::TextDisabled("Water + leaves + pinned wind + sun depth shadows + contact AO + height support");
  ImGui::End();ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  if(!captureUI.empty()&&++uiFrames>=180){
   saveUI(captureUI,ww,hh);checkGL();double sum=0;for(auto t:uiFrameTimes)sum+=t;std::sort(uiFrameTimes.begin(),uiFrameTimes.end());
   double mean=uiFrameTimes.empty()?0:sum/uiFrameTimes.size(),median=uiFrameTimes.empty()?0:uiFrameTimes[uiFrameTimes.size()/2],p95=uiFrameTimes.empty()?0:uiFrameTimes[size_t((uiFrameTimes.size()-1)*.95)];
   size_t cachedBytes=0;for(auto&[key,m]:scene.fixedMeshes)cachedBytes+=size_t(m.count)*sizeof(Vertex);
   fs::create_directories(module/"Evidence");std::ofstream perf(module/"Evidence/performance-validation.json");
   perf<<"{\"scope\":\"editor + scene + swap, hidden window capture; 60 warmup frames then 120 measured frames; no independent cross-device benchmark\",\"renderer\":\""<<glGetString(GL_RENDERER)<<"\",\"scene_size\":["<<scene.w<<","<<scene.h<<"],\"measured_frames\":"<<uiFrameTimes.size()<<",\"mean_frame_ms\":"<<mean*1000<<",\"median_frame_ms\":"<<median*1000<<",\"p95_frame_ms\":"<<p95*1000<<",\"mean_fps\":"<<(mean>0?1/mean:0)<<",\"static_vertex_buffers\":"<<scene.fixedMeshes.size()<<",\"cached_vertex_bytes\":"<<cachedBytes<<",\"per_frame_dynamic_mesh_uploads\":"<<scene.streamUploads<<",\"rhi_draws\":"<<scene.draws<<",\"target_debug_optimization\":\"O2 with debug symbols; shared Render unchanged\"}\n";
   std::cout<<"UI captured "<<captureUI.string()<<" | warm FPS "<<(mean>0?1/mean:0)<<" | static VBOs "<<scene.fixedMeshes.size()<<" | streamed draws "<<scene.streamUploads<<"\n";break;
  }
  LabRhi::backend().EndFrame({true});
 }
 ImGui_ImplOpenGL3_Shutdown();ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();return 0;
}catch(const std::exception&e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}}
