#include "Model/Public/skeletal_asset.h"
#include <json/json.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace {
using Point=glm::dvec3;
namespace fs=std::filesystem;
constexpr std::uint64_t MaxMetadataBytes=64ull*1024*1024;
constexpr std::uint64_t MaxReferenceBytes=1024ull*1024*1024;
constexpr std::uint64_t MaxObjectVertices=20'000'000;
void Require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
bool Finite(const Point& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}

// Balanced median partitions keep even a dense subdivided character bounded
// by O(N log N) construction. Duplicate material/UV-seam points remain valid;
// an exact point match ends the far-side search immediately.
class PointTree {
    std::vector<Point> points_;
    std::vector<std::size_t> order_;
    void Build(std::size_t begin,std::size_t end,unsigned depth){
        if(begin>=end)return;
        const auto middle=begin+(end-begin)/2;const auto axis=depth%3;
        std::nth_element(order_.begin()+begin,order_.begin()+middle,order_.begin()+end,
            [&](std::size_t a,std::size_t b){return points_[a][axis]<points_[b][axis];});
        Build(begin,middle,depth+1);Build(middle+1,end,depth+1);
    }
    void Nearest(const Point& query,std::size_t begin,std::size_t end,unsigned depth,double& best,std::size_t& index)const{
        if(begin>=end||best==0)return;
        const auto middle=begin+(end-begin)/2;const auto axis=depth%3;
        const auto difference=query-points_[order_[middle]];const double distance=glm::dot(difference,difference);if(distance<best){best=distance;index=order_[middle];}
        const double split=difference[axis];
        if(split<0){Nearest(query,begin,middle,depth+1,best,index);if(split*split<best)Nearest(query,middle+1,end,depth+1,best,index);}
        else{Nearest(query,middle+1,end,depth+1,best,index);if(split*split<best)Nearest(query,begin,middle,depth+1,best,index);}
    }
public:
    explicit PointTree(std::vector<Point> points):points_(std::move(points)){
        Require(!points_.empty(),"Cannot compare an empty point cloud");order_.resize(points_.size());std::iota(order_.begin(),order_.end(),0);Build(0,points_.size(),0);
    }
    std::pair<double,std::size_t> Closest(const Point& query)const{double best=std::numeric_limits<double>::infinity();std::size_t index=0;Nearest(query,0,points_.size(),0,best,index);return {std::sqrt(best),index};}
    double Distance(const Point& query)const{return Closest(query).first;}
    const std::vector<Point>& Points()const{return points_;}
};
struct Statistics {
    double maximum=0,sum=0;std::uint64_t samples=0,overTolerance=0;
    void Add(double distance,double tolerance){Require(std::isfinite(distance),"Nearest-point comparison produced a non-finite distance");maximum=std::max(maximum,distance);sum+=distance;++samples;overTolerance+=distance>tolerance;}
    void Merge(const Statistics& other){maximum=std::max(maximum,other.maximum);sum+=other.sum;samples+=other.samples;overTolerance+=other.overTolerance;}
    double Mean()const{return samples?sum/samples:0;}
};
Json::Value ReadMetadata(const fs::path& path){
    std::error_code error;const auto bytes=fs::file_size(path,error);
    Require(!error&&bytes>0&&bytes<=MaxMetadataBytes,"Source metadata is missing, empty or exceeds 64 MiB");
    std::ifstream stream(path,std::ios::binary);Json::CharReaderBuilder builder;
    builder["collectComments"]=false;builder["rejectDupKeys"]=true;Json::Value result;std::string detail;
    Require(Json::parseFromStream(builder,stream,&result,&detail),"Cannot parse source metadata: "+detail);
    Require(result["assetType"]=="BlenderSourceMetadata"&&result["version"]==1&&result["objects"].isArray()&&result["objects"].size()<=100000,
        "Unsupported Blender source metadata");
    return result;
}
std::vector<Point> ReadReference(const fs::path& metadataPath,const Json::Value& reference){
    Require(reference.isObject()&&reference["file"].isString()&&reference["byteOffset"].isUInt64()&&reference["vertexCount"].isUInt64(),
        "Missing or invalid source pose point-cloud range");
    Require(reference["componentType"]=="float32"&&reference["byteOrder"]=="little"&&
        reference["coordinateSystem"]=="glTF right-handed Y-up (x,z,-y)"&&reference["shape"]=="Basis",
        "Source pose reference must contain original posed Basis positions in little-endian engine-world float32 XYZ");
    const auto name=reference["file"].asString();const auto relative=fs::u8path(name);
    Require(name.size()<=1024&&!relative.empty()&&!relative.is_absolute()&&relative==relative.filename(),
        "Source pose reference must be a sibling file of the metadata");
    const auto offset=reference["byteOffset"].asUInt64(),count=reference["vertexCount"].asUInt64();
    Require(count>0&&count<=MaxObjectVertices&&offset%12==0,"Source pose range has an empty/oversized count or a misaligned offset");
    const auto path=metadataPath.parent_path()/relative;std::error_code error;const auto fileBytes=fs::file_size(path,error);
    Require(!error&&fileBytes>0&&fileBytes<=MaxReferenceBytes&&fileBytes%12==0,"Source pose binary is missing, malformed or exceeds 1 GiB");
    const auto bytes=count*12;Require(offset<=fileBytes&&bytes<=fileBytes-offset,"Source pose point-cloud range exceeds its binary file");
    std::ifstream stream(path,std::ios::binary);Require(bool(stream),"Cannot open source pose binary");stream.seekg(static_cast<std::streamoff>(offset));
    std::vector<unsigned char> data(static_cast<std::size_t>(bytes));stream.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(bytes));
    Require(stream.gcount()==static_cast<std::streamsize>(bytes),"Source pose point-cloud range is truncated");
    std::vector<Point> result(static_cast<std::size_t>(count));
    for(std::size_t i=0;i<result.size();++i)for(std::size_t axis=0;axis<3;++axis){const auto* p=data.data()+i*12+axis*4;
        const auto bits=std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
        const auto value=std::bit_cast<float>(bits);Require(std::isfinite(value),"Source pose binary contains non-finite positions");result[i][axis]=value;}
    return result;
}
std::map<std::string,std::vector<Point>> ImportedPoints(const Model::SkeletalAssetPackage& asset,const Model::StaticModel& skinned){
    std::vector<std::string> objects(skinned.meshes.size());
    for(const auto& region:asset.meshRegions){Require(region.mesh<objects.size()&&objects[region.mesh].empty(),"Imported source object mapping is duplicate or outside the mesh list");
        Require(!region.sourceObject.empty(),"Imported render section has no original Blender object provenance");objects[region.mesh]=region.sourceObject;}
    for(const auto& object:objects)Require(!object.empty(),"An imported render section lacks Blender source object provenance");
    std::map<std::string,std::vector<Point>> result;std::vector<bool> instanced(skinned.meshes.size(),false);
    const auto append=[&](std::size_t mesh,const glm::dmat4& transform){Require(mesh<skinned.meshes.size(),"Imported instance references an invalid mesh");instanced[mesh]=true;
        auto& points=result[objects[mesh]];for(const auto& vertex:skinned.meshes[mesh].vertices){const auto world=Point(transform*glm::dvec4(vertex.position,1));Require(Finite(world),"Imported world-space point is not finite");points.push_back(world);}};
    if(skinned.instances.empty())for(std::size_t mesh=0;mesh<skinned.meshes.size();++mesh)append(mesh,glm::dmat4(1));
    else for(const auto& instance:skinned.instances)append(instance.meshIndex,instance.transform);
    for(bool found:instanced)Require(found,"Imported render section is missing its world-space instance");
    return result;
}
void VerifySourceMorphDefaults(const Model::SkeletalAssetPackage& asset,const Model::SkeletalPose& pose,const Json::Value& metadata){
    const auto& entries=metadata["sourcePose"]["morphDefaults"];
    Require(entries.isArray()&&entries.size()<=1'000'000,"Source metadata has no bounded morph default list");
    using Key=std::pair<std::string,std::string>;std::map<Key,double> nonzero;std::set<Key> matched;
    for(const auto& entry:entries){Require(entry["sourceObject"].isString()&&entry["name"].isString()&&entry["value"].isNumeric(),"Malformed source morph default");
        const double value=entry["value"].asDouble();Require(std::isfinite(value),"Source morph default is not finite");
        if(value!=0)Require(nonzero.emplace(Key{entry["sourceObject"].asString(),entry["name"].asString()},value).second,"Duplicate nonzero source morph default");}
    std::vector<std::string> objects(asset.geometry->meshes.size());for(const auto& region:asset.meshRegions)if(region.mesh<objects.size())objects[region.mesh]=region.sourceObject;
    std::size_t checkedTracks=0;
    for(std::size_t i=0;i<asset.morphs.size();++i){const auto& morph=asset.morphs[i];std::set<std::string> sourceObjects;
        for(const auto& delta:morph.deltas)if(delta.mesh<objects.size())sourceObjects.insert(objects[delta.mesh]);
        for(const auto& object:sourceObjects){const Key key{object,morph.name};const auto found=nonzero.find(key);if(found==nonzero.end())continue;
            Require(i<pose.morphWeights.size()&&std::isfinite(pose.morphWeights[i])&&std::abs(pose.morphWeights[i]-found->second)<=1e-6,
                "Imported SourcePose lost authored nonzero morph default: "+object+" / "+morph.name);
            matched.insert(key);++checkedTracks;}}
    Require(matched.size()==nonzero.size(),"An authored nonzero source morph default has no mapped native SourcePose expression");
    std::printf("SourcePose morph defaults: %zu authored nonzero expressions, %zu native section tracks verified\n",nonzero.size(),checkedTracks);
}
int Run(const fs::path& packagePath,const fs::path& metadataPath){
    const auto metadata=ReadMetadata(metadataPath);std::string error;const auto asset=Model::LoadSkeletalAssetPackage(packagePath,&error);
    Require(bool(asset),"Cannot load imported skeletal package: "+error);
    const auto sourcePose=std::find_if(asset->animations.begin(),asset->animations.end(),[](const auto& sequence){return sequence.name=="SourcePose";});
    Require(sourcePose!=asset->animations.end(),"Imported package has no SourcePose sequence");
    auto pose=Model::EvaluateSkeletalPose(*asset,&*sourcePose,0,false);
    VerifySourceMorphDefaults(*asset,pose,metadata);
    // The reference explicitly uses Basis while retaining the original
    // authored armature pose. Morph animation is an independent input and
    // must not accidentally turn a default eye effect into a fit deviation.
    std::fill(pose.morphWeights.begin(),pose.morphWeights.end(),0);
    Model::StaticModel skinned;Require(Model::SkinSkeletalMesh(*asset,pose,skinned,&error),"Cannot skin imported SourcePose: "+error);
    auto imported=ImportedPoints(*asset,skinned);Require(!imported.empty(),"Imported package has no source object point clouds");
    const auto& bounds=asset->geometry->bounds;Require(bounds.valid&&Finite(bounds.minimum)&&Finite(bounds.maximum),"Imported package has invalid scene bounds");
    const double height=bounds.maximum.y-bounds.minimum.y;Require(std::isfinite(height)&&height>=0,"Imported package has invalid scene height");
    const double tolerance=1e-4*std::max(1.0,height);Statistics total;std::set<std::string> compared;
    std::printf("Original Blender posed Basis vs imported SourcePose; symmetric nearest-point tolerance %.9g (height %.9g)\n",tolerance,height);
    for(const auto& object:metadata["objects"]){
        if(object["type"]!="MESH"||!object["export"]["nodes"].isArray()||object["export"]["nodes"].empty())continue;
        Require(object["name"].isString(),"Source metadata contains an unnamed exported object");const auto name=object["name"].asString();
        Require(compared.insert(name).second,"Source metadata contains duplicate exported object names");
        const auto found=imported.find(name);Require(found!=imported.end()&&!found->second.empty(),"Original exported object is absent from native geometry: "+name);
        auto reference=ReadReference(metadataPath,object["export"]["sourceReference"]);
        PointTree original(std::move(reference));PointTree native(std::move(found->second));Statistics stats,nativeToOriginal,originalToNative;
        const auto compare=[&](const PointTree& queries,const PointTree& targets,Statistics& direction,const char* label){
            std::size_t printed=0;
            for(std::size_t pointIndex=0;pointIndex<queries.Points().size();++pointIndex){const auto& point=queries.Points()[pointIndex];const auto [distance,nearestIndex]=targets.Closest(point);direction.Add(distance,tolerance);
                if(distance>tolerance&&printed++<8){const auto& nearest=targets.Points()[nearestIndex];
                    std::printf("  %s %s over[%zu] queryPointIndex=%zu point=(%.12g,%.12g,%.12g) nearestPointIndex=%zu nearest=(%.12g,%.12g,%.12g) distance=%.12g\n",
                        name.c_str(),label,printed,pointIndex,point.x,point.y,point.z,nearestIndex,nearest.x,nearest.y,nearest.z,distance);}}
        };
        compare(native,original,nativeToOriginal,"native->source");compare(original,native,originalToNative,"source->native");
        stats.Merge(nativeToOriginal);stats.Merge(originalToNative);
        std::printf("%-30s original=%zu native=%zu max=%.9g mean=%.9g over=%llu %s\n",name.c_str(),original.Points().size(),native.Points().size(),stats.maximum,stats.Mean(),
            static_cast<unsigned long long>(stats.overTolerance),stats.overTolerance?"FAIL":"ok");
        std::printf("  native->source samples=%llu max=%.12g mean=%.12g over=%llu | source->native samples=%llu max=%.12g mean=%.12g over=%llu\n",
            static_cast<unsigned long long>(nativeToOriginal.samples),nativeToOriginal.maximum,nativeToOriginal.Mean(),static_cast<unsigned long long>(nativeToOriginal.overTolerance),
            static_cast<unsigned long long>(originalToNative.samples),originalToNative.maximum,originalToNative.Mean(),static_cast<unsigned long long>(originalToNative.overTolerance));total.Merge(stats);
    }
    Require(compared.size()==imported.size(),"Native geometry contains source objects without original evaluated pose references");
    Require(total.samples>0,"No original evaluated source object references were compared");
    std::printf("Blender pose reference: objects=%zu samples=%llu max=%.9g mean=%.9g over=%llu tolerance=%.9g\n",compared.size(),static_cast<unsigned long long>(total.samples),
        total.maximum,total.Mean(),static_cast<unsigned long long>(total.overTolerance),tolerance);
    return total.overTolerance?1:0;
}
}
int main(int argc,char** argv){
    if(argc==5&&std::string(argv[1])=="--dump-clip") {
        try {
            std::string error;const auto asset=Model::LoadSkeletalAssetPackage(fs::u8path(argv[2]),&error);
            Require(bool(asset),"Cannot load pose dump package: "+error);
            const auto clip=std::find_if(asset->animations.begin(),asset->animations.end(),[&](const auto& value){return value.name==argv[3];});
            Require(clip!=asset->animations.end(),"Pose dump clip was not found");
            const auto pose=Model::EvaluateSkeletalPose(*asset,&*clip,0,false);Model::StaticModel model;
            Require(Model::SkinSkeletalMesh(*asset,pose,model,&error),"Cannot skin pose dump: "+error);
            std::ofstream stream(fs::u8path(argv[4]),std::ios::binary|std::ios::trunc);Require(bool(stream),"Cannot open pose dump");
            std::size_t points=0;
            for(const auto& mesh:model.meshes)for(const auto& vertex:mesh.vertices) {
                const double point[]{vertex.position.x,vertex.position.y,vertex.position.z};
                stream.write(reinterpret_cast<const char*>(point),sizeof(point));++points;
            }
            stream.close();Require(bool(stream),"Cannot save pose dump");
            std::printf("Native %s: %zu meshes / %zu points; mesh order, float64 XYZ, little endian.\n",argv[3],model.meshes.size(),points);return 0;
        }catch(const std::exception& error){std::fprintf(stderr,"Native pose dump failed: %s\n",error.what());return 1;}
    }
    if(argc!=3){std::fputs("Usage: blend_pose_reference_test <Package.asset.json> <source.blend.json>\n",stderr);return 2;}
    try{return Run(fs::u8path(argv[1]),fs::u8path(argv[2]));}
    catch(const std::exception& error){std::fprintf(stderr,"Blender pose reference FAILED: %s\n",error.what());return 1;}
}
