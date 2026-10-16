#include "cloth_convex.h"
#include "cloth_simd.h"
#include "cloth_convex_support_simd.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <glm/gtx/matrix_decompose.hpp>

#if (defined(__clang__) || defined(__GNUC__)) && (defined(_M_X64) || defined(__x86_64__))
#define MODEL_CLOTH_CONVEX_AVX2_IMPLEMENTED 1
#define MODEL_CLOTH_CONVEX_AVX2 __attribute__((target("avx2,no-fma")))
#define MODEL_CLOTH_CONVEX_AVX2_INLINE __attribute__((target("avx2,no-fma"),always_inline)) inline
#include <immintrin.h>
#else
#define MODEL_CLOTH_CONVEX_AVX2_IMPLEMENTED 0
#endif

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace Model::detail {
namespace {
bool Finite(glm::dvec3 value){return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);}
bool Affine(const glm::dmat4& matrix){
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(matrix[c][r]))return false;
    return std::abs(matrix[0][3])<1e-12&&std::abs(matrix[1][3])<1e-12&&std::abs(matrix[2][3])<1e-12&&
        std::abs(matrix[3][3]-1)<1e-12&&std::abs(glm::determinant(glm::dmat3(matrix)))>1e-30;
}
double Length(glm::dvec3 value){return std::sqrt(std::max(0.0,glm::dot(value,value)));}
glm::dmat4 Stretch(glm::dvec3 scale,glm::dvec3 skew){
    glm::dmat4 shear(1);shear[1][0]=skew.z;shear[2][0]=skew.y;shear[2][1]=skew.x;
    return shear*glm::scale(glm::dmat4(1),scale);
}
double QueryTieTolerance(const ClothWorldConvex& shape){
    const auto size=shape.maximum-shape.minimum;const double extent=size.x+size.y+size.z;
    const auto absolute=glm::max(glm::abs(shape.minimum),glm::abs(shape.maximum));
    return 1e-10*std::max(1.0,extent)+64*std::numeric_limits<double>::epsilon()*std::max({absolute.x,absolute.y,absolute.z});
}
}
std::uint32_t ClothConvexPlaneBudget(){
    std::uint32_t result=96;
#ifdef _WIN32
    char* stored=nullptr;std::size_t length=0;_dupenv_s(&stored,&length,"MODEL_CLOTH_CONVEX_PLANES");const char* value=stored;
#else
    const char* value=std::getenv("MODEL_CLOTH_CONVEX_PLANES");
#endif
    if(value&&*value){char* end=nullptr;const auto parsed=std::strtoul(value,&end,10);
        if(end&&!*end&&(parsed==0||(parsed>=4&&parsed<=256)))result=std::uint32_t(parsed);}
#ifdef _WIN32
    std::free(stored);
#endif
    return result;
}
ClothConvexRuntimeLayout CompileClothConvexLayout(const ClothConvexCollider& source,std::uint32_t budget){
    ClothConvexRuntimeLayout result;result.budget=source.skin.empty()?0:budget;
    if(!result.budget){result.sourcePlanes.resize(source.planes.size());for(std::size_t i=0;i<source.planes.size();++i)result.sourcePlanes[i]=i;return result;}
    std::vector<std::size_t> exterior,partitions;
    const double distanceTolerance=1e-10*std::max(1.0,Length(source.vertices.empty()?glm::dvec3(0):source.vertices.front()));
    for(std::size_t i=0;i<source.planes.size();++i){if(source.planes[i].collision){exterior.push_back(i);continue;}
        bool duplicate=false;for(auto other:partitions)if(glm::dot(source.planes[i].normal,source.planes[other].normal)>1-1e-12&&
            std::abs(source.planes[i].distance-source.planes[other].distance)<=distanceTolerance){duplicate=true;break;}
        if(!duplicate)partitions.push_back(i);}
    if(exterior.size()>result.budget){
        std::vector<std::uint8_t> selected(source.planes.size(),0);std::vector<std::size_t> chosen;
        std::vector<double> closest(source.planes.size(),-1);
        auto choose=[&](std::size_t index){if(!selected[index]&&chosen.size()<result.budget){selected[index]=1;chosen.push_back(index);
            // This is the same ordered max as rescanning every chosen face,
            // updated only once when that face joins the set: O(P*K), not
            // O(P*K*K). Seed order, ties and selected source identities stay
            // identical to the full-rescan reference.
            for(auto candidate:exterior)if(!selected[candidate])closest[candidate]=std::max(closest[candidate],glm::dot(source.planes[candidate].normal,source.planes[index].normal));}};
        // Seed geometrically distinct extremes without inventing new normals.
        // Source index is the tie breaker, so every pose and warm start uses
        // exactly the same source face identities and runtime plane ordering.
        for(int axis=0;axis<3;++axis)for(double sign:{1.,-1.}){double best=-std::numeric_limits<double>::infinity();std::size_t index=exterior.front();
            for(auto candidate:exterior){const double score=source.planes[candidate].normal[axis]*sign;if(score>best+1e-14){best=score;index=candidate;}}choose(index);}
        while(chosen.size()<result.budget){double best=-1;std::size_t index=exterior.front();
            for(auto candidate:exterior)if(!selected[candidate]){
                const double score=1-closest[candidate];if(score>best+1e-14){best=score;index=candidate;}}
            choose(index);
        }
        exterior=std::move(chosen);
    }
    result.sourcePlanes=std::move(exterior);result.sourcePlanes.insert(result.sourcePlanes.end(),partitions.begin(),partitions.end());
    std::sort(result.sourcePlanes.begin(),result.sourcePlanes.end());return result;
}
bool BuildClothWorldConvex(const ClothConvexCollider& source,const glm::dmat4& transform,ClothWorldConvex& output,const ClothConvexRuntimeLayout* layout){
    if(source.vertices.size()<4||source.planes.size()<4||source.planes.size()>256||!Affine(transform)||
        std::none_of(source.planes.begin(),source.planes.end(),[](const auto& plane){return plane.collision;}))return false;
    const auto inverseTranspose=glm::transpose(glm::inverse(transform));
    output.transform=transform;output.vertices.resize(source.vertices.size());output.planes.resize(source.planes.size());
    output.triangles=source.triangles;
    output.minimum=glm::dvec3(std::numeric_limits<double>::infinity());output.maximum=-output.minimum;
    for(std::size_t i=0;i<source.vertices.size();++i){
        const auto point=glm::dvec3(transform*glm::dvec4(source.vertices[i],1));if(!Finite(point))return false;
        output.vertices[i]=point;output.minimum=glm::min(output.minimum,point);output.maximum=glm::max(output.maximum,point);
    }
    // Neutral skinned shapes use the same reduced face identities and OBB
    // membership bounds as their later deformed shapes. Rigid legacy assets
    // keep the original exact plane path.
    if(!source.skin.empty())return RefitClothWorldConvex(source,output,layout);
    for(std::size_t i=0;i<source.planes.size();++i){
        const auto& plane=source.planes[i];const auto equation=inverseTranspose*glm::dvec4(plane.normal,-plane.distance);
        const double length=Length(glm::dvec3(equation));
        if(!std::isfinite(length)||length<1e-20||!std::isfinite(equation.w))return false;
        output.planes[i]={glm::dvec3(equation)/length,-equation.w/length,plane.collision};
    }
    return true;
}
bool RefitClothWorldConvex(const ClothConvexCollider& source,ClothWorldConvex& output,const ClothConvexRuntimeLayout* preparedLayout){
    if(source.vertices.size()<4||source.planes.size()!=source.triangles.size()||source.planes.size()<4||source.planes.size()>256||
        output.vertices.size()!=source.vertices.size()||!Affine(output.transform))return false;
    const auto inverse=glm::inverse(output.transform),inverseTranspose=glm::transpose(inverse);
    output.minimum=glm::dvec3(std::numeric_limits<double>::infinity());output.maximum=-output.minimum;
    glm::dvec3 localMinimum(std::numeric_limits<double>::infinity()),localMaximum=-localMinimum;
    for(const auto& point:output.vertices){if(!Finite(point))return false;const auto local=glm::dvec3(inverse*glm::dvec4(point,1));
        localMinimum=glm::min(localMinimum,local);localMaximum=glm::max(localMaximum,local);}
    ClothConvexRuntimeLayout generatedLayout;
    if(!preparedLayout){generatedLayout=CompileClothConvexLayout(source,ClothConvexPlaneBudget());preparedLayout=&generatedLayout;}
    const auto& indices=preparedLayout->sourcePlanes;
    output.triangles=source.triangles;output.planes.resize(indices.size()+6);
    // Refit has no recursive call or user callback. Reuse one complete cloud
    // per thread for every support direction; SIMD preserves each double dot
    // and the ordered scalar max, including signed-zero ties.
    thread_local PreparedClothSupportCloud supportCloud;
    supportCloud.Prepare(output.vertices);
    bool external=false;
    for(std::size_t runtime=0;runtime<indices.size();++runtime){const auto i=indices[runtime];if(i>=source.planes.size())return false;const auto& triangle=source.triangles[i];
        if(triangle[0]>=output.vertices.size()||triangle[1]>=output.vertices.size()||triangle[2]>=output.vertices.size())return false;
        const auto expected=glm::dvec3(inverseTranspose*glm::dvec4(source.planes[i].normal,0));
        auto normal=glm::cross(output.vertices[triangle[1]]-output.vertices[triangle[0]],output.vertices[triangle[2]]-output.vertices[triangle[0]]);
        const double length=Length(normal);
        if(length<1e-18){const double fallbackLength=Length(expected);if(fallbackLength<1e-20)return false;normal=expected/fallbackLength;}
        else {normal/=length;if(glm::dot(normal,expected)<0)normal=-normal;}
        const double distance=supportCloud.Maximum(normal);
        output.planes[runtime]={normal,distance,source.planes[i].collision};external|=source.planes[i].collision;
    }
    // Re-oriented supporting planes can enclose more than the deformed point
    // hull. Bound them in the dominant BONE frame, so a global rigid transform
    // does not change membership. The transformed OBB's eight corners provide
    // a conservative world AABB without inventing six axis-aligned skin faces.
    for(int axis=0;axis<3;++axis)for(int side=0;side<2;++side){glm::dvec3 normal(0);normal[axis]=side?-1.:1.;
        const double distance=side?-localMinimum[axis]:localMaximum[axis];const auto equation=inverseTranspose*glm::dvec4(normal,-distance);const double length=Length(glm::dvec3(equation));
        if(length<1e-20)return false;output.planes[indices.size()+axis*2+side]={glm::dvec3(equation)/length,-equation.w/length,false};}
    for(int corner=0;corner<8;++corner){const auto local=glm::dvec3(corner&1?localMaximum.x:localMinimum.x,corner&2?localMaximum.y:localMinimum.y,corner&4?localMaximum.z:localMinimum.z);
        const auto point=glm::dvec3(output.transform*glm::dvec4(local,1));output.minimum=glm::min(output.minimum,point);output.maximum=glm::max(output.maximum,point);}
    return external;
}
bool ClothConvexSimdAvailable(){
#if MODEL_CLOTH_CONVEX_AVX2_IMPLEMENTED
#ifdef _WIN32
    char* stored=nullptr;std::size_t length=0;
    const bool disabled=_dupenv_s(&stored,&length,"MODEL_CLOTH_CONVEX_SIMD")==0&&stored&&stored[0]=='0'&&stored[1]=='\0';
    std::free(stored);
#else
    const char* stored=std::getenv("MODEL_CLOTH_CONVEX_SIMD");
    const bool disabled=stored&&stored[0]=='0'&&stored[1]=='\0';
#endif
    return !disabled&&ClothSimdAvailable();
#else
    return false;
#endif
}
void PreparedClothConvexQueries::Prepare(const ClothWorldConvex& shape){
    owner=&shape;planes=shape.planes.data();planeCount=shape.planes.size();
    simd=planeCount>0&&planeCount<=262&&ClothConvexSimdAvailable();
    if(!simd){packets.clear();return;}
    packets.resize((planeCount+3)/4);
    for(std::size_t packet=0;packet<packets.size();++packet)for(std::size_t lane=0;lane<4;++lane){
        const auto index=packet*4+lane;auto& data=packets[packet];
        if(index<planeCount){const auto& plane=shape.planes[index];data.x[lane]=plane.normal.x;data.y[lane]=plane.normal.y;
            data.z[lane]=plane.normal.z;data.distance[lane]=plane.distance;}
        else {data.x[lane]=data.y[lane]=data.z[lane]=0;data.distance[lane]=std::numeric_limits<double>::infinity();}
    }
}
namespace {
ClothConvexContact PointContactScalar(const ClothWorldConvex& shape,glm::dvec3 point,double padding){
    ClothConvexContact result;result.point=point;
    if(!ConvexBoundsOverlap(point,point,shape,padding))return result;
    double separation=-std::numeric_limits<double>::infinity();const double tieTolerance=QueryTieTolerance(shape);
    for(std::size_t i=0;i<shape.planes.size();++i){const auto& plane=shape.planes[i];const double distance=glm::dot(plane.normal,point)-plane.distance;
        if(distance>padding){result.separatingPlane=i;return result;}
        // Artificial partition faces close a skin piece for membership and
        // triangle clipping. They are interior to the union of neighboring
        // pieces and must never become an outward contact constraint.
        if(plane.collision&&distance>separation+tieTolerance){separation=distance;result.normal=plane.normal;result.plane=i;}}
    result.hit=std::isfinite(separation);result.separation=separation;return result;
}
struct ClipVertex {glm::dvec3 point,bary;};
// Queries have no recursive triangle call or user callback; their point-query
// candidates use no polygon scratch. One buffer per physics thread therefore
// preserves independent graph colors while avoiding a 25 KB Win64 stack probe
// on every narrow-phase query. Only the first three entries need initializing.
struct TriangleScratch {std::array<ClipVertex,268> first,second;};
thread_local TriangleScratch triangleScratch;
struct ClippedTriangle {
    // A convex polygon clipped by P halfspaces has at most P+3 vertices.
    ClipVertex* input=triangleScratch.first.data();ClipVertex* output=triangleScratch.second.data();std::size_t count=3;
    ClippedTriangle(glm::dvec3 a,glm::dvec3 b,glm::dvec3 c){input[0]={a,{1,0,0}};input[1]={b,{0,1,0}};input[2]={c,{0,0,1}};}
    bool Clip(const ClothConvexPlane& plane,double padding){
        bool allInside=true;for(std::size_t i=0;i<count;++i)if(glm::dot(plane.normal,input[i].point)-plane.distance>padding){allInside=false;break;}
        if(allInside)return true;
        std::size_t next=0;ClipVertex previous=input[count-1];double oldDistance=glm::dot(plane.normal,previous.point)-plane.distance-padding;
        for(std::size_t i=0;i<count;++i){const auto current=input[i];const double distance=glm::dot(plane.normal,current.point)-plane.distance-padding;
            const bool inside=distance<=0,wasInside=oldDistance<=0;
            if(inside!=wasInside){const double fraction=oldDistance/(oldDistance-distance);
                if(next>=triangleScratch.first.size())return false;
                output[next++]={previous.point+(current.point-previous.point)*fraction,previous.bary+(current.bary-previous.bary)*fraction};}
            if(inside){if(next>=triangleScratch.first.size())return false;output[next++]=current;}
            previous=current;oldDistance=distance;
        }
        count=next;if(!count)return false;std::swap(input,output);return true;
    }
    bool ClipOffsets(const ClothConvexPlane& plane,glm::dvec3 offsets,double padding){
        // Offset is an affine function of material barycentrics. Intersections
        // therefore still use exact linear halfspace clipping, including when
        // a pin is allowed a negative authored depth in the body's skin.
        auto distance=[&](const ClipVertex& vertex){return glm::dot(plane.normal,vertex.point)-plane.distance-
            (plane.collision?ConvexInterpolatedContactOffset(offsets,vertex.bary):padding);};
        bool allInside=true;for(std::size_t i=0;i<count;++i)if(distance(input[i])>0){allInside=false;break;}
        if(allInside)return true;
        std::size_t next=0;ClipVertex previous=input[count-1];double oldDistance=distance(previous);
        for(std::size_t i=0;i<count;++i){const auto current=input[i];const double currentDistance=distance(current);
            const bool inside=currentDistance<=0,wasInside=oldDistance<=0;
            if(inside!=wasInside){const double fraction=oldDistance/(oldDistance-currentDistance);
                if(next>=triangleScratch.first.size())return false;
                output[next++]={previous.point+(current.point-previous.point)*fraction,previous.bary+(current.bary-previous.bary)*fraction};}
            if(inside){if(next>=triangleScratch.first.size())return false;output[next++]=current;}
            previous=current;oldDistance=currentDistance;
        }
        count=next;if(!count)return false;std::swap(input,output);return true;
    }
};
ClothConvexContact FinishTriangleContact(const ClothWorldConvex& shape,glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,
        double padding,const ClippedTriangle& polygon,const PreparedClothConvexQueries* prepared){
    ClothConvexContact result;const auto* input=polygon.input;const auto count=polygon.count;
    // The intersection may lie strictly inside the face while all three render
    // vertices are outside. Its center is a material (barycentric) point. Also
    // inspect original face center and clipped edge midpoints so a skewed small
    // sliver cannot hide the deeper interior contact behind a boundary point.
    ClipVertex center{{0,0,0},{0,0,0}};for(std::size_t i=0;i<count;++i){center.point+=input[i].point;center.bary+=input[i].bary;}
    center.point/=double(count);center.bary/=double(count);
    const double tieTolerance=QueryTieTolerance(shape);
    auto consider=[&](const ClipVertex& candidate){auto hit=ConvexPointContact(shape,candidate.point,padding+1e-12,prepared);
        if(hit.hit&&(!result.hit||hit.separation<result.separation-tieTolerance)){hit.bary=candidate.bary;result=hit;}};
    consider(center);consider({(a+b+c)/3.0,glm::dvec3(1./3)});
    // Avoid O(P^2) work on large skin hulls: a center already inside the real
    // skin gives a useful normal. The bounded extra candidates mainly improve
    // grazing inflated contacts, where the clipped center can be asymmetric.
    if(!result.hit||result.separation>0){
        const std::size_t stride=std::max<std::size_t>(1,count/8);
        for(std::size_t i=0;i<count;i+=stride){consider(input[i]);const auto& other=input[(i+1)%count];consider({(input[i].point+other.point)*.5,(input[i].bary+other.bary)*.5});}
    }
    if(result.hit&&result.separation>padding+1e-10)result.hit=false;
    return result;
}
ClothConvexContact TriangleContactScalar(const ClothWorldConvex& shape,glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,double padding,bool* emptyClip,std::uint16_t* emptyPlane){
    ClothConvexContact result;
    if(shape.planes.empty()||shape.planes.size()>262||!ConvexBoundsOverlap(glm::min(a,glm::min(b,c)),glm::max(a,glm::max(b,c)),shape,padding))return result;
    ClippedTriangle polygon(a,b,c);
    for(std::size_t planeIndex=0;planeIndex<shape.planes.size();++planeIndex){const auto& plane=shape.planes[planeIndex];
        // Preserve original plane order, including the first separation proof.
        const double da=glm::dot(plane.normal,a)-plane.distance-padding,
            db=glm::dot(plane.normal,b)-plane.distance-padding,dc=glm::dot(plane.normal,c)-plane.distance-padding;
        if(da>0&&db>0&&dc>0){result.separatingPlane=planeIndex;return result;}
        if(da<=0&&db<=0&&dc<=0)continue;
        if(!polygon.Clip(plane,padding)){if(emptyClip)*emptyClip=polygon.count==0;if(emptyPlane&&polygon.count==0)*emptyPlane=std::uint16_t(planeIndex);return result;}
    }
    return FinishTriangleContact(shape,a,b,c,padding,polygon,nullptr);
}
#if MODEL_CLOTH_CONVEX_AVX2_IMPLEMENTED
MODEL_CLOTH_CONVEX_AVX2_INLINE __m256d PlaneDistances(const PreparedClothConvexQueries::Packet& packet,glm::dvec3 point){
    // Exactly GLM's ((nx*x + ny*y) + nz*z) - distance association. Intrinsics
    // plus no-fma keep comparisons, boundary decisions and ties bit-identical.
    return _mm256_sub_pd(_mm256_add_pd(_mm256_add_pd(
        _mm256_mul_pd(_mm256_load_pd(packet.x.data()),_mm256_set1_pd(point.x)),
        _mm256_mul_pd(_mm256_load_pd(packet.y.data()),_mm256_set1_pd(point.y))),
        _mm256_mul_pd(_mm256_load_pd(packet.z.data()),_mm256_set1_pd(point.z))),_mm256_load_pd(packet.distance.data()));
}
MODEL_CLOTH_CONVEX_AVX2 ClothConvexContact PointContactAvx2(const ClothWorldConvex& shape,glm::dvec3 point,
        double padding,const PreparedClothConvexQueries& prepared){
    ClothConvexContact result;result.point=point;
    if(!ConvexBoundsOverlap(point,point,shape,padding))return result;
    double separation=-std::numeric_limits<double>::infinity();const double tieTolerance=QueryTieTolerance(shape);
    const auto paddingPacket=_mm256_set1_pd(padding);
    for(std::size_t packet=0;packet<prepared.packets.size();++packet){const auto distances=PlaneDistances(prepared.packets[packet],point);
        const int outside=_mm256_movemask_pd(_mm256_cmp_pd(distances,paddingPacket,_CMP_GT_OQ));
        alignas(32) double values[4];_mm256_store_pd(values,distances);
        const auto count=std::min<std::size_t>(4,shape.planes.size()-packet*4);
        // Updating in original lane order preserves tolerant, non-associative
        // nearest-face ties, and the partial result of an early separation.
        for(std::size_t lane=0;lane<count;++lane){const auto index=packet*4+lane;const auto& plane=shape.planes[index];
            if(outside&(1<<lane)){result.separatingPlane=index;return result;}
            if(plane.collision&&values[lane]>separation+tieTolerance){separation=values[lane];result.normal=plane.normal;result.plane=index;}}
    }
    result.hit=std::isfinite(separation);result.separation=separation;return result;
}
MODEL_CLOTH_CONVEX_AVX2 ClothConvexContact TriangleContactAvx2(const ClothWorldConvex& shape,glm::dvec3 a,glm::dvec3 b,
        glm::dvec3 c,double padding,const PreparedClothConvexQueries& prepared,bool* emptyClip,std::uint16_t* emptyPlane){
    ClothConvexContact result;
    if(!ConvexBoundsOverlap(glm::min(a,glm::min(b,c)),glm::max(a,glm::max(b,c)),shape,padding))return result;
    ClippedTriangle polygon(a,b,c);const auto paddingPacket=_mm256_set1_pd(padding),zero=_mm256_setzero_pd();
    // Dispatch is once per complete query. There is no per-packet ISA boundary
    // or vzeroupper call; only genuinely partial planes reach scalar clipping.
    for(std::size_t packet=0;packet<prepared.packets.size();++packet){const auto& data=prepared.packets[packet];
        const auto da=_mm256_sub_pd(PlaneDistances(data,a),paddingPacket),
            db=_mm256_sub_pd(PlaneDistances(data,b),paddingPacket),dc=_mm256_sub_pd(PlaneDistances(data,c),paddingPacket);
        const int inside=_mm256_movemask_pd(_mm256_and_pd(_mm256_and_pd(_mm256_cmp_pd(da,zero,_CMP_LE_OQ),
            _mm256_cmp_pd(db,zero,_CMP_LE_OQ)),_mm256_cmp_pd(dc,zero,_CMP_LE_OQ)));
        if(inside==15)continue;
        const int outside=_mm256_movemask_pd(_mm256_and_pd(_mm256_and_pd(_mm256_cmp_pd(da,zero,_CMP_GT_OQ),
            _mm256_cmp_pd(db,zero,_CMP_GT_OQ)),_mm256_cmp_pd(dc,zero,_CMP_GT_OQ)));
        const auto count=std::min<std::size_t>(4,shape.planes.size()-packet*4);
        for(std::size_t lane=0;lane<count;++lane){const auto index=packet*4+lane;
            // A preceding partial plane can empty the composite polygon. It
            // must run before a later single-plane proof, even in one packet.
            if(outside&(1<<lane)){result.separatingPlane=index;return result;}
            if(inside&(1<<lane))continue;
            if(!polygon.Clip(shape.planes[index],padding)){if(emptyClip)*emptyClip=polygon.count==0;if(emptyPlane&&polygon.count==0)*emptyPlane=std::uint16_t(index);return result;}
        }
    }
    return FinishTriangleContact(shape,a,b,c,padding,polygon,&prepared);
}
#endif
} // namespace
ClothConvexContact ConvexPointContact(const ClothWorldConvex& shape,glm::dvec3 point,double padding,
        const PreparedClothConvexQueries* prepared){
#if MODEL_CLOTH_CONVEX_AVX2_IMPLEMENTED
    if(prepared&&prepared->Matches(shape))return PointContactAvx2(shape,point,padding,*prepared);
#endif
    return PointContactScalar(shape,point,padding);
}
ClothConvexContact ConvexTriangleContact(const ClothWorldConvex& shape,glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,
        double padding,const PreparedClothConvexQueries* prepared,bool* emptyClip,std::uint16_t* emptyPlane){
    if(emptyClip)*emptyClip=false;
    if(emptyPlane)*emptyPlane=std::numeric_limits<std::uint16_t>::max();
#if MODEL_CLOTH_CONVEX_AVX2_IMPLEMENTED
    if(prepared&&prepared->Matches(shape))return TriangleContactAvx2(shape,a,b,c,padding,*prepared,emptyClip,emptyPlane);
#endif
    return TriangleContactScalar(shape,a,b,c,padding,emptyClip,emptyPlane);
}
double ConvexKinematicContactOffset(const ClothWorldConvex& shape,glm::dvec3 target,double padding,
        const PreparedClothConvexQueries* prepared){
    const auto contact=ConvexPointContact(shape,target,padding,prepared);
    return contact.hit?std::min(padding,contact.separation):padding;
}
ClothConvexContact ConvexTriangleContactOffsets(const ClothWorldConvex& shape,glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,
        glm::dvec3 offsets,double padding,const PreparedClothConvexQueries* prepared,bool* emptyClip,std::uint16_t* emptyPlane){
    if(emptyClip)*emptyClip=false;
    if(emptyPlane)*emptyPlane=std::numeric_limits<std::uint16_t>::max();
    if(offsets.x==padding&&offsets.y==padding&&offsets.z==padding)return ConvexTriangleContact(shape,a,b,c,padding,prepared,emptyClip,emptyPlane);
    ClothConvexContact result;
    if(!Finite(offsets)||offsets.x>padding||offsets.y>padding||offsets.z>padding||shape.planes.empty()||shape.planes.size()>262||
        !ConvexBoundsOverlap(glm::min(a,glm::min(b,c)),glm::max(a,glm::max(b,c)),shape,padding))return result;
    ClippedTriangle polygon(a,b,c);
    for(std::size_t index=0;index<shape.planes.size();++index){const auto& plane=shape.planes[index];
        const double rawA=glm::dot(plane.normal,a)-plane.distance,rawB=glm::dot(plane.normal,b)-plane.distance,rawC=glm::dot(plane.normal,c)-plane.distance;
        const double da=rawA-(plane.collision?offsets.x:padding),db=rawB-(plane.collision?offsets.y:padding),dc=rawC-(plane.collision?offsets.z:padding);
        if(da>0&&db>0&&dc>0){
            // Only the GLOBAL margin is a reusable separation-cache proof.
            // The narrower, pin-dependent rejection itself is not such proof.
            if(rawA-padding>0&&rawB-padding>0&&rawC-padding>0)result.separatingPlane=index;
            return result;
        }
        if(da<=0&&db<=0&&dc<=0)continue;
        if(!polygon.ClipOffsets(plane,offsets,padding)){if(emptyClip)*emptyClip=polygon.count==0;if(emptyPlane&&polygon.count==0)*emptyPlane=std::uint16_t(index);return result;}
    }
    const auto* input=polygon.input;const auto count=polygon.count;const double tolerance=QueryTieTolerance(shape);
    double bestGap=std::numeric_limits<double>::infinity();
    auto consider=[&](const ClipVertex& candidate){auto hit=ConvexPointContact(shape,candidate.point,padding+1e-12,prepared);
        const double gap=hit.separation-ConvexInterpolatedContactOffset(offsets,candidate.bary);
        if(hit.hit&&gap<=1e-12&&(!result.hit||gap<bestGap-tolerance)){hit.bary=candidate.bary;result=hit;bestGap=gap;}};
    ClipVertex center{{0,0,0},{0,0,0}};for(std::size_t i=0;i<count;++i){center.point+=input[i].point;center.bary+=input[i].bary;}
    center.point/=double(count);center.bary/=double(count);
    consider(center);consider({(a+b+c)/3.0,glm::dvec3(1./3)});
    // A raw-deep fixed corner has zero effective penetration; it must not
    // eclipse a genuinely penetrating free/material point in this polygon.
    if(!result.hit||bestGap>=-1e-12){const std::size_t stride=std::max<std::size_t>(1,count/8);
        for(std::size_t i=0;i<count;i+=stride){consider(input[i]);const auto& other=input[(i+1)%count];
            consider({(input[i].point+other.point)*.5,(input[i].bary+other.bary)*.5});}}
    return result;
}
namespace {
struct GuardedHalfspace {double value,error;};
GuardedHalfspace CompositeDistance(const ClothWorldConvex& shape,const ClothConvexPlane& plane,glm::dvec3 point,double padding){
    const double value=glm::dot(plane.normal,point)-plane.distance-padding;
    const double magnitude=std::abs(plane.normal.x*point.x)+std::abs(plane.normal.y*point.y)+
        std::abs(plane.normal.z*point.z)+std::abs(plane.distance)+std::abs(padding);
    // More conservative than the dot's gamma(6) error: allow accumulation of
    // convex-interpolation roundoff through every preceding clip as well. The
    // absolute-term bound remains safe under cancellation at 1e7 coordinates.
    // The absolute floor also covers the point-query's 1e-12 margin tolerance.
    const double error=64*double(shape.planes.size()+4)*std::numeric_limits<double>::epsilon()*magnitude+4e-12;
    return {value,error};
}
bool CompositeVertexSeparates(GuardedHalfspace first,GuardedHalfspace second,double alpha){
    const double beta=1-alpha,one=alpha*first.value,two=beta*second.value,combined=one+two;
    const double error=alpha*first.error+beta*second.error+
        64*std::numeric_limits<double>::epsilon()*(std::abs(one)+std::abs(two))+4e-12;
    return std::isfinite(combined)&&std::isfinite(error)&&combined>error;
}
}
bool ConvexCompositeSeparates(const ClothWorldConvex& shape,const ClothConvexCompositeSeparator& proof,
        glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,double padding){
    if(proof.first>=shape.planes.size()||proof.second>=shape.planes.size()||proof.first==proof.second||
        !std::isfinite(proof.alpha)||proof.alpha<0||proof.alpha>1||!std::isfinite(padding)||padding<0)return false;
    const auto& first=shape.planes[proof.first];const auto& second=shape.planes[proof.second];
    for(const auto point:{a,b,c})if(!CompositeVertexSeparates(CompositeDistance(shape,first,point,padding),
        CompositeDistance(shape,second,point,padding),proof.alpha))return false;
    return true;
}
bool BuildConvexCompositeSeparator(const ClothWorldConvex& shape,std::uint16_t emptyPlane,
        glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,double padding,ClothConvexCompositeSeparator& proof){
    proof={};if(emptyPlane>=shape.planes.size()||emptyPlane==0||!std::isfinite(padding)||padding<0)return false;
    const glm::dvec3 points[3]={a,b,c};GuardedHalfspace first[3];bool firstPartial=false;
    for(int v=0;v<3;++v){first[v]=CompositeDistance(shape,shape.planes[emptyPlane],points[v],padding);
        firstPartial|=first[v].value>first[v].error;}
    if(!firstPartial)return false; // mixed-only rejection cannot certify the full global-margin hull
    for(std::uint16_t previous=0;previous<emptyPlane;++previous){GuardedHalfspace second[3];bool partial=false;
        for(int v=0;v<3;++v){second[v]=CompositeDistance(shape,shape.planes[previous],points[v],padding);partial|=second[v].value>second[v].error;}
        if(!partial)continue;
        double lower=0,upper=1;bool feasible=true;
        for(int v=0;v<3&&feasible;++v){const double slope=first[v].value-second[v].value;
            const double threshold=2*(first[v].error+second[v].error)+
                128*std::numeric_limits<double>::epsilon()*(std::abs(first[v].value)+std::abs(second[v].value))+8e-12;
            if(!std::isfinite(slope)||!std::isfinite(threshold)){feasible=false;break;}
            if(slope>0)lower=std::max(lower,(threshold-second[v].value)/slope);
            else if(slope<0)upper=std::min(upper,(threshold-second[v].value)/slope);
            else if(second[v].value<=threshold)feasible=false;
            feasible=feasible&&lower<upper;
        }
        if(!feasible)continue;
        ClothConvexCompositeSeparator candidate{emptyPlane,previous,std::clamp(lower+(upper-lower)*.5,0.,1.)};
        // Interval arithmetic proposes a weight; only a fresh guarded check on
        // the ORIGINAL vertices accepts it. No rounded synthetic plane or
        // negative material offset is ever used as a certificate.
        if(ConvexCompositeSeparates(shape,candidate,a,b,c,padding)){proof=candidate;return true;}
    }
    return false;
}
double ConvexInteriorRadius(const ClothWorldConvex& shape){
    if(shape.vertices.empty()||shape.planes.empty())return 0;
    glm::dvec3 center(0);for(const auto& vertex:shape.vertices)center+=vertex;center/=double(shape.vertices.size());
    double radius=std::numeric_limits<double>::infinity();for(const auto& plane:shape.planes)radius=std::min(radius,plane.distance-glm::dot(plane.normal,center));
    return std::max(0.0,radius);
}
bool PreparedClothConvexMotion::Prepare(const ClothConvexCollider& collider,const glm::dmat4& from,const glm::dmat4& to,const ClothConvexRuntimeLayout* preparedLayout){
    source=&collider;begin=from;end=to;changed=from!=to;travel=0;worldEndpoints=false;localBegin.clear();localEnd.clear();
    layout=preparedLayout?*preparedLayout:CompileClothConvexLayout(collider,ClothConvexPlaneBudget());
    if(!Affine(from)||!Affine(to)||collider.vertices.empty())return false;
    glm::dvec4 perspectiveA,perspectiveB;
    if(!glm::decompose(from,scaleA,rotationA,translationA,skewA,perspectiveA)||
       !glm::decompose(to,scaleB,rotationB,translationB,skewB,perspectiveB))return false;
    const double normA=glm::dot(rotationA,rotationA),normB=glm::dot(rotationB,rotationB);
    if(!std::isfinite(normA)||!std::isfinite(normB)||normA<1e-20||normB<1e-20)return false;
    rotationA/=std::sqrt(normA);rotationB/=std::sqrt(normB);if(glm::dot(rotationA,rotationB)<0)rotationB=-rotationB;
    double localRadius=0;for(const auto& vertex:collider.vertices)localRadius=std::max(localRadius,Length(vertex));
    UpdateBounds(localRadius,0);
    return Finite(minimum)&&Finite(maximum)&&std::isfinite(travel);
}
void PreparedClothConvexMotion::UpdateBounds(double localRadius,double deformationTravel){
    const auto scaleMaximum=glm::max(glm::abs(scaleA),glm::abs(scaleB)),skewMaximum=glm::max(glm::abs(skewA),glm::abs(skewB));
    // Frobenius bound for shear*scale over the whole interval, including the
    // cross term of linearly interpolated shear and scale.
    const double frobenius=std::sqrt(scaleMaximum.x*scaleMaximum.x+
        scaleMaximum.y*scaleMaximum.y*(1+skewMaximum.z*skewMaximum.z)+
        scaleMaximum.z*scaleMaximum.z*(1+skewMaximum.x*skewMaximum.x+skewMaximum.y*skewMaximum.y));
    const double radius=localRadius*frobenius;minimum=glm::min(translationA,translationB)-glm::dvec3(radius);maximum=glm::max(translationA,translationB)+glm::dvec3(radius);
    if(changed){const double angle=2*std::acos(std::clamp(glm::dot(rotationA,rotationB),0.0,1.0));
        const auto scaleDelta=glm::abs(scaleB-scaleA),skewDelta=glm::abs(skewB-skewA);
        const double stretchTravel=Length(scaleDelta)*(1+Length(skewMaximum))+Length(skewDelta)*Length(scaleMaximum);
        travel=Length(translationB-translationA)+radius*angle+localRadius*stretchTravel+deformationTravel*frobenius;}
}
bool PreparedClothConvexMotion::Prepare(const ClothConvexCollider& collider,const ClothWorldConvex& from,const ClothWorldConvex& to,const ClothConvexRuntimeLayout* preparedLayout){
    if(!Prepare(collider,from.transform,to.transform,preparedLayout))return false;
    if(collider.skin.empty())return true;
    if(from.vertices.size()!=collider.vertices.size()||to.vertices.size()!=collider.vertices.size())return false;
    worldEndpoints=true;beginWorld=from;endWorld=to;
    localBegin.resize(from.vertices.size());localEnd.resize(to.vertices.size());
    const auto inverseBegin=glm::inverse(from.transform),inverseEnd=glm::inverse(to.transform);
    glm::dvec3 maximumAbsolute(0);double deformationTravel=0;
    for(std::size_t i=0;i<localBegin.size();++i){localBegin[i]=glm::dvec3(inverseBegin*glm::dvec4(from.vertices[i],1));localEnd[i]=glm::dvec3(inverseEnd*glm::dvec4(to.vertices[i],1));
        if(!Finite(localBegin[i])||!Finite(localEnd[i]))return false;
        const double distance=Length(localEnd[i]-localBegin[i]);changed|=distance>1e-12*std::max(1.0,Length(localBegin[i]));
        maximumAbsolute=glm::max(maximumAbsolute,glm::max(glm::abs(localBegin[i]),glm::abs(localEnd[i])));deformationTravel=std::max(deformationTravel,distance);}
    UpdateBounds(Length(maximumAbsolute),deformationTravel*std::sqrt(3.0));
    return Finite(minimum)&&Finite(maximum)&&std::isfinite(travel);
}
bool PreparedClothConvexMotion::At(double fraction,ClothWorldConvex& output)const{
    if(!source)return false;
    if(fraction<=0){if(worldEndpoints){output=beginWorld;return true;}return BuildClothWorldConvex(*source,begin,output,&layout);}
    if(fraction>=1){if(worldEndpoints){output=endWorld;return true;}return BuildClothWorldConvex(*source,end,output,&layout);}
    if(!changed){if(worldEndpoints){output=beginWorld;return true;}return BuildClothWorldConvex(*source,begin,output,&layout);}
    const auto rotation=glm::normalize(glm::slerp(rotationA,rotationB,fraction));
    const auto transform=glm::translate(glm::dmat4(1),glm::mix(translationA,translationB,fraction))*glm::mat4_cast(rotation)*
        Stretch(glm::mix(scaleA,scaleB,fraction),glm::mix(skewA,skewB,fraction));
    if(!worldEndpoints)return BuildClothWorldConvex(*source,transform,output,&layout);
    output.transform=transform;output.vertices.resize(localBegin.size());
    for(std::size_t i=0;i<localBegin.size();++i)output.vertices[i]=glm::dvec3(transform*glm::dvec4(glm::mix(localBegin[i],localEnd[i],fraction),1));
    return RefitClothWorldConvex(*source,output,&layout);
}
} // namespace Model::detail

