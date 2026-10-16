#include "Model/Public/cloth_asset.h"
#include <json/json.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace Model {
namespace {
namespace fs=std::filesystem;
constexpr std::size_t MaxParticles=500000, MaxConstraints=3000000, MaxBindings=4000000;
constexpr std::uint64_t MaxJsonBytes=256ull*1024*1024;
void Need(bool value,const std::string& message) { if(!value) throw std::runtime_error(message); }
std::string Utf8(const fs::path& p) { const auto s=p.generic_u8string();return {reinterpret_cast<const char*>(s.data()),s.size()}; }
fs::path Path(const std::string& s) { return fs::u8path(s); }
void Finite(double v) { Need(std::isfinite(v),"Cloth contains a non-finite value."); }
template<class V> void FiniteVector(const V& v) { for(glm::length_t i=0;i<v.length();++i) Finite(v[i]); }
template<class V> Json::Value Vector(const V& v) { Json::Value r(Json::arrayValue);for(glm::length_t i=0;i<v.length();++i) r.append(v[i]);return r; }
template<class V> V ReadVector(const Json::Value& a) {
    V v(0);Need(a.isArray()&&a.size()==v.length(),"Invalid cloth vector.");
    for(glm::length_t i=0;i<v.length();++i) { Need(a[i].isNumeric(),"Expected cloth numeric component.");v[i]=a[i].asDouble();Finite(v[i]); }return v;
}
double Number(const Json::Value& v) { Need(v.isNumeric(),"Missing numeric cloth property.");const auto x=v.asDouble();Finite(x);return x; }
std::uint32_t U32(const Json::Value& v) { Need(v.isUInt(),"Missing or overflowing cloth integer.");return v.asUInt(); }
std::int32_t I32(const Json::Value& v) { Need(v.isInt(),"Missing or overflowing cloth signed integer.");return v.asInt(); }
bool Boolean(const Json::Value& v) { Need(v.isBool(),"Missing cloth boolean.");return v.asBool(); }
std::string String(const Json::Value& v) { Need(v.isString()&&v.asString().size()<=65536,"Missing or oversized cloth string.");return v.asString(); }
void Array(const Json::Value& v,std::size_t maximum,const char* name) { Need(v.isArray()&&v.size()<=maximum,std::string("Missing or oversized cloth ")+name+"."); }
Json::Value ReadJson(const fs::path& path) {
    std::error_code ec;const auto size=fs::file_size(path,ec);Need(!ec&&size>0&&size<=MaxJsonBytes,"Missing or oversized cloth JSON: "+Utf8(path));
    std::ifstream f(path,std::ios::binary);Need(bool(f),"Cannot open cloth JSON.");Json::CharReaderBuilder b;
    b["rejectDupKeys"]=true;b["failIfExtra"]=true;b["allowSpecialFloats"]=false;
    Json::Value r;std::string e;Need(Json::parseFromStream(b,f,&r,&e),"Invalid cloth JSON: "+e);return r;
}
void WriteJson(const fs::path& path,const Json::Value& value) {
    fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary|std::ios::trunc);Need(bool(f),"Cannot create cloth JSON.");
    Json::StreamWriterBuilder b;b["indentation"]="";b["precision"]=17;f<<Json::writeString(b,value)<<'\n';f.close();Need(bool(f),"Cannot write cloth JSON.");
}
void RegularPath(const fs::path& path,bool allowMissing=false) {
    auto prefix=path.root_path();for(const auto& p:path.relative_path()) {
        prefix/=p;
#ifdef _WIN32
        const auto attrs=GetFileAttributesW(prefix.c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES) { const auto e=GetLastError();Need(allowMissing&&(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND),"Missing/inaccessible cloth path: "+Utf8(prefix));continue; }
        Need((attrs&FILE_ATTRIBUTE_REPARSE_POINT)==0,"Cloth references cannot traverse symlinks or junctions.");
#else
        std::error_code ec;const auto s=fs::symlink_status(prefix,ec);if(!fs::exists(s)) { Need(allowMissing,"Missing cloth path.");continue; }
        Need(!ec&&!fs::is_symlink(s),"Cloth references cannot traverse symlinks.");
#endif
    }
}
fs::path Child(const fs::path& parent,const Json::Value& value) {
    const auto s=String(value);Need(!s.empty(),"Empty modular character reference.");const auto relative=Path(s);
    Need(!relative.is_absolute()&&!relative.has_root_name()&&!relative.has_root_directory(),"Absolute modular references are not allowed.");
    for(const auto& p:relative) {
        const auto c=Utf8(p);Need(!c.empty()&&c!="."&&c!=".."&&c.find(':')==std::string::npos,"Modular reference escapes its package.");
#ifdef _WIN32
        Need(c.back()!='.'&&c.back()!=' '&&c.find_first_of("<>\"|?*")==std::string::npos,"Ambiguous Windows asset filename.");
#endif
    }
    const auto base=fs::absolute(parent).lexically_normal(),target=(base/relative).lexically_normal();
    const auto within=target.lexically_relative(base);Need(!within.empty()&&*within.begin()!="..","Modular reference escapes its package.");RegularPath(target);return target;
}
void Header(const Json::Value& r,const char* kind) { Need(r.isObject()&&r["assetType"].isString()&&r["assetType"].asString()==kind,"Incorrect cloth asset type.");const auto version=U32(r["version"]);Need(version>=1&&version<=ClothAssetVersion,"Unsupported cloth asset version."); }
struct Hash {
    std::uint64_t v=14695981039346656037ull;
    void Byte(std::uint8_t b) {v^=b;v*=1099511628211ull;}
    template<class T> void Scalar(T value) {
        // Define the fingerprint's byte order explicitly, independent of ABI/padding.
        if constexpr(std::is_floating_point_v<T>) {Finite(value);if(value==0)value=0;}
        const auto bytes=std::bit_cast<std::array<std::uint8_t,sizeof(T)>>(value);
        if constexpr(std::endian::native==std::endian::little)for(auto b:bytes)Byte(b);else for(auto i=bytes.rbegin();i!=bytes.rend();++i)Byte(*i);
    }
    void Size(std::size_t n) {Scalar(std::uint64_t(n));}
    void Text(const std::string& s) {Size(s.size());for(unsigned char b:s)Byte(b);}
    template<class V>void Vec(const V& v) {for(glm::length_t i=0;i<v.length();++i)Scalar(v[i]);}
    void Matrix(const glm::dmat4& m) {for(int i=0;i<4;++i)Vec(m[i]);}
    void Transform(const BoneTransform& t) {Vec(t.translation);Scalar(t.rotation.w);Scalar(t.rotation.x);Scalar(t.rotation.y);Scalar(t.rotation.z);Vec(t.scale);}
    std::string Finish()const {std::ostringstream s;s<<"fnv1a64-v1-"<<std::hex<<std::setw(16)<<std::setfill('0')<<v;return s.str();}
};
void SkeletonHash(Hash& h,const SkeletonAsset& s) {
    h.Text(s.id);h.Size(s.bones.size());for(const auto& b:s.bones) {
        h.Text(b.name);h.Text(b.englishName);h.Scalar(b.parent);h.Transform(b.restLocal);h.Matrix(b.inverseBind);
        h.Scalar(b.sourceFlags);h.Vec(b.fixedAxis);h.Vec(b.localAxisX);h.Vec(b.localAxisZ);h.Scalar(b.inheritParent);h.Scalar(b.inheritWeight);
    }
}
std::string CompatibilityHash(const SkeletalAssetPackage& a) {
    Hash h;SkeletonHash(h,a.skeleton);h.Size(a.morphs.size());for(const auto& m:a.morphs) {h.Text(m.name);h.Text(m.englishName);h.Scalar(m.sourceType);h.Scalar(m.panel);}return h.Finish();
}
bool SameLayout(const SkeletalAssetPackage& a,const SkeletalAssetPackage& b) {
    if(a.skeleton.bones.size()!=b.skeleton.bones.size()||a.morphs.size()!=b.morphs.size())return false;
    for(std::size_t i=0;i<a.skeleton.bones.size();++i) {
        const auto& x=a.skeleton.bones[i];const auto& y=b.skeleton.bones[i];
        if(x.name!=y.name||x.englishName!=y.englishName||x.parent!=y.parent
            ||x.restLocal.translation!=y.restLocal.translation||x.restLocal.rotation!=y.restLocal.rotation||x.restLocal.scale!=y.restLocal.scale
            ||x.sourceFlags!=y.sourceFlags||x.fixedAxis!=y.fixedAxis||x.localAxisX!=y.localAxisX||x.localAxisZ!=y.localAxisZ
            ||x.inheritParent!=y.inheritParent||x.inheritWeight!=y.inheritWeight)return false;
        for(int c=0;c<4;++c)if(x.inverseBind[c]!=y.inverseBind[c])return false;
    }
    for(std::size_t i=0;i<a.morphs.size();++i) {
        const auto& x=a.morphs[i];const auto& y=b.morphs[i];
        if(x.name!=y.name||x.englishName!=y.englishName||x.sourceType!=y.sourceType||x.panel!=y.panel)return false;
    }return true;
}
void SettingsValid(const ClothSolverSettings& s) {
    for(double x:{s.fixedTimeStep,s.convergenceTolerance,s.stretchCompliance,s.bendCompliance,s.areaCompliance,s.animationCompliance,s.damping,s.localDamping,s.animationDamping,s.friction,s.thickness})Finite(x);
    FiniteVector(s.gravity);FiniteVector(s.wind);
    Need(s.fixedTimeStep>=1.0/1000&&s.fixedTimeStep<=1.0/15,"Cloth fixed timestep must be 1/1000..1/15 seconds.");
    Need(s.substeps>=1&&s.substeps<=16&&s.iterations>=1&&s.iterations<=64,"Cloth iteration/substep budget exceeded.");
    Need(s.convergenceTolerance>=0&&s.convergenceTolerance<=.01,"Invalid cloth convergence tolerance.");
    Need(s.stretchCompliance>=0&&s.stretchCompliance<=1e4&&s.bendCompliance>=0&&s.bendCompliance<=1e8&&s.areaCompliance>=0&&s.areaCompliance<=1e4&&s.animationCompliance>=0&&s.animationCompliance<=1e4,"Invalid cloth compliance.");
    Need(s.damping>=0&&s.damping<=1000&&s.localDamping>=0&&s.localDamping<=1000&&s.animationDamping>=0&&s.animationDamping<=1000&&s.friction>=0&&s.friction<=1&&s.thickness>=1e-5&&s.thickness<=.25,"Invalid cloth damping, friction or thickness.");
    Need(glm::length(s.gravity)<=1000&&glm::length(s.wind)<=1000,"Cloth force exceeds supported range.");
}
Json::Value SettingsJson(const ClothSolverSettings& s) {
    Json::Value r;r["fixedTimeStep"]=s.fixedTimeStep;r["substeps"]=s.substeps;r["iterations"]=s.iterations;
    r["convergenceTolerance"]=s.convergenceTolerance;
    r["stretchCompliance"]=s.stretchCompliance;r["bendCompliance"]=s.bendCompliance;r["animationCompliance"]=s.animationCompliance;
    r["areaCompliance"]=s.areaCompliance;r["localDamping"]=s.localDamping;r["animationDamping"]=s.animationDamping;
    r["damping"]=s.damping;r["friction"]=s.friction;r["thickness"]=s.thickness;r["gravity"]=Vector(s.gravity);r["wind"]=Vector(s.wind);
    r["collision"]=s.collision;r["selfCollision"]=s.selfCollision;return r;
}
ClothSolverSettings ReadSettings(const Json::Value& r,bool legacy) {
    ClothSolverSettings s;s.fixedTimeStep=Number(r["fixedTimeStep"]);s.substeps=U32(r["substeps"]);s.iterations=U32(r["iterations"]);
    if(r.isMember("convergenceTolerance"))s.convergenceTolerance=Number(r["convergenceTolerance"]);
    else if(legacy)s.convergenceTolerance=0;
    s.stretchCompliance=Number(r["stretchCompliance"]);s.bendCompliance=Number(r["bendCompliance"]);s.animationCompliance=Number(r["animationCompliance"]);
    if(legacy)s.bendCompliance=100;
    else {s.areaCompliance=Number(r["areaCompliance"]);s.localDamping=Number(r["localDamping"]);s.animationDamping=Number(r["animationDamping"]);}
    s.damping=Number(r["damping"]);s.friction=Number(r["friction"]);s.thickness=Number(r["thickness"]);s.gravity=ReadVector<glm::dvec3>(r["gravity"]);s.wind=ReadVector<glm::dvec3>(r["wind"]);
    s.collision=Boolean(r["collision"]);s.selfCollision=Boolean(r["selfCollision"]);SettingsValid(s);return s;
}
void ValidateConvex(const ClothConvexCollider& shape,const SkeletalAssetPackage& garment,double units,double limit) {
    Need(shape.name.size()<=65536&&shape.bone>=0&&std::size_t(shape.bone)<garment.skeleton.bones.size(),"Invalid convex collision bone reference.");
    Need(shape.vertices.size()>=4&&shape.vertices.size()<=1024&&shape.triangles.size()>=4&&shape.triangles.size()<=256&&shape.planes.size()==shape.triangles.size(),"Convex collision shape exceeds its vertex/face budget or has missing planes.");
    const double tolerance=std::max(1e-9,units*1e-7);glm::dvec3 center(0);
    const auto neutral=GlobalBoneTransforms(garment.skeleton,RestPose(garment.skeleton));
    for(const auto& p:shape.vertices){FiniteVector(p);Need(glm::length(p)<=limit,"Convex collision vertex exceeds the supported range.");center+=p;}
    center/=double(shape.vertices.size());
    Need(shape.skin.empty()||shape.skin.size()==shape.vertices.size(),"Convex skin binding count does not match its vertices.");
    for(std::size_t i=0;i<shape.skin.size();++i){const auto& influences=shape.skin[i];Need(!influences.empty()&&influences.size()<=16,"Convex vertex has missing or excessive skin influences.");double total=0;glm::dvec4 reconstructed(0);std::set<std::int32_t> bones;
        for(const auto& influence:influences){Need(influence.bone>=0&&std::size_t(influence.bone)<garment.skeleton.bones.size()&&bones.insert(influence.bone).second,"Convex skin contains an absent or duplicate bone.");FiniteVector(influence.weightedPosition);Finite(influence.weight);Need(influence.weight>0&&influence.weight<=1+1e-6&&glm::length(influence.weightedPosition)<=limit,"Invalid convex skin contribution.");total+=influence.weight;reconstructed+=neutral[influence.bone]*glm::dvec4(influence.weightedPosition,influence.weight);}
        Need(std::abs(total-1)<=1e-6,"Convex skin weights must sum to one.");const auto expected=shape.referenceTransform*glm::dvec4(shape.vertices[i],1);Need(glm::length(glm::dvec3(reconstructed-expected))<=tolerance*4,"Convex skin bind contributions do not reconstruct the source support point.");}
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::pair<int,int>> edges;std::vector<bool> used(shape.vertices.size(),false);double volume=0;bool collisionFace=false;
    for(std::size_t i=0;i<shape.triangles.size();++i){const auto& t=shape.triangles[i];const auto& plane=shape.planes[i];
        Need(t[0]<shape.vertices.size()&&t[1]<shape.vertices.size()&&t[2]<shape.vertices.size()&&t[0]!=t[1]&&t[1]!=t[2]&&t[2]!=t[0],"Invalid convex collision face index.");
        const auto a=shape.vertices[t[0]],b=shape.vertices[t[1]],c=shape.vertices[t[2]],cross=glm::cross(b-a,c-a);const double length=glm::length(cross);
        Need(length>tolerance*tolerance,"Degenerate convex collision face.");const auto normal=cross/length;
        FiniteVector(plane.normal);Finite(plane.distance);Need(std::abs(glm::length(plane.normal)-1)<=1e-6&&std::abs(plane.distance)<=limit,"Convex collision plane must have a unit normal and finite distance.");
        collisionFace|=plane.collision;
        Need(glm::dot(normal,plane.normal)>=1-1e-6&&std::abs(glm::dot(normal,a)-plane.distance)<=tolerance,"Convex collision planes must match the ordered outward faces.");
        Need(glm::dot(normal,center)<plane.distance-tolerance*.01,"Convex collision faces have inward winding or zero volume.");
        for(const auto& p:shape.vertices)Need(glm::dot(plane.normal,p)<=plane.distance+tolerance,"Convex collision plane excludes one of its own vertices.");
        for(int k=0;k<3;++k){used[t[k]]=true;const auto first=t[k],second=t[(k+1)%3];auto& edge=edges[{std::min(first,second),std::max(first,second)}];++edge.first;edge.second+=first<second?1:-1;}
        volume+=glm::dot(a-center,glm::cross(b-center,c-center))/6;
    }
    for(const auto& [edge,count]:edges)Need(count.first==2&&count.second==0,"Convex collision surface must be a closed oriented manifold.");
    if(shape.skin.empty())for(bool v:used)Need(v,"Rigid convex collision shape contains an unused vertex.");Need(volume>tolerance*tolerance*tolerance,"Convex collision shape has no enclosed volume.");Need(collisionFace,"Convex collision shape has no physical exterior face.");
    for(int column=0;column<4;++column)FiniteVector(shape.referenceTransform[column]);
    const auto& matrix=shape.referenceTransform;
    Need(std::abs(matrix[0][3])<1e-10&&std::abs(matrix[1][3])<1e-10&&std::abs(matrix[2][3])<1e-10&&std::abs(matrix[3][3]-1)<1e-10&&std::abs(glm::determinant(glm::dmat3(matrix)))>1e-12,"Convex reference transform must be finite, invertible and affine.");
    if(shape.referenceValid){const auto& expected=neutral[shape.bone];for(int column=0;column<4;++column)for(int row=0;row<4;++row)Need(std::abs(matrix[column][row]-expected[column][row])<=tolerance,"Convex reference transform does not match the skeleton bind pose.");}
}
void Validate(const ClothAsset& c,const SkeletalAssetPackage& g) {
    std::string e;const auto garmentValid=ValidateSkeletalAsset(g,&e);Need(garmentValid,"Invalid garment: "+e);
    Need(c.version>=1&&c.version<=ClothAssetVersion,"Unsupported cloth asset version.");
    Need(!c.id.empty()&&!c.name.empty()&&!c.garmentId.empty()&&!c.skeletonId.empty(),"Missing cloth identity.");
    Need(c.id.size()<=65536&&c.name.size()<=65536&&c.garmentId.size()<=65536&&c.skeletonId.size()<=65536,"Cloth identity exceeds format limit.");
    Need(c.garmentId==g.id&&c.skeletonId==g.skeleton.id,"Cloth was authored for a different garment or skeleton.");
    Need(c.topologyHash==ClothTopologyHash(g),"Cloth topology/rest/skin fingerprint does not match this garment.");
    Finite(c.unitsPerMeter);Need(c.unitsPerMeter>=1e-5&&c.unitsPerMeter<=1e8,"Unsupported source unit scale.");SettingsValid(c.settings);
    Need(c.motionBone>=-1&&(c.motionBone<0||std::size_t(c.motionBone)<g.skeleton.bones.size()),"Invalid cloth motion reference bone.");
    for(int col=0;col<4;++col)FiniteVector(c.motionInverseBind[col]);
    Need(std::abs(c.motionInverseBind[0][3])<1e-10&&std::abs(c.motionInverseBind[1][3])<1e-10&&std::abs(c.motionInverseBind[2][3])<1e-10&&std::abs(c.motionInverseBind[3][3]-1)<1e-10&&std::abs(glm::determinant(glm::dmat3(c.motionInverseBind)))>1e-12,"Cloth motion reference must be an invertible affine matrix.");
    if(c.motionBone>=0)for(int col=0;col<4;++col)for(int row=0;row<4;++row)Need(std::abs(c.motionInverseBind[col][row]-g.skeleton.bones[c.motionBone].inverseBind[col][row])<1e-8,"Cloth motion reference does not match its skeleton rest pose.");
    Need(!c.particles.empty()&&c.particles.size()<=MaxParticles,"Missing or oversized simulation mesh.");
    Need(!c.triangles.empty()&&c.triangles.size()<=MaxConstraints&&c.stretch.size()<=MaxConstraints&&c.bend.size()<=MaxConstraints&&c.tethers.size()<=MaxParticles*4,"Cloth constraint budget exceeded.");
    Need(c.renderBindings.size()<=MaxBindings&&c.capsules.size()<=1024&&c.convexes.size()<=32&&c.diagnostics.size()<=10000,"Cloth binding/collider budget exceeded.");
    const auto n=c.particles.size();const double limit=c.unitsPerMeter*10000;
    std::vector<std::size_t> parent(n);std::iota(parent.begin(),parent.end(),0);
    const auto find=[&](std::size_t x) {while(parent[x]!=x){parent[x]=parent[parent[x]];x=parent[x];}return x;};
    for(const auto& p:c.particles) {
        FiniteVector(p.restPosition);Finite(p.inverseMass);Finite(p.maxDistance);Finite(p.animationDrive);
        Need(glm::length(p.restPosition)<=limit&&p.inverseMass>=0&&p.inverseMass<=1e12&&p.maxDistance>=0&&p.maxDistance<=limit&&p.animationDrive>=0&&p.animationDrive<=1,"Invalid cloth particle values.");
        Need(p.inverseMass>0||p.maxDistance==0,"Kinematic cloth anchors must have zero max distance.");
    }
    std::set<std::array<std::uint32_t,3>> triangles;
    std::vector<bool> used(n,false);
    for(auto t:c.triangles) {
        Need(t[0]<n&&t[1]<n&&t[2]<n&&t[0]!=t[1]&&t[1]!=t[2]&&t[0]!=t[2],"Invalid cloth triangle index.");
        const auto ab=c.particles[t[1]].restPosition-c.particles[t[0]].restPosition,ac=c.particles[t[2]].restPosition-c.particles[t[0]].restPosition;
        Need(glm::length(glm::cross(ab,ac))>1e-18,"Degenerate simulation triangle.");
        for(auto p:t)used[p]=true;std::sort(t.begin(),t.end());Need(triangles.insert(t).second,"Duplicate cloth simulation triangle.");
    }
    auto constraints=[&](const auto& cs,bool connect) {
        std::set<std::uint64_t> edges;for(const auto& x:cs) {
            Finite(x.restLength);Need(x.a<n&&x.b<n&&x.a!=x.b&&x.restLength>0&&x.restLength<=limit,"Invalid cloth distance constraint.");
            const auto key=(std::uint64_t(std::min(x.a,x.b))<<32)|std::max(x.a,x.b);Need(edges.insert(key).second,"Duplicate cloth distance constraint.");
            if(connect)parent[find(x.a)]=find(x.b);
        }
    };constraints(c.stretch,true);constraints(c.bend,false);
    std::vector<bool> anchored(n,false),bound(n,false);
    for(std::size_t i=0;i<n;++i)if(c.particles[i].inverseMass==0)anchored[find(i)]=true;
    for(std::size_t i=0;i<n;++i)Need(used[i]&&anchored[find(i)],"Each cloth component must contain triangles and a kinematic anchor.");
    std::set<std::uint64_t> tetherKeys;
    for(const auto& t:c.tethers) {
        Finite(t.restLength);Need(t.particle<n&&t.anchor<n&&c.particles[t.anchor].inverseMass==0&&t.restLength>=0&&t.restLength<=limit,"Invalid cloth tether/anchor.");
        Need(find(t.particle)==find(t.anchor),"Tether crosses disconnected cloth components.");
        Need(tetherKeys.insert((std::uint64_t(t.particle)<<32)|t.anchor).second,"Duplicate cloth tether.");
    }
    Need(g.geometry!=nullptr,"Garment geometry is absent.");std::vector<std::vector<bool>> vertices;
    std::size_t vertexCount=0;for(const auto& m:g.geometry->meshes){vertices.emplace_back(m.vertices.size(),false);vertexCount+=m.vertices.size();}
    Need(vertexCount==c.renderBindings.size(),"Every garment render vertex must have exactly one simulation binding.");
    for(const auto& b:c.renderBindings) {
        Need(b.mesh<vertices.size()&&b.vertex<vertices[b.mesh].size()&&b.particle<n,"Invalid cloth render binding.");
        Need(!vertices[b.mesh][b.vertex],"Duplicate cloth render vertex binding.");vertices[b.mesh][b.vertex]=true;bound[b.particle]=true;
        const auto modelPosition=glm::dvec3(g.skins[b.mesh].bindTransform*glm::dvec4(g.geometry->meshes[b.mesh].vertices[b.vertex].position,1));
        const double bindingTolerance=std::max(1e-5,c.unitsPerMeter*1e-4);
        Need(glm::length(modelPosition-c.particles[b.particle].restPosition)<=bindingTolerance,"Cloth rest particle does not coincide with its welded render binding.");
    }
    for(bool b:bound)Need(b,"Simulation particle has no render binding.");
    for(const auto& x:c.capsules) {
        Need(x.name.size()<=65536&&x.boneA>=0&&x.boneB>=0&&std::size_t(x.boneA)<g.skeleton.bones.size()&&std::size_t(x.boneB)<g.skeleton.bones.size(),"Invalid cloth capsule bone reference.");
        FiniteVector(x.localA);FiniteVector(x.localB);Finite(x.radius);Need(x.radius>0&&x.radius<=limit&&glm::length(x.localA)<=limit&&glm::length(x.localB)<=limit,"Invalid cloth capsule size/offset.");
        FiniteVector(x.restA);FiniteVector(x.restB);Need(glm::length(x.restA)<=limit&&glm::length(x.restB)<=limit,"Invalid neutral cloth collision capsule.");
    }
    for(const auto& shape:c.convexes)ValidateConvex(shape,g,c.unitsPerMeter,limit);
    for(const auto& d:c.diagnostics)Need(d.size()<=65536,"Oversized cloth diagnostic.");
}
Json::Value ClothJson(const ClothAsset& c) {
    Json::Value r;r["assetType"]="Cloth";r["version"]=c.version;r["id"]=c.id;r["name"]=c.name;r["skeletonId"]=c.skeletonId;r["garmentId"]=c.garmentId;r["topologyHash"]=c.topologyHash;r["unitsPerMeter"]=c.unitsPerMeter;r["settings"]=SettingsJson(c.settings);
    r["version"]=ClothAssetVersion;r["motionBone"]=c.motionBone;r["motionInverseBind"]=Json::Value(Json::arrayValue);for(int col=0;col<4;++col)for(int row=0;row<4;++row)r["motionInverseBind"].append(c.motionInverseBind[col][row]);
    for(const char* name:{"particles","triangles","stretch","bend","tethers","renderBindings","capsules","convexes","diagnostics"})r[name]=Json::Value(Json::arrayValue);
    for(const auto& p:c.particles){Json::Value v;v["restPosition"]=Vector(p.restPosition);v["inverseMass"]=p.inverseMass;v["maxDistance"]=p.maxDistance;v["animationDrive"]=p.animationDrive;r["particles"].append(v);}
    for(const auto& t:c.triangles){Json::Value v(Json::arrayValue);for(auto x:t)v.append(x);r["triangles"].append(v);}
    const auto distance=[&](const char* name,const auto& cs){for(const auto& c:cs){Json::Value v;v["a"]=c.a;v["b"]=c.b;v["restLength"]=c.restLength;r[name].append(v);}};distance("stretch",c.stretch);distance("bend",c.bend);
    for(const auto& t:c.tethers){Json::Value v;v["particle"]=t.particle;v["anchor"]=t.anchor;v["restLength"]=t.restLength;r["tethers"].append(v);}
    for(const auto& b:c.renderBindings){Json::Value v;v["mesh"]=b.mesh;v["vertex"]=b.vertex;v["particle"]=b.particle;r["renderBindings"].append(v);}
    for(const auto& c:c.capsules){Json::Value v;v["name"]=c.name;v["boneA"]=c.boneA;v["boneB"]=c.boneB;v["localA"]=Vector(c.localA);v["localB"]=Vector(c.localB);v["radius"]=c.radius;v["referenceValid"]=c.referenceValid;v["restA"]=Vector(c.restA);v["restB"]=Vector(c.restB);r["capsules"].append(v);}
    for(const auto& shape:c.convexes){Json::Value v;v["name"]=shape.name;v["bone"]=shape.bone;v["referenceValid"]=shape.referenceValid;for(const char* name:{"vertices","triangles","planes","skin","referenceTransform"})v[name]=Json::Value(Json::arrayValue);
        for(const auto& p:shape.vertices)v["vertices"].append(Vector(p));for(const auto& t:shape.triangles){Json::Value triangle(Json::arrayValue);for(auto index:t)triangle.append(index);v["triangles"].append(triangle);}
        for(const auto& influences:shape.skin){Json::Value bindings(Json::arrayValue);for(const auto& influence:influences){Json::Value binding;binding["bone"]=influence.bone;binding["weightedPosition"]=Vector(influence.weightedPosition);binding["weight"]=influence.weight;bindings.append(binding);}v["skin"].append(bindings);}
        for(const auto& plane:shape.planes){Json::Value p;p["normal"]=Vector(plane.normal);p["distance"]=plane.distance;p["collision"]=plane.collision;v["planes"].append(p);}for(int column=0;column<4;++column)for(int row=0;row<4;++row)v["referenceTransform"].append(shape.referenceTransform[column][row]);r["convexes"].append(v);}
    for(const auto& d:c.diagnostics)r["diagnostics"].append(d);return r;
}
ClothAsset ParseCloth(const Json::Value& r) {
    Header(r,"Cloth");const auto sourceVersion=U32(r["version"]);const bool legacy=sourceVersion==1;ClothAsset c;c.id=String(r["id"]);c.name=String(r["name"]);c.skeletonId=String(r["skeletonId"]);c.garmentId=String(r["garmentId"]);c.topologyHash=String(r["topologyHash"]);c.unitsPerMeter=Number(r["unitsPerMeter"]);c.settings=ReadSettings(r["settings"],legacy);
    if(!legacy){c.motionBone=I32(r["motionBone"]);Need(r["motionInverseBind"].isArray()&&r["motionInverseBind"].size()==16,"Invalid cloth motion reference matrix.");for(int col=0;col<4;++col)for(int row=0;row<4;++row)c.motionInverseBind[col][row]=Number(r["motionInverseBind"][col*4+row]);}
    Array(r["particles"],MaxParticles,"particles");Array(r["triangles"],MaxConstraints,"triangles");Array(r["stretch"],MaxConstraints,"stretch constraints");Array(r["bend"],MaxConstraints,"bend constraints");Array(r["tethers"],MaxParticles*4,"tethers");Array(r["renderBindings"],MaxBindings,"bindings");Array(r["capsules"],1024,"capsules");Array(r["diagnostics"],10000,"diagnostics");
    for(const auto& p:r["particles"])c.particles.push_back({ReadVector<glm::dvec3>(p["restPosition"]),Number(p["inverseMass"]),Number(p["maxDistance"]),Number(p["animationDrive"])});
    for(const auto& t:r["triangles"]){Need(t.isArray()&&t.size()==3,"Invalid cloth triangle.");c.triangles.push_back({U32(t[0]),U32(t[1]),U32(t[2])});}
    const auto distance=[&](const char* name,auto& cs){for(const auto& v:r[name])cs.push_back({U32(v["a"]),U32(v["b"]),Number(v["restLength"])});};distance("stretch",c.stretch);distance("bend",c.bend);
    for(const auto& t:r["tethers"])c.tethers.push_back({U32(t["particle"]),U32(t["anchor"]),Number(t["restLength"])});
    for(const auto& b:r["renderBindings"])c.renderBindings.push_back({U32(b["mesh"]),U32(b["vertex"]),U32(b["particle"])});
    for(const auto& x:r["capsules"]){ClothCapsule cap{String(x["name"]),I32(x["boneA"]),I32(x["boneB"]),ReadVector<glm::dvec3>(x["localA"]),ReadVector<glm::dvec3>(x["localB"]),Number(x["radius"])};if(x.isMember("referenceValid")){cap.referenceValid=Boolean(x["referenceValid"]);cap.restA=ReadVector<glm::dvec3>(x["restA"]);cap.restB=ReadVector<glm::dvec3>(x["restB"]);}c.capsules.push_back(cap);}
    if(sourceVersion>=3){Array(r["convexes"],32,"convex colliders");for(const auto& v:r["convexes"]){ClothConvexCollider shape;shape.name=String(v["name"]);shape.bone=I32(v["bone"]);shape.referenceValid=Boolean(v["referenceValid"]);
        Array(v["vertices"],1024,"convex vertices");Array(v["triangles"],256,"convex faces");Array(v["planes"],256,"convex planes");Array(v["referenceTransform"],16,"convex reference transform");Need(v["referenceTransform"].size()==16,"Invalid convex reference transform.");
        for(const auto& p:v["vertices"])shape.vertices.push_back(ReadVector<glm::dvec3>(p));for(const auto& t:v["triangles"]){Need(t.isArray()&&t.size()==3,"Invalid convex collision triangle.");shape.triangles.push_back({U32(t[0]),U32(t[1]),U32(t[2])});}
        for(const auto& p:v["planes"])shape.planes.push_back({ReadVector<glm::dvec3>(p["normal"]),Number(p["distance"]),p.isMember("collision")?Boolean(p["collision"]):true});
        if(v.isMember("skin")){Array(v["skin"],1024,"convex skin bindings");for(const auto& bindings:v["skin"]){Array(bindings,16,"convex vertex influences");std::vector<ClothConvexInfluence> influences;for(const auto& binding:bindings)influences.push_back({I32(binding["bone"]),ReadVector<glm::dvec3>(binding["weightedPosition"]),Number(binding["weight"])});shape.skin.push_back(std::move(influences));}}
        for(int column=0;column<4;++column)for(int row=0;row<4;++row)shape.referenceTransform[column][row]=Number(v["referenceTransform"][column*4+row]);c.convexes.push_back(std::move(shape));}}
    for(const auto& d:r["diagnostics"])c.diagnostics.push_back(String(d));return c;
}
fs::path Candidate(const fs::path& target) {
    RegularPath(target.parent_path(),true);fs::create_directories(target.parent_path());RegularPath(target.parent_path());
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(int i=0;i<32;++i){const auto p=target.parent_path()/Path(".cloth-candidate-"+std::to_string(stamp)+"-"+std::to_string(i));if(fs::create_directory(p))return p;}
    throw std::runtime_error("Cannot allocate an isolated cloth asset candidate.");
}
void Cleanup(const fs::path& candidate,const fs::path& target) {
    if(!candidate.empty()&&candidate.parent_path()==target.parent_path()&&candidate.filename().native().starts_with(Path(".cloth-candidate-").native())) {std::error_code ec;fs::remove_all(candidate,ec);}
}
}

