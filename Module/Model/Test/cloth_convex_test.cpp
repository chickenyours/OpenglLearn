#include "Model/Private/cloth_convex.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>

namespace {
using namespace Model;
using namespace Model::detail;
int failures=0;
bool Expect(bool condition,const char* message){std::printf("  %-100s %s\n",message,condition?"ok":"FAIL");failures+=!condition;return condition;}
bool Near(glm::dvec3 a,glm::dvec3 b,double tolerance=1e-9){return glm::length(a-b)<=tolerance;}
struct ScopedSeparatingCache {
    std::string previous;bool existed=false;
    explicit ScopedSeparatingCache(const char* value){
#ifdef _WIN32
        char* stored=nullptr;std::size_t length=0;if(_dupenv_s(&stored,&length,"MODEL_CLOTH_SEPARATING_CACHE")==0&&stored){existed=true;previous=stored;}std::free(stored);_putenv_s("MODEL_CLOTH_SEPARATING_CACHE",value);
#else
        if(const auto* stored=std::getenv("MODEL_CLOTH_SEPARATING_CACHE")){existed=true;previous=stored;}setenv("MODEL_CLOTH_SEPARATING_CACHE",value,1);
#endif
    }
    ~ScopedSeparatingCache(){
#ifdef _WIN32
        _putenv_s("MODEL_CLOTH_SEPARATING_CACHE",existed?previous.c_str():"");
#else
        if(existed)setenv("MODEL_CLOTH_SEPARATING_CACHE",previous.c_str(),1);else unsetenv("MODEL_CLOTH_SEPARATING_CACHE");
#endif
    }
};
struct ScopedConvexSimd {
    std::string previous;bool existed=false;
    explicit ScopedConvexSimd(const char* value){
#ifdef _WIN32
        char* stored=nullptr;std::size_t length=0;if(_dupenv_s(&stored,&length,"MODEL_CLOTH_CONVEX_SIMD")==0&&stored){existed=true;previous=stored;}std::free(stored);_putenv_s("MODEL_CLOTH_CONVEX_SIMD",value);
#else
        if(const auto* stored=std::getenv("MODEL_CLOTH_CONVEX_SIMD")){existed=true;previous=stored;}setenv("MODEL_CLOTH_CONVEX_SIMD",value,1);
#endif
    }
    ~ScopedConvexSimd(){
#ifdef _WIN32
        _putenv_s("MODEL_CLOTH_CONVEX_SIMD",existed?previous.c_str():"");
#else
        if(existed)setenv("MODEL_CLOTH_CONVEX_SIMD",previous.c_str(),1);else unsetenv("MODEL_CLOTH_CONVEX_SIMD");
#endif
    }
};
struct ScopedCcdSampleBounds {
    std::string previous;bool existed=false;
    explicit ScopedCcdSampleBounds(const char* value){
#ifdef _WIN32
        char* stored=nullptr;std::size_t length=0;if(_dupenv_s(&stored,&length,"MODEL_CLOTH_CCD_SAMPLE_BOUNDS")==0&&stored){existed=true;previous=stored;}std::free(stored);_putenv_s("MODEL_CLOTH_CCD_SAMPLE_BOUNDS",value);
#else
        if(const auto* stored=std::getenv("MODEL_CLOTH_CCD_SAMPLE_BOUNDS")){existed=true;previous=stored;}setenv("MODEL_CLOTH_CCD_SAMPLE_BOUNDS",value,1);
#endif
    }
    ~ScopedCcdSampleBounds(){
#ifdef _WIN32
        _putenv_s("MODEL_CLOTH_CCD_SAMPLE_BOUNDS",existed?previous.c_str():"");
#else
        if(existed)setenv("MODEL_CLOTH_CCD_SAMPLE_BOUNDS",previous.c_str(),1);else unsetenv("MODEL_CLOTH_CCD_SAMPLE_BOUNDS");
#endif
    }
};
struct ScopedCompositeCache {
    std::string previous;bool existed=false;
    explicit ScopedCompositeCache(const char* value){
#ifdef _WIN32
        char* stored=nullptr;std::size_t length=0;if(_dupenv_s(&stored,&length,"MODEL_CLOTH_COMPOSITE_CACHE")==0&&stored){existed=true;previous=stored;}std::free(stored);_putenv_s("MODEL_CLOTH_COMPOSITE_CACHE",value);
#else
        if(const auto* stored=std::getenv("MODEL_CLOTH_COMPOSITE_CACHE")){existed=true;previous=stored;}setenv("MODEL_CLOTH_COMPOSITE_CACHE",value,1);
#endif
    }
    ~ScopedCompositeCache(){
#ifdef _WIN32
        _putenv_s("MODEL_CLOTH_COMPOSITE_CACHE",existed?previous.c_str():"");
#else
        if(existed)setenv("MODEL_CLOTH_COMPOSITE_CACHE",previous.c_str(),1);else unsetenv("MODEL_CLOTH_COMPOSITE_CACHE");
#endif
    }
};
bool SameBits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
bool SameBits(glm::dvec3 a,glm::dvec3 b){return SameBits(a.x,b.x)&&SameBits(a.y,b.y)&&SameBits(a.z,b.z);}
bool SameContact(const ClothConvexContact& a,const ClothConvexContact& b){
    return a.hit==b.hit&&SameBits(a.point,b.point)&&SameBits(a.bary,b.bary)&&SameBits(a.normal,b.normal)&&
        SameBits(a.separation,b.separation)&&a.plane==b.plane&&a.separatingPlane==b.separatingPlane;
}
bool SameShape(const ClothWorldConvex& a,const ClothWorldConvex& b){
    if(a.vertices.size()!=b.vertices.size()||a.planes.size()!=b.planes.size()||a.triangles!=b.triangles||
        !SameBits(a.minimum,b.minimum)||!SameBits(a.maximum,b.maximum))return false;
    for(std::size_t i=0;i<a.vertices.size();++i)if(!SameBits(a.vertices[i],b.vertices[i]))return false;
    for(std::size_t i=0;i<a.planes.size();++i)if(!SameBits(a.planes[i].normal,b.planes[i].normal)||
        !SameBits(a.planes[i].distance,b.planes[i].distance)||a.planes[i].collision!=b.planes[i].collision)return false;
    for(int column=0;column<4;++column)for(int row=0;row<4;++row)if(!SameBits(a.transform[column][row],b.transform[column][row]))return false;
    return true;
}
bool SamePhysicsStats(const ClothSimulationStats& a,const ClothSimulationStats& b){
    return a.steps==b.steps&&a.resets==b.resets&&a.constraintIterations==b.constraintIterations&&a.particles==b.particles&&a.pinned==b.pinned&&
        a.contacts==b.contacts&&a.selfContacts==b.selfContacts&&a.infeasibleAnimatedTargets==b.infeasibleAnimatedTargets&&
        a.kinematicColliderOverlaps==b.kinematicColliderOverlaps&&a.collapsedTriangles==b.collapsedTriangles&&
        SameBits(a.maxDisplacement,b.maxDisplacement)&&SameBits(a.maxStretchError,b.maxStretchError)&&SameBits(a.maxPenetration,b.maxPenetration)&&
        SameBits(a.maxDynamicEdgeError,b.maxDynamicEdgeError)&&SameBits(a.dynamicEdgeError95,b.dynamicEdgeError95)&&
        SameBits(a.maxKinematicStretchError,b.maxKinematicStretchError)&&SameBits(a.maxSurfacePenetration,b.maxSurfacePenetration)&&
        SameBits(a.maxBendDeviation,b.maxBendDeviation)&&SameBits(a.maxResolvableSurfacePenetration,b.maxResolvableSurfacePenetration)&&
        SameBits(a.maxKinematicColliderPenetration,b.maxKinematicColliderPenetration)&&SameBits(a.maxAttachmentError,b.maxAttachmentError);
}
ClothConvexCollider Box(glm::dvec3 half,glm::dvec3 center={0,0,0}){
    ClothConvexCollider shape;shape.name="skin-box";shape.bone=-1;
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x)
        shape.vertices.push_back(center+glm::dvec3(x?half.x:-half.x,y?half.y:-half.y,z?half.z:-half.z));
    shape.triangles={{{0,4,6}},{{0,6,2}},{{1,3,7}},{{1,7,5}},{{0,1,5}},{{0,5,4}},
        {{2,6,7}},{{2,7,3}},{{0,2,3}},{{0,3,1}},{{4,5,7}},{{4,7,6}}};
    for(int axis=0;axis<3;++axis)for(double sign:{1.,-1.}){glm::dvec3 normal(0);normal[axis]=sign;shape.planes.push_back({normal,half[axis]+glm::dot(normal,center)});}
    return shape;
}
struct Fixture {ClothAsset cloth;StaticModel mesh;SkeletalPose pose;};
void AddPoint(Fixture& f,glm::dvec3 point,bool pinned=false){
    const auto index=std::uint32_t(f.cloth.particles.size());if(f.mesh.meshes.empty())f.mesh.meshes.resize(1);
    f.cloth.particles.push_back({point,pinned?0.:1.,pinned?0.:3.,0});f.cloth.renderBindings.push_back({0,index,index});
    StaticVertex vertex;vertex.position=point;vertex.normal={0,0,1};f.mesh.meshes[0].vertices.push_back(vertex);
}
Fixture Point(glm::dvec3 point){Fixture f;f.cloth.id="convex-point";f.cloth.settings.gravity={0,0,0};f.cloth.settings.damping=0;
    f.cloth.settings.localDamping=0;f.cloth.settings.animationDamping=0;f.cloth.settings.friction=0;
    f.cloth.settings.convergenceTolerance=0;f.cloth.settings.substeps=1;AddPoint(f,point);return f;}