namespace Model {
std::vector<ClothWorldConvex> EvaluateClothConvexes(const ClothAsset& cloth,const SkeletalPose& pose){
    std::vector<ClothWorldConvex> result;result.reserve(cloth.convexes.size());
    for(const auto& source:cloth.convexes){if(source.bone< -1||(source.bone>=0&&std::size_t(source.bone)>=pose.global.size()))continue;
        ClothWorldConvex shape;const auto transform=source.bone<0?glm::dmat4(1):pose.global[source.bone];
        bool valid=true;
        if(source.skin.empty())valid=detail::BuildClothWorldConvex(source,transform,shape);
        else {
            if(source.skin.size()!=source.vertices.size())continue;
            if(!detail::Affine(transform))continue;shape.transform=transform;shape.vertices.resize(source.vertices.size());
            for(std::size_t i=0;i<source.skin.size()&&valid;++i){glm::dvec3 point(0);double weight=0;
                for(const auto& influence:source.skin[i]){
                    if(influence.bone< -1||(influence.bone>=0&&std::size_t(influence.bone)>=pose.global.size())||!std::isfinite(influence.weight)||influence.weight<0){valid=false;break;}
                    const auto bone=influence.bone<0?glm::dmat4(1):pose.global[influence.bone];
                    if(!detail::Affine(bone)){valid=false;break;}
                    point+=glm::dvec3(bone*glm::dvec4(influence.weightedPosition,influence.weight));weight+=influence.weight;}
                if(!valid||std::abs(weight-1)>1e-5){valid=false;break;}shape.vertices[i]=point;
            }
            if(valid)valid=detail::RefitClothWorldConvex(source,shape);
        }
        if(valid)result.push_back(std::move(shape));}
    return result;
}
} // namespace Model
