#pragma once
#include "Render/Private/Pipeline/fiber_volume.h"
#include <array>
#include <future>
#include <iostream>
#include <limits>
#include <vector>

namespace Render::TestData {
// The oracle reads the independently populated 3D cells, never the atlas UVs
// or the optimized support-factor expression. Run on an already started RHI
// device; the caller owns adapter selection and GL/Vulkan validation policy.
inline void RunFiberVolumeSamplingGpuOracle(RHIDevice& device) {
    const auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto read=[&](RenderResourceHandle<RHITextureSpec> image) {
        auto promise=std::make_shared<std::promise<std::vector<float>>>();auto future=promise->get_future();
        device.async_ExecuteCode([&device,image,promise] {
            try{promise->set_value(device.BackendDiagnostics()->ReadTexture(image));}
            catch(...){promise->set_exception(std::current_exception());}
        });
        return future.get();
    };
    const auto submit=[&](RHIFrameEncoder& encoder) {
        require(encoder.End(false),"End fiber interpolation oracle frame");
        auto done=std::make_shared<bool>(false);
        require(device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[done]{*done=true;}),"Submit fiber interpolation oracle");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(!*done){device.returnSystem.DrainCallbacks();require(std::chrono::steady_clock::now()<deadline,"Fiber interpolation oracle timeout");if(!*done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
    };
    double maximumLegacyError=0,maximumLinearError=0,maximumLinearVisibilityError=0;
    std::uint64_t componentCount=0;
    for(const auto grid:std::array<glm::ivec3,3>{{{5,3,5},{8,9,10},{1,2,1}}}) {
        const int columns=int(std::ceil(std::sqrt(float(grid.z))));
        const std::uint32_t width=std::uint32_t(grid.x*columns),height=std::uint32_t(grid.y*((grid.z+columns-1)/columns));
        // Poison unused tiles. Boundary clamping must exclude these values.
        std::vector<glm::vec4> densityAtlas(std::size_t(width)*height,glm::vec4(4096,-4096,2048,-2048));
        std::vector<glm::vec4> visibilityAtlas(densityAtlas.size(),glm::vec4(-4096));
        std::vector<glm::vec4> density(std::size_t(grid.x)*grid.y*grid.z),visibility(density.size());
        const auto index=[&](int x,int y,int z){return (std::size_t(z)*grid.y+y)*grid.x+x;};
        for(int z=0;z<grid.z;++z)for(int y=0;y<grid.y;++y)for(int x=0;x<grid.x;++x) {
            const auto i=index(x,y,z);const unsigned hash=unsigned(17*x+31*y+47*z);
            // All values are exactly representable in the original RGBA16F.
            density[i]={float(hash%257)*.125f,float(int(hash%61)-30)*.25f,
                float(int((hash*7)%97)-48)*.125f,float(int((hash*11)%73)-36)*.125f};
            visibility[i]={float(hash%65)/64.f,float((hash*3)%65)/64.f,
                float((hash*7)%65)/64.f,float((hash*13)%65)/64.f};
            const auto at=std::size_t(z/columns*grid.y+y)*width+z%columns*grid.x+x;
            densityAtlas[at]=density[i];visibilityAtlas[at]=visibility[i];
        }
        std::vector<glm::vec4> positions;
        const auto add=[&](glm::vec3 p){positions.emplace_back(p+glm::vec3(.5f),1.f);};
        // Face/edge/corner support, first/last texel centers and tile seams.
        std::array<std::vector<float>,3> axes;
        for(int axis=0;axis<3;++axis)axes[axis]={-1.25f,-1.f,-.875f,-.5f,0.f,.125f,
            float(grid[axis]-1)*.5f,float(grid[axis])-1.f,float(grid[axis])-.5f,float(grid[axis]),float(grid[axis])+.25f};
        for(float z:axes[2])for(float y:axes[1])for(float x:axes[0])add({x,y,z});
        // Non-dyadic normalized atlas UVs and arbitrary interpolation phases.
        unsigned state=0x789abcdu;
        for(int i=0;i<512;++i){glm::vec3 p;for(int axis=0;axis<3;++axis){state=state*1664525u+1013904223u;p[axis]=-1.5f+float(state&0xffffu)/65536.f*float(grid[axis]+2);}add(p);}
        const auto oracle=[&](const std::vector<glm::vec4>& cells,glm::vec3 local,double outside) {
            std::array<double,4> result{};const glm::dvec3 p=glm::dvec3(local)-glm::dvec3(.5);
            const glm::ivec3 first(glm::floor(p));
            for(int z=first.z;z<=first.z+1;++z)for(int y=first.y;y<=first.y+1;++y)for(int x=first.x;x<=first.x+1;++x) {
                const double weight=(1.-std::abs(p.x-x))*(1.-std::abs(p.y-y))*(1.-std::abs(p.z-z));
                const bool valid=x>=0&&x<grid.x&&y>=0&&y<grid.y&&z>=0&&z<grid.z;
                for(int c=0;c<4;++c)result[c]+=weight*(valid?double(cells[index(x,y,z)][c]):outside);
            }
            return result;
        };
        PipelineDetail::ResourceBuilder resources(device);
        const auto inputTexture=[&](std::uint32_t w,std::uint32_t h,const std::vector<glm::vec4>& values,RHITextureFormat format) {
            CreateRHITextureSpec desc;desc.width=w;desc.height=h;desc.mipmaps=false;
            desc.textureDataStoreType=format;desc.textureUseType=RHITextureFormat::RGBA32F;desc.filterMode=RHIFilterMode::Linear;
            desc.addressMode=RHIAddressMode::ClampToEdge;desc.data=values.data();
            return resources.Create<RHITextureSpec>([&](auto callback){device.async_CreateTexture(desc,callback);});
        };
        const auto densityTexture=inputTexture(width,height,densityAtlas,RHITextureFormat::RGBA16F);
        const auto visibilityTexture=inputTexture(width,height,visibilityAtlas,RHITextureFormat::RGBA16F);
        const auto positionsTexture=inputTexture(std::uint32_t(positions.size()),1,positions,RHITextureFormat::RGBA32F);
        const auto output=resources.Texture(std::uint32_t(positions.size()),1,RHITextureFormat::RGBA32F);
        const auto target=resources.Target(output,{});
        const auto mesh=resources.FullscreenMesh();
        const auto uniform=resources.Uniform(sizeof(PipelineDetail::FiberVolumeConstants));
        std::string fragment=R"GLSL(#version 450 core
layout(binding=8) uniform sampler2D auditPositions;
layout(binding=12) uniform sampler2D auditField;
layout(location=0) out vec4 result;
)GLSL";
        fragment+=PipelineDetail::FiberVolumeBlockGLSL();fragment+=R"GLSL(
void main(){
    vec3 p=texelFetch(auditPositions,ivec2(int(gl_FragCoord.x),0),0).xyz;
    result=fiberOpaque.z>.5?FiberVolumeVisibilitySample(auditField,p):FiberVolumeSample(auditField,p);
}
)GLSL";
        const auto pipeline=resources.FullscreenPipeline(FullscreenVertexShader(),fragment);
        const auto owner=resources.Finish();
        PipelineDetail::FiberVolumeConstants constants;
        constants.minimumStep={0,0,0,1};constants.size={glm::vec3(grid),float(columns)};
        constants.atlas={float(width),float(height),1,1};
        for(int field=0;field<2;++field)for(int filtering=0;filtering<2;++filtering) {
            constants.opaque.z=float(field);constants.opaque.w=float(filtering);
            auto encoder=device.BeginFrame({0,std::uint32_t(positions.size()),1});require(encoder.KeepAlive(owner),"Pin fiber interpolation resources");
            require(encoder.SetRenderTarget({target,std::uint32_t(positions.size()),1,RHICommand::ClearColor}),"Bind fiber interpolation output");
            PipelineDetail::FiberVolumeUpload(encoder,uniform,constants);
            require(encoder.BindTexture(positionsTexture,8)&&encoder.BindTexture(field?visibilityTexture:densityTexture,12)&&
                encoder.BindPipeline(pipeline)&&encoder.BindMesh(mesh)&&encoder.DrawIndexed({3}),"Draw fiber interpolation oracle");
            submit(encoder);const auto values=read(output);require(values.size()==positions.size()*4,"Fiber interpolation readback size");
            const auto& cells=field?visibility:density;
            for(std::size_t i=0;i<positions.size();++i){const auto expected=oracle(cells,glm::vec3(positions[i]),field?1.:0.);
                const glm::vec3 p=glm::vec3(positions[i])-glm::vec3(.5f);
                const bool outside=p.x<=-1.f||p.y<=-1.f||p.z<=-1.f||p.x>=grid.x||p.y>=grid.y||p.z>=grid.z;
                for(int c=0;c<4;++c){const double actual=values[4*i+c],error=std::abs(actual-expected[c]);
                    require(std::isfinite(actual),"Nonfinite fiber interpolation result");
                    if(!filtering)maximumLegacyError=std::max(maximumLegacyError,error);
                    else if(field)maximumLinearVisibilityError=std::max(maximumLinearVisibilityError,error);
                    else maximumLinearError=std::max(maximumLinearError,error);
                    // OpenGL/Vulkan linear filtering may quantize interpolation
                    // weights. Eight fraction bits permit <=2*range/256 for XY;
                    // Z/support remain explicit float. Report measured errors.
                    const double range=field?1.:(c==0?32.:c==1?15.:c==2?12.:9.);
                    const double tolerance=filtering?(2.*range/256.+3e-5):3e-5;
                    require(error<=tolerance,"Fiber hardware/manual interpolation exceeded independent constant-border oracle");
                    if(outside)require(error<2e-6,"Fiber finite-grid zero/one border changed or leaked another atlas tile");
                    if(field)require(actual>=-1e-6&&actual<=1.+1e-6,"Fiber visibility escaped zero-one range");
                    ++componentCount;
                }
            }
        }
    }
    // Exercise production Prepare/cache rather than only the sampling helper.
    StrandMesh geometry(device);const std::array<StrandVertex,4> vertices{{
        {{-.08f,-.3f,0,.001f},{0,1,0,0}},{{-.08f,.3f,0,.001f},{0,1,0,1}},
        {{-.2f,.03f,-.1f,.0015f},{1,0,0,0}},{{.2f,.03f,.1f,.0015f},{1,0,0,1}}}};
    const std::array<std::uint32_t,4> segments{0,1,2,3};require(geometry.Update(vertices,segments),"Create interpolation cache fixture");
    StrandRenderItem item;item.geometry=geometry.Snapshot();item.material.spatialVolume=true;item.material.volumeResolution=12;
    item.material.volumeVoxelSize=.03f;item.material.volumeOpaqueBlockers=false;item.material.volumeHardwareFiltering=true;
    item.material.volumeSkyTransmission=true;
    PipelineDetail::StrandPassView view;view.cameraPosition={0,0,2};view.sunDirection={-.7f,-.3f,-.2f};view.sunIntensity=1;
    view.sky.options.x=1;for(auto& light:view.lighting.lightColors)light=glm::vec4(0);
    PipelineDetail::FiberVolumePass volume(device);std::shared_ptr<const void> blockerOwner;
    const auto prepare=[&]{volume.Begin(1);auto encoder=device.BeginFrame({0,1,1});if(blockerOwner)require(encoder.KeepAlive(blockerOwner),"Pin interpolation fixture opaque depth");volume.Prepare(encoder,0,item,view);submit(encoder);return volume.Statistics();};
    require(prepare().passes==4,"First fiber interpolation fixture did not build density and N");
    const auto linearTextures=volume.DebugTextures();const auto originalDensity=read(linearTextures.density);
    const auto linearPaths=read(linearTextures.paths),linearAmbient=read(linearTextures.pathsAmbient);
    require(prepare().passes==0,"Unchanged hardware filtering invalidated fiber cache");
    item.material.volumeHardwareFiltering=false;require(prepare().passes==3,"Filtering mode did not rebuild N independently of density");
    require(originalDensity==read(volume.DebugTextures().density),"Filtering changed physical density or tangent moments");
    const auto legacyPaths=read(volume.DebugTextures().paths),legacyAmbient=read(volume.DebugTextures().pathsAmbient);
    double maximumPathDifference=0;
    for(const auto& pair:std::array<std::pair<const std::vector<float>*,const std::vector<float>*>,2>{{{&linearPaths,&legacyPaths},{&linearAmbient,&legacyAmbient}}}){
        require(pair.first->size()==pair.second->size(),"Fiber mode path dimensions differ");
        for(std::size_t i=0;i<pair.first->size();++i){const double a=(*pair.first)[i],b=(*pair.second)[i];
            require(std::isfinite(a)&&std::isfinite(b)&&a>=0&&b>=0,"Filtering generated invalid N");
            maximumPathDifference=std::max(maximumPathDifference,std::abs(a-b));
            require(std::abs(a-b)<=.015*(1.+std::max(a,b)),"Integrated N changed beyond hardware interpolation precision budget");}
    }
    require(prepare().passes==0,"Unchanged legacy filtering invalidated fiber cache");
    item.material.volumeHardwareFiltering=true;require(prepare().passes==3,"Restoring hardware filtering did not invalidate N");
    require(read(volume.DebugTextures().paths)==linearPaths&&read(volume.DebugTextures().pathsAmbient)==linearAmbient,
        "Restoring hardware filtering did not deterministically restore N");
    PipelineDetail::ResourceBuilder blockerResources(device);
    const auto opaqueDepth=blockerResources.Texture(16,16,RHITextureFormat::Depth32F);
    const auto opaqueTarget=blockerResources.Target({},opaqueDepth);blockerOwner=blockerResources.Finish();
    {auto encoder=device.BeginFrame({0,16,16});require(encoder.KeepAlive(blockerOwner),"Pin constant opaque depth");
        RHICommand::SetRenderTarget command;command.target=opaqueTarget;command.width=command.height=16;
        command.clearFlags=RHICommand::ClearDepth;command.clearDepth=.5f;
        require(encoder.SetRenderTarget(command),"Clear interpolation fixture opaque depth");submit(encoder);}
    item.material.volumeOpaqueBlockers=true;item.material.volumeDepthThreshold=.12f;view.opaqueDepth=opaqueDepth;view.opaqueContentRevision=1;
    require(prepare().passes==3,"Enabling G must retain physical density and N");
    const auto shell=read(volume.DebugTextures().opaqueBlockers);
    const auto linearG=read(volume.DebugTextures().directVisibility),linearExtraG=read(volume.DebugTextures().extraVisibility);
    float minimumG=1;for(float value:linearG)minimumG=std::min(minimumG,value);
    require(minimumG<.95f,"Interpolation fixture did not exercise an opaque blocker");
    item.material.volumeHardwareFiltering=false;const auto blockerSwitch=prepare();
    require(blockerSwitch.passes==5&&blockerSwitch.blockerPasses==2,
        "Filtering mode must retrace N/G while retaining the opaque shell");
    require(shell==read(volume.DebugTextures().opaqueBlockers)&&originalDensity==read(volume.DebugTextures().density),
        "Filtering rebuilt or changed the opaque shell, density, or tangent moments");
    const auto legacyG=read(volume.DebugTextures().directVisibility),legacyExtraG=read(volume.DebugTextures().extraVisibility);
    double maximumGDifference=0;
    for(const auto& pair:std::array<std::pair<const std::vector<float>*,const std::vector<float>*>,2>{{{&linearG,&legacyG},{&linearExtraG,&legacyExtraG}}}){
        require(pair.first->size()==pair.second->size(),"Fiber G mode dimensions differ");
        for(std::size_t i=0;i<pair.first->size();++i){const double a=(*pair.first)[i],b=(*pair.second)[i];
            require(std::isfinite(a)&&std::isfinite(b)&&a>=0&&a<=1&&b>=0&&b<=1,"Filtering generated invalid G visibility");
            maximumGDifference=std::max(maximumGDifference,std::abs(a-b));
            require(std::abs(a-b)<=.015,"Integrated G changed beyond hardware interpolation precision budget");}
    }
    item.material.volumeHardwareFiltering=true;require(prepare().passes==5,"Restoring hardware filtering did not retrace N and G");
    require(read(volume.DebugTextures().directVisibility)==linearG&&read(volume.DebugTextures().extraVisibility)==linearExtraG,
        "Restoring hardware filtering did not deterministically restore G");
    item.material.volumeDepthThreshold=0;require(prepare().passes==3,"Zero-thickness shell must only rebuild G");
    for(const auto image:{volume.DebugTextures().directVisibility,volume.DebugTextures().extraVisibility})
        for(float value:read(image))require(value==1.f,"Empty opaque shell must yield exact visibility one");
    volume.Reset();
    std::cout<<"Fiber volume interpolation: "<<componentCount<<" independent component checks; legacy max="<<maximumLegacyError
        <<", hardware signed-density max="<<maximumLinearError<<", hardware visibility max="<<maximumLinearVisibilityError
        <<", integrated N max="<<maximumPathDifference<<", integrated G max="<<maximumGDifference
        <<"; noncubic/degenerate grids, tile seams, poisoned padding, constant borders, N/G-only invalidation and empty-shell visibility one passed\n";
}
} // namespace Render::TestData
