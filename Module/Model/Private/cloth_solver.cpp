#include "Model/Public/cloth_asset.h"
#include "cloth_parallel.h"
#include "cloth_math.h"
#include "cloth_simd.h"
#include "cloth_area_simd.h"
#include "cloth_distance_simd.h"
#include "cloth_convex.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <queue>
#include <glm/gtc/quaternion.hpp>
#include <unordered_map>
#include <unordered_set>

namespace Model {
namespace {
constexpr double Epsilon=1e-12;
bool Finite(double value){return std::isfinite(value);}
bool Finite(glm::dvec3 value){return Finite(value.x)&&Finite(value.y)&&Finite(value.z);}
bool Finite(const glm::dmat4& value){for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!Finite(value[c][r]))return false;return true;}
bool DiagnosticsEnabled(){
    static const bool enabled=[](){
#ifdef _WIN32
        char* value=nullptr;std::size_t length=0;const bool result=_dupenv_s(&value,&length,"MODEL_CLOTH_DIAGNOSTICS")==0&&value&&*value;std::free(value);return result;
#else
        return std::getenv("MODEL_CLOTH_DIAGNOSTICS")!=nullptr;
#endif
    }();return enabled;
}
bool ProfileEnabled(){
    static const bool enabled=[](){
#ifdef _WIN32
        char* value=nullptr;std::size_t length=0;const bool result=_dupenv_s(&value,&length,"MODEL_CLOTH_PROFILE")==0&&value&&*value;std::free(value);return result;
#else
        return std::getenv("MODEL_CLOTH_PROFILE")!=nullptr;
#endif
    }();return enabled;
}
bool EnvironmentValueIs(const char* name,char expected){
#ifdef _WIN32
    char* value=nullptr;std::size_t length=0;const bool enabled=_dupenv_s(&value,&length,name)==0&&value&&value[0]==expected&&value[1]=='\0';std::free(value);return enabled;
#else
    const auto* value=std::getenv(name);return value&&value[0]==expected&&value[1]=='\0';
#endif
}
bool EnvironmentFlag(const char* name){return EnvironmentValueIs(name,'1');}
struct ProfileScope {
    double* elapsed;std::chrono::steady_clock::time_point started;
    ProfileScope(double& destination,bool enabled):elapsed(enabled?&destination:nullptr){if(elapsed)started=std::chrono::steady_clock::now();}
    ~ProfileScope(){if(elapsed)*elapsed+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();}
};
double Length(glm::dvec3 value){return std::sqrt(std::max(0.0,glm::dot(value,value)));}
glm::dvec3 Unit(glm::dvec3 value,glm::dvec3 fallback={0,0,1}){const double length=Length(value);return length>Epsilon?value/length:fallback;}
std::uint64_t Pair(std::uint32_t a,std::uint32_t b){if(a>b)std::swap(a,b);return (std::uint64_t(a)<<32)|b;}
void Hash(std::uint64_t& hash,std::uint64_t value){for(int i=0;i<8;++i){hash^=(value>>(i*8))&255;hash*=1099511628211ull;}}
void Hash(std::uint64_t& hash,double value){Hash(hash,std::bit_cast<std::uint64_t>(value));}
void Hash(std::uint64_t& hash,glm::dvec3 value){Hash(hash,value.x);Hash(hash,value.y);Hash(hash,value.z);}
std::uint64_t Signature(const ClothAsset& cloth){
    std::uint64_t hash=1469598103934665603ull;
    for(const auto& s:{cloth.id,cloth.garmentId,cloth.skeletonId,cloth.topologyHash})for(unsigned char c:s){hash^=c;hash*=1099511628211ull;}
    Hash(hash,cloth.unitsPerMeter);Hash(hash,std::uint64_t(cloth.particles.size()));
    Hash(hash,std::uint64_t(cloth.motionBone));for(int c=0;c<4;++c)for(int r=0;r<4;++r)Hash(hash,cloth.motionInverseBind[c][r]);
    for(const auto& p:cloth.particles){Hash(hash,p.restPosition);Hash(hash,p.inverseMass);Hash(hash,p.maxDistance);Hash(hash,p.animationDrive);}
    for(const auto* constraints:{&cloth.stretch,&cloth.bend}){Hash(hash,std::uint64_t(constraints->size()));for(const auto& c:*constraints){Hash(hash,std::uint64_t(c.a));Hash(hash,std::uint64_t(c.b));Hash(hash,c.restLength);}}
    for(const auto& t:cloth.tethers){Hash(hash,std::uint64_t(t.particle));Hash(hash,std::uint64_t(t.anchor));Hash(hash,t.restLength);}
    for(const auto& t:cloth.triangles)for(auto v:t)Hash(hash,std::uint64_t(v));
    for(const auto& b:cloth.renderBindings){Hash(hash,std::uint64_t(b.mesh));Hash(hash,std::uint64_t(b.vertex));Hash(hash,std::uint64_t(b.particle));}
    for(const auto& c:cloth.capsules){Hash(hash,std::uint64_t(c.boneA));Hash(hash,std::uint64_t(c.boneB));Hash(hash,c.localA);Hash(hash,c.localB);Hash(hash,c.radius);Hash(hash,std::uint64_t(c.referenceValid));Hash(hash,c.restA);Hash(hash,c.restB);}
    Hash(hash,std::uint64_t(cloth.convexes.size()));
    for(const auto& c:cloth.convexes){Hash(hash,std::uint64_t(c.bone));Hash(hash,std::uint64_t(c.referenceValid));
        for(int col=0;col<4;++col)for(int row=0;row<4;++row)Hash(hash,c.referenceTransform[col][row]);
        Hash(hash,std::uint64_t(c.vertices.size()));for(const auto& v:c.vertices)Hash(hash,v);
        Hash(hash,std::uint64_t(c.triangles.size()));for(const auto& triangle:c.triangles)for(auto v:triangle)Hash(hash,std::uint64_t(v));
        Hash(hash,std::uint64_t(c.planes.size()));for(const auto& p:c.planes){Hash(hash,p.normal);Hash(hash,p.distance);Hash(hash,std::uint64_t(p.collision));}
        Hash(hash,std::uint64_t(c.skin.size()));for(const auto& vertex:c.skin){Hash(hash,std::uint64_t(vertex.size()));for(const auto& influence:vertex){Hash(hash,std::uint64_t(influence.bone));Hash(hash,influence.weightedPosition);Hash(hash,influence.weight);}}}
    return hash;
}
bool SettingsValid(const ClothSolverSettings& s,std::string* error){
    auto fail=[&](const char* message){if(error)*error=message;return false;};
    if(!Finite(s.fixedTimeStep)||s.fixedTimeStep<.001||s.fixedTimeStep>.1||s.substeps<1||s.substeps>16||s.iterations<1||s.iterations>64)
        return fail("Cloth requires a 0.001..0.1 second fixed step, 1..16 substeps and 1..64 iterations");
    if(!Finite(s.convergenceTolerance)||s.convergenceTolerance<0||s.convergenceTolerance>.01)return fail("Cloth convergence tolerance must be finite and between 0 and 0.01 meters");
    for(double c:{s.stretchCompliance,s.areaCompliance,s.animationCompliance})if(!Finite(c)||c<0||c>1e4)return fail("Cloth distance compliance must be finite and between 0 and 10000");
    if(!Finite(s.bendCompliance)||s.bendCompliance<0||s.bendCompliance>1e8)return fail("Cloth angular compliance must be finite and between 0 and 100000000");
    for(double damping:{s.animationDamping,s.localDamping})if(!Finite(damping)||damping<0||damping>1000)return fail("Cloth animation and local damping must be finite and between 0 and 1000");
    if(!Finite(s.damping)||s.damping<0||s.damping>1000||!Finite(s.friction)||s.friction<0||s.friction>1||!Finite(s.thickness)||s.thickness<1e-5||s.thickness>.25)
        return fail("Cloth damping, friction or collision thickness is outside the supported SI range");
    if(!Finite(s.gravity)||Length(s.gravity)>1000||!Finite(s.wind)||Length(s.wind)>1000)return fail("Cloth gravity and wind must be finite and at most 1000 SI units");
    return true;
}
bool AssetValid(const ClothAsset& cloth,std::string* error){
    auto fail=[&](const char* message){if(error)*error=message;return false;};
    if((cloth.version!=1&&cloth.version!=2&&cloth.version!=ClothAssetVersion)||cloth.particles.empty()||cloth.particles.size()>1000000||!Finite(cloth.unitsPerMeter)||cloth.unitsPerMeter<1e-6||cloth.unitsPerMeter>1e8)
        return fail("Cloth version, particle count or units per meter is invalid");
    if(cloth.motionBone< -1||!Finite(cloth.motionInverseBind)||std::abs(glm::determinant(glm::dmat3(cloth.motionInverseBind)))<Epsilon)return fail("Cloth motion reference bone or inverse bind transform is invalid");
    for(const auto& p:cloth.particles)if(!Finite(p.restPosition)||Length(p.restPosition/cloth.unitsPerMeter)>1e7||!Finite(p.inverseMass)||p.inverseMass<0||p.inverseMass>1e12||!Finite(p.maxDistance)||p.maxDistance<0||p.maxDistance/cloth.unitsPerMeter>1000||!Finite(p.animationDrive)||p.animationDrive<0||p.animationDrive>1)
        return fail("Cloth particle has invalid position, mass, maximum distance or animation drive");
    for(const auto* constraints:{&cloth.stretch,&cloth.bend})for(const auto& c:*constraints)if(c.a>=cloth.particles.size()||c.b>=cloth.particles.size()||c.a==c.b||!Finite(c.restLength)||c.restLength<0||c.restLength/cloth.unitsPerMeter>1e5)
        return fail("Cloth distance constraint has invalid indices or rest length");
    for(const auto& t:cloth.tethers)if(t.particle>=cloth.particles.size()||t.anchor>=cloth.particles.size()||t.particle==t.anchor||!Finite(t.restLength)||t.restLength<0||t.restLength/cloth.unitsPerMeter>1e5)
        return fail("Cloth tether has invalid indices or rest length");
    for(const auto& t:cloth.triangles)if(t[0]>=cloth.particles.size()||t[1]>=cloth.particles.size()||t[2]>=cloth.particles.size())return fail("Cloth simulation triangle index is out of range");
    for(const auto& b:cloth.renderBindings)if(b.particle>=cloth.particles.size())return fail("Cloth render binding particle is out of range");
    for(const auto& c:cloth.capsules)if(!Finite(c.localA)||!Finite(c.localB)||!Finite(c.radius)||c.radius<=0||c.radius/cloth.unitsPerMeter>1000||c.boneA< -1||c.boneB< -1||!Finite(c.restA)||!Finite(c.restB)||(c.referenceValid&&(Length(c.restA/cloth.unitsPerMeter)>1e7||Length(c.restB/cloth.unitsPerMeter)>1e7)))
        return fail("Cloth collision capsule is invalid");
    if(cloth.convexes.size()>64)return fail("Cloth has too many convex collision bodies");
    for(const auto& c:cloth.convexes){
        if(c.bone< -1||c.vertices.size()<4||c.vertices.size()>1024||c.triangles.size()<4||c.triangles.size()>1024||c.planes.size()<4||c.planes.size()>256||!Finite(c.referenceTransform)||std::abs(glm::determinant(glm::dmat3(c.referenceTransform)))<1e-24)
            return fail("Cloth convex body has invalid bone, topology or reference transform");
        for(const auto& v:c.vertices)if(!Finite(v)||Length(v/cloth.unitsPerMeter)>1e7)return fail("Cloth convex body has an invalid vertex");
        for(const auto& t:c.triangles)if(t[0]>=c.vertices.size()||t[1]>=c.vertices.size()||t[2]>=c.vertices.size()||t[0]==t[1]||t[1]==t[2]||t[2]==t[0])return fail("Cloth convex body has an invalid triangle");
        for(const auto& p:c.planes)if(!Finite(p.normal)||std::abs(glm::dot(p.normal,p.normal)-1)>1e-5||!Finite(p.distance)||std::abs(p.distance/cloth.unitsPerMeter)>1e7)return fail("Cloth convex body has an invalid unit plane");
        if(std::none_of(c.planes.begin(),c.planes.end(),[](const auto& p){return p.collision;}))return fail("Cloth convex body requires at least one real skin collision plane");
        if(!c.skin.empty()){
            if(c.skin.size()!=c.vertices.size()||c.triangles.size()!=c.planes.size())return fail("Cloth skinned convex support or surface layout is invalid");
            for(const auto& vertex:c.skin){if(vertex.empty()||vertex.size()>16)return fail("Cloth convex support requires one to sixteen skin influences");double sum=0;
                for(const auto& influence:vertex){if(influence.bone< -1||!Finite(influence.weightedPosition)||Length(influence.weightedPosition/cloth.unitsPerMeter)>1e7||!Finite(influence.weight)||influence.weight<0||influence.weight>1)return fail("Cloth convex skin influence is invalid");sum+=influence.weight;}
                if(std::abs(sum-1)>1e-5)return fail("Cloth convex skin weights are not normalized");}
        }
    }
    return true;
}
bool SameMatrix(const glm::dmat4& a,const glm::dmat4& b){for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(std::abs(a[c][r]-b[c][r])>1e-10)return false;return true;}
bool MeshTransforms(const StaticModel& model,std::vector<glm::dmat4>& transforms,std::string* error){
    transforms.assign(model.meshes.size(),glm::dmat4(1));std::vector<bool> assigned(model.meshes.size(),false);
    auto fail=[&](const char* message){if(error)*error=message;return false;};
    for(const auto& instance:model.instances){
        if(instance.meshIndex>=model.meshes.size())return fail("Cloth garment instance references a nonexistent mesh");
        const auto& m=instance.transform;
        if(!Finite(m)||std::abs(m[0][3])>Epsilon||std::abs(m[1][3])>Epsilon||std::abs(m[2][3])>Epsilon||std::abs(m[3][3]-1)>Epsilon||std::abs(glm::determinant(glm::dmat3(m)))<Epsilon)
            return fail("Cloth garment requires finite, invertible affine mesh instance transforms");
        if(assigned[instance.meshIndex]&&!SameMatrix(transforms[instance.meshIndex],m))return fail("Expand differently transformed instances before cloth simulation");
        transforms[instance.meshIndex]=m;assigned[instance.meshIndex]=true;
    }
    return true;
}
bool AnimatedTargets(const ClothAsset& cloth,const StaticModel& garment,std::vector<glm::dvec3>& targets,std::string* error){
    std::vector<glm::dmat4> transforms;if(!MeshTransforms(garment,transforms,error))return false;
    targets.assign(cloth.particles.size(),glm::dvec3(0));std::vector<std::uint32_t> counts(cloth.particles.size(),0);
    std::unordered_set<std::uint64_t> bound;
    for(const auto& binding:cloth.renderBindings){
        if(binding.mesh>=garment.meshes.size()||binding.vertex>=garment.meshes[binding.mesh].vertices.size()||binding.particle>=targets.size()){
            if(error)*error="Cloth render binding is outside the animated garment geometry";return false;}
        const auto key=(std::uint64_t(binding.mesh)<<32)|binding.vertex;
        if(!bound.insert(key).second){if(error)*error="A cloth render vertex has multiple simulation bindings";return false;}
        const auto point=glm::dvec3(transforms[binding.mesh]*glm::dvec4(garment.meshes[binding.mesh].vertices[binding.vertex].position,1))/cloth.unitsPerMeter;
        if(!Finite(point)||Length(point)>1e7){if(error)*error="Animated cloth target is nonfinite or outside the supported SI range";return false;}
        targets[binding.particle]+=point;++counts[binding.particle];
    }
    for(std::size_t i=0;i<targets.size();++i)targets[i]=counts[i]?targets[i]/double(counts[i]):cloth.particles[i].restPosition/cloth.unitsPerMeter;
    return true;
}
// The attachment is authored by skin weights, and can belong to the torso
// rather than the semantic pelvis. Fit that actual boundary before constructing
// free goals. In particular, resetting a bent torso must not join pelvis-space
// free points to torso-space fixed points with an instantaneous stretched seam.
struct AttachmentReference {
    struct Component {std::vector<std::uint32_t> particles,pins;glm::dvec3 center{0};bool noncollinear=false;};
    struct Influence {std::uint32_t pin=0;double weight=0;};
    std::vector<Component> components;
    std::vector<std::array<Influence,4>> influences;
    std::vector<std::uint8_t> influenceCounts;
    std::vector<glm::dvec3> residual;
    void Build(const ClothAsset& cloth){
        const auto count=cloth.particles.size();components.clear();influences.assign(count,{});influenceCounts.assign(count,0);residual.resize(count);
        std::vector<std::vector<std::pair<std::uint32_t,double>>> adjacency(count);
        std::unordered_set<std::uint64_t> edges;
        auto edge=[&](std::uint32_t a,std::uint32_t b){if(a==b||!edges.insert(Pair(a,b)).second)return;const double distance=Length(cloth.particles[a].restPosition-cloth.particles[b].restPosition);if(distance<=Epsilon)return;adjacency[a].push_back({b,distance});adjacency[b].push_back({a,distance});};
        for(const auto& c:cloth.stretch)edge(c.a,c.b);
        for(const auto& t:cloth.triangles)for(int i=0;i<3;++i)edge(t[i],t[(i+1)%3]);
        std::vector<std::uint8_t> seen(count,0);
        for(std::uint32_t first=0;first<count;++first)if(!seen[first]){
            Component component;component.particles.push_back(first);seen[first]=1;
            for(std::size_t k=0;k<component.particles.size();++k){const auto i=component.particles[k];
                if(cloth.particles[i].inverseMass==0||cloth.particles[i].maxDistance<=Epsilon){component.pins.push_back(i);component.center+=cloth.particles[i].restPosition;}
                for(const auto& [next,distance]:adjacency[i])if(!seen[next]){seen[next]=1;component.particles.push_back(next);}}
            if(!component.pins.empty())component.center/=double(component.pins.size());
            double longest2=0;glm::dvec3 axis(0);
            for(auto i:component.pins){const auto p=cloth.particles[i].restPosition-component.center;const double length2=glm::dot(p,p);if(length2>longest2){axis=p;longest2=length2;}}
            for(auto i:component.pins){const auto cross=glm::cross(axis,cloth.particles[i].restPosition-component.center);if(glm::dot(cross,cross)>longest2*longest2*1e-10)component.noncollinear=true;}
            components.push_back(std::move(component));
        }
        // Four nearest attachment anchors in graph distance. This bounded
        // multi-source traversal is cached at topology reset; each vertex/edge
        // is visited at most four times, even for very large pin sets.
        struct Entry {double distance;std::uint32_t particle,pin;bool operator>(const Entry& other)const{return distance!=other.distance?distance>other.distance:pin!=other.pin?pin>other.pin:particle>other.particle;}};
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> pending;
        for(const auto& component:components)for(auto pin:component.pins)pending.push({0,pin,pin});
        while(!pending.empty()){
            const auto entry=pending.top();pending.pop();auto& used=influenceCounts[entry.particle];auto& anchors=influences[entry.particle];
            bool duplicate=false;for(std::size_t k=0;k<used;++k)duplicate|=anchors[k].pin==entry.pin;
            if(duplicate||used==anchors.size())continue;
            anchors[used++]={entry.pin,entry.distance};
            for(const auto& [next,distance]:adjacency[entry.particle])if(influenceCounts[next]<4)pending.push({entry.distance+distance,next,entry.pin});
        }
        for(std::size_t i=0;i<count;++i){auto& anchors=influences[i];const auto used=influenceCounts[i];if(!used)continue;
            if(anchors[0].weight<=Epsilon){anchors[0].weight=1;influenceCounts[i]=1;continue;}
            const double nearest=anchors[0].weight;double sum=0;
            for(std::size_t k=0;k<used;++k){const double ratio=nearest/anchors[k].weight;anchors[k].weight=ratio*ratio;sum+=anchors[k].weight;}
            for(std::size_t k=0;k<used;++k)anchors[k].weight/=sum;
        }
    }
    glm::dmat4 Frame(const ClothAsset& cloth,const Component& component,const std::vector<glm::dvec3>& targets,const glm::dmat4& fallback)const{
        if(component.pins.empty())return fallback;
        glm::dvec3 center(0);for(auto pin:component.pins)center+=targets[pin]*cloth.unitsPerMeter;center/=double(component.pins.size());
        glm::dmat4 result=fallback;
        if(component.noncollinear){
            double covariance[3][3]{},restVariance=0;
            for(auto pin:component.pins){const auto a=cloth.particles[pin].restPosition-component.center,b=targets[pin]*cloth.unitsPerMeter-center;restVariance+=glm::dot(a,a);for(int r=0;r<3;++r)for(int c=0;c<3;++c)covariance[r][c]+=a[r]*b[c];}
            const auto& h=covariance;const double trace=h[0][0]+h[1][1]+h[2][2];
            double matrix[4][4]={{trace,h[1][2]-h[2][1],h[2][0]-h[0][2],h[0][1]-h[1][0]},
                {h[1][2]-h[2][1],h[0][0]-h[1][1]-h[2][2],h[0][1]+h[1][0],h[0][2]+h[2][0]},
                {h[2][0]-h[0][2],h[0][1]+h[1][0],-h[0][0]+h[1][1]-h[2][2],h[1][2]+h[2][1]},
                {h[0][1]-h[1][0],h[0][2]+h[2][0],h[1][2]+h[2][1],-h[0][0]-h[1][1]+h[2][2]}};
            double vectors[4][4]{};for(int i=0;i<4;++i)vectors[i][i]=1;
            // Symmetric Jacobi gives the largest algebraic eigenvalue. Power
            // iteration is unsuitable here: planar rings can have a negative
            // eigenvalue with the same magnitude, including 180-degree turns.
            for(int sweep=0;sweep<12;++sweep)for(int p=0;p<4;++p)for(int q=p+1;q<4;++q){
                const double off=matrix[p][q];if(std::abs(off)<=1e-15*std::max(1e-30,restVariance))continue;
                const double tau=(matrix[q][q]-matrix[p][p])/(2*off);const double tangent=std::copysign(1.0,tau)/(std::abs(tau)+std::sqrt(1+tau*tau));const double cosine=1/std::sqrt(1+tangent*tangent),sine=tangent*cosine;
                matrix[p][p]-=tangent*off;matrix[q][q]+=tangent*off;matrix[p][q]=matrix[q][p]=0;
                for(int r=0;r<4;++r)if(r!=p&&r!=q){const double a=matrix[r][p],b=matrix[r][q];matrix[r][p]=matrix[p][r]=cosine*a-sine*b;matrix[r][q]=matrix[q][r]=sine*a+cosine*b;}
                for(int r=0;r<4;++r){const double a=vectors[r][p],b=vectors[r][q];vectors[r][p]=cosine*a-sine*b;vectors[r][q]=sine*a+cosine*b;}
            }
            int largest=0;for(int i=1;i<4;++i)if(matrix[i][i]>matrix[largest][largest])largest=i;
            const glm::dquat rotation(vectors[0][largest],vectors[1][largest],vectors[2][largest],vectors[3][largest]);result=glm::mat4_cast(glm::normalize(rotation));
            double scale=0;for(auto pin:component.pins)scale+=glm::dot(glm::dvec3(result*glm::dvec4(cloth.particles[pin].restPosition-component.center,0)),targets[pin]*cloth.unitsPerMeter-center);
            scale=restVariance>Epsilon?scale/restVariance:1;if(!Finite(scale)||scale<1e-6||scale>1e6)scale=1;
            for(int c=0;c<3;++c)result[c]*=scale;
        }
        result[3]=glm::dvec4(center-glm::dvec3(result*glm::dvec4(component.center,0)),1);return result;
    }
    glm::dmat4 Apply(const ClothAsset& cloth,std::vector<glm::dvec3>& targets,const glm::dmat4& fallback){
        glm::dmat4 mainFrame=fallback;std::size_t mainSize=0;
        for(const auto& component:components){const auto frame=Frame(cloth,component,targets,fallback);
            if(!component.pins.empty()&&component.particles.size()>mainSize){mainSize=component.particles.size();mainFrame=frame;}
            for(auto pin:component.pins)residual[pin]=targets[pin]-glm::dvec3(frame*glm::dvec4(cloth.particles[pin].restPosition,1))/cloth.unitsPerMeter;
            for(auto i:component.particles)if(cloth.particles[i].inverseMass>0&&cloth.particles[i].maxDistance>Epsilon){
                auto point=glm::dvec3(frame*glm::dvec4(cloth.particles[i].restPosition,1))/cloth.unitsPerMeter;
                for(std::size_t k=0;k<influenceCounts[i];++k){const auto& anchor=influences[i][k];point+=residual[anchor.pin]*anchor.weight;}
                targets[i]=point;
            }
        }
        return mainFrame;
    }
};
bool MotionTargets(const ClothAsset& cloth,const SkeletalPose& pose,std::vector<glm::dvec3>& targets,AttachmentReference& attachment,glm::dmat4& frame,std::string* error){
    frame=glm::dmat4(1);
    if(cloth.motionBone<0)return true;
    if(std::size_t(cloth.motionBone)>=pose.global.size()){if(error)*error="Cloth motion reference bone is missing from the animated pose";return false;}
    const auto transform=pose.global[cloth.motionBone]*cloth.motionInverseBind;
    if(!Finite(transform)){if(error)*error="Cloth motion reference has a nonfinite animated transform";return false;}
    // Only fixed attachment positions use full skinning. Free points use the
    // fitted attachment frame and its small smooth boundary residual, so thigh
    // and skirt helper bones still cannot drag the cloth reference through legs.
    frame=attachment.Apply(cloth,targets,transform);
    for(std::size_t i=0;i<targets.size();++i)if(cloth.particles[i].inverseMass>0&&cloth.particles[i].maxDistance>Epsilon){
        if(!Finite(targets[i])||Length(targets[i])>1e7){if(error)*error="Cloth motion reference produced an unsupported target position";return false;}}
    return true;
}
glm::dvec3 Closest(glm::dvec3 p,const ClothWorldCapsule& c,double* parameter=nullptr){
    const auto axis=c.b-c.a;const double length2=glm::dot(axis,axis);
    const double t=length2>Epsilon?std::clamp(glm::dot(p-c.a,axis)/length2,0.0,1.0):0;
    if(parameter)*parameter=t;return c.a+axis*t;
}
ClothWorldCapsule Mix(const ClothWorldCapsule& a,const ClothWorldCapsule& b,double t){return {glm::mix(a.a,b.a,t),glm::mix(a.b,b.b,t),a.radius+(b.radius-a.radius)*t};}
struct Cell {std::int64_t x=0,y=0,z=0;bool operator==(const Cell&)const=default;};
struct CellHash {std::size_t operator()(const Cell& c)const noexcept{return std::size_t(std::uint64_t(c.x)*73856093ull^std::uint64_t(c.y)*19349663ull^std::uint64_t(c.z)*83492791ull);}};
Cell CellAt(glm::dvec3 p,double width){return {std::int64_t(std::floor(p.x/width)),std::int64_t(std::floor(p.y/width)),std::int64_t(std::floor(p.z/width))};}
struct TriangleClosest {glm::dvec3 point{0},bary{1,0,0};};
TriangleClosest ClosestTriangle(glm::dvec3 p,glm::dvec3 a,glm::dvec3 b,glm::dvec3 c){
    const auto ab=b-a,ac=c-a,ap=p-a;const double d1=glm::dot(ab,ap),d2=glm::dot(ac,ap);
    if(d1<=0&&d2<=0)return {a,{1,0,0}};
    const auto bp=p-b;const double d3=glm::dot(ab,bp),d4=glm::dot(ac,bp);
    if(d3>=0&&d4<=d3)return {b,{0,1,0}};
    const double vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0){const double v=d1/std::max(Epsilon,d1-d3);return {a+v*ab,{1-v,v,0}};}
    const auto cp=p-c;const double d5=glm::dot(ab,cp),d6=glm::dot(ac,cp);
    if(d6>=0&&d5<=d6)return {c,{0,0,1}};
    const double vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0){const double w=d2/std::max(Epsilon,d2-d6);return {a+w*ac,{1-w,0,w}};}
    const double va=d3*d6-d5*d4;
    if(va<=0&&d4-d3>=0&&d5-d6>=0){const double w=(d4-d3)/std::max(Epsilon,d4-d3+d5-d6);return {b+w*(c-b),{0,1-w,w}};}
    const double sum=va+vb+vc;
    if(std::abs(sum)<=Epsilon){ // Degenerate faces still expose their edges.
        TriangleClosest best;double distance=std::numeric_limits<double>::infinity();const glm::dvec3 v[3]={a,b,c};
        for(int edge=0;edge<3;++edge){const auto direction=v[(edge+1)%3]-v[edge];const double denominator=glm::dot(direction,direction),t=denominator>Epsilon?glm::clamp(glm::dot(p-v[edge],direction)/denominator,0.0,1.0):0;
            const auto point=v[edge]+t*direction;const double d=glm::dot(point-p,point-p);if(d<distance){distance=d;best.point=point;best.bary={0,0,0};best.bary[edge]=1-t;best.bary[(edge+1)%3]=t;}}
        return best;
    }
    const double v=vb/sum,w=vc/sum;return {a+ab*v+ac*w,{1-v-w,v,w}};
}
struct TriangleCapsuleClosest {glm::dvec3 cloth{0},axis{0},bary{1,0,0};double distance=std::numeric_limits<double>::infinity();};
TriangleCapsuleClosest ClosestTriangleCapsule(glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,const ClothWorldCapsule& capsule){
    TriangleCapsuleClosest best;
    // All candidates are compared in squared distance. Taking a square root
    // for each endpoint/edge used to dominate dense surface contact queries.
    double distance2=std::numeric_limits<double>::infinity();int priority=6;
    auto candidate=[&](glm::dvec3 point,glm::dvec3 axis,glm::dvec3 bary,int rank){const auto delta=point-axis;const double d=glm::dot(delta,delta);if(d<distance2||(d==distance2&&rank<priority)){distance2=d;priority=rank;best.cloth=point;best.axis=axis;best.bary=bary;}};
    const auto direction=capsule.b-capsule.a,normal=glm::cross(b-a,c-a);const double denominator=glm::dot(normal,direction),normal2=glm::dot(normal,normal);
    if(std::abs(denominator)>Epsilon){const double t=glm::dot(normal,a-capsule.a)/denominator;if(t>=0&&t<=1){const auto point=capsule.a+direction*t;const auto triangle=ClosestTriangle(point,a,b,c);const auto delta=point-triangle.point;if(glm::dot(delta,delta)<1e-16){candidate(triangle.point,point,triangle.bary,0);
                if(triangle.bary.x>0&&triangle.bary.y>0&&triangle.bary.z>0){best.distance=std::sqrt(std::max(0.0,distance2));return best;}}}}
    const bool planeValid=normal2>Epsilon*Epsilon;
    // Preserve the full degenerate-face path. Otherwise, the three edges give
    // an upper bound before querying either often distant capsule endpoint.
    if(!planeValid){const auto first=ClosestTriangle(capsule.a,a,b,c),second=ClosestTriangle(capsule.b,a,b,c);candidate(first.point,capsule.a,first.bary,1);candidate(second.point,capsule.b,second.bary,2);}
    const glm::dvec3 vertices[3]={a,b,c};const double aa=glm::dot(direction,direction);
    for(int edge=0;edge<3;++edge){const auto e=vertices[(edge+1)%3]-vertices[edge],r=capsule.a-vertices[edge];const double ee=glm::dot(e,e),de=glm::dot(direction,e),dr=glm::dot(direction,r),er=glm::dot(e,r),det=aa*ee-de*de;
        double u=0,v=0;if(aa<=Epsilon){v=ee>Epsilon?glm::clamp(er/ee,0.0,1.0):0;}
        else {u=det>Epsilon?glm::clamp((de*er-ee*dr)/det,0.0,1.0):0;v=ee>Epsilon?(de*u+er)/ee:0;
            if(v<0){v=0;u=glm::clamp(-dr/aa,0.0,1.0);}else if(v>1){v=1;u=glm::clamp((de-dr)/aa,0.0,1.0);}}
        glm::dvec3 bary(0);bary[edge]=1-v;bary[(edge+1)%3]=v;candidate(vertices[edge]+e*v,capsule.a+direction*u,bary,3+edge);
    }
    if(planeValid){int rank=1;for(const auto& endpoint:{capsule.a,capsule.b}){const double signedDistance=glm::dot(normal,endpoint-a);
            // Distance to the infinite supporting plane is a lower bound on
            // distance to the triangle. Reject only beyond a conservative
            // floating-point margin; an exact tie still favors the endpoint,
            // matching the endpoint-before-edge order used by the solver.
            if(signedDistance*signedDistance<=distance2*normal2*(1+1e-12)+normal2*Epsilon*Epsilon){const auto point=ClosestTriangle(endpoint,a,b,c);candidate(point.point,endpoint,point.bary,rank);}++rank;}}
    best.distance=std::sqrt(std::max(0.0,distance2));return best;
}
bool BoundsOverlap(glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,const ClothWorldCapsule& capsule,double thickness){
    const auto minimum=glm::min(a,glm::min(b,c)),maximum=glm::max(a,glm::max(b,c));const auto radius=glm::dvec3(capsule.radius+thickness);
    const auto capsuleMin=glm::min(capsule.a,capsule.b)-radius,capsuleMax=glm::max(capsule.a,capsule.b)+radius;
    return minimum.x<=capsuleMax.x&&maximum.x>=capsuleMin.x&&minimum.y<=capsuleMax.y&&maximum.y>=capsuleMin.y&&minimum.z<=capsuleMax.z&&maximum.z>=capsuleMin.z;
}
double SignedAngle(glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,glm::dvec3 d){
    const auto edge=b-a,opposite=d-a,first=glm::cross(edge,c-a),second=glm::cross(opposite,edge);const double n1=glm::dot(first,first),n2=glm::dot(second,second),length2=glm::dot(edge,edge);
    if(length2<Epsilon*Epsilon||n1<Epsilon*Epsilon||n2<Epsilon*Epsilon)return 0;
    // Both atan2 arguments share |first|*|second|. Cancelling that
    // normalization also reduces the scalar triple product to one dot.
    return detail::FastAtan2(-std::sqrt(length2)*glm::dot(first,opposite),glm::dot(first,second));
}
double AngleDifference(double a,double b){
    constexpr double pi=3.14159265358979323846;const double difference=a-b;
    // Both inputs come from atan2, hence one wrap is sufficient. Avoid a
    // general-purpose remainder division in every angular constraint.
    return difference>pi?difference-2*pi:(difference< -pi?difference+2*pi:difference);
}
void EdgeDiagnostics(const ClothAsset& cloth,const std::vector<glm::dvec3>& positions,
    const std::vector<double>& inverseMass,ClothSimulationStats& stats){
    stats.maxStretchError=0;stats.maxDynamicEdgeError=0;stats.dynamicEdgeError95=0;stats.maxKinematicStretchError=0;
    std::vector<double> dynamicErrors;dynamicErrors.reserve(cloth.stretch.size());
    for(const auto& edge:cloth.stretch){
        const double absolute=std::abs(Length(positions[edge.a]-positions[edge.b])*cloth.unitsPerMeter-edge.restLength);
        const double relative=absolute/std::max(edge.restLength,1e-8*cloth.unitsPerMeter);stats.maxStretchError=std::max(stats.maxStretchError,relative);
        if(inverseMass[edge.a]==0&&inverseMass[edge.b]==0)stats.maxKinematicStretchError=std::max(stats.maxKinematicStretchError,relative);
        else {stats.maxDynamicEdgeError=std::max(stats.maxDynamicEdgeError,absolute);dynamicErrors.push_back(absolute);}
    }
    if(!dynamicErrors.empty()){
        const auto index=std::min(dynamicErrors.size()-1,std::size_t(std::ceil(dynamicErrors.size()*.95))-1);
        std::nth_element(dynamicErrors.begin(),dynamicErrors.begin()+index,dynamicErrors.end());stats.dynamicEdgeError95=dynamicErrors[index];
    }
}
}

