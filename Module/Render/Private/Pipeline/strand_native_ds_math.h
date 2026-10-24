#pragma once
#include <string>

namespace Render::PipelineDetail {
// Native Texture DSVolume arithmetic only. No buffers, textures, dispatch or
// sampler declarations. Host atlas layout and robust invalid-input guards are
// explicit caller policies, not recovered native resource configuration.
inline std::string StrandNativeDsMathGLSL(){return R"GLSL(
const uint StrandNativeDsLowMask=0x00ffffffu;
float StrandNativeDsAdd(float a,float b){precise float q=a+b;return q;}
float StrandNativeDsMul(float a,float b){precise float q=a*b;return q;}
float StrandNativeDsDiv(float a,float b){precise float q=a/b;return q;}
float StrandNativeDsMinVoxel(vec3 voxel){return min(min(voxel.x,voxel.y),voxel.z);}
float StrandNativeDsNormalizedWidth(uint packedWidth,float averageFullWidth){
    return max(StrandNativeDsDiv(StrandNativeDsMul(float(packedWidth&65535u),0.00001525902189314365),averageFullWidth),0.0);
}
// Source rasterDSVolumeIntpij %131,%132,%225..228,%247. Lengths here are cm.
// Interpolate already-scaled endpoint widths, then divide by MIN voxel size.
float StrandNativeDsDoseTotal(float rasterWidthScale,float w0,float w1,float progress,float h){
    float scale=StrandNativeDsMul(rasterWidthScale,0.004999999888241291);
    float start=StrandNativeDsMul(scale,w0),delta=StrandNativeDsMul(scale,StrandNativeDsAdd(w1,-w0));
    return StrandNativeDsMul(StrandNativeDsDiv(StrandNativeDsAdd(StrandNativeDsMul(progress,delta),start),h),1000.0);
}
uint StrandNativeDsDose(float total){return uint(roundEven(total))&StrandNativeDsLowMask;}
void StrandNativeDsIntpij(float total,vec3 fraction,out uint doses[8]){
    float ix=StrandNativeDsAdd(1.0,-fraction.x),iy=StrandNativeDsAdd(1.0,-fraction.y),iz=StrandNativeDsAdd(1.0,-fraction.z);
    float a=StrandNativeDsMul(total,ix),b=StrandNativeDsMul(total,fraction.x);
    float c=StrandNativeDsMul(a,iy),d=StrandNativeDsMul(b,iy),e=StrandNativeDsMul(a,fraction.y),f=StrandNativeDsMul(b,fraction.y);
    doses[0]=StrandNativeDsDose(StrandNativeDsMul(c,iz));doses[1]=StrandNativeDsDose(StrandNativeDsMul(d,iz));
    doses[2]=StrandNativeDsDose(StrandNativeDsMul(e,iz));doses[3]=StrandNativeDsDose(StrandNativeDsMul(f,iz));
    doses[4]=StrandNativeDsDose(StrandNativeDsMul(c,fraction.z));doses[5]=StrandNativeDsDose(StrandNativeDsMul(d,fraction.z));
    doses[6]=StrandNativeDsDose(StrandNativeDsMul(e,fraction.z));doses[7]=StrandNativeDsDose(StrandNativeDsMul(f,fraction.z));
}
vec2 StrandNativeDsCopy(uint raw){return vec2(StrandNativeDsMul(float(raw&StrandNativeDsLowMask),0.0010000000474974513),
    clamp(StrandNativeDsMul(float(raw>>24u),0.003921568859368563),0.0,1.0));}
vec2 StrandNativeDsMip(in vec2 children[8]){
    vec2 q=children[0];for(int i=1;i<8;++i)q=vec2(StrandNativeDsAdd(q.x,children[i].x),StrandNativeDsAdd(q.y,children[i].y));
    return vec2(StrandNativeDsMul(q.x,.125),StrandNativeDsMul(q.y,.125));
}
uvec3 StrandNativeDsResolution(vec3 extentCm,float requestedVoxelCm,uint resourceR,out vec3 voxelCm,out vec3 inverseExtent){
    uvec3 n=uvec3(roundEven(abs(extentCm)/requestedVoxelCm));
    for(int i=0;i<3;++i){voxelCm[i]=n[i]<resourceR?requestedVoxelCm:StrandNativeDsDiv(abs(extentCm[i]),float(resourceR));
        inverseExtent[i]=StrandNativeDsDiv(1.0,extentCm[i]);}
    return min(n,uvec3(resourceR));
}
vec3 StrandNativeDsCoordinate(vec3 position,vec3 minimum,vec3 inverseExtent,uvec3 actual){
    return vec3(StrandNativeDsMul(clamp(StrandNativeDsMul(StrandNativeDsAdd(position.x,-minimum.x),inverseExtent.x),0.0,1.0),float(actual.x-1u)),
        StrandNativeDsMul(clamp(StrandNativeDsMul(StrandNativeDsAdd(position.y,-minimum.y),inverseExtent.y),0.0,1.0),float(actual.y-1u)),
        StrandNativeDsMul(clamp(StrandNativeDsMul(StrandNativeDsAdd(position.z,-minimum.z),inverseExtent.z),0.0,1.0),float(actual.z-1u)));
}
// Source %6829..6834 DIVIDES first, then multiplies; preserve its f32 grouping.
vec3 StrandNativeDsTextureUV(vec3 normalized,uvec3 actual,uint resourceR){
    float denominator=float(resourceR-1u);return vec3(StrandNativeDsMul(StrandNativeDsDiv(normalized.x,denominator),float(actual.x-1u)),
        StrandNativeDsMul(StrandNativeDsDiv(normalized.y,denominator),float(actual.y-1u)),
        StrandNativeDsMul(StrandNativeDsDiv(normalized.z,denominator),float(actual.z-1u)));
}
float StrandNativeDsTraceWeight(float factor,float baseDs,float voxelX){return StrandNativeDsDiv(StrandNativeDsMul(factor,baseDs),voxelX);}
// Negative rounded LOD -> float-to-uint is not specified by source evidence.
// Return false before that cast. A caller may explicitly choose a host clamp0.
bool StrandNativeDsTryLod(float factor,float baseDs,float voxelX,out float weight,out uint lod,out float rounded){
    weight=StrandNativeDsTraceWeight(factor,baseDs,voxelX);rounded=min(roundEven(log2(weight)),5.0);
    bool valid=weight>0.0&&!isnan(rounded)&&!isinf(rounded)&&rounded>=0.0;lod=valid?uint(rounded):0u;return valid;
}
float StrandNativeDsNextFactor(float factor,float tracingDelta,uint traceLimit){return min(StrandNativeDsMul(tracingDelta,factor),float(traceLimit));}
// All4 unrolled attempts finish before the caller tests G>=1 or cursor>=count.
// weights use OLD factor; cursor advances NEW factor only after a valid new cell.
void StrandNativeDsAccumulate4(in vec2 reads[4],vec4 weights,inout vec2 ng){
    for(int i=0;i<4;++i){ng.x=StrandNativeDsAdd(StrandNativeDsMul(reads[i].x,weights[i]),ng.x);ng.y=max(ng.y,reads[i].y);}
}
)GLSL";}
} // namespace Render::PipelineDetail
