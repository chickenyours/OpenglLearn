#include "Model/Public/cloth_asset.h"
#include "Model/Public/vmd_animation.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string>

namespace {
using namespace Model;
constexpr double Infinity=std::numeric_limits<double>::infinity();
int failures=0;
bool Expect(bool condition,const char* message){std::printf("  %-91s %s\n",message,condition?"ok":"FAIL");failures+=!condition;return condition;}
bool Finite(double value){return std::isfinite(value);}
glm::dvec3 Point(const glm::dmat4& transform,glm::dvec3 p){return glm::dvec3(transform*glm::dvec4(p,1));}
int RoleBone(const SkeletalAssetPackage& asset,const std::string& role){for(const auto& entry:asset.ikRig.humanoid)if(entry.role==role)return entry.bone;return -1;}
double Weight(const VertexSkin& skin,int bone){double result=0;for(int i=0;i<4;++i)if(skin.joints[i]==std::uint32_t(bone))result+=skin.weights[i];return result;}
double Percentile(std::vector<double> values,double fraction){if(values.empty())return Infinity;std::sort(values.begin(),values.end());return values[std::min(values.size()-1,std::size_t(std::ceil(fraction*values.size()))-1)];}
glm::dmat4 MeshTransform(const StaticModel& geometry,std::size_t mesh){for(const auto& instance:geometry.instances)if(instance.meshIndex==mesh)return instance.transform;return glm::dmat4(1);}
double SegmentDistance(glm::dvec3 point,glm::dvec3 a,glm::dvec3 b){const auto axis=b-a;const double length2=glm::dot(axis,axis);return glm::length(point-(a+axis*(length2>1e-18?std::clamp(glm::dot(point-a,axis)/length2,0.,1.):0.)));}
struct Sample {
    std::size_t mesh=0,vertex=0;
    std::size_t second=std::numeric_limits<std::size_t>::max(),third=std::numeric_limits<std::size_t>::max();
    bool Triangle()const{return second!=std::numeric_limits<std::size_t>::max();}
};
struct Region {
    std::string name;
    int bone=-1,end=-1;
    std::vector<Sample> samples;
    ClothCapsule baseline;
};
struct ProxyReference {
    enum class Kind {None,Convex,Capsule};
    Kind kind=Kind::None;
    std::size_t index=0,plane=0;
};
struct ProxyQuery {
    double membership=Infinity,physical=Infinity;
    ProxyReference membershipProxy,physicalProxy;
};
ProxyQuery QueryUnion(const std::vector<ClothWorldConvex>& convexes,const std::vector<ClothWorldCapsule>& capsules,glm::dvec3 point,double thickness,const std::vector<std::size_t>* selected=nullptr){
    ProxyQuery result;
    for(std::size_t i=0;i<convexes.size();++i){if(selected&&std::find(selected->begin(),selected->end(),i)==selected->end())continue;const auto& shape=convexes[i];double membership=-Infinity,physical=-Infinity;std::size_t memberPlane=0,physicalPlane=0;
        for(std::size_t p=0;p<shape.planes.size();++p){const auto& plane=shape.planes[p];const double distance=glm::dot(plane.normal,point)-plane.distance;if(distance>membership){membership=distance;memberPlane=p;}if(plane.collision&&distance>physical){physical=distance;physicalPlane=p;}}
        if(membership<result.membership){result.membership=membership;result.membershipProxy={ProxyReference::Kind::Convex,i,memberPlane};}
        // Partition caps and the skinned-cloud AABB bound only select a patch.
        // They must never become the nearest escape surface: doing so would
        // under-report the actual distance that cloth is pushed from the skin.
        if(membership<=thickness&&Finite(physical)&&physical<result.physical){result.physical=physical;result.physicalProxy={ProxyReference::Kind::Convex,i,physicalPlane};}
    }
    for(std::size_t i=0;i<capsules.size();++i){const auto& capsule=capsules[i];const double distance=SegmentDistance(point,capsule.a,capsule.b)-capsule.radius;
        if(distance<result.membership){result.membership=distance;result.membershipProxy={ProxyReference::Kind::Capsule,i,0};}
        if(distance<=thickness&&distance<result.physical){result.physical=distance;result.physicalProxy={ProxyReference::Kind::Capsule,i,0};}
    }
    return result;
}
struct Metrics {
    struct Observation {Sample sample;glm::dvec3 point{0};ProxyQuery query;double gap=0,leak=0;};
    std::vector<double> shellGaps;
    std::vector<Observation> observations;
    std::size_t rawOutside=0,shellOutside=0,missingSurface=0;
    double maximumLeak=0,minimumSigned=Infinity,maximumSigned=-Infinity;
    bool finite=true;
    void Add(const ProxyQuery& source,double thickness,double units=1,Sample sample={},glm::dvec3 point={0,0,0}){
        auto query=source;query.membership/=units;query.physical/=units;const double signedDistance=query.membership;
        finite&=Finite(signedDistance);if(!Finite(signedDistance))return;
        minimumSigned=std::min(minimumSigned,signedDistance);maximumSigned=std::max(maximumSigned,signedDistance);
        rawOutside+=signedDistance>1e-6;shellOutside+=signedDistance>thickness+1e-6;
        maximumLeak=std::max(maximumLeak,std::max(0.,signedDistance-thickness));
        if(signedDistance<=thickness&&!Finite(query.physical)){++missingSurface;finite=false;}
        // Outside samples remain in the denominator with zero positive gap.
        // Their explicit counts/leak below prevent a sparse proxy from passing
        // by simply omitting hard-to-cover skin points.
        const double gap=Finite(query.physical)?std::max(0.,thickness-query.physical):0;
        shellGaps.push_back(gap);observations.push_back({sample,point/units,query,gap,std::max(0.,signedDistance-thickness)});
    }
    void Add(double signedDistance,double thickness){ProxyQuery query;query.membership=query.physical=signedDistance;Add(query,thickness);}
    double Median()const{return Percentile(shellGaps,.5);}
    double P95()const{return Percentile(shellGaps,.95);}
    double Maximum()const{return shellGaps.empty()?Infinity:*std::max_element(shellGaps.begin(),shellGaps.end());}
    void Print(const char* label)const{
        const double count=double(shellGaps.size());
        std::printf("      %-20s n=%zu physical gap median/p95/max %.3f / %.3f / %.3f mm; outside proxy %zu (%.2f%%), outside shell %zu (%.2f%%), max leak %.3f mm, missing physical surface %zu\n",
            label,shellGaps.size(),Median()*1000,P95()*1000,Maximum()*1000,rawOutside,count?rawOutside*100/count:0,shellOutside,count?shellOutside*100/count:0,maximumLeak*1000,missingSurface);
    }
    void Locations(const char* label,const ClothAsset& cloth,const SkeletalAssetPackage& body,double p95Limit,double maxLimit)const{
        if(observations.empty()||(P95()<p95Limit&&Maximum()<maxLimit&&rawOutside==0&&missingSurface==0))return;
        auto print=[&](const char* reason,const Observation& observation){const auto& sample=observation.sample;
            auto proxy=[&](const ProxyReference& reference){std::string name="none";if(reference.kind==ProxyReference::Kind::Convex&&reference.index<cloth.convexes.size())name="hull["+std::to_string(reference.index)+"] "+cloth.convexes[reference.index].name+" plane "+std::to_string(reference.plane);else if(reference.kind==ProxyReference::Kind::Capsule&&reference.index<cloth.capsules.size())name="capsule["+std::to_string(reference.index)+"] "+cloth.capsules[reference.index].name;return name;};
            std::printf("        %s %s: mesh %zu (%s), %s %zu",label,reason,sample.mesh,sample.mesh<body.geometry->meshes.size()?body.geometry->meshes[sample.mesh].name.c_str():"?",sample.Triangle()?"triangle centroid":"vertex",sample.vertex);
            if(sample.Triangle())std::printf("/%zu/%zu",sample.second,sample.third);
            std::printf("; world %.6f %.6f %.6f m; physical gap %.3f mm, membership %.3f mm, leak %.3f mm; physical=%s; coverage=%s\n",observation.point.x,observation.point.y,observation.point.z,observation.gap*1000,observation.query.membership*1000,observation.leak*1000,proxy(observation.query.physicalProxy).c_str(),proxy(observation.query.membershipProxy).c_str());
        };
        std::vector<std::size_t> order(observations.size());for(std::size_t i=0;i<order.size();++i)order[i]=i;std::sort(order.begin(),order.end(),[&](auto a,auto b){return observations[a].gap<observations[b].gap;});
        if(P95()>=p95Limit)print("P95",observations[order[std::min(order.size()-1,std::size_t(std::ceil(order.size()*.95))-1)]]);
        print("maximum",observations[order.back()]);
        if(rawOutside){const auto worst=std::max_element(observations.begin(),observations.end(),[](const auto& a,const auto& b){return a.query.membership<b.query.membership;});print("coverage failure",*worst);}
    }
};
ClothWorldCapsule WorldCapsule(const ClothCapsule& cap,const SkeletalPose& pose){
    const auto a=cap.boneA<0?glm::dmat4(1):pose.global[cap.boneA],b=cap.boneB<0?glm::dmat4(1):pose.global[cap.boneB];double scale=0;
    for(int c=0;c<3;++c)scale=std::max({scale,glm::length(glm::dvec3(a[c])),glm::length(glm::dvec3(b[c]))});
    return {Point(a,cap.localA),Point(b,cap.localB),cap.radius*scale};
}
bool SkinMesh(const SkeletalAssetPackage& asset,std::size_t mesh){
    auto label=asset.geometry->meshes[mesh].name+" "+asset.geometry->materials[asset.geometry->meshes[mesh].materialIndex].name;
    for(auto& c:label)if(static_cast<unsigned char>(c)<128)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return label.find("skin")!=std::string::npos;
}
std::vector<Sample> WeightedSamples(const SkeletalAssetPackage& body,int bone){
    std::vector<Sample> named,fallback;
    for(std::size_t mi=0;mi<body.skins.size();++mi){const bool skin=SkinMesh(body,mi);const auto& weights=body.skins[mi].vertices;
        for(std::size_t vi=0;vi<weights.size();++vi)if(Weight(weights[vi],bone)>=.2){fallback.push_back({mi,vi});if(skin)named.push_back({mi,vi});}
        // Sample actual rendered face interiors as well as source vertices.
        // Vertex-only coverage cannot detect a gap between independently fit
        // axial/sector hulls that a long source triangle bridges.
        const auto& indices=body.geometry->meshes[mi].indices;
        for(std::size_t t=0;t<indices.size();t+=3){const auto a=indices[t],b=indices[t+1],c=indices[t+2];if((Weight(weights[a],bone)+Weight(weights[b],bone)+Weight(weights[c],bone))/3>=.2){const Sample sample{mi,a,b,c};fallback.push_back(sample);if(skin)named.push_back(sample);}}
    }
    return named.empty()?fallback:named;
}
glm::dvec3 RestPoint(const SkeletalAssetPackage& body,Sample sample){auto p=body.geometry->meshes[sample.mesh].vertices[sample.vertex].position;if(sample.Triangle())p=(p+body.geometry->meshes[sample.mesh].vertices[sample.second].position+body.geometry->meshes[sample.mesh].vertices[sample.third].position)/3.;return Point(body.skins[sample.mesh].bindTransform,p);}
glm::dvec3 SkinnedPoint(const StaticModel& body,Sample sample){auto p=body.meshes[sample.mesh].vertices[sample.vertex].position;if(sample.Triangle())p=(p+body.meshes[sample.mesh].vertices[sample.second].position+body.meshes[sample.mesh].vertices[sample.third].position)/3.;return Point(MeshTransform(body,sample.mesh),p);}
glm::dvec3 SampleNormal(const StaticModel& body,Sample sample){const auto& vertices=body.meshes[sample.mesh].vertices;const auto transform=MeshTransform(body,sample.mesh);glm::dvec3 normal;
    if(sample.Triangle())normal=glm::cross(Point(transform,vertices[sample.second].position)-Point(transform,vertices[sample.vertex].position),Point(transform,vertices[sample.third].position)-Point(transform,vertices[sample.vertex].position));
    else normal=glm::transpose(glm::inverse(glm::dmat3(transform)))*glm::dvec3(vertices[sample.vertex].normal);
    const double length=glm::length(normal);return length>1e-15?normal/length:glm::dvec3(0);
}
// Geometry-only classification of a rendered skin surface buried by body
// self-intersection. Both epsilon-offset sides must independently be inside
// the ACTUAL skinned body, by winding AND oriented ray tests. Proxy gap is used
// only to decide whether to spend time on this probe, never to decide its result.
struct BodySurfaceProbe {
    struct Triangle {glm::dvec3 a,b,c,minimum,maximum,center;};
    struct Node {glm::dvec3 minimum{Infinity},maximum{-Infinity};std::size_t first=0,count=0,left=0,right=0;};
    struct Result {bool tested=false,buried=false;double windingPlus=0,windingMinus=0;int raysPlus=0,raysMinus=0;};
    std::vector<Triangle> triangles;std::vector<std::size_t> order;std::vector<Node> nodes;
    std::map<std::array<std::size_t,4>,Result> cache;
    double units=1;
    explicit BodySurfaceProbe(double scale):units(scale){}
    BodySurfaceProbe(const SkeletalAssetPackage& source,const StaticModel& skinned,double scale):units(scale){
        for(std::size_t mi=0;mi<skinned.meshes.size();++mi)if(SkinMesh(source,mi)){const auto transform=MeshTransform(skinned,mi);const auto& mesh=skinned.meshes[mi];for(std::size_t t=0;t<mesh.indices.size();t+=3)Add(Point(transform,mesh.vertices[mesh.indices[t]].position),Point(transform,mesh.vertices[mesh.indices[t+1]].position),Point(transform,mesh.vertices[mesh.indices[t+2]].position));}Build();
    }
    void Add(glm::dvec3 a,glm::dvec3 b,glm::dvec3 c){if(glm::length(glm::cross(b-a,c-a))<units*units*1e-16)return;triangles.push_back({a,b,c,glm::min(a,glm::min(b,c)),glm::max(a,glm::max(b,c)),(a+b+c)/3.});}
    std::size_t BuildNode(std::size_t first,std::size_t count){const auto index=nodes.size();nodes.push_back({});glm::dvec3 lo(Infinity),hi(-Infinity);for(std::size_t i=first;i<first+count;++i){const auto& triangle=triangles[order[i]];nodes[index].minimum=glm::min(nodes[index].minimum,triangle.minimum);nodes[index].maximum=glm::max(nodes[index].maximum,triangle.maximum);lo=glm::min(lo,triangle.center);hi=glm::max(hi,triangle.center);}
        nodes[index].first=first;nodes[index].count=count;if(count<=8)return index;const auto extent=hi-lo;const int axis=extent.x>=extent.y&&extent.x>=extent.z?0:extent.y>=extent.z?1:2;const auto middle=first+count/2;
        std::nth_element(order.begin()+first,order.begin()+middle,order.begin()+first+count,[&](auto a,auto b){return triangles[a].center[axis]<triangles[b].center[axis];});const auto left=BuildNode(first,middle-first),right=BuildNode(middle,first+count-middle);nodes[index].left=left;nodes[index].right=right;return index;
    }
    void Build(){order.resize(triangles.size());std::iota(order.begin(),order.end(),0);nodes.clear();nodes.reserve(triangles.size()*2);if(!triangles.empty())BuildNode(0,triangles.size());}
    double Winding(glm::dvec3 point)const{double angle=0;for(const auto& triangle:triangles){const auto a=triangle.a-point,b=triangle.b-point,c=triangle.c-point;const double la=glm::length(a),lb=glm::length(b),lc=glm::length(c);const double numerator=glm::dot(a,glm::cross(b,c)),denominator=la*lb*lc+glm::dot(a,b)*lc+glm::dot(b,c)*la+glm::dot(c,a)*lb;angle+=2*std::atan2(numerator,denominator);}return angle/(4*std::acos(-1.));}
    bool RayBox(const Node& node,glm::dvec3 origin,glm::dvec3 inverse)const{double near=0,far=Infinity;for(int axis=0;axis<3;++axis){double a=(node.minimum[axis]-origin[axis])*inverse[axis],b=(node.maximum[axis]-origin[axis])*inverse[axis];if(a>b)std::swap(a,b);near=std::max(near,a);far=std::min(far,b);if(near>far)return false;}return true;}
    bool RayInside(glm::dvec3 point,glm::dvec3 direction)const{if(nodes.empty())return false;struct Hit {double time;int orientation;};std::vector<Hit> hits;std::vector<std::size_t> stack{0};const auto inverse=glm::dvec3(1)/direction;
        while(!stack.empty()){const auto index=stack.back();stack.pop_back();const auto& node=nodes[index];if(!RayBox(node,point,inverse))continue;if(node.count>8){stack.push_back(node.left);stack.push_back(node.right);continue;}
            for(std::size_t i=node.first;i<node.first+node.count;++i){const auto& triangle=triangles[order[i]];const auto ab=triangle.b-triangle.a,ac=triangle.c-triangle.a,h=glm::cross(direction,ac);const double determinant=glm::dot(ab,h);if(std::abs(determinant)<units*units*1e-16)continue;const double inv=1/determinant;const auto offset=point-triangle.a;const double u=glm::dot(offset,h)*inv;if(u< -1e-9||u>1+1e-9)continue;const auto q=glm::cross(offset,ab);const double v=glm::dot(direction,q)*inv;if(v< -1e-9||u+v>1+1e-9)continue;const double time=glm::dot(ac,q)*inv;if(time>units*1e-10)hits.push_back({time,glm::dot(glm::cross(ab,ac),direction)>0?1:-1});}}
        std::sort(hits.begin(),hits.end(),[](const auto& a,const auto& b){return a.time<b.time;});int winding=0;for(std::size_t i=0;i<hits.size();){const double time=hits[i].time;int orientations=0;do{orientations+=hits[i++].orientation;}while(i<hits.size()&&hits[i].time-time<=units*1e-7);winding+=orientations>0?1:orientations<0?-1:0;}return std::abs(winding)>=1;
    }
    Result Inspect(glm::dvec3 point,glm::dvec3 normal)const{Result result;result.tested=true;if(glm::length(normal)<.5||triangles.empty())return result;const double epsilon=units*1e-5;const auto plus=point+normal*epsilon,minus=point-normal*epsilon;
        for(auto direction:{glm::dvec3(1,.371,.529),glm::dvec3(-.423,1,.281),glm::dvec3(.173,-.617,1)}){direction=glm::normalize(direction);result.raysPlus+=RayInside(plus,direction);result.raysMinus+=RayInside(minus,direction);}
        if(result.raysPlus>=2&&result.raysMinus>=2){result.windingPlus=Winding(plus);result.windingMinus=Winding(minus);result.buried=std::abs(result.windingPlus)>.75&&std::abs(result.windingMinus)>.75;}return result;
    }
    Result Inspect(const StaticModel& skinned,Sample sample){const std::array<std::size_t,4> key{sample.mesh,sample.vertex,sample.second,sample.third};if(const auto found=cache.find(key);found!=cache.end())return found->second;return cache.emplace(key,Inspect(SkinnedPoint(skinned,sample),SampleNormal(skinned,sample))).first->second;}
};
Metrics SubsetMetrics(const Metrics& all,const std::vector<BodySurfaceProbe::Result>& classification,bool buried,double thickness){Metrics result;for(std::size_t i=0;i<all.observations.size();++i)if(classification[i].buried==buried){const auto& observation=all.observations[i];result.Add(observation.query,thickness,1,observation.sample,observation.point);}return result;}
ClothCapsule OldCapsule(const SkeletalAssetPackage& body,const std::vector<glm::dmat4>& rest,const std::string& name,int a,int b,int weighted,double fallback,double maximum,double height,int extraWeighted=-1){
    ClothCapsule result;result.name=name;result.boneA=a;result.boneB=b;const auto pa=glm::dvec3(rest[a][3]),pb=glm::dvec3(rest[b][3]);const auto axis=pb-pa;const double length2=glm::dot(axis,axis);std::vector<double> radii;
    // Reproduce the old single-circle fit, including all weighted meshes and
    // its 8..92% axial range. The evaluation samples below use the full 0..100%
    // region, so this baseline does not quietly remove uncovered joints.
    for(std::size_t mi=0;mi<body.skins.size();++mi)for(std::size_t vi=0;vi<body.skins[mi].vertices.size();++vi)if(Weight(body.skins[mi].vertices[vi],weighted)+(extraWeighted>=0?Weight(body.skins[mi].vertices[vi],extraWeighted):0)>=.2){const auto p=RestPoint(body,{mi,vi});const double t=length2>1e-12?glm::dot(p-pa,axis)/length2:.5;if(t>=.08&&t<=.92)radii.push_back(SegmentDistance(p,pa,pb));}
    double radius=fallback;if(!radii.empty()){std::sort(radii.begin(),radii.end());radius=radii[std::size_t((radii.size()-1)*.95)]*1.015;}
    result.radius=std::clamp(radius,height*.012,maximum);result.localA=Point(body.skeleton.bones[a].inverseBind,pa);result.localB=Point(body.skeleton.bones[b].inverseBind,pb);return result;
}
std::vector<Region> Regions(const SkeletalAssetPackage& body,const ClothAsset& cloth){
    const auto rest=GlobalBoneTransforms(body.skeleton,RestPose(body.skeleton));const double height=cloth.unitsPerMeter*1.6;std::vector<Region> result;
    for(const std::string side:{"left","right"})for(const std::string limb:{"thigh","calf"}){
        const bool thigh=limb=="thigh";const int bone=RoleBone(body,side+(thigh?"UpperLeg":"LowerLeg")),end=RoleBone(body,side+(thigh?"LowerLeg":"Foot"));if(bone<0||end<0)continue;
        Region region;region.name=side+" "+limb;region.bone=bone;region.end=end;const auto a=glm::dvec3(rest[bone][3]),axis=glm::dvec3(rest[end][3])-a;const double length2=glm::dot(axis,axis);
        for(auto sample:WeightedSamples(body,bone)){const double t=glm::dot(RestPoint(body,sample)-a,axis)/length2;if(t>=0&&t<=1)region.samples.push_back(sample);}
        region.baseline=OldCapsule(body,rest,region.name,bone,end,bone,height*(thigh?.034:.023),height*(thigh?.052:.038),height);result.push_back(std::move(region));
    }
    const int hips=RoleBone(body,"hips"),left=RoleBone(body,"leftUpperLeg"),right=RoleBone(body,"rightUpperLeg");
    if(hips>=0&&left>=0&&right>=0){Region region;region.name="Pelvis";region.bone=hips;const auto center=(glm::dvec3(rest[left][3])+glm::dvec3(rest[right][3]))*.5;
        for(auto sample:WeightedSamples(body,hips)){const auto p=RestPoint(body,sample);if(p.y>=center.y-height*.075&&p.y<=center.y+height*.1125)region.samples.push_back(sample);}
        region.baseline=OldCapsule(body,rest,region.name,left,right,hips,height*.038,height*.055,height);result.push_back(std::move(region));}
    return result;
}
bool FixedAttachmentConflictScan(const ModularCharacterBundle& character,const AnimationSequenceAsset& clip){
    std::puts("Independent fixed-waist collision conflict scan (no dynamics):");const auto& cloth=character.cloth;std::vector<std::uint32_t> pins;std::vector<std::uint8_t> fixed(cloth.particles.size(),0);
    for(std::uint32_t i=0;i<cloth.particles.size();++i)if(cloth.particles[i].inverseMass==0||cloth.particles[i].maxDistance<=1e-12){pins.push_back(i);fixed[i]=1;}
    struct Worst {double depth=0,time=0;std::size_t tick=0;std::uint32_t pin=0;glm::dvec3 point{0};ProxyQuery query;};
    Worst membershipWorst,rawWorst,marginWorst;std::vector<double> rawMembershipDepths,rawDepths,marginDepths;
    std::uint64_t rawMembers=0,rawContacts=0,marginContacts=0,marginOnly=0;
    const auto frameCount=std::size_t(std::ceil(clip.duration*60-1e-9))+1;std::string error;StaticModel garment;bool valid=true;
    for(std::size_t tick=0;tick<frameCount&&valid;++tick){const double time=std::min(clip.duration,tick/60.);const auto pose=EvaluateSkeletalPose(*character.garment,&clip,time,false);
        if(!SkinSkeletalMesh(*character.garment,pose,garment,&error)){valid=false;break;}const auto convexes=EvaluateClothConvexes(cloth,pose);const auto capsules=EvaluateClothCapsules(cloth,pose);
        if(convexes.size()!=cloth.convexes.size()||capsules.size()!=cloth.capsules.size()){error="Fixed-pin scan could not evaluate every body proxy";valid=false;break;}
        std::vector<glm::dvec3> targets(cloth.particles.size(),glm::dvec3(0));std::vector<std::size_t> counts(targets.size(),0);
        for(const auto& binding:cloth.renderBindings)if(fixed[binding.particle]){targets[binding.particle]+=Point(MeshTransform(garment,binding.mesh),garment.meshes[binding.mesh].vertices[binding.vertex].position);++counts[binding.particle];}
        for(auto pin:pins){if(!counts[pin]){error="A fixed waist particle has no render binding";valid=false;break;}const auto point=targets[pin]/double(counts[pin]);const auto raw=QueryUnion(convexes,capsules,point,0),margin=QueryUnion(convexes,capsules,point,cloth.settings.thickness*cloth.unitsPerMeter);
            if(!Finite(raw.membership)||!Finite(margin.membership)){error="Nonfinite fixed-pin collision geometry";valid=false;break;}
            const double memberDepth=std::max(0.,-raw.membership/cloth.unitsPerMeter),rawDepth=Finite(raw.physical)?std::max(0.,-raw.physical/cloth.unitsPerMeter):0,marginDepth=Finite(margin.physical)?std::max(0.,cloth.settings.thickness-margin.physical/cloth.unitsPerMeter):0;
            rawMembershipDepths.push_back(memberDepth);rawDepths.push_back(rawDepth);marginDepths.push_back(marginDepth);rawMembers+=memberDepth>1e-7;rawContacts+=rawDepth>1e-7;marginContacts+=marginDepth>1e-7;marginOnly+=marginDepth>1e-7&&rawDepth<=1e-7;
            if(memberDepth>membershipWorst.depth)membershipWorst={memberDepth,time,tick,pin,point,raw};if(rawDepth>rawWorst.depth)rawWorst={rawDepth,time,tick,pin,point,raw};if(marginDepth>marginWorst.depth)marginWorst={marginDepth,time,tick,pin,point,margin};
        }
    }
    auto proxyName=[&](const ProxyReference& reference){if(reference.kind==ProxyReference::Kind::Convex&&reference.index<cloth.convexes.size())return "hull["+std::to_string(reference.index)+"] "+cloth.convexes[reference.index].name+" plane "+std::to_string(reference.plane);if(reference.kind==ProxyReference::Kind::Capsule&&reference.index<cloth.capsules.size())return "capsule["+std::to_string(reference.index)+"] "+cloth.capsules[reference.index].name;return std::string("none");};
    auto printWorst=[&](const char* label,const Worst& worst){if(worst.depth<=1e-7){std::printf("      %s: no penetration above 0.1 micrometer\n",label);return;}std::printf("      %s: %.6f mm at %.6fs tick %zu pin %u world %.6f %.6f %.6f m; membership=%s; physical=%s\n",label,worst.depth*1000,worst.time,worst.tick,worst.pin,worst.point.x/cloth.unitsPerMeter,worst.point.y/cloth.unitsPerMeter,worst.point.z/cloth.unitsPerMeter,proxyName(worst.query.membershipProxy).c_str(),proxyName(worst.query.physicalProxy).c_str());};
    const auto count=rawDepths.size();std::printf("    %zu fixed pins x %zu fixed-tick poses, %zu complete pin samples; counts are observations, not distinct particles\n",pins.size(),frameCount,count);
    std::printf("      raw geometric membership inside: %llu; external raw penetration: %llu; %.3fmm contact-margin penetration: %llu, margin-only %llu\n",static_cast<unsigned long long>(rawMembers),static_cast<unsigned long long>(rawContacts),cloth.settings.thickness*1000,static_cast<unsigned long long>(marginContacts),static_cast<unsigned long long>(marginOnly));
    std::printf("      raw membership / external raw / margin depth P95 (including zero-depth samples): %.6f / %.6f / %.6f mm\n",Percentile(rawMembershipDepths,.95)*1000,Percentile(rawDepths,.95)*1000,Percentile(marginDepths,.95)*1000);
    printWorst("maximum raw membership",membershipWorst);printWorst("maximum external raw",rawWorst);printWorst("maximum external margin",marginWorst);
    std::set<std::pair<std::size_t,std::uint32_t>> inspected;
    for(const auto& entry:std::array<std::pair<const char*,Worst>,3>{{{"raw membership",membershipWorst},{"external raw",rawWorst},{"external margin",marginWorst}}}){
        const auto& worst=entry.second;if(worst.depth<=1e-7||!inspected.insert({worst.tick,worst.pin}).second)continue;const auto pose=EvaluateSkeletalPose(*character.body,&clip,worst.time,false);StaticModel body;
        if(!SkinSkeletalMesh(*character.body,pose,body,&error)){valid=false;break;}BodySurfaceProbe actual(*character.body,body,cloth.unitsPerMeter);const double winding=actual.Winding(worst.point);int rays=0;
        for(auto direction:{glm::dvec3(1,.371,.529),glm::dvec3(-.423,1,.281),glm::dvec3(.173,-.617,1)})rays+=actual.RayInside(worst.point,glm::normalize(direction));
        const bool inside=std::abs(winding)>.75&&rays>=2,outside=std::abs(winding)<.25&&rays<=1;
        std::printf("      actual-body check of worst %s: %.6fs pin %u; winding %.9f; oriented-ray inside votes %d/3; independent point classification %s\n",entry.first,worst.time,worst.pin,winding,rays,inside?"INSIDE actual rendered skin":outside?"OUTSIDE actual rendered skin":"AMBIGUOUS (no exclusion inferred)");
    }
    if(!valid)std::printf("    fixed-pin geometry scan error: %s\n",error.c_str());
    std::puts("    Raw penetration and margin-only conflicts are diagnostic; this scan does not move pins, exempt constraints or alter the solver's attachment assertions.");
    return valid&&count==pins.size()*frameCount;
}
void PortableBuriedClassification(){
    std::puts("Independent actual-surface self-intersection classification:");BodySurfaceProbe probe(1);
    const std::array<std::array<int,3>,12> faces={{{0,3,2},{0,2,1},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{3,7,6},{3,6,2},{1,2,6},{1,6,5},{0,4,7},{0,7,3}}};
    for(double center:{-.05,.05}){const std::array<glm::dvec3,8> points={{{center-.1,.1,-.1},{center+.1,.1,-.1},{center+.1,.1,.1},{center-.1,.1,.1},{center-.1,-.1,-.1},{center+.1,-.1,-.1},{center+.1,-.1,.1},{center-.1,-.1,.1}}};for(const auto& face:faces)probe.Add(points[face[0]],points[face[1]],points[face[2]]);}probe.Build();
    const auto internal=probe.Inspect({.05,0,0},{1,0,0}),external=probe.Inspect({-.15,0,0},{-1,0,0});
    Expect(internal.buried&&internal.raysPlus>=2&&internal.raysMinus>=2&&std::abs(internal.windingPlus)>.75&&std::abs(internal.windingMinus)>.75,"Both actual-surface sides inside overlapping body lobes require ray and winding evidence");
    Expect(!external.buried,"A genuinely exposed actual skin face stays subject to the unchanged fit budget");
}
void PortableFlattenedLimb(){
    std::puts("Portable flattened/tapered limb and world-plane evaluation:");ClothAsset cloth;cloth.unitsPerMeter=1;cloth.settings.thickness=.003;ClothConvexCollider shape;shape.name="flattened tapered thigh";shape.bone=0;shape.referenceValid=true;
    shape.vertices={{-.06,0,-.025},{.06,0,-.025},{.06,0,.025},{-.06,0,.025},{-.035,-.4,-.015},{.035,-.4,-.015},{.035,-.4,.015},{-.035,-.4,.015}};
    shape.triangles={{0,3,2},{0,2,1},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{3,7,6},{3,6,2},{1,2,6},{1,6,5},{0,4,7},{0,7,3}};
    for(const auto& triangle:shape.triangles){const auto a=shape.vertices[triangle[0]],normal=glm::normalize(glm::cross(shape.vertices[triangle[1]]-a,shape.vertices[triangle[2]]-a));shape.planes.push_back({normal,glm::dot(normal,a)});}cloth.convexes.push_back(shape);
    SkeletalPose pose;pose.global={glm::translate(glm::dmat4(1),glm::dvec3(.8,-.2,.3))*glm::rotate(glm::dmat4(1),1.2,glm::normalize(glm::dvec3(.3,1,.7)))};const auto world=EvaluateClothConvexes(cloth,pose);bool valid=world.size()==1;Metrics fitted,round;
    if(valid)for(int i=1;i<20;++i){const double t=i/20.,y=-.4*t,x=.06-.025*t,z=.025-.01*t;for(auto p:{glm::dvec3(x,y,0),glm::dvec3(-x,y,0),glm::dvec3(0,y,z),glm::dvec3(0,y,-z)}){fitted.Add(QueryUnion(world,{},Point(pose.global[0],p),.003),.003);round.Add(SegmentDistance(p,{0,0,0},{0,-.4,0})-.065,.008);}}
    Expect(valid&&fitted.finite&&fitted.rawOutside==0&&std::abs(fitted.Median()-.003)<1e-10&&fitted.Maximum()<.003000001,"A transformed tapered hull retains a three-millimeter shell at actual skin surface points");
    Expect(valid&&round.Median()>.02&&fitted.P95()<round.P95()*.2,"An oversized circular proxy demonstrably floats above a flattened limb while the fitted hull does not");
    fitted.Print("fitted body hull");round.Print("old round proxy");
    if(valid){const auto outside=Point(pose.global[0],{0,-.2,.04});Metrics uncovered;uncovered.Add(QueryUnion(world,{},outside,.003),.003);Expect(uncovered.rawOutside==1&&uncovered.shellOutside==1&&uncovered.maximumLeak>.01,"Outside skin samples are reported as coverage failures rather than disappearing from gap statistics");
        auto partition=world;const auto point=Point(pose.global[0],{0,-.2,0});const auto normal=glm::normalize(glm::dvec3(pose.global[0]*glm::dvec4(0,1,0,0)));partition[0].planes.push_back({normal,glm::dot(normal,point),false});Metrics fullSurface;
        fullSurface.Add(QueryUnion(partition,{},point,.003),.003);const auto expected=QueryUnion(world,{},point,.003);
        Expect(std::abs(QueryUnion(partition,{},point,.003).membership)<1e-10&&fullSurface.Median()>.02&&std::abs(fullSurface.Median()-(.003-expected.physical))<1e-10,"An artificial membership plane cannot under-report the true physical shell gap");}
}
void RealBodyFit(){
    std::puts("Independent actual-body skin to collision-proxy fit:");std::puts("    Coverage = min over hulls of max(ALL plane phi). Physical gap uses COLLISION planes of membership-eligible hulls only.\n    Positive physical shell gap = max(0, thickness - min eligible max collision phi); artificial caps/AABB planes cannot shrink it.\n    All outside counts and maximum leaks remain visible. Regions include weighted named-SKIN vertices AND rendered face centroids\n    in the full axial/height domain, including joints. A freely hanging garment's distance from skin is not a fit error.");
    const std::filesystem::path root=MODEL_SOURCE_DIR;const auto sourcePath=root/"Asset/Generated/MahiroMaid/Package.asset.json",animationPath=root/"Asset/Generated/Animations/MahiroTaitui.anim.json";
    if(!std::filesystem::exists(sourcePath)){std::puts("    Local real fixture unavailable; portable body-fit checks completed.");return;}
    std::string error;auto source=LoadSkeletalAssetPackage(sourcePath,&error);if(!Expect(bool(source),"The original generated character loads for independent body fitting")){std::printf("    %s\n",error.c_str());return;}
    auto character=BuildSkirtGarment(*source,{},&error);if(!Expect(bool(character),"A fresh separated garment/body and fitted collision asset can be generated")){std::printf("    %s\n",error.c_str());return;}
    const auto& cloth=character->cloth;const auto& body=*character->body;const auto regions=Regions(body,cloth);
    const int pelvisBone=RoleBone(body,"hips");const auto pelvisHulls=std::count_if(cloth.convexes.begin(),cloth.convexes.end(),[&](const auto& shape){return shape.bone==pelvisBone;});
    Expect(cloth.convexes.size()==18&&pelvisHulls==8&&cloth.capsules.empty(),"The real body uses eighteen fitted hulls, including eight pelvis sectors, without oversized foot capsules");
    Expect(std::abs(cloth.settings.thickness-.003)<1e-12,"The fitted body uses the intended three-millimeter contact thickness");
    bool allRegions=true;for(const auto& region:regions)allRegions&=!region.samples.empty();Expect(regions.size()==5&&allRegions,"Both thighs, both calves and pelvis retain a nonempty complete skin sample set");
    std::optional<AnimationSequenceAsset> clip;if(std::filesystem::exists(animationPath)){clip=LoadAnimationSequenceAsset(animationPath,body,&error);Expect(bool(clip),"The real baked VMD sequence is available for dynamic fit diagnostics");}
    struct PoseCase {const char* name;double time;bool reference;};
    const std::array<PoseCase,10> cases={{{"reference",0,true},{"VMD start",0,false},{"raised 0.667",2./3,false},{"raised 1.0",1,false},{"turn 2.7",2.7,false},{"reset 2.9",2.9,false},{"middle 4.6",4.6,false},{"turn 6.967",209./30,false},{"reset 7.4",7.4,false},{"end 9.2",9.2,false}}};
    const auto rest=GlobalBoneTransforms(body.skeleton,RestPose(body.skeleton));const double height=cloth.unitsPerMeter*1.6;std::vector<ClothCapsule> oldFeet;
    for(const std::string side:{"left","right"}){const int foot=RoleBone(body,side+"Foot"),toes=RoleBone(body,side+"Toes");if(foot>=0&&toes>=0)oldFeet.push_back(OldCapsule(body,rest,side+" foot",foot,toes,foot,height*.020,height*.032,height,toes));}
    bool evaluations=true;Metrics staticThighNew,staticThighOld;std::size_t dynamicPoses=0,totalBuried=0,totalProbed=0;
    for(const auto& poseCase:cases){if(!poseCase.reference&&!clip)continue;const auto pose=EvaluateSkeletalPose(body,poseCase.reference?nullptr:&*clip,poseCase.time,false);StaticModel skinned;
        if(!SkinSkeletalMesh(body,pose,skinned,&error)){evaluations=false;break;}const auto convexes=EvaluateClothConvexes(cloth,pose);const auto currentCapsules=EvaluateClothCapsules(cloth,pose);evaluations&=convexes.size()==cloth.convexes.size()&&currentCapsules.size()==cloth.capsules.size();
        BodySurfaceProbe probe(body,skinned,cloth.unitsPerMeter);
        std::vector<ClothWorldCapsule> baselineUnion;for(const auto& region:regions)baselineUnion.push_back(WorldCapsule(region.baseline,pose));for(const auto& foot:oldFeet)baselineUnion.push_back(WorldCapsule(foot,pose));
        if(!poseCase.reference)++dynamicPoses;std::printf("    %s (%.6fs):\n",poseCase.name,poseCase.time);
        for(const auto& region:regions){Metrics matched,bodyUnion,oldMatched,oldBodyUnion;std::vector<std::size_t> shapeIndices;for(std::size_t i=0;i<cloth.convexes.size();++i)if(cloth.convexes[i].bone==region.bone)shapeIndices.push_back(i);const auto old=WorldCapsule(region.baseline,pose);
            for(const auto& sample:region.samples){const auto point=SkinnedPoint(skinned,sample);const auto matching=QueryUnion(convexes,{},point,cloth.settings.thickness*cloth.unitsPerMeter,&shapeIndices),all=QueryUnion(convexes,currentCapsules,point,cloth.settings.thickness*cloth.unitsPerMeter);
                matched.Add(matching,cloth.settings.thickness,cloth.unitsPerMeter,sample,point);bodyUnion.Add(all,cloth.settings.thickness,cloth.unitsPerMeter,sample,point);
                oldMatched.Add((SegmentDistance(point,old.a,old.b)-old.radius)/cloth.unitsPerMeter,.008);oldBodyUnion.Add(QueryUnion({},baselineUnion,point,.008*cloth.unitsPerMeter),.008,cloth.unitsPerMeter,sample,point);
                if(poseCase.reference&&region.name.find("thigh")!=std::string::npos){staticThighNew.Add(matching,cloth.settings.thickness,cloth.unitsPerMeter,sample,point);staticThighOld.Add((SegmentDistance(point,old.a,old.b)-old.radius)/cloth.unitsPerMeter,.008);}
            }
            std::printf("      %s: %zu matched hulls\n",region.name.c_str(),shapeIndices.size());matched.Print("matched hull union");bodyUnion.Print("whole body union");oldMatched.Print("old circle + 8mm");oldBodyUnion.Print("old body + 8mm");
            evaluations&=matched.finite&&bodyUnion.finite&&oldMatched.finite&&oldBodyUnion.finite&&matched.shellGaps.size()==region.samples.size();
            const bool pelvis=region.name=="Pelvis",thigh=region.name.find("thigh")!=std::string::npos;const double p95Limit=pelvis?.016:thigh?.012:.013;
            matched.Locations("matched",cloth,body,p95Limit,.025);bodyUnion.Locations("whole body",cloth,body,p95Limit,.025);
            if(matched.observations.size()!=region.samples.size()||bodyUnion.observations.size()!=region.samples.size()){Expect(false,"Every skin sample must retain a finite proxy query before surface classification");continue;}
            std::vector<BodySurfaceProbe::Result> classification(region.samples.size());std::size_t buriedCount=0,probed=0,worstBuried=0;double worstBuriedGap=0;
            for(std::size_t i=0;i<region.samples.size();++i)if(matched.observations[i].gap>=p95Limit||bodyUnion.observations[i].gap>=p95Limit){classification[i]=probe.Inspect(skinned,region.samples[i]);++probed;if(classification[i].buried){++buriedCount;const double gap=std::max(matched.observations[i].gap,bodyUnion.observations[i].gap);if(gap>worstBuriedGap){worstBuriedGap=gap;worstBuried=i;}}}
            totalProbed+=probed;totalBuried+=buriedCount;
            const auto exposedMatched=SubsetMetrics(matched,classification,false,cloth.settings.thickness),exposedBody=SubsetMetrics(bodyUnion,classification,false,cloth.settings.thickness),buriedMatched=SubsetMetrics(matched,classification,true,cloth.settings.thickness),buriedBody=SubsetMetrics(bodyUnion,classification,true,cloth.settings.thickness);
            std::printf("      body-only classification: %zu / %zu samples probed, %zu geometrically buried, %zu conservatively exposed; all original statistics above remain unchanged\n",probed,region.samples.size(),buriedCount,region.samples.size()-buriedCount);exposedMatched.Print("exposed matched");exposedBody.Print("exposed whole body");
            exposedMatched.Locations("EXPOSED matched",cloth,body,p95Limit,.025);exposedBody.Locations("EXPOSED whole body",cloth,body,p95Limit,.025);
            if(buriedCount){buriedMatched.Print("buried matched");buriedBody.Print("buried whole body");const auto& info=classification[worstBuried];const auto& sample=region.samples[worstBuried];std::printf("        maximum buried gap %.3f mm at mesh %zu %s %zu",worstBuriedGap*1000,sample.mesh,sample.Triangle()?"triangle":"vertex",sample.vertex);if(sample.Triangle())std::printf("/%zu/%zu",sample.second,sample.third);std::printf("; independent actual body winding +/- %.6f / %.6f; oriented-ray inside votes +/- %d / %d of 3\n",info.windingPlus,info.windingMinus,info.raysPlus,info.raysMinus);}
            Expect(!shapeIndices.empty()&&matched.finite&&matched.rawOutside==0&&matched.shellGaps.size()==region.samples.size(),poseCase.reference?"Reference-pose matched hulls cover every actual skin sample with a physical surface":"Dynamic skin-aware matched hulls cover every actual skin sample with a physical surface");
            Expect(!exposedMatched.shellGaps.empty()&&exposedMatched.P95()<p95Limit&&exposedMatched.Maximum()<.025,poseCase.reference?"Exposed reference skin retains the original regional physical fit budget":"Exposed dynamic skin retains the original regional physical fit budget");
            Expect(bodyUnion.finite&&bodyUnion.rawOutside==0,poseCase.reference?"The full collision-body union covers every selected reference skin sample":"The full collision-body union covers every selected dynamic skin sample");
            Expect(!exposedBody.shellGaps.empty()&&exposedBody.P95()<p95Limit&&exposedBody.Maximum()<.025,"Other body proxies cannot push exposed skin beyond the unchanged regional gap budget");
        }
    }
    if(clip)evaluations=FixedAttachmentConflictScan(*character,*clip)&&evaluations;
    Expect(evaluations,"All reported poses evaluate the actual skinned geometry with finite complete fit and coverage statistics");
    std::printf("    Independent body-geometry observations examined: %zu (probe results cached within each pose), geometrically buried observations %zu; no source sample removed from coverage or all-sample gap reports\n",totalProbed,totalBuried);
    Expect(!clip||dynamicPoses==9,"All nine real VMD test poses contribute strict dynamic coverage and physical-gap checks");
    Expect(!staticThighNew.shellGaps.empty()&&staticThighNew.rawOutside==0&&staticThighNew.Median()<.008&&staticThighNew.P95()<.012,"The full reference thigh sample set has no missing coverage or systematic centimeter-scale shell gap");
    Expect(staticThighOld.Median()>.015&&staticThighNew.Median()<staticThighOld.Median()*.6&&staticThighNew.P95()<staticThighOld.P95()*.7,"Independent fitting removes the old circular thigh proxy's measured centimeter-scale floating baseline");
    std::puts("    All-sample coverage remains strict. Only independently verified body-self-intersection interiors are reported separately from the exposed-skin fit budget.");
    if(!evaluations)std::printf("    %s\n",error.c_str());
}
}
int main(){PortableFlattenedLimb();PortableBuriedClassification();RealBodyFit();std::printf("Cloth body fit: %s (%d failures)\n",failures?"FAILED":"PASSED",failures);return failures?1:0;}