std::vector<ClothWorldCapsule> EvaluateClothCapsules(const ClothAsset& cloth,const SkeletalPose& pose){
    std::vector<ClothWorldCapsule> result;result.reserve(cloth.capsules.size());
    for(const auto& c:cloth.capsules){
        if(c.boneA< -1||c.boneB< -1||(c.boneA>=0&&std::size_t(c.boneA)>=pose.global.size())||(c.boneB>=0&&std::size_t(c.boneB)>=pose.global.size()))continue;
        const auto a=c.boneA<0?glm::dmat4(1):pose.global[c.boneA],b=c.boneB<0?glm::dmat4(1):pose.global[c.boneB];
        auto affine=[](const glm::dmat4& m){return Finite(m)&&std::abs(m[0][3])<Epsilon&&std::abs(m[1][3])<Epsilon&&std::abs(m[2][3])<Epsilon&&std::abs(m[3][3]-1)<Epsilon;};
        if(!affine(a)||!affine(b))continue;
        auto conservativeScale=[](const glm::dmat4& m){
            double maximum=0,sum2=0;bool orthogonal=true;
            for(int i=0;i<3;++i){const auto column=glm::dvec3(m[i]);const double length=Length(column);maximum=std::max(maximum,length);sum2+=length*length;
                for(int j=0;j<i;++j)if(std::abs(glm::dot(column,glm::dvec3(m[j])))>1e-8*std::max(Epsilon,length*Length(glm::dvec3(m[j]))))orthogonal=false;}
            return orthogonal?maximum:std::sqrt(sum2); // conservative for inherited shear
        };
        const double scale=std::max(conservativeScale(a),conservativeScale(b));
        ClothWorldCapsule world{glm::dvec3(a*glm::dvec4(c.localA,1)),glm::dvec3(b*glm::dvec4(c.localB,1)),c.radius*scale};
        if(Finite(world.a)&&Finite(world.b)&&Finite(world.radius)&&world.radius>0)result.push_back(world);
    }
    return result;
}

