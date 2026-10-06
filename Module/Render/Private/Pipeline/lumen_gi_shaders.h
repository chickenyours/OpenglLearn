#pragma once
#include "Render/Public/Pipeline/lumen_gi.h"
#include "Render/Public/Pipeline/surface_pass.h"
#include "Render/Private/Pipeline/realtime_gi_shaders.h"
#include "Render/Private/Pipeline/indirect_lighting_shaders.h"
namespace Render::PipelineDetail {
inline std::string LumenDataGLSL() {return R"GLSL(
layout(std140,binding=10) uniform LumenData {
    mat4 lmInverseView,lmPreviousVP;
    vec4 lmTrace,lmSettings;
    ivec4 lmAtlas,lmBudget,lmGather;
    vec4 lmCamera;
    vec4 lmFieldOrigin,lmFieldSpacing;
    ivec4 lmFieldCounts;
    vec4 lmReflection;
};
layout(binding=4) uniform sampler2D lmAttributes;
layout(binding=5) uniform sampler2D lmCache;
layout(binding=8) uniform sampler2D lmDistanceField;
bool LmIntersect(vec3 p,vec3 d,inout float distance,out vec3 normal,out int triangle,out bool front) {
#ifdef RENDER_NATIVE_RAY_QUERY
    return RtIntersect(p,d,distance,normal,triangle,front);
#else
    if(lmFieldCounts.x==0)return RtIntersect(p,d,distance,normal,triangle,front);
    vec3 lo=lmFieldOrigin.xyz,hi=lo+vec3(lmFieldCounts.xyz-1)*lmFieldSpacing.xyz;float near=0,far=distance;
    for(int a=0;a<3;++a){if(abs(d[a])<1e-8){if(p[a]<lo[a]||p[a]>hi[a]){triangle=-1;normal=vec3(0);front=true;return false;}continue;}float x=(lo[a]-p[a])/d[a],y=(hi[a]-p[a])/d[a];near=max(near,min(x,y));far=min(far,max(x,y));}
    if(near>far){triangle=-1;normal=vec3(0);front=true;return false;}
    float at=near;
    for(int step=0;step<64;++step){vec3 q=p+d*at;ivec3 cell=clamp(ivec3(round((q-lo)/lmFieldSpacing.xyz)),ivec3(0),lmFieldCounts.xyz-1);int index=(cell.z*lmFieldCounts.y+cell.y)*lmFieldCounts.x+cell.x;
        float value=abs(texelFetch(lmDistanceField,ivec2(index%lmFieldCounts.w,index/lmFieldCounts.w),0).x);
        float safe=max(0.0,value-length(q-(lo+vec3(cell)*lmFieldSpacing.xyz))-.00001);
        if(safe<lmFieldSpacing.x*.05)break;at+=safe;
        if(at>far){triangle=-1;normal=vec3(0);front=true;return false;}
    }
    // Exact refinement handles sub-voxel walls and corners. Conservative steps
    // cannot cross a triangle; a step-budget exhaustion also refines, never misses.
    float remaining=distance-at;bool hit=RtIntersect(p+d*at,d,remaining,normal,triangle,front);if(hit)distance=at+remaining;return hit;
#endif
}
vec4 LmAttribute(int index) {return texelFetch(lmAttributes,ivec2(index%lmAtlas.x,index/lmAtlas.x),0);}
vec4 LmCacheTexel(sampler2D cache,int index) {return texelFetch(cache,ivec2(index%lmAtlas.x,index/lmAtlas.x),0);}
bool LmActive(int index) {return (index-lmBudget.x+lmAtlas.y)%max(lmAtlas.y,1)<lmBudget.y;}
uint LmHash(uvec3 p) {
    uint h=p.x*0x9e3779b9u^p.y*0x85ebca6bu^p.z*0xc2b2ae35u;
    h=(h^(h>>16))*0x7feb352du;h=(h^(h>>15))*0x846ca68bu;return h^(h>>16);
}
vec3 LmHemispherePhase(int ray,int count,vec3 N,float phase) {
    float u=(float(ray)+.5)/float(count),phi=6.283185307*(float(ray)*.618033989+phase);
    vec3 axis=abs(N.z)<.99?vec3(0,0,1):vec3(0,1,0),T=normalize(cross(axis,N));
    return T*(sqrt(u)*cos(phi))+cross(N,T)*(sqrt(u)*sin(phi))+N*sqrt(1-u);
}
vec3 LmHemisphere(int ray,int count,vec3 N,uint seed) {return LmHemispherePhase(ray,count,N,float(seed&0xffffu)/65536.0);}
vec4 LmIncidentLinearBase(sampler2D cache,sampler2D attributes,int triangle,vec3 p);
vec4 LmLookupValue(sampler2D cache,sampler2D attributes,int triangle,vec3 p) {
    // Folded square texels can represent distant positions along the triangle
    // diagonal. Transport hits must interpolate only canonical local samples;
    // the three-tap hull lookup also keeps the weights nonnegative at edges.
    return max(LmIncidentLinearBase(cache,attributes,triangle,p),vec4(0));
}
vec3 LmLookup(sampler2D cache,int triangle,vec3 p) {return LmLookupValue(cache,lmAttributes,triangle,p).rgb;}
vec2 LmBarycentric(int triangle,vec3 p) {
    int offset=rtGeometry.x+triangle*5;vec3 a=RtGeometry(offset).xyz,ab=RtGeometry(offset+1).xyz-a,ac=RtGeometry(offset+2).xyz-a,q=p-a;
    float aa=dot(ab,ab),bb=dot(ac,ac),cc=dot(ab,ac),den=max(aa*bb-cc*cc,1e-20);
    return vec2((dot(q,ab)*bb-dot(q,ac)*cc)/den,(dot(q,ac)*aa-dot(q,ab)*cc)/den);
}
vec4 LmIncidentLinearBase(sampler2D cache,sampler2D attributes,int triangle,vec3 p) {
    vec4 meta=texelFetch(attributes,ivec2(triangle%lmAtlas.x,triangle/lmAtlas.x),0);int r=int(meta.y),first=int(meta.x);
    if(r<2)return LmCacheTexel(cache,first);
    vec2 at=LmBarycentric(triangle,p)*float(r)-1.0/3.0;
    // Interpolate only inside the canonical sample hull. Picking an arbitrary
    // neighbouring cell for negative-weight extrapolation made the outer
    // third-cell strip discontinuous whenever floor(at) crossed a grid line.
    vec2 q=max(at,vec2(0));float last=float(r-1);
    if(q.x+q.y>last){float t=clamp((at.x-at.y+last)*.5,0,last);q=vec2(t,last-t);}
    ivec2 base=clamp(ivec2(floor(q)),ivec2(0),ivec2(r-2));
    int excess=max(base.x+base.y-(r-2),0),move=min(excess,base.x);base.x-=move;base.y-=excess-move;
    vec2 f=q-vec2(base);ivec2 c0=base,c1=base+ivec2(1,0),c2=base+ivec2(0,1);vec3 weights=vec3(1-f.x-f.y,f.x,f.y);
    if(f.x+f.y>1&&base.x+base.y<r-2) {
        c0=base+ivec2(1,1);c1=base+ivec2(0,1);c2=base+ivec2(1,0);weights=vec3(f.x+f.y-1,1-f.x,1-f.y);
    }
    weights=max(weights,vec3(0));weights/=max(dot(weights,vec3(1)),1e-8);
    vec4 value=LmCacheTexel(cache,first+c0.y*r+c0.x)*weights.x+LmCacheTexel(cache,first+c1.y*r+c1.x)*weights.y+LmCacheTexel(cache,first+c2.y*r+c2.x)*weights.z;
    // The canonical hull is sampled inset from the mesh boundary. Extend by
    // its nearest convex value; an affine prediction outside that hull needs
    // negative weights and can turn an additional bright source into black.
    return vec4(max(value.rgb,vec3(0)),clamp(value.a,0,1));
}
float LmIncidentSpline(float x) {
    x=abs(x);return x<.5?.75-x*x:(x<1.5?.5*(1.5-x)*(1.5-x):0);
}
vec4 LmIncidentBase(sampler2D cache,sampler2D attributes,int triangle,vec3 p) {
    vec4 meta=texelFetch(attributes,ivec2(triangle%lmAtlas.x,triangle/lmAtlas.x),0);int r=int(meta.y),first=int(meta.x);
    if(r<2)return LmCacheTexel(cache,first);
    vec2 at=LmBarycentric(triangle,p)*float(r)-1.0/3.0,q=max(at,vec2(0));float last=float(r-1);
    if(q.x+q.y>last){float t=clamp((at.x-at.y+last)*.5,0,last);q=vec2(t,last-t);}
    ivec2 center=ivec2(floor(q+.5));vec2 moment=vec2(0);vec3 second=vec3(0);float total=0;
    // Build the shape function from geometry alone. Affine MLS reproduces
    // smooth interior transport, but its signed boundary coefficients can
    // undershoot a shadow or bright cone. Project its coefficients into the
    // nonnegative partition of unity before applying any lighting samples.
    // This preserves constants, source superposition and the sample range.
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        ivec2 c=center+ivec2(x,y);if(c.x<0||c.y<0||c.x+c.y>=r)continue;
        vec2 d=vec2(c)-q;float weight=LmIncidentSpline(d.x)*LmIncidentSpline(d.y);if(weight<=0)continue;
        moment+=d*weight;second+=vec3(d.x*d.x,d.x*d.y,d.y*d.y)*weight;total+=weight;
    }
    if(total<=1e-8)return LmIncidentLinearBase(cache,attributes,triangle,p);
    vec2 mean=moment/total,shift=at-q-mean;
    vec3 covariance=second/total-vec3(mean.x*mean.x,mean.x*mean.y,mean.y*mean.y);
    float determinant=covariance.x*covariance.z-covariance.y*covariance.y;
    if(determinant<=1e-10)return LmIncidentLinearBase(cache,attributes,triangle,p);
    vec2 slope=vec2(covariance.z*shift.x-covariance.y*shift.y,
                    covariance.x*shift.y-covariance.y*shift.x)/determinant;
    vec4 sum=vec4(0);float weights=0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        ivec2 c=center+ivec2(x,y);if(c.x<0||c.y<0||c.x+c.y>=r)continue;
        vec2 d=vec2(c)-q;float weight=LmIncidentSpline(d.x)*LmIncidentSpline(d.y)*max(0.0,1+dot(d-mean,slope));
        sum+=LmCacheTexel(cache,first+c.y*r+c.x)*weight;weights+=weight;
    }
    if(weights<=1e-8)return LmIncidentLinearBase(cache,attributes,triangle,p);
    vec4 value=sum/weights;return vec4(max(value.rgb,vec3(0)),clamp(value.a,0,1));
}
vec4 LmAdjacency(sampler2D attributes,int triangle) {
    int index=lmAtlas.w+lmAtlas.y*2+lmAtlas.z+triangle;
    return texelFetch(attributes,ivec2(index%lmAtlas.x,index/lmAtlas.x),0);
}
vec4 LmAttributeFrom(sampler2D attributes,int index) {return texelFetch(attributes,ivec2(index%lmAtlas.x,index/lmAtlas.x),0);}
int LmPartialCount(sampler2D attributes,vec4 neighbours) {return neighbours.w>0?min(int(LmAttributeFrom(attributes,int(neighbours.w)).x),6):0;}
float LmIncidentCellSize(sampler2D attributes,int triangle) {
    vec4 meta=texelFetch(attributes,ivec2(triangle%lmAtlas.x,triangle/lmAtlas.x),0);
    return meta.z;
}
vec4 LmFieldLookup(sampler2D cache,sampler2D attributes,int triangle,vec3 p,bool preserveMaterial) {
    vec4 value=LmIncidentBase(cache,attributes,triangle,p),neighbours=LmAdjacency(attributes,triangle);float weights=1;
    if(all(lessThan(neighbours.xyz,vec3(0)))&&neighbours.w<=0)return value;
    int offset=rtGeometry.x+triangle*5;vec3 a=RtGeometry(offset).xyz,b=RtGeometry(offset+1).xyz,c=RtGeometry(offset+2).xyz,N=normalize(cross(b-a,c-a));
    vec3 corners[3]=vec3[3](a,b,c);float cell=LmIncidentCellSize(attributes,triangle);
    int partialCount=LmPartialCount(attributes,neighbours);
    for(int entry=0;entry<3+partialCount;++entry) {
        int edge=entry,neighbour;vec2 interval=vec2(0,1);
        if(entry<3)neighbour=int(neighbours[entry]);
        else {vec4 partial=LmAttributeFrom(attributes,int(neighbours.w)+1+entry-3);neighbour=int(partial.x);edge=int(partial.y);interval=partial.zw;}
        if(neighbour<0)continue;
        if(preserveMaterial) {
            int other=rtGeometry.x+neighbour*5;
            if(any(greaterThan(abs(RtGeometry(offset+3).rgb-RtGeometry(other+3).rgb),vec3(.00001)))||
               any(greaterThan(abs(RtTransportEmission(RtGeometry(offset+4))-RtTransportEmission(RtGeometry(other+4))),vec3(.00001))))continue;
        }
        vec3 start=corners[edge],end=corners[(edge+1)%3];float distance=abs(dot(p-start,normalize(cross(N,end-start))));
        float band=.75*min(cell,LmIncidentCellSize(attributes,neighbour));if(distance>=band||band<=1e-7)continue;
        // The same common-edge distance gives both triangles identical .5/.5
        // values at the seam. Only coplanar topology neighbours participate.
        float lengthSquared=dot(end-start,end-start),t=dot(p-start,end-start)/max(lengthSquared,1e-12);
        float beyond=max(max(interval.x-t,t-interval.y),0)*sqrt(lengthSquared);
        float weight=(1-smoothstep(0,band,distance))*(1-smoothstep(0,band,beyond));if(weight<=0)continue;
        value+=LmIncidentBase(cache,attributes,neighbour,p)*weight;weights+=weight;
    }
    return value/weights;
}
vec4 LmIncidentLookup(sampler2D cache,sampler2D attributes,int triangle,vec3 p) {
    return LmFieldLookup(cache,attributes,triangle,p,false);
}
vec4 LmRadianceLookup(sampler2D cache,sampler2D attributes,int triangle,vec3 p) {
    return LmFieldLookup(cache,attributes,triangle,p,true);
}
vec3 LmWorldRay(vec3 p,vec3 d,out bool escaped) {
    vec3 N;float distance=lmTrace.x;int triangle;bool front;
    escaped=!LmIntersect(p,d,distance,N,triangle,front);
    if(escaped)return skyOptions.x>.5?SkyRadiance(d,0)*skyOptions.y*skyOptions.z:vec3(0);
    if(!front) {vec4 e=RtGeometry(rtGeometry.x+triangle*5+4);return RtEmissionTwoSided(e)?RtTransportEmission(e):vec3(0);}
    return LmLookup(lmCache,triangle,p+d*distance);
}
vec3 LmRayOrigin(vec3 p,vec3 N) {
    // Reconstructed raster depth can lie just behind its own triangle. Escape
    // that same-facing surface before measuring clearance to a contact surface;
    // otherwise the contact clamp keeps the origin behind the receiver and its
    // entire hemisphere intermittently hits the receiver's black backside.
    vec3 origin=p+N*.00002,normal;int triangle;bool front;
    float distance=lmTrace.y;
    if(RtIntersect(origin,N,distance,normal,triangle,front)) {
        if(!front&&distance<max(.001,length(p)*.00002)) {
            origin+=N*(distance+.00002);distance=lmTrace.y;
            if(RtIntersect(origin,N,distance,normal,triangle,front))distance*=.25;
        } else distance*=.25;
    }
    return origin+N*max(.00002,distance);
}
float LmTriangleCosineForm(vec3 p,vec3 N,vec3 a,vec3 b,vec3 c) {
    // Integrate the projected solid angle exactly. Clip at the receiver horizon
    // before evaluating the polygon; a large emitter may straddle that plane.
    vec3 vertices[3]=vec3[3](a-p,b-p,c-p),clipped[4];int count=0;
    for(int i=0;i<3;++i) {
        vec3 x=vertices[i],y=vertices[(i+1)%3];float dx=dot(N,x),dy=dot(N,y);
        if(dx>=0)clipped[count++]=x;
        if((dx>=0)!=(dy>=0))clipped[count++]=mix(x,y,dx/(dx-dy));
    }
    if(count<3)return 0;vec3 integral=vec3(0);
    for(int i=0;i<count;++i) {
        vec3 x=normalize(clipped[i]),y=normalize(clipped[(i+1)%count]),axis=cross(x,y);float len=length(axis);
        if(len>1e-7)integral+=axis/len*atan(len,clamp(dot(x,y),-1,1));
    }
    return clamp(abs(dot(N,integral))*.5,0,3.14159265359);
}
vec3 LmEmissiveIncident(vec3 p,vec3 N,int receiver) {
    vec3 incident=vec3(0);int begin=lmAtlas.w+lmAtlas.y*2,samples=min(lmAtlas.z,16);
    for(int emitterSample=0;emitterSample<samples;++emitterSample) {
        int emitter=emitterSample;float weight=1;
        if(lmAtlas.z>samples) {
            float target=(float(emitterSample)+.5)/float(samples);int low=0,high=lmAtlas.z-1;
            for(int search=0;search<16&&low<high;++search){int mid=(low+high)/2;if(LmAttribute(begin+mid).y<target)low=mid+1;else high=mid;}
            emitter=low;weight=1.0/(float(samples)*max(LmAttribute(begin+emitter).z,1e-8));
        }
        int triangle=int(LmAttribute(begin+emitter).x);if(triangle==receiver)continue;
        int offset=rtGeometry.x+triangle*5;vec4 emission=RtGeometry(offset+4);
        vec3 a=RtGeometry(offset).xyz,b=RtGeometry(offset+1).xyz,c=RtGeometry(offset+2).xyz;
        vec3 emitterN=normalize(cross(b-a,c-a));float side=dot(emitterN,p-a);
        if(abs(side)<1e-5||(!RtEmissionTwoSided(emission)&&side<=0))continue;
        // Four fixed subtriangles resolve partial occlusion without a stochastic
        // emitter hit/miss. Unoccluded energy is independent of emitter tessellation.
        vec3 ab=(a+b)*.5,bc=(b+c)*.5,ca=(c+a)*.5;
        vec3 corners[12]=vec3[12](a,ab,ca,ab,b,bc,ca,bc,c,ab,bc,ca);
        for(int cell=0;cell<4;++cell) {
            vec3 x=corners[cell*3],y=corners[cell*3+1],z=corners[cell*3+2],delta=(x+y+z)/3-p;
            float distance=length(delta),form=LmTriangleCosineForm(p,N,x,y,z);
            if(form>0&&distance>1e-5&&RtVisible(p,N,delta/distance,distance))incident+=RtTransportEmission(emission)*(form*weight/3.14159265359);
        }
    }
    return incident;
}
)GLSL";}
inline std::string LumenWorldPreamble() {
    return std::string("#version 450 core\n")+RealtimeGiQueryGLSL()+SkyLightGLSL()+RealtimeGiGeometryGLSL()+LumenDataGLSL()+"\nlayout(location=0) out vec4 outColor;\n";
}
inline std::string LumenSurfaceDirectShader() {return LumenWorldPreamble()+R"GLSL(
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);int index=pixel.y*lmAtlas.x+pixel.x;
    if(index>=lmAtlas.y){outColor=vec4(0);return;}
    if(lmSettings.z<.5&&lmReflection.y<.5){outColor=LmCacheTexel(lmCache,index);return;}
    vec4 point=LmAttribute(lmAtlas.w+index*2);vec3 N=LmAttribute(lmAtlas.w+index*2+1).xyz;
    int offset=rtGeometry.x+int(point.w)*5;vec3 rho=min(RtGeometry(offset+3).rgb,vec3(.95));
    vec3 direct=RtTransportEmission(RtGeometry(offset+4));
    if(any(greaterThan(rho,vec3(0))))direct+=rho*RtDirect(point.xyz,N)/3.14159265359;
    outColor=vec4(clamp(direct,vec3(0),vec3(60000)),1);
}
)GLSL";}
inline std::string LumenSurfaceBounceShader() {return LumenWorldPreamble()+R"GLSL(
layout(binding=6) uniform sampler2D lmDirect;
layout(binding=7) uniform sampler2D lmOldDirect;
layout(binding=9) uniform sampler2D lmOldIncident;
vec3 LmBounceRay(vec3 p,vec3 d,out bool escaped) {
    vec3 N;float distance=lmTrace.x;int triangle;bool front;
    escaped=!LmIntersect(p,d,distance,N,triangle,front);
    if(escaped)return skyOptions.x>.5?SkyRadiance(d,0)*skyOptions.y*skyOptions.z:vec3(0);
    if(!front)return vec3(0); // Emission is integrated separately by next-event sampling.
    vec3 hit=p+d*distance;
    vec3 bounce=max(LmLookup(lmCache,triangle,hit)-LmLookup(lmOldDirect,triangle,hit),vec3(0));
    if(lmGather.w!=0) {
        vec3 rho=min(RtGeometry(rtGeometry.x+triangle*5+3).rgb,vec3(.95));
        return bounce+rho*RtDirect(hit,N)/3.14159265359;
    }
    vec3 value=bounce+LmLookup(lmDirect,triangle,hit);
    return max(value-RtTransportEmission(RtGeometry(rtGeometry.x+triangle*5+4)),vec3(0));
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);int index=pixel.y*lmAtlas.x+pixel.x;
    if(index>=lmAtlas.y){outColor=vec4(0);return;}
    vec4 old=lmSettings.z>.5?vec4(0):LmCacheTexel(lmOldIncident,index);
    if(lmSettings.z<.5&&!LmActive(index)){outColor=old;return;}
    vec4 point=LmAttribute(lmAtlas.w+index*2);vec3 N=LmAttribute(lmAtlas.w+index*2+1).xyz;
    vec3 incoming=vec3(0),origin=LmRayOrigin(point.xyz,N);float sky=0;
    // Stable independent quadrature per canonical surfel. A slowly varying
    // world-position phase makes neighbouring samples miss the same bright
    // patch together, so spatial filtering cannot remove the resulting blobs.
    vec4 meta=LmAttribute(int(point.w));uint resolution=uint(meta.y);
    uint local=uint(index-int(meta.x));uvec2 cell=uvec2(local%resolution,local/resolution);
    if(cell.x+cell.y>=resolution)cell=uvec2(resolution-1u)-cell;
    float phase=float(LmHash(uvec3(uint(point.w),cell))&0xffffu)/65536.0;
    for(int ray=0;ray<lmBudget.z;++ray) {
        vec3 d=LmHemispherePhase(ray,lmBudget.z,N,phase);bool escaped;
        vec3 value=LmBounceRay(origin,d,escaped);incoming+=value*(escaped?1:lmTrace.z);
        sky+=escaped?1:0;
    }
    vec3 value=incoming/float(lmBudget.z)+LmEmissiveIncident(point.xyz,N,int(point.w))*lmTrace.z;
    // Deterministic directions; only genuine relighting/bounce convergence
    // makes this reactive. Geometry/visibility has no temporal rotation.
    float relative=length(value-old.rgb)/max(length(value),.05);
    float history=lmSettings.z>.5?0:mix(lmTrace.w,min(lmTrace.w,.25),clamp((relative-.05)/.25,0,1));
    outColor=mix(vec4(clamp(value,vec3(0),vec3(60000)),sky/float(lmBudget.z)),old,history);
}
)GLSL";}
inline std::string LumenSurfaceComposeShader() {return LumenWorldPreamble()+R"GLSL(
layout(binding=6) uniform sampler2D lmDirect;
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);int index=pixel.y*lmAtlas.x+pixel.x;
    if(index>=lmAtlas.y){outColor=vec4(0);return;}
    vec4 point=LmAttribute(lmAtlas.w+index*2);vec3 rho=min(RtGeometry(rtGeometry.x+int(point.w)*5+3).rgb,vec3(.95));
    outColor=vec4(clamp(LmCacheTexel(lmDirect,index).rgb+rho*LmCacheTexel(lmCache,index).rgb,vec3(0),vec3(60000)),1);
}
)GLSL";}
inline std::string LumenIncidentFilterShader() {return LumenWorldPreamble()+R"GLSL(
bool LmIncidentContains(int triangle,vec3 p) {vec2 uv=LmBarycentric(triangle,p);return uv.x>=-.00001&&uv.y>=-.00001&&uv.x+uv.y<=1.00001;}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);int index=pixel.y*lmAtlas.x+pixel.x;
    if(index>=lmAtlas.y){outColor=vec4(0);return;}
    vec4 point=LmAttribute(lmAtlas.w+index*2);int triangle=int(point.w);vec3 N=LmAttribute(lmAtlas.w+index*2+1).xyz;
    int offset=rtGeometry.x+triangle*5;vec3 a=RtGeometry(offset).xyz,T=normalize(RtGeometry(offset+1).xyz-a),B=cross(N,T);
    vec4 meta=LmAttributeFrom(lmAttributes,triangle);
    // Area alone underestimates the support of a skew barycentric cell. The
    // longest world-space lattice step covers its diagonal rather than leaving
    // long-axis sample noise visible as broad triangular color facets.
    float radius=max(meta.z,meta.w);vec4 center=LmCacheTexel(lmCache,index),neighbours=LmAdjacency(lmAttributes,triangle);
    vec4 values[9];float kernel[9];
    vec2 moment=vec2(0);vec3 second=vec3(0);float weights=0;int partialCount=LmPartialCount(lmAttributes,neighbours);
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        int tap=(y+1)*3+x+1;kernel[tap]=0;values[tap]=vec4(0);
        vec3 delta=(T*float(x)+B*float(y))*radius,q=point.xyz+delta;int receiver=triangle;
        if(!LmIncidentContains(receiver,q)) {
            receiver=-1;for(int edge=0;edge<3;++edge){int candidate=int(neighbours[edge]);if(candidate>=0&&LmIncidentContains(candidate,q)){receiver=candidate;break;}}
            if(receiver<0)for(int entry=0;entry<partialCount;++entry){int candidate=int(LmAttributeFrom(lmAttributes,int(neighbours.w)+1+entry).x);if(LmIncidentContains(candidate,q)){receiver=candidate;break;}}
            if(receiver<0)continue;
        }
        vec4 value=LmIncidentLookup(lmCache,lmAttributes,receiver,q);
        float skyDelta=value.a-center.a;
        float weight=exp(-.5*dot(delta,delta)/max(radius*radius,1e-10))*exp(-skyDelta*skyDelta/.12);
        values[tap]=value;kernel[tap]=weight;vec2 d=vec2(x,y);
        moment+=d*weight;second+=vec3(d.x*d.x,d.x*d.y,d.y*d.y)*weight;weights+=weight;
    }
    vec2 mean=moment/max(weights,1e-8),slope=vec2(0);
    vec3 covariance=second/max(weights,1e-8)-vec3(mean.x*mean.x,mean.x*mean.y,mean.y*mean.y);
    float determinant=covariance.x*covariance.z-covariance.y*covariance.y;
    if(determinant>1e-8) {
        slope=vec2(-covariance.z*mean.x+covariance.y*mean.y,
                   -covariance.x*mean.y+covariance.y*mean.x)/determinant;
    } else if(covariance.x+covariance.z>1e-8) {
        // Retain the well-defined first moment of a rank-one thin surface.
        float trace=covariance.x+covariance.z;
        slope=-vec2(covariance.x*mean.x+covariance.y*mean.y,
                    covariance.y*mean.x+covariance.z*mean.y)/(trace*trace);
    }
    // Limit shape coefficients, not RGB values. A light-dependent clamp would
    // still break source superposition and cause dark halos on relighting.
    vec4 sum=vec4(0);weights=0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        int tap=(y+1)*3+x+1;float w=kernel[tap]*max(0.0,1+dot(vec2(x,y)-mean,slope));
        sum+=values[tap]*w;weights+=w;
    }
    vec4 value=weights>1e-8?sum/weights:center;
    outColor=vec4(max(value.rgb,vec3(0)),clamp(value.a,0,1));
}
)GLSL";}
inline std::string LumenRadianceProbeShader() {
    return LumenWorldPreamble()+RealtimeGiRayDirectionsGLSL()+R"GLSL(
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);int ray=pixel.x,probe=pixel.y;
    if(ray>=rtOptions.x||probe>=int(rtCounts.w)||!RtActive(probe))discard;
    vec3 p=RtPosition(probe),d=RtRayDirection(ray,probe),N;float distance=rtTrace.x;int triangle;bool front;
    vec3 radiance=vec3(0);
    if(LmIntersect(p,d,distance,N,triangle,front)) {
        if(front)radiance=LmLookup(lmCache,triangle,p+d*distance);
        else {vec4 e=RtGeometry(rtGeometry.x+triangle*5+4);if(RtEmissionTwoSided(e))radiance=RtTransportEmission(e);}
    } else if(skyOptions.x>.5)radiance=SkyRadiance(d,0)*skyOptions.y*skyOptions.z;
    outColor=vec4(clamp(radiance,vec3(0),vec3(60000)),front?distance:-distance);
}
)GLSL";
}
inline std::string LumenScreenPreamble() {
    return IndirectLightingDetail::Preamble(true)+"\n#define RT_GEOMETRY_SLOT 6\n"+RealtimeGiQueryGLSL()+SkyLightGLSL()+RealtimeGiGeometryGLSL()+LumenDataGLSL()+R"GLSL(
ivec2 LmFullPixel(ivec2 probe) {return clamp(((probe*2+1)*GIFullSize())/(lmGather.xy*2),ivec2(0),GIFullSize()-1);}
bool LmFilteredSurface(ivec2 pixel,out vec3 p,out vec3 N,out float roughness) {
    if(!GISurface(pixel,p,N)||dot(N,N)<.5)return false;
    roughness=clamp(texelFetch(giNormal,pixel,0).a-1,.045,1.0);
    if(lmReflection.y<.5)return true;
    vec3 sum=vec3(0);float weights=0,r4=0;
    vec2 radius=max(vec2(GIFullSize())/vec2(lmGather.xy)*.4,vec2(1));
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        ivec2 tap=clamp(pixel+ivec2(round(vec2(x,y)*radius)),ivec2(0),GIFullSize()-1);vec3 q,n;
        if(!GISurface(tap,q,n)||dot(n,N)<.35||length(q-p)>max(.08,length(p)*.06))continue;
        float w=float((x==0?2:1)*(y==0?2:1)),r=clamp(texelFetch(giNormal,tap,0).a-1,.045,1.0);
        sum+=n*w;r4+=pow(r,4)*w;weights+=w;
    }
    if(weights>0){sum/=weights;float len=clamp(length(sum),.001,1);N=normalize(sum);
        roughness=sqrt(sqrt(min(r4/weights+min(.3*(1-len)/len,.2),1.0)));}
    return true;
}
vec4 LmGuide(ivec2 probe) {
    ivec2 pixel=LmFullPixel(probe);vec3 p,N;float filtered;
    if(!LmFilteredSurface(pixel,p,N,filtered))return vec4(0);
    vec3 worldN=normalize(mat3(lmInverseView)*N);
    ivec2 oct=ivec2(round(RtOct(worldN)*255));
    int rough=int(round(filtered*255));
    return vec4((lmInverseView*vec4(p,1)).xyz,float(1+oct.x+oct.y*256+rough*65536));
}
vec3 LmGuideNormal(vec4 guide) {
    int code=(int(guide.w)-1)&65535;return RtDecodeOct(vec2(code%256,code/256)/255);
}
float LmGuideRoughness(vec4 guide){return float((int(guide.w)-1)>>16)/255.0;}
bool LmScreenRay(vec3 origin,vec3 direction,out vec3 radiance,out vec3 hitWorld) {
    radiance=vec3(0);hitWorld=vec3(0);float previous=0;
    vec4 start=giProjection*vec4(origin,1),delta=giProjection*vec4(direction,0);
    for(int step=0;step<lmGather.z;++step) {
        float distance=min(lmTrace.x,8.0)*float(step+1)/float(lmGather.z);ivec2 pixel;
        if(!GIProjectClip(start+delta*distance,pixel))break;
        vec3 surface,N;if(!GISurface(pixel,surface,N)) {previous=distance;continue;}
        vec3 at=origin+direction*distance;
        if(surface.z-at.z>=-lmSettings.y&&dot(at-surface,N)<=0) {
            float low=previous,high=distance;
            for(int refine=0;refine<5;++refine) {
                float middle=(low+high)*.5;ivec2 tap;vec3 q,n;
                if(GIProject(origin+direction*middle,tap)&&GISurface(tap,q,n)&&q.z-(origin+direction*middle).z>=-lmSettings.y&&dot(origin+direction*middle-q,n)<=0)high=middle;
                else low=middle;
            }
            at=origin+direction*high;
            if(!GIProject(at,pixel)||!GISurface(pixel,surface,N)||abs(surface.z-at.z)>lmSettings.y||
               high<=lmTrace.y||dot(N,-direction)<=.05||dot(N,origin-surface)<=0)return false;
            hitWorld=(lmInverseView*vec4(at,1)).xyz;
            // Finite footprint: resolve bright fine emissive texels without
            // treating a tiny emitter as the entire angular ray footprint.
            float radius=clamp(high*.5*float(GIFullSize().y)*abs(giProjection[1][1])/max(-at.z,.1)*.35/sqrt(float(lmBudget.w)),.75,3.0);
            vec3 fine=vec3(0);float weights=0;
            for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
                float w=float((x==0?2:1)*(y==0?2:1));weights+=w;
                ivec2 tap=pixel+ivec2(round(vec2(x,y)*radius));vec3 q,n;
                if(any(lessThan(tap,ivec2(0)))||any(greaterThanEqual(tap,GIFullSize()))||!GISurface(tap,q,n)||dot(n,N)<.9||abs(dot(q-surface,N))>.05)continue;
                fine+=textureLod(giInput,(vec2(tap)+.5)/vec2(GIFullSize()),0).rgb*w;
            }
            radiance=fine/weights;return true;
        }
        previous=distance;
    }
    return false;
}
)GLSL";
}
inline std::string LumenGuideShader(){return LumenScreenPreamble()+R"GLSL(
void main(){outColor=LmGuide(ivec2(gl_FragCoord.xy));}
)GLSL";}
inline std::string LumenGatherShader(){return LumenScreenPreamble()+R"GLSL(
layout(binding=9) uniform sampler2D lmIncident;
void main() {
    ivec2 index=ivec2(gl_FragCoord.xy),pixel=LmFullPixel(index);vec3 p,N;
    if(!GISurface(pixel,p,N)||dot(N,N)<.5){outColor=vec4(0);return;}
    vec3 world=(lmInverseView*vec4(p,1)).xyz,worldN=normalize(mat3(lmInverseView)*N),hitN;
    // A sparse receiver lookup identifies the stable world irradiance solution.
    // It does not shoot a new hemisphere quadrature whenever the view moves.
    float offset=max(.004,lmTrace.y),distance=offset*2;int triangle;bool front;
    if(RtIntersect(world+worldN*offset,-worldN,distance,hitN,triangle,front)&&front&&dot(hitN,worldN)>.8) {
        vec4 incident=LmIncidentLookup(lmIncident,lmAttributes,triangle,world);
        outColor=vec4(incident.rgb*(3.14159265359*lmSettings.x),float(triangle+1));return;
    }
    vec3 E;float sky;EvaluateRealtimeGi(world,worldN,worldN,1,E,sky);
    outColor=vec4(E*(3.14159265359*lmSettings.x),0);
}
)GLSL";}
inline std::string LumenTemporalShader(bool reflection=false){return LumenScreenPreamble()+(reflection?"\n#define LM_TEMPORAL_REFLECTION 1\n":"\n#define LM_TEMPORAL_REFLECTION 0\n")+R"GLSL(
layout(binding=7) uniform sampler2D lmPreviousGuide;
void main() {
    ivec2 at=ivec2(gl_FragCoord.xy);vec4 current=texelFetch(giInput,at,0),guide=texelFetch(lmAttributes,at,0);
    if(guide.w<.5||lmGather.w==0||lmCamera.w<=0){outColor=current;return;}
    vec4 q=lmPreviousVP*vec4(guide.xyz,1);vec2 uv=q.xy/q.w*.5+.5;
    if(q.w<=0||any(lessThanEqual(uv,vec2(0)))||any(greaterThanEqual(uv,vec2(1)))){outColor=current;return;}
    vec3 N=LmGuideNormal(guide);float roughness=LmGuideRoughness(guide);
    float footprint=max(.12,length(guide.xyz-lmCamera.xyz)*.06);
    vec2 previous=uv*vec2(lmGather.xy)-.5;ivec2 base=ivec2(floor(previous));vec2 f=fract(previous);
    vec4 old=vec4(0);float oldWeight=0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){ivec2 tap=base+ivec2(x,y);
        if(any(lessThan(tap,ivec2(0)))||any(greaterThanEqual(tap,lmGather.xy)))continue;
        vec4 g=texelFetch(lmPreviousGuide,tap,0);
        if(g.w<.5||distance(g.xyz,guide.xyz)>footprint||dot(LmGuideNormal(g),N)<.9||abs(dot(g.xyz-guide.xyz,N))>.04)continue;
#if LM_TEMPORAL_REFLECTION
        if(abs(LmGuideRoughness(g)-roughness)>.08)continue;
#endif
        float w=(x==0?1-f.x:f.x)*(y==0?1-f.y:f.y);
        old+=texelFetch(lmCache,tap,0)*w;oldWeight+=w;
    }
    if(oldWeight<.05){outColor=current;return;}old/=oldWeight;
    float gridMotion=length(uv*vec2(lmGather.xy)-(vec2(at)+.5));
    bool stableTransport=lmSettings.z<.5&&gridMotion<1.0;
#if LM_TEMPORAL_REFLECTION
    stableTransport=stableTransport&&roughness>.14;
#endif
    vec4 lo=current,hi=current;vec3 mean=vec3(0),square=vec3(0);float count=0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        ivec2 tap=clamp(at+ivec2(x,y),ivec2(0),lmGather.xy-1);vec4 g=texelFetch(lmAttributes,tap,0);
        if(g.w<.5||dot(LmGuideNormal(g),N)<.9||abs(dot(g.xyz-guide.xyz,N))>.08)continue;
        vec4 c=texelFetch(giInput,tap,0);lo=min(lo,c);hi=max(hi,c);mean+=c.rgb;square+=c.rgb*c.rgb;count+=1;
    }
    mean/=max(count,1);vec3 sigma=sqrt(max(square/max(count,1)-mean*mean,vec3(0)));
    vec3 margin=sigma*.5+vec3(.005);
    // Sparse hemisphere/rough rays can cross a tiny emitter as the view moves. Include
    // the geometrically validated history in the noise envelope; otherwise a
    // coherent new quadrature outlier clips all history and flashes instantly.
    // Real source/geometry changes and faster parallax retain reactive clipping.
    if(stableTransport)margin=max(margin,abs(current.rgb-old.rgb)*.9);
    old.rgb=clamp(old.rgb,max(lo.rgb-margin,vec3(0)),hi.rgb+margin);
    float relative=max(0.0,length(old.rgb-current.rgb)-length(sigma)*1.5)/max(length(current.rgb),.05);
    float history=lmCamera.w,confidence=1;
