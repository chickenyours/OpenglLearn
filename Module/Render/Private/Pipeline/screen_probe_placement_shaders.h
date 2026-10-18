#pragma once
#include <string>

namespace Render::ScreenProbePlacementDetail {
inline std::string Constants() { return R"GLSL(
layout(std140,binding=9) uniform PlacementData {
    mat4 spInverseView; mat4 spInverseProjection; mat4 spViewProjection; mat4 spView;
    ivec4 spGrid; ivec4 spScreen; vec4 spOptions;
    ivec4 spTracing; vec4 spTraceParams; ivec4 spTraceAtlas; ivec4 spTraceDisplay;
    vec4 spRefinement; // enabled, detection radius / spacing, support / spacing, normal cosine
};
)GLSL"; }
// Placement and final reconstruction must use exactly the same jittered tile
// stencil and surface-validity test. Otherwise the allocator can declare a
// curved surface covered by a probe the final lighting pass must reject.
inline std::string SamplingGLSL() {return R"GLSL(
vec2 SPHammersley(uint index,uint count,uvec2 seed) {
    return vec2(fract(float(index)/float(count)+float(seed.x)/65536.0),
        float((bitfieldReverse(index)>>16)^seed.y)/65536.0);
}
uvec2 SPSeed(ivec2 tile) {
    uvec3 v=uvec3(tile,spScreen.z)*1664525u+1013904223u;
    v.x+=v.y*v.z;v.y+=v.z*v.x;v.z+=v.x*v.y;
    v.x+=v.y*v.z;v.y+=v.z*v.x;v.z+=v.x*v.y;
    return (v>>16).xy;
}
ivec2 SPJitter(){return ivec2(SPHammersley(uint(spScreen.z),8u,uvec2(0))*float(spGrid.w));}
ivec2 SPUniformPixel(ivec2 tile){return min(tile*spGrid.w+SPJitter(),spScreen.xy-1);}
ivec2 SPCandidatePixel(ivec2 tile,int sampleIndex) {
    return tile*spGrid.w+SPJitter()+ivec2(clamp(
        SPHammersley(uint(sampleIndex),uint(spGrid.z),SPSeed(tile))*float(spGrid.w),
        vec2(0),vec2(spGrid.w-1)));
}
ivec2 SPSlot(ivec2 tile,int sampleIndex){return ivec2(tile.x*spGrid.z+sampleIndex,tile.y);}
ivec2 SPCornerBase(ivec2 pixel) {
    ivec2 full=clamp(pixel-SPJitter(),ivec2(0),spScreen.xy-1);
    return min(full/spGrid.w,max(spGrid.xy-2,ivec2(0)));
}
ivec2 SPCorner(ivec2 base,int corner){return min(base+ivec2(corner%2,corner/2),spGrid.xy-1);}
vec2 SPBilinearFractions(ivec2 pixel,ivec2 base) {
    ivec2 full=clamp(pixel-SPJitter(),ivec2(0),spScreen.xy-1);
    vec2 f=vec2(full-base*spGrid.w+1)/float(spGrid.w+2);
    return (spTracing.w&2)!=0?clamp(f,vec2(0),vec2(1)):f;
}
float SPGiPlaneWeight(vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    if(probe.w<.5||normalDepth.w<=0||dot(normalDepth.xyz,normalDepth.xyz)<.5)return 0;
    if(any(isnan(probe.xyz))||any(isinf(probe.xyz))||
        any(isnan(normalDepth.xyz))||any(isinf(normalDepth.xyz)))return 0;
    vec3 pn=normalize(normalDepth.xyz);float alignment=dot(n,pn);if(alignment<=.6)return 0;
    vec3 delta=probe.xyz-p;
    float plane=max(abs(dot(delta,n)),abs(dot(delta,pn)));
    float relative=plane/max(min(depth,normalDepth.w),.0001);
    // Values beneath the allocation threshold are missing coverage, not a
    // tiny confidence to normalize back into a full-strength light sample.
    if(relative>spOptions.x)return 0;
    float weight=exp2(-6.643856189774725*relative/spOptions.x)*pow(alignment,8.0);
    return weight>=.01?weight:0;
}
bool SPGiRefinementEnabled(){return (spTracing.w&2)!=0&&spRefinement.x>=.5;}
float SPGiContinuousPlaneWeight(vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    if(probe.w<.5||normalDepth.w<=0||dot(normalDepth.xyz,normalDepth.xyz)<.5)return 0;
    if(any(isnan(probe.xyz))||any(isinf(probe.xyz))||
        any(isnan(normalDepth.xyz))||any(isinf(normalDepth.xyz)))return 0;
    vec3 pn=normalize(normalDepth.xyz);float alignment=dot(n,pn);if(alignment<=.6)return 0;
    vec3 delta=probe.xyz-p;
    float plane=max(abs(dot(delta,n)),abs(dot(delta,pn)));
    float relative=plane/max(min(depth,normalDepth.w),.0001);
    // Keep the hard geometric rejection. Confidence inside that support is
    // continuous: a small normal or plane change must not toggle lighting off.
    if(relative>spOptions.x)return 0;
    return exp2(-6.643856189774725*relative/spOptions.x)*pow(alignment,8.0);
}
vec2 SPGiProbePixel(vec4 probe) {
    vec4 clip=spViewProjection*vec4(probe.xyz,1);
    return clip.w>0?(clip.xy/clip.w*.5+.5)*vec2(spScreen.xy)-.5:vec2(-1e9);
}
float SPGiLocalSupport(ivec2 pixel,vec4 probe,float radius) {
    vec2 delta=abs(SPGiProbePixel(probe)-vec2(pixel))/max(float(spGrid.w)*radius,1.0);
    float support=max(0.0,1.0-max(delta.x,delta.y));
    return support*support*(3.0-2.0*support);
}
// The coarse estimate must remain a partition of its geometric corner weights.
// Restricting each corner with a query-local square makes the coarse signal
// itself alternate with the probe lattice before fine lighting is even added.
float SPGiUniformWeight(ivec2 pixel,vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    return SPGiRefinementEnabled()?SPGiContinuousPlaneWeight(p,n,depth,probe,normalDepth):
        SPGiPlaneWeight(p,n,depth,probe,normalDepth);
}
// Metadata stores detection confidence at the vertex. Apply the support limit
// to that fixed vertex value, never to the farthest corner of a query stencil.
// Interpolating these values gives a continuous refinement-domain boundary.
float SPGiRefinementConfidence(float strength) {
    float fraction=clamp(spRefinement.z/max(spRefinement.y,.0001),.0001,1.0);
    float outer=1.0-fraction*fraction*(3.0-2.0*fraction);
    return smoothstep(outer,1.0,clamp(strength,0.0,1.0));
}
float SPGiAdaptiveWeight(ivec2 pixel,vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    float geometry=SPGiRefinementEnabled()?SPGiContinuousPlaneWeight(p,n,depth,probe,normalDepth):
        SPGiPlaneWeight(p,n,depth,probe,normalDepth);
    if(geometry==0)return 0;
    if(SPGiRefinementEnabled()&&probe.w>1.5)
        return geometry*SPGiLocalSupport(pixel,probe,spRefinement.z)*clamp(probe.w-2.0,0.0,1.0);
    vec2 delta=abs(SPGiProbePixel(probe)-vec2(pixel))/float(spGrid.w);
    return geometry*max(0.0,1.0-max(delta.x,delta.y));
}
vec4 SPGiAtlasPart(sampler2D uniformAtlas,sampler2D spawnedAtlas,int probe,int part) {
    int uniformSlots=spGrid.x*spGrid.y;
    bool uniformProbe=probe<uniformSlots;int slot=uniformProbe?probe:probe-uniformSlots;
    int columns=spGrid.x*(uniformProbe?1:spGrid.z);
    ivec2 at=ivec2(2*(slot%columns)+part,slot/columns);
    return uniformProbe?texelFetch(uniformAtlas,at,0):texelFetch(spawnedAtlas,at,0);
}
ivec2 SPGiFineLayout() {
    int columns=spGrid.z==4?2:4;
    return ivec2(columns,spGrid.z/columns);
}
ivec2 SPGiFineCount(ivec2 fineGrid) {
    return max((spScreen.xy*fineGrid+spGrid.w-1)/spGrid.w,ivec2(1));
}
int SPGiFineCenter(int coordinate,int axis,ivec2 fineGrid) {
    coordinate=clamp(coordinate,0,SPGiFineCount(fineGrid)[axis]-1);
    int tile=coordinate/fineGrid[axis],cell=coordinate%fineGrid[axis];
    int begin=tile*spGrid.w+cell*spGrid.w/fineGrid[axis];
    int end=min(tile*spGrid.w+(cell+1)*spGrid.w/fineGrid[axis],spScreen[axis]);
    // Identical to floor(((begin+end)-1)*.5+.5) in MarkShader.
    return (begin+end)/2;
}
ivec2 SPGiFineBase(ivec2 pixel,ivec2 fineGrid,out vec2 fraction) {
    ivec2 tile=pixel/spGrid.w;
    ivec2 cell=min((pixel-tile*spGrid.w)*fineGrid/spGrid.w,fineGrid-1);
    ivec2 left=tile*fineGrid+cell;
    for(int axis=0;axis<2;++axis) {
        if(pixel[axis]<SPGiFineCenter(left[axis],axis,fineGrid))--left[axis];
        left[axis]=clamp(left[axis],0,max(SPGiFineCount(fineGrid)[axis]-2,0));
        int a=SPGiFineCenter(left[axis],axis,fineGrid);
        int b=SPGiFineCenter(left[axis]+1,axis,fineGrid);
        fraction[axis]=clamp(float(pixel[axis]-a)/float(max(b-a,1)),0.0,1.0);
    }
    return left;
}
int SPGiFineProbe(ivec2 coordinate,ivec2 fineGrid) {
    coordinate=clamp(coordinate,ivec2(0),SPGiFineCount(fineGrid)-1);
    ivec2 tile=coordinate/fineGrid,cell=coordinate%fineGrid;
    return spGrid.x*spGrid.y+(tile.y*spGrid.x+tile.x)*spGrid.z+cell.y*fineGrid.x+cell.x;
}
// Stable shared fine-cell vertices replace coarse lighting independently, as
// UE's adaptive upsample substitutes geometrically usable individual corners.
// A missing corner leaves only its own bilinear mass on the coarse estimate:
// there is no four-quadrant gate, nearest-set topology, or minimum-support mask.
void SPGiReconstructionStencil(ivec2 pixel,vec3 p,vec3 n,float depth,
    sampler2D uniformAtlas,sampler2D spawnedAtlas,sampler2D irradianceAtlas,bool needLighting,
    out vec4 adaptiveLighting[4],
    out vec4 uniformWeights,out vec4 adaptiveWeights,out float uniformWeight) {
    ivec2 base=SPCornerBase(pixel);vec2 f=SPBilinearFractions(pixel,base);
    uniformWeights=vec4(0);adaptiveWeights=vec4(0);
    for(int c=0;c<4;++c) {
        adaptiveLighting[c]=vec4(0);
        ivec2 tile=SPCorner(base,c);int probe=tile.x+tile.y*spGrid.x;
        float bilinear=(c%2==0?1.0-f.x:f.x)*(c/2==0?1.0-f.y:f.y);
        vec4 pp=SPGiAtlasPart(uniformAtlas,spawnedAtlas,probe,0),nd=SPGiAtlasPart(uniformAtlas,spawnedAtlas,probe,1);
        float geometry=SPGiUniformWeight(pixel,p,n,depth,pp,nd);
        uniformWeights[c]=bilinear*geometry;
    }
    uniformWeight=dot(uniformWeights,vec4(1));
    // Four fixed fine corners are cheap to inspect. A coarse tile's metadata
    // cannot decide whether its interior contains allocated adaptive support.
    if(spOptions.z<.5)return;

    ivec2 fineGrid=SPGiFineLayout();vec2 fineFraction;
    ivec2 fineBase=SPGiFineBase(pixel,fineGrid,fineFraction);
    vec4 fineWeights=vec4(0);vec4 weightedLighting[4];
    for(int c=0;c<4;++c) {
        weightedLighting[c]=vec4(0);
        ivec2 coordinate=fineBase+ivec2(c%2,c/2);
        int probe=SPGiFineProbe(coordinate,fineGrid);
        vec4 pp=SPGiAtlasPart(uniformAtlas,spawnedAtlas,probe,0);
        // Coverage repair and intersection refinement share the same fixed
        // subcell identity. A coverage-priority node must not punch a hole into
        // the fine field merely because its allocation reason changed.
        if(pp.w<.5)continue;
        vec4 nd=SPGiAtlasPart(uniformAtlas,spawnedAtlas,probe,1);
        float geometry=SPGiContinuousPlaneWeight(p,n,depth,pp,nd);
        if(geometry<=0)continue;
        float bilinear=(c%2==0?1.0-fineFraction.x:fineFraction.x)*
            (c/2==0?1.0-fineFraction.y:fineFraction.y);
        float reliability=pp.w>2.0?SPGiRefinementConfidence(pp.w-2.0):1.0;
        fineWeights[c]=bilinear*geometry*reliability;
        if(needLighting&&fineWeights[c]>0)
            weightedLighting[c]=texelFetch(irradianceAtlas,ivec2(probe%spGrid.x,probe/spGrid.x),0);
    }
    float fineMass=clamp(dot(fineWeights,vec4(1)),0.0,1.0);
    uniformWeights*=1.0-fineMass;
    // On a covered plane this preserves the exact coarse confidence and the
    // bilinear partition. The small floor only lets valid fine support repair
    // an absent coarse estimate; it never changes surface compatibility.
    adaptiveWeights=fineWeights*max(uniformWeight,.01);
    for(int c=0;c<4;++c)weightedLighting[c]*=adaptiveWeights[c];

    float reconstructedMass=dot(uniformWeights+adaptiveWeights,vec4(1));
    float fallbackBlend=1.0-smoothstep(0.0,.01,reconstructedMass);
    if(fallbackBlend>0) {
        // Missing geometric coverage is repaired with continuous kernel mass.
        // Four buckets are only storage: every compatible candidate contributes
        // before normalization, so no top-four winner changes can make seams.
        vec4 fallbackWeights=vec4(0);vec4 fallbackLighting[4];
        for(int c=0;c<4;++c)fallbackLighting[c]=vec4(0);
        ivec2 receiverTile=pixel/spGrid.w;
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
            ivec2 tile=receiverTile+ivec2(x,y);
            if(any(lessThan(tile,ivec2(0)))||any(greaterThanEqual(tile,spGrid.xy)))continue;
            for(int sampleIndex=0;sampleIndex<16;++sampleIndex) {
                if(sampleIndex>=spGrid.z)break;
                int probe=spGrid.x*spGrid.y+(tile.y*spGrid.x+tile.x)*spGrid.z+sampleIndex;
                vec4 pp=SPGiAtlasPart(uniformAtlas,spawnedAtlas,probe,0);if(pp.w<.5)continue;
                vec2 delta=abs(SPGiProbePixel(pp)-vec2(pixel))/float(spGrid.w);
                float support=max(0.0,1.0-max(delta.x,delta.y));if(support<=0)continue;
                vec4 nd=SPGiAtlasPart(uniformAtlas,spawnedAtlas,probe,1);
                float geometry=SPGiContinuousPlaneWeight(p,n,depth,pp,nd);if(geometry<=0)continue;
                float weight=geometry*support;int bucket=sampleIndex%4;
                fallbackWeights[bucket]+=weight;
                if(needLighting)fallbackLighting[bucket]+=
                    texelFetch(irradianceAtlas,ivec2(probe%spGrid.x,probe/spGrid.x),0)*weight;
            }
        }
        float total=dot(fallbackWeights,vec4(1));
        float scale=.01*fallbackBlend/max(total,.01);
        adaptiveWeights+=fallbackWeights*scale;
        for(int c=0;c<4;++c)weightedLighting[c]+=fallbackLighting[c]*scale;
    }
    for(int c=0;c<4;++c)if(adaptiveWeights[c]>0)
        adaptiveLighting[c]=weightedLighting[c]/adaptiveWeights[c];
}

)GLSL";}
inline std::string Common() { return std::string("#version 450 core\n")+Constants()+SamplingGLSL()+R"GLSL(
in vec2 vUV;
layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D spInput;
layout(binding=1) uniform sampler2D spNormals;
layout(binding=2) uniform sampler2D spDepth;
layout(binding=3) uniform sampler2D spGeometry;
layout(binding=4) uniform sampler2D spUniform;
layout(binding=5) uniform sampler2D spMarked;
layout(binding=6) uniform sampler2D spSpawned;
const float SP_MIN_WEIGHT=.01;
bool SPFinite(vec3 p){return !any(isnan(p))&&!any(isinf(p));}
vec4 SPPosition(sampler2D atlas,ivec2 slot){return texelFetch(atlas,ivec2(slot.x*2,slot.y),0);}
vec4 SPNormalDepth(sampler2D atlas,ivec2 slot){return texelFetch(atlas,ivec2(slot.x*2+1,slot.y),0);}
bool SPSurface(ivec2 pixel,out vec3 p,out vec3 n,out float depth) {
    p=vec3(0);n=vec3(0);depth=0;
    if(any(lessThan(pixel,ivec2(0)))||any(greaterThanEqual(pixel,spScreen.xy)))return false;
    vec4 geometry=texelFetch(spGeometry,pixel,0),normal=texelFetch(spNormals,pixel,0);
    if(geometry.w<.5||normal.w<.5||!SPFinite(geometry.xyz)||!SPFinite(normal.xyz)||dot(normal.xyz,normal.xyz)<.5)return false;
    p=(spInverseView*vec4(geometry.xyz,1)).xyz;n=normalize(normal.xyz);depth=max(abs(geometry.z),.0001);
    return SPFinite(p);
}
float SPPlaneWeight(vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    if((spTracing.w&2)!=0)return SPGiPlaneWeight(p,n,depth,probe,normalDepth);
    if(probe.w<.5)return 0;
    return exp2(log2(SP_MIN_WEIGHT)*abs(dot(probe.xyz-p,n))/max(depth,.0001)/spOptions.x);
}
vec4 SPUniformWeights(ivec2 pixel,vec3 p,vec3 n,float depth) {
    ivec2 base=SPCornerBase(pixel);vec2 f=SPBilinearFractions(pixel,base);vec4 weights=vec4(0);
    for(int c=0;c<4;++c){float b=(c%2==0?1-f.x:f.x)*(c/2==0?1-f.y:f.y);
        ivec2 corner=SPCorner(base,c);
        vec4 nd=(spTracing.w&2)!=0?SPNormalDepth(spUniform,corner):vec4(0);
        weights[c]=b*(SPGiRefinementEnabled()?SPGiUniformWeight(pixel,p,n,depth,SPPosition(spUniform,corner),nd):
            SPPlaneWeight(p,n,depth,SPPosition(spUniform,corner),nd));}
    return weights;
}
float SPUniformCoverage(ivec2 pixel,vec3 p,vec3 n,float depth){return dot(SPUniformWeights(pixel,p,n,depth),vec4(1));}
float SPAdaptiveWeight(ivec2 pixel,vec3 p,vec3 n,float depth,ivec2 probePixel,vec4 probe,vec4 normalDepth) {
    vec2 delta=abs(vec2(probePixel-pixel));
    float support=(spTracing.w&2)!=0?max(delta.x,delta.y):min(delta.x,delta.y);
    return SPPlaneWeight(p,n,depth,probe,normalDepth)*(1-clamp(support/float(spGrid.w),0,1));
}
float SPGiWorldPixelFootprint(vec3 p,vec3 n) {
    vec4 clip=spViewProjection*vec4(p,1);
    if(clip.w<=0)return 0;
    vec4 x=spInverseView*(spInverseProjection*(clip+vec4(2.0*clip.w/float(spScreen.x),0,0,0)));
    vec4 y=spInverseView*(spInverseProjection*(clip+vec4(0,2.0*clip.w/float(spScreen.y),0,0)));
    if(abs(x.w)<.000001||abs(y.w)<.000001)return 0;
    float footprint=max(length(x.xyz/x.w-p),length(y.xyz/y.w-p));
    vec3 toCamera=spInverseView[3].xyz-p;
    float facing=abs(dot(n,normalize(toCamera)));
    // A grazing foreground silhouette must not acquire an unbounded footprint
    // and masquerade as a distant wall junction. Missing such grazing junction
    // refinement is conservative; geometric coverage repair remains available.
    return footprint/max(facing,.25);
}
float SPGiIntersectionStrength(ivec2 pixel,vec3 p,vec3 n,float depth) {
    if(!SPGiRefinementEnabled())return 0;
    int radius=max(1,int(ceil(spRefinement.y*float(spGrid.w))));
    const ivec2 directions[8]=ivec2[8](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1),
        ivec2(-1,-1),ivec2(1,-1),ivec2(-1,1),ivec2(1,1));
    float strength=0,worldRadius=SPGiWorldPixelFootprint(p,n)*float(radius+2);
    for(int tap=0;tap<8;++tap) {
        vec3 other,otherNormal;float otherDepth;
        if(!SPSurface(pixel+directions[tap]*radius,other,otherNormal,otherDepth))continue;
        vec3 delta=other-p;
        float relativePlane=max(abs(dot(delta,n)),abs(dot(delta,otherNormal)))/max(min(depth,otherDepth),.0001);
        float agreement=dot(n,otherNormal);
        if(agreement>=spRefinement.w&&relativePlane<=spOptions.x)continue;
        vec3 tangentNormal=otherNormal-n*agreement;
        float denominator=dot(tangentNormal,tangentNormal);if(denominator<.0001)continue;
        vec3 intersection=p-tangentNormal*(dot(p-other,otherNormal)/denominator);
        vec3 otherIntersection=other-(n-otherNormal*agreement)*(dot(other-p,n)/denominator);
        float otherRadius=SPGiWorldPixelFootprint(other,otherNormal)*float(radius+2);
        // Intersect finite observed patches, not just their infinite planes.
        // A separated foreground/back wall depth edge belongs to the coverage
        // allocator, even if its tangent planes happen to intersect on screen.
        if(length(intersection-p)>worldRadius*1.25||
            length(otherIntersection-other)>otherRadius*1.25||
            length(delta)>1.5*(worldRadius+otherRadius))continue;
        vec4 clip=spViewProjection*vec4(intersection,1);if(clip.w<=0)continue;
        vec2 intersectionPixel=(clip.xy/clip.w*.5+.5)*vec2(spScreen.xy)-.5;
        float distance=length(intersectionPixel-vec2(pixel));
        strength=max(strength,1.0-smoothstep(0.0,float(radius),distance));
    }
    return strength;
}

)GLSL"; }
inline std::string UniformShader(){return Common()+R"GLSL(
void main(){ivec2 texel=ivec2(gl_FragCoord.xy),tile=ivec2(texel.x/2,texel.y);
    ivec2 pixel=SPUniformPixel(tile);vec3 p,n;float depth;bool valid=SPSurface(pixel,p,n,depth);
    if(texel.x%2!=0){outColor=valid?vec4(n,depth):vec4(0);return;}
    float state=valid?1.0+SPGiIntersectionStrength(pixel,p,n,depth):1.0;
    outColor=!valid?vec4(0):vec4(p,state);}
)GLSL";}
inline std::string MarkShader(){return Common()+R"GLSL(
void main(){ivec2 texel=ivec2(gl_FragCoord.xy),slot=ivec2(texel.x/2,texel.y);
    ivec2 tile=ivec2(slot.x/spGrid.z,slot.y);int sampleIndex=slot.x%spGrid.z;
    if((spTracing.w&2)!=0) {
        if(spOptions.z<.5&&spScreen.w!=1){outColor=vec4(0);return;}
        // A random tap can miss a thin plate or a silhouette for every phase.
        // Search disjoint screen subcells instead; all candidates together visit
        // each receiver once, independent of spacing. This is placement work,
        // not more rays or a full-resolution lighting pass.
        int columns=spGrid.z==4?2:4,rows=spGrid.z/columns;
        ivec2 cell=ivec2(sampleIndex%columns,sampleIndex/columns);
        ivec2 begin=tile*spGrid.w+cell*spGrid.w/ivec2(columns,rows);
        ivec2 end=min(tile*spGrid.w+(cell+1)*spGrid.w/ivec2(columns,rows),spScreen.xy);
        vec2 center=(vec2(begin+end)-1.0)*.5;
        float best=1e30,bestCoverage=1;vec3 bestP=vec3(0),bestN=vec3(0);float bestDepth=0;
        for(int y=begin.y;y<end.y;++y)for(int x=begin.x;x<end.x;++x) {
            ivec2 pixel=ivec2(x,y);vec3 p,n;float depth;if(!SPSurface(pixel,p,n,depth))continue;
            float coverage=SPUniformCoverage(pixel,p,n,depth);
            // Favor the middle of equally uncovered pixels so adjacent cells
            // do not collapse onto their shared discontinuity.
            float score=coverage+1e-7*dot(vec2(pixel)-center,vec2(pixel)-center);
            if(score<best){best=score;bestCoverage=coverage;bestP=p;bestN=n;bestDepth=depth;}
        }
        if(bestDepth<=0){outColor=vec4(0);return;}
        // Coverage holes retain priority, including thin plates and curved
        // silhouettes. Remaining slots refine geometric lighting gradients.
        if(bestCoverage>=SP_MIN_WEIGHT&&SPGiRefinementEnabled()) {
            // Refinement uses a stable subcell center, not a winner selected
            // from every receiver. Coverage holes above still search all taps.
            ivec2 centerPixel=ivec2(floor(center+.5));vec3 refinedP,refinedN;float refinedDepth;
            if(SPSurface(centerPixel,refinedP,refinedN,refinedDepth)) {
                float strength=SPGiIntersectionStrength(centerPixel,refinedP,refinedN,refinedDepth);
                if(SPGiRefinementConfidence(strength)>.0001) {
                    outColor=texel.x%2==0?vec4(refinedP,3.0+strength):vec4(refinedN,refinedDepth);return;
                }
            }
        }
        outColor=texel.x%2==0?vec4(bestP,bestCoverage<SP_MIN_WEIGHT?2.0:1.0):vec4(bestN,bestDepth);
        return;
    }
    ivec2 pixel=SPCandidatePixel(tile,sampleIndex);vec3 p,n;float depth;
    if(!SPSurface(pixel,p,n,depth)){outColor=vec4(0);return;}
    float state=SPUniformCoverage(pixel,p,n,depth)<SP_MIN_WEIGHT?2.0:1.0;
    outColor=texel.x%2==0?vec4(p,state):vec4(n,depth);}
)GLSL";}
inline std::string SpawnShader(){return Common()+R"GLSL(
void main(){ivec2 texel=ivec2(gl_FragCoord.xy),slot=ivec2(texel.x/2,texel.y);
    ivec2 tile=ivec2(slot.x/spGrid.z,slot.y);int sampleIndex=slot.x%spGrid.z;
    vec4 p=SPPosition(spMarked,slot),nd=SPNormalDepth(spMarked,slot);
    if(spOptions.z<.5||p.w<1.5){outColor=vec4(0);return;}
    if((spTracing.w&2)!=0) {
        // A merely marked neighbor is not guaranteed to survive its own
        // suppression test. Keeping one winner per disjoint subcell avoids
        // that transitive suppression hole without a serial GPU dependency.
        outColor=texel.x%2==0?vec4(p.xyz,p.w>2.5?p.w-1.0:1.0):nd;return;
    }
    ivec2 pixel=SPCandidatePixel(tile,sampleIndex),base=SPCornerBase(pixel);
    vec4 weights=vec4(0);bool place=true;
    // UE checks lower-index marked candidates, rather than recursively reading
    // concurrently written spawn decisions. This keeps the pass race-free.
    for(int c=0;c<4&&place;++c){ivec2 neighbor=SPCorner(base,c);
        for(int previous=0;previous<16;++previous){if(previous>=sampleIndex)break;
            ivec2 otherSlot=SPSlot(neighbor,previous);vec4 other=SPPosition(spMarked,otherSlot);
            if(other.w<1.5)continue;
            vec4 otherNormal=(spTracing.w&2)!=0?SPNormalDepth(spMarked,otherSlot):vec4(0);
            weights[c]=max(weights[c],SPAdaptiveWeight(pixel,p.xyz,nd.xyz,nd.w,SPCandidatePixel(neighbor,previous),other,otherNormal));
            if(dot(weights,vec4(1))>=SP_MIN_WEIGHT){place=false;break;}}
    }
    outColor=!place?vec4(0):(texel.x%2==0?vec4(p.xyz,1):nd);}
)GLSL";}
inline std::string CoverageShader(){return Common()+R"GLSL(
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);vec3 p,n;float depth;
    vec4 scene=texelFetch(spInput,pixel,0);
    if(!SPSurface(pixel,p,n,depth)){outColor=scene;return;}
    vec4 weights=SPUniformWeights(pixel,p,n,depth);float uniformWeight=dot(weights,vec4(1));
    if(SPGiRefinementEnabled()) {
        vec4 adaptiveLighting[4];vec4 uw,aw;float unrefined;
        SPGiReconstructionStencil(pixel,p,n,depth,spUniform,spSpawned,spUniform,false,adaptiveLighting,uw,aw,unrefined);
        float total=dot(uw+aw,vec4(1));
        vec3 tint=total<.01?vec3(.75,.03,.04):
            (dot(aw,vec4(1))>.0001?vec3(1,.30,.03):vec3(.04,.22,.60));
        outColor=vec4(mix(scene.rgb,tint,.65),scene.a);return;
    }
    if((spTracing.w&2)!=0) {
        float adaptive=0;
        if(spOptions.z>.5&&uniformWeight<SP_MIN_WEIGHT) {
            ivec2 tile=pixel/spGrid.w;
            for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
                ivec2 neighbor=tile+ivec2(x,y);
                if(any(lessThan(neighbor,ivec2(0)))||any(greaterThanEqual(neighbor,spGrid.xy)))continue;
                for(int s=0;s<16;++s){if(s>=spGrid.z)break;ivec2 slot=SPSlot(neighbor,s);
                    adaptive=max(adaptive,SPGiAdaptiveWeight(pixel,p,n,depth,SPPosition(spSpawned,slot),SPNormalDepth(spSpawned,slot)));}
            }
        }
        vec3 tint=uniformWeight>=SP_MIN_WEIGHT?vec3(.04,.22,.60):
            (adaptive>=SP_MIN_WEIGHT?vec3(1,.30,.03):vec3(.75,.03,.04));
        outColor=vec4(mix(scene.rgb,tint,.65),scene.a);return;
    }
    ivec2 base=SPCornerBase(pixel);
    if(spOptions.z>.5)for(int c=0;c<4;++c){if(weights[c]>=SP_MIN_WEIGHT)continue;ivec2 neighbor=SPCorner(base,c);
        for(int s=0;s<16;++s){if(s>=spGrid.z)break;ivec2 slot=SPSlot(neighbor,s);vec4 probe=SPPosition(spSpawned,slot);
            vec4 nd=(spTracing.w&2)!=0?SPNormalDepth(spSpawned,slot):vec4(0);
            weights[c]=max(weights[c],SPAdaptiveWeight(pixel,p,n,depth,SPCandidatePixel(neighbor,s),probe,nd));}}
    vec3 tint=uniformWeight>=SP_MIN_WEIGHT?vec3(.04,.22,.60):
        (dot(weights,vec4(1))>=SP_MIN_WEIGHT?vec3(1,.30,.03):vec3(.75,.03,.04));
    outColor=vec4(mix(scene.rgb,tint,.65),scene.a);}
)GLSL";}
inline std::string MarkerVertexShader(){return std::string("#version 450 core\n")+Constants()+R"GLSL(
layout(location=0) in vec2 position;layout(location=1) in vec2 uv;
layout(binding=0) uniform sampler2D spAtlas;
out vec2 markerPosition;flat out vec3 markerColor;
flat out vec2 markerSphere;flat out vec4 markerDepthProjection;
flat out int markerProbe;
void main(){ivec2 entries=textureSize(spAtlas,0)/ivec2(2,1);
    ivec2 slot=ivec2(gl_InstanceID%entries.x,gl_InstanceID/entries.x);
    vec4 p=texelFetch(spAtlas,ivec2(slot.x*2,slot.y),0),nd=texelFetch(spAtlas,ivec2(slot.x*2+1,slot.y),0);
    markerPosition=position;markerColor=spOptions.w<.5?vec3(.04,.34,1):vec3(1,.32,.03);
    markerProbe=gl_InstanceID+(spOptions.w>.5?spTraceAtlas.z:0);
    markerSphere=vec2(0);markerDepthProjection=vec4(0);
    if(spScreen.w==1&&spOptions.w>.5)markerColor=p.w>1.5?vec3(1,.20,.02):vec3(.06,.85,.24);
    if(p.w<.5){gl_Position=vec4(2,2,2,1);return;}
    // Display a sphere tangent to the stored surface position. Its curved front
    // depth keeps sloped surfaces from cutting a constant-depth disk in half.
    // The atlas position stays unchanged, including while placement is frozen.
    vec4 probeClip=spViewProjection*vec4(p.xyz,1);
    if(probeClip.w<=0){gl_Position=vec4(2,2,2,1);return;}
    float radiusWorld=2.0*probeClip.w*spOptions.y*abs(spInverseProjection[1][1])/float(spScreen.y);
    vec3 world=p.xyz+nd.xyz*radiusWorld;
    vec4 clip=spViewProjection*vec4(world,1);
    if(clip.w<=0){gl_Position=vec4(2,2,2,1);return;}
    markerSphere=vec2((spView*vec4(world,1)).z,radiusWorld);
    mat4 projection=spViewProjection*spInverseView;
    // Include the W terms as well, so perspective and orthographic cameras use
    // the same depth reconstruction without assuming clipW == -viewZ.
    markerDepthProjection=vec4(projection[2][2],projection[3][2],projection[2][3],projection[3][3]);
    clip.xy+=position*spOptions.y*2.0/vec2(spScreen.xy)*clip.w;gl_Position=clip;
}
)GLSL";}
inline std::string MarkerFragmentShader(const std::string& traceGLSL={}){return std::string("#version 450 core\n")+Constants()+traceGLSL+R"GLSL(
in vec2 markerPosition;flat in vec3 markerColor;
flat in vec2 markerSphere;flat in vec4 markerDepthProjection;
flat in int markerProbe;
layout(location=0) out vec4 outColor;
void main(){float radiusSquared=dot(markerPosition,markerPosition);if(radiusSquared>1)discard;
    float sphereFront=sqrt(max(0.0,1.0-radiusSquared));
    float viewZ=markerSphere.x+markerSphere.y*sphereFront;
    float clipZ=markerDepthProjection.x*viewZ+markerDepthProjection.y;
    float clipW=markerDepthProjection.z*viewZ+markerDepthProjection.w;if(clipW<=0)discard;
    gl_FragDepth=clamp((clipZ/clipW)*.5+.5,0.0,1.0);
    float shading=.4+.6*max(dot(normalize(vec3(markerPosition,sphereFront)),normalize(vec3(-.3,.5,.8))),0.0);
    vec3 color=markerColor*shading;
    if(spTracing.w!=0&&spScreen.w!=1) {
        vec3 worldNormal=normalize(mat3(spInverseView)*vec3(markerPosition,sphereFront));
        int rayCount=spTracing.x*spTracing.x,bestRay=0;float bestDot=-2;
        for(int ray=0;ray<256;++ray){if(ray>=rayCount)break;float score=dot(worldNormal,STReadRay(markerProbe,ray,3).xyz);
            if(score>bestDot){bestDot=score;bestRay=ray;}}
        int address=(markerProbe*rayCount+bestRay)*4;
        vec4 result=texelFetch(stRays,ivec2(address%spTraceAtlas.x,address/spTraceAtlas.x),0);
        color=STDiagnosticColor(result.rgb,result.a);
        if(markerProbe==spTraceDisplay.y&&radiusSquared>.78)color=vec3(1.5);
        else if((spTracing.w&2)!=0&&radiusSquared>.60)color=markerColor;
    }
    outColor=vec4(color,1);}
)GLSL";}
} // namespace Render::ScreenProbePlacementDetail