struct ClothSimulator::Impl {
    using Hinge=detail::ClothHinge;
    struct ConstraintColors {
        std::vector<std::uint32_t> order,offsets;
        template<class Vertices> bool Build(std::size_t count,std::size_t particleCount,const Vertices& vertices){
            order.clear();offsets.clear();if(count>std::numeric_limits<std::uint32_t>::max())return false;if(count==0){offsets={0};return true;}
            std::vector<std::uint64_t> used(particleCount,0);std::vector<std::uint32_t> colors(count),sizes;
            for(std::size_t k=0;k<count;++k){std::uint64_t forbidden=0;const auto indices=vertices(k);for(auto particle:indices)forbidden|=used.data()[particle];
                // A highly connected exceptional topology falls back to the
                // validated serial solver instead of imposing a color limit.
                if(forbidden==std::numeric_limits<std::uint64_t>::max()){order.clear();return false;}
                const auto color=std::uint32_t(std::countr_one(forbidden));colors.data()[k]=color;
                if(sizes.size()<=color)sizes.resize(color+1);++sizes.data()[color];for(auto particle:indices)used.data()[particle]|=std::uint64_t(1)<<color;
            }
            offsets.resize(sizes.size()+1,0);for(std::size_t c=0;c<sizes.size();++c)offsets.data()[c+1]=offsets.data()[c]+sizes.data()[c];
            auto cursors=offsets;order.resize(count);for(std::uint32_t k=0;k<count;++k)order.data()[cursors.data()[colors.data()[k]]++]=k;
            return true;
        }
    };
    std::uint64_t signature=0;double units=1,accumulator=0;bool initialized=false,motionInitialized=false,needsWarmStart=true;
    std::vector<glm::dvec3> x,velocity,previousTargets,positions;
    std::vector<Hinge> hinges;
    std::vector<double> restAreas,restAreaScales,triangleSizes;
    std::vector<glm::dvec3> triangleNormals;
    std::vector<std::vector<std::uint32_t>> incidentTriangles;
    std::unordered_set<std::size_t> debugCollapsedTriangles;
    double debugWorstPenetration=0;
    std::vector<ClothWorldCapsule> previousCapsules;
    std::vector<ClothWorldConvex> previousConvexes;
    std::vector<double> inverseMass;
    AttachmentReference attachment;
    std::unordered_set<std::uint64_t> adjacent;
    ConstraintColors stretchColors,bendColors,triangleColors,hingeColors;
    std::unique_ptr<detail::ClothParallelExecutor> executor;
    bool coloredTopology=false;
    ClothSimulationStats stats;
    void Publish(){positions.resize(x.size());for(std::size_t i=0;i<x.size();++i)positions[i]=x[i]*units;}
};

ClothSimulator::ClothSimulator():impl_(std::make_unique<Impl>()){}
ClothSimulator::~ClothSimulator()=default;
ClothSimulator::ClothSimulator(ClothSimulator&&)noexcept=default;
ClothSimulator& ClothSimulator::operator=(ClothSimulator&&)noexcept=default;

bool ClothSimulator::Reset(const ClothAsset& cloth,const StaticModel& animatedGarment,std::string* error){
    if(!AssetValid(cloth,error))return false;
    std::vector<glm::dvec3> targets;if(!AnimatedTargets(cloth,animatedGarment,targets,error))return false;
    if(!impl_)impl_=std::make_unique<Impl>();auto& s=*impl_;
    const auto signature=Signature(cloth);
    if(!s.initialized||s.signature!=signature)s.attachment.Build(cloth);
    s.signature=signature;s.units=cloth.unitsPerMeter;s.accumulator=0;s.x=targets;s.previousTargets=targets;
    s.velocity.assign(targets.size(),glm::dvec3(0));s.inverseMass.resize(targets.size());s.previousCapsules.clear();s.previousConvexes.clear();s.adjacent.clear();
    s.motionInitialized=cloth.motionBone<0;s.needsWarmStart=true;s.hinges.clear();s.restAreas.clear();s.restAreaScales.clear();s.triangleSizes.clear();s.triangleNormals.clear();s.incidentTriangles.assign(targets.size(),{});s.debugCollapsedTriangles.clear();s.debugWorstPenetration=0;
    const auto resets=s.stats.resets+1;s.stats={};s.stats.resets=resets;s.stats.particles=targets.size();
    for(std::size_t i=0;i<targets.size();++i){s.inverseMass[i]=cloth.particles[i].maxDistance<=Epsilon?0:cloth.particles[i].inverseMass;if(s.inverseMass[i]==0)++s.stats.pinned;}
    for(const auto& t:cloth.triangles){s.adjacent.insert(Pair(t[0],t[1]));s.adjacent.insert(Pair(t[1],t[2]));s.adjacent.insert(Pair(t[2],t[0]));}
    for(const auto* constraints:{&cloth.stretch,&cloth.bend})for(const auto& c:*constraints)s.adjacent.insert(Pair(c.a,c.b));
    std::map<std::uint64_t,std::vector<std::uint32_t>> opposites;
    for(const auto& tri:cloth.triangles){const auto triangle=std::uint32_t(s.restAreas.size());for(auto particle:tri)s.incidentTriangles[particle].push_back(triangle);const auto a=cloth.particles[tri[0]].restPosition/s.units,b=cloth.particles[tri[1]].restPosition/s.units,c=cloth.particles[tri[2]].restPosition/s.units;s.restAreas.push_back(Length(glm::cross(b-a,c-a))*.5);s.triangleSizes.push_back(std::max({Length(b-a),Length(c-b),Length(a-c)}));
        s.triangleNormals.push_back(Unit(glm::cross(targets[tri[1]]-targets[tri[0]],targets[tri[2]]-targets[tri[0]]),Unit(glm::cross(b-a,c-a))));
        for(int edge=0;edge<3;++edge)opposites[Pair(tri[edge],tri[(edge+1)%3])].push_back(tri[(edge+2)%3]);}
    for(const auto& [edge,opposite]:opposites)if(opposite.size()==2&&opposite[0]!=opposite[1]){const std::uint32_t a=std::uint32_t(edge>>32),b=std::uint32_t(edge);s.hinges.push_back({{a,b,opposite[0],opposite[1]},SignedAngle(cloth.particles[a].restPosition/s.units,cloth.particles[b].restPosition/s.units,cloth.particles[opposite[0]].restPosition/s.units,cloth.particles[opposite[1]].restPosition/s.units)});}
    s.restAreaScales.reserve(s.restAreas.size());for(double area:s.restAreas)s.restAreaScales.push_back(area>Epsilon?1/std::sqrt(area):0);
    s.coloredTopology=targets.size()>=512&&(cloth.triangles.size()>=768||cloth.stretch.size()>=1024);
    if(s.coloredTopology){
        s.coloredTopology=s.stretchColors.Build(cloth.stretch.size(),targets.size(),[&](std::size_t k){const auto& c=cloth.stretch.data()[k];return std::array<std::uint32_t,2>{c.a,c.b};})&&
            s.bendColors.Build(cloth.bend.size(),targets.size(),[&](std::size_t k){const auto& c=cloth.bend.data()[k];return std::array<std::uint32_t,2>{c.a,c.b};})&&
            s.triangleColors.Build(cloth.triangles.size(),targets.size(),[&](std::size_t k){return cloth.triangles.data()[k];})&&
            s.hingeColors.Build(s.hinges.size(),targets.size(),[&](std::size_t k){return s.hinges.data()[k].vertices;});
        if(s.coloredTopology&&!s.executor)s.executor=std::make_unique<detail::ClothParallelExecutor>();
    }
    s.initialized=true;EdgeDiagnostics(cloth,s.x,s.inverseMass,s.stats);s.Publish();return true;
}

bool ClothSimulator::Reset(const ClothAsset& cloth,const SkeletalPose& pose,const StaticModel& animatedGarment,std::string* error){
    if(!Reset(cloth,animatedGarment,error))return false;auto& s=*impl_;auto targets=s.previousTargets;
    glm::dmat4 frame;
    if(!MotionTargets(cloth,pose,targets,s.attachment,frame,error))return false;s.x=targets;s.previousTargets=targets;s.motionInitialized=true;EdgeDiagnostics(cloth,s.x,s.inverseMass,s.stats);s.Publish();return true;
}