void TransformAndQueries(){
    std::puts("Convex skin planes and triangle material contacts:");
    auto source=Box({.1,.3,.2});const auto transform=glm::translate(glm::dmat4(1),glm::dvec3(1,2,3))*
        glm::mat4_cast(glm::angleAxis(.6,glm::dvec3(0,0,1)))*glm::scale(glm::dmat4(1),glm::dvec3(2,.5,1.5));
    ClothWorldConvex shape;bool valid=BuildClothWorldConvex(source,transform,shape);
    double maximumPlaneError=0,maximumUnitError=0;
    for(std::size_t i=0;i<source.planes.size()&&valid;++i){const auto point=glm::dvec3(transform*glm::dvec4(source.planes[i].normal*source.planes[i].distance,1));
        maximumPlaneError=std::max(maximumPlaneError,std::abs(glm::dot(shape.planes[i].normal,point)-shape.planes[i].distance));
        maximumUnitError=std::max(maximumUnitError,std::abs(glm::length(shape.planes[i].normal)-1));}
    for(const auto& point:shape.vertices)for(const auto& plane:shape.planes)valid&=glm::dot(plane.normal,point)<=plane.distance+1e-11;
    Expect(valid&&maximumPlaneError<1e-11&&maximumUnitError<1e-12,"Affine inverse-transpose transforms preserve skin planes under rotation and nonuniform scale");
    const auto inside=ConvexPointContact(shape,glm::dvec3(transform*glm::dvec4(.02,.02,.02,1)));
    Expect(inside.hit&&inside.separation<0&&std::abs(glm::length(inside.normal)-1)<1e-12,"Interior points return a signed depth and normalized nearest escape face");
    source=Box({.3,.05,.3});BuildClothWorldConvex(source,glm::dmat4(1),shape);
    const glm::dvec3 a(-1,0,-1),b(1,0,-1),c(0,0,1);
    const auto contact=ConvexTriangleContact(shape,a,b,c);
    Expect(!ConvexPointContact(shape,a).hit&&!ConvexPointContact(shape,b).hit&&!ConvexPointContact(shape,c).hit&&
        contact.hit&&contact.separation<-.049&&Near(contact.point,a*contact.bary.x+b*contact.bary.y+c*contact.bary.z)&&
        std::abs(contact.bary.x+contact.bary.y+contact.bary.z-1)<1e-12&&glm::min(contact.bary.x,glm::min(contact.bary.y,contact.bary.z))>=-1e-12,
        "Triangle clipping detects a skin volume passing through a face whose three vertices are all outside");
    Expect(!ConvexTriangleContact(shape,a+glm::dvec3(0,.2,0),b+glm::dvec3(0,.2,0),c+glm::dvec3(0,.2,0),.008).hit,
        "Supporting skin planes reject nearby AABB candidates that do not touch the cloth");
    auto singular=transform;singular[0]=glm::dvec4(0);
    Expect(!BuildClothWorldConvex(source,singular,shape),"A singular body transform is rejected before geometric queries");
}
void RigidMotion(){
    std::puts("Rigid rotation sampling and complete swept bounds:");
    const auto source=Box({.04,.1,.04},{.3,0,0});const auto begin=glm::dmat4(1),
        end=glm::mat4_cast(glm::angleAxis(std::acos(-1.),glm::dvec3(0,1,0)));
    PreparedClothConvexMotion motion;ClothWorldConvex first,middle,last;
    bool valid=motion.Prepare(source,begin,end)&&motion.At(0,first)&&motion.At(.5,middle)&&motion.At(1,last);
    const auto expected=glm::mat4_cast(glm::angleAxis(std::acos(-1.)*.5,glm::dvec3(0,1,0)));
    double error=0;
    for(std::size_t i=0;i<source.vertices.size()&&valid;++i){error=std::max(error,glm::length(middle.vertices[i]-glm::dvec3(expected*glm::dvec4(source.vertices[i],1))));
        for(int axis=0;axis<3;++axis)valid&=middle.vertices[i][axis]>=motion.minimum[axis]-1e-12&&middle.vertices[i][axis]<=motion.maximum[axis]+1e-12;}
    Expect(valid&&error<1e-10&&std::abs(glm::length(middle.vertices[1]-middle.vertices[0])-.08)<1e-10,
        "Half-turn sampling preserves limb shape and follows a quaternion arc rather than a shortened endpoint chord");
    Expect(valid&&motion.travel>.8&&!ConvexBoundsOverlap({0,0,-.3},{0,0,-.3},first)&&!ConvexBoundsOverlap({0,0,-.3},{0,0,-.3},last)&&
        ConvexPointContact(middle,{0,0,-.3}).hit,"Swept bounds include a rotating limb that crosses cloth outside both endpoint bounds");
    ClothAsset cloth;cloth.convexes.push_back(source);cloth.convexes[0].bone=0;SkeletalPose pose;pose.global={end};
    const auto evaluated=EvaluateClothConvexes(cloth,pose);
    Expect(evaluated.size()==1&&Near(evaluated[0].vertices[0],last.vertices[0]),"Public convex evaluation follows the named animated bone");
    pose.global.clear();Expect(EvaluateClothConvexes(cloth,pose).empty(),"An absent animated body bone cannot silently produce an identity collider");
}
void PartitionContacts(){
    std::puts("Artificial hull partitions retain membership without introducing internal collision surfaces:");
    auto left=Box({.1,.2,.2},{-.09,0,0}),right=Box({.1,.2,.2},{.09,0,0});
    left.planes[0].collision=false;right.planes[1].collision=false;
    ClothWorldConvex a,b;bool valid=BuildClothWorldConvex(left,glm::dmat4(1),a)&&BuildClothWorldConvex(right,glm::dmat4(1),b);
    const auto point=ConvexPointContact(a,{0,.1,0},.008),other=ConvexPointContact(b,{0,.1,0},.008);
    const glm::dvec3 p(-.08,.1,-.05),q(.08,.1,-.05),r(0,.1,.05);
    const auto first=ConvexTriangleContact(a,p,q,r,.008),second=ConvexTriangleContact(b,p,q,r,.008);
    Expect(valid&&!a.planes[0].collision&&!b.planes[1].collision&&point.hit&&other.hit&&first.hit&&second.hit&&
        point.normal.y>.99&&other.normal.y>.99&&first.normal.y>.99&&second.normal.y>.99,
        "Overlapping skin pieces choose real outer faces for both point and triangle contacts");
    Expect(!ConvexPointContact(a,{.03,.1,0},.008).hit,"Disabled internal planes still reject points outside the piece's membership volume");
    auto f=Point(p);AddPoint(f,q);AddPoint(f,r);f.cloth.id="convex-internal-partitions";
    f.cloth.triangles={{{0,1,2}}};f.mesh.meshes[0].indices={0,1,2};f.cloth.convexes={left,right};
    ClothSimulator simulator;std::string error;valid=true;
    for(int i=0;i<24&&valid;++i)valid=simulator.Step(f.cloth,f.pose,f.mesh,1./60,f.cloth.settings,&error);
    double xError=0;if(valid)for(std::size_t i=0;i<simulator.Positions().size();++i)xError=std::max(xError,std::abs(simulator.Positions()[i].x-f.cloth.particles[i].restPosition.x));
    Expect(valid&&xError<1e-8&&simulator.Stats().maxPenetration<1e-7&&simulator.Stats().maxSurfacePenetration<1e-7&&simulator.Stats().collapsedTriangles==0,
        "Repeated overlap solving does not bounce cloth between neighboring artificial partition faces");
    for(auto& plane:left.planes)plane.collision=false;
    Expect(!BuildClothWorldConvex(left,glm::dmat4(1),a),"A body with no real skin collision face is rejected");
    // Equal geometric distances retain source face/candidate order across a
    // rigid transform instead of amplifying sub-ulp dot-product tie changes.
    auto box=Box({.1,.1,.1});BuildClothWorldConvex(box,glm::dmat4(1),a);
    const auto frame=glm::translate(glm::dmat4(1),glm::dvec3(.2,.5,.1))*glm::mat4_cast(glm::angleAxis(.4,glm::normalize(glm::dvec3(1,2,3))));
    BuildClothWorldConvex(box,frame,b);const auto transformedPoint=glm::dvec3(frame*glm::dvec4(0,0,0,1));
    const auto original=ConvexPointContact(a,{0,0,0}),transformed=ConvexPointContact(b,transformedPoint);
    const glm::dvec3 ta(-.2,0,-.2),tb(.2,0,-.2),tc(0,0,.2);
    const auto originalFace=ConvexTriangleContact(a,ta,tb,tc),transformedFace=ConvexTriangleContact(b,
        glm::dvec3(frame*glm::dvec4(ta,1)),glm::dvec3(frame*glm::dvec4(tb,1)),glm::dvec3(frame*glm::dvec4(tc,1)));
    Expect(original.hit&&transformed.hit&&original.plane==transformed.plane&&originalFace.hit&&transformedFace.hit&&
        originalFace.plane==transformedFace.plane&&Near(originalFace.bary,transformedFace.bary,1e-10),
        "Symmetric closest-face and clipped-contact ties retain the same material point under rigid rotation");
}
void SkinnedSupportMotion(){
    std::puts("Skin-weighted convex supports and rigid-frame deformation interpolation:");
    auto source=Box({.1,.1,.1});source.bone=0;source.planes.clear();
    for(const auto& triangle:source.triangles){const auto a=source.vertices[triangle[0]],b=source.vertices[triangle[1]],c=source.vertices[triangle[2]];
        const auto normal=glm::normalize(glm::cross(b-a,c-a));source.planes.push_back({normal,glm::dot(normal,a),true});}
    source.vertices.push_back({0,0,0}); // This neutral interior point can become an animated extremum.
    source.skin.resize(source.vertices.size());
    for(std::size_t i=0;i<source.vertices.size();++i)source.skin[i].push_back({0,source.vertices[i],1});
    source.skin[0]={{0,source.vertices[0]*.5,.5},{1,source.vertices[0]*.5,.5}};
    source.skin.back()={{1,{0,0,0},1}};
    ClothAsset cloth;cloth.convexes.push_back(source);SkeletalPose pose;pose.global={glm::dmat4(1),glm::dmat4(1)};
    const auto begin=EvaluateClothConvexes(cloth,pose);
    const auto rotation=glm::mat4_cast(glm::angleAxis(std::acos(-1.)*.5,glm::dvec3(0,1,0)));
    pose.global[0]=rotation;pose.global[1]=rotation*glm::translate(glm::dmat4(1),glm::dvec3(0,0,.5));
    const auto end=EvaluateClothConvexes(cloth,pose);
    bool valid=begin.size()==1&&end.size()==1;
    if(valid){for(const auto& vertex:end[0].vertices)for(const auto& plane:end[0].planes)valid&=glm::dot(plane.normal,vertex)<=plane.distance+1e-10;
        valid&=Near(end[0].vertices.back(),glm::dvec3(rotation*glm::dvec4(0,0,.5,1)))&&
            Near(end[0].vertices[0],glm::dvec3(rotation*glm::dvec4(source.vertices[0]+glm::dvec3(0,0,.25),1)))&&
            end[0].planes.size()==source.planes.size()+6;
        for(std::size_t i=source.planes.size();i<end[0].planes.size();++i)valid&=!end[0].planes[i].collision;}
    Expect(valid,"Weighted source supports, including neutral interior vertices, remain inside every animated support plane");
    PreparedClothConvexMotion motion;ClothWorldConvex middle,last;
    valid=valid&&motion.Prepare(source,begin[0],end[0])&&motion.At(.5,middle)&&motion.At(1,last);
    if(valid){const auto halfway=glm::mat4_cast(glm::angleAxis(std::acos(-1.)*.25,glm::dvec3(0,1,0)));
        valid&=Near(middle.vertices.back(),glm::dvec3(halfway*glm::dvec4(0,0,.25,1)),1e-10)&&
            Near(middle.vertices[0],glm::dvec3(halfway*glm::dvec4(source.vertices[0]+glm::dvec3(0,0,.125),1)),1e-10)&&
            last.vertices==end[0].vertices;
        for(const auto& vertex:middle.vertices)for(const auto& plane:middle.planes)valid&=glm::dot(plane.normal,vertex)<=plane.distance+1e-10;}
    Expect(valid,"Intermediate supports combine the dominant bone's quaternion arc with local weighted deformation and exact endpoints");
    pose.global.resize(1);Expect(EvaluateClothConvexes(cloth,pose).empty(),"A missing secondary skin bone invalidates the whole convex instead of silently discarding its weight");
}
ClothConvexRuntimeLayout SlowLayoutReference(const ClothConvexCollider& source,std::uint32_t budget){
    ClothConvexRuntimeLayout result;result.budget=source.skin.empty()?0:budget;
    if(!result.budget){for(std::size_t i=0;i<source.planes.size();++i)result.sourcePlanes.push_back(i);return result;}
    std::vector<std::size_t> exterior,partitions;
    const double tolerance=1e-10*std::max(1.0,source.vertices.empty()?0.:glm::length(source.vertices.front()));
    for(std::size_t i=0;i<source.planes.size();++i){if(source.planes[i].collision){exterior.push_back(i);continue;}
        bool duplicate=false;for(auto other:partitions)if(glm::dot(source.planes[i].normal,source.planes[other].normal)>1-1e-12&&
            std::abs(source.planes[i].distance-source.planes[other].distance)<=tolerance){duplicate=true;break;}
        if(!duplicate)partitions.push_back(i);}
    if(exterior.size()>result.budget){std::vector<std::uint8_t> selected(source.planes.size(),0);std::vector<std::size_t> chosen;
        auto choose=[&](std::size_t index){if(!selected[index]&&chosen.size()<result.budget){selected[index]=1;chosen.push_back(index);}};
        for(int axis=0;axis<3;++axis)for(double sign:{1.,-1.}){double best=-std::numeric_limits<double>::infinity();auto index=exterior.front();
            for(auto candidate:exterior){const double score=source.planes[candidate].normal[axis]*sign;if(score>best+1e-14){best=score;index=candidate;}}choose(index);}
        while(chosen.size()<result.budget){double best=-1;auto index=exterior.front();
            for(auto candidate:exterior)if(!selected[candidate]){double closest=-1;
                for(auto other:chosen)closest=std::max(closest,glm::dot(source.planes[candidate].normal,source.planes[other].normal));
                const double score=1-closest;if(score>best+1e-14){best=score;index=candidate;}}choose(index);}
        exterior=std::move(chosen);}
    result.sourcePlanes=std::move(exterior);result.sourcePlanes.insert(result.sourcePlanes.end(),partitions.begin(),partitions.end());
    std::sort(result.sourcePlanes.begin(),result.sourcePlanes.end());return result;
}
void RuntimePlaneLayout(){
    std::puts("Deterministic skinned source-face direction budgets retain supports and CCD face identities:");
    ClothConvexCollider source;source.name="dense-skinned-ellipsoid";source.bone=0;
    constexpr std::uint32_t rings=5,sectors=12;const double pi=std::acos(-1.);
    source.vertices.push_back({0,.22,0});
    for(std::uint32_t ring=1;ring<=rings;++ring)for(std::uint32_t sector=0;sector<sectors;++sector){const double latitude=pi*ring/(rings+1),longitude=2*pi*sector/sectors;
        source.vertices.push_back({.1*std::sin(latitude)*std::cos(longitude),.22*std::cos(latitude),.075*std::sin(latitude)*std::sin(longitude)});}
    const auto bottom=std::uint32_t(source.vertices.size());source.vertices.push_back({0,-.22,0});
    auto face=[&](std::uint32_t a,std::uint32_t b,std::uint32_t c){auto normal=glm::normalize(glm::cross(source.vertices[b]-source.vertices[a],source.vertices[c]-source.vertices[a]));
        if(glm::dot(normal,source.vertices[a])<0){std::swap(b,c);normal=-normal;}source.triangles.push_back({a,b,c});source.planes.push_back({normal,glm::dot(normal,source.vertices[a]),true});};
    for(std::uint32_t sector=0;sector<sectors;++sector)face(0,1+sector,1+(sector+1)%sectors);
    for(std::uint32_t ring=0;ring+1<rings;++ring)for(std::uint32_t sector=0;sector<sectors;++sector){const auto a=1+ring*sectors+sector,b=1+ring*sectors+(sector+1)%sectors,c=a+sectors,d=b+sectors;face(a,b,d);face(a,d,c);}
    for(std::uint32_t sector=0;sector<sectors;++sector)face(bottom,1+(rings-1)*sectors+sector,1+(rings-1)*sectors+(sector+1)%sectors);
    source.skin.resize(source.vertices.size());for(std::size_t i=0;i<source.vertices.size();++i){const auto point=source.vertices[i];
        if(i%3)source.skin[i]={{0,point,1}};else source.skin[i]={{0,point*.75,.75},{1,point*.25,.25}};}
    bool sameLayout=true;for(std::uint32_t budget:{0u,4u,5u,8u,32u,48u,64u,96u,256u})
        sameLayout&=CompileClothConvexLayout(source,budget).sourcePlanes==SlowLayoutReference(source,budget).sourcePlanes;
    Expect(sameLayout,"Incremental O(P*K) angular selection exactly matches the previous O(P*K*K) seed/tie/source-index reference");
    const auto full=CompileClothConvexLayout(source,0),reduced=CompileClothConvexLayout(source,48),repeat=CompileClothConvexLayout(source,48),smaller=CompileClothConvexLayout(source,32);
    bool valid=full.sourcePlanes.size()==source.planes.size()&&reduced.sourcePlanes.size()==48&&smaller.sourcePlanes.size()==32&&reduced.sourcePlanes==repeat.sourcePlanes&&
        std::is_sorted(reduced.sourcePlanes.begin(),reduced.sourcePlanes.end());
    ClothAsset cloth;cloth.convexes.push_back(source);SkeletalPose pose;pose.global={glm::dmat4(1),glm::dmat4(1)};
    auto evaluated=EvaluateClothConvexes(cloth,pose);valid&=evaluated.size()==1;
    if(!valid){Expect(false,"A dense source compiles stable 32/48 layouts without changing its complete skin/support asset");return;}
    auto neutral=evaluated[0];valid=RefitClothWorldConvex(source,neutral,&reduced);
    const auto rotation=glm::mat4_cast(glm::angleAxis(.8,glm::normalize(glm::dvec3(1,2,3))));pose.global[0]=rotation;
    pose.global[1]=rotation*glm::mat4_cast(glm::angleAxis(.25,glm::dvec3(0,0,1)));
    evaluated=EvaluateClothConvexes(cloth,pose);valid&=evaluated.size()==1;if(!valid){Expect(false,"Skinned layout evaluation remains finite after a mixed-weight pose");return;}
    auto complete=evaluated[0],subset=evaluated[0];valid=RefitClothWorldConvex(source,complete,&full)&&RefitClothWorldConvex(source,subset,&reduced);
    auto scalarSupport=evaluated[0],vectorSupport=evaluated[0];
    {ScopedConvexSimd mode("0");valid&=RefitClothWorldConvex(source,scalarSupport,&reduced);}
    {ScopedConvexSimd mode("1");valid&=RefitClothWorldConvex(source,vectorSupport,&reduced);}
    Expect(valid&&SameShape(scalarSupport,vectorSupport),"Packed support refit preserves every world shape field bit-for-bit across scalar/SIMD modes");
    for(std::size_t i=0;i<reduced.sourcePlanes.size()&&valid;++i){const auto original=reduced.sourcePlanes[i];
        valid&=Near(subset.planes[i].normal,complete.planes[original].normal,1e-12)&&std::abs(subset.planes[i].distance-complete.planes[original].distance)<1e-12;}
    for(const auto& vertex:subset.vertices)for(const auto& plane:subset.planes)valid&=glm::dot(plane.normal,vertex)<=plane.distance+1e-10;
    Expect(valid&&subset.vertices==complete.vertices&&subset.planes.size()==54,
        "Budgeting changes only selected source faces; their deformed normals and support distances exactly match the full reference");
    ClothWorldConvex warm;valid=BuildClothWorldConvex(source,glm::dmat4(1),warm,&reduced)&&warm.planes.size()==subset.planes.size();
    if(valid)for(std::size_t i=0;i<reduced.sourcePlanes.size();++i)
        valid&=Near(warm.planes[i].normal,neutral.planes[i].normal,1e-12)&&std::abs(warm.planes[i].distance-neutral.planes[i].distance)<1e-12;
    PreparedClothConvexMotion motion;ClothWorldConvex middle,end;valid=valid&&motion.Prepare(source,neutral,subset,&reduced)&&motion.At(.5,middle)&&motion.At(1,end);
    Expect(valid&&middle.planes.size()==warm.planes.size()&&end.planes.size()==warm.planes.size()&&end.vertices==subset.vertices&&motion.layout.sourcePlanes==reduced.sourcePlanes,
        "Neutral warmup, intermediate CCD shapes and exact endpoints share one source-face layout");
    auto partition=Box({.1,.2,.1});partition.bone=0;partition.planes.clear();
    for(const auto& triangle:partition.triangles){const auto a=partition.vertices[triangle[0]],b=partition.vertices[triangle[1]],c=partition.vertices[triangle[2]];const auto normal=glm::normalize(glm::cross(b-a,c-a));partition.planes.push_back({normal,glm::dot(normal,a),true});}
    partition.planes[0].collision=partition.planes[1].collision=false;partition.skin.resize(partition.vertices.size());for(std::size_t i=0;i<partition.vertices.size();++i)partition.skin[i]={{0,partition.vertices[i],1}};
    const auto compact=CompileClothConvexLayout(partition,48);std::size_t falsePlanes=0;for(auto index:compact.sourcePlanes)falsePlanes+=!partition.planes[index].collision;
    Expect(falsePlanes==1&&compact.sourcePlanes.size()==11&&CompileClothConvexLayout(partition,0).sourcePlanes.size()==12,
        "Only coincident artificial planes deduplicate; full reference preserves every authored face");
    sameLayout=true;for(std::uint32_t budget:{0u,4u,5u,8u,48u})sameLayout&=CompileClothConvexLayout(partition,budget).sourcePlanes==SlowLayoutReference(partition,budget).sourcePlanes;
    Expect(sameLayout,"Incremental angular selection retains the reference's artificial-plane deduplication and small-budget ties");
}
void ProjectionInducedSurfaceSweep(){
    std::puts("Surface CCD includes preceding particle and body projection motion:");
    auto f=Point({-1,-1,0});AddPoint(f,{1,-1,0});AddPoint(f,{0,1,0});f.cloth.id="convex-projected-surface-sweep";
    f.cloth.settings.iterations=1;f.cloth.triangles={{{0,1,2}}};f.mesh.meshes[0].indices={0,1,2};
    auto first=Box({.04,.04,.5},{-1,-1,0});for(auto& plane:first.planes)plane.collision=false;first.planes[4].collision=true;
    auto second=Box({.015,.015,.015},{0,-1./3,.1});f.cloth.convexes={first,second};
    ClothWorldConvex obstacle;BuildClothWorldConvex(second,glm::dmat4(1),obstacle);
    const auto start=ConvexTriangleContact(obstacle,{-1,-1,0},{1,-1,0},{0,1,0},.008),
        endpoint=ConvexTriangleContact(obstacle,{-1,-1,.508},{1,-1,0},{0,1,0},.008);
    ClothSimulator simulator;std::string error;const bool valid=simulator.Step(f.cloth,f.pose,f.mesh,1./60,f.cloth.settings,&error);
    Expect(!start.hit&&!endpoint.hit&&valid&&simulator.Stats().contacts>=2&&simulator.Stats().collapsedTriangles==0,
        "A first body's particle projection cannot tunnel the face across a second body before surface CCD refreshes its trajectory");
}
void SeparatingPlaneProofs(){
    std::puts("Separating plane certificates are distinct from collision planes:");
    auto source=Box({.1,.2,.1});ClothWorldConvex shape;
    BuildClothWorldConvex(source,glm::mat4_cast(glm::angleAxis(std::acos(-1.)*.25,glm::dvec3(0,1,0))),shape);
    const auto point=ConvexPointContact(shape,{.13,0,.13},.008),
        triangle=ConvexTriangleContact(shape,{.13,0,.13},{.132,.002,.13},{.13,.002,.132},.008),
        inside=ConvexPointContact(shape,{0,0,0},.008);
    bool valid=!point.hit&&!triangle.hit&&point.separatingPlane<shape.planes.size()&&triangle.separatingPlane<shape.planes.size()&&
        inside.hit&&inside.separatingPlane==std::numeric_limits<std::size_t>::max();
    if(valid){const auto& p=shape.planes[point.separatingPlane];const auto& t=shape.planes[triangle.separatingPlane];
        valid&=glm::dot(p.normal,glm::dvec3(.13,0,.13))-p.distance>.008&&
            glm::dot(t.normal,glm::dvec3(.13,0,.13))-t.distance-.008>0&&
            glm::dot(t.normal,glm::dvec3(.132,.002,.13))-t.distance-.008>0&&
            glm::dot(t.normal,glm::dvec3(.13,.002,.132))-t.distance-.008>0;}
    Expect(valid,"Point and original-face separation return reusable plane proofs without setting a collision hit");
    source=Box({1,1,1});BuildClothWorldConvex(source,glm::dmat4(1),shape);
    const auto composite=ConvexTriangleContact(shape,{.8,1.4,0},{1.4,.8,0},{1.5,1.5,0});
    Expect(!composite.hit&&composite.separatingPlane==std::numeric_limits<std::size_t>::max(),
        "An empty composite clip does not invent a separating plane for the original triangle");
}
void SimdQueryEquivalence(){
    std::puts("Exact AVX2 plane classification, scalar clipping and complete contact proofs:");
    bool valid=true,enabled=false;std::size_t queries=0,hits=0,proofs=0;
    std::uint64_t randomState=0x98a373cb264f120dULL;
    auto random=[&](){randomState^=randomState>>12;randomState^=randomState<<25;randomState^=randomState>>27;
        return double((randomState*2685821657736338717ULL)>>11)*0x1.0p-53;};
    const glm::dvec3 half(.12,.23,.09);
    for(const std::size_t count:{6u,7u,8u,9u,31u,32u,33u,95u,96u,97u,255u,256u,257u,262u}){
        auto source=Box(half);source.vertices.clear();
        for(int axis=0;axis<3;++axis)for(double sign:{1.,-1.}){glm::dvec3 point(0);point[axis]=half[axis]*sign;source.vertices.push_back(point);}
        for(int i=0;i<96;++i){const double y=1-2*(i+.5)/96,longitude=i*2.39996322972865332;
            source.vertices.push_back(half*glm::dvec3(std::sqrt(1-y*y)*std::cos(longitude),y,std::sqrt(1-y*y)*std::sin(longitude)));}
        for(std::size_t i=6;i<std::min<std::size_t>(count,256);++i){const auto normal=glm::normalize(glm::dvec3(2*random()-1,2*random()-1,2*random()-1));
            double distance=-std::numeric_limits<double>::infinity();for(const auto point:source.vertices)distance=std::max(distance,glm::dot(normal,point));
            source.planes.push_back({normal,distance,i%5!=0});}
        // False faces must participate in classification and clipping while
        // never winning the outward normal, including deep interior ties.
        source.planes[1].collision=false;
        const auto transform=glm::translate(glm::dmat4(1),glm::dvec3(.17,-.31,.08))*
            glm::mat4_cast(glm::angleAxis(.731,glm::normalize(glm::dvec3(1,3,2))))*glm::scale(glm::dmat4(1),glm::dvec3(1.3,.8,1.1));
        ClothWorldConvex shape;valid&=BuildClothWorldConvex(source,transform,shape);
        while(shape.planes.size()<count){auto extra=shape.planes[shape.planes.size()%6];extra.collision=false;shape.planes.push_back(extra);}
        const auto original=shape;PreparedClothConvexQueries on,off;
        {ScopedConvexSimd mode("0");off.Prepare(shape);valid&=!off.simd&&off.packets.empty();}
        {ScopedConvexSimd mode("1");on.Prepare(shape);enabled|=on.simd;valid&=on.simd==ClothConvexSimdAvailable();}
        auto compare=[&](glm::dvec3 point,glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,double padding){
            const auto p=ConvexPointContact(shape,point,padding),t=ConvexTriangleContact(shape,a,b,c,padding);
            const auto pv=ConvexPointContact(shape,point,padding,&on),tv=ConvexTriangleContact(shape,a,b,c,padding,&on);
            const glm::dvec3 offsets(ConvexKinematicContactOffset(shape,a,padding),padding,padding);
            valid&=SameContact(p,pv)&&SameContact(t,tv)&&SameContact(p,ConvexPointContact(shape,point,padding,&off))&&
                SameContact(t,ConvexTriangleContact(shape,a,b,c,padding,&off))&&
                SameContact(t,ConvexTriangleContactOffsets(shape,a,b,c,glm::dvec3(padding),padding,&on))&&
                SameContact(ConvexTriangleContactOffsets(shape,a,b,c,offsets,padding),
                    ConvexTriangleContactOffsets(shape,a,b,c,offsets,padding,&on));queries+=2;hits+=p.hit+t.hit;
            proofs+=(p.separatingPlane<shape.planes.size())+(t.separatingPlane<shape.planes.size());
        };
        auto world=[&](glm::dvec3 point){return glm::dvec3(transform*glm::dvec4(point,1));};
        // Random triangles include slivers, deep intersections, fully contained
        // faces and AABB-overlapping triangles rejected only by a skin plane.
        for(int sample=0;sample<180&&valid;++sample){auto local=[&](){return half*glm::dvec3(4*random()-2,4*random()-2,4*random()-2);};
            const auto point=world(local()),a=world(local()),b=world(local()),c=world(local());
            compare(point,a,b,c,sample%3==0?.008:0.);
        }
        const auto center=world({0,0,0});compare(center,world({-.001,0,0}),world({.001,0,0}),world({0,.001,0}),0);
        compare(center,world({-.5,0,-.5}),world({.5,0,-.5}),world({0,0,.5}),.008);
        for(std::size_t face=0;face<std::min<std::size_t>(shape.planes.size(),12);++face){const auto& plane=shape.planes[face];
            const auto axis=std::abs(plane.normal.x)<.8?glm::dvec3(1,0,0):glm::dvec3(0,1,0),tangent=glm::normalize(glm::cross(plane.normal,axis))*.02;
            for(double padding:{0.,.008})for(double delta:{-1e-15,-0.,0.,1e-15}){const auto point=plane.normal*(plane.distance+padding+delta);
                compare(point,point-tangent,point+tangent,point+glm::cross(plane.normal,tangent),padding);}}
        // The cache is bound to its immutable shape, so an unrelated copy must
        // fall back safely even if it has the same number of planes.
        auto other=shape;valid&=!on.Matches(other)&&SameContact(ConvexPointContact(other,center),ConvexPointContact(other,center,0,&on));
        valid&=SameShape(shape,original);
    }
    // Exact zero and one-ULP boundary values exercise ordered comparisons and
    // signed zero without the roundoff of an oblique transformed skin plane.
    ClothWorldConvex box;BuildClothWorldConvex(Box({1,1,1}),glm::dmat4(1),box);box.planes[0].collision=false;
    PreparedClothConvexQueries prepared;{ScopedConvexSimd mode("1");prepared.Prepare(box);}
    for(double padding:{0.,.125})for(double x:{-0.,0.,1.+padding,std::nextafter(1.+padding,0.),std::nextafter(1.+padding,2.)}){
        const glm::dvec3 p(x,0,0),a(x,-.2,-.2),b(x,.2,-.2),c(x,0,.2);
        valid&=SameContact(ConvexPointContact(box,p,padding),ConvexPointContact(box,p,padding,&prepared))&&
            SameContact(ConvexTriangleContact(box,a,b,c,padding),ConvexTriangleContact(box,a,b,c,padding,&prepared));}
    // The first two partial cuts empty the polygon. A later separating lane in
    // the same packet must not overwrite that composite-empty no-proof result.
    auto clipped=box;clipped.planes={{{1,0,0},0,true},{{0,1,0},0,false},{{-1,0,0},-3,true},{{0,-1,0},1,true}};
    {ScopedConvexSimd mode("1");prepared.Prepare(clipped);}
    const glm::dvec3 a(2,-1,0),b(-1,2,0),c(2,2,0);bool scalarEmpty=false,simdEmpty=false;
    const auto composite=ConvexTriangleContact(clipped,a,b,c,0,nullptr,&scalarEmpty),
        vectorComposite=ConvexTriangleContact(clipped,a,b,c,0,&prepared,&simdEmpty);
    valid&=!composite.hit&&composite.separatingPlane==std::numeric_limits<std::size_t>::max()&&
        scalarEmpty&&simdEmpty&&SameContact(composite,vectorComposite);
    scalarEmpty=simdEmpty=true;
    {ScopedConvexSimd mode("1");prepared.Prepare(box);}
    const auto separatedScalar=ConvexTriangleContact(box,{1.05,0,0},{1.05,.01,0},{1.05,0,.01},0,nullptr,&scalarEmpty),
        separatedSimd=ConvexTriangleContact(box,{1.05,0,0},{1.05,.01,0},{1.05,0,.01},0,&prepared,&simdEmpty);
    valid&=!scalarEmpty&&!simdEmpty&&SameContact(separatedScalar,separatedSimd);
    std::array<ClothConvexContact,12> expected;for(std::size_t i=0;i<expected.size();++i){const double y=-1.1+i*.2;
        expected[i]=ConvexTriangleContact(box,{-2,y,-2},{2,y,-2},{0,y,2},.125);}
    {ScopedConvexSimd mode("1");prepared.Prepare(box);}
    std::array<bool,4> threadResults{true,true,true,true};std::array<std::thread,4> threads;
    for(std::size_t worker=0;worker<threads.size();++worker)threads[worker]=std::thread([&,worker](){
        for(int iteration=0;iteration<80;++iteration){const auto i=(iteration+worker)%expected.size();const double y=-1.1+i*.2;
            threadResults[worker]&=SameContact(expected[i],ConvexTriangleContact(box,{-2,y,-2},{2,y,-2},{0,y,2},.125,&prepared));
            threadResults[worker]&=SameContact(expected[i],ConvexTriangleContact(box,{-2,y,-2},{2,y,-2},{0,y,2},.125));}});
    for(auto& thread:threads)thread.join();for(bool result:threadResults)valid&=result;
    Expect(valid&&hits>0&&proofs>0,"Scalar/AVX2 point and triangle contacts are bit-identical, including every barycentric/normal/depth/proof field");
    std::printf("    convex AVX2 %s; %zu direct queries, %zu hits, %zu separation proofs; all shape fields unchanged\n",enabled?"enabled":"unavailable (scalar fallback)",queries,hits,proofs);
}
void SeparatingCacheEquivalence(){
    std::puts("Exact end-query separating plane cache across interacting moving body poses:");
    Fixture f;f.cloth.id="convex-separating-cache-equivalence";f.cloth.settings.gravity={0,-.6,0};f.cloth.settings.convergenceTolerance=0;
    constexpr std::uint32_t width=6,height=7;
    for(std::uint32_t row=0;row<height;++row)for(std::uint32_t column=0;column<width;++column)
        AddPoint(f,{(double(column)-2.5)*.1,1-double(row)*.1,0},row==0);
    std::set<std::pair<std::uint32_t,std::uint32_t>> edges;
    for(std::uint32_t row=0;row+1<height;++row)for(std::uint32_t column=0;column+1<width;++column){const auto a=row*width+column,b=a+1,c=a+width,d=c+1;
        f.cloth.triangles.push_back({a,c,b});f.cloth.triangles.push_back({b,c,d});}
    for(const auto& triangle:f.cloth.triangles)for(int edge=0;edge<3;++edge){auto a=triangle[edge],b=triangle[(edge+1)%3];if(a>b)std::swap(a,b);edges.insert({a,b});}
    for(const auto& [a,b]:edges)f.cloth.stretch.push_back({a,b,glm::length(f.cloth.particles[a].restPosition-f.cloth.particles[b].restPosition)});
    f.cloth.convexes={Box({.1,.22,.055}),Box({.09,.16,.05}),Box({.07,.12,.045})};
    for(int i=0;i<3;++i)f.cloth.convexes[i].bone=i;
    f.cloth.convexes[0].planes[3].collision=false;f.pose.global.resize(3,glm::dmat4(1));
    ScopedCompositeCache enableComposite("1");
    ClothSimulator uncached,cached,scalarConvex,ungated,noComposite;std::string error;bool valid=true,simdValid=true,gateValid=true,compositeValid=true;double maximumDifference=0;std::size_t contacts=0;
    for(int frame=0;frame<90&&valid;++frame){const double phase=frame*.07;
        f.pose.global[0]=glm::translate(glm::dmat4(1),glm::dvec3(.22*std::sin(phase),.65,-.16+.14*std::sin(phase)))*
            glm::mat4_cast(glm::angleAxis(.7*std::sin(phase),glm::dvec3(0,1,0)));
        f.pose.global[1]=glm::translate(glm::dmat4(1),glm::dvec3(-.2*std::cos(phase),.61,-.14+.13*std::cos(phase)))*
            glm::mat4_cast(glm::angleAxis(.6*std::cos(phase),glm::dvec3(0,0,1)))*glm::mat4_cast(glm::angleAxis(-.4,glm::dvec3(0,1,0)));
        f.pose.global[2]=glm::translate(glm::dmat4(1),glm::dvec3(.27*std::cos(phase*.7),.5,.03))*
            glm::mat4_cast(glm::angleAxis(.45*std::sin(phase),glm::dvec3(1,0,0)));
        {ScopedSeparatingCache mode("0");ScopedConvexSimd simd("1");ScopedCcdSampleBounds bounds("1");valid=uncached.Step(f.cloth,f.pose,f.mesh,1./60,f.cloth.settings,&error);}
        {ScopedSeparatingCache mode("1");ScopedConvexSimd simd("1");ScopedCcdSampleBounds bounds("1");valid=valid&&cached.Step(f.cloth,f.pose,f.mesh,1./60,f.cloth.settings,&error);}
        {ScopedSeparatingCache mode("1");ScopedConvexSimd simd("0");ScopedCcdSampleBounds bounds("1");simdValid=simdValid&&scalarConvex.Step(f.cloth,f.pose,f.mesh,1./60,f.cloth.settings,&error);}
        {ScopedSeparatingCache mode("1");ScopedConvexSimd simd("1");ScopedCcdSampleBounds bounds("0");gateValid=gateValid&&ungated.Step(f.cloth,f.pose,f.mesh,1./60,f.cloth.settings,&error);}
        {ScopedSeparatingCache mode("1");ScopedConvexSimd simd("1");ScopedCcdSampleBounds bounds("1");ScopedCompositeCache composite("0");compositeValid=compositeValid&&noComposite.Step(f.cloth,f.pose,f.mesh,1./60,f.cloth.settings,&error);}
        if(!valid)break;
        valid&=uncached.Positions()==cached.Positions();
        for(std::size_t i=0;i<uncached.Positions().size();++i)maximumDifference=std::max(maximumDifference,glm::length(uncached.Positions()[i]-cached.Positions()[i]));
        const auto& a=uncached.Stats();const auto& b=cached.Stats();contacts+=b.contacts;
        valid&=a.steps==b.steps&&a.resets==b.resets&&a.constraintIterations==b.constraintIterations&&a.contacts==b.contacts&&
            a.maxPenetration==b.maxPenetration&&a.maxSurfacePenetration==b.maxSurfacePenetration&&a.maxAttachmentError==b.maxAttachmentError&&
            a.maxDynamicEdgeError==b.maxDynamicEdgeError&&a.dynamicEdgeError95==b.dynamicEdgeError95&&a.collapsedTriangles==b.collapsedTriangles;
        const auto& scalar=scalarConvex.Stats();simdValid&=scalarConvex.Positions().size()==cached.Positions().size();
        for(std::size_t i=0;i<scalarConvex.Positions().size()&&simdValid;++i)simdValid&=SameBits(scalarConvex.Positions()[i],cached.Positions()[i]);
        simdValid&=scalar.steps==b.steps&&scalar.resets==b.resets&&scalar.constraintIterations==b.constraintIterations&&scalar.contacts==b.contacts&&
            SameBits(scalar.maxPenetration,b.maxPenetration)&&SameBits(scalar.maxSurfacePenetration,b.maxSurfacePenetration)&&
            SameBits(scalar.maxAttachmentError,b.maxAttachmentError)&&SameBits(scalar.maxDynamicEdgeError,b.maxDynamicEdgeError)&&
            SameBits(scalar.dynamicEdgeError95,b.dynamicEdgeError95)&&scalar.collapsedTriangles==b.collapsedTriangles&&
            SameBits(scalar.maxResolvableSurfacePenetration,b.maxResolvableSurfacePenetration)&&
            SameBits(scalar.maxKinematicColliderPenetration,b.maxKinematicColliderPenetration);
        gateValid&=ungated.Positions().size()==cached.Positions().size()&&SamePhysicsStats(ungated.Stats(),b);
        for(std::size_t i=0;i<ungated.Positions().size()&&gateValid;++i)gateValid&=SameBits(ungated.Positions()[i],cached.Positions()[i]);
        compositeValid&=noComposite.Positions().size()==cached.Positions().size()&&SamePhysicsStats(noComposite.Stats(),b);
        for(std::size_t i=0;i<noComposite.Positions().size()&&compositeValid;++i)compositeValid&=SameBits(noComposite.Positions()[i],cached.Positions()[i]);
    }
    Expect(valid&&contacts>0&&maximumDifference==0,"Cache ON/OFF produces bit-identical particles, contacts and diagnostics over 90 interacting moving-body poses");
    Expect(valid&&simdValid&&contacts>0,"Convex SIMD ON/OFF produces bit-identical particles, contacts and diagnostics over the same 90 poses");
    Expect(valid&&gateValid&&contacts>0,"Discrete sample envelope ON/OFF preserves every particle and all non-timing physics statistics over 90 poses");
    Expect(valid&&compositeValid&&contacts>0,"Composite certificate cache ON/OFF preserves every particle and every non-timing statistic over 90 interacting poses");
    if(!valid)std::printf("    cache diagnostic: %s; maximum particle difference %.12g\n",error.c_str(),maximumDifference);
}
void CompositeSeparationProofs(){
    std::puts("Guarded nonnegative two-plane certificates never alter contact semantics:");
    ClothWorldConvex shape;BuildClothWorldConvex(Box({1,1,1}),glm::dmat4(1),shape);
    const glm::dvec3 a(.8,1.4,0),b(1.4,.8,0),c(1.5,1.5,0);constexpr double margin=.003;
    bool empty=false;std::uint16_t plane=0;const auto raw=ConvexTriangleContact(shape,a,b,c,margin,nullptr,&empty,&plane);
    ClothConvexCompositeSeparator proof;
    bool valid=!raw.hit&&empty&&raw.separatingPlane==std::numeric_limits<std::size_t>::max()&&
        BuildConvexCompositeSeparator(shape,plane,a,b,c,margin,proof)&&ConvexCompositeSeparates(shape,proof,a,b,c,margin);
    Expect(valid&&sizeof(proof)<=16,"A true composite-empty face supplies a bounded two-plane certificate without inventing a single-plane contact proof");
    const auto moved=glm::dvec3(-.3,-.3,0);
    Expect(!ConvexCompositeSeparates(shape,proof,a+moved,b+moved,c+moved,margin)&&
        ConvexTriangleContact(shape,a+moved,b+moved,c+moved,margin).hit,
        "Fresh current-vertex verification invalidates the certificate after another body's correction creates contact");
    auto cut=shape;cut.planes[0].collision=false;ClothConvexCompositeSeparator cutProof;
    const auto cutRaw=ConvexTriangleContact(cut,a,b,c,margin,nullptr,&empty,&plane);
    Expect(!cutRaw.hit&&empty&&BuildConvexCompositeSeparator(cut,plane,a,b,c,margin,cutProof)&&
        ConvexCompositeSeparates(cut,cutProof,a,b,c,margin),
        "Artificial cut faces may certify membership separation but never become an outward contact normal");
    const glm::dvec3 ta(.8,1.206,0),tb(1.206,.8,0),tc(1.003,1.003,0);
    Expect(!ConvexCompositeSeparates(shape,proof,ta,tb,tc,margin),"Tangent global-margin ties are uncertain and always fall back to the original full query");
    const glm::dvec3 ma(.6,1.2,0),mb(1.2,.6,0),mc(1.3,1.3,0);ClothConvexCompositeSeparator mixedProof;
    const auto mixed=ConvexTriangleContactOffsets(shape,ma,mb,mc,glm::dvec3(-.2),margin,nullptr,&empty,&plane);
    Expect(!mixed.hit&&empty&&ConvexTriangleContact(shape,ma,mb,mc,margin).hit&&
        !BuildConvexCompositeSeparator(shape,plane,ma,mb,mc,margin,mixedProof),
        "Negative pin offsets cannot certify separation when the original global-thickness hull still intersects the triangle");
    const glm::dvec3 ha(1.4,.9,.9),hb(.9,1.4,.9),hc(.9,.9,1.4);ClothConvexCompositeSeparator threeProof;
    const auto three=ConvexTriangleContact(shape,ha,hb,hc,margin,nullptr,&empty,&plane);
    Expect(!three.hit&&empty&&!BuildConvexCompositeSeparator(shape,plane,ha,hb,hc,margin,threeProof),
        "A three-plane-only empty intersection is honestly rejected by the bounded two-plane search");
    const auto transform=glm::translate(glm::dmat4(1),glm::dvec3(9e6,-2e6,1e6))*
        glm::mat4_cast(glm::angleAxis(.41,glm::normalize(glm::dvec3(1,2,3))));
    ClothWorldConvex large;BuildClothWorldConvex(Box({1,1,1}),transform,large);
    auto world=[&](glm::dvec3 point){return glm::dvec3(transform*glm::dvec4(point,1));};
    const auto la=world(a),lb=world(b),lc=world(c);ClothConvexCompositeSeparator largeProof;
    const auto largeContact=ConvexTriangleContact(large,la,lb,lc,margin,nullptr,&empty,&plane);
    const bool found=empty&&BuildConvexCompositeSeparator(large,plane,la,lb,lc,margin,largeProof);
    Expect(!largeContact.hit&&(!found||(ConvexCompositeSeparates(large,largeProof,la,lb,lc,margin)&&
        !ConvexCompositeSeparates(large,largeProof,world(ta),world(tb),world(tc),margin))),
        "At near-1e7 coordinates, scale-aware cancellation guards reject boundary ties or safely decline the certificate");
    plane=0;empty=true;auto invalid=shape;invalid.planes.clear();ConvexTriangleContact(invalid,a,b,c,margin,nullptr,&empty,&plane);
    valid=!empty&&plane==std::numeric_limits<std::uint16_t>::max();
    auto capacity=shape;capacity.planes.resize(263,shape.planes.front());plane=0;empty=true;ConvexTriangleContact(capacity,a,b,c,margin,nullptr,&empty,&plane);
    Expect(valid&&!empty&&plane==std::numeric_limits<std::uint16_t>::max(),"Invalid/capacity query failures never masquerade as a geometric empty-clip certificate seed");
}
void SharedSampleQueryEquivalence(){
    std::puts("Immutable shared CCD query packets survive same-size overwrite, regridding and stationary bodies:");
    auto source=Box({.12,.08,.1},{.2,0,0});source.planes.clear();
    for(const auto& triangle:source.triangles){const auto a=source.vertices[triangle[0]],b=source.vertices[triangle[1]],c=source.vertices[triangle[2]];
        const auto normal=glm::normalize(glm::cross(b-a,c-a));source.planes.push_back({normal,glm::dot(normal,a),true});}
    source.planes[0].collision=source.planes[1].collision=false;source.vertices.push_back({.2,0,0});
    source.skin.resize(source.vertices.size());for(std::size_t i=0;i<source.vertices.size();++i)source.skin[i]={{-1,source.vertices[i],1}};
    std::vector<ClothWorldConvex> samples;std::vector<PreparedClothConvexQueries> packets;
    bool valid=true;std::size_t comparisons=0;int revision=0;
    for(const std::uint32_t count:{3u,3u,11u,2u,9u}){
        const auto from=glm::translate(glm::dmat4(1),glm::dvec3(revision*.013,.02,0))*glm::mat4_cast(glm::angleAxis(revision*.11,glm::dvec3(0,1,0))),
            to=glm::translate(glm::dmat4(1),glm::dvec3(.03,-.01,revision*.007))*glm::mat4_cast(glm::angleAxis(3.0+revision*.07,glm::normalize(glm::dvec3(1,2,3))));
        PreparedClothConvexMotion motion;ClothWorldConvex fromWorld,toWorld;
        valid&=BuildClothWorldConvex(source,from,fromWorld)&&BuildClothWorldConvex(source,to,toWorld);
        toWorld.vertices[0]+=glm::dvec3(to*glm::dvec4(.015,.03,-.01,0));
        toWorld.vertices.back()+=glm::dvec3(to*glm::dvec4(.07,.04,.02,0));
        valid&=RefitClothWorldConvex(source,toWorld)&&motion.Prepare(source,fromWorld,toWorld);samples.resize(count+1);
        for(std::uint32_t k=0;k<=count;++k)valid&=motion.At(double(k)/count,samples[k]);
        // Exactly the production lifetime: fill/relocate every shape first,
        // then rebuild all packets even when addresses/counts stayed the same.
        packets.resize(count+1);{ScopedConvexSimd mode("1");for(std::uint32_t k=0;k<=count;++k)packets[k].Prepare(samples[k]);}
        ClothConvexSampleBounds envelope;envelope.Prepare(fromWorld,toWorld,samples);
        for(std::uint32_t k=0;k<=count;++k){const auto& shape=samples[k];const auto unchanged=shape;const auto& prepared=packets[k];
            valid&=prepared.owner==&shape&&prepared.planes==shape.planes.data()&&prepared.planeCount==shape.planes.size();
            valid&=envelope.ready&&glm::all(glm::lessThanEqual(envelope.minimum,shape.minimum))&&glm::all(glm::greaterThanEqual(envelope.maximum,shape.maximum));
            auto world=[&](glm::dvec3 point){return glm::dvec3(shape.transform*glm::dvec4(point,1));};
            for(int outside=0;outside<2;++outside){const auto shift=outside?glm::dvec3(10,10,10):glm::dvec3(0);
                const auto point=world({.2,.01,.01})+shift,a=world({.2,.075,0})+shift,
                    b=world({-.1,0,-.3})+shift,c=world({.5,0,.3})+shift;
                constexpr double margin=.003;const glm::dvec3 offsets(ConvexKinematicContactOffset(shape,a,margin),margin,margin);
                const auto pointReference=ConvexPointContact(shape,point,margin);
                ClothConvexContact pointInline;pointInline.point=point;
                if(envelope.Overlaps(point,point,margin)&&ConvexBoundsOverlap(point,point,shape,margin))pointInline=ConvexPointContact(shape,point,margin,&prepared);
                bool scalarEmpty=false,simdEmpty=false;
                const auto triangleReference=ConvexTriangleContactOffsets(shape,a,b,c,offsets,margin,nullptr,&scalarEmpty);
                ClothConvexContact triangleInline;
                if(envelope.Overlaps(glm::min(a,glm::min(b,c)),glm::max(a,glm::max(b,c)),margin)&&
                    ConvexBoundsOverlap(glm::min(a,glm::min(b,c)),glm::max(a,glm::max(b,c)),shape,margin))
                    triangleInline=ConvexTriangleContactOffsets(shape,a,b,c,offsets,margin,&prepared,&simdEmpty);
                valid&=SameContact(pointReference,pointInline)&&SameContact(triangleReference,triangleInline)&&scalarEmpty==simdEmpty;
                comparisons+=2;
            }
            valid&=SameShape(shape,unchanged);
        }
        ++revision;
    }
    PreparedClothConvexMotion stationary;ClothWorldConvex start,end;const auto transform=glm::translate(glm::dmat4(1),glm::dvec3(.1,.2,.3));
    valid&=stationary.Prepare(source,transform,transform)&&!stationary.changed&&stationary.At(0,start)&&stationary.At(1,end);
    for(auto& vertex:end.vertices)vertex.x+=1e-14;
    valid&=RefitClothWorldConvex(source,end)&&stationary.Prepare(source,start,end)&&!stationary.changed&&start.minimum.x<end.minimum.x;
    ClothConvexSampleBounds stationaryBounds;stationaryBounds.Prepare(start,end,{});
    valid&=SameBits(stationaryBounds.minimum.x,start.minimum.x)&&SameBits(stationaryBounds.maximum.x,end.maximum.x);
    PreparedClothConvexQueries startQueries,endQueries;{ScopedConvexSimd mode("1");startQueries.Prepare(start);endQueries.Prepare(end);}
    const auto point=glm::dvec3(transform*glm::dvec4(.2,0,0,1));
    valid&=startQueries.owner==&start&&endQueries.owner==&end;
    for(int k=0;k<=9;++k)valid&=SameContact(ConvexPointContact(start,point,.003),ConvexPointContact(start,point,.003,&startQueries))&&
        SameContact(ConvexPointContact(end,point,.003),ConvexPointContact(end,point,.003,&endQueries));
    Expect(valid&&comparisons>0,"Shared CCD packets and inline AABB gating preserve complete contacts/proofs, sample shapes and clip-empty metadata bit-for-bit");
    // A non-nested denser grid can contain an extreme absent from the old grid.
    // Its queries must retain the original spherical bound instead of reusing
    // the shared-grid envelope, even though every old sample is represented.
    auto thin=Box({.001,.001,.001},{.3,0,0});PreparedClothConvexMotion arc;
    const auto arcFrom=glm::mat4_cast(glm::angleAxis(-.6,glm::dvec3(0,1,0))),arcTo=glm::mat4_cast(glm::angleAxis(.6,glm::dvec3(0,1,0)));
    std::vector<ClothWorldConvex> oldGrid(4);ClothWorldConvex adaptiveMid;
    bool gridValid=arc.Prepare(thin,arcFrom,arcTo);for(std::size_t k=0;k<oldGrid.size();++k)gridValid&=arc.At(double(k)/3,oldGrid[k]);
    gridValid&=arc.At(.5,adaptiveMid);ClothConvexSampleBounds oldBounds;oldBounds.Prepare(oldGrid.front(),oldGrid.back(),oldGrid);
    const glm::dvec3 extreme(.3,0,0);const auto adaptiveHit=ConvexPointContact(adaptiveMid,extreme,.003);
    constexpr std::uint32_t oldCount=3,newCount=4;
    const bool keepAdaptive=(newCount!=oldCount||oldBounds.Overlaps(extreme,extreme,.003))&&adaptiveHit.hit;
    Expect(gridValid&&!oldBounds.Overlaps(extreme,extreme,.003)&&adaptiveHit.hit&&keepAdaptive,
        "Different adaptive grids bypass the shared envelope and retain a rotating extreme absent from the old k/N samples");
}
void KinematicContactOffsets(){
    std::puts("Authored kinematic seam depth, effective material contacts and honest raw diagnostics:");
    constexpr double margin=.003,depth=.005646467;const auto source=Box({.3,1,.3});
    ClothWorldConvex shape;BuildClothWorldConvex(source,glm::dmat4(1),shape);
    const glm::dvec3 pin(0,1-depth,0),q(.08,1+margin,0),r(0,1+margin,.08);
    const double pinOffset=ConvexKinematicContactOffset(shape,pin,margin);const glm::dvec3 offsets(pinOffset,margin,margin);
    const auto stable=ConvexTriangleContactOffsets(shape,pin,q,r,offsets,margin);
    const double stableDepth=stable.hit?ConvexInterpolatedContactOffset(offsets,stable.bary)-stable.separation:0;
    Expect(std::abs(pinOffset+depth)<1e-12&&std::abs(stableDepth)<1e-11&&
        ConvexKinematicContactOffset(shape,{0,1.1,0},margin)==margin,
        "Only an already-overlapping pin inherits its exact raw skin phi; a compatible seam has zero effective residual");
    // Uniform clipping chooses a raw-deep barycentric point whose interpolated
    // allowance is actually satisfied. Merely changing its final correction
    // would miss B's real effective penetration. Affine mixed clipping finds it.
    const glm::dvec3 a(0,.9,0),b(.08,1.002,0),c(0,1.1,.08),varying(-.1,margin,margin);
    const auto raw=ConvexTriangleContact(shape,a,b,c,margin),effective=ConvexTriangleContactOffsets(shape,a,b,c,varying,margin);
    Expect(raw.hit&&effective.hit&&ConvexInterpolatedContactOffset(varying,raw.bary)-raw.separation<0&&
        ConvexInterpolatedContactOffset(varying,effective.bary)-effective.separation>1e-5&&effective.bary.z<.02,
        "Mixed clipping and deepest-candidate selection detect free fabric penetration that final-only barycentric subtraction misses");
    auto partition=shape;partition.planes.push_back({{1,0,0},-.001,false});
    const auto partitioned=ConvexTriangleContactOffsets(partition,pin,q,r,offsets,margin);
    Expect(partitioned.hit&&partitioned.plane<partition.planes.size()-1&&partitioned.bary.x>0&&
        std::abs(ConvexInterpolatedContactOffset(offsets,partitioned.bary)-partitioned.separation)<1e-10,
        "Artificial partition membership retains global padding instead of eroding at a negative-depth fixed seam");
    const glm::dvec3 rejectedA(0,.999,0),rejectedB(.01,.999,0),rejectedC(0,.999,.01);
    const auto reduced=ConvexTriangleContactOffsets(shape,rejectedA,rejectedB,rejectedC,glm::dvec3(-.01),margin);
    Expect(!reduced.hit&&reduced.separatingPlane==std::numeric_limits<std::size_t>::max(),
        "A pin-reduced rejection does not publish a false global-margin separation-cache proof");
    auto leftSource=Box({.1,1,.3},{-.1,0,0}),rightSource=Box({.1,1,.3},{.1,0,0});
    leftSource.planes[0].collision=false;rightSource.planes[1].collision=false;
    ClothWorldConvex left,right;BuildClothWorldConvex(leftSource,glm::dmat4(1),left);BuildClothWorldConvex(rightSource,glm::dmat4(1),right);
    const glm::dvec3 outsidePin(-.02,1-depth,0),insideB(.08,1.002,0),insideC(.08,1.004,.08);
    const double leftAllowance=ConvexKinematicContactOffset(left,outsidePin,margin),rightAllowance=ConvexKinematicContactOffset(right,outsidePin,margin);
    const auto rightRaw=ConvexTriangleContact(right,outsidePin,insideB,insideC,margin),
        rightEffective=ConvexTriangleContactOffsets(right,outsidePin,insideB,insideC,{rightAllowance,margin,margin},margin);
    Expect(right.minimum.x-outsidePin.x>margin&&glm::dot(right.planes[1].normal,outsidePin)-right.planes[1].distance>margin&&
        std::abs(leftAllowance+depth)<1e-12&&rightAllowance==margin&&rightRaw.hit&&SameContact(rightRaw,rightEffective)&&
        margin-rightEffective.separation>1e-5&&rightEffective.bary.x<1,
        "A pin outside a neighboring piece's cut and AABB keeps full thickness there; another body's negative allowance never propagates");
    auto rotating=Box({.3,.1,.1});PreparedClothConvexMotion motion;
    const auto from=glm::mat4_cast(glm::angleAxis(-.8,glm::dvec3(0,0,1))),to=glm::mat4_cast(glm::angleAxis(.8,glm::dvec3(0,0,1)));
    ClothWorldConvex first,middle,last;bool valid=motion.Prepare(rotating,from,to)&&motion.At(0,first)&&motion.At(.5,middle)&&motion.At(1,last);
    const glm::dvec3 fixedTarget(0,.095,0);const double firstOffset=ConvexKinematicContactOffset(first,fixedTarget,margin),
        middleOffset=ConvexKinematicContactOffset(middle,fixedTarget,margin),lastOffset=ConvexKinematicContactOffset(last,fixedTarget,margin);
    const glm::dvec3 sampleOffsets(middleOffset,margin,margin);
    const auto sampleContact=ConvexTriangleContactOffsets(middle,fixedTarget,{.04,.102,0},{0,.104,.04},sampleOffsets,margin);
    Expect(valid&&std::abs(middleOffset-(firstOffset+lastOffset)*.5)>.02&&sampleContact.hit&&
        ConvexInterpolatedContactOffset(sampleOffsets,sampleContact.bary)-sampleContact.separation>1e-5,
        "Rotating CCD samples re-evaluate the actual nearest skin face rather than interpolating endpoint pin depths");
    auto f=Point(pin);f.cloth.id="authored-seam-offset";f.cloth.settings.thickness=margin;
    f.cloth.particles[0].inverseMass=0;f.cloth.particles[0].maxDistance=0;AddPoint(f,q);AddPoint(f,r);
    f.cloth.triangles={{{0,1,2}}};f.mesh.meshes[0].indices={0,1,2};f.cloth.convexes={source};
    ClothSimulator solver;std::string error;valid=solver.Step(f.cloth,f.pose,f.mesh,1./60,f.cloth.settings,&error);
    const auto& stats=solver.Stats();
    valid&=SameBits(solver.Positions()[0],pin)&&Near(solver.Positions()[1],q,1e-11)&&Near(solver.Positions()[2],r,1e-11)&&
        stats.kinematicColliderOverlaps==1&&std::abs(stats.maxKinematicColliderPenetration-(margin+depth))<1e-11&&
        std::abs(stats.maxSurfacePenetration-(margin+depth))<1e-10&&stats.maxResolvableSurfacePenetration<1e-10&&stats.maxAttachmentError<1e-12;
    Expect(valid,"The solver keeps the source seam exact, free vertices at 3 mm, raw pin/body conflicts visible and effective residual zero");
    // Reference-ready Reset/Step0 must not spend 60 warm substeps trying to
    // remove a raw pin intersection that the authored seam makes unavoidable.
    auto warm=f;warm.cloth.id="authored-seam-offset-warm";warm.cloth.convexes[0].referenceValid=true;
    ClothSimulator prepared;valid=prepared.Reset(warm.cloth,warm.pose,warm.mesh,&error)&&
        prepared.Step(warm.cloth,warm.pose,warm.mesh,0,warm.cloth.settings,&error);
    Expect(valid&&prepared.Stats().constraintIterations==0&&prepared.Stats().maxKinematicColliderPenetration>margin&&
        prepared.Stats().maxResolvableSurfacePenetration<1e-10&&SameBits(prepared.Positions()[0],pin),
        "Reset and zero-time preparation preserve raw seam diagnostics without a false deep-intersection warm start");
    auto centimeters=f;centimeters.cloth.id="authored-seam-offset-centimeters";centimeters.cloth.unitsPerMeter=100;
    for(auto& particle:centimeters.cloth.particles){particle.restPosition*=100;particle.maxDistance*=100;}
    for(auto& vertex:centimeters.mesh.meshes[0].vertices)vertex.position*=100;
    for(auto& collider:centimeters.cloth.convexes){for(auto& vertex:collider.vertices)vertex*=100;for(auto& plane:collider.planes)plane.distance*=100;}
    ClothSimulator scaled;valid=scaled.Step(centimeters.cloth,centimeters.pose,centimeters.mesh,1./60,centimeters.cloth.settings,&error);
    Expect(valid&&Near(scaled.Positions()[0]/100.,pin,1e-12)&&
        std::abs(scaled.Stats().maxKinematicColliderPenetration-100*stats.maxKinematicColliderPenetration)<1e-9&&
        std::abs(scaled.Stats().maxSurfacePenetration-100*stats.maxSurfacePenetration)<1e-8&&
        scaled.Stats().maxResolvableSurfacePenetration<1e-8,
        "Raw kinematic depth and effective surface residual are reported in source units with SI-equivalent seam behavior");
    auto seamBoundary=Point(outsidePin);seamBoundary.cloth.id="kinematic-cut-boundary";seamBoundary.cloth.settings.thickness=margin;
    seamBoundary.cloth.settings.iterations=1;seamBoundary.cloth.particles[0].inverseMass=0;seamBoundary.cloth.particles[0].maxDistance=0;
    AddPoint(seamBoundary,insideB);AddPoint(seamBoundary,insideC);seamBoundary.cloth.triangles={{{0,1,2}}};
    seamBoundary.mesh.meshes[0].indices={0,1,2};seamBoundary.cloth.convexes={leftSource,rightSource};ClothSimulator boundarySolver;
    valid=boundarySolver.Step(seamBoundary.cloth,seamBoundary.pose,seamBoundary.mesh,1./60,seamBoundary.cloth.settings,&error);
    Expect(valid&&boundarySolver.Stats().constraintIterations==1&&SameBits(boundarySolver.Positions()[0],outsidePin)&&
        boundarySolver.Stats().contacts>0&&boundarySolver.Stats().maxKinematicColliderPenetration>margin,
        "A single iteration across a partition boundary preserves the source pin and resolves only movable cloth with body-local allowances");
}
void SweptEndpointRetention(){
    std::puts("A non-actionable swept point cannot discard a valid endpoint material contact:");
    auto f=Point({2,0,0});AddPoint(f,{-2,-1,0});AddPoint(f,{-2,1,0});f.cloth.id="convex-single-iteration-endpoint";
    f.cloth.settings.iterations=1;f.cloth.settings.thickness=.003;f.cloth.triangles={{{0,1,2}}};f.mesh.meshes[0].indices={0,1,2};
    auto body=Box({.1,.1,.1});body.bone=0;f.cloth.convexes={body};
    f.pose.global={glm::translate(glm::dmat4(1),glm::dvec3(2.3,0,0))*glm::mat4_cast(glm::angleAxis(std::acos(-1.),glm::dvec3(0,0,1)))};
    ClothSimulator solver;std::string error;bool valid=solver.Step(f.cloth,f.pose,f.mesh,0,f.cloth.settings,&error);
    const auto before=solver.Positions();f.pose.global[0]=glm::dmat4(1);
    valid=valid&&solver.Step(f.cloth,f.pose,f.mesh,1./60,f.cloth.settings,&error);
    double motion=0;for(std::size_t i=0;i<before.size();++i)motion=std::max(motion,glm::length(solver.Positions()[i]-before[i]));
    Expect(valid&&solver.Stats().constraintIterations==1&&solver.Stats().contacts>0&&motion>1e-5,
        "With one iteration, a rotating/translating body keeps the positive end contact after its first transported entry face has negative correction");
}
void SolverContacts(){
    std::puts("Convex particle, triangle and swept solver constraints:");std::string error;
    auto f=Point({.02,0,0});f.cloth.convexes.push_back(Box({.1,.2,.2}));ClothSimulator sim;
    bool valid=sim.Step(f.cloth,f.pose,f.mesh,1./60,f.cloth.settings,&error);
    Expect(valid&&sim.Positions()[0].x>=.108-1e-9&&sim.Stats().contacts==1&&sim.Stats().maxPenetration<1e-8,
        "Convex particle contact reaches the true skin face plus thickness without a round capsule clearance");
    auto scaled=f;scaled.cloth.unitsPerMeter=100;scaled.cloth.id="convex-centimeters";
    for(auto& point:scaled.cloth.particles){point.restPosition*=100;point.maxDistance*=100;}
    for(auto& vertex:scaled.mesh.meshes[0].vertices)vertex.position*=100;
    for(auto& shape:scaled.cloth.convexes){for(auto& vertex:shape.vertices)vertex*=100;for(auto& plane:shape.planes)plane.distance*=100;}
    ClothSimulator centimeters;valid=centimeters.Step(scaled.cloth,scaled.pose,scaled.mesh,1./60,scaled.cloth.settings,&error);
    Expect(valid&&Near(centimeters.Positions()[0]/100.,sim.Positions()[0],1e-9),"Source meters and centimeters produce the same skin contact in internal SI units");
    auto pinned=f;pinned.cloth.id="convex-kinematic";pinned.cloth.particles[0].inverseMass=0;pinned.cloth.particles[0].maxDistance=0;
    ClothSimulator fixed;valid=fixed.Step(pinned.cloth,pinned.pose,pinned.mesh,0,pinned.cloth.settings,&error);
    Expect(valid&&fixed.Stats().kinematicColliderOverlaps==1&&fixed.Stats().maxAttachmentError<1e-12&&Near(fixed.Positions()[0],{.02,0,0}),
        "Diagnostics report a conflicting fixed seam without moving its authored attachment");
    Fixture face=Point({-1,0,-1});AddPoint(face,{1,0,-1});AddPoint(face,{0,0,1});face.cloth.id="convex-face";
    face.cloth.triangles={{{0,1,2}}};face.mesh.meshes[0].indices={0,1,2};face.cloth.convexes.push_back(Box({.3,.05,.3}));
    ClothSimulator surface;valid=true;for(int frame=0;frame<8&&valid;++frame)valid=surface.Step(face.cloth,face.pose,face.mesh,1./60,face.cloth.settings,&error);
    Expect(valid&&surface.Stats().maxSurfacePenetration<.003&&surface.Stats().collapsedTriangles==0&&
        std::max({surface.Positions()[0].y,surface.Positions()[1].y,surface.Positions()[2].y})>.05,
        "Barycentric triangle contact expels an interior skin crossing while retaining a noncollapsed membrane");
    auto fast=Point({0,0,0});fast.cloth.id="convex-fast-translation";fast.cloth.convexes.push_back(Box({.08,.1,.08}));
    fast.cloth.convexes[0].bone=0;fast.pose.global={glm::translate(glm::dmat4(1),glm::dvec3(-.3,0,0))};ClothSimulator swept;
    valid=swept.Step(fast.cloth,fast.pose,fast.mesh,0,fast.cloth.settings,&error);
    fast.pose.global[0]=glm::translate(glm::dmat4(1),glm::dvec3(.3,0,0));valid=valid&&swept.Step(fast.cloth,fast.pose,fast.mesh,1./60,fast.cloth.settings,&error);
    Expect(valid&&swept.Positions()[0].x>.387&&swept.Stats().contacts==1&&swept.Stats().maxPenetration<1e-8,
        "A fast skin body crossing a particle transports its entry face to the correct final side");
    auto rotating=Point({0,0,-.3});rotating.cloth.id="convex-fast-rotation";rotating.cloth.convexes.push_back(Box({.04,.1,.04},{.3,0,0}));
    rotating.cloth.convexes[0].bone=0;rotating.pose.global={glm::dmat4(1)};ClothSimulator angular;
    valid=angular.Step(rotating.cloth,rotating.pose,rotating.mesh,0,rotating.cloth.settings,&error);
    rotating.pose.global[0]=glm::mat4_cast(glm::angleAxis(std::acos(-1.),glm::dvec3(0,1,0)));
    valid=valid&&angular.Step(rotating.cloth,rotating.pose,rotating.mesh,1./60,rotating.cloth.settings,&error);
    Expect(valid&&angular.Stats().contacts==1&&glm::length(angular.Positions()[0]-rotating.cloth.particles[0].restPosition)>.01&&angular.Stats().maxPenetration<1e-8,
        "Rotation CCD catches a skin volume passing outside both endpoint AABBs");
    auto mixed=f;mixed.cloth.id="mixed-body-proxies";mixed.cloth.capsules.push_back({"legacy-foot",-1,-1,{-1,0,0},{-1,.1,0},.08});
    ClothSimulator mixedSolver;valid=mixedSolver.Step(mixed.cloth,mixed.pose,mixed.mesh,1./60,mixed.cloth.settings,&error);
    Expect(valid&&Near(mixedSolver.Positions()[0],sim.Positions()[0])&&mixedSolver.Stats().contacts==1,"Legacy capsules and skin convexes coexist without double-counting contacting particles");
    for(std::uint32_t version:{1u,2u,3u}){auto legacy=f;legacy.cloth.version=version;ClothSimulator compatible;
        Expect(compatible.Step(legacy.cloth,legacy.pose,legacy.mesh,1./60,legacy.cloth.settings,&error),"Solver accepts validated v1, v2 and v3 cloth assets");}
    auto bad=f;bad.cloth.convexes[0].planes[0].normal={0,0,0};ClothSimulator invalid;
    Expect(!invalid.Step(bad.cloth,bad.pose,bad.mesh,1./60,bad.cloth.settings,&error),"An invalid convex plane is rejected before parallel constraint solving");
}
Fixture WarmFixture(){
    auto f=Point({-.2,.2,0});f.cloth.id="convex-warm-reference";f.cloth.particles[0].inverseMass=0;f.cloth.particles[0].maxDistance=0;
    AddPoint(f,{.2,.2,0},true);AddPoint(f,{0,.22,.02},true);AddPoint(f,{-.2,-.4,0});AddPoint(f,{.2,-.4,0});
    f.cloth.motionBone=0;f.pose.global={glm::dmat4(1),glm::translate(glm::dmat4(1),glm::dvec3(0,-.2,0))};
    f.cloth.triangles={{{0,3,1}},{{1,3,4}},{{0,1,2}}};f.mesh.meshes[0].indices={0,3,1,1,3,4,0,1,2};
    std::set<std::pair<std::uint32_t,std::uint32_t>> edges;
    for(const auto& triangle:f.cloth.triangles)for(int i=0;i<3;++i){auto a=triangle[i],b=triangle[(i+1)%3];if(a>b)std::swap(a,b);edges.insert({a,b});}
    for(const auto& [a,b]:edges)f.cloth.stretch.push_back({a,b,glm::length(f.cloth.particles[a].restPosition-f.cloth.particles[b].restPosition)});
    auto shape=Box({.06,.3,.06});shape.bone=1;shape.referenceValid=true;
    shape.referenceTransform=glm::translate(glm::dmat4(1),glm::dvec3(0,-.2,-.3));f.cloth.convexes.push_back(std::move(shape));return f;
}
void AttachmentWarmStart(){
    std::puts("Neutral skin collision rigs share the fitted attachment frame:");auto f=WarmFixture();auto moved=f;moved.cloth.id="convex-warm-moved";
    const auto frame=glm::translate(glm::dmat4(1),glm::dvec3(.2,.5,.1))*glm::mat4_cast(glm::angleAxis(.4,glm::normalize(glm::dvec3(1,2,3))));
    for(auto& vertex:moved.mesh.meshes[0].vertices)vertex.position=glm::dvec3(frame*glm::dvec4(vertex.position,1));
    for(auto& pose:moved.pose.global)pose=frame*pose;
    ClothSimulator reference,transformed;std::string error;
    bool valid=reference.Reset(f.cloth,f.pose,f.mesh,&error)&&reference.Step(f.cloth,f.pose,f.mesh,0,f.cloth.settings,&error)&&
        transformed.Reset(moved.cloth,moved.pose,moved.mesh,&error)&&transformed.Step(moved.cloth,moved.pose,moved.mesh,0,moved.cloth.settings,&error);
    double difference=0;
    if(valid)for(std::size_t i=0;i<reference.Positions().size();++i)
        difference=std::max(difference,glm::length(transformed.Positions()[i]-glm::dvec3(frame*glm::dvec4(reference.Positions()[i],1))));
    Expect(valid&&reference.Stats().steps==0&&transformed.Stats().steps==0&&reference.Stats().constraintIterations>0&&
        transformed.Stats().constraintIterations>0&&difference<1e-6&&transformed.Stats().maxAttachmentError<1e-9,
        "Warmup moves neutral skin bodies from attachmentFrame * referenceTransform and preserves a rotated/translated waist");
    if(!valid||difference>=1e-6)std::printf("    warm diagnostic: %s; transformed difference %.12g\n",error.c_str(),difference);
    valid=transformed.Step(moved.cloth,moved.pose,moved.mesh,.4,moved.cloth.settings,&error);
    Expect(valid&&transformed.Stats().steps==0&&transformed.Stats().constraintIterations>0&&transformed.Stats().maxAttachmentError<1e-9,
        "A discontinuity reset completes zero-time collision preparation before exposing its result");
}
}
int main(){TransformAndQueries();RigidMotion();PartitionContacts();SkinnedSupportMotion();RuntimePlaneLayout();ProjectionInducedSurfaceSweep();SeparatingPlaneProofs();SimdQueryEquivalence();SeparatingCacheEquivalence();CompositeSeparationProofs();SharedSampleQueryEquivalence();KinematicContactOffsets();SweptEndpointRetention();SolverContacts();AttachmentWarmStart();
    std::printf("Cloth convex: %s (%d failures)\n",failures?"FAILED":"PASSED",failures);return failures?1:0;}