#if LM_TEMPORAL_REFLECTION
    float hitChange=abs(old.a-current.a)/max(max(old.a,current.a),.25);
    if(roughness<.12&&hitChange>.15){outColor=current;return;}
    confidence=roughness<.12?1:mix(1.0,.6,clamp(hitChange,0,1));
    history=mix(history,max(history,.93),smoothstep(.12,.5,roughness));
#endif
    float weight=mix(history,min(history,.35),clamp((relative-.12)/.6,0,1))*confidence;
    if(stableTransport)weight=min(.98,lmCamera.w*(.97/.85));
    outColor=mix(current,old,weight);
}
)GLSL";}
inline std::string LumenFilterShader(){return LumenScreenPreamble()+R"GLSL(
void main() {
    ivec2 at=ivec2(gl_FragCoord.xy);vec4 guide=texelFetch(lmAttributes,at,0);
    if(guide.w<.5){outColor=vec4(0);return;}vec3 N=LmGuideNormal(guide);vec4 sum=vec4(0);float weights=0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        ivec2 tap=clamp(at+ivec2(x,y)*giOptions.w,ivec2(0),lmGather.xy-1);vec4 g=texelFetch(lmAttributes,tap,0);
        if(g.w<.5||dot(LmGuideNormal(g),N)<.9)continue;
        float plane=abs(dot(g.xyz-guide.xyz,N));float w=float((x==0?2:1)*(y==0?2:1))*exp(-plane*plane/.02);
        sum+=texelFetch(giInput,tap,0)*w;weights+=w;
    }
    outColor=sum/max(weights,1e-6);
}
)GLSL";}
inline std::string LumenViewResolveShader(){return LumenScreenPreamble()+R"GLSL(
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);vec3 p,N;
    if(!GISurface(pixel,p,N)||dot(N,N)<.5){outColor=vec4(0);return;}
    vec3 world=(lmInverseView*vec4(p,1)).xyz,worldN=normalize(mat3(lmInverseView)*N);
    vec2 at=(vec2(pixel)+.5)*vec2(lmGather.xy)/vec2(GIFullSize())-.5;ivec2 base=ivec2(floor(at));vec4 sum=vec4(0);float weights=0;
    float sigma=max(.04,length(p)*.015);
    for(int y=-1;y<=2;++y)for(int x=-1;x<=2;++x) {
        ivec2 tap=clamp(base+ivec2(x,y),ivec2(0),lmGather.xy-1);vec4 guide=texelFetch(lmAttributes,tap,0);
        if(guide.w<.5||dot(LmGuideNormal(guide),worldN)<.8)continue;
        float plane=max(abs(dot(guide.xyz-world,worldN)),abs(dot(guide.xyz-world,LmGuideNormal(guide))));
        vec2 distance=at-vec2(base+ivec2(x,y));float spatial=exp(-dot(distance,distance)*1.5);
        float w=spatial*exp(-.5*plane*plane/(sigma*sigma));sum+=texelFetch(giInput,tap,0)*w;weights+=w;
    }
    if(weights>.05)outColor=sum/weights;
    else {vec3 E;float sky;EvaluateRealtimeGi(world,worldN,worldN,1,E,sky);outColor=vec4(E*3.14159265359*lmSettings.x,sky);}
}
)GLSL";}
inline std::string LumenResolveShader(bool fullResolutionDirect=false){return LumenScreenPreamble()+SurfacePassGLSL()+
    (fullResolutionDirect?"\n#define LM_RECEIVER_DIRECT 1\n":"\n#define LM_RECEIVER_DIRECT 0\n")+R"GLSL(
layout(binding=7) uniform sampler2D lmSceneAttributes;
layout(binding=9) uniform sampler2D lmIncident;
bool LmReceiverContains(int triangle,vec3 p,vec3 N) {
    int offset=rtGeometry.x+triangle*5;vec3 a=RtGeometry(offset).xyz,ab=RtGeometry(offset+1).xyz-a,ac=RtGeometry(offset+2).xyz-a,q=p-a;
    vec3 normal=normalize(cross(ab,ac));if(dot(normal,N)<.8||abs(dot(q,normal))>max(.012,length(p-lmCamera.xyz)*.00004))return false;
    float aa=dot(ab,ab),bb=dot(ac,ac),cc=dot(ab,ac),den=max(aa*bb-cc*cc,1e-20);
    float u=(dot(q,ab)*bb-dot(q,ac)*cc)/den,v=(dot(q,ac)*aa-dot(q,ab)*cc)/den;
    return u>=-.005&&v>=-.005&&u+v<=1.005;
}
vec4 LmResolvedReceiver(int triangle,vec3 world,int lightingView) {
#if LM_RECEIVER_DIRECT
    // This variant is selected only for visible total/direct diagnostic views.
    // Cache-space binary visibility cannot resolve the spotlight footprint.
    int offset=rtGeometry.x+triangle*5;
    vec3 a=RtGeometry(offset).xyz;
    vec3 N=normalize(cross(RtGeometry(offset+1).xyz-a,RtGeometry(offset+2).xyz-a));
    vec3 receiver=world-N*dot(world-a,N),rho=min(RtGeometry(offset+3).rgb,vec3(.95));
    vec3 radiance=RtTransportEmission(RtGeometry(offset+4))+rho*RtDirect(receiver,N)/3.14159265359;
    if(lightingView==1)radiance+=rho*LmIncidentLookup(lmIncident,lmSceneAttributes,triangle,world).rgb;
#else
    if(lightingView==0) {
        vec4 incident=LmIncidentLookup(lmCache,lmSceneAttributes,triangle,world);
        return vec4(incident.rgb*(3.14159265359*lmSettings.x),clamp(incident.a,0,1));
    }
    vec3 radiance;
    if(lightingView==2) {
        vec3 rho=min(RtGeometry(rtGeometry.x+triangle*5+3).rgb,vec3(.95));
        radiance=rho*LmIncidentLookup(lmCache,lmSceneAttributes,triangle,world).rgb;
    } else {
        // These caches already include rho and, where applicable, transport
        // emission. Preserve material boundaries by reconstructing only this
        // triangle; cross-material irradiance blending applies above instead.
        radiance=LmRadianceLookup(lmCache,lmSceneAttributes,triangle,world).rgb;
    }
#endif
    return vec4(max(radiance,vec3(0))*lmSettings.x,1);
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);vec3 p,N;
    if(!GISurface(pixel,p,N)||dot(N,N)<.5){outColor=vec4(0);return;}
    vec3 world=(lmInverseView*vec4(p,1)).xyz,worldN=normalize(mat3(lmInverseView)*N);
    int lightingView=int(round(surfaceScreenJitter.z));
    vec2 at=(vec2(pixel)+.5)*vec2(lmGather.xy)/vec2(GIFullSize())-.5;ivec2 base=ivec2(floor(at));
    // Identify this exact world receiver from a neighbouring sparse-grid ID.
#if LM_RECEIVER_DIRECT
    int receiver=-1;
    for(int y=0;y<=1&&receiver<0;++y)for(int x=0;x<=1&&receiver<0;++x) {
        ivec2 tap=clamp(base+ivec2(x,y),ivec2(0),lmGather.xy-1);int triangle=int(texelFetch(giInput,tap,0).a+.5)-1;
        if(triangle>=0&&triangle<rtGeometry.w&&LmReceiverContains(triangle,world,worldN)) {
            receiver=triangle;
        }
    }
    if(receiver<0) {
        // A missing sparse-grid ID is not missing physical illumination. One
        // short receiver query supplies the exact triangle for diagnostic
        // pixels; never substitute probe E for already reflected radiance.
        float offset=max(.004,lmTrace.y),distance=offset*2;vec3 hitN;int triangle;bool front;
        if(RtIntersect(world+worldN*offset,-worldN,distance,hitN,triangle,front)&&front&&
           dot(hitN,worldN)>.8&&LmReceiverContains(triangle,world,worldN))
            receiver=triangle;
    }
    // Resolve lighting once after choosing the receiver. Inlining the direct
    // ray queries at each ID candidate increases register pressure even for
    // fragments whose first candidate succeeds.
    if(receiver>=0){outColor=LmResolvedReceiver(receiver,world,lightingView);return;}
    outColor=vec4(0);
#else
    // Keep the lightweight cached path separate from analytic shadow queries.
    for(int y=0;y<=1;++y)for(int x=0;x<=1;++x) {
        ivec2 tap=clamp(base+ivec2(x,y),ivec2(0),lmGather.xy-1);int triangle=int(texelFetch(giInput,tap,0).a+.5)-1;
        if(triangle>=0&&triangle<rtGeometry.w&&LmReceiverContains(triangle,world,worldN)) {
            outColor=LmResolvedReceiver(triangle,world,lightingView);return;
        }
    }
    if(lightingView!=0) {
        float offset=max(.004,lmTrace.y),distance=offset*2;vec3 hitN;int triangle;bool front;
        if(RtIntersect(world+worldN*offset,-worldN,distance,hitN,triangle,front)&&front&&
           dot(hitN,worldN)>.8&&LmReceiverContains(triangle,world,worldN))
            outColor=LmResolvedReceiver(triangle,world,lightingView);
        else outColor=vec4(0);
        return;
    }
    vec3 E;float sky;EvaluateRealtimeGi(world,worldN,worldN,1,E,sky);outColor=vec4(E*(3.14159265359*lmSettings.x),sky);
#endif
}
)GLSL";}
inline std::string LumenReflectionShader(){return LumenScreenPreamble()+R"GLSL(
layout(binding=7) uniform sampler2D lmDirect;
layout(binding=9) uniform sampler2D lmIncident;
float LmLambda(float cosine,float alpha) {
    float c=max(cosine,.0001);return .5*(sqrt(1+alpha*alpha*(1-c*c)/(c*c))-1);
}
vec3 LmVisibleNormal(vec3 V,float alpha,vec2 xi) {
    vec3 vh=normalize(vec3(alpha*V.xy,V.z));float lensq=dot(vh.xy,vh.xy);
    vec3 t1=lensq>1e-8?vec3(-vh.y,vh.x,0)/sqrt(lensq):vec3(1,0,0),t2=cross(vh,t1);
    float radius=sqrt(xi.x),angle=6.283185307*xi.y,u=radius*cos(angle),v=radius*sin(angle);
    float s=.5*(1+vh.z);v=(1-s)*sqrt(max(0.0,1-u*u))+s*v;
    vec3 nh=u*t1+v*t2+sqrt(max(0.0,1-u*u-v*v))*vh;
    return normalize(vec3(alpha*nh.xy,max(nh.z,0)));
}
vec3 LmSpecularBrdf(vec3 N,vec3 V,vec3 L,vec3 F0,float roughness) {
    float nv=max(dot(N,V),.0001),nl=max(dot(N,L),0);if(nl==0)return vec3(0);
    vec3 H=normalize(V+L);float a=max(roughness*roughness,.002),a2=a*a,nh=max(dot(N,H),0),den=nh*nh*(a2-1)+1;
    float D=a2/(3.14159265359*den*den),G=1/(1+LmLambda(nv,a)+LmLambda(nl,a));
    vec3 F=F0+(1-F0)*pow(1-max(dot(V,H),0),5);return F*(D*G/(4*nv*max(nl,.0001)));
}
vec3 LmHitRadiance(vec3 origin,vec3 direction,float cone,float primaryRoughness,out float distance,out int triangle,out bool front) {
    distance=lmTrace.x;vec3 N;
    if(!LmIntersect(origin,direction,distance,N,triangle,front))return skyOptions.x>.5?SkyRadiance(direction,0)*skyOptions.y*skyOptions.w:vec3(0);
    int offset=rtGeometry.x+triangle*5;vec4 emission=RtGeometry(offset+4);
    if(RtEmissionAnalytic(emission))return vec3(0);
    if(!front)return RtEmissionTwoSided(emission)?RtTransportEmission(emission):vec3(0);
    vec3 point=origin+direction*distance,V=-direction;
    vec3 value=LmLookup(lmCache,triangle,point);
    // Rough primary cones reuse the solved hit radiance. Evaluating every
    // secondary point/area highlight and another specular ray is unnecessary
    // for this bandwidth and creates a second sparse high-energy estimator.
    if(lmFieldSpacing.w>0&&primaryRoughness>.2)return value;
    vec3 F0=vec3(RtGeometry(offset).w,RtGeometry(offset+1).w,RtGeometry(offset+2).w);
    float roughness=clamp(RtGeometry(offset+3).w,.045,1.0);
    // Integrate secondary highlights over the primary reflection's angular
    // footprint. A sparse rough ray must not see an infinitesimal point BRDF.
    roughness=sqrt(sqrt(min(pow(roughness,4)+cone*cone,1.0)));
    vec3 L=-rtSunDirectionIntensity.xyz;float nl=max(dot(N,L),0);
    if(rtSunDirectionIntensity.w>0&&nl>0&&RtVisible(point,N,L,lmTrace.x))
        value+=LmSpecularBrdf(N,V,L,F0,roughness)*rtSunColor.rgb*(nl*rtSunDirectionIntensity.w);
    for(int i=0;i<4;++i){if(all(lessThanEqual(rtPointColors[i].rgb,vec3(0))))continue;vec3 delta=rtPointPositions[i].xyz-point;float range=length(delta);if(range<.0001)continue;L=delta/range;nl=max(dot(N,L),0);
        float radius=max(rtPointColors[i].w,0),filtered=roughness;
        if(radius>0){vec3 H=normalize(V+L);float hc=dot(H,L),kernel=.5*min(radius*radius/max(range*range,1e-8),1.0)*(1+hc*hc)/max(dot(V+L,V+L),1e-4);filtered=sqrt(sqrt(min(pow(roughness,4)+kernel,1.0)));}
        if(nl>0&&RtVisible(point,N,L,max(range-radius,0)))value+=LmSpecularBrdf(N,V,L,F0,filtered)*rtPointColors[i].rgb*(nl/max(max(range*range,radius*radius),.0001));}
    if(any(greaterThan(rtSpotIntensityInner.rgb,vec3(0)))) {
        vec3 delta=rtSpotPositionRadius.xyz-point;float range=length(delta);
        if(range>.0001) {
            L=delta/range;nl=max(dot(N,L),0);float beam=RtSpotCone(-L),radius=rtSpotPositionRadius.w,filtered=roughness;
            if(radius>0){vec3 H=normalize(V+L);float hc=dot(H,L),kernel=.5*min(radius*radius/max(range*range,1e-8),1.0)*(1+hc*hc)/max(dot(V+L,V+L),1e-4);filtered=sqrt(sqrt(min(pow(roughness,4)+kernel,1.0)));}
            if(nl>0&&beam>0&&RtVisible(point,N,L,max(range-radius,0)))
                value+=LmSpecularBrdf(N,V,L,F0,filtered)*rtSpotIntensityInner.rgb*(nl*beam/max(max(range*range,radius*radius),.0001));
        }
    }
    for(int i=0;i<4;++i){if(rtArea[i].center.w<.5)continue;vec3 crossUV=cross(rtArea[i].u.xyz,rtArea[i].v.xyz);float area=length(crossUV);vec3 emitterN=crossUV/max(area,.00001);
        for(int y=0;y<2;++y)for(int x=0;x<2;++x){vec3 delta=rtArea[i].center.xyz+rtArea[i].u.xyz*(float(x)-.5)+rtArea[i].v.xyz*(float(y)-.5)-point;float range=length(delta);if(range<.0001)continue;L=delta/range;nl=max(dot(N,L),0);float face=dot(emitterN,-L),lightFace=rtArea[i].radiance.w>.5?abs(face):max(face,0);
            vec3 H=normalize(V+L),du=(rtArea[i].u.xyz-L*dot(L,rtArea[i].u.xyz))/range,dv=(rtArea[i].v.xyz-L*dot(L,rtArea[i].v.xyz))/range;
            du=(du-H*dot(H,du))/max(length(V+L),.0001);dv=(dv-H*dot(H,dv))/max(length(V+L),.0001);
            float filtered=sqrt(sqrt(min(pow(roughness,4)+.5*(dot(du,du)+dot(dv,dv)),1.0)));
            if(nl>0&&lightFace>0&&RtVisible(point,N,L,range))value+=LmSpecularBrdf(N,V,L,F0,filtered)*rtArea[i].radiance.rgb*(nl*lightFace*area/max(range*range,.0001));}}
    // One bounded secondary specular bounce supplies reflected offscreen
    // objects. The sampled GGX weight is the same BRDF/PDF ratio as the primary.
    vec3 T=normalize(cross(abs(N.z)<.99?vec3(0,0,1):vec3(0,1,0),N)),B=cross(N,T);
    vec3 localV=vec3(dot(V,T),dot(V,B),max(dot(V,N),.0001));float a=roughness*roughness;
    vec3 H=LmVisibleNormal(localV,a,vec2(.375,.618033989)),localL=reflect(-localV,H);
    if(localL.z>0){vec3 ray=normalize(T*localL.x+B*localL.y+N*localL.z);bool escaped;vec3 incoming=LmWorldRay(LmRayOrigin(point,N),ray,escaped);
        if(escaped)incoming=skyOptions.x>.5?SkyRadiance(ray,0)*skyOptions.y*skyOptions.w:vec3(0);
        float weight=(1+LmLambda(localV.z,a))/(1+LmLambda(localV.z,a)+LmLambda(localL.z,a));vec3 F=F0+(1-F0)*pow(1-max(dot(localV,H),0),5);value+=incoming*F*weight;}
    return clamp(value,vec3(0),vec3(60000));
}
vec3 LmRoughReflection(vec3 world,vec3 N,vec3 V,float roughness) {
    float offset=max(.004,lmTrace.y),distance=offset*2;vec3 hitN;int triangle;bool front;
    vec3 incident;float sky;
    if(RtIntersect(world+N*offset,-N,distance,hitN,triangle,front)&&front&&dot(hitN,N)>.35) {
        vec4 cached=LmIncidentLookup(lmIncident,lmAttributes,triangle,world);incident=cached.rgb;sky=clamp(cached.a,0,1);
    } else EvaluateRealtimeGi(world,N,N,1,incident,sky);
    // Separate cached sky diffuse from local scene radiance so diffuse and
    // specular sky-strength controls remain independent. The prefiltered sky
    // retains directional reflections; local rough transport uses E / PI.
    vec3 radiance=incident;
    if(skyOptions.x>.5)radiance=max(incident-SkyDiffuse(N)*skyOptions.y*skyOptions.z*sky,vec3(0))+
        SkyRadiance(reflect(-V,N),roughness)*skyOptions.y*skyOptions.w*sky;
    vec2 dfg=textureLod(skyBrdf,vec2(max(dot(N,V),.001),roughness),0).rg;
    return radiance*(lmReflection.w<.5?dfg.x:dfg.y);
}
void main() {
    ivec2 pixel=LmFullPixel(ivec2(gl_FragCoord.xy));vec3 p,N;float roughness;
    if(!LmFilteredSurface(pixel,p,N,roughness)){outColor=vec4(0);return;}
    vec3 world=(lmInverseView*vec4(p,1)).xyz,worldN=normalize(mat3(lmInverseView)*N);
    vec3 V=normalize(lmCamera.xyz-world);float alpha=roughness*roughness;
    float reuse=lmFieldSpacing.w>0?smoothstep(max(.01,lmFieldSpacing.w-.1),lmFieldSpacing.w,roughness):0;
    vec3 roughResult=vec3(0);
    if(reuse>0)roughResult=LmRoughReflection(world,worldN,V,roughness);
    if(reuse>=1){outColor=vec4(clamp(roughResult,vec3(0),vec3(60000)),lmTrace.x);return;}
    vec3 T=normalize(cross(abs(worldN.z)<.99?vec3(0,0,1):vec3(0,1,0),worldN)),B=cross(worldN,T);
    vec3 localV=vec3(dot(V,T),dot(V,B),max(dot(V,worldN),.0001));vec3 origin=LmRayOrigin(world,worldN),sum=vec3(0);float distances=0;
    int count=int(lmReflection.x);float phase=fract(dot(world,vec3(.1031,.11369,.13787)));
    for(int ray=0;ray<count;++ray){vec2 xi=vec2((float(ray)+.5)/float(count),fract(float(ray)*.618033989+phase));
        // Fixed quadrature keeps static rough reflections stable. History
        // accumulates reprojection and changing views rather than rotating noise.
        vec3 H=LmVisibleNormal(localV,alpha,xi),localL=reflect(-localV,H);if(localL.z<=0){distances+=lmTrace.x;continue;}
        vec3 d=normalize(T*localL.x+B*localL.y+worldN*localL.z),value,hit;float distance=lmTrace.x;
        int triangle;bool front;float cone=max(alpha*.25,.0005);
        value=LmHitRadiance(origin,d,cone,roughness,distance,triangle,front);
        vec3 fine;bool screenHit=lmSettings.w>.5&&triangle>=0&&front&&!RtEmissionAnalytic(RtGeometry(rtGeometry.x+triangle*5+4))&&LmScreenRay((giView*vec4(origin,1)).xyz,mat3(giView)*d,fine,hit);
        if(screenHit&&length(hit-(origin+d*distance))<max(.04,lmSettings.y)) {
            vec3 viewHit=(giView*vec4(hit,1)).xyz;
            float footprint=distance*max(alpha,.002)*float(GIFullSize().y)*abs(giProjection[1][1])/max(-viewHit.z,.1)/sqrt(float(count));
            float confidence=clamp(3.0/max(footprint,3.0),0,1);
            value=mix(value,fine,confidence);
        }
        float fc=pow(1-max(dot(localV,H),0),5),weight=(1+LmLambda(localV.z,alpha))/(1+LmLambda(localV.z,alpha)+LmLambda(localL.z,alpha));
        sum+=value*weight*(lmReflection.w<.5?1-fc:fc);distances+=distance;
    }
    outColor=vec4(clamp(mix(sum/float(count),roughResult,reuse),vec3(0),vec3(60000)),distances/float(count));
}
)GLSL";}
inline std::string LumenReflectionTemporalShader() {
    return LumenTemporalShader(true);
}
inline std::string LumenReflectionFilterShader() {
    auto source=LumenFilterShader();auto at=source.find("if(guide.w<.5)");source.insert(at,"float roughness=LmGuideRoughness(guide);if(roughness<.14){outColor=texelFetch(giInput,at,0);return;}\n    ");
    at=source.find("if(g.w<.5||");source.insert(at,"if(abs(LmGuideRoughness(g)-roughness)>.08)continue;\n        ");return source;
}
inline std::string LumenReflectionResolveShader() {
    auto source=LumenViewResolveShader();
    const std::string pixel="ivec2 pixel=ivec2(gl_FragCoord.xy);vec3 p,N;";
    auto pixelAt=source.find(pixel);source.replace(pixelAt,pixel.size(),"ivec2 pixel=clamp(ivec2(gl_FragCoord.xy*vec2(lmReflection.z,lmFieldOrigin.w)),ivec2(0),GIFullSize()-1);vec3 p,N;");
    const std::string old="outColor=vec4(E*3.14159265359*lmSettings.x,sky);";
    const auto at=source.find(old);
    source.replace(at,old.size(),"vec3 V=normalize(lmCamera.xyz-world);float roughness=clamp(texelFetch(giNormal,pixel,0).a-1,0,1);vec2 dfg=textureLod(skyBrdf,vec2(max(dot(V,worldN),0),roughness),0).rg;outColor=vec4((skyOptions.x>.5?SkyRadiance(reflect(-V,worldN),roughness)*skyOptions.y*skyOptions.w*sky:vec3(0))*(lmReflection.w<.5?dfg.x:dfg.y),1);");
    const auto marker="if(guide.w<.5||dot(LmGuideNormal(guide),worldN)<.8)continue;";
    const auto p=source.find(marker);source.replace(p,std::string(marker).size(),"if(guide.w<.5||dot(LmGuideNormal(guide),worldN)<.8)continue;");
    const auto weight=source.find("sum+=texelFetch(giInput,tap,0)*w;");
    source.insert(weight,"float roughDelta=LmGuideRoughness(guide)-clamp(texelFetch(giNormal,pixel,0).a-1,0,1);w*=exp(-roughDelta*roughDelta/.02);\n        ");
    return source;
}
} // namespace Render::PipelineDetail
