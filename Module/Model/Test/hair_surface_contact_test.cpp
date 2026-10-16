#include "Model/Private/hair_surface_contact.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
using namespace Model;
using namespace Model::detail;
int failures=0;
void Expect(bool condition,const char* text){std::printf("%-105s %s\n",text,condition?"ok":"FAIL");if(!condition)++failures;}
bool Near(glm::dvec3 a,glm::dvec3 b,double epsilon=1e-8){return glm::length(a-b)<=epsilon;}
struct ScopedOption{
    std::string name,previous;bool existed=false;
    ScopedOption(const char* key,const char* value):name(key){if(const auto* old=std::getenv(key)){existed=true;previous=old;}
#ifdef _WIN32
        _putenv_s(key,value);
#else
        setenv(key,value,1);
#endif
    }
    ~ScopedOption(){
#ifdef _WIN32
        _putenv_s(name.c_str(),existed?previous.c_str():"");
#else
        if(existed)setenv(name.c_str(),previous.c_str(),1);else unsetenv(name.c_str());
#endif
    }
};
SkeletalAssetPackage Fixture(){
    SkeletalAssetPackage p;p.id="surface";p.geometry=std::make_shared<StaticModel>();p.geometry->materials.push_back({});
    p.skeleton.bones.resize(3);p.skeleton.bones[0].name="body";p.skeleton.bones[1].name="Head";p.skeleton.bones[2].name="J_Sec_Hair";p.skeleton.bones[2].parent=1;
    StaticMesh box;box.name="Body";const std::array<glm::dvec3,8> corners{{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}}};
    for(auto c:corners){StaticVertex v;v.position=c*.1;v.normal=glm::vec3(glm::normalize(c));box.vertices.push_back(v);box.bounds.Include(v.position);}
    box.indices={0,2,1,0,3,2,4,5,6,4,6,7,1,2,6,1,6,5,0,4,7,0,7,3,3,7,6,3,6,2,0,1,5,0,5,4};
    p.geometry->meshes.push_back(box);p.skins.emplace_back();p.skins[0].vertices.resize(8);
    StaticMesh hair;hair.name="Hair";
    for(auto point:std::array<glm::dvec3,6>{{{.09,-.03,-.03},{.09,.03,-.03},{.09,0,.03},{.09,-.03,-.03},{.09,-.01,0},{.09,.01,0}}}){StaticVertex v;v.position=point;v.normal=glm::normalize(glm::vec3(1,.2,.1));v.tangent={0,0,1,-1};hair.vertices.push_back(v);hair.bounds.Include(point);}
    hair.indices={0,1,2,3,1,2};p.geometry->meshes.push_back(hair);p.skins.emplace_back();p.skins[1].vertices.resize(6);
    for(auto& skin:p.skins[1].vertices)skin.joints[0]=2;p.skins[1].vertices[4].joints[0]=1;
    p.geometry->instances={{"body",0,glm::dmat4(1)},{"hair",1,glm::dmat4(1)}};return p;
}
HairAsset Hair(){HairAsset hair;hair.unitsPerMeter=1;hair.hairMeshes={1};HairChain chain;chain.anchorBone=1;chain.links.push_back({2,{0,-.1,0}});hair.chains.push_back(chain);hair.attachments.push_back({1,5,0});return hair;}
void RefineBody(SkeletalAssetPackage& package){
    const auto original=package.geometry->meshes[0];auto& mesh=package.geometry->meshes[0];mesh.vertices.clear();mesh.indices.clear();mesh.bounds={};constexpr std::uint32_t divisions=8;
    for(std::size_t t=0;t+2<original.indices.size();t+=3){const auto a=original.vertices[original.indices[t]].position,b=original.vertices[original.indices[t+1]].position,c=original.vertices[original.indices[t+2]].position;const auto normal=glm::normalize(glm::cross(b-a,c-a));std::array<std::vector<std::uint32_t>,divisions+1> rows;
        for(std::uint32_t i=0;i<=divisions;++i)for(std::uint32_t j=0;j<=divisions-i;++j){StaticVertex vertex;vertex.position=a+(b-a)*(double(i)/divisions)+(c-a)*(double(j)/divisions);vertex.normal=glm::vec3(normal);rows[i].push_back(static_cast<std::uint32_t>(mesh.vertices.size()));mesh.vertices.push_back(vertex);mesh.bounds.Include(vertex.position);}
        for(std::uint32_t i=0;i<divisions;++i)for(std::uint32_t j=0;j<divisions-i;++j){mesh.indices.insert(mesh.indices.end(),{rows[i][j],rows[i+1][j],rows[i][j+1]});if(j+1<divisions-i)mesh.indices.insert(mesh.indices.end(),{rows[i+1][j],rows[i+1][j+1],rows[i][j+1]});}
    }
    package.skins[0].vertices.assign(mesh.vertices.size(),VertexSkin{});
}
glm::dvec3 RenderPoint(const StaticModel& model,std::uint32_t mesh,std::uint32_t vertex){for(const auto& i:model.instances)if(i.meshIndex==mesh)return glm::dvec3(i.transform*glm::dvec4(model.meshes[mesh].vertices[vertex].position,1));return model.meshes[mesh].vertices[vertex].position;}
double Penetration(const HairBodyCollision& body,const StaticModel& output){double maximum=0;for(auto v:{0u,1u,2u,3u}){const auto nearest=body.Nearest(RenderPoint(output,1,v));if(nearest.inside)maximum=std::max(maximum,nearest.distance);}return maximum;}
bool Corrected(const SkeletalAssetPackage& package,const SkeletalPose& pose,const HairSurfaceContact& contact,StaticModel& out,std::string& error){return SkinSkeletalMesh(package,pose,out,&error)&&ApplyHairSurfaceOffsets(package,pose,contact.Offsets(),out,&error);}
}