bool ClothSimulator::Step(const ClothAsset& cloth,const SkeletalPose& pose,const StaticModel& animatedGarment,
    double deltaSeconds,const ClothSolverSettings& settings,std::string* error){
    const auto started=std::chrono::steady_clock::now();
    const bool profiling=ProfileEnabled();double prepareMilliseconds=0,diagnosticMilliseconds=0,distanceMilliseconds=0,areaMilliseconds=0,barrierMilliseconds=0,bendMilliseconds=0,particleMilliseconds=0,surfaceMilliseconds=0;std::uint32_t solvedSubsteps=0;
    double convexSweepPrepareMilliseconds=0;
    std::uint64_t profileSweepCalls=0,profileSweepRebuilds=0,profileSweepBodyGrids=0,profileSweepSamples=0,profileSweepMidShapes=0;
    double profileMaximumNetDelta2=0,profileLastNetDelta2=0,profileMaximumBarrierDelta2=0,profileLastBarrierDelta2=0,iterationBarrierDelta2=0;
    std::uint64_t profileBarrierRepairs=0,profileBarrierVetoIterations=0,profileLastBarrierRepairs=0,iterationBarrierRepairs=0;
    std::size_t profileLastUnsettledParticles=0,profileLastWorstParticle=0;
    auto fail=[&](const char* message){if(error)*error=message;return false;};
    if(!Finite(deltaSeconds)||deltaSeconds<0)return fail("Cloth delta time must be finite and nonnegative; reset explicitly when rewinding");
    if(!SettingsValid(settings,error)||!AssetValid(cloth,error))return false;
    if(!impl_||!impl_->initialized||impl_->signature!=Signature(cloth))if(!Reset(cloth,pose,animatedGarment,error))return false;
    auto& s=*impl_;s.stats.constraintIterations=0;std::vector<glm::dvec3> targets;glm::dmat4 attachmentFrame;
    if(!AnimatedTargets(cloth,animatedGarment,targets,error)||!MotionTargets(cloth,pose,targets,s.attachment,attachmentFrame,error))return false;
    if(!s.motionInitialized){s.x=targets;s.previousTargets=targets;s.motionInitialized=true;s.Publish();}
    auto capsules=EvaluateClothCapsules(cloth,pose);
    if(capsules.size()!=cloth.capsules.size())return fail("Cloth collider references an invalid or nonfinite animated bone transform");
    for(auto& c:capsules){c.a/=s.units;c.b/=s.units;c.radius/=s.units;
        if(!Finite(c.a)||!Finite(c.b)||!Finite(c.radius)||Length(c.a)>1e7||Length(c.b)>1e7||c.radius>1000)return fail("Animated cloth capsule exceeds the supported finite SI range");}
    auto convexes=EvaluateClothConvexes(cloth,pose);
    if(convexes.size()!=cloth.convexes.size())return fail("Cloth convex body references an invalid or nonfinite animated bone transform");
    const auto toSI=glm::scale(glm::dmat4(1),glm::dvec3(1/s.units));
    for(auto& shape:convexes){
        for(auto& v:shape.vertices){v/=s.units;if(!Finite(v)||Length(v)>1e7)return fail("Animated cloth convex body exceeds the supported finite SI range");}
        for(auto& p:shape.planes)p.distance/=s.units;
        shape.minimum/=s.units;shape.maximum/=s.units;shape.transform=toSI*shape.transform;
    }
    // Only fixed vertices bordering a movable face need a material offset.
    // All-fixed attachments remain immovable, and their raw conflicts are
    // still measured below without allocating a body-by-all-particles table.
    constexpr auto noPin=std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> pinOrdinal(convexes.empty()?0:s.x.size(),noPin),pinIndices;
    if(!convexes.empty())for(const auto& triangle:cloth.triangles){if(s.inverseMass[triangle[0]]+s.inverseMass[triangle[1]]+s.inverseMass[triangle[2]]<=0)continue;
        for(auto particle:triangle)if(s.inverseMass[particle]==0&&pinOrdinal[particle]==noPin){pinOrdinal[particle]=std::uint32_t(pinIndices.size());pinIndices.push_back(particle);}}
    auto fillPinOffsets=[&](const ClothWorldConvex& shape,const auto& targetForPin,std::vector<double>& output,
            const detail::PreparedClothConvexQueries* prepared=nullptr){
        output.resize(pinIndices.size());for(std::size_t pin=0;pin<pinIndices.size();++pin)
            output[pin]=detail::ConvexKinematicContactOffset(shape,targetForPin(pinIndices[pin]),settings.thickness,prepared);
    };
    auto triangleOffsets=[&](const auto& triangle,const std::vector<double>& values){glm::dvec3 offsets(settings.thickness);
        for(int vertex=0;vertex<3;++vertex)if(pinOrdinal[triangle[vertex]]!=noPin)offsets[vertex]=values[pinOrdinal[triangle[vertex]]];return offsets;};
    std::vector<std::vector<double>> targetPinOffsets(convexes.size());
    for(std::size_t body=0;body<convexes.size();++body)fillPinOffsets(convexes[body],[&](std::size_t pin){return targets[pin];},targetPinOffsets[body]);
    auto diagnostics=[&](){
        ProfileScope timing(diagnosticMilliseconds,profiling);
        s.stats.maxDisplacement=0;s.stats.maxPenetration=0;s.stats.maxAttachmentError=0;s.stats.infeasibleAnimatedTargets=0;s.stats.kinematicColliderOverlaps=0;
        s.stats.maxKinematicColliderPenetration=0;s.stats.maxResolvableSurfacePenetration=0;
        for(std::size_t i=0;i<s.x.size();++i){
            s.stats.maxDisplacement=std::max(s.stats.maxDisplacement,Length(s.x[i]-targets[i])*s.units);double targetPenetration=0;
            if(s.inverseMass[i]==0)s.stats.maxAttachmentError=std::max(s.stats.maxAttachmentError,Length(s.x[i]-targets[i])*s.units);
            for(const auto& c:capsules){targetPenetration=std::max(targetPenetration,c.radius+settings.thickness-Length(targets[i]-Closest(targets[i],c)));
                if(settings.collision&&s.inverseMass[i]>0)s.stats.maxPenetration=std::max(s.stats.maxPenetration,std::max(0.0,c.radius+settings.thickness-Length(s.x[i]-Closest(s.x[i],c)))*s.units);}
            for(const auto& c:convexes){const auto target=detail::ConvexPointContact(c,targets[i],settings.thickness);
                if(target.hit)targetPenetration=std::max(targetPenetration,settings.thickness-target.separation);
                if(settings.collision&&s.inverseMass[i]>0){const auto contact=detail::ConvexPointContact(c,s.x[i],settings.thickness);
                    if(contact.hit)s.stats.maxPenetration=std::max(s.stats.maxPenetration,std::max(0.0,settings.thickness-contact.separation)*s.units);}}
            // A single capsule already proves an authored animation sphere is
            // infeasible when its closest free point is outside MaxDistance.
            // Count particles once, even when several body capsules overlap.
            if(s.inverseMass[i]==0){if(targetPenetration>1e-7)++s.stats.kinematicColliderOverlaps;
                s.stats.maxKinematicColliderPenetration=std::max(s.stats.maxKinematicColliderPenetration,targetPenetration*s.units);}
            else if(targetPenetration>cloth.particles[i].maxDistance/s.units+1e-7)++s.stats.infeasibleAnimatedTargets;
        }
        s.stats.maxSurfacePenetration=0;s.stats.collapsedTriangles=0;s.stats.maxBendDeviation=0;
        std::size_t worstTriangle=0,worstCapsule=0;TriangleCapsuleClosest worstContact;bool worstIsConvex=false;
        for(std::size_t t=0;t<cloth.triangles.size();++t){const auto& tri=cloth.triangles[t];const auto a=s.x[tri[0]],b=s.x[tri[1]],c=s.x[tri[2]];const double area=Length(glm::cross(b-a,c-a))*.5;
            if(s.restAreas[t]>Epsilon&&area<s.restAreas[t]*.03){++s.stats.collapsedTriangles;
                if(DiagnosticsEnabled()&&s.debugCollapsedTriangles.insert(t).second)std::fprintf(stderr,"cloth collapse step %llu collision=%d tri %zu [%u,%u,%u] areaRatio %.7f invmass %.3g %.3g %.3g edges %.5f %.5f %.5fm restSize %.5fm\n",static_cast<unsigned long long>(s.stats.steps),int(settings.collision),t,tri[0],tri[1],tri[2],area/s.restAreas[t],s.inverseMass[tri[0]],s.inverseMass[tri[1]],s.inverseMass[tri[2]],Length(a-b),Length(b-c),Length(c-a),s.triangleSizes[t]);}
            if(settings.collision&&s.inverseMass[tri[0]]+s.inverseMass[tri[1]]+s.inverseMass[tri[2]]>0)for(std::size_t ci=0;ci<capsules.size();++ci){const auto& cap=capsules[ci];if(BoundsOverlap(a,b,c,cap,settings.thickness)){const auto closest=ClosestTriangleCapsule(a,b,c,cap);const double penetration=std::max(0.0,cap.radius+settings.thickness-closest.distance)*s.units;
                s.stats.maxResolvableSurfacePenetration=std::max(s.stats.maxResolvableSurfacePenetration,penetration);
                if(penetration>s.stats.maxSurfacePenetration){s.stats.maxSurfacePenetration=penetration;worstTriangle=t;worstCapsule=ci;worstContact=closest;worstIsConvex=false;}}}
            if(settings.collision&&s.inverseMass[tri[0]]+s.inverseMass[tri[1]]+s.inverseMass[tri[2]]>0)for(std::size_t ci=0;ci<convexes.size();++ci){const auto closest=detail::ConvexTriangleContact(convexes[ci],a,b,c,settings.thickness);
                if(closest.hit){const double penetration=std::max(0.0,settings.thickness-closest.separation)*s.units;
                    if(penetration>s.stats.maxSurfacePenetration){s.stats.maxSurfacePenetration=penetration;worstTriangle=t;worstCapsule=ci;worstContact.bary=closest.bary;worstIsConvex=true;}}
                const auto offsets=triangleOffsets(tri,targetPinOffsets[ci]);
                const auto effective=offsets==glm::dvec3(settings.thickness)?closest:detail::ConvexTriangleContactOffsets(convexes[ci],a,b,c,offsets,settings.thickness);
                if(effective.hit)s.stats.maxResolvableSurfacePenetration=std::max(s.stats.maxResolvableSurfacePenetration,
                    std::max(0.0,detail::ConvexInterpolatedContactOffset(offsets,effective.bary)-effective.separation)*s.units);
            }}
        if(DiagnosticsEnabled()&&s.stats.maxSurfacePenetration/s.units>std::max(.01,s.debugWorstPenetration+.002)){
            s.debugWorstPenetration=s.stats.maxSurfacePenetration/s.units;const auto& tri=cloth.triangles[worstTriangle];
            std::fprintf(stderr,"cloth surface step %llu: raw=%.6fm resolvable=%.6fm pinRaw=%.6fm tri %zu [%u,%u,%u] %s %zu bary %.6f %.6f %.6f invmass %.3g %.3g %.3g edges %.5f %.5f %.5fm restSize %.5fm\n",static_cast<unsigned long long>(s.stats.steps),s.debugWorstPenetration,s.stats.maxResolvableSurfacePenetration/s.units,s.stats.maxKinematicColliderPenetration/s.units,worstTriangle,tri[0],tri[1],tri[2],worstIsConvex?"convex":"cap",worstCapsule,worstContact.bary.x,worstContact.bary.y,worstContact.bary.z,s.inverseMass[tri[0]],s.inverseMass[tri[1]],s.inverseMass[tri[2]],Length(s.x[tri[0]]-s.x[tri[1]]),Length(s.x[tri[1]]-s.x[tri[2]]),Length(s.x[tri[2]]-s.x[tri[0]]),s.triangleSizes[worstTriangle]);}
        for(const auto& hinge:s.hinges){const auto& v=hinge.vertices;s.stats.maxBendDeviation=std::max(s.stats.maxBendDeviation,std::abs(AngleDifference(SignedAngle(s.x[v[0]],s.x[v[1]],s.x[v[2]],s.x[v[3]]),hinge.angle)));}
        EdgeDiagnostics(cloth,s.x,s.inverseMass,s.stats);
    };
    if(s.previousCapsules.size()!=capsules.size())s.previousCapsules=capsules;
    if(s.previousConvexes.size()!=convexes.size())s.previousConvexes=convexes;
    double targetMotion=0;for(std::size_t i=0;i<targets.size();++i)targetMotion=std::max(targetMotion,Length(targets[i]-s.previousTargets[i]));
    // A seek/teleport must not turn an arbitrarily large animation jump into an
    // unbounded inertial impulse. Ordinary frames retain velocity and lag.
    const bool discontinuity=deltaSeconds>.25||s.accumulator+deltaSeconds>settings.fixedTimeStep*16+.000001||targetMotion>.75;
    if(discontinuity){if(!Reset(cloth,pose,animatedGarment,error))return false;return Step(cloth,pose,animatedGarment,0,settings,error);}
    std::uint32_t warmTicks=0;
    if(s.needsWarmStart){s.needsWarmStart=false;
        const bool referenceAvailable=(!cloth.capsules.empty()||!cloth.convexes.empty())&&
            std::all_of(cloth.capsules.begin(),cloth.capsules.end(),[](const auto& capsule){return capsule.referenceValid;})&&
            std::all_of(cloth.convexes.begin(),cloth.convexes.end(),[](const auto& convex){return convex.referenceValid;});
        bool deepIntersection=false;
        if(referenceAvailable&&settings.collision)for(const auto& tri:cloth.triangles){if(s.inverseMass[tri[0]]+s.inverseMass[tri[1]]+s.inverseMass[tri[2]]<=0)continue;
            for(const auto& capsule:capsules)if(BoundsOverlap(s.x[tri[0]],s.x[tri[1]],s.x[tri[2]],capsule,settings.thickness)){
                const auto closest=ClosestTriangleCapsule(s.x[tri[0]],s.x[tri[1]],s.x[tri[2]],capsule);if(capsule.radius+settings.thickness-closest.distance>settings.thickness){deepIntersection=true;break;}}
            if(!deepIntersection)for(std::size_t body=0;body<convexes.size();++body){const auto offsets=triangleOffsets(tri,targetPinOffsets[body]);
                if(offsets==glm::dvec3(settings.thickness)){
                    const auto contact=detail::ConvexTriangleContact(convexes[body],s.x[tri[0]],s.x[tri[1]],s.x[tri[2]],0);
                    if(contact.hit&&contact.separation<0){deepIntersection=true;break;}
                }else {const auto contact=detail::ConvexTriangleContactOffsets(convexes[body],s.x[tri[0]],s.x[tri[1]],s.x[tri[2]],offsets,settings.thickness);
                    if(contact.hit&&detail::ConvexInterpolatedContactOffset(offsets,contact.bary)-contact.separation>settings.thickness){deepIntersection=true;break;}}}
            if(deepIntersection)break;
        }
        if(deepIntersection){
            const auto& frame=attachmentFrame;double frameScale=0;
            for(int i=0;i<3;++i)frameScale=std::max(frameScale,Length(glm::dvec3(frame[i])));
            s.previousCapsules.clear();for(const auto& capsule:cloth.capsules)s.previousCapsules.push_back({glm::dvec3(frame*glm::dvec4(capsule.restA,1))/s.units,glm::dvec3(frame*glm::dvec4(capsule.restB,1))/s.units,capsule.radius*frameScale/s.units});
            s.previousConvexes.clear();for(const auto& convex:cloth.convexes){ClothWorldConvex shape;
                if(!detail::BuildClothWorldConvex(convex,toSI*frame*convex.referenceTransform,shape))return fail("Cloth convex neutral reference transform is invalid");
                s.previousConvexes.push_back(std::move(shape));}
            // A motion may start with a raised thigh already cutting through
            // the neutral skirt. Endpoint projection would place neighboring
            // vertices on opposite sides and create an unrecoverable needle.
            // Sweep the neutral collision rig into this pose before exposing
            // the frame; these initialization ticks are not animation time.
            warmTicks=std::max(1u,(60u+settings.substeps-1)/settings.substeps);s.previousTargets=targets;s.accumulator=0;
        }
    }
    if(deltaSeconds==0&&warmTicks==0){
        for(std::size_t i=0;i<targets.size();++i)if(s.inverseMass[i]==0){s.x[i]=targets[i];s.velocity[i]={0,0,0};}
        s.previousTargets=targets;s.previousCapsules=capsules;s.previousConvexes=convexes;diagnostics();s.Publish();
        s.stats.simulationMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();return true;
    }
    // Asset indices and finite ranges were validated above. Cache contiguous
    // storage here so checked STL iterators do not enter per-constraint math.
    // None of these arrays are resized during a solve; Reset exits immediately.
    const std::size_t particleCount=s.x.size(),triangleCount=cloth.triangles.size(),hingeCount=s.hinges.size(),tetherCount=cloth.tethers.size(),capsuleCount=capsules.size(),convexCount=convexes.size();
    auto* positions=s.x.data();auto* velocity=s.velocity.data();const auto* inverseMass=s.inverseMass.data();const auto* previousTargets=s.previousTargets.data();const auto* previousCapsules=s.previousCapsules.data();const auto* targetPositions=targets.data();const auto* targetCapsules=capsules.data();
    const auto* particles=cloth.particles.data();const auto* triangles=cloth.triangles.data();const auto* hinges=s.hinges.data();const auto* tethers=cloth.tethers.data();const auto* restAreas=s.restAreas.data();const auto* restAreaScales=s.restAreaScales.data();const auto* triangleSizes=s.triangleSizes.data();auto* triangleNormals=s.triangleNormals.data();
    const double warmDuration=warmTicks*settings.fixedTimeStep,submissionDuration=deltaSeconds;
    const double oldAccumulator=s.accumulator;s.accumulator+=deltaSeconds+warmDuration;
    s.stats.contacts=0;s.stats.selfContacts=0;s.stats.maxStretchError=0;s.stats.maxPenetration=0;
    std::vector<std::uint8_t> contactedStorage(particleCount,0);auto* contacted=contactedStorage.data();std::unordered_set<std::uint64_t> selfContacts;
    std::vector<glm::dvec3> subTargetStorage(particleCount),beforeStorage(particleCount),accelerationStorage(particleCount);auto* subTargets=subTargetStorage.data();auto* before=beforeStorage.data();auto* acceleration=accelerationStorage.data();
    std::vector<glm::dvec3> areaCorrectionStorage(particleCount);std::vector<double> areaWeightStorage(particleCount);auto* areaCorrections=areaCorrectionStorage.data();auto* areaWeights=areaWeightStorage.data();
    std::vector<std::uint32_t> areaActive,areaNext,areaTouched;std::vector<std::uint8_t> areaMarkedStorage(triangleCount,0);auto* areaMarked=areaMarkedStorage.data();
    std::vector<ClothWorldCapsule> subCapsuleStorage(capsuleCount),startCapsuleStorage(capsuleCount);auto* subCapsules=subCapsuleStorage.data();auto* startCapsules=startCapsuleStorage.data();
    std::vector<glm::dvec3> capsuleMinimumStorage(capsuleCount),capsuleMaximumStorage(capsuleCount),sweptCapsuleMinimumStorage(capsuleCount),sweptCapsuleMaximumStorage(capsuleCount);auto* capsuleMinimum=capsuleMinimumStorage.data();auto* capsuleMaximum=capsuleMaximumStorage.data();auto* sweptCapsuleMinimum=sweptCapsuleMinimumStorage.data();auto* sweptCapsuleMaximum=sweptCapsuleMaximumStorage.data();
    std::vector<glm::dvec3> capsuleAxisMinimumStorage(capsuleCount),capsuleAxisMaximumStorage(capsuleCount);auto* capsuleAxisMinimum=capsuleAxisMinimumStorage.data();auto* capsuleAxisMaximum=capsuleAxisMaximumStorage.data();
    std::vector<double> capsuleTravelStorage(capsuleCount);auto* capsuleTravel=capsuleTravelStorage.data();
    std::vector<detail::PreparedClothConvexMotion> convexMotions(convexCount);
    struct ConvexSubstep {
        ClothWorldConvex start,end;
        detail::PreparedClothConvexMotion motion;
        detail::PreparedClothConvexQueries startQueries,endQueries;
        std::vector<ClothWorldConvex> samples;
        std::vector<detail::PreparedClothConvexQueries> sampleQueries;
        detail::ClothConvexSampleBounds sampleBounds;
        std::vector<double> startPinOffsets,endPinOffsets;
        std::vector<std::vector<double>> samplePinOffsets;
        std::uint32_t sampleCount=0;
        double interiorRadius=1e-5;
        glm::dmat4 inverseEnd{1};
        const ClothWorldConvex& Sample(std::size_t index)const{return motion.changed?samples[index]:end;}
        const detail::PreparedClothConvexQueries& SampleQueries(std::size_t index)const{return motion.changed?sampleQueries[index]:endQueries;}
    };
    std::vector<ConvexSubstep> subConvexes(convexCount);
    const bool sampleBoundsGate=!EnvironmentValueIs("MODEL_CLOTH_CCD_SAMPLE_BOUNDS",'0');
    constexpr std::uint16_t noSeparatingPlane=std::numeric_limits<std::uint16_t>::max();
    constexpr std::size_t maximumSeparatingCachePairs=1000000;
    const bool separatingCacheEnabled=convexCount&&!EnvironmentValueIs("MODEL_CLOTH_SEPARATING_CACHE",'0')&&
        particleCount<=maximumSeparatingCachePairs/convexCount&&triangleCount<=maximumSeparatingCachePairs/convexCount;
    std::vector<std::uint16_t> particleSeparatingStorage(separatingCacheEnabled?particleCount*convexCount:0,noSeparatingPlane),
        triangleSeparatingStorage(separatingCacheEnabled?triangleCount*convexCount:0,noSeparatingPlane);
    constexpr std::size_t maximumCompositePairs=250000;
    static_assert(sizeof(detail::ClothConvexCompositeSeparator)<=16);
    const bool compositeCacheEnabled=separatingCacheEnabled&&!EnvironmentValueIs("MODEL_CLOTH_COMPOSITE_CACHE",'0')&&
        triangleCount<=maximumCompositePairs/convexCount;
    std::vector<detail::ClothConvexCompositeSeparator> compositeStorage(compositeCacheEnabled?triangleCount*convexCount:0);
    // 0 not attempted, 1 failed once, 2 certificate available. Reset only this
    // byte per pair/substep; never repeat an unsuccessful O(P) search.
    std::vector<std::uint8_t> compositeState(compositeStorage.size(),0);
    auto* particleSeparating=particleSeparatingStorage.data();auto* triangleSeparating=triangleSeparatingStorage.data();
    std::vector<std::uint64_t> particleSeparatingHits(profiling&&separatingCacheEnabled?particleCount:0),
        triangleSeparatingHits(profiling&&separatingCacheEnabled?triangleCount:0);
    // CCD counts are attempted initial/sample tests, including inline AABB
    // misses; only the explicitly named EndExact counters count full helpers.
    std::vector<std::uint64_t> profilePointExact(profiling?particleCount:0),profilePointAabb(profiling?particleCount:0),
        profilePointCcd(profiling?particleCount:0),profileTriangleExact(profiling?triangleCount:0),
        profileTriangleComposite(profiling?triangleCount:0),profileTriangleCcd(profiling?triangleCount:0),
        profileTriangleLocalMidShapes(profiling?triangleCount:0),profileCompositeAttempts(profiling?triangleCount:0),
        profileCompositeFound(profiling?triangleCount:0),profileCompositeReuse(profiling?triangleCount:0);
    for(std::size_t i=0;i<convexCount;++i)if(!convexMotions[i].Prepare(cloth.convexes[i],s.previousConvexes[i],convexes[i]))
        return fail("Cloth convex motion requires finite, nonsingular affine transforms");
    bool convexSweepReady=false;
    double convexPreparedClothTravel=0;
    // The experiment stays opt-in until full-clip quality and timing pass.
    // Check division before multiplication: valid large assets must fall back
    // rather than overflow the pair count or allocate an unbounded manifold.
    constexpr std::size_t maximumSurfaceCachePairs=1000000;
    const bool surfaceCacheEnabled=settings.collision&&capsuleCount>0&&triangleCount>0&&triangleCount<=maximumSurfaceCachePairs/capsuleCount&&EnvironmentFlag("MODEL_CLOTH_CONTACT_CACHE")&&!EnvironmentFlag("MODEL_CLOTH_FULL_NARROWPHASE");
    struct SurfacePairCache {glm::dvec3 bary{1,0,0};double distance=0,motion=0;std::uint32_t substep=0;};
    struct SurfaceTriangleCache {std::array<glm::dvec3,3> observed;double motion=0;std::uint32_t substep=0;std::uint64_t exact=0,reused=0,separated=0,invalidated=0;};
    std::vector<SurfacePairCache> surfacePairStorage(surfaceCacheEnabled?triangleCount*capsuleCount:0);auto* surfacePairs=surfacePairStorage.data();
    std::vector<SurfaceTriangleCache> surfaceTriangleStorage(surfaceCacheEnabled?triangleCount:0);auto* surfaceTriangles=surfaceTriangleStorage.data();
    std::vector<double> stretchLambdaStorage(cloth.stretch.size()),bendLambdaStorage(std::max(cloth.bend.size(),hingeCount)),areaLambdaStorage(triangleCount),driveLambdaStorage(particleCount),tetherLambdaStorage(tetherCount);auto* stretchLambda=stretchLambdaStorage.data();auto* bendLambda=bendLambdaStorage.data();auto* areaLambda=areaLambdaStorage.data();auto* driveLambda=driveLambdaStorage.data();auto* tetherLambda=tetherLambdaStorage.data();
    const double h=settings.fixedTimeStep/settings.substeps;double tickTime=settings.fixedTimeStep-oldAccumulator;
    const double convergenceTolerance=settings.selfCollision?0:std::min(settings.convergenceTolerance,settings.thickness*.1);
    std::vector<glm::dvec3> iterationStartStorage(convergenceTolerance>0||profiling?particleCount:0);auto* iterationStart=iterationStartStorage.data();
    const double globalDampingDecay=std::exp(-settings.damping*h);
    using DistancePrepared=detail::ClothDistancePrepared;
    auto prepareDistance=[&](const auto& constraints,double compliance){const double alpha=compliance/(h*h);std::vector<DistancePrepared> prepared(constraints.size());const auto* source=constraints.data();auto* destination=prepared.data();
        for(std::size_t k=0;k<constraints.size();++k){const auto& c=source[k];auto& cached=destination[k];cached.rest=c.restLength/s.units;cached.massSum=inverseMass[c.a]+inverseMass[c.b];cached.inverseDenominator=cached.massSum>0?1/(cached.massSum+alpha):0;}return prepared;};
    const auto stretchPrepared=prepareDistance(cloth.stretch,settings.stretchCompliance);
    const auto bendPrepared=hingeCount==0?prepareDistance(cloth.bend,settings.bendCompliance):std::vector<DistancePrepared>{};
    std::vector<double> driveAlphaStorage(particleCount),driveDecayStorage(particleCount),maximumDistanceStorage(particleCount),tetherLengthStorage(tetherCount);auto* driveAlpha=driveAlphaStorage.data();auto* driveDecay=driveDecayStorage.data();auto* maximumDistance=maximumDistanceStorage.data();auto* tetherLength=tetherLengthStorage.data();
    for(std::size_t i=0;i<particleCount;++i){const auto& p=particles[i];driveAlpha[i]=p.animationDrive>0?settings.animationCompliance*std::max(.0001,1-p.animationDrive)/p.animationDrive/(h*h):0;driveDecay[i]=std::exp(-settings.animationDamping*p.animationDrive*h);maximumDistance[i]=p.maxDistance/s.units;}
    for(std::size_t k=0;k<tetherCount;++k)tetherLength[k]=tethers[k].restLength/s.units;
    auto* executor=s.coloredTopology?s.executor.get():nullptr;
    struct ParallelSession {
        detail::ClothParallelExecutor* executor;
        explicit ParallelSession(detail::ClothParallelExecutor* value):executor(value){if(executor)executor->BeginSession();}
        ~ParallelSession(){if(executor)executor->EndSession();}
    } parallelSession(executor);
    auto runColored=[&](const Impl::ConstraintColors& colors,std::size_t count,const auto& solve,bool parallel=true){
        if(!executor){for(std::size_t k=0;k<count;++k)solve(k);return;}
        if(parallel){executor->ForColored(colors.offsets,colors.order,32,solve);return;}
        const auto* offsets=colors.offsets.data();const auto* order=colors.order.data();
        // A fixed greedy color order and stable order within each color make
        // results independent of worker count. A color never shares particles.
        for(std::size_t c=0;c+1<colors.offsets.size();++c){const auto begin=offsets[c],length=offsets[c+1]-begin;
            for(std::uint32_t k=0;k<length;++k)solve(order[begin+k]);}
    };
    auto runParticles=[&](const auto& solve){if(executor)executor->For(particleCount,128,solve);else for(std::size_t k=0;k<particleCount;++k)solve(k);};
    auto runColoredRanges=[&](const Impl::ConstraintColors& colors,const auto& solve,bool parallel){
        if(parallel&&executor){executor->ForColoredRanges(colors.offsets,colors.order,32,solve);return;}
        const auto* offsets=colors.offsets.data();const auto* order=colors.order.data();
        for(std::size_t color=0;color+1<colors.offsets.size();++color)if(offsets[color+1]>offsets[color])
            solve(std::span<const std::uint32_t>(order+offsets[color],offsets[color+1]-offsets[color]));
    };
    if(profiling)prepareMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    auto fraction=[&](double time){return std::clamp(time/(warmTicks?warmDuration:deltaSeconds),0.0,1.0);};
    auto distanceConstraints=[&](const auto* constraints,std::size_t count,double* lambdas,double compliance,const Impl::ConstraintColors& colors,const DistancePrepared* prepared){
        ProfileScope timing(distanceMilliseconds,profiling);
        const double alpha=compliance/(h*h);
        if(s.coloredTopology&&detail::ClothSimdAvailable()){
            runColoredRanges(colors,[&](std::span<const std::uint32_t> indices){detail::SolveClothDistances(constraints,prepared,indices.data(),indices.size(),positions,inverseMass,lambdas,alpha);},false);return;
        }
        runColored(colors,count,[&](std::size_t k){const auto& c=constraints[k];const auto& cached=prepared[k];if(cached.massSum<=0)return;
            const auto d=positions[c.a]-positions[c.b];const double length=Length(d);if(length<=Epsilon)return;
            const double constraint=length-cached.rest;if(constraint==0&&lambdas[k]==0)return;
            const double dl=(-constraint-alpha*lambdas[k])*cached.inverseDenominator;lambdas[k]+=dl;
            const auto correction=d*(dl/length);positions[c.a]+=correction*inverseMass[c.a];positions[c.b]-=correction*inverseMass[c.b];},false);
    };
    auto areaConstraints=[&](){
        ProfileScope timing(areaMilliseconds,profiling);
        const double alpha=settings.areaCompliance/(h*h);
        if(s.coloredTopology&&detail::ClothSimdAvailable()){
            runColoredRanges(s.triangleColors,[&](std::span<const std::uint32_t> indices){detail::SolveClothAreas(triangles,restAreas,restAreaScales,indices.data(),indices.size(),positions,inverseMass,areaLambda,alpha);},false);return;
        }
        runColored(s.triangleColors,triangleCount,[&](std::size_t k){const auto& t=triangles[k];const double rest=restAreas[k];if(rest<=Epsilon)return;
            const auto a=positions[t[0]],b=positions[t[1]],c=positions[t[2]],cross=glm::cross(b-a,c-a);const double twiceArea=Length(cross);if(twiceArea<=Epsilon)return;
            if(twiceArea*.5==rest&&areaLambda[k]==0)return;
            // Normalize by a rest length, retaining a distance-like constraint
            // with compliance independent of triangle refinement and SI scale.
            const double scale=restAreaScales[k];const auto normal=cross/twiceArea;
            const glm::dvec3 gradients[3]={glm::cross(b-c,normal)*(.5*scale),glm::cross(c-a,normal)*(.5*scale),glm::cross(a-b,normal)*(.5*scale)};
            double denominator=alpha;for(int j=0;j<3;++j)denominator+=inverseMass[t[j]]*glm::dot(gradients[j],gradients[j]);if(denominator<=Epsilon)return;
            const double dl=(-((twiceArea*.5-rest)*scale)-alpha*areaLambda[k])/denominator;areaLambda[k]+=dl;
            for(int j=0;j<3;++j)positions[t[j]]+=gradients[j]*(inverseMass[t[j]]*dl);
        },false);
    };
    auto preventAreaInversion=[&](bool converge){
        ProfileScope timing(barrierMilliseconds,profiling);
        bool repaired=false;
        auto repair=[&](std::size_t k,bool jacobi){const auto& t=triangles[k];const double rest=restAreas[k];if(rest<=Epsilon)return;
            const auto a=positions[t[0]],b=positions[t[1]],c=positions[t[2]],normal=triangleNormals[k];const double signedArea=glm::dot(glm::cross(b-a,c-a),normal)*.5,minimum=rest*.12;if(signedArea>=minimum*(1-1e-5))return;
            // Unlike unsigned area recovery, this barrier has useful gradients
            // even for collinear vertices. Use the preceding substep normal so
            // fabric may freely rotate while contact cannot flip it through a
            // zero-area needle within one integration interval.
            const double scale=restAreaScales[k];const glm::dvec3 gradients[3]={glm::cross(b-c,normal)*(.5*scale),glm::cross(c-a,normal)*(.5*scale),glm::cross(a-b,normal)*(.5*scale)};
            double denominator=0;for(int v=0;v<3;++v)denominator+=inverseMass[t[v]]*glm::dot(gradients[v],gradients[v]);if(denominator<=Epsilon)return;
            repaired=true;
            if(profiling&&!converge){++profileBarrierRepairs;++iterationBarrierRepairs;}
            const double dl=(minimum-signedArea)*scale/denominator;for(int v=0;v<3;++v)if(inverseMass[t[v]]>0){const auto particle=t[v];const auto correction=gradients[v]*(inverseMass[particle]*dl);
                if(profiling&&!converge){const double move2=glm::dot(correction,correction);iterationBarrierDelta2=std::max(iterationBarrierDelta2,move2);profileMaximumBarrierDelta2=std::max(profileMaximumBarrierDelta2,move2);}
                if(jacobi){if(areaWeights[particle]==0){areaTouched.push_back(particle);areaCorrections[particle]=glm::dvec3(0);}areaCorrections[particle]+=correction;areaWeights[particle]+=1;}
                else positions[particle]+=correction;}
        };
        if(!converge){for(std::size_t k=0;k<triangleCount;++k)repair(k,false);return repaired;}
        // Only the substep boundary needs converged simultaneous repair. Begin
        // with all faces; later rounds visit just faces incident to particles
        // changed by the preceding round. Unchanged faces cannot become worse.
        areaActive.resize(triangleCount);for(std::uint32_t k=0;k<triangleCount;++k)areaActive.data()[k]=k;
        for(int pass=0;pass<24&&!areaActive.empty();++pass){areaTouched.clear();const auto* active=areaActive.data();for(std::size_t k=0;k<areaActive.size();++k)repair(active[k],true);if(areaTouched.empty())break;
            const auto* touched=areaTouched.data();for(std::size_t k=0;k<areaTouched.size();++k){const auto particle=touched[k];positions[particle]+=areaCorrections[particle]*(.8/areaWeights[particle]);}
            areaNext.clear();std::fill_n(areaMarked,triangleCount,std::uint8_t(0));
            for(std::size_t k=0;k<areaTouched.size();++k){const auto particle=touched[k];areaWeights[particle]=0;const auto& incident=s.incidentTriangles.data()[particle];const auto* adjacent=incident.data();for(std::size_t j=0;j<incident.size();++j){const auto triangle=adjacent[j];if(!areaMarked[triangle]){areaMarked[triangle]=true;areaNext.push_back(triangle);}}}
            areaActive.swap(areaNext);
        }
        return repaired;
    };
    auto bendingConstraints=[&](){
        ProfileScope timing(bendMilliseconds,profiling);
        if(hingeCount==0){distanceConstraints(cloth.bend.data(),cloth.bend.size(),bendLambda,settings.bendCompliance,s.bendColors,bendPrepared.data());return;}
        const double alpha=settings.bendCompliance/(h*h);
        if(s.coloredTopology&&detail::ClothSimdAvailable()){
            // One graph color has disjoint particles. Four independent hinges
            // share a double-precision packet, retaining the scalar color order.
            runColoredRanges(s.hingeColors,[&](std::span<const std::uint32_t> indices){detail::SolveClothHinges(hinges,indices.data(),indices.size(),positions,inverseMass,bendLambda,alpha);},true);
            return;
        }
        runColored(s.hingeColors,hingeCount,[&](std::size_t k){const auto& hinge=hinges[k];const auto& t=hinge.vertices;const auto a=positions[t[0]],b=positions[t[1]],c=positions[t[2]],d=positions[t[3]],edge=b-a;
            const double edge2=glm::dot(edge,edge);const auto first=glm::cross(edge,c-a),second=glm::cross(d-a,edge);const double area1Squared=glm::dot(first,first),area2Squared=glm::dot(second,second);
            if(edge2<=Epsilon*Epsilon||area1Squared<=Epsilon*Epsilon||area2Squared<=Epsilon*Epsilon)return;
            const double edgeLength=std::sqrt(edge2);
            const double angle=detail::FastAtan2(-edgeLength*glm::dot(first,d-a),glm::dot(first,second));const double constraint=AngleDifference(angle,hinge.angle);if(constraint==0&&bendLambda[k]==0)return;
            glm::dvec3 gradients[4];
            gradients[2]=first*(-edgeLength/area1Squared);gradients[3]=second*(-edgeLength/area2Squared);
            const double u=glm::dot(c-a,edge)/edge2,v=glm::dot(d-a,edge)/edge2;
            gradients[0]=-(1-u)*gradients[2]-(1-v)*gradients[3];gradients[1]=-u*gradients[2]-v*gradients[3];
            double denominator=alpha;for(int j=0;j<4;++j)denominator+=inverseMass[t[j]]*glm::dot(gradients[j],gradients[j]);if(denominator<=Epsilon)return;
            const double dl=(-constraint-alpha*bendLambda[k])/denominator;bendLambda[k]+=dl;
            for(int j=0;j<4;++j)positions[t[j]]+=gradients[j]*(inverseMass[t[j]]*dl);
        });
    };
    auto dampLocalDeformation=[&](){
        if(settings.localDamping<=0)return;double mass=0;glm::dvec3 center(0),linear(0);
        for(std::size_t i=0;i<particleCount;++i)if(inverseMass[i]>0){const double m=1/inverseMass[i];mass+=m;center+=positions[i]*m;linear+=velocity[i]*m;}
        if(mass<=Epsilon)return;center/=mass;linear/=mass;glm::dmat3 inertia(0);glm::dvec3 momentum(0);
        for(std::size_t i=0;i<particleCount;++i)if(inverseMass[i]>0){const double m=1/inverseMass[i];const auto r=positions[i]-center;inertia+=(glm::dmat3(glm::dot(r,r))-glm::outerProduct(r,r))*m;momentum+=glm::cross(r,velocity[i]-linear)*m;}
        const auto angular=std::abs(glm::determinant(inertia))>1e-20?glm::inverse(inertia)*momentum:glm::dvec3(0);const double decay=std::exp(-settings.localDamping*h);
        for(std::size_t i=0;i<particleCount;++i)if(inverseMass[i]>0){const auto coherent=linear+glm::cross(angular,positions[i]-center);velocity[i]=coherent+(velocity[i]-coherent)*decay;}
    };
    auto selfCollision=[&](){
        const double diameter=settings.thickness*2;std::unordered_map<Cell,std::vector<std::uint32_t>,CellHash> grid;grid.reserve(particleCount*2);
        for(std::uint32_t i=0;i<particleCount;++i)grid[CellAt(positions[i],diameter)].push_back(i);
        for(std::uint32_t i=0;i<particleCount;++i){const auto cell=CellAt(positions[i],diameter);
            for(int z=-1;z<=1;++z)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){const auto found=grid.find({cell.x+x,cell.y+y,cell.z+z});if(found==grid.end())continue;
                for(auto j:found->second){if(j<=i||s.adjacent.contains(Pair(i,j)))continue;const double sum=inverseMass[i]+inverseMass[j];if(sum<=0)continue;
                    const auto d=positions[i]-positions[j];const double length=Length(d);if(length>=diameter)continue;
                    const auto direction=Unit(d,Unit(subTargets[i]-subTargets[j],{1,0,0}));const auto correction=direction*((diameter-length)/sum);
                    positions[i]+=correction*inverseMass[i];positions[j]-=correction*inverseMass[j];selfContacts.insert(Pair(i,j));}
            }
        }
    };
    auto collide=[&](bool sweep){
        ProfileScope timing(particleMilliseconds,profiling);
        runParticles([&](std::size_t i){if(inverseMass[i]<=0)return;for(std::size_t c=0;c<capsuleCount;++c){
            const auto& cap=subCapsules[c];const double radius=cap.radius+settings.thickness;
            const auto point=positions[i];const auto& minimum=capsuleMinimum[c];const auto& maximum=capsuleMaximum[c];
            const bool endOverlap=point.x>=minimum.x&&point.x<=maximum.x&&point.y>=minimum.y&&point.y<=maximum.y&&point.z>=minimum.z&&point.z<=maximum.z;
            if(!endOverlap){
                if(!sweep)continue;
                // A point outside both the endpoint bound and the entire
                // swept capsule/particle bound cannot produce a CCD contact.
                // Re-evaluate after each preceding capsule correction so the
                // Gauss-Seidel ordering remains identical to the narrow phase.
                const auto from=glm::min(before[i],point),to=glm::max(before[i],point);
                const auto& sweepMin=sweptCapsuleMinimum[c];const auto& sweepMax=sweptCapsuleMaximum[c];
                if(from.x>sweepMax.x||to.x<sweepMin.x||from.y>sweepMax.y||to.y<sweepMin.y||from.z>sweepMax.z||to.z<sweepMin.z)continue;
            }
            const auto center=Closest(positions[i],cap);const auto difference=positions[i]-center;const double distance=Length(difference);
            const auto& start=startCapsules[c];
            // Conservative temporal sampling followed by bisection catches a
            // moving thigh crossing a particle. Entry must be considered even
            // when the final point is inside: endpoint projection alone may
            // choose the opposite surface after a collider crossed its center.
            double previous=0;bool hit=false;double contactTime=0,axisParameter=0;glm::dvec3 normal;
            int samples=0;
            if(sweep){const double relativeTravel=Length(positions[i]-before[i])+capsuleTravel[c];
                if(relativeTravel>=radius*.5&&Length(before[i]-Closest(before[i],start))>=start.radius+settings.thickness-1e-7)samples=int(std::clamp(std::ceil(relativeTravel/std::max(radius*.35,1e-5)),2.0,64.0));}
            for(int k=0;samples&&k<=samples;++k){const double t=double(k)/samples;const auto moving=Mix(start,cap,t);const auto point=glm::mix(before[i],positions[i],t);
                if(Length(point-Closest(point,moving))<moving.radius+settings.thickness){double lo=previous,hi=t;
                    for(int bisect=0;bisect<8;++bisect){const double mid=(lo+hi)*.5;const auto middle=Mix(start,cap,mid);const auto p=glm::mix(before[i],positions[i],mid);if(Length(p-Closest(p,middle))<middle.radius+settings.thickness)hi=mid;else lo=mid;}
                    contactTime=hi;const auto atHit=Mix(start,cap,hi);const auto atPoint=glm::mix(before[i],positions[i],hi);const auto closest=Closest(atPoint,atHit,&axisParameter);
                    normal=Unit(atPoint-closest,Unit(before[i]-Closest(before[i],start)));hit=true;break;}
                previous=t;
            }
            if(hit){
                const auto centerEnd=glm::mix(cap.a,cap.b,axisParameter);const auto contactPoint=glm::mix(before[i],positions[i],contactTime);
                const auto endResidual=positions[i]-contactPoint;const auto tangential=endResidual-normal*glm::dot(endResidual,normal);
                // Keep the original side of the swept collider. Retaining the
                // tangential motion avoids glueing cloth to a moving leg.
                positions[i]=centerEnd+normal*radius+tangential*(1-settings.friction);contacted[i]=true;
                const auto endCenter=Closest(positions[i],cap);const auto endDirection=positions[i]-endCenter;const double endDistance=Length(endDirection);
                if(endDistance<radius)positions[i]=endCenter+Unit(endDirection,normal)*radius;
            }
            else if(distance<radius){positions[i]=center+Unit(difference,Unit(before[i]-Closest(before[i],start)))*radius;contacted[i]=true;}
        }});
    };
    auto collideSurface=[&](bool sweep,bool rebuild){
        ProfileScope timing(surfaceMilliseconds,profiling);
        runColored(s.triangleColors,triangleCount,[&](std::size_t triangleIndex){const auto& tri=triangles[triangleIndex];if(inverseMass[tri[0]]+inverseMass[tri[1]]+inverseMass[tri[2]]<=0)return;
            auto a=positions[tri[0]],b=positions[tri[1]],d=positions[tri[2]];auto minimum=glm::min(a,glm::min(b,d)),maximum=glm::max(a,glm::max(b,d));
            auto* manifold=surfaceCacheEnabled?surfaceTriangles+triangleIndex:nullptr;
            const double cacheMargin=surfaceCacheEnabled?std::max(1e-8,std::min(settings.thickness*.5,triangleSizes[triangleIndex]*.1)):0;
            auto observeMotion=[&](){if(!manifold)return;
                if(manifold->substep!=solvedSubsteps){manifold->motion=0;manifold->substep=solvedSubsteps;}
                else {double largest2=0;for(int v=0;v<3;++v){const auto delta=positions[tri[v]]-manifold->observed[v];largest2=std::max(largest2,glm::dot(delta,delta));}manifold->motion+=std::sqrt(largest2);}
                for(int v=0;v<3;++v)manifold->observed[v]=positions[tri[v]];};
            observeMotion();
            glm::dvec3 beforeMinimum(0),beforeMaximum(0),planeNormal(0);double triangleTravel=0,planeLength2=0;bool planeReady=false;
            auto refreshTriangle=[&](){a=positions[tri[0]];b=positions[tri[1]];d=positions[tri[2]];minimum=glm::min(a,glm::min(b,d));maximum=glm::max(a,glm::max(b,d));
                observeMotion();
                planeReady=false;
                if(sweep){triangleTravel=0;for(auto i:tri)triangleTravel=std::max(triangleTravel,Length(positions[i]-before[i]));}};
            if(sweep){beforeMinimum=glm::min(before[tri[0]],glm::min(before[tri[1]],before[tri[2]]));beforeMaximum=glm::max(before[tri[0]],glm::max(before[tri[1]],before[tri[2]]));for(auto i:tri)triangleTravel=std::max(triangleTravel,Length(positions[i]-before[i]));}
            for(std::size_t c=0;c<capsuleCount;++c){const auto& cap=subCapsules[c];const auto& start=startCapsules[c];const double radius=cap.radius+settings.thickness;
                // Retain near contacts as well as penetrating contacts, so a
                // newly approaching feature is available between rebuilds.
                const double queryRadius=radius+cacheMargin;
                const auto capMin=capsuleMinimum[c]-glm::dvec3(cacheMargin),capMax=capsuleMaximum[c]+glm::dvec3(cacheMargin);const bool endOverlap=minimum.x<=capMax.x&&maximum.x>=capMin.x&&minimum.y<=capMax.y&&maximum.y>=capMin.y&&minimum.z<=capMax.z&&maximum.z>=capMin.z;
                const double travel=sweep?triangleTravel+capsuleTravel[c]:0;
                const bool checkSweep=sweep&&travel>radius*.5;
                if(!endOverlap&&!checkSweep)continue;
                if(!endOverlap){const auto sweptMinimum=glm::min(minimum,beforeMinimum),sweptMaximum=glm::max(maximum,beforeMaximum);const auto& sweepMin=sweptCapsuleMinimum[c];const auto& sweepMax=sweptCapsuleMaximum[c];
                    if(sweptMinimum.x>sweepMax.x||sweptMaximum.x<sweepMin.x||sweptMinimum.y>sweepMax.y||sweptMaximum.y<sweepMin.y||sweptMinimum.z>sweepMax.z||sweptMaximum.z<sweepMin.z)continue;}
                if(!checkSweep){
                    // Distance between the triangle AABB and the capsule axis
                    // AABB is a lower bound on triangle-to-axis distance. The
                    // rounded-box test rejects the corners admitted by the
                    // expanded AABB, without skipping any surface or CCD hit.
                    const auto gap=glm::max(glm::max(minimum-capsuleAxisMaximum[c],capsuleAxisMinimum[c]-maximum),glm::dvec3(0));
                    if(glm::dot(gap,gap)>queryRadius*queryRadius*(1+1e-12)+Epsilon*Epsilon)continue;
                    if(!planeReady){planeNormal=glm::cross(b-a,d-a);planeLength2=glm::dot(planeNormal,planeNormal);planeReady=true;}
                    const double firstDistance=glm::dot(planeNormal,cap.a-a),secondDistance=glm::dot(planeNormal,cap.b-a);
                    if(firstDistance*secondDistance>0){const double nearest=std::min(std::abs(firstDistance),std::abs(secondDistance));
                        // The infinite supporting plane already separates the
                        // whole capsule. This conservative test rejects many
                        // loose AABB candidates without any closest-point or
                        // square-root work. Never apply it to a CCD query.
                        if(nearest*nearest>queryRadius*queryRadius*planeLength2*(1+1e-12))continue;}
                }
                auto* cached=surfaceCacheEnabled?surfacePairs+triangleIndex*capsuleCount+c:nullptr;
                TriangleCapsuleClosest closest;bool reuse=false;
                if(cached&&!rebuild&&cached->substep==solvedSubsteps){
                    const double motion=std::max(0.0,manifold->motion-cached->motion);
                    // Distance to a fixed segment is 1-Lipschitz under the
                    // largest vertex displacement. Summed observed motion is
                    // a conservative upper bound, including neighboring-face
                    // corrections and earlier capsules in this same visit.
                    // Include the axis/plane candidate's 1e-8 geometric
                    // acceptance tolerance as well as floating-point roundoff.
                    if(cached->distance-motion>radius+1e-8+1e-12*std::max(1.0,radius)){if(profiling)++manifold->separated;continue;}
                    if(motion<=cacheMargin){closest.bary=cached->bary;closest.cloth=a*closest.bary.x+b*closest.bary.y+d*closest.bary.z;closest.axis=Closest(closest.cloth,cap);closest.distance=Length(closest.cloth-closest.axis);
                        // A cached material point is only an upper bound on
                        // face distance. If it is outside while another feature
                        // may be inside, refresh immediately (edge transitions).
                        reuse=closest.distance<radius;if(reuse&&profiling)++manifold->reused;}
                    if(!reuse&&profiling)++manifold->invalidated;
                }
                if(!reuse){closest=ClosestTriangleCapsule(a,b,d,cap);if(manifold&&profiling)++manifold->exact;}
                glm::dvec3 normal(0);double correction=radius-closest.distance;
                if(checkSweep){const auto atStart=ClosestTriangleCapsule(before[tri[0]],before[tri[1]],before[tri[2]],start);const int samples=atStart.distance>=start.radius+settings.thickness-1e-7?int(std::clamp(std::ceil(travel/std::max(radius*.45,1e-5)),2.0,24.0)):0;
                    for(int sample=1;sample<=samples;++sample){const double time=double(sample)/samples;const auto moving=Mix(start,cap,time);const auto hit=ClosestTriangleCapsule(glm::mix(before[tri[0]],a,time),glm::mix(before[tri[1]],b,time),glm::mix(before[tri[2]],d,time),moving);
                        if(hit.distance>=moving.radius+settings.thickness)continue;
                        closest.bary=hit.bary;const auto oldPoint=before[tri[0]]*hit.bary.x+before[tri[1]]*hit.bary.y+before[tri[2]]*hit.bary.z;
                        normal=Unit(hit.cloth-hit.axis,Unit(oldPoint-Closest(oldPoint,start)));
                        const auto point=a*hit.bary.x+b*hit.bary.y+d*hit.bary.z;const auto axis=Closest(point,cap);correction=radius-glm::dot(point-axis,normal);break;
                    }
                }
                // Only a complete query supplies a distance lower-bound seed.
                // Never overwrite it with the cached point's upper bound.
                if(cached&&!reuse){cached->bary=closest.bary;cached->distance=closest.distance;cached->motion=manifold->motion;cached->substep=solvedSubsteps;}
                if(correction<=1e-9)continue;
                if(Length(normal)<.5){const auto oldPoint=before[tri[0]]*closest.bary.x+before[tri[1]]*closest.bary.y+before[tri[2]]*closest.bary.z;
                    normal=Unit(closest.cloth-closest.axis,Unit(oldPoint-Closest(oldPoint,start),Unit(glm::cross(b-a,d-a))));}
                double denominator=0;for(int v=0;v<3;++v)denominator+=inverseMass[tri[v]]*closest.bary[v]*closest.bary[v];if(denominator<=Epsilon)continue;
                double largestMove=0;for(int v=0;v<3;++v)largestMove=std::max(largestMove,correction*inverseMass[tri[v]]*closest.bary[v]/denominator);
                // A barycentric point extremely close to a fixed corner has
                // an ill-conditioned effective mass. Moving a free corner by
                // penetration/bary can create a long needle and inject huge
                // velocity. Bound each local projection by its rest patch size;
                // subsequent stretch/contact iterations finish the correction.
                const double maximumMove=std::max(.001,triangleSizes[triangleIndex]*.5);if(largestMove>maximumMove)correction*=maximumMove/largestMove;
                // Contact is a constraint on a barycentric point of the whole
                // fabric surface. A leg can no longer pass through a face just
                // because its three vertices happen to lie outside a capsule.
                for(int v=0;v<3;++v)if(inverseMass[tri[v]]>0){positions[tri[v]]+=normal*(correction*inverseMass[tri[v]]*closest.bary[v]/denominator);contacted[tri[v]]=true;}
                // A preceding capsule can move the face into a later capsule.
                // Refresh only after an actual correction, preserving ordering.
                refreshTriangle();
            }
        });
    };
    auto convexSampleCount=[&](const ConvexSubstep& data,double clothTravel){const double travel=data.motion.travel+clothTravel;
        return travel>data.interiorRadius*.4?std::uint32_t(std::clamp(std::ceil(travel/std::max({data.interiorRadius*.35,settings.thickness*.5,1e-5})),2.0,64.0)):0;};
    auto prepareConvexSweep=[&](){
        // Nested inside the caller's particle/surface stage, not an additional
        // disjoint part of total frame time. Count actual refit samples below.
        ProfileScope timing(convexSweepPrepareMilliseconds,profiling);if(profiling)++profileSweepCalls;
        double clothTravel=0;for(std::size_t i=0;i<particleCount;++i)clothTravel=std::max(clothTravel,Length(positions[i]-before[i]));
        if(convexSweepReady&&clothTravel<=convexPreparedClothTravel+1e-12)return true;
        if(profiling)++profileSweepRebuilds;
        convexSweepReady=true;convexPreparedClothTravel=clothTravel;
        for(auto& data:subConvexes){
            const auto required=convexSampleCount(data,clothTravel);if(required<=data.sampleCount)continue;data.sampleCount=required;
            if(profiling){++profileSweepBodyGrids;profileSweepSamples+=data.sampleCount+1;}
            // Shared samples make CCD cost proportional to colliders, rather
            // than repeatedly transforming every skin plane for every particle
            // and triangle. Stationary shapes need only their one endpoint.
            if(data.sampleCount&&data.motion.changed){data.samples.resize(data.sampleCount+1);
                data.samples.front()=data.start;data.samples.back()=data.end;
                for(std::uint32_t k=1;k<data.sampleCount;++k){if(!data.motion.At(double(k)/data.sampleCount,data.samples[k]))return false;
                    if(profiling)++profileSweepMidShapes;}
                // Resize/At can relocate a shape or overwrite its planes at
                // the same address. Always rebuild all packets after the full
                // grid is complete, then publish it immutable to graph colors.
                data.sampleQueries.resize(data.sampleCount+1);
                for(std::uint32_t k=0;k<=data.sampleCount;++k)data.sampleQueries[k].Prepare(data.samples[k]);}
            // Build only after every actual shared-grid shape is immutable.
            // This is a second proof for the existing discrete queries; it
            // does not replace the continuous motion bound or alter sampling.
            if(data.sampleCount)data.sampleBounds.Prepare(data.start,data.end,
                data.motion.changed?std::span<const ClothWorldConvex>(data.samples):std::span<const ClothWorldConvex>{});
            // Re-evaluate the authored depth at the actual sampled body pose:
            // a nearest external plane can change between interval endpoints.
            if(data.sampleCount&&!pinIndices.empty()){data.samplePinOffsets.resize(data.sampleCount+1);
                data.samplePinOffsets.front()=data.startPinOffsets;data.samplePinOffsets.back()=data.endPinOffsets;
                for(std::uint32_t k=1;k<data.sampleCount;++k){const double time=double(k)/data.sampleCount;
                    fillPinOffsets(data.Sample(k),[&](std::size_t pin){return glm::mix(before[pin],subTargets[pin],time);},data.samplePinOffsets[k],&data.SampleQueries(k));}}
        }
        return true;
    };
    auto convexPointEndQuery=[&](std::size_t particle,std::size_t body){
        const auto& shape=subConvexes[body].end;const auto point=positions[particle];
        // Most point/body pairs are far apart. Reject here before the cross-TU
        // large contact return, by-value arguments and SIMD dispatch. Keep the
        // scalar helper's exact no-hit fields; any old proof is rechecked when
        // the pair becomes relevant again.
        if(!detail::ConvexBoundsOverlap(point,point,shape,settings.thickness)){
            if(profiling)++profilePointAabb[particle];
            detail::ClothConvexContact separated;separated.point=point;return separated;
        }
        auto* cached=separatingCacheEnabled?particleSeparating+particle*convexCount+body:nullptr;
        if(cached&&*cached<shape.planes.size()){
            const auto& plane=shape.planes[*cached];
            // This is a fresh geometric proof on the CURRENT point. A previous
            // body's projection can invalidate it, in which case query fully.
            if(glm::dot(plane.normal,point)-plane.distance>settings.thickness){
                if(profiling)++particleSeparatingHits[particle];
                detail::ClothConvexContact separated;separated.separatingPlane=*cached;return separated;
            }
        }
        if(profiling)++profilePointExact[particle];
        const auto contact=detail::ConvexPointContact(shape,point,settings.thickness,&subConvexes[body].endQueries);
        if(cached)*cached=contact.separatingPlane<noSeparatingPlane?std::uint16_t(contact.separatingPlane):noSeparatingPlane;
        return contact;
    };
    auto convexTriangleEndQuery=[&](std::size_t triangle,std::size_t body,glm::dvec3 a,glm::dvec3 b,glm::dvec3 c){
        const auto& shape=subConvexes[body].end;
        auto* cached=separatingCacheEnabled?triangleSeparating+triangle*convexCount+body:nullptr;
        if(cached&&*cached<shape.planes.size()){
            const auto& plane=shape.planes[*cached];
            // Match the full query's exact expression/order, including padding.
            // Recheck all three current vertices; cached barys or vertex bounds
            // cannot prove that a face remains separated after another solve.
            if(glm::dot(plane.normal,a)-plane.distance-settings.thickness>0&&
               glm::dot(plane.normal,b)-plane.distance-settings.thickness>0&&
               glm::dot(plane.normal,c)-plane.distance-settings.thickness>0){
                if(profiling)++triangleSeparatingHits[triangle];
                detail::ClothConvexContact separated;separated.separatingPlane=*cached;return separated;
            }
        }
        const auto compositePair=triangle*convexCount+body;
        auto* state=compositeCacheEnabled?&compositeState[compositePair]:nullptr;
        if(state&&*state==2&&detail::ConvexCompositeSeparates(shape,compositeStorage[compositePair],a,b,c,settings.thickness)){
            if(profiling)++profileCompositeReuse[triangle];return detail::ClothConvexContact{};
        }
        const auto offsets=triangleOffsets(triangles[triangle],subConvexes[body].endPinOffsets);
        const bool tryComposite=state&&*state==0;std::uint16_t emptyPlane=noSeparatingPlane;
        bool emptyClip=false;if(profiling)++profileTriangleExact[triangle];
        const auto contact=detail::ConvexTriangleContactOffsets(shape,a,b,c,offsets,settings.thickness,&subConvexes[body].endQueries,
            profiling||tryComposite?&emptyClip:nullptr,tryComposite?&emptyPlane:nullptr);
        if(profiling&&emptyClip)++profileTriangleComposite[triangle];
        if(tryComposite&&emptyClip&&emptyPlane!=noSeparatingPlane){*state=1;if(profiling)++profileCompositeAttempts[triangle];
            if(detail::BuildConvexCompositeSeparator(shape,emptyPlane,a,b,c,settings.thickness,compositeStorage[compositePair])){
                *state=2;if(profiling)++profileCompositeFound[triangle];}}
        if(cached)*cached=contact.separatingPlane<noSeparatingPlane?std::uint16_t(contact.separatingPlane):noSeparatingPlane;
        return contact;
    };
    auto collideConvexParticles=[&](bool sweep){
        if(!convexCount)return true;
        ProfileScope timing(particleMilliseconds,profiling);
        if(sweep&&!prepareConvexSweep())return false;
        runParticles([&](std::size_t i){if(inverseMass[i]<=0)return;
            for(std::size_t body=0;body<convexCount;++body){const auto& data=subConvexes[body];
                auto contact=convexPointEndQuery(i,body);
                bool swept=false;
                if(sweep&&data.sampleCount){
                    const auto from=glm::min(before[i],positions[i]),to=glm::max(before[i],positions[i]);
                    const auto minimum=data.motion.minimum-glm::dvec3(settings.thickness),maximum=data.motion.maximum+glm::dvec3(settings.thickness);
                    bool overlap=from.x<=maximum.x&&to.x>=minimum.x&&from.y<=maximum.y&&to.y>=minimum.y&&from.z<=maximum.z&&to.z>=minimum.z;
                    if(overlap&&sampleBoundsGate)overlap=data.sampleBounds.Overlaps(from,to,settings.thickness);
                    if(overlap){if(profiling)++profilePointCcd[i];
                        detail::ClothConvexContact initial;initial.point=before[i];
                        if(detail::ConvexBoundsOverlap(before[i],before[i],data.start,settings.thickness))initial=detail::ConvexPointContact(data.start,before[i],settings.thickness,&data.startQueries);
                        if(!initial.hit||settings.thickness-initial.separation<1e-7){
                            for(std::uint32_t k=1;k<=data.sampleCount;++k){const double fraction=double(k)/data.sampleCount;
                                if(profiling)++profilePointCcd[i];
                                const auto& sample=data.Sample(k);const auto point=glm::mix(before[i],positions[i],fraction);
                                if(!detail::ConvexBoundsOverlap(point,point,sample,settings.thickness))continue;
                                const auto hit=detail::ConvexPointContact(sample,point,settings.thickness,&data.SampleQueries(k));
                                if(!hit.hit||settings.thickness-hit.separation<=1e-10)continue;
                                // The entry skin face follows the same bone to
                                // its endpoint, so a thigh cannot exit through
                                // the opposite side after crossing this point.
                                const auto& plane=data.end.planes[hit.plane];const double correction=settings.thickness-(glm::dot(plane.normal,positions[i])-plane.distance);
                                if(correction>0){positions[i]+=plane.normal*correction;contacted[i]=true;}
                                swept=true;break;
                            }
                        }
                    }
                }
                if(swept)contact=convexPointEndQuery(i,body);
                if(contact.hit&&contact.separation<settings.thickness){positions[i]+=contact.normal*(settings.thickness-contact.separation);contacted[i]=true;}
            }
        });
        return true;
    };
    auto collideConvexSurface=[&](bool sweep){
        if(!convexCount)return true;
        ProfileScope timing(surfaceMilliseconds,profiling);
        // Particle projection can move a face far beyond its integrated
        // trajectory. Rebuild shared samples before querying that new path.
        if(sweep&&!prepareConvexSweep())return false;
        std::atomic<bool> samplingFailed{false};
        runColored(s.triangleColors,triangleCount,[&](std::size_t triangleIndex){const auto& tri=triangles[triangleIndex];
            if(inverseMass[tri[0]]+inverseMass[tri[1]]+inverseMass[tri[2]]<=0)return;
            auto a=positions[tri[0]],b=positions[tri[1]],c=positions[tri[2]];
            auto minimum=glm::min(a,glm::min(b,c)),maximum=glm::max(a,glm::max(b,c));
            double triangleTravel=sweep?std::max({Length(a-before[tri[0]]),Length(b-before[tri[1]]),Length(c-before[tri[2]])}):0;
            for(std::size_t body=0;body<convexCount;++body){const auto& data=subConvexes[body];
                const bool endOverlap=detail::ConvexBoundsOverlap(minimum,maximum,data.end,settings.thickness);
                std::uint32_t sampleCount=data.sampleCount;
                if(sweep)sampleCount=std::max(sampleCount,convexSampleCount(data,triangleTravel));
                bool sweepOverlap=false;
                if(sweep&&sampleCount){const auto from=glm::min(minimum,glm::min(before[tri[0]],glm::min(before[tri[1]],before[tri[2]]))),
                        to=glm::max(maximum,glm::max(before[tri[0]],glm::max(before[tri[1]],before[tri[2]])));
                    sweepOverlap=from.x<=data.motion.maximum.x+settings.thickness&&to.x>=data.motion.minimum.x-settings.thickness&&
                        from.y<=data.motion.maximum.y+settings.thickness&&to.y>=data.motion.minimum.y-settings.thickness&&
                        from.z<=data.motion.maximum.z+settings.thickness&&to.z>=data.motion.minimum.z-settings.thickness;
                    // Non-nested adaptive grids can contain orientations absent
                    // from the shared grid. Keep their original motion bound.
                    if(sweepOverlap&&sampleBoundsGate&&sampleCount==data.sampleCount)sweepOverlap=data.sampleBounds.Overlaps(from,to,settings.thickness);}
                if(!endOverlap&&!sweepOverlap)continue;
                auto contact=endOverlap?convexTriangleEndQuery(triangleIndex,body,a,b,c):detail::ClothConvexContact{};
                const auto endOffsets=triangleOffsets(tri,data.endPinOffsets);
                double correction=contact.hit?detail::ConvexInterpolatedContactOffset(endOffsets,contact.bary)-contact.separation:0;
                if(sweepOverlap){const auto startOffsets=triangleOffsets(tri,data.startPinOffsets);
                    if(profiling)++profileTriangleCcd[triangleIndex];
                    detail::ClothConvexContact initial;
                    if(detail::ConvexBoundsOverlap(glm::min(before[tri[0]],glm::min(before[tri[1]],before[tri[2]])),
                        glm::max(before[tri[0]],glm::max(before[tri[1]],before[tri[2]])),data.start,settings.thickness))
                        initial=detail::ConvexTriangleContactOffsets(data.start,before[tri[0]],before[tri[1]],before[tri[2]],startOffsets,settings.thickness,&data.startQueries);
                    if(!initial.hit||detail::ConvexInterpolatedContactOffset(startOffsets,initial.bary)-initial.separation<1e-7){
                        ClothWorldConvex sampledShape;
                        for(std::uint32_t k=1;k<=sampleCount;++k){const double fraction=double(k)/sampleCount;
                            const ClothWorldConvex* shape=&data.end;
                            const detail::PreparedClothConvexQueries* queries=&data.endQueries;
                            if(data.motion.changed){
                                if(sampleCount==data.sampleCount){shape=&data.Sample(k);queries=&data.SampleQueries(k);}
                                else {
                                    // An earlier colored face or body can move
                                    // this triangle after shared preparation.
                                    // Adapt locally without mutating samples
                                    // concurrently used by other colors/tasks.
                                    if(!data.motion.At(fraction,sampledShape)){samplingFailed.store(true,std::memory_order_relaxed);return;}shape=&sampledShape;
                                    queries=nullptr; // overwritten local object: no stale shared packets
                                    if(profiling&&k<sampleCount)++profileTriangleLocalMidShapes[triangleIndex];
                                }
                            }
                            const auto sampleA=glm::mix(before[tri[0]],a,fraction),sampleB=glm::mix(before[tri[1]],b,fraction),sampleC=glm::mix(before[tri[2]],c,fraction);
                            if(profiling)++profileTriangleCcd[triangleIndex];
                            if(!detail::ConvexBoundsOverlap(glm::min(sampleA,glm::min(sampleB,sampleC)),glm::max(sampleA,glm::max(sampleB,sampleC)),*shape,settings.thickness))continue;
                            glm::dvec3 offsets(settings.thickness);
                            if(!pinIndices.empty()){
                                if(sampleCount==data.sampleCount)offsets=triangleOffsets(tri,data.samplePinOffsets[k]);
                                else for(int vertex=0;vertex<3;++vertex)if(pinOrdinal[tri[vertex]]!=noPin)
                                    offsets[vertex]=detail::ConvexKinematicContactOffset(*shape,glm::mix(before[tri[vertex]],subTargets[tri[vertex]],fraction),settings.thickness,queries);
                            }
                            auto hit=detail::ConvexTriangleContactOffsets(*shape,sampleA,sampleB,sampleC,offsets,settings.thickness,queries);
                            if(!hit.hit||detail::ConvexInterpolatedContactOffset(offsets,hit.bary)-hit.separation<=1e-10)continue;
                            const auto& plane=data.end.planes[hit.plane];
                            const auto point=a*hit.bary.x+b*hit.bary.y+c*hit.bary.z;
                            const double sweptCorrection=detail::ConvexInterpolatedContactOffset(endOffsets,hit.bary)-(glm::dot(plane.normal,point)-plane.distance);
                            // A swept material point can already be outside
                            // its transported entry plane at this endpoint,
                            // while a different endpoint point still collides.
                            // Preserve that valid end contact until a sweep
                            // supplies an actionable positive correction.
                            if(sweptCorrection<=1e-9)continue;
                            hit.normal=plane.normal;correction=sweptCorrection;contact=hit;break;
                        }
                    }
                }
                if(!contact.hit||correction<=1e-9)continue;
                double denominator=0;for(int v=0;v<3;++v)denominator+=inverseMass[tri[v]]*contact.bary[v]*contact.bary[v];
                if(denominator<=Epsilon)continue;
                double largestMove=0;for(int v=0;v<3;++v)largestMove=std::max(largestMove,correction*inverseMass[tri[v]]*contact.bary[v]/denominator);
                const double maximumMove=std::max(.001,triangleSizes[triangleIndex]*.5);if(largestMove>maximumMove)correction*=maximumMove/largestMove;
                for(int v=0;v<3;++v)if(inverseMass[tri[v]]>0){positions[tri[v]]+=contact.normal*(correction*inverseMass[tri[v]]*contact.bary[v]/denominator);contacted[tri[v]]=true;}
                a=positions[tri[0]];b=positions[tri[1]];c=positions[tri[2]];minimum=glm::min(a,glm::min(b,c));maximum=glm::max(a,glm::max(b,c));
                if(sweep)triangleTravel=std::max({Length(a-before[tri[0]]),Length(b-before[tri[1]]),Length(c-before[tri[2]])});
            }
        });
        return !samplingFailed.load(std::memory_order_relaxed);
    };
    while(s.accumulator+1e-12>=settings.fixedTimeStep){
        for(std::uint32_t sub=0;sub<settings.substeps;++sub){
            ++solvedSubsteps;
            const double startFraction=fraction(tickTime-settings.fixedTimeStep+sub*h),endFraction=fraction(tickTime-settings.fixedTimeStep+(sub+1)*h);
            for(std::size_t i=0;i<particleCount;++i)subTargets[i]=glm::mix(previousTargets[i],targetPositions[i],endFraction);
            for(std::size_t i=0;i<capsuleCount;++i){startCapsules[i]=Mix(previousCapsules[i],targetCapsules[i],startFraction);subCapsules[i]=Mix(previousCapsules[i],targetCapsules[i],endFraction);const auto& start=startCapsules[i];const auto& cap=subCapsules[i];const auto radius=glm::dvec3(cap.radius+settings.thickness);
                capsuleTravel[i]=std::max(Length(cap.a-start.a),Length(cap.b-start.b));capsuleAxisMinimum[i]=glm::min(cap.a,cap.b);capsuleAxisMaximum[i]=glm::max(cap.a,cap.b);capsuleMinimum[i]=capsuleAxisMinimum[i]-radius;capsuleMaximum[i]=capsuleAxisMaximum[i]+radius;
                const auto sweptRadius=glm::dvec3(std::max(cap.radius,start.radius)+settings.thickness);
                sweptCapsuleMinimum[i]=glm::min(glm::min(cap.a,cap.b),glm::min(start.a,start.b))-sweptRadius;sweptCapsuleMaximum[i]=glm::max(glm::max(cap.a,cap.b),glm::max(start.a,start.b))+sweptRadius;}
            convexSweepReady=false;convexPreparedClothTravel=0;
            if(separatingCacheEnabled){std::fill(particleSeparatingStorage.begin(),particleSeparatingStorage.end(),noSeparatingPlane);
                std::fill(triangleSeparatingStorage.begin(),triangleSeparatingStorage.end(),noSeparatingPlane);}
            if(compositeCacheEnabled)std::fill(compositeState.begin(),compositeState.end(),0);
            for(std::size_t i=0;i<convexCount;++i){auto& data=subConvexes[i];data.sampleCount=0;data.sampleBounds.ready=false;
                if(!convexMotions[i].At(startFraction,data.start)||!convexMotions[i].At(endFraction,data.end)||
                    !data.motion.Prepare(cloth.convexes[i],data.start,data.end,&convexMotions[i].layout))return fail("Cloth convex interpolation produced a singular transform");
                data.startQueries.Prepare(data.start);data.endQueries.Prepare(data.end);
                fillPinOffsets(data.start,[&](std::size_t pin){return positions[pin];},data.startPinOffsets,&data.startQueries);
                fillPinOffsets(data.end,[&](std::size_t pin){return subTargets[pin];},data.endPinOffsets,&data.endQueries);
                data.inverseEnd=glm::inverse(data.end.transform);data.interiorRadius=std::max(1e-5,std::min(detail::ConvexInteriorRadius(data.start),detail::ConvexInteriorRadius(data.end)));}
            std::copy_n(positions,particleCount,before);
            for(std::size_t k=0;k<triangleCount;++k){const auto& t=triangles[k];const auto normal=glm::cross(before[t[1]]-before[t[0]],before[t[2]]-before[t[0]]);if(Length(normal)>Epsilon)triangleNormals[k]=Unit(normal);}
            dampLocalDeformation();
            for(std::size_t i=0;i<particleCount;++i)acceleration[i]=settings.gravity+(settings.wind-velocity[i])*.35;
            // Two-sided pressure based on current triangle normals; the wind
            // setting is velocity rather than a frame-dependent position offset.
            for(std::size_t triangleIndex=0;triangleIndex<triangleCount;++triangleIndex){const auto& tri=triangles[triangleIndex];const auto cross=glm::cross(positions[tri[1]]-positions[tri[0]],positions[tri[2]]-positions[tri[0]]);const double twiceArea=Length(cross);if(twiceArea<=Epsilon)continue;
                const auto n=cross/twiceArea;const auto relative=settings.wind-(velocity[tri[0]]+velocity[tri[1]]+velocity[tri[2]])/3.0;const double speed=glm::dot(relative,n);
                const auto force=n*(.5*1.225*(twiceArea*.5)*speed*std::abs(speed)/3.0);
                for(auto v:tri)acceleration[v]+=force*inverseMass[v];}
            for(std::size_t i=0;i<particleCount;++i){if(inverseMass[i]==0){positions[i]=subTargets[i];velocity[i]={0,0,0};}
                else {velocity[i]*=globalDampingDecay;const double drive=particles[i].animationDrive;
                    if(drive>0&&settings.animationDamping>0){const auto referenceVelocity=(targetPositions[i]-previousTargets[i])/std::max(submissionDuration,settings.fixedTimeStep);velocity[i]=referenceVelocity+(velocity[i]-referenceVelocity)*driveDecay[i];}
                    velocity[i]+=acceleration[i]*h;positions[i]+=velocity[i]*h;}
                if(!Finite(positions[i])||Length(positions[i])>1e7){Reset(cloth,animatedGarment,nullptr);return fail("Cloth integration exceeded finite SI safety bounds and reset to the animated pose");}}
            std::fill_n(stretchLambda,stretchLambdaStorage.size(),0.0);std::fill_n(bendLambda,bendLambdaStorage.size(),0.0);std::fill_n(areaLambda,areaLambdaStorage.size(),0.0);std::fill_n(driveLambda,driveLambdaStorage.size(),0.0);std::fill_n(tetherLambda,tetherLambdaStorage.size(),0.0);
            std::uint32_t settledIterations=0;
            for(std::uint32_t iteration=0;iteration<settings.iterations;++iteration){
                if(convergenceTolerance>0||profiling)std::copy_n(positions,particleCount,iterationStart);
                if(profiling){iterationBarrierDelta2=0;iterationBarrierRepairs=0;}
                ++s.stats.constraintIterations;
                distanceConstraints(cloth.stretch.data(),cloth.stretch.size(),stretchLambda,settings.stretchCompliance,s.stretchColors,stretchPrepared.data());areaConstraints();bool barrierRepaired=preventAreaInversion(false);bendingConstraints();
                for(std::size_t i=0;i<particleCount;++i)if(inverseMass[i]>0&&particles[i].animationDrive>0){const auto offset=positions[i]-subTargets[i];const double length=Length(offset);
                    if(length>Epsilon){const double alpha=driveAlpha[i];const double dl=(-length-alpha*driveLambda[i])/(inverseMass[i]+alpha);driveLambda[i]+=dl;positions[i]+=offset*(inverseMass[i]*dl/length);}}
                for(std::size_t k=0;k<tetherCount;++k){const auto& t=tethers[k];const auto delta=positions[t.particle]-positions[t.anchor];const double length=Length(delta),sum=inverseMass[t.particle]+inverseMass[t.anchor];
                    if(length<=Epsilon||sum<=0)continue;const double c=length-tetherLength[k];const double next=std::min(0.0,tetherLambda[k]-c/sum);const double dl=next-tetherLambda[k];tetherLambda[k]=next;
                    const auto correction=delta*(dl/length);positions[t.particle]+=correction*inverseMass[t.particle];positions[t.anchor]-=correction*inverseMass[t.anchor];}
                for(std::size_t i=0;i<particleCount;++i)if(inverseMass[i]>0){const auto offset=positions[i]-subTargets[i];const double distance2=glm::dot(offset,offset),maximum=maximumDistance[i];if(distance2>maximum*maximum){const double distance=std::sqrt(distance2);if(distance>Epsilon)positions[i]=subTargets[i]+offset*(maximum/distance);}}
                // An early convergence candidate also gets an exact final
                // contact pass; two settled cycles cannot end on stale barys.
                const bool rebuildSurface=iteration%4==0||iteration+1==settings.iterations||(iteration+1>=4&&settledIterations>=1);
                if(settings.selfCollision)selfCollision();if(settings.collision){collide(iteration==0);collideSurface(iteration==0,rebuildSurface);
                    if(!collideConvexParticles(iteration==0)||!collideConvexSurface(iteration==0))return fail("Cloth convex sweep produced a singular transform");}
                barrierRepaired=preventAreaInversion(false)||barrierRepaired;
                for(std::size_t i=0;i<particleCount;++i)if(inverseMass[i]==0)positions[i]=subTargets[i];
                if(convergenceTolerance>0||profiling){double maximumDelta2=0;std::size_t unsettledParticles=0,worstParticle=0;for(std::size_t i=0;i<particleCount;++i)if(inverseMass[i]>0){const auto delta=positions[i]-iterationStart[i];const double move2=glm::dot(delta,delta);if(move2>maximumDelta2){maximumDelta2=move2;worstParticle=i;}if(profiling&&move2>convergenceTolerance*convergenceTolerance)++unsettledParticles;}
                    if(profiling){profileMaximumNetDelta2=std::max(profileMaximumNetDelta2,maximumDelta2);profileLastNetDelta2=maximumDelta2;profileLastBarrierDelta2=iterationBarrierDelta2;profileLastBarrierRepairs=iterationBarrierRepairs;profileLastUnsettledParticles=unsettledParticles;profileLastWorstParticle=worstParticle;if(barrierRepaired)++profileBarrierVetoIterations;}
                    // Test the complete projection cycle, including contact and
                    // pins. A repaired directed area barrier vetoes convergence
                    // even if opposing projections happened to cancel net motion.
                    if(convergenceTolerance>0){if(!barrierRepaired&&maximumDelta2<=convergenceTolerance*convergenceTolerance)++settledIterations;else settledIterations=0;
                        if(iteration+1>=4&&settledIterations>=2)break;}}
            }
            preventAreaInversion(true);
            for(std::size_t i=0;i<particleCount;++i){
                if(!Finite(positions[i])||Length(positions[i])>1e7){Reset(cloth,animatedGarment,nullptr);return fail("Cloth solver exceeded its finite SI safety bounds and reset to the animated pose");}
                velocity[i]=inverseMass[i]>0?(positions[i]-before[i])/h:glm::dvec3(0);
                if(contacted[i]&&settings.collision){for(std::size_t capsuleIndex=0;capsuleIndex<capsuleCount;++capsuleIndex){const auto& cap=subCapsules[capsuleIndex];const auto direction=positions[i]-Closest(positions[i],cap);const double distance=Length(direction),gap=distance-cap.radius-settings.thickness;
                        if(gap>settings.thickness*.1)continue;const auto n=distance>Epsilon?direction/distance:glm::dvec3(0,0,1);const auto colliderMotion=(cap.a-startCapsules[capsuleIndex].a+cap.b-startCapsules[capsuleIndex].b)*(.5/h);
                        auto relative=velocity[i]-colliderMotion;const double normalSpeed=glm::dot(relative,n);if(normalSpeed<0)relative-=n*normalSpeed;
                        const auto tangent=relative-n*glm::dot(relative,n);relative-=tangent*settings.friction;velocity[i]=colliderMotion+relative;}}
                if(contacted[i]&&settings.collision)for(const auto& data:subConvexes){const auto contact=detail::ConvexPointContact(data.end,positions[i],settings.thickness*1.1);
                    if(!contact.hit)continue;const auto local=data.inverseEnd*glm::dvec4(positions[i],1);
                    const auto colliderMotion=glm::dvec3(data.end.transform*local-data.start.transform*local)/h;
                    auto relative=velocity[i]-colliderMotion;const double speed=glm::dot(relative,contact.normal);if(speed<0)relative-=contact.normal*speed;
                    const auto tangent=relative-contact.normal*glm::dot(relative,contact.normal);relative-=tangent*settings.friction;velocity[i]=colliderMotion+relative;}
                // Bound an exceptional collision correction without changing
                // ordinary velocity. Reset/seek paths are handled above.
                const double speed=Length(velocity[i]);if(speed>100)velocity[i]*=100/speed;
            }
        }
        s.accumulator-=settings.fixedTimeStep;
        if(warmTicks&&tickTime<=warmDuration+1e-10){if(tickTime>=warmDuration-1e-10)std::fill_n(velocity,particleCount,glm::dvec3(0));}
        else ++s.stats.steps;
        tickTime+=settings.fixedTimeStep;
    }
    s.previousTargets=targets;s.previousCapsules=capsules;s.previousConvexes=convexes;diagnostics();
    for(std::size_t i=0;i<particleCount;++i)if(contacted[i])++s.stats.contacts;
    s.stats.selfContacts=selfContacts.size();s.stats.simulationMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();s.Publish();
    if(profiling){std::uint64_t exact=0,reused=0,separated=0,invalidated=0;for(const auto& t:surfaceTriangleStorage){exact+=t.exact;reused+=t.reused;separated+=t.separated;invalidated+=t.invalidated;}
        std::fprintf(stderr,"cloth profile step=%llu threads=%zu colored=%d substeps=%u iterations=%llu total=%.3fms prepare=%.3f distance=%.3f area=%.3f barrier=%.3f bend=%.3f particle=%.3f surface=%.3f diagnostics=%.3f netMax=%.6gmm netLast=%.6gmm unsettledLast=%zu worstParticle=%zu barrierRepairs=%llu barrierVetoIterations=%llu barrierMax=%.6gmm barrierLast=%.6gmm barrierRepairsLast=%llu contactCache=%d surfaceExact=%llu surfaceReuse=%llu surfaceSeparated=%llu surfaceInvalidated=%llu\n",static_cast<unsigned long long>(s.stats.steps),executor?executor->ThreadCount():std::size_t(1),int(executor!=nullptr),solvedSubsteps,static_cast<unsigned long long>(s.stats.constraintIterations),s.stats.simulationMilliseconds,prepareMilliseconds,distanceMilliseconds,areaMilliseconds,barrierMilliseconds,bendMilliseconds,particleMilliseconds,surfaceMilliseconds,diagnosticMilliseconds,std::sqrt(profileMaximumNetDelta2)*1000,std::sqrt(profileLastNetDelta2)*1000,profileLastUnsettledParticles,profileLastWorstParticle,static_cast<unsigned long long>(profileBarrierRepairs),static_cast<unsigned long long>(profileBarrierVetoIterations),std::sqrt(profileMaximumBarrierDelta2)*1000,std::sqrt(profileLastBarrierDelta2)*1000,static_cast<unsigned long long>(profileLastBarrierRepairs),int(surfaceCacheEnabled),static_cast<unsigned long long>(exact),static_cast<unsigned long long>(reused),static_cast<unsigned long long>(separated),static_cast<unsigned long long>(invalidated));}
    if(profiling&&separatingCacheEnabled){std::uint64_t pointHits=0,triangleHits=0;
        for(auto hits:particleSeparatingHits)pointHits+=hits;for(auto hits:triangleSeparatingHits)triangleHits+=hits;
        std::fprintf(stderr,"cloth separating proofs step=%llu particle=%llu surface=%llu\n",static_cast<unsigned long long>(s.stats.steps),static_cast<unsigned long long>(pointHits),static_cast<unsigned long long>(triangleHits));}
    if(profiling){auto sum=[](const auto& values){std::uint64_t total=0;for(auto value:values)total+=value;return static_cast<unsigned long long>(total);};
        std::fprintf(stderr,"cloth convex profile step=%llu pointAabb=%llu pointEndExact=%llu triangleEndExact=%llu triangleEndCompositeEmpty=%llu pointCcdQueries=%llu triangleCcdQueries=%llu sweepPrepareNested=%.3fms sweepCalls=%llu sweepRebuilds=%llu sweepBodyGrids=%llu sweepSamples=%llu sharedMidShapes=%llu localMidShapes=%llu compositeAttempt=%llu compositeFound=%llu compositeReuse=%llu\n",
            static_cast<unsigned long long>(s.stats.steps),sum(profilePointAabb),sum(profilePointExact),sum(profileTriangleExact),sum(profileTriangleComposite),sum(profilePointCcd),sum(profileTriangleCcd),convexSweepPrepareMilliseconds,
            static_cast<unsigned long long>(profileSweepCalls),static_cast<unsigned long long>(profileSweepRebuilds),static_cast<unsigned long long>(profileSweepBodyGrids),
            static_cast<unsigned long long>(profileSweepSamples),static_cast<unsigned long long>(profileSweepMidShapes),sum(profileTriangleLocalMidShapes),
            sum(profileCompositeAttempts),sum(profileCompositeFound),sum(profileCompositeReuse));}
    return true;
}

