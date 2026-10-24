#pragma once
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Public/Pipeline/strand_rendering.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_encoder.h"
#include "Render/Private/Pipeline/strand_curve_pass.h"
#include "Render/Private/Pipeline/strand_curve_shaders.h"
#include <algorithm>
#include <array>
#include <memory>

namespace Render::PipelineDetail {
// Shared source strings keep the legacy ordered caster and the HOST DOM
// producer on the same geometry and analytic projected-coverage contract.
inline std::string StrandShadowVertexShader(){return R"GLSL(#version 450 core
layout(location=0) in vec4 positionRadius;
layout(location=1) in vec4 tangentArc;
layout(std140,binding=0) uniform ShadowView {mat4 vp;vec4 lightDirection;vec4 viewport;};
layout(std140,binding=1) uniform ShadowObject {mat4 model;};
layout(std140,binding=2) uniform ShadowMaterial {vec4 widths;};
layout(location=0) noperspective out float side;
layout(location=1) out float halfWidth;
vec3 Safe(vec3 v,vec3 f){return dot(v,v)>1e-12?normalize(v):f;}
void main(){
    vec3 p=(model*vec4(positionRadius.xyz,1)).xyz;
    vec3 t=Safe(mat3(model)*tangentArc.xyz,vec3(0,1,0));
    vec3 axis=abs(t.y)<.95?vec3(0,1,0):vec3(1,0,0);
    vec3 s=Safe(cross(t,lightDirection.xyz),Safe(cross(t,axis),vec3(1,0,0)));
    float scale=max(length(model[0].xyz),max(length(model[1].xyz),length(model[2].xyz)));
    float w=max(2.0*abs(positionRadius.w)*widths.x,widths.y);
    if(widths.z>0.0)w=min(w,widths.z);
    vec4 c=vp*vec4(p,1),e=vp*vec4(p+s*(.5*w*scale),1);
    vec2 delta=(e.xy/e.w-c.xy/c.w)*viewport.xy*.5;
    halfWidth=length(delta);
    vec2 n=halfWidth>1e-7?delta/halfWidth:vec2(1,0);
    float support=.5*(abs(n.x)+abs(n.y));
    float rasterHalf=max(halfWidth+support,.5);
    side=positionRadius.w<0.0?-1.0:1.0;
    c.xy+=side*n*rasterHalf*2.0/viewport.xy*c.w;
    gl_Position=c;
}
)GLSL";}
inline std::string StrandShadowFragmentShader(){return R"GLSL(#version 450 core
layout(location=0) noperspective in float side;
layout(location=1) in float halfWidth;
layout(std140,binding=2) uniform ShadowMaterial {vec4 widths;};
float Cdf(float x,vec2 n){
    float a=.5*max(n.x,n.y),b=.5*min(n.x,n.y);
    if(b<1e-5)return clamp(.5+x/max(2.0*a,1e-6),0.0,1.0);
    float sum=a+b,d=a-b;
    if(x<=-sum)return 0.0;if(x>=sum)return 1.0;
    if(x<-d){float q=x+sum;return q*q/(8.0*a*b);}
    if(x<=d)return .5+x/(2.0*a);
    float q=sum-x;return 1.0-q*q/(8.0*a*b);
}
void main(){
    vec2 dd=vec2(dFdx(side),dFdy(side));float g=max(length(dd),1e-6);
    vec2 n=abs(dd)/g;float distancePixels=abs(side)/g;
    float a=clamp((Cdf(halfWidth-distancePixels,n)-Cdf(-halfWidth-distancePixels,n))*widths.w,0.0,1.0);
    ivec2 p=ivec2(gl_FragCoord.xy)&ivec2(7);
    int rank=0;
    for(int bit=0;bit<3;++bit){int x=(p.x>>bit)&1,y=(p.y>>bit)&1;rank|=((x^y)*2+y)<<(2*(2-bit));}
    // Decorrelation across physical segments avoids identical partial
    // coverage masks for every overlapping fiber. The two ribbon triangles
    // share one phase. This remains quantized CSM coverage, not deep opacity.
    uint phase=uint(gl_PrimitiveID/2)*1664525u+1013904223u;
    phase^=phase>>16;rank=(rank+int(phase&63u))&63;
    if(a<=(float(rank)+.5)/64.0)discard;
}
)GLSL";}
// Host CSM adapter. Physical projected area is filtered before a stable 8x8
// ordered coverage test. This is not the game's unobserved shadow PSO.
class StrandShadowPass {
    RHIDevice& device_;
    std::shared_ptr<const void> owner_;
    RenderResourceHandle<PipelineSpec> pipeline_;
    RenderResourceHandle<UniformBufferSpec> view_,object_,material_;
    struct CurveCache {
        std::unique_ptr<StrandCurvePass> pass;
        glm::mat4 projection{1};
        std::uint32_t tileSize=0;
        std::uint64_t lastUse=0;
        bool valid=false;
    };
    std::array<CurveCache,4> curveCaches_;
    std::uint64_t curveCacheUse_=0;
    StrandCurveStatistics curveStatistics_{};
    std::shared_ptr<const void> curveOwner_;
    RenderResourceHandle<PipelineSpec> curvePipeline_;
    std::string vertexSource_,fragmentSource_;
    static bool SameProjection(const glm::mat4& a,const glm::mat4& b){
        for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(a[c][r]!=b[c][r])return false;
        return true;
    }
    StrandCurvePass& CurveCacheFor(const glm::mat4& projection,std::uint32_t tileSize){
        CurveCache* selected=nullptr;
        for(auto& cache:curveCaches_)if(cache.valid&&cache.tileSize==tileSize&&SameProjection(cache.projection,projection)){
            selected=&cache;break;
        }
        if(!selected)for(auto& cache:curveCaches_)if(!cache.valid){selected=&cache;break;}
        if(!selected)selected=&*std::min_element(curveCaches_.begin(),curveCaches_.end(),
            [](const auto& a,const auto& b){return a.lastUse<b.lastUse;});
        if(!selected->pass)selected->pass=std::make_unique<StrandCurvePass>(device_);
        selected->projection=projection;selected->tileSize=tileSize;selected->valid=true;selected->lastUse=++curveCacheUse_;
        // The slot's immutable atlas generations remain pinned by each queued
        // draw. LRU reuse never overwrites the previously submitted texture.
        return *selected->pass;
    }
    struct alignas(16) View {glm::mat4 vp{1};glm::vec4 direction{0},viewport{1};};
    void Initialize() {
        if(owner_)return;
        ResourceBuilder b(device_);
        const std::string vs=StrandShadowVertexShader();
        const std::string fs=StrandShadowFragmentShader();
        CreateGraphicShaderDesc desc;
        desc.vertexShaderSource=b.Source(ShaderSourceType::Vertex,vs);
        desc.fragmentShaderSource=b.Source(ShaderSourceType::Fragment,fs);
        auto shader=b.Create<ShaderProgramSpec>([&](auto cb){device_.async_CreateGraphicShader(desc,cb);});
        PipelineSpec spec;spec.shaderProgram=shader;
        spec.expectVertexLayout={{VertexFieldType::Vec4,VertexFieldType::Vec4,VertexFieldType::Vec2,VertexFieldType::Vec4,VertexFieldType::Vec4}};
        spec.cullMode=CullMode::None;spec.depthTest=spec.depthWrite=true;spec.depthCompare=CompareOp::LessEqual;
        pipeline_=b.Create<PipelineSpec>([&](auto cb){device_.async_CreatePipeline({spec},cb);});
        view_=b.Uniform(sizeof(View));object_=b.Uniform(64);material_=b.Uniform(16);owner_=b.Finish();
        vertexSource_=vs;fragmentSource_=fs;
    }
    void InitializeCurve(){
        if(curveOwner_)return;
        auto vs=vertexSource_,fs=fragmentSource_;
        vs.insert(vs.find("void main()"),"layout(location=2) in vec2 colorCoordinates;\nlayout(location=3) in vec4 previousPositionRadius;\nlayout(location=4) in vec4 previousTangentArc;\n");
        vs=StrandCurveVertexMain(std::move(vs));
        vs.insert(vs.find("void main()"),"layout(location=2) flat out uint strandCurveSegment;\n");
        const auto output=vs.find("    gl_Position=c;");
        if(output==std::string::npos)throw std::runtime_error("Curve shadow requires shared ribbon output");
        vs.insert(output,"    strandCurveSegment=curve.segment;\n");
        fs.insert(fs.find('\n')+1,"layout(location=2) flat in uint strandCurveSegment;\n");
        StrandCurveReplace(fs,"uint(gl_PrimitiveID/2)","strandCurveSegment");
        ResourceBuilder b(device_);CreateGraphicShaderDesc desc;
        desc.vertexShaderSource=b.Source(ShaderSourceType::Vertex,vs);
        desc.fragmentShaderSource=b.Source(ShaderSourceType::Fragment,fs);
        const auto shader=b.Create<ShaderProgramSpec>([&](auto cb){device_.async_CreateGraphicShader(desc,cb);});
        PipelineSpec spec;spec.shaderProgram=shader;
        spec.expectVertexLayout={{VertexFieldType::Vec4,VertexFieldType::Vec4,VertexFieldType::Vec2,VertexFieldType::Vec4,VertexFieldType::Vec4}};
        spec.cullMode=CullMode::None;spec.depthTest=spec.depthWrite=true;spec.depthCompare=CompareOp::LessEqual;
        curvePipeline_=b.Create<PipelineSpec>([&](auto cb){device_.async_CreatePipeline({spec},cb);});curveOwner_=b.Finish();
    }
    static void Check(bool ok){if(!ok)throw std::runtime_error("Cannot record strand CSM caster");}
public:
    explicit StrandShadowPass(RHIDevice& device):device_(device){}
    std::uint32_t Record(RHIFrameEncoder& encoder,std::span<const StrandRenderItem> items,
                         const glm::mat4& vp,glm::vec3 towardLight,std::uint32_t tileSize){
        curveStatistics_={};
        bool any=false,anyCurves=false;
        for(const auto& i:items){const bool caster=bool(i.geometry)&&!i.guides&&i.material.castDirectionalShadows;
            any|=caster;anyCurves|=caster&&i.material.sourceCurveGeometry;}
        if(!any)return 0;
        Initialize();Check(encoder.KeepAlive(owner_));
        auto* curves=anyCurves?&CurveCacheFor(vp,tileSize):nullptr;
        if(curves)curves->Begin(items.size());
        View v;v.vp=vp;v.direction=glm::vec4(towardLight,0);v.viewport=glm::vec4(tileSize,tileSize,0,0);
        Check(encoder.UpdateUniformBufferBytes(view_,&v,sizeof(v),0)&&encoder.BindUniformBuffer(view_,0)&&encoder.BindPipeline(pipeline_));
        std::uint32_t draws=0;
        for(std::size_t index=0;index<items.size();++index){const auto& i=items[index];
            if(!i.geometry||i.guides||!i.material.castDirectionalShadows)continue;
            const float q=i.strandCountFraction>1e-5f?1.f/i.strandCountFraction:100000.f;
            const auto& m=i.material;glm::vec4 widths(m.widthScale*q,m.minWidthMetres*q,m.maxWidthMetres*q,m.opacity);
            Check(encoder.KeepAlive(i.geometry)&&encoder.UpdateUniformBufferBytes(object_,&i.model,64,0)&&encoder.BindUniformBuffer(object_,1)&&
                encoder.UpdateUniformBufferBytes(material_,&widths,16,0)&&encoder.BindUniformBuffer(material_,2));
            if(i.material.sourceCurveGeometry){
                InitializeCurve();StrandPassView lightView;lightView.viewProjection=vp;
                // Independent light-space list: the camera's visibility list
                // must never remove an offscreen caster from this CSM draw.
                curves->Prepare(index,i,lightView,tileSize,tileSize);
                Check(encoder.KeepAlive(curveOwner_)&&encoder.BindPipeline(curvePipeline_));
                curves->Bind(encoder,index);curves->Draw(encoder,index);
            }else Check(encoder.BindPipeline(pipeline_)&&encoder.BindMesh(i.geometry->Mesh())&&encoder.DrawIndexed({i.geometry->IndexCount()}));
            ++draws;
        }
        if(curves)curveStatistics_=curves->Statistics();
        return draws;
    }
    // Most recent light-space Record only; four static cascades should each
    // report uploads0 after their first preparation. Not a native GPU counter.
    const StrandCurveStatistics& CurveStatistics()const{return curveStatistics_;}
    void Reset(){for(auto& cache:curveCaches_){if(cache.pass)cache.pass->Shutdown();cache=CurveCache{};}
        curveCacheUse_=0;curveStatistics_={};curveOwner_.reset();curvePipeline_={};owner_.reset();pipeline_={};view_={};object_={};material_={};vertexSource_.clear();fragmentSource_.clear();}
};
}
