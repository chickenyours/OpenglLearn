#pragma once
#include "Render/Private/Pipeline/probe_sdf_shaders.h"
#include <string>

namespace Render::ProbeSdfVisualizationDetail {
// This view ray-marches the distance textures themselves. No scene triangles,
// analytic shapes, screen depths, or BVH refinement generate its surfaces.
inline std::string VolumeShader(unsigned viewMode=0) {
    return std::string("#version 450 core\n#define PV_RENDER_MODE ")+std::to_string(viewMode)+"\n"+ScreenProbeTraceDetail::ConstantsGLSL()+
        ProbeSdfDetail::ConstantsGLSL()+ProbeSdfDetail::SamplingGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D pvScene;

bool PVBox(vec3 p,vec3 d,vec3 low,vec3 high,inout float near,inout float far) {
    for(int axis=0;axis<3;++axis) {
        if(abs(d[axis])<1e-9) {if(p[axis]<low[axis]||p[axis]>high[axis])return false;continue;}
        float a=(low[axis]-p[axis])/d[axis],b=(high[axis]-p[axis])/d[axis];
        near=max(near,min(a,b));far=min(far,max(a,b));if(near>far)return false;
    }
    return true;
}
float PVLocalSample(vec3 local,vec3 origin,vec3 spacing,ivec3 counts,int offset) {
    vec3 grid=clamp((local-origin)/spacing,vec3(0),vec3(counts-1));
    ivec3 cell=ivec3(floor(grid));vec3 f=fract(grid);float distance=0;
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        vec3 weight=mix(1.0-f,f,vec3(x,y,z));
        distance+=PSMeshTexel(min(cell+ivec3(x,y,z),counts-1),counts,offset)*weight.x*weight.y*weight.z;
    }
    return distance;
}
float PVPixelFootprint(float distance) {
    return max(.0001,2.0*distance*abs(spInverseProjection[1][1])/float(spScreen.y));
}
float PVEpsilon(float distance) { return max(psSlice.z,PVPixelFootprint(distance)*.15); }
vec3 PVLocalGradient(vec3 p,vec3 origin,vec3 spacing,ivec3 counts,int offset) {
    vec3 gradient;
    for(int axis=0;axis<3;++axis) {
        vec3 delta=vec3(0);delta[axis]=spacing[axis]*.5;
        gradient[axis]=(PVLocalSample(p+delta,origin,spacing,counts,offset)-
            PVLocalSample(p-delta,origin,spacing,counts,offset))/max(spacing[axis],1e-8);
    }
    return gradient;
}
vec3 PVUnsignedGradient(vec3 p,vec3 origin,vec3 spacing,ivec3 counts,int offset) {
    float center=PVLocalSample(p,origin,spacing,counts,offset);vec3 gradient;
    for(int axis=0;axis<3;++axis) {
        vec3 delta=vec3(0);delta[axis]=spacing[axis]*.5;
        float forward=(PVLocalSample(p+delta,origin,spacing,counts,offset)-center)/max(delta[axis],1e-8);
        float backward=(center-PVLocalSample(p-delta,origin,spacing,counts,offset))/max(delta[axis],1e-8);
        // An unsigned sheet has a V-shaped cusp at zero. Central differences
        // cancel across that cusp and incorrectly report a vanishing slope;
        // use the one-sided slope that stays on a monotone branch instead.
        gradient[axis]=abs(forward)>=abs(backward)?forward:backward;
    }
    return gradient;
}
bool PVTraceObject(int object,vec3 p,vec3 d,float start,inout float best,
    out vec3 normal,out vec3 grid,out vec3 localNormal,out float gridCellWorld) {
    normal=vec3(0);grid=vec3(0);localNormal=vec3(0);gridCellWorld=1;
    mat4 inverseTransform=mat4(PSObject(object,0),PSObject(object,1),PSObject(object,2),PSObject(object,3));
    vec4 origin=PSObject(object,4),spacingClosed=PSObject(object,5),countsScale=PSObject(object,6);
    float surfaceBias=PSObject(object,10).w;
    ivec3 counts=max(ivec3(countsScale.xyz),ivec3(1));
    vec3 localOrigin=(inverseTransform*vec4(p,1)).xyz,localDirection=mat3(inverseTransform)*d;
    float near=start,far=best;
    if(!PVBox(localOrigin,localDirection,origin.xyz,origin.xyz+spacingClosed.xyz*vec3(counts-1),near,far))return false;
    float at=near,previousTime=at,previousValue=0;bool hasPrevious=false,found=false;
    for(int step=0;step<192;++step) {
        if(at>far+.000001)break;
        // Imported thin shells may opt into an explicit coverage iso level.
        // Default assets have zero bias. The raw texture is never replaced or
        // changed: this is the sampled surface rawDistance == surfaceBias.
        float value=PVLocalSample(localOrigin+localDirection*at,origin.xyz,spacingClosed.xyz,counts,int(origin.w))-surfaceBias;
        float worldValue=value*countsScale.w,epsilon=PVEpsilon(at);
        float surfaceTolerance=spacingClosed.w>.5?min(epsilon,.0001):epsilon;
        // Signed zero crossings are refined in the field, including a camera
        // that starts inside an asset. Unsigned open meshes require a genuine
        // near-zero field sample; an AABB entry alone never creates a surface.
        if(hasPrevious&&value*previousValue<0) {
            float low=previousTime,high=at,lowValue=previousValue;
            for(int refine=0;refine<10;++refine) {
                float mid=(low+high)*.5;
                float sampled=PVLocalSample(localOrigin+localDirection*mid,origin.xyz,spacingClosed.xyz,counts,int(origin.w))-surfaceBias;
                if(sampled*lowValue>0){low=mid;lowValue=sampled;}else high=mid;
            }
            at=(low+high)*.5;found=true;break;
        }
        if(abs(worldValue)<=epsilon) {
            vec3 localPoint=localOrigin+localDirection*at;
            vec3 gradient=spacingClosed.w<.5?PVUnsignedGradient(localPoint,origin.xyz,spacingClosed.xyz,counts,int(origin.w)):
                PVLocalGradient(localPoint,origin.xyz,spacingClosed.xyz,counts,int(origin.w));
            float worldGradient=length(transpose(mat3(inverseTransform))*gradient);
            // min-axis scale is a tracing lower bound, not the thickness of
            // the displayed surface. Using it for a hit tolerance would swell
            // the tall axis of a thin, nonuniformly scaled post. Measure this
            // iso band along the field's transformed world gradient instead.
            // Closed volumes have a signed zero surface. Do not terminate at
            // the broader display band: its outward bias can outweigh the
            // interpolation error and make a finer asset less accurate. Reach
            // a precise zero or cross it, then use the bisection above.
            if(abs(value)<1e-7||(worldGradient>1e-8&&abs(value)/worldGradient<=surfaceTolerance)) {
                found=true;break;
            }
        }
        previousTime=at;previousValue=value;hasPrevious=true;
        // Linear interpolation of an exact sampled distance can have a larger
        // gradient than the original SDF. This factor bounds marching through
        // its interpolated zero surface; the small terminal tolerance is the
        // displayed iso band, not triangle or screen-space geometry.
        at+=max(abs(value)*.4/max(length(localDirection),1e-8),epsilon*.2);
    }
    if(!found||at>=best)return false;
    best=at;vec3 localPoint=localOrigin+localDirection*at;
    localNormal=spacingClosed.w<.5?PVUnsignedGradient(localPoint,origin.xyz,spacingClosed.xyz,counts,int(origin.w)):
        PVLocalGradient(localPoint,origin.xyz,spacingClosed.xyz,counts,int(origin.w));
    if(dot(localNormal,localNormal)<1e-12)localNormal=-localDirection;
    normal=transpose(mat3(inverseTransform))*localNormal;
    if(dot(normal,normal)<1e-12)normal=-d;normal=normalize(normal);
    if(spacingClosed.w<.5&&dot(normal,-d)<0)normal=-normal;
    grid=(localPoint-origin.xyz)/spacingClosed.xyz;
    gridCellWorld=max(spacingClosed.x,max(spacingClosed.y,spacingClosed.z))*PSObject(object,8).w;
    return true;
}
bool PVTraceMeshes(vec3 p,vec3 d,float start,inout float distance,
    out vec3 normal,out vec3 grid,out vec3 gridNormal,out float cellWorld,out int owner) {
    normal=vec3(0);grid=vec3(0);gridNormal=vec3(0);cellWorld=1;owner=-1;
    int selected=clamp(psDisplay.y,0,max(psAtlas.z-1,0));
    for(int object=0;object<psAtlas.z;++object) {
        if(psFlags.w!=0&&object!=selected)continue;
        vec3 objectNormal,objectGrid,localNormal;float objectCellWorld;
        if(PVTraceObject(object,p,d,start,distance,objectNormal,objectGrid,localNormal,objectCellWorld)) {
            owner=object;normal=objectNormal;grid=objectGrid;gridNormal=localNormal;cellWorld=objectCellWorld;
        }
    }
    return owner>=0;
}
bool PVGlobalUnsigned(vec3 point,int level) {
    ivec3 cell=clamp(ivec3(round((point-PSGlobalLevelOrigin(level))/PSGlobalLevelSpacing(level))),ivec3(0),PSGlobalLevelCounts(level)-1);
    int owner=int(PSGlobalTexelLevel(level,cell).y+.5)-1;
    return owner>=0&&owner<psAtlas.z&&PSObject(owner,5).w<.5;
}
vec3 PVGlobalGradient(vec3 point,bool oneSided,int level) {
    float center=oneSided?PSGlobalSampleLevel(level,point):0;vec3 gradient,spacing=PSGlobalLevelSpacing(level);
    for(int axis=0;axis<3;++axis) {
        vec3 delta=vec3(0);delta[axis]=spacing[axis]*.5;
        float forward=PSGlobalSampleLevel(level,point+delta),backward=PSGlobalSampleLevel(level,point-delta);
        if(oneSided) {
            float a=(forward-center)/max(delta[axis],1e-8),b=(center-backward)/max(delta[axis],1e-8);
            gradient[axis]=abs(a)>=abs(b)?a:b;
        } else gradient[axis]=(forward-backward)/max(spacing[axis],1e-8);
    }
    return gradient;
}
bool PVGlobalAtZero(vec3 point,float value,int level) {
    float gradient=length(PVGlobalGradient(point,PVGlobalUnsigned(point,level),level));
    return value==0.0||(gradient>1e-8&&abs(value)/gradient<=.0001);
}
float PVGlobalCellAdvance(vec3 point,vec3 direction,float value,int level) {
    vec3 origin=PSGlobalLevelOrigin(level),spacing=PSGlobalLevelSpacing(level);
    vec3 at=(point-origin)/spacing;
    ivec3 cell=clamp(ivec3(floor(at)),ivec3(0),PSGlobalLevelCounts(level)-2);
    float values[8];
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x)
        values[(z*2+y)*2+x]=PSGlobalTexelLevel(level,cell+ivec3(x,y,z)).x;
    vec3 slope=vec3(0);
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)
        slope.x=max(slope.x,abs(values[(z*2+y)*2+1]-values[(z*2+y)*2]));
    for(int z=0;z<2;++z)for(int x=0;x<2;++x)
        slope.y=max(slope.y,abs(values[(z*2+1)*2+x]-values[z*4+x]));
    for(int y=0;y<2;++y)for(int x=0;x<2;++x)
        slope.z=max(slope.z,abs(values[4+y*2+x]-values[y*2+x]));
    float derivative=dot(slope/spacing,abs(direction));
    float safe=derivative>1e-9?abs(value)*.8/derivative:1e20;
    float exit=1e20;
    for(int axis=0;axis<3;++axis)if(abs(direction[axis])>1e-9) {
        float edge=origin[axis]+float(cell[axis]+(direction[axis]>0?1:0))*spacing[axis];
        exit=min(exit,max(0.0,(edge-point[axis])/direction[axis]));
    }
    // The directional derivative bound is exact for this trilinear cell. It
    // permits faster near-zero progress when a nonuniform mesh's min-axis
    // distance is much smaller than the distance along the camera ray.
    return min(safe,exit+.000001);
}
void PVGlobalMinimum(vec3 p,vec3 d,float low,float high,int level,out float time,out float value) {
    // A minimum step can straddle the zero cusp of an unsigned sheet. Search
    // the sampled field in this small bracket; a positive unresolved minimum
    // remains a miss, including an undersampled thin feature with no zero.
    const float golden=.38196601125;
    float a=low+(high-low)*golden,b=high-(high-low)*golden;
    float av=PSGlobalSampleLevel(level,p+d*a),bv=PSGlobalSampleLevel(level,p+d*b);
    for(int iteration=0;iteration<16;++iteration) {
        if(av<bv){high=b;b=a;bv=av;a=low+(high-low)*golden;av=PSGlobalSampleLevel(level,p+d*a);}
        else{low=a;a=b;av=bv;b=high-(high-low)*golden;bv=PSGlobalSampleLevel(level,p+d*b);}
    }
    time=av<bv?a:b;value=min(av,bv);
}
bool PVTraceGlobalLegacy(vec3 p,vec3 d,float start,inout float distance,
    out vec3 normal,out vec3 grid,out float cellWorld,out int owner,out int hitLevel,out bool unresolved) {
    normal=vec3(0);grid=vec3(0);owner=-1;hitLevel=-1;cellWorld=1;
    unresolved=false;
    float near=start,far=distance;
    int outer=PSGlobalOuterLevel(true);if(outer<0)return false;
    vec3 low,high;PSGlobalLevelBounds(outer,low,high);
    if(!PVBox(p,d,low,high,near,far))return false;
    float at=near,previousTime=at,olderTime=at,previousValue=0;bool hasPrevious=false,hasOlder=false,found=false;
    int previousLevel=-1,level=0;
    for(int step=0;step<512;++step) {
        if(psClipmap.x==0&&step>=256)break;
        if(at>far+.000001)break;
        vec3 point=p+d*at;level=PSGlobalViewLevel(point);
        float transition=PSGlobalTransition(point,d,level,true);
        if(level<0) {
            if(transition>1e19)break;
            at+=transition+PSGlobalNudge(0);hasPrevious=false;hasOlder=false;previousLevel=-1;continue;
        }
        if(level!=previousLevel){hasPrevious=false;hasOlder=false;}
        if(transition<=PSGlobalNudge(level)) {
            at+=transition+PSGlobalNudge(level);hasPrevious=false;hasOlder=false;previousLevel=-1;continue;
        }
        float value=PSGlobalSampleLevel(level,point),epsilon=PVEpsilon(at);
        // Never interpolate a sign change across different clipmaps. Their
        // actual fields and iso expansion can differ; a handoff alone is not
        // a surface. All root searches and gradients stay in one level.
        if(hasPrevious&&value*previousValue<0) {
            float a=previousTime,b=at,lowValue=previousValue;
            for(int refine=0;refine<10;++refine) {
                float mid=(a+b)*.5,sampled=PSGlobalSampleLevel(level,p+d*mid);
                if(sampled*lowValue>0){a=mid;lowValue=sampled;}else b=mid;
            }
            at=(a+b)*.5;found=true;break;
        }
        if(abs(value)<=epsilon&&PVGlobalAtZero(point,value,level)){found=true;break;}
        if(hasOlder&&previousValue>=0&&value>previousValue&&previousValue<=max(epsilon,.006)*2.0) {
            float minimumTime,minimumValue;PVGlobalMinimum(p,d,olderTime,at,level,minimumTime,minimumValue);
            if(minimumValue<0) {
                float a=olderTime,b=minimumTime,lowValue=PSGlobalSampleLevel(level,p+d*a);
                for(int refine=0;refine<12;++refine) {
                    float mid=(a+b)*.5,sampled=PSGlobalSampleLevel(level,p+d*mid);
                    if(sampled*lowValue>0){a=mid;lowValue=sampled;}else b=mid;
                }
                at=(a+b)*.5;found=true;break;
            }
            if(PVGlobalAtZero(p+d*minimumTime,minimumValue,level)){at=minimumTime;found=true;break;}
            unresolved=true;
        }
        if(hasPrevious){olderTime=previousTime;hasOlder=true;}
        previousTime=at;previousValue=value;hasPrevious=true;previousLevel=level;
        float advance=abs(value)*.4;
        if(abs(value)<.1)advance=PVGlobalCellAdvance(point,d,value,level);
        at+=min(max(advance,epsilon*.2),transition);
    }
    if(!found||at>=distance)return false;
    distance=at;vec3 point=p+d*at;hitLevel=level;
    bool unsignedSurface=PVGlobalUnsigned(point,level);normal=PVGlobalGradient(point,unsignedSurface,level);
    if(dot(normal,normal)<1e-12)normal=-d;normal=normalize(normal);
    if(unsignedSurface&&dot(normal,-d)<0)normal=-normal;
    vec3 spacing=PSGlobalLevelSpacing(level);cellWorld=max(spacing.x,max(spacing.y,spacing.z));
    grid=(point-PSGlobalLevelOrigin(level))/spacing;
    ivec3 cell=clamp(ivec3(round(grid)),ivec3(0),PSGlobalLevelCounts(level)-1);
    owner=int(PSGlobalTexelLevel(level,cell).y+.5)-1;return true;
}
// A trilinear cell is a cubic along a ray. Fetch its eight corners once;
// solve its monotone polynomial intervals and differentiate the same field.
// No triangle geometry, lower-detail substitute, or pixel decimation is used.
struct PVCellData { vec4 a;vec4 b;float minimum;float maximum; };
PVCellData PVGlobalCell(int level,ivec3 cell) {
    ivec3 counts=PSGlobalLevelCounts(level),physical=cell+psClipScroll[level].xyz;
    physical-=ivec3(greaterThanEqual(physical,counts))*counts;
    ivec3 next=physical+1;next-=ivec3(greaterThanEqual(next,counts))*counts;
    float v[8];
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        ivec3 q=ivec3(x==0?physical.x:next.x,y==0?physical.y:next.y,z==0?physical.z:next.z);
        int index=psClipCounts[level].w+(q.z*counts.y+q.y)*counts.x+q.x;
        ivec2 address=psGlobalCounts.w==1024?ivec2(index&1023,index>>10):ivec2(index%psGlobalCounts.w,index/psGlobalCounts.w);
        v[(z*2+y)*2+x]=texelFetch(psGlobal,address,0).x;
    }
    PVCellData result;
    result.a=vec4(v[0],v[1]-v[0],v[2]-v[0],v[4]-v[0]);
    result.b=vec4(v[3]-v[1]-v[2]+v[0],v[5]-v[1]-v[4]+v[0],
        v[6]-v[2]-v[4]+v[0],v[7]-v[3]-v[5]-v[6]+v[1]+v[2]+v[4]-v[0]);
    result.minimum=min(min(min(v[0],v[1]),min(v[2],v[3])),min(min(v[4],v[5]),min(v[6],v[7])));
    result.maximum=max(max(max(v[0],v[1]),max(v[2],v[3])),max(max(v[4],v[5]),max(v[6],v[7])));
    return result;
}
float PVCellValue(PVCellData cell,vec3 f) {
    return dot(cell.a,vec4(1,f))+dot(cell.b,vec4(f.x*f.y,f.x*f.z,f.y*f.z,f.x*f.y*f.z));
}
vec3 PVCellGradient(PVCellData cell,vec3 f,vec3 spacing) {
    return vec3(cell.a.y+cell.b.x*f.y+cell.b.y*f.z+cell.b.w*f.y*f.z,
        cell.a.z+cell.b.x*f.x+cell.b.z*f.z+cell.b.w*f.x*f.z,
        cell.a.w+cell.b.y*f.x+cell.b.z*f.y+cell.b.w*f.x*f.y)/spacing;
}
vec4 PVCellPolynomial(PVCellData cell,vec3 f,vec3 d) {
    float a=cell.b.w*d.x*d.y*d.z;
    float b=cell.b.x*d.x*d.y+cell.b.y*d.x*d.z+cell.b.z*d.y*d.z+
        cell.b.w*(f.x*d.y*d.z+f.y*d.x*d.z+f.z*d.x*d.y);
    float c=dot(cell.a.yzw,d)+cell.b.x*(f.x*d.y+f.y*d.x)+
        cell.b.y*(f.x*d.z+f.z*d.x)+cell.b.z*(f.y*d.z+f.z*d.y)+
        cell.b.w*(f.x*f.y*d.z+f.x*f.z*d.y+f.y*f.z*d.x);
    return vec4(a,b,c,PVCellValue(cell,f));
}
float PVPolynomial(vec4 polynomial,float t) { return ((polynomial.x*t+polynomial.y)*t+polynomial.z)*t+polynomial.w; }
float PVPolynomialDerivative(vec4 polynomial,float t) { return (3.0*polynomial.x*t+2.0*polynomial.y)*t+polynomial.z; }
vec2 PVPolynomialCritical(vec4 polynomial) {
    float a=3.0*polynomial.x,b=2.0*polynomial.y,c=polynomial.z;
    float scale=max(max(abs(a),abs(b)),max(abs(c),1e-20));vec2 roots=vec2(1);
    if(abs(a)<=scale*1e-7) {
        if(abs(b)>scale*1e-7){float r=-c/b;if(r>0&&r<1)roots.x=r;}
        return roots;
    }
    float discriminant=b*b-4.0*a*c;if(discriminant<0)return roots;
    float q=-.5*(b+(b>=0?1.0:-1.0)*sqrt(max(discriminant,0.0)));
    float first=q/a,second=abs(q)>1e-30?c/q:-b/(2.0*a);
    if(first>second){float temporary=first;first=second;second=temporary;}
    if(first>0&&first<1)roots.x=first;
    if(second>0&&second<1){if(roots.x==1)roots.x=second;else roots.y=second;}
    return roots;
}
bool PVCellRoot(PVCellData cell,vec3 fraction,vec3 delta,vec3 spacing,out float root,out vec3 gradient,out bool unresolved) {
    root=0;gradient=vec3(0);unresolved=false;
    // Convexity of trilinear interpolation certifies same-sign cells empty.
    if(cell.minimum>1e-7||cell.maximum< -1e-7)return false;
    vec4 polynomial=PVCellPolynomial(cell,fraction,delta);vec2 critical=PVPolynomialCritical(polynomial);
    float left=0,leftValue=polynomial.w;
    for(int segment=0;segment<3;++segment) {
        float right=segment==0?critical.x:(segment==1?critical.y:1.0);
        if(right<left)continue;
        vec3 leftGradient=PVCellGradient(cell,fraction+delta*left,spacing);
        if(leftValue==0.0||(length(leftGradient)>1e-8&&abs(leftValue)<=length(leftGradient)*.00002)) {
            root=left;gradient=leftGradient;return true;
        }
        float rightValue=PVPolynomial(polynomial,right);
        if(leftValue*rightValue<0) {
            float low=left,high=right,lowValue=leftValue,t=(low+high)*.5;
            // Safeguarded Newton converges rapidly on ordinary cells. Keep the
            // sign bracket for grazing, repeated, and nearly constant roots.
            for(int refine=0;refine<18;++refine) {
                float value=PVPolynomial(polynomial,t);
                if(value*lowValue>0){low=t;lowValue=value;}else high=t;
                vec3 g=PVCellGradient(cell,fraction+delta*t,spacing);
                if(abs(value)<=max(1e-8,length(g)*.000002)){root=t;gradient=g;return true;}
                float derivative=PVPolynomialDerivative(polynomial,t);
                float candidate=abs(derivative)>1e-12?t-value/derivative:(low+high)*.5;
                t=candidate>low&&candidate<high?candidate:(low+high)*.5;
            }
            root=(low+high)*.5;gradient=PVCellGradient(cell,fraction+delta*root,spacing);return true;
        }
        if(right>left&&rightValue>=0&&rightValue<.012)unresolved=true;
        left=right;leftValue=rightValue;
    }
    gradient=PVCellGradient(cell,fraction+delta,spacing);
    if(leftValue==0.0||(length(gradient)>1e-8&&abs(leftValue)<=length(gradient)*.00002)){root=1;return true;}
    return false;
}
float PVGridExit(vec3 rayOrigin,vec3 rayDirection,ivec3 first,ivec3 last,float limit) {
    for(int axis=0;axis<3;++axis)if(abs(rayDirection[axis])>1e-12) {
        float edge=float(rayDirection[axis]>0?last[axis]:first[axis]);
        limit=min(limit,(edge-rayOrigin[axis])/rayDirection[axis]);
    }
    return limit;
}
vec2 PVGlobalBrickRange(int level,ivec3 logicalCell) {
    ivec3 counts=psClipRanges[level].xyz;
    ivec3 brick=logicalCell/psRangeAtlas.y+psClipScroll[level].xyz/psRangeAtlas.y;
    brick-=ivec3(greaterThanEqual(brick,counts))*counts;
    int index=psClipRanges[level].w+(brick.z*counts.y+brick.y)*counts.x+brick.x;
    ivec2 address=psRangeAtlas.x==1024?ivec2(index&1023,index>>10):ivec2(index%psRangeAtlas.x,index/psRangeAtlas.x);
    return texelFetch(psRanges,address,0).xy;
}
bool PVTraceGlobalClipmap(vec3 p,vec3 d,float start,inout float distance,
    out vec3 normal,out vec3 grid,out float cellWorld,out int owner,out int hitLevel,out bool unresolved) {
    normal=vec3(0);grid=vec3(0);cellWorld=1;owner=-1;hitLevel=-1;unresolved=false;
    vec4 entries=vec4(1e20),exits=vec4(-1);
    // Four ray-box queries per ray, rather than four queries per march step.
    for(int level=0;level<4;++level) {
        if(!PSGlobalLevelReady(level)||(psClipmap.y>=0&&psClipmap.y!=level))continue;
        vec3 low,high;PSGlobalLevelBounds(level,low,high);float near=start,far=distance;
        if(PVBox(p,d,low,high,near,far)){entries[level]=near;exits[level]=far;}
    }
    float at=start;
    for(int section=0;section<9;++section) {
        int level=-1;float nearest=1e20;
        for(int candidate=0;candidate<4;++candidate) {
            if(entries[candidate]<=at+.000001&&exits[candidate]>at+.000001&&level<0)level=candidate;
            if(entries[candidate]>at+.000001)nearest=min(nearest,entries[candidate]);
        }
        if(level<0){if(nearest>=distance)return false;at=nearest;continue;}
        float end=exits[level];
        for(int finer=0;finer<level;++finer)if(entries[finer]>at+.000001)end=min(end,entries[finer]);
        vec3 spacing=PSGlobalLevelSpacing(level),origin=PSGlobalLevelOrigin(level);
        vec3 rayOrigin=(p-origin)/spacing,rayDirection=d/spacing;ivec3 counts=PSGlobalLevelCounts(level);
        // DDA touches each cell at most once. A range brick has the min/max of
        // every shared corner, so a same-sign range can skip the entire brick.
        for(int visit=0;visit<1024&&at<end-.000001;++visit) {
            vec3 coordinate=rayOrigin+rayDirection*at;
            ivec3 cell=clamp(ivec3(floor(coordinate+sign(rayDirection)*.00001)),ivec3(0),counts-2);
            if(psRangeAtlas.z!=0) {
                vec2 range=PVGlobalBrickRange(level,cell);
                if(range.x>1e-7||range.y< -1e-7) {
                    ivec3 first=(cell/psRangeAtlas.y)*psRangeAtlas.y,last=min(first+psRangeAtlas.y,counts-1);
                    float exit=PVGridExit(rayOrigin,rayDirection,first,last,end);
                    at=max(exit,at)+max(.000002,abs(exit)*.0000002);continue;
                }
            }
            float exit=PVGridExit(rayOrigin,rayDirection,cell,cell+1,end);
            if(exit<=at+.0000001){at+=max(.000002,abs(at)*.0000002);continue;}
            PVCellData sampled=PVGlobalCell(level,cell);float root;vec3 gradient;bool candidateUnresolved;
            if(PVCellRoot(sampled,clamp(coordinate-vec3(cell),vec3(0),vec3(1)),rayDirection*(exit-at),spacing,root,gradient,candidateUnresolved)) {
                float hit=at+root*(exit-at);if(hit>=distance)return false;
                distance=hit;hitLevel=level;grid=rayOrigin+rayDirection*hit;cellWorld=max(spacing.x,max(spacing.y,spacing.z));
                normal=dot(gradient,gradient)>1e-12?normalize(gradient):-d;
                ivec3 nearestCell=clamp(ivec3(round(grid)),ivec3(0),counts-1);
                owner=int(PSGlobalTexelLevel(level,nearestCell).y+.5)-1;
                bool unsignedSurface=owner>=0&&owner<psAtlas.z&&PSObject(owner,5).w<.5;
                // Keep the original half-voxel filtered shading normal. The
                // exact cell derivative is discontinuous across cell faces;
                // evaluate this smoother normal only ONCE after the hit,
                // never inside the marching/root-search loop.
                vec3 shadingGradient=PVGlobalGradient(p+d*hit,unsignedSurface,level);
                if(dot(shadingGradient,shadingGradient)>1e-12)normal=normalize(shadingGradient);
                if(unsignedSurface&&dot(normal,-d)<0)normal=-normal;
                return true;
            }
            unresolved=unresolved||candidateUnresolved;
            at=exit+max(.000002,abs(exit)*.0000002);
        }
        at=end+max(.000002,abs(end)*.0000002);
    }
    return false;
}
bool PVTraceGlobal(vec3 p,vec3 d,float start,inout float distance,
    out vec3 normal,out vec3 grid,out float cellWorld,out int owner,out int hitLevel,out bool unresolved) {
    if(psClipmap.x==0)return PVTraceGlobalLegacy(p,d,start,distance,normal,grid,cellWorld,owner,hitLevel,unresolved);
    return PVTraceGlobalClipmap(p,d,start,distance,normal,grid,cellWorld,owner,hitLevel,unresolved);
}
float PVGridShade(vec3 grid,vec3 normal,float cellWorld,float distance) {
    if(psSlice.w<.5)return 1;
    vec3 fractions=min(fract(grid),1.0-fract(grid));normal=abs(normal);
    // Ignore the grid coordinate normal to the surface. A floor exactly on a
    // voxel plane then displays its two tangent axes rather than a solid line.
    float edge;
    if(normal.x>=normal.y&&normal.x>=normal.z)edge=min(fractions.y,fractions.z);
    else if(normal.y>=normal.z)edge=min(fractions.x,fractions.z);
    else edge=min(fractions.x,fractions.y);
    float visibility=1.0-smoothstep(cellWorld*.35,cellWorld*.9,PVPixelFootprint(distance));
    return 1.0-(1.0-smoothstep(.025,.06,edge))*visibility*.5;
}
vec3 PVMeshColor(int owner) {
    float resolution=PSObject(owner,11).w;
    if(resolution<32)return vec3(1,.36,.13);
    if(resolution<52)return vec3(.12,.72,.85);
    return vec3(.24,.4,1);
}
vec3 PVClipmapColor(int level) {
    if(level==0)return vec3(.06,.82,1);
    if(level==1)return vec3(.18,.84,.32);
    if(level==2)return vec3(1,.51,.09);
    return vec3(.69,.23,.94);
}
vec3 PVGlobalColor(int owner,int level) {
    if(psClipmap.x>0&&psClipmap.z!=0)return PVClipmapColor(level);
    float phase=fract(float(max(owner,0))*.381966011);
    return mix(vec3(.13,.75,.35),vec3(.85,.58,.13),phase);
}
vec3 PVClipmapBounds(vec2 uv,vec2 viewPixels,float horizontalCrop,vec3 hitPoint,bool hit,vec3 color) {
    if(psClipmap.x==0||psClipmap.w==0)return color;
    vec2 pixel=uv*viewPixels;
    for(int level=0;level<4;++level) {
        if(!PSGlobalLevelReady(level)||(psClipmap.y>=0&&psClipmap.y!=level))continue;
        vec3 low,high;PSGlobalLevelBounds(level,low,high);float closest=1e20;
        for(int edge=0;edge<12;++edge) {
            vec4 projected=psBoundLines[level*12+edge];
            if(any(greaterThan(abs(projected),vec4(1e10))))continue;
            vec2 p=projected.xy,q=projected.zw;p.x/=horizontalCrop;q.x/=horizontalCrop;
            p=(p*.5+.5)*viewPixels;q=(q*.5+.5)*viewPixels;
            // Most pixels are nowhere near a box edge. Reject them before
            // computing projection distance; endpoints are projected once/CPU.
            if(any(lessThan(pixel,min(p,q)-vec2(1.2)))||any(greaterThan(pixel,max(p,q)+vec2(1.2))))continue;
            vec2 line=q-p;float along=clamp(dot(pixel-p,line)/max(dot(line,line),1e-8),0,1);
            closest=min(closest,length(pixel-(p+line*along)));
        }
        float coverage=1.0-smoothstep(.45,1.15,closest);
        // Also mark where the coverage box crosses a real sampled surface.
        // This remains visible when camera-centered box edges lie offscreen.
        if(hit&&all(greaterThanEqual(hitPoint,low))&&all(lessThanEqual(hitPoint,high))) {
            vec3 boundary=min(hitPoint-low,high-hitPoint);float distance=min(boundary.x,min(boundary.y,boundary.z));
            float width=PVPixelFootprint(length(hitPoint-spInverseView[3].xyz));
            coverage=max(coverage,1.0-smoothstep(width*.35,width*1.1,distance));
        }
        color=mix(color,PVClipmapColor(level)*1.15,coverage*.75);
    }
    return color;
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
#if PV_RENDER_MODE == 0
    int mode=psDisplay.w;
#else
    const int mode=PV_RENDER_MODE;
#endif
    if(mode==0){outColor=texelFetch(pvScene,pixel,0);return;}
    vec2 uv=(vec2(pixel)+.5)/vec2(spScreen.xy);
    bool globalField=mode==2||(mode==3&&uv.x>=.5);
    if(mode==3)uv.x=fract(uv.x*2.0);
    vec2 ndc=uv*2.0-1.0;
    // Each half has half the physical aspect ratio. Apply the same horizontal
    // crop to both camera rays instead of squeezing a full-width sphere image.
    if(mode==3)ndc.x*=.5;
    vec4 farView=spInverseProjection*vec4(ndc,1,1);farView/=farView.w;
    vec4 nearView=spInverseProjection*vec4(ndc,-1,1);nearView/=nearView.w;
    vec3 p=spInverseView[3].xyz,d=normalize(mat3(spInverseView)*farView.xyz);
    float distance=length(farView.xyz),start=length(nearView.xyz);
    vec3 normal,grid,gridNormal;float cellWorld;int owner,hitLevel=-1;
    bool hit,unresolved=false;
    if(globalField){hit=PVTraceGlobal(p,d,start,distance,normal,grid,cellWorld,owner,hitLevel,unresolved);gridNormal=normal;}
    else hit=PVTraceMeshes(p,d,start,distance,normal,grid,gridNormal,cellWorld,owner);
    vec3 color=mix(vec3(.025,.035,.06),vec3(.07,.095,.13),uv.y);
    if(globalField&&!hit&&unresolved)color=vec3(.65,.045,.28);
    if(hit) {
        vec3 base=globalField?PVGlobalColor(owner,hitLevel):PVMeshColor(owner);
        float diffuse=max(dot(normal,normalize(vec3(-.4,.8,.55))),0.0);
        float rim=pow(1.0-abs(dot(normal,-d)),3.0);
        color=base*(.22+.68*diffuse)+vec3(.12)*rim;
        color*=PVGridShade(grid,gridNormal,cellWorld,distance);
        color=mix(color,vec3(.045,.06,.08),1.0-exp(-distance*.006));
    }
    if(globalField)color=PVClipmapBounds(uv,vec2(mode==3?float(spScreen.x)*.5:float(spScreen.x),float(spScreen.y)),mode==3?.5:1.0,p+d*distance,hit,color);
    if(mode==3&&abs(float(pixel.x)+.5-float(spScreen.x)*.5)<1.5)color=vec3(.7,.78,.9);
    // The visualization target retains actual texture-marched world distance
    // for independent shape verification. Zero denotes a field-view miss.
    outColor=vec4(color,hit?distance:0.0);
}
)GLSL";
}
} // namespace Render::ProbeSdfVisualizationDetail
