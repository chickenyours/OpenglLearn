#pragma once
#include <string>

namespace Render::SurfaceCacheDetail {
// UE 5.7.1 SurfaceCache/LumenSurfaceCacheSampling.ush:258-309,415-419,
// 457-475,585-591: sum projection lighting with normal^2 * valid bilinear
// coverage, then normalize. This primary-direct adapter retains the project's
// original owner, nearest-anchor, layer, depth and lighting-validity tests.
// GI lookup and ray launch consumers keep their original single-card helper.
inline std::string UeLumenCardDirectSampleGLSL() {return R"GLSL(
float ueCardVisibility=0.0;
bool ueCardVisibilityEncoded=false;
bool UECardHasVisibility(vec4 lighting){return !isnan(lighting.w)&&!isinf(lighting.w)&&lighting.w>=2.0&&lighting.w<=3.0;}
float UECardDecodeVisibility(vec4 lighting){return all(equal(lighting.rgb,vec3(0)))?0.0:clamp(lighting.w-2.0,0.0,1.0);}
bool UECardDirectProjection(vec3 p,vec3 n,vec3 local,int card,int firstTexel,int resolution,
    vec4 low,vec4 high,vec4 facing,int knownAnchor,vec4 knownPosition,vec4 knownNormal,vec4 knownLighting,
    out vec3 radiance,out float coverage,out float cardVisibility,out bool visibilityEncoded) {
    radiance=vec3(0);coverage=0;cardVisibility=0;visibilityEncoded=false;
    int axis=card%6/2,u=(axis+1)%3,v=(axis+2)%3;
    vec2 extent=max(vec2(high[u]-low[u],high[v]-low[v]),vec2(1e-8));
    vec2 uv=(vec2(local[u],local[v])-vec2(low[u],low[v]))/extent;
    if(any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))return false;
    vec2 at=uv*float(resolution)-.5;ivec2 base=ivec2(floor(at));vec2 fraction=fract(at);
    float tolerance=max(facing.w,.00001);
    int anchor=knownAnchor;vec4 anchorPosition=knownPosition,anchorNormal=knownNormal;
    float best=1e30;vec4 positions[4],normals[4];int indices[4];
    // Each secondary projection discovers its own nearest valid surface once.
    // Carry these four immutable taps into filtering rather than fetching them
    // again through another lookup/reconstruction chain.
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        int tap=y*2+x;ivec2 cell=clamp(base+ivec2(x,y),ivec2(0),ivec2(resolution-1));
        int index=firstTexel+cell.y*resolution+cell.x;indices[tap]=index;
        positions[tap]=vec4(0);normals[tap]=vec4(0);
        float bilinear=(x==0?1-fraction.x:fraction.x)*(y==0?1-fraction.y:fraction.y);
        if(knownAnchor>=0&&bilinear<=.000001)continue;
        positions[tap]=SCAttribute(index);
        if(positions[tap].w>=.5)normals[tap]=SCAttribute(scAtlas.y+index);
        if(knownAnchor>=0)continue;
        vec4 position=positions[tap],normal=normals[tap];
        if(position.w<.5||dot(n,normal.xyz)<.6)continue;
        vec3 delta=p-position.xyz;float depth=abs(dot(delta,facing.xyz)),distance=dot(delta,delta);
        if(depth>facing.w||distance>facing.w*facing.w*6.25)continue;
        if(distance<best){best=distance;anchor=index;anchorPosition=position;anchorNormal=normal;}
    }
    if(anchor<0||int(anchorNormal.w+.5)-1!=card)return false;
    if(knownAnchor<0) {
        // A different projection may capture a parallel hidden layer of this
        // same object. Certify its anchor against the original covered layer,
        // using the existing dual-plane/curvature slack without a new bias.
        vec3 separation=anchorPosition.xyz-knownPosition.xyz;
        float normalDifference=sqrt(max(0.0,2.0*(1-clamp(dot(anchorNormal.xyz,knownNormal.xyz),-1.0,1.0))));
        float layerSlack=max(.0002,.05*tolerance)+.75*length(separation)*normalDifference;
        if(max(abs(dot(separation,knownNormal.xyz)),abs(dot(separation,anchorNormal.xyz)))>layerSlack)return false;
    }
    vec4 anchorLighting=knownAnchor>=0?knownLighting:texelFetch(wgTransport,ivec2(anchor%scAtlas.x,anchor/scAtlas.x),0);
    if(anchorLighting.w<.5||any(isnan(anchorLighting.rgb))||any(isinf(anchorLighting.rgb)))return false;
    bool allEncoded=UECardHasVisibility(anchorLighting);float visibilitySum=0;
    float weightSum=0;vec3 sum=vec3(0);bool missingLighting=false;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        float bilinear=(x==0?1-fraction.x:fraction.x)*(y==0?1-fraction.y:fraction.y);
        if(bilinear<=.000001)continue;
        int tap=y*2+x,index=indices[tap];vec4 position=positions[tap],normal=normals[tap];
        if(position.w<.5||dot(n,normal.xyz)<.6||int(normal.w+.5)-1!=card)continue;
        vec3 delta=p-position.xyz;float depth=abs(dot(delta,facing.xyz));
        if(depth>=tolerance||dot(delta,delta)>tolerance*tolerance*6.25)continue;
        vec3 fromAnchor=position.xyz-anchorPosition.xyz;
        float normalDifference=sqrt(max(0.0,2.0*(1-clamp(dot(normal.xyz,anchorNormal.xyz),-1.0,1.0))));
        float layerSlack=max(.0002,.05*tolerance)+.75*length(fromAnchor)*normalDifference;
        if(abs(dot(fromAnchor,anchorNormal.xyz))>layerSlack||
            max(abs(dot(delta,n)),abs(dot(delta,normal.xyz)))>tolerance)continue;
        if(knownAnchor<0) {
            // Layer compatibility is not transitive across two anchors. Each
            // secondary tap must also belong to the original covered layer.
            vec3 fromPrimary=position.xyz-knownPosition.xyz;
            float primaryNormalDifference=sqrt(max(0.0,2.0*(1-clamp(dot(normal.xyz,knownNormal.xyz),-1.0,1.0))));
            float primaryLayerSlack=max(.0002,.05*tolerance)+.75*length(fromPrimary)*primaryNormalDifference;
            if(max(abs(dot(fromPrimary,knownNormal.xyz)),abs(dot(fromPrimary,normal.xyz)))>primaryLayerSlack)continue;
        }
        float visibility=1-clamp((depth-.8*tolerance)/(.2*tolerance),0.0,1.0);
        float weight=bilinear*visibility;if(weight<=.000001)continue;
        vec4 lighting=texelFetch(wgTransport,ivec2(index%scAtlas.x,index/scAtlas.x),0);
        // Valid black retains its full weight. An unavailable eligible tap
        // rejects this projection; it never receives a white replacement.
        if(lighting.w<.5||any(isnan(lighting.rgb))||any(isinf(lighting.rgb))) {missingLighting=true;continue;}
        allEncoded=allEncoded&&UECardHasVisibility(lighting);
        visibilitySum+=UECardDecodeVisibility(lighting)*weight;
        sum+=lighting.rgb*weight;weightSum+=weight;
    }
    if(missingLighting||weightSum<=.000001)return false;
    cardVisibility=visibilitySum/weightSum;visibilityEncoded=allEncoded;
    radiance=sum/weightSum;coverage=clamp(weightSum,0.0,1.0);return true;
}
bool UESampleDirectCards(vec3 p,vec3 n,int object,out vec3 radiance,out float coverage) {
    radiance=vec3(0);coverage=0;ueCardVisibility=0;ueCardVisibilityEncoded=false;
    int anchor=wgAnchor;if(anchor<0)return false;
    // Keep the original covered anchor and lighting-availability gate before
    // consulting any other projection. This adapter cannot create coverage.
    vec4 anchorLighting=texelFetch(wgTransport,ivec2(anchor%scAtlas.x,anchor/scAtlas.x),0);
    if(anchorLighting.w<.5||any(isnan(anchorLighting.rgb))||any(isinf(anchorLighting.rgb)))return false;
    if((int(scParameters.w+.5)&2)==0) {radiance=anchorLighting.rgb;coverage=1;
        ueCardVisibility=UECardDecodeVisibility(anchorLighting);ueCardVisibilityEncoded=UECardHasVisibility(anchorLighting);return true;}
    vec4 anchorNormal=SCAttribute(scAtlas.y+anchor),anchorPosition=SCAttribute(anchor);
    int anchorCard=int(anchorNormal.w+.5)-1;
    if(anchorCard<0||anchorCard>=scAtlas.w||(object>=0&&anchorCard/6!=object))return false;
    int group=anchorCard-anchorCard%6;
    vec4 low=SCMeta(group,4),high=SCMeta(group,5);
    int resolution=int(SCMeta(group,9).w),pageSize=resolution*resolution;
    mat4 inverseTransform=mat4(SCMeta(group,0),SCMeta(group,1),SCMeta(group,2),SCMeta(group,3));
    vec3 local=(inverseTransform*vec4(p,1)).xyz;
    // Metadata stores normalized inverse-transpose card axes. Under rotation
    // and nonuniform scale these are precisely the rotated unit frame used by
    // UE MeshCardsSpaceNormal; never transform n with the position inverse.
    vec4 cx=SCMeta(group,6),cy=SCMeta(group+2,6),cz=SCMeta(group+4,6);
    vec3 alignment=vec3(dot(n,cx.xyz),dot(n,cy.xyz),dot(n,cz.xyz));
    vec3 axisWeights=alignment*alignment;
    int anchorAxis=anchorCard%6/2;
    vec4 anchorFacing=anchorAxis==0?cx:(anchorAxis==1?cy:cz);
    if((anchorCard&1)!=0)anchorFacing.xyz=-anchorFacing.xyz;
    vec3 primary;float primaryCoverage,primaryVisibility;bool primaryEncoded;
    if(!UECardDirectProjection(p,n,local,anchorCard,int(high.w)+(anchorCard-group)*pageSize,resolution,
        low,high,anchorFacing,anchor,anchorPosition,anchorNormal,anchorLighting,primary,primaryCoverage,primaryVisibility,primaryEncoded))return false;
    float weightSum=axisWeights[anchorAxis]*primaryCoverage;
    float visibilitySum=primaryVisibility*weightSum;bool allEncoded=primaryEncoded;
    vec3 sum=primary*weightSum;int validCards=1;
    for(int axis=0;axis<3;++axis) {
        if(axis==anchorAxis||axisWeights[axis]<=0.0)continue;
        int side=axis*2+(alignment[axis]<0.0?1:0),card=group+side;
        vec4 facing=axis==0?cx:(axis==1?cy:cz);if((side&1)!=0)facing.xyz=-facing.xyz;
        vec3 value;float cardCoverage,cardVisibility;bool cardEncoded;
        if(!UECardDirectProjection(p,n,local,card,int(high.w)+side*pageSize,resolution,
            low,high,facing,-1,anchorPosition,anchorNormal,vec4(0),value,cardCoverage,cardVisibility,cardEncoded))continue;
        float weight=axisWeights[axis]*cardCoverage;
        visibilitySum+=cardVisibility*weight;allEncoded=allEncoded&&cardEncoded;
        sum+=value*weight;weightSum+=weight;++validCards;
    }
    // Preserve exact original single-card arithmetic for planar surfaces and
    // for secondary projections rejected by layer/depth/availability gates.
    if(validCards==1){radiance=primary;coverage=primaryCoverage;
        ueCardVisibility=primaryVisibility;ueCardVisibilityEncoded=allEncoded;return true;}
    if(weightSum<=0.0)return false;
    ueCardVisibility=visibilitySum/weightSum;ueCardVisibilityEncoded=allEncoded;
    radiance=sum/weightSum;coverage=clamp(weightSum,0.0,1.0);return true;
}
)GLSL";}
} // namespace Render::SurfaceCacheDetail