std::string ClothTopologyHash(const SkeletalAssetPackage& g) {
    Need(bool(g.geometry),"Garment has no geometry for cloth fingerprint.");Hash h;h.Text("cloth-topology-v1");h.Text(g.id);h.Text(CompatibilityHash(g));
    h.Size(g.geometry->meshes.size());for(const auto& m:g.geometry->meshes){h.Size(m.vertices.size());for(const auto& v:m.vertices)h.Vec(v.position);h.Size(m.indices.size());for(auto i:m.indices)h.Scalar(i);}
    h.Size(g.geometry->instances.size());for(const auto& i:g.geometry->instances){h.Scalar(i.meshIndex);h.Matrix(i.transform);}
    h.Size(g.skins.size());for(const auto& s:g.skins){h.Matrix(s.bindTransform);h.Size(s.boneFromMesh.size());for(const auto& m:s.boneFromMesh)h.Matrix(m);h.Size(s.vertices.size());for(const auto& v:s.vertices){for(auto j:v.joints)h.Scalar(j);h.Vec(v.weights);h.Scalar(std::uint8_t(v.mode));h.Vec(v.sdefC);h.Vec(v.sdefR0);h.Vec(v.sdefR1);}}
    // Keep legacy fingerprints byte-identical. Extended source weights change
    // the physical drive target even when the four-wide summary is unchanged.
    if(std::any_of(g.skins.begin(),g.skins.end(),[](const auto& skin){return !skin.influenceOffsets.empty();})) {
        h.Text("complete-skin-v1");
        for(const auto& skin:g.skins){h.Size(skin.influenceOffsets.size());for(auto offset:skin.influenceOffsets)h.Scalar(offset);h.Size(skin.influences.size());for(const auto& influence:skin.influences){h.Scalar(influence.joint);h.Scalar(influence.weight);}}
    }
    // Cloth uses the animated skinned position as its drive target. Morph deltas
    // affect that target and therefore belong to the binding fingerprint.
    for(const auto& m:g.morphs){h.Size(m.deltas.size());for(const auto& d:m.deltas){h.Scalar(d.mesh);h.Scalar(d.vertex);h.Vec(d.position);h.Vec(d.normal);}}return h.Finish();
}
bool GarmentCompatible(const SkeletalAssetPackage& body,const SkeletalAssetPackage& garment,std::string* error) {
    try{std::string e;auto valid=ValidateSkeletalAsset(body,&e);Need(valid,"Invalid body: "+e);valid=ValidateSkeletalAsset(garment,&e);Need(valid,"Invalid garment: "+e);
        Need(body.skeleton.id==garment.skeleton.id,"Replacement garment has a different skeleton ID. Retarget it offline first.");
        Need(SameLayout(body,garment),"Replacement garment requires the identical ordered rest skeleton and morph layout.");if(error)error->clear();return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
bool ValidateClothAsset(const ClothAsset& c,const SkeletalAssetPackage& garment,std::string* error) {
    try{Validate(c,garment);if(error)error->clear();return true;}catch(const std::exception& e){if(error)*error=e.what();return false;}
}
std::optional<ClothAsset> LoadClothAsset(const fs::path& path,const SkeletalAssetPackage& garment,std::string* error) {
    try{RegularPath(fs::absolute(path).lexically_normal());auto c=ParseCloth(ReadJson(path));Validate(c,garment);if(error)error->clear();return c;}catch(const std::exception& e){if(error)*error=e.what();return {};}
}
bool SaveClothAsset(const ClothAsset& c,const SkeletalAssetPackage& garment,const fs::path& path,std::string* error) {
    fs::path target,candidate;try{Validate(c,garment);target=fs::absolute(path).lexically_normal();Need(!fs::exists(target),"Cloth output already exists; export a new revision.");candidate=Candidate(target);
        const auto file=candidate/"Cloth.json";WriteJson(file,ClothJson(c));std::string e;const auto verified=LoadClothAsset(file,garment,&e);Need(bool(verified),"Cloth candidate verification failed: "+e);
#ifdef _WIN32
        Need(MoveFileW(file.c_str(),target.c_str())!=0,"Cannot publish cloth asset; output may already exist.");
#else
        fs::create_hard_link(file,target);fs::remove(file);
#endif
        fs::remove(candidate);candidate.clear();if(error)error->clear();return true;
    }catch(const std::exception& e){Cleanup(candidate,target);if(error)*error=e.what();return false;}
}
std::optional<ModularCharacterBundle> LoadModularCharacter(const fs::path& input,std::string* error) {
    try{const auto file=fs::is_directory(input)?input/"Character.asset.json":input;RegularPath(fs::absolute(file).lexically_normal());const auto r=ReadJson(file);Header(r,"ModularCharacter");
        ModularCharacterBundle b;b.assembly.id=String(r["id"]);b.assembly.name=String(r["name"]);b.assembly.skeletonId=String(r["skeletonId"]);
        Need(!b.assembly.id.empty()&&!b.assembly.name.empty()&&!b.assembly.skeletonId.empty(),"Missing modular character identity.");
        Array(r["slots"],1,"garment slots");Need(r["slots"].size()==1,"This runtime supports exactly one garment slot. Extra slots cannot be ignored.");
        const auto root=file.parent_path();std::string e;b.assembly.bodyPackage=Path(String(r["bodyPackage"]));b.body=LoadSkeletalAssetPackage(Child(root,r["bodyPackage"]),&e);Need(bool(b.body),"Body load failed: "+e);
        const auto& s=r["slots"][0];GarmentSlotReference slot;slot.slot=String(s["slot"]);slot.garmentId=String(s["garmentId"]);slot.package=Path(String(s["package"]));slot.cloth=Path(String(s["cloth"]));slot.enabled=Boolean(s["enabled"]);Need(!slot.slot.empty()&&!slot.garmentId.empty(),"Missing garment slot identity.");
        b.garment=LoadSkeletalAssetPackage(Child(root,s["package"]),&e);Need(bool(b.garment),"Garment load failed: "+e);
        Need(b.assembly.skeletonId==b.body->skeleton.id&&slot.garmentId==b.garment->id,"Modular asset identity does not match its references.");const auto compatible=GarmentCompatible(*b.body,*b.garment,&e);Need(compatible,e);
        auto c=LoadClothAsset(Child(root,s["cloth"]),*b.garment,&e);Need(bool(c),"Cloth load failed: "+e);b.cloth=std::move(*c);b.assembly.slots.push_back(std::move(slot));
        if(r.isMember("hairPhysics")){
            b.assembly.hairPhysics=Path(String(r["hairPhysics"]));
            b.hair=LoadHairAsset(Child(root,r["hairPhysics"]),*b.body,&e);Need(bool(b.hair),"Hair load failed: "+e);
        }
        if(r.isMember("diagnostics")){Array(r["diagnostics"],10000,"diagnostics");for(const auto& d:r["diagnostics"])b.diagnostics.push_back(String(d));}if(error)error->clear();return b;
    }catch(const std::exception& e){if(error)*error=e.what();return {};}
}
bool SaveModularCharacter(const ModularCharacterBundle& b,const fs::path& directory,std::string* error) {
    fs::path target,candidate;try{
        Need(b.body&&b.garment,"Modular character needs a body and garment.");Need(!b.assembly.id.empty()&&!b.assembly.name.empty(),"Missing modular character identity.");
        Need(b.assembly.slots.size()==1,"This runtime supports exactly one garment slot. Extra slots cannot be ignored.");
        const auto& s=b.assembly.slots[0];Need(!s.slot.empty()&&s.garmentId==b.garment->id&&b.assembly.skeletonId==b.body->skeleton.id,"Incorrect modular slot/skeleton identity.");
        std::string e;const auto compatible=GarmentCompatible(*b.body,*b.garment,&e);Need(compatible,e);Validate(b.cloth,*b.garment);
        target=fs::absolute(directory).lexically_normal();RegularPath(target,true);Need(!fs::exists(target)||(fs::is_directory(target)&&fs::is_empty(target)),"Modular output is not a new or empty directory.");candidate=Candidate(target);
        auto saved=SaveSkeletalAssetPackage(*b.body,candidate/"Body",&e);Need(saved,"Body save failed: "+e);
        saved=SaveSkeletalAssetPackage(*b.garment,candidate/"Garments/Skirt",&e);Need(saved,"Garment save failed: "+e);
        saved=SaveClothAsset(b.cloth,*b.garment,candidate/"Cloth/Skirt.cloth.json",&e);Need(saved,"Cloth save failed: "+e);
        if(b.hair){saved=SaveHairAsset(*b.hair,*b.body,candidate/"Hair/Hair.hair.json",&e);Need(saved,"Hair save failed: "+e);}
        Json::Value r;r["assetType"]="ModularCharacter";r["version"]=ClothAssetVersion;r["id"]=b.assembly.id;r["name"]=b.assembly.name;r["skeletonId"]=b.assembly.skeletonId;r["bodyPackage"]="Body/Package.asset.json";
        if(b.hair)r["hairPhysics"]="Hair/Hair.hair.json";
        Json::Value slot;slot["slot"]=s.slot;slot["garmentId"]=b.garment->id;slot["package"]="Garments/Skirt/Package.asset.json";slot["cloth"]="Cloth/Skirt.cloth.json";slot["enabled"]=s.enabled;r["slots"]=Json::Value(Json::arrayValue);r["slots"].append(slot);
        r["diagnostics"]=Json::Value(Json::arrayValue);Need(b.diagnostics.size()<=10000,"Too many modular diagnostics.");for(const auto& d:b.diagnostics){Need(d.size()<=65536,"Oversized modular diagnostic.");r["diagnostics"].append(d);}
        WriteJson(candidate/"Character.asset.json",r);const auto verified=LoadModularCharacter(candidate,&e);Need(bool(verified),"Modular candidate verification failed: "+e);
        if(fs::exists(target)){Need(fs::is_directory(target)&&fs::is_empty(target),"Modular output changed during export.");fs::remove(target);}fs::rename(candidate,target);candidate.clear();if(error)error->clear();return true;
    }catch(const std::exception& e){Cleanup(candidate,target);if(error)*error=e.what();return false;}
}
} // namespace Model
