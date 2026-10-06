#include "Render/Public/Pipeline/lumen_gi.h"
#include "Render/Private/Pipeline/lumen_gi_shaders.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace Render;
void Check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
namespace {
unsigned FindTriangle(const LumenGiScene& scene,const ProbeTriangle& triangle) {
    const auto& geometry=*scene.Geometry();const auto& pixels=geometry.Pixels();const unsigned first=geometry.NodeCount()*2;
    for(unsigned i=0;i<geometry.TriangleCount();++i)
        if(glm::vec3(pixels[first+i*5])==triangle.a&&glm::vec3(pixels[first+i*5+1])==triangle.b&&
           glm::vec3(pixels[first+i*5+2])==triangle.c)return i;
    throw std::runtime_error("Diagnostic triangle missing from reordered geometry");
}
std::vector<glm::vec4> PartialNeighbors(const LumenGiScene& scene,unsigned triangle) {
    const auto& attributes=scene.Attributes();const auto metadata=attributes[scene.AdjacencyOffset()+triangle];
    if(metadata.w==0)return {};
    const auto first=unsigned(metadata.w);
    Check(first>=scene.PartialAdjacencyOffset()&&first<attributes.size(),"Partial adjacency header escaped its appended region");
    const auto count=unsigned(attributes[first].x);
    Check(count>0&&count<=MaxLumenPartialNeighbours&&first+count<attributes.size(),"Partial adjacency exceeded its bounded list ABI");
    std::vector<glm::vec4> entries(attributes.begin()+first+1,attributes.begin()+first+1+count);
    float edge=-1,last=-1;
    for(const auto& entry:entries) {
        Check(entry.x>=0&&entry.x<scene.Geometry()->TriangleCount()&&entry.y>=0&&entry.y<3&&
              entry.z>=0&&entry.w<=1&&entry.z<entry.w,"Malformed partial-edge neighbor interval");
        Check(entry.y>edge||(entry.y==edge&&entry.z>=last),"Partial-edge neighbors are not sorted by edge/t0");
        edge=entry.y;last=entry.z;
    }
    return entries;
}
void CheckPartialRoofEdges() {
    // One long roof patch meets two shorter coplanar patches. This real
    // triangle topology has both complete diagonals and T-junction seams.
    std::vector<ProbeTriangle> roof;
    const auto patch=[&](float x0,float z0,float x1,float z1) {
        roof.push_back({{x0,0,z0},{x0,0,z1},{x1,0,z0}});
        roof.push_back({{x1,0,z0},{x0,0,z1},{x1,0,z1}});
    };
    patch(-2,0,0,4);patch(0,0,2,2);patch(0,2,2,4);
    const auto scene=BuildLumenGiScene(roof,.5f);
    const unsigned longSide=FindTriangle(*scene,roof[1]),lower=FindTriangle(*scene,roof[2]),upper=FindTriangle(*scene,roof[4]);
    const auto entries=PartialNeighbors(*scene,longSide);
    Check(entries.size()==2&&entries[0]==glm::vec4(float(upper),2,0,.5f)&&entries[1]==glm::vec4(float(lower),2,.5f,1),
          "Long roof edge did not split into the two receiver-local T-junction intervals");
    for(const auto neighbor:{lower,upper}) {
        const auto reciprocal=PartialNeighbors(*scene,neighbor);
        Check(reciprocal.size()==1&&reciprocal[0]==glm::vec4(float(longSide),0,0,1),
              "Short roof edge lost its reciprocal full local interval");
    }
    const auto diagonal=scene->Attributes()[scene->AdjacencyOffset()+FindTriangle(*scene,roof[0])];
    Check(diagonal.y==float(longSide)&&diagonal.w==0,"Partial-edge support changed complete-edge adjacency ABI");
    Check(scene->Attributes()[scene->AdjacencyOffset()+longSide].z==-1,"T-junction was incorrectly encoded as one complete-edge neighbor");
    auto separated=roof;
    for(unsigned triangle:{2u,3u})for(auto* p:{&separated[triangle].a,&separated[triangle].b,&separated[triangle].c})p->x+=5;
    const auto moved=RefitLumenGiScene(*scene,separated);
    const auto remaining=PartialNeighbors(*moved,longSide);
    Check(remaining.size()==1&&remaining[0]==glm::vec4(float(upper),2,0,.5f),"Refit retained a moved-away partial edge");
    Check(PartialNeighbors(*scene,longSide).size()==2,"Refit mutated partial-edge metadata of an in-flight asset");
    const auto restored=RefitLumenGiScene(*moved,roof);
    Check(PartialNeighbors(*restored,longSide)==entries&&restored->SurfelCount()==scene->SurfelCount(),
          "Refit did not restore partial adjacency without changing the surface layout");
    auto differentMaterial=roof;differentMaterial[2].diffuseReflectance={.8f,.1f,.05f};differentMaterial[4].diffuseReflectance={.1f,.7f,.2f};
    const auto colored=RefitLumenGiScene(*scene,differentMaterial);
    Check(PartialNeighbors(*colored,longSide)==entries,"Incident light continuity incorrectly depended on receiver albedo");
    auto transformed=roof;
    for(auto& triangle:transformed)for(auto* p:{&triangle.a,&triangle.b,&triangle.c}) {
        const auto q=*p;*p=glm::vec3(q.x+2*q.z,2*q.x-q.z,q.x+q.z)+glm::vec3(17,7,-5);
    }
    const auto inclined=BuildLumenGiScene(transformed,.5f);
    const auto transformedLong=FindTriangle(*inclined,transformed[1]);
    Check(PartialNeighbors(*inclined,transformedLong).size()==2,"Translated/inclined coplanar roof edges lost T-junction adjacency");
    auto ambiguous=roof;ambiguous.push_back(roof[2]);
    const auto nonmanifold=BuildLumenGiScene(ambiguous,.5f);
    const auto safe=PartialNeighbors(*nonmanifold,FindTriangle(*nonmanifold,roof[1]));
    Check(safe.size()==1&&safe[0].x==FindTriangle(*nonmanifold,roof[4])&&safe[0].z==0&&safe[0].w==.5f,
          "Non-manifold partial interval blended an arbitrary overlapping roof surface");
    auto touching=roof;touching.push_back({{0,0,4},{0,0,6},{2,0,6}});
    const auto contact=BuildLumenGiScene(touching,.5f);
    Check(PartialNeighbors(*contact,FindTriangle(*contact,touching.back())).empty(),"Vertex-only contact became a partial edge");
    std::array<ProbeTriangle,2> coincident{roof[0],roof[0]};const auto duplicate=BuildLumenGiScene(coincident,.5f);
    for(unsigned triangle=0;triangle<2;++triangle)
        Check(duplicate->Attributes()[duplicate->AdjacencyOffset()+triangle]==glm::vec4(-1,-1,-1,0),
              "Same-side coincident faces acquired irradiance neighbors");
    std::vector<ProbeTriangle> crowded{{{0,0,0},{0,0,9},{2,0,0}}};
    for(unsigned i=0;i<9;++i)crowded.push_back({{0,0,float(i+1)},{0,0,float(i)},{-2,0,float(i)}});
    const auto bounded=BuildLumenGiScene(crowded,.5f);
    for(unsigned triangle=0;triangle<bounded->Geometry()->TriangleCount();++triangle)
        Check(PartialNeighbors(*bounded,triangle).empty(),"Over-budget adjacency retained asymmetric or unbounded neighbor lists");
}
}
int main() {
    try {
        const ProbeTriangle triangle{{0,0,0},{0,0,4},{4,0,0},glm::vec3(.5f),{2,1,0}};
        const auto scene=BuildLumenGiScene(std::span(&triangle,1),.5f);
        const auto defaultDense=BuildLumenGiScene(std::span(&triangle,1),.05f);
        const auto labDense=BuildLumenGiScene(std::span(&triangle,1),.05f,32);
        Check(defaultDense->Attributes()[0].y==16&&labDense->Attributes()[0].y==32&&labDense->SurfelCount()==1024,
              "Optional surface lattice cap changed default scenes or failed to refine the lab");
        Check(RefitLumenGiScene(*labDense,std::span(&triangle,1))->SurfelCount()==labDense->SurfelCount(),
              "Refit discarded the refined surface lattice");
        for(unsigned invalid:{0u,33u}) {
            bool rejected=false;try{BuildLumenGiScene(std::span(&triangle,1),.5f,invalid);}catch(const std::invalid_argument&){rejected=true;}
            Check(rejected,"Invalid surface lattice cap was accepted");
        }
        Check(scene->Geometry()->HasEmission()&&scene->SurfelCount()>1,"Surface Cache lost material/large-surface coverage");
        Check(scene->EmissiveTriangleCount()==1,"Standalone emission was omitted from next-event sampling");
        const auto emitter=scene->Attributes()[1+scene->SurfelCount()*2];
        Check(emitter.x==0&&emitter.y==1&&emitter.z==1,"Single emitter importance/PDF is not normalized");
        const auto& data=scene->Attributes();Check(data.size()%LumenSurfaceWidth==0,"Attribute atlas is not padded");
        const auto meta=data[0];const auto r=unsigned(meta.y);Check(r*r==scene->SurfelCount(),"Barycentric lattice has missing texels");
        Check(std::abs(meta.z-std::sqrt(8.f)/r)<1e-6f,"Precomputed incident cell footprint changed world filtering scale");
        Check(std::abs(meta.w-std::sqrt(32.f)/r)<1e-6f,"Skew triangle filter footprint does not cover its longest cell axis");
        auto scaledTriangle=triangle;scaledTriangle.a*=2;scaledTriangle.b*=2;scaledTriangle.c*=2;
        const auto scaledAsset=RefitLumenGiScene(*scene,std::span(&scaledTriangle,1));
        Check(std::abs(scaledAsset->Attributes()[0].z-meta.z*2)<1e-6f&&scene->Attributes()[0].z==meta.z,
              "Refit did not update incident footprint independently of the in-flight asset");
        Check(std::abs(scaledAsset->Attributes()[0].w-meta.w*2)<1e-6f&&scene->Attributes()[0].w==meta.w,
              "Refit did not update skew filtering footprint independently of the in-flight asset");
        for(unsigned i=0;i<scene->SurfelCount();++i) {
            const auto p=data[1+i*2],N=data[2+i*2];
            Check(p.x>=0&&p.z>=0&&p.x+p.z<=4&&p.y==0,"Surfel escaped its triangle");
            Check(N.y>.999f&&glm::length(glm::vec3(N))>.999f,"Surfel normal/winding is wrong");
            const unsigned sourceX=i%r,sourceY=i/r;
            const unsigned expectedX=sourceX+sourceY>=r?r-1-sourceX:sourceX;
            const unsigned expectedY=sourceX+sourceY>=r?r-1-sourceY:sourceY;
            // Mirrors occupy identical world positions. Lookup must recover
            // their canonical lattice cell, without a singular vertex chart.
            Check(std::abs(p.z/4-(float(expectedX)+1.f/3)/r)<1e-6f&&
                  std::abs(p.x/4-(float(expectedY)+1.f/3)/r)<1e-6f,
                  "World-hit barycentrics cannot recover uniform Surface Cache texel");
            if(sourceX+sourceY>=r)Check(glm::length(glm::vec3(p-data[1+(expectedY*r+expectedX)*2]))<1e-6f,
                "Folded Surface Cache texels differ from their canonical samples");
        }
        // Neighbouring interior cells stay equally spaced in world units.
        // This catches the old sqrt chart's elongated radial fan near vertex A.
        const auto origin=glm::vec3(data[1]);
        Check(glm::length(glm::vec3(data[3])-origin-glm::vec3(0,0,4.f/r))<1e-6f&&
              glm::length(glm::vec3(data[1+r*2])-origin-glm::vec3(4.f/r,0,0))<1e-6f,
              "Triangle cache spacing is distorted near its anchor vertex");
        const glm::vec3 query(.8f,0,1.0f);
        const auto radiance=[](glm::vec3 p){return 2.f+p.x*.4f+p.z*.6f;};
        const glm::vec2 grid(query.z/4*r-1.f/3,query.x/4*r-1.f/3);
        const glm::ivec2 low(glm::floor(grid));const glm::vec2 fraction=glm::fract(grid);float reconstructed=0;
        for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
            const glm::ivec2 at=glm::clamp(low+glm::ivec2(x,y),glm::ivec2(0),glm::ivec2(r-1));
            const float weight=(x?fraction.x:1-fraction.x)*(y?fraction.y:1-fraction.y);
            reconstructed+=radiance(glm::vec3(data[1+(at.y*r+at.x)*2]))*weight;
        }
        Check(std::abs(reconstructed-radiance(query))<1e-5f,"Uniform cache lookup distorted a linear world radiance field");
        const auto single=BuildLumenGiScene(std::span(&triangle,1),10);
        Check(single->SurfelCount()==1&&glm::length(glm::vec3(single->Attributes()[1])-glm::vec3(4.f/3,0,4.f/3))<1e-6f,
              "Single-texel triangle cache no longer uses an interior centroid");
        const auto empty=BuildLumenGiScene({});Check(empty->SurfelCount()==0&&empty->CacheHeight()==1,"Empty scene cannot allocate a legal atlas");
        auto moved=triangle;moved.a+=glm::vec3(1,2,3);moved.b+=glm::vec3(1,2,3);moved.c+=glm::vec3(1,2,3);
        const auto refit=RefitLumenGiScene(*scene,std::span(&moved,1));
        Check(refit->Geometry()->TopologyKey()==scene->Geometry()->TopologyKey()&&refit->SurfelCount()==scene->SurfelCount(),"Refit invalidated stable cache layout");
        Check(!refit->DistanceFieldCurrent()&&refit->DistanceField()==scene->DistanceField(),"Refit retained a stale distance-field accelerator");
        for(unsigned i=0;i<scene->SurfelCount();++i)Check(glm::length(glm::vec3(refit->Attributes()[1+i*2]-data[1+i*2])-glm::vec3(1,2,3))<1e-5f,"Refit surfel did not follow geometry");
        Check(glm::vec3(refit->Geometry()->Pixels()[0])==glm::vec3(1,2,3)&&glm::vec3(refit->Geometry()->Pixels()[1])==glm::vec3(5,2,7),"Refit BVH bounds are stale");
        Check(scene->DistanceFieldCurrent()&&scene->Attributes()[1].y==0,"Refit mutated an in-flight extraction asset");
        auto owned=moved;owned.analyticEmission=true;
        auto ownedScene=RefitLumenGiScene(*refit,std::span(&owned,1));
        Check(ownedScene->EmissiveTriangleCount()==0&&ownedScene->SurfelCount()==scene->SurfelCount(),"Analytic emission is sampled twice or refit lost irradiance layout");
        auto restored=RefitLumenGiScene(*ownedScene,std::span(&moved,1));
        Check(restored->EmissiveTriangleCount()==1&&restored->Attributes()[1+restored->SurfelCount()*2].y==1,"Refit did not restore standalone emitter sampling");
        std::array<ProbeTriangle,2> joined{};
        joined[0].a={0,0,0};joined[0].b={0,0,2};joined[0].c={2,0,0};
        joined[1].a={2,0,0};joined[1].b={0,0,2};joined[1].c={2,0,2};
        auto joinedAsset=BuildLumenGiScene(joined,.5f);
        const auto neighbours=[&](const auto& asset,unsigned triangle){return asset->Attributes()[asset->AdjacencyOffset()+triangle];};
        Check(neighbours(joinedAsset,0).y==1&&neighbours(joinedAsset,1).x==0,"Coplanar common edge is missing reciprocal adjacency");
        Check(neighbours(joinedAsset,0).x==-1&&neighbours(joinedAsset,0).z==-1,"Open surface boundary connected unrelated triangles");
        auto separated=joined;for(auto* p:{&separated[1].a,&separated[1].b,&separated[1].c})*p+=glm::vec3(0,0,3);
        auto separatedAsset=RefitLumenGiScene(*joinedAsset,separated);
        Check(neighbours(separatedAsset,0).y==-1&&neighbours(separatedAsset,1).x==-1,"Moved surface retained disconnected irradiance adjacency");
        Check(neighbours(joinedAsset,0).y==1,"Refit mutated in-flight adjacency");
        auto rejoined=RefitLumenGiScene(*separatedAsset,joined);
        Check(neighbours(rejoined,0).y==1&&rejoined->SurfelCount()==joinedAsset->SurfelCount(),"Refit did not restore adjacency with a stable surfel layout");
        std::array<ProbeTriangle,2> corner=joined;corner[1].a={0,0,0};corner[1].b={0,2,0};corner[1].c={0,0,2};
        auto cornerAsset=BuildLumenGiScene(corner,.5f);
        for(unsigned i=0;i<2;++i)Check(glm::vec3(neighbours(cornerAsset,i))==glm::vec3(-1),"Irradiance smoothing connected a wall corner");
        std::array<ProbeTriangle,3> ambiguous{joined[0],joined[1],joined[1]};auto ambiguousAsset=BuildLumenGiScene(ambiguous,.5f);
        Check(neighbours(ambiguousAsset,0).y==-1,"Non-manifold edge joined arbitrary surfaces");
        CheckPartialRoofEdges();
        bool topologyRejected=false;try{RefitLumenGiScene(*scene,{});}catch(const std::invalid_argument&){topologyRejected=true;}
        Check(topologyRejected,"Refit silently changed triangle IDs");
        std::vector<ProbeTriangle> parts(17,triangle);
        for(unsigned i=0;i<parts.size();++i)for(auto* p:{&parts[i].a,&parts[i].b,&parts[i].c})*p+=glm::vec3(float(i)*7,0,0);
        auto tree=BuildRealtimeGiScene(parts);
        for(unsigned i=0;i<parts.size();++i)for(auto* p:{&parts[i].a,&parts[i].b,&parts[i].c})*p+=glm::vec3(float(parts.size()-i)*-14,1,0);
        auto refittedTree=RefitRealtimeGiScene(*tree,parts);const auto& packed=refittedTree->Pixels();
        Check(refittedTree->NodeCount()>1,"Branch refit regression lacks internal nodes");
        for(unsigned i=0;i<refittedTree->NodeCount();++i) {
            auto lo=packed[i*2],hi=packed[i*2+1];
            const auto contained=[&](glm::vec3 p){return glm::all(glm::greaterThanEqual(p,glm::vec3(lo)))&&glm::all(glm::lessThanEqual(p,glm::vec3(hi)));};
            if(hi.w<0)for(unsigned t=unsigned(lo.w);t<unsigned(lo.w-hi.w);++t)for(unsigned v=0;v<3;++v)
                Check(contained(glm::vec3(packed[refittedTree->NodeCount()*2+t*5+v])),"Refit leaf lost its reordered triangles");
            else for(auto child:{unsigned(lo.w),unsigned(hi.w)})Check(contained(glm::vec3(packed[child*2]))&&contained(glm::vec3(packed[child*2+1])),"Refit parent bounds did not enclose child");
        }
        std::vector<ProbeTriangle> dense(32768,triangle);const auto bounded=BuildLumenGiScene(dense,.05f);
        Check(bounded->SurfelCount()<=MaxLumenSurfels&&bounded->SurfelCount()>=dense.size(),"Surface Cache budget exceeded or dropped triangles");
        const auto& atlas=bounded->Attributes();unsigned covered=0;
        Check(bounded->EmissiveTriangleCount()==dense.size(),"Power CDF dropped dense emitters");
        const auto emitterBegin=unsigned(dense.size())+bounded->SurfelCount()*2;float cdf=0,pdf=0;
        for(unsigned i=0;i<dense.size();++i) {
            auto entry=atlas[emitterBegin+i];Check(entry.y>cdf&&entry.z>0,"Emitter power CDF is not monotonic");cdf=entry.y;pdf+=entry.z;
        }
        Check(cdf==1&&std::abs(pdf-1)<.001f,"Emitter importance distribution is not normalized");
        for(unsigned i=0;i<dense.size();++i){Check(unsigned(atlas[i].x)==covered,"Surface Cache allocations overlap");covered+=unsigned(atlas[i].y*atlas[i].y);}
        Check(covered==bounded->SurfelCount(),"Surface Cache allocations have holes");
        for(unsigned i=0;i<dense.size();++i)Check(atlas[bounded->AdjacencyOffset()+i]==glm::vec4(-1,-1,-1,0),
            "Dense coincident/non-manifold scene allocated neighbor lists");
        LumenGiSettings settings;settings.enabled=true;Check(!ValidateLumenGiSettings(settings),"Missing hybrid scene accepted");
        settings.scene=scene;Check(ValidateLumenGiSettings(settings),"Default hybrid settings rejected");
        Check(settings.lightingView==LumenLightingView::Material,"Default hybrid view changed material shading");
        for(const auto view:{LumenLightingView::SurfaceCache,LumenLightingView::IndirectOnly,LumenLightingView::DirectOnly}) {
            settings.lightingView=view;Check(ValidateLumenGiSettings(settings),"Valid surface-cache lighting view rejected");
            Check(settings.scene==scene,"Display mode replaced the transport scene");
        }
        settings.lightingView=static_cast<LumenLightingView>(4);Check(!ValidateLumenGiSettings(settings),"Unknown surface-cache lighting view accepted");
        settings.lightingView=LumenLightingView::Material;
        // Reflection resolution derives from its unchanged view-resolve
        // factory. A missing replacement marker throws during source creation.
        const auto reflectionResolve=PipelineDetail::LumenReflectionResolveShader();
        Check(reflectionResolve.find("roughDelta")!=std::string::npos&&
              reflectionResolve.find("SkyRadiance(reflect(-V,worldN)")!=std::string::npos,
              "Material reflection-resolve shader lost its generated fallback");
        settings.reflectionResolutionScale=.4f;Check(!ValidateLumenGiSettings(settings),"Unbounded reflection resolution accepted");settings.reflectionResolutionScale=1;
        for(float scale:{.25f,.5f,1.f}) {
            settings.reflectionSourceResolutionScale=scale;
            Check(ValidateLumenGiSettings(settings),"Valid independent reflection source scale rejected");
        }
        for(float scale:{0.f,.249f,1.001f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
            settings.reflectionSourceResolutionScale=scale;
            Check(!ValidateLumenGiSettings(settings),"Invalid reflection source scale accepted");
        }
        settings.reflectionSourceResolutionScale=.5f;
        settings.reflectionTraceMaxDimension=0;Check(!ValidateLumenGiSettings(settings),"Zero reflection trace budget accepted");settings.reflectionTraceMaxDimension=256;
        settings.maxReflectionRoughness=1.1f;Check(!ValidateLumenGiSettings(settings),"Out-of-range rough reflection reuse accepted");settings.maxReflectionRoughness=.4f;
        settings.surfaceUpdatesPerFrame=0;Check(!ValidateLumenGiSettings(settings),"Zero cache updates accepted");settings.surfaceUpdatesPerFrame=512;
        settings.bounceFeedback=1;Check(!ValidateLumenGiSettings(settings),"Unbounded diffuse feedback accepted");settings.bounceFeedback=.9f;
        settings.counts={4,4,5};Check(!ValidateLumenGiSettings(settings),"Oversized radiance volume accepted");
        bool rejected=false;try{BuildLumenGiScene({},0);}catch(const std::invalid_argument&){rejected=true;}Check(rejected,"Invalid surface density accepted");
        std::cout<<"Hybrid GI CPU tests passed: triangle coverage, barycentric lookup, bounded cache and settings\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