int main(){
    std::string error;
    {
        auto p=Fixture();auto hair=Hair();auto pose=EvaluateSkeletalPose(p,nullptr,0,false);HairBodyCollision body;HairSurfaceContact contact;HairSimulationStats stats;
        Expect(body.Initialize(p,hair.hairMeshes,1,&error)&&contact.Initialize(hair,p,&error),"Initializes immutable body and post-skin vertex contact layer");
        stats.steps=71;stats.maxPenetration=.0123;
        Expect(contact.Reset(hair,p,pose,body,stats,&error),"Reset repairs actual free hair embedded ten millimeters inside a closed body");
        StaticModel out;Expect(Corrected(p,pose,contact,out,error),"Renders ordinary skeletal skin followed by anchor-local surface offsets");
        Expect(stats.surfaceVertices==4&&stats.surfaceClusters==1&&!contact.Offsets().empty(),"Every positively hair-weighted free vertex participates, including seam copies");
        Expect(Penetration(body,out)<1e-7&&stats.maxMeshCorrection>.009,"Actual corrected render vertices leave the finite body even with no bone rotation");
        Expect(stats.steps==71&&stats.maxPenetration==.0123,"Surface diagnostics preserve bone step count and bone penetration diagnostics");
        Expect(Near(out.meshes[1].vertices[4].position,p.geometry->meshes[1].vertices[4].position)&&Near(out.meshes[1].vertices[5].position,p.geometry->meshes[1].vertices[5].position),"Rigid Head vertices and explicit authored attachments remain exactly unchanged");
        Expect(Near(out.meshes[1].vertices[0].position,out.meshes[1].vertices[3].position),"Duplicate UV seam vertices receive coherent surface displacements");
        Expect(Near(out.meshes[0].vertices[0].position,p.geometry->meshes[0].vertices[0].position)&&Near(p.geometry->meshes[1].vertices[0].position,{.09,-.03,-.03}),"Body geometry and immutable source hair remain unchanged");
        Expect(out.meshes[1].bounds.maximum.x>.1&&out.bounds.valid&&out.bounds.maximum.x>.1,"Corrected mesh and instance-space scene bounds include displaced hair");
        Expect(std::abs(glm::length(out.meshes[1].vertices[0].normal)-1)<1e-6&&out.meshes[1].vertices[0].tangent.w==-1,"Normals remain normalized and imported tangent handedness survives contact deformation");
        auto saved=contact.Offsets();const auto queries=body.QueryStats();const auto lastStats=stats;auto presented=pose;presented.local[1].translation={.2,.3,0};presented.local[1].rotation=glm::angleAxis(.7,glm::dvec3(0,0,1));presented.global=GlobalBoneTransforms(p.skeleton,presented.local);
        Expect(contact.Step(hair,p,presented,body,0,hair.settings,stats,&error)&&contact.Offsets()==saved,"Render-only zero tick preserves exact anchor-local offsets after Head movement");
        Expect(body.QueryStats().nearestQueries==queries.nearestQueries&&body.QueryStats().sweepQueries==queries.sweepQueries&&body.QueryStats().broadphaseQueries==queries.broadphaseQueries,"Zero tick does not query stale body geometry or advance physical trajectories");
        Expect(stats.maxSurfacePenetration==lastStats.maxSurfacePenetration&&stats.surfaceContacts==lastStats.surfaceContacts&&stats.surfaceMilliseconds==lastStats.surfaceMilliseconds,"Render-only presentation retains last physical surface residual and work diagnostics");
        StaticModel plain,moved;SkinSkeletalMesh(p,presented,plain,&error);Corrected(p,presented,contact,moved,error);bool follows=true;
        for(const auto& o:saved)follows&=Near(moved.meshes[o.mesh].vertices[o.vertex].position-plain.meshes[o.mesh].vertices[o.vertex].position,glm::dmat3(presented.global[o.anchorBone])*o.displacement);
        Expect(follows,"Post-skin corrections follow the current anchor transform during presentation");
        body.ResetQueryStats();bool stable=true;for(int i=0;i<20;++i){body.ResetMotion();stable&=contact.Step(hair,p,pose,body,1./60,hair.settings,stats,&error)&&Corrected(p,pose,contact,out,error)&&Penetration(body,out)<1e-7;}
        Expect(stable,"Resting contacts remain outside without repeatedly relaxing their solved displacements");
        Expect(!contact.Step(hair,p,pose,body,-1,hair.settings,stats,&error)&&!contact.Step(hair,p,pose,body,NAN,hair.settings,stats,&error),"Invalid physical timesteps fail without integrating");
    }
    {
        // A plane contact makes the old artificial decay cycle measurable:
        // the point drifts toward the plane until CCD pushes it out again.
        auto p=Fixture();auto hair=Hair();auto pose=EvaluateSkeletalPose(p,nullptr,0,false);
        HairBodyCollision body,referenceBody,validationBody;HairSurfaceContact contact,reference;HairSimulationStats stats,referenceStats;
        bool valid=body.Initialize(p,hair.hairMeshes,1,&error)&&referenceBody.Initialize(p,hair.hairMeshes,1,&error)&&validationBody.Initialize(p,hair.hairMeshes,1,&error);
        {ScopedOption option("MODEL_HAIR_SURFACE_STATIC_CONTACTS","1"),cache("MODEL_HAIR_SURFACE_REUSE","1");valid&=contact.Initialize(hair,p,&error);}
        {ScopedOption option("MODEL_HAIR_SURFACE_STATIC_CONTACTS","0"),cache("MODEL_HAIR_SURFACE_REUSE","0");valid&=reference.Initialize(hair,p,&error);}
        valid&=contact.Reset(hair,p,pose,body,stats,&error)&&reference.Reset(hair,p,pose,referenceBody,referenceStats,&error);
        StaticModel out,baseline,old;valid&=Corrected(p,pose,contact,baseline,error);double travel=0,referenceTravel=0;body.ResetQueryStats();referenceBody.ResetQueryStats();
        for(int tick=0;tick<180&&valid;++tick){body.ResetMotion();referenceBody.ResetMotion();valid&=contact.Step(hair,p,pose,body,1./60,hair.settings,stats,&error)&&reference.Step(hair,p,pose,referenceBody,1./60,hair.settings,referenceStats,&error)&&Corrected(p,pose,contact,out,error)&&Corrected(p,pose,reference,old,error);
            for(auto vertex:{0u,1u,2u,3u}){travel=std::max(travel,glm::length(RenderPoint(out,1,vertex)-RenderPoint(baseline,1,vertex)));referenceTravel=std::max(referenceTravel,glm::length(RenderPoint(old,1,vertex)-RenderPoint(baseline,1,vertex)));}
            // Independent quality scans must not be charged to the solver's
            // query counters used by the exact-certificate assertion below.
            valid&=Penetration(validationBody,out)<1e-7;
        }
        std::printf("Static fine-contact optimized/reference travel %.9f / %.9f mm\n",travel*1000,referenceTravel*1000);
        Expect(valid&&travel<1e-10&&referenceTravel>.0005,"Three seconds of unchanged skin/body stop the artificial decay/projection cycle without disabling contact checks");
        std::printf("Static solver-only nearest optimized/reference %llu / %llu; independent scan %llu\n",static_cast<unsigned long long>(body.QueryStats().nearestQueries),static_cast<unsigned long long>(referenceBody.QueryStats().nearestQueries),static_cast<unsigned long long>(validationBody.QueryStats().nearestQueries));
        Expect(body.QueryStats().nearestQueries<20&&body.QueryStats().nearestQueries<referenceBody.QueryStats().nearestQueries,"Exact outside certificates survive physical ticks only while the actual body revision and query point remain unchanged");
        const auto before=RenderPoint(out,1,0);pose.local[2].translation.x+=5e-10;pose.global=GlobalBoneTransforms(p.skeleton,pose.local);body.ResetMotion();
        valid=contact.Step(hair,p,pose,body,1./60,hair.settings,stats,&error)&&Corrected(p,pose,contact,out,error);
        Expect(valid&&Near(RenderPoint(out,1,0)-before,{5e-10,0,0},1e-12),"Nanometer relaxation deadband still applies the actual new skinned position instead of freezing geometry");
        const auto saved=contact.Offsets();auto disabled=hair.settings;disabled.collision=false;body.ResetMotion();
        Expect(contact.Step(hair,p,pose,body,1./60,disabled,stats,&error)&&contact.Offsets()!=saved,"Disabling collision invalidates static-contact retention and resumes offset release");
    }
    {
        auto p=Fixture();auto hair=Hair();for(auto& v:p.geometry->meshes[1].vertices)v.position.x=.15;auto pose=EvaluateSkeletalPose(p,nullptr,0,false);HairBodyCollision body;HairSurfaceContact contact;HairSimulationStats stats;
        body.Initialize(p,hair.hairMeshes,1,&error);contact.Initialize(hair,p,&error);contact.Reset(hair,p,pose,body,stats,&error);body.ResetQueryStats();
        Expect(contact.Step(hair,p,pose,body,1./60,hair.settings,stats,&error)&&stats.surfaceBroadphaseRejects==stats.surfaceClusters&&stats.surfaceSweeps==0,"Spatial clusters reject static free hair away from actual body surface bounds");
        Expect(body.QueryStats().nearestQueries==0&&body.QueryStats().sweepQueries==0,"Separated steady state performs no per-vertex nearest or sweep work");
        pose.local[2].translation.x=-.1;pose.global=GlobalBoneTransforms(p.skeleton,pose.local);body.ResetMotion();
        StaticModel out;Expect(contact.Step(hair,p,pose,body,1./60,hair.settings,stats,&error)&&Corrected(p,pose,contact,out,error)&&Penetration(body,out)<1e-7&&stats.surfaceContacts>0,"Fast skeletal motion across a finite body face triggers post-skin continuous contact");
    }
    {
        auto p=Fixture();auto hair=Hair();for(auto& v:p.geometry->meshes[1].vertices)v.position.x=.15;auto pose=EvaluateSkeletalPose(p,nullptr,0,false);HairBodyCollision body;HairSurfaceContact contact;HairSimulationStats stats;
        body.Initialize(p,hair.hairMeshes,1,&error);contact.Initialize(hair,p,&error);contact.Reset(hair,p,pose,body,stats,&error);
        auto bodyPose=pose;bodyPose.local[0].translation.x=.1;bodyPose.global=GlobalBoneTransforms(p.skeleton,bodyPose.local);body.Update(bodyPose,&error);StaticModel out;
        Expect(contact.Step(hair,p,pose,body,1./60,hair.settings,stats,&error)&&Corrected(p,pose,contact,out,error)&&Penetration(body,out)<1e-7&&stats.surfaceContacts>0,"Actual moving body geometry invalidates resting contact even when the passed hair pose is exactly unchanged");
    }
    {
        auto p=Fixture();const auto transform=glm::translate(glm::dmat4(1),glm::dvec3(2,3,4))*glm::rotate(glm::dmat4(1),.4,glm::dvec3(0,1,0))*glm::scale(glm::dmat4(1),glm::dvec3(2,.5,1.5));
        p.geometry->instances[1].transform=transform;p.skins[1].bindTransform=transform;const auto pose=EvaluateSkeletalPose(p,nullptr,0,false);StaticModel before,out;SkinSkeletalMesh(p,pose,before,&error);out=before;
        const HairSurfaceOffset offset{1,0,1,{.012,.004,-.003}};
        Expect(ApplyHairSurfaceOffsets(p,pose,std::span<const HairSurfaceOffset>(&offset,1),out,&error)&&Near(RenderPoint(out,1,0)-RenderPoint(before,1,0),offset.displacement),"Nonidentity rotated and scaled mesh instances convert anchor displacements into mesh local space exactly");
        const auto artist=before.meshes[1].vertices[0].normal;Expect(glm::dot(out.meshes[1].vertices[0].normal,artist)>.7f&&glm::length(out.meshes[1].vertices[0].normal-glm::vec3(1,0,0))>.01f,"Changed surface transports the supplied artist normal instead of replacing it with a flat triangle normal");
        const auto position=out.meshes[1].vertices[0].position;HairSurfaceOffset bad=offset;bad.displacement.x=NAN;
        Expect(!ApplyHairSurfaceOffsets(p,pose,std::span<const HairSurfaceOffset>(&bad,1),out,&error)&&Near(out.meshes[1].vertices[0].position,position),"Invalid offset validation leaves the current rendered mesh untouched");
        std::array<HairSurfaceOffset,2> duplicates{offset,offset};Expect(!ApplyHairSurfaceOffsets(p,pose,duplicates,out,&error)&&Near(out.meshes[1].vertices[0].position,position),"Duplicate vertex corrections fail before partial mutation");
    }
    {
        auto p=Fixture();auto hair=Hair();auto& indices=p.geometry->meshes[0].indices;indices.erase(indices.begin()+24,indices.begin()+30); // Open neck.
        for(auto& v:p.geometry->meshes[1].vertices)v.position.y=.15;
        auto pose=EvaluateSkeletalPose(p,nullptr,0,false);HairBodyCollision body;HairSurfaceContact contact;HairSimulationStats stats;body.Initialize(p,hair.hairMeshes,1,&error);contact.Initialize(hair,p,&error);contact.Reset(hair,p,pose,body,stats,&error);
        pose.local[2].translation.y=-.15;pose.global=GlobalBoneTransforms(p.skeleton,pose.local);body.ResetMotion();const auto before=body.SweepContact({.09,.15,-.03},{.09,0,-.03},hair.settings.thickness,false);
        Expect(!before.hit&&body.Nearest({.09,0,-.03}).inside,"Open neck fixture enters a genuinely enclosed garment region without crossing a finite triangle");
        StaticModel out;Expect(contact.Step(hair,p,pose,body,1./60,hair.settings,stats,&error)&&Corrected(p,pose,contact,out,error)&&Penetration(body,out)<1e-7&&stats.surfaceContacts>0,"Cheap backside endpoint candidates repair open-boundary entry missed by pure CCD");
    }
    {
        auto p=Fixture();auto hair=Hair();auto& vertices=p.geometry->meshes[1].vertices;
        vertices[0].position=vertices[3].position={.105,0,-.03};vertices[1].position={.085,.12,.015};vertices[2].position={.085,-.12,.015};
        auto pose=EvaluateSkeletalPose(p,nullptr,0,false);HairBodyCollision body;HairSurfaceContact contact;HairSimulationStats stats;body.Initialize(p,hair.hairMeshes,1,&error);contact.Initialize(hair,p,&error);
        bool cornersOutside=true;for(auto i:{0u,1u,2u,3u})cornersOutside&=!body.Nearest(vertices[i].position).inside;const auto before=(vertices[0].position+vertices[1].position+vertices[2].position)/3.;
        Expect(cornersOutside&&body.Nearest(before).inside&&body.Nearest(before).distance>.008,"Face fixture has outside render corners but an interior centroid deeper than five millimeters");
        StaticModel out;bool corrected=contact.Reset(hair,p,pose,body,stats,&error)&&Corrected(p,pose,contact,out,error);const auto center=(RenderPoint(out,1,0)+RenderPoint(out,1,1)+RenderPoint(out,1,2))/3.;const auto nearest=body.Nearest(center);
        Expect(corrected&&(!nearest.inside||nearest.distance<.005)&&Penetration(body,out)<1e-7,"Sparse true-inside face centroid correction fixes interior penetration while keeping all corners outside");
        Expect(Near(RenderPoint(out,1,0),RenderPoint(out,1,3),1e-12),"Face barycentric corrections and final endpoint guards preserve hard UV seam groups");
    }
    {
        ScopedOption gate("MODEL_HAIR_CONSERVATIVE_GATE","1"),componentGate("MODEL_HAIR_COMPONENT_BACKFACE_GATE","1");auto p=Fixture();auto hair=Hair();auto& box=p.geometry->meshes[0];
        // Preserve two correct enclosing ray pairs while the closest patch's
        // winding and supplied normal both face inward. A backface is not a
        // necessary condition for this collider's verified finite enclosure.
        for(std::size_t t=12;t<18;t+=3)std::swap(box.indices[t+1],box.indices[t+2]);for(auto& vertex:box.vertices)if(vertex.position.x>0)vertex.normal.x=-vertex.normal.x;
        for(auto& vertex:p.geometry->meshes[1].vertices)vertex.position={.15,.085,.085};p.geometry->meshes[1].vertices[1].position.y+=.001;p.geometry->meshes[1].vertices[2].position.z+=.001;
        auto pose=EvaluateSkeletalPose(p,nullptr,0,false);HairBodyCollision body;HairSurfaceContact contact;HairSimulationStats stats;body.Initialize(p,hair.hairMeshes,1,&error);contact.Initialize(hair,p,&error);contact.Reset(hair,p,pose,body,stats,&error);
        const auto interior=body.Nearest({.09,.085,.085});Expect(interior.inside&&!interior.backfacing&&!body.HasNearbyBackFacingSurface({.09,.085,.085},.03),"Verified enclosed point can face the nearest patch's positive side and have no nearest-component-backface candidate");
        pose.local[2].translation.x=-.06;pose.global=GlobalBoneTransforms(p.skeleton,pose.local);body.ResetMotion();StaticModel out;
        Expect(contact.Step(hair,p,pose,body,1./60,hair.settings,stats,&error)&&Corrected(p,pose,contact,out,error)&&Penetration(body,out)<1e-7,"Conservative component-box endpoint gate repairs enclosure despite locally inverted closest-face orientation");
    }
    {
        auto p=Fixture();auto hair=Hair();auto& vertices=p.geometry->meshes[1].vertices;
        vertices[0].position=vertices[3].position={-.105,0,0};vertices[1].position={.1001,.012,-.008};vertices[2].position={.1001,-.012,.008};
        auto pose=EvaluateSkeletalPose(p,nullptr,0,false);HairBodyCollision body,validation;HairSurfaceContact contact;HairSimulationStats stats;
        bool valid=body.Initialize(p,hair.hairMeshes,1,&error)&&validation.Initialize(p,hair.hairMeshes,1,&error)&&contact.Initialize(hair,p,&error);
        // Translating this centroid to the right moves the leftmost corner
        // inside the box. Repairing that corner then buries the centroid
        // again: face and endpoint constraints must be alternated locally.
        std::array<glm::dvec3,3> once{vertices[0].position,vertices[1].position,vertices[2].position};auto center=(once[0]+once[1]+once[2])/3.;const auto initial=validation.Nearest(center);
        for(auto& point:once)point+=initial.normal*(initial.distance+hair.settings.thickness);
        for(auto& point:once){const auto n=validation.Nearest(point);if(n.inside)point+=n.normal*(n.distance+hair.settings.thickness);}
        const auto undone=validation.Nearest((once[0]+once[1]+once[2])/3.);
        Expect(initial.inside&&undone.inside&&undone.distance>.005,"Opposite-side endpoint repair can invalidate an already projected face by more than five millimeters");
        StaticModel out;valid&=contact.Reset(hair,p,pose,body,stats,&error)&&Corrected(p,pose,contact,out,error);double maximumFaceDepth=0;
        auto check=[&](){const auto center=(RenderPoint(out,1,0)+RenderPoint(out,1,1)+RenderPoint(out,1,2))/3.;const auto n=validation.Nearest(center);maximumFaceDepth=std::max(maximumFaceDepth,n.inside?n.distance:0.);return (!n.inside||n.distance<=.002)&&Penetration(validation,out)<1e-7&&Near(RenderPoint(out,1,0),RenderPoint(out,1,3),1e-12);};
        valid&=check();
        for(int tick=0;tick<30&&valid;++tick){pose.local[0].translation.x=.003*std::sin(tick*.23);pose.local[2].translation.x=.002*std::cos(tick*.19);pose.global=GlobalBoneTransforms(p.skeleton,pose.local);
            valid&=body.Update(pose,&error)&&validation.Update(pose,&error)&&contact.Step(hair,p,pose,body,1./60,hair.settings,stats,&error)&&Corrected(p,pose,contact,out,error)&&check();
        }
        std::printf("Endpoint/face alternating solve max centroid depth %.9f mm\n",maximumFaceDepth*1000);
        Expect(valid,"Dirty face/endpoint alternation converges below two millimeters and keeps both UV copies welded through thirty moving frames");
        const auto last=out.meshes[1].vertices;double restingTravel=0;
        for(int tick=0;tick<60&&valid;++tick){body.ResetMotion();valid&=contact.Step(hair,p,pose,body,1./60,hair.settings,stats,&error)&&Corrected(p,pose,contact,out,error)&&check();for(auto index:{0u,1u,2u,3u})restingTravel=std::max(restingTravel,glm::length(out.meshes[1].vertices[index].position-last[index].position));}
        Expect(valid&&restingTravel<.002,"Converged face contacts retain their solution at rest instead of restarting seam averaging cycles");
    }
    {
        ScopedOption gate("MODEL_HAIR_CONSERVATIVE_GATE","1");auto p=Fixture();auto hair=Hair();auto& vertices=p.geometry->meshes[1].vertices;
        vertices[0].position=vertices[3].position={.2,0,0};vertices[1].position={-.09,.2,.02};vertices[2].position={-.09,-.2,-.02};
        auto pose=EvaluateSkeletalPose(p,nullptr,0,false);HairBodyCollision body;HairSurfaceContact contact;HairSimulationStats stats;body.Initialize(p,hair.hairMeshes,1,&error);contact.Initialize(hair,p,&error);const auto source=(vertices[0].position+vertices[1].position+vertices[2].position)/3.;const auto initial=body.Nearest(source);
        Expect(initial.inside&&initial.distance>.03&&!body.HasNearbyBackFacingSurface(source,.012),"Deep face centroid exceeds both former twelve-millimeter and thirty-millimeter query gates");
        StaticModel out;const bool solved=contact.Reset(hair,p,pose,body,stats,&error)&&Corrected(p,pose,contact,out,error);const auto center=(RenderPoint(out,1,0)+RenderPoint(out,1,1)+RenderPoint(out,1,2))/3.;const auto nearest=body.Nearest(center);
        Expect(solved&&(!nearest.inside||nearest.distance<.005),"Component-volume broad phase retains deep face interiors even when no nearby triangle surface overlaps the old range");
    }
    {
        auto p=Fixture();auto hair=Hair();auto& mesh=p.geometry->meshes[1];auto& skin=p.skins[1];
        for(auto point:std::array<glm::dvec3,2>{{{.02,.09,0},{-.02,.09,0}}}){StaticVertex v;v.position=point;mesh.vertices.push_back(v);VertexSkin w;w.joints[0]=2;skin.vertices.push_back(w);}
        mesh.indices={0,6,7,3,4,5}; // Same seam corner, different fans and correction directions.
        auto pose=EvaluateSkeletalPose(p,nullptr,0,false);HairBodyCollision body;HairSurfaceContact contact;HairSimulationStats stats;body.Initialize(p,hair.hairMeshes,1,&error);contact.Initialize(hair,p,&error);StaticModel out;
        Expect(contact.Reset(hair,p,pose,body,stats,&error)&&Corrected(p,pose,contact,out,error)&&Near(RenderPoint(out,1,0),RenderPoint(out,1,3),1e-12),"Hard UV seam groups stay welded when distinct triangle fans produce different tangential smoothing offsets");
        bool welded=true;for(int tick=0;tick<10;++tick){body.ResetMotion();welded&=contact.Step(hair,p,pose,body,1./60,hair.settings,stats,&error)&&Corrected(p,pose,contact,out,error)&&Near(RenderPoint(out,1,0),RenderPoint(out,1,3),1e-12);}
        Expect(welded,"Seam welding remains exact while offsets relax and each fan is independently smoothed");
    }
    {
        auto p=Fixture();auto hair=Hair();hair.settings.collision=false;auto pose=EvaluateSkeletalPose(p,nullptr,0,false);HairBodyCollision body;HairSurfaceContact contact;HairSimulationStats stats;body.Initialize(p,hair.hairMeshes,1,&error);contact.Initialize(hair,p,&error);contact.Reset(hair,p,pose,body,stats,&error);
        Expect(contact.Offsets().empty(),"Disabled contact preserves the uncorrected source vertex positions");
        auto enabled=hair.settings;enabled.collision=true;StaticModel out;
        Expect(contact.Step(hair,p,pose,body,1./60,enabled,stats,&error)&&Corrected(p,pose,contact,out,error)&&Penetration(body,out)<1e-7,"Re-enabling collision repairs stationary inside vertices once even with no surface AABB crossing");
    }
    {
        ScopedOption quiet("MODEL_HAIR_PROFILE","0");auto p=Fixture();auto hair=Hair();auto& v=p.geometry->meshes[1].vertices;
        v[0].position=v[3].position={.105,0,-.03};v[1].position={.085,.12,.015};v[2].position={.085,-.12,.015};
        HairBodyCollision referenceBody,optimizedBody;HairSurfaceContact reference,optimized;HairSimulationStats referenceStats,optimizedStats;
        bool initialized=referenceBody.Initialize(p,hair.hairMeshes,1,&error)&&optimizedBody.Initialize(p,hair.hairMeshes,1,&error);
        {ScopedOption option("MODEL_HAIR_SURFACE_REUSE","0");initialized&=reference.Initialize(hair,p,&error);}
        {ScopedOption option("MODEL_HAIR_SURFACE_REUSE","1");initialized&=optimized.Initialize(hair,p,&error);}
        auto pose=EvaluateSkeletalPose(p,nullptr,0,false);initialized&=reference.Reset(hair,p,pose,referenceBody,referenceStats,&error)&&optimized.Reset(hair,p,pose,optimizedBody,optimizedStats,&error);
        bool positionsEqual=initialized,offsetsEqual=initialized;double maximumPositionDifference=0,maximumOffsetDifference=0;
        for(int tick=0;tick<60&&positionsEqual&&offsetsEqual;++tick){pose.local[0].translation={.012*std::sin(tick*.13),.006*std::cos(tick*.11),0};pose.local[1].translation={.02*std::sin(tick*.09),0,0};pose.local[1].rotation=glm::angleAxis(.2*std::sin(tick*.07),glm::dvec3(0,0,1));pose.local[2].rotation=glm::angleAxis(.5*std::sin(tick*.17),glm::dvec3(0,1,0));pose.global=GlobalBoneTransforms(p.skeleton,pose.local);
            positionsEqual&=referenceBody.Update(pose,&error)&&optimizedBody.Update(pose,&error)&&reference.Step(hair,p,pose,referenceBody,1./60,hair.settings,referenceStats,&error)&&optimized.Step(hair,p,pose,optimizedBody,1./60,hair.settings,optimizedStats,&error);
            StaticModel a,b;positionsEqual&=Corrected(p,pose,reference,a,error)&&Corrected(p,pose,optimized,b,error);
            if(!positionsEqual)break;for(std::size_t m=0;m<a.meshes.size();++m)for(std::size_t vertex=0;vertex<a.meshes[m].vertices.size();++vertex){const double difference=glm::length(a.meshes[m].vertices[vertex].position-b.meshes[m].vertices[vertex].position);maximumPositionDifference=std::max(maximumPositionDifference,difference);positionsEqual&=difference<1e-10;}
            std::vector<glm::dvec3> referenceOffsets(v.size(),glm::dvec3(0)),optimizedOffsets(v.size(),glm::dvec3(0));for(const auto& offset:reference.Offsets())if(offset.mesh==1)referenceOffsets[offset.vertex]=offset.displacement;for(const auto& offset:optimized.Offsets())if(offset.mesh==1)optimizedOffsets[offset.vertex]=offset.displacement;
            for(std::size_t vertex=0;vertex<v.size();++vertex){const double difference=glm::length(referenceOffsets[vertex]-optimizedOffsets[vertex]);maximumOffsetDifference=std::max(maximumOffsetDifference,difference);offsetsEqual&=difference<1e-10;}
        }
        std::printf("Reuse comparison max render/anchor-local differences %.12g / %.12g\n",maximumPositionDifference,maximumOffsetDifference);
        Expect(positionsEqual&&offsetsEqual,"Sixty animated physical ticks preserve reference render positions and anchor-local offsets under exact query reuse");
        const auto& a=referenceBody.QueryStats();const auto& b=optimizedBody.QueryStats();
        Expect(b.sweepQueries+b.nearbyBackfaceQueries+b.nearestQueries<a.sweepQueries+a.nearbyBackfaceQueries+a.nearestQueries,"Exact seam/unchanged-face reuse reduces real collision queries without changing geometry");
    }
    {
        // A subdivided open tube has genuine outside points inside its convex
        // bounds. These still need the exact enclosure rays, but do not need a
        // global closest surface before the inside-only result is known.
        auto p=Fixture();auto hair=Hair();p.geometry->meshes[0].indices.resize(24);RefineBody(p);auto& v=p.geometry->meshes[1].vertices;
        v[0].position=v[3].position={.09,.095,-.03};v[1].position={.09,.095,.03};v[2].position={.08,.095,0};
        HairBodyCollision referenceBody,optimizedBody;HairSurfaceContact reference,optimized;HairSimulationStats referenceStats,optimizedStats;bool valid=true;
        {ScopedOption option("MODEL_HAIR_INSIDE_ONLY","0");valid&=referenceBody.Initialize(p,hair.hairMeshes,1,&error);}
        {ScopedOption option("MODEL_HAIR_INSIDE_ONLY","1");valid&=optimizedBody.Initialize(p,hair.hairMeshes,1,&error);}
        valid&=reference.Initialize(hair,p,&error)&&optimized.Initialize(hair,p,&error);auto pose=EvaluateSkeletalPose(p,nullptr,0,false);
        Expect(valid&&referenceBody.MayBeInsideComponentBounds(v[0].position)&&!referenceBody.Nearest(v[0].position).inside,"Inside-only benchmark includes verified outside points retained by the conservative component gate");
        valid&=reference.Reset(hair,p,pose,referenceBody,referenceStats,&error)&&optimized.Reset(hair,p,pose,optimizedBody,optimizedStats,&error);referenceBody.ResetQueryStats();optimizedBody.ResetQueryStats();bool positionsExact=valid,offsetsExact=valid,sawContact=false;
        for(int tick=0;tick<60&&positionsExact&&offsetsExact;++tick){pose.local[0].translation.x=.0007*std::sin(tick*.13);pose.local[2].translation.y=tick<30?0:-.1*std::pow(std::sin((tick-30)*.1),2);pose.global=GlobalBoneTransforms(p.skeleton,pose.local);
            positionsExact&=referenceBody.Update(pose,&error)&&optimizedBody.Update(pose,&error)&&reference.Step(hair,p,pose,referenceBody,1./60,hair.settings,referenceStats,&error)&&optimized.Step(hair,p,pose,optimizedBody,1./60,hair.settings,optimizedStats,&error);
            StaticModel a,b;positionsExact&=Corrected(p,pose,reference,a,error)&&Corrected(p,pose,optimized,b,error);if(!positionsExact)break;
            for(std::size_t mesh=0;mesh<a.meshes.size();++mesh)for(std::size_t vertex=0;vertex<a.meshes[mesh].vertices.size();++vertex)positionsExact&=a.meshes[mesh].vertices[vertex].position==b.meshes[mesh].vertices[vertex].position;
            offsetsExact&=reference.Offsets()==optimized.Offsets();sawContact|=optimizedStats.surfaceContacts>0;
        }
        const auto& a=referenceBody.QueryStats();const auto& b=optimizedBody.QueryStats();
        std::printf("Inside-only reference/optimized geometry triangle tests %llu / %llu; inside-only calls %llu / %llu\n",static_cast<unsigned long long>(a.triangleTests),static_cast<unsigned long long>(b.triangleTests),static_cast<unsigned long long>(a.insideOnlyQueries),static_cast<unsigned long long>(b.insideOnlyQueries));
        Expect(positionsExact&&offsetsExact&&sawContact,"Sixty moving outside/inside frames keep bit-identical rendered positions and contact offsets under exact inside-only queries");
        Expect(a.insideOnlyQueries>0&&b.insideOnlyQueries>0&&b.triangleTests<a.triangleTests,"Inside-only queries reduce actual surface triangle traversal without omitting enclosure classification");
    }
    {
        SkeletalAssetPackage p;HairAsset hair;HairBodyCollision body;HairSurfaceContact contact;HairSimulationStats stats;SkeletalPose pose;
        Expect(contact.Initialize(hair,p,&error)&&contact.Reset(hair,p,pose,body,stats,&error)&&contact.Step(hair,p,pose,body,0,hair.settings,stats,&error)&&contact.Offsets().empty(),"Legacy skeleton-only and empty-hair fixtures remain successful no-ops");
        p=Fixture();hair=Hair();for(auto& v:p.geometry->meshes[1].vertices)v.position={.09,0,0};pose=EvaluateSkeletalPose(p,nullptr,0,false);body.Initialize(p,hair.hairMeshes,1,&error);
        Expect(contact.Initialize(hair,p,&error)&&contact.Reset(hair,p,pose,body,stats,&error)&&!contact.Offsets().empty(),"Degenerate hair triangles still support real vertex contacts without rejecting the asset");
    }
    std::printf("Hair surface contact: %d failure(s)\n",failures);return failures?1:0;
}