bool ClothSimulator::Apply(const ClothAsset& cloth,StaticModel& garment,std::string* error)const{
    auto fail=[&](const char* message){if(error)*error=message;return false;};
    if(!impl_||!impl_->initialized||impl_->signature!=Signature(cloth))return fail("Reset cloth against this garment asset before applying simulation positions");
    std::vector<glm::dmat4> transforms;if(!MeshTransforms(garment,transforms,error))return false;
    std::vector<glm::dmat4> inverse;inverse.reserve(transforms.size());for(const auto& transform:transforms)inverse.push_back(glm::inverse(transform));
    for(const auto& b:cloth.renderBindings)if(b.mesh>=garment.meshes.size()||b.vertex>=garment.meshes[b.mesh].vertices.size()||b.particle>=impl_->positions.size())return fail("Cloth render binding is outside the output garment geometry");
    for(const auto& mesh:garment.meshes){if(mesh.indices.size()%3)return fail("Cloth garment render topology is not a triangle list");for(auto index:mesh.indices)if(index>=mesh.vertices.size())return fail("Cloth garment triangle index is out of range");}
    for(const auto& b:cloth.renderBindings)garment.meshes[b.mesh].vertices[b.vertex].position=glm::dvec3(inverse[b.mesh]*glm::dvec4(impl_->positions[b.particle],1));
    std::vector<glm::dvec3> particleNormals(cloth.particles.size(),glm::dvec3(0));
    for(const auto& tri:cloth.triangles){const auto normal=glm::cross(impl_->positions[tri[1]]-impl_->positions[tri[0]],impl_->positions[tri[2]]-impl_->positions[tri[0]]);for(auto particle:tri)particleNormals[particle]+=normal;}
    std::vector<std::vector<std::uint32_t>> bindings(garment.meshes.size());
    for(std::size_t m=0;m<bindings.size();++m)bindings[m].assign(garment.meshes[m].vertices.size(),std::numeric_limits<std::uint32_t>::max());
    for(const auto& b:cloth.renderBindings)bindings[b.mesh][b.vertex]=b.particle;
    garment.bounds={};
    for(std::size_t m=0;m<garment.meshes.size();++m){auto& mesh=garment.meshes[m];mesh.bounds={};std::vector<glm::dvec3> normals(mesh.vertices.size(),glm::dvec3(0)),tangents=normals,bitangents=normals;
        for(std::size_t t=0;t<mesh.indices.size();t+=3){const auto a=mesh.indices[t],b=mesh.indices[t+1],c=mesh.indices[t+2];const auto ab=mesh.vertices[b].position-mesh.vertices[a].position,ac=mesh.vertices[c].position-mesh.vertices[a].position;
            const auto normal=glm::cross(ab,ac);const auto uv1=glm::dvec2(mesh.vertices[b].uv-mesh.vertices[a].uv),uv2=glm::dvec2(mesh.vertices[c].uv-mesh.vertices[a].uv);const double determinant=uv1.x*uv2.y-uv1.y*uv2.x;
            glm::dvec3 tangent(0),bitangent(0);if(std::abs(determinant)>Epsilon){tangent=(ab*uv2.y-ac*uv1.y)/determinant;bitangent=(ac*uv1.x-ab*uv2.x)/determinant;}
            for(auto v:{a,b,c}){normals[v]+=normal;tangents[v]+=tangent;bitangents[v]+=bitangent;}}
        for(std::size_t i=0;i<mesh.vertices.size();++i){auto& vertex=mesh.vertices[i];if(!Finite(vertex.position))return fail("Cloth output contains a nonfinite vertex");auto normal=Unit(normals[i],Unit(glm::dvec3(vertex.normal),{0,1,0}));const auto particle=bindings[m][i];
            // Derive one normal per welded simulation vertex, then transform
            // back to each render instance. UV seam copies must shade smoothly
            // even though their tangent directions can legitimately differ.
            if(particle<particleNormals.size()&&Length(particleNormals[particle])>Epsilon)normal=Unit(glm::transpose(glm::dmat3(transforms[m]))*particleNormals[particle],normal);
            const auto tangent=Unit(tangents[i]-normal*glm::dot(normal,tangents[i]),Unit(glm::cross(std::abs(normal.y)<.9?glm::dvec3(0,1,0):glm::dvec3(1,0,0),normal),{1,0,0}));
            vertex.normal=glm::vec3(normal);vertex.tangent=glm::vec4(glm::vec3(tangent),glm::dot(glm::cross(normal,tangent),bitangents[i])<0?-1.f:1.f);mesh.bounds.Include(vertex.position);
            garment.bounds.Include(glm::dvec3(transforms[m]*glm::dvec4(vertex.position,1)));}
    }
    return true;
}

const std::vector<glm::dvec3>& ClothSimulator::Positions()const{static const std::vector<glm::dvec3> empty;return impl_?impl_->positions:empty;}
const ClothSimulationStats& ClothSimulator::Stats()const{static const ClothSimulationStats empty;return impl_?impl_->stats:empty;}
} // namespace Model
