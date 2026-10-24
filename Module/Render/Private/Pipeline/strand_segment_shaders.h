#pragma once
#include "Render/Private/Pipeline/strand_shaders.h"
#include <iterator>

namespace Render::PipelineDetail {
// The compute and fragment producers use the SAME emitted lighting block.
// This generation contract fails loudly if its boundaries are reorganized.
inline std::string StrandSegmentComputeShader(bool diagnostics,bool pointPaths) {
    const auto fragment=StrandFragmentShader(false,0,diagnostics,pointPaths);
    const auto main=fragment.find("void main()");
    const auto begin=fragment.find("    vec3 base=mix(",main);
    const auto end=fragment.find("#ifdef STRAND_PEEL_LAYER",begin);
    if(main==std::string::npos||begin==std::string::npos||end==std::string::npos)
        throw std::runtime_error("Segment producer shared lighting contract changed");
    auto source=fragment.substr(0,main);
    // Only volume rasterization uses this atlas-pixel helper. Its fragment
    // builtin is not legal in a compute translation unit, even when unused.
    const auto atlasVoxel=source.find("ivec3 FiberAtlasVoxel()"),sampleLegacy=source.find("vec4 FiberVolumeSampleLegacy(",atlasVoxel);
    if(atlasVoxel==std::string::npos||sampleLegacy==std::string::npos)
        throw std::runtime_error("Segment producer volume block contract changed");
    source.erase(atlasVoxel,sampleLegacy-atlasVoxel);
    // Fragment-only interfaces are replaced by per-invocation shading inputs.
    const std::string declarations[]={
        "layout(location=0) in vec3 strandWorldPosition;","layout(location=1) in vec3 strandWorldTangent;",
        "layout(location=2) in float strandArc;","layout(location=3) noperspective in float strandSide;",
        "layout(location=4) in float strandCoverage;","layout(location=5) in vec2 strandColorCoordinates;",
        "layout(location=7) in vec3 strandShutterCamera;","layout(location=0) out vec4 outColor;"};
    const std::string replacements[]={"vec3 strandWorldPosition;","vec3 strandWorldTangent;","float strandArc;",
        "","","vec2 strandColorCoordinates;","vec3 strandShutterCamera;",""};
    for(std::size_t i=0;i<std::size(declarations);++i) {
        const auto& text=declarations[i];
        const auto at=source.find(text);
        if(at==std::string::npos)throw std::runtime_error("Segment producer fragment interface changed");
        source.replace(at,text.size(),replacements[i]);
    }
    source+=R"GLSL(
layout(local_size_x=64) in;
layout(std140,binding=1) uniform StrandObject {mat4 strandModel;};
layout(std140,binding=8) uniform StrandMotionObject {mat4 strandPreviousModel;vec4 strandMotionObject;};
struct SegmentPoint {vec4 positionArc,previousPositionArc,colorCoordinates;};
layout(std430,set=2,binding=0) readonly buffer StrandSegmentPoints {SegmentPoint segmentPoints[];};
layout(std430,set=2,binding=1) readonly buffer StrandSegments {uvec4 segmentRecords[];};
layout(rgba32f,set=3,binding=0) uniform writeonly image2D strandSegmentOutput;
layout(push_constant) uniform StrandSegmentDispatch {uvec4 segmentDispatch;};
vec3 SegmentLocalPosition(uint index,bool oldPosition) {
    return oldPosition?segmentPoints[index].previousPositionArc.xyz:segmentPoints[index].positionArc.xyz;
}
vec3 SegmentTangent(uvec4 record,bool oldPosition) {
    vec3 p=SegmentLocalPosition(record.x,oldPosition),q=SegmentLocalPosition(record.y,oldPosition);
    vec3 previous=(record.w&1u)!=0u?2.0*p-q:SegmentLocalPosition(record.z,oldPosition);
    vec3 a=p-previous,b=q-p;
    // calcShading %142..226: the s=0 centripetal Catmull-Rom derivative.
    // Algebraically canceling its zero-position terms leaves these weights.
    // Degenerate control points are guarded host inputs, not source behavior.
    float da=sqrt(length(a)),db=sqrt(length(b));
    if(min(da,db)<1e-7)return StrandSafe(b,vec3(0,-1,0));
    return StrandSafe((a*(db/da)+b*(da/db))/(da+db),StrandSafe(b,vec3(0,-1,0)));
}
vec3 SegmentPackedColor(vec3 value) {
    // Exact source layout %8797..8807: half conversion followed by dropping
    // 4 mantissa bits for R/G and 5 for B (unsigned 11/11/10 float fields).
    uvec3 h=uvec3(packHalf2x16(vec2(value.r,0)),packHalf2x16(vec2(value.g,0)),packHalf2x16(vec2(value.b,0)));
    h&=uvec3(0x7ff0u,0x7ff0u,0x7fe0u);
    return vec3(unpackHalf2x16(h.r).x,unpackHalf2x16(h.g).x,unpackHalf2x16(h.b).x);
}
void main() {
    uint segment=gl_GlobalInvocationID.x;if(segment>=segmentDispatch.x)return;
    uvec4 record=segmentRecords[segment];
    // Source Emissive %99/%674..679: fixed phase from the original END
    // vertex index, not a frame index or the current compact-list ordinal.
    strandSourceShadowAngle=DirectionalSourceShadowAngle(record.y);
    vec3 currentViewOrigin=(strandModel*vec4(SegmentLocalPosition(record.x,false),1)).xyz;
    vec3 current=(strandModel*vec4(SegmentLocalPosition(record.y,false),1)).xyz;
    vec3 currentT=StrandSafe(mat3(strandModel)*SegmentTangent(record,false),vec3(0,-1,0));
    strandWorldPosition=current;strandViewOrigin=currentViewOrigin;
    strandWorldTangent=currentT;strandShutterCamera=strandCamera.xyz;
    mat4 rasterProjection=strandViewProjection;
    if(strandMotionState.z>0.0&&strandMotionState.x>.5&&strandMotionObject.x>.5) {
        float time=1.0-clamp(strandMotionState.z,0.0,1.0);
        vec3 oldViewOrigin=(strandPreviousModel*vec4(SegmentLocalPosition(record.x,true),1)).xyz;
        vec3 old=(strandPreviousModel*vec4(SegmentLocalPosition(record.y,true),1)).xyz;
        vec3 oldT=StrandSafe(mat3(strandPreviousModel)*SegmentTangent(record,true),currentT);
        strandWorldPosition=mix(old,current,time);
        strandViewOrigin=mix(oldViewOrigin,currentViewOrigin,time);
        strandWorldTangent=StrandSafe(mix(oldT,currentT,time),currentT);
        strandShutterCamera=mix(strandPreviousCamera.xyz,strandCamera.xyz,time);
        rasterProjection=strandPreviousUnjitteredViewProjection*(1.0-time)+strandCurrentUnjitteredViewProjection*time;
        vec2 jitterNdc=vec2(strandPreviousCamera.w,strandMotionState.w);
        for(int column=0;column<4;++column)rasterProjection[column].xy+=jitterNdc*rasterProjection[column].w;
    }
    // Emissive: V and the s=0 T use START. Radiance, range/cone, probe and
    // DSVolume queries use END (%293/297/301), as do both authored bytes.
    strandArc=clamp(segmentPoints[record.y].positionArc.w,0.0,1.0);
    strandColorCoordinates=segmentPoints[record.y].colorCoordinates.xy;
    // CSM is a host adapter. A compute invocation has no fragment derivatives;
    // use a camera-facing plane's one-pixel world footprint explicitly.
    vec4 clip=rasterProjection*vec4(strandWorldPosition,1);
    mat4 invProjection=inverse(rasterProjection);
    vec4 wx=invProjection*(clip+vec4(2.0*clip.w/strandViewport.x,0,0,0));
    vec4 wy=invProjection*(clip+vec4(0,2.0*clip.w/strandViewport.y,0,0));
    vec3 shadowDx=wx.xyz/max(abs(wx.w),1e-7)-strandWorldPosition;
    vec3 shadowDy=wy.xyz/max(abs(wy.w),1e-7)-strandWorldPosition;
)GLSL";
    source+=fragment.substr(begin,end-begin);
    source+=R"GLSL(
    vec4 packedColorValue=vec4(SegmentPackedColor(color),1.0);
    uint width=segmentDispatch.y;
    imageStore(strandSegmentOutput,ivec2(record.y%width,record.y/width),packedColorValue);
    if((record.w&2u)!=0u)imageStore(strandSegmentOutput,ivec2(record.x%width,record.x/width),packedColorValue);
}
)GLSL";
    return source;
}
// OpenGL's current RHI has no in-frame storage dispatch. A fullscreen RHI draw
// evaluates the identical source record for each endpoint, then the SAME
// raster consumer reads its atlas. Only root evaluations are duplicated.
inline std::string StrandSegmentFragmentShader(bool diagnostics,bool pointPaths) {
    auto source=StrandSegmentComputeShader(diagnostics,pointPaths);
    const auto replace=[&](const std::string& old,const std::string& value){
        const auto at=source.find(old);if(at==std::string::npos)throw std::runtime_error("Segment raster producer contract changed");
        source.replace(at,old.size(),value);
    };
    replace("layout(local_size_x=64) in;","layout(location=0) out vec4 segmentColorOutput;");
    replace("layout(std430,set=2,binding=0) readonly buffer StrandSegmentPoints {SegmentPoint segmentPoints[];};",R"GLSL(
layout(binding=2) uniform sampler2D segmentPointInput;
layout(binding=3) uniform sampler2D segmentMappingInput;
SegmentPoint SegmentRead(uint index) {
    ivec2 size=textureSize(segmentMappingInput,0),p=ivec2(index%uint(size.x),index/uint(size.x));
    return SegmentPoint(texelFetch(segmentPointInput,p,0),texelFetch(segmentPointInput,p+ivec2(0,size.y),0),
        texelFetch(segmentPointInput,p+ivec2(0,2*size.y),0));
}
)GLSL");
    replace("layout(std430,set=2,binding=1) readonly buffer StrandSegments {uvec4 segmentRecords[];};","");
    replace("layout(rgba32f,set=3,binding=0) uniform writeonly image2D strandSegmentOutput;","");
    replace("layout(push_constant) uniform StrandSegmentDispatch {uvec4 segmentDispatch;};","");
    replace("segmentPoints[index]","SegmentRead(index)");
    // The local-position ternary refers to the point twice.
    replace("segmentPoints[index]","SegmentRead(index)");
    replace("segmentPoints[record.y]","SegmentRead(record.y)");
    replace("segmentPoints[record.y]","SegmentRead(record.y)");
    replace("    uint segment=gl_GlobalInvocationID.x;if(segment>=segmentDispatch.x)return;\n    uvec4 record=segmentRecords[segment];",R"GLSL(
    vec4 mapping=texelFetch(segmentMappingInput,ivec2(gl_FragCoord.xy),0);
    if(mapping.x<0.0){segmentColorOutput=vec4(0.0);return;}
    uvec4 record=uvec4(mapping);
)GLSL");
    const auto store=source.find("    uint width=segmentDispatch.y;");
    if(store==std::string::npos)throw std::runtime_error("Segment raster producer store contract changed");
    source.replace(store,source.size()-store,"    segmentColorOutput=packedColorValue;\n}\n");
    return source;
}
} // namespace Render::PipelineDetail
