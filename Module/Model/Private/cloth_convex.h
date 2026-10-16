#pragma once
#include "Model/Public/cloth_asset.h"
#include <array>
#include <limits>
#include <span>
#include <glm/gtc/quaternion.hpp>

namespace Model::detail {

// All queries use the units of the supplied world shape. Normals remain unit
// length after affine transforms; padding and separation are distances. All
// planes define membership, but only collision planes supply outward contacts.
struct ClothConvexRuntimeLayout {
    std::vector<std::size_t> sourcePlanes;
    std::uint32_t budget=0; // external faces; 0 keeps every source face
};
std::uint32_t ClothConvexPlaneBudget(); // MODEL_CLOTH_CONVEX_PLANES; default 96, 0 = full reference
ClothConvexRuntimeLayout CompileClothConvexLayout(const ClothConvexCollider& source,std::uint32_t budget);
bool BuildClothWorldConvex(const ClothConvexCollider& source,const glm::dmat4& transform,ClothWorldConvex& output,
    const ClothConvexRuntimeLayout* layout=nullptr);
// Refit supporting planes to a deformed support cloud already in world space.
// Source triangles and planes must correspond. Interior support vertices need
// not belong to the neutral hull surface; every vertex participates in support.
bool RefitClothWorldConvex(const ClothConvexCollider& source,ClothWorldConvex& output,
    const ClothConvexRuntimeLayout* layout=nullptr);
struct ClothConvexContact {
    bool hit=false;
    glm::dvec3 point{0},bary{1,0,0},normal{0,1,0};
    double separation=std::numeric_limits<double>::infinity();
    std::size_t plane=0;
    // Only a proof that the ORIGINAL query point / all three original triangle
    // vertices lie outside this one plane. An empty composite clip supplies no
    // such proof. This index is never a collision/escape plane.
    std::size_t separatingPlane=std::numeric_limits<std::size_t>::max();
};
// Immutable query data for a substep endpoint or shared CCD sample, reused by
// all graph colors. Re-prepare after any shape relocation or plane overwrite.
// Local adaptive samples may omit it for scalar queries. No target-wide ISA flag.
struct PreparedClothConvexQueries {
    struct alignas(32) Packet {std::array<double,4> x,y,z,distance;};
    const ClothWorldConvex* owner=nullptr;
    const ClothConvexPlane* planes=nullptr;
    std::size_t planeCount=0;
    bool simd=false;
    std::vector<Packet> packets;
    void Prepare(const ClothWorldConvex& shape);
    bool Matches(const ClothWorldConvex& shape)const{
        return simd&&owner==&shape&&planes==shape.planes.data()&&planeCount==shape.planes.size();
    }
};
// MODEL_CLOTH_CONVEX_SIMD=0 disables this kernel; CPU and OS must support AVX2.
bool ClothConvexSimdAvailable();
ClothConvexContact ConvexPointContact(const ClothWorldConvex& shape,glm::dvec3 point,double padding=0,
    const PreparedClothConvexQueries* prepared=nullptr);
// Optional profiling output distinguishes actual empty composite clips from
// single-plane/AABB rejection or a failed final candidate. No proof search.
ClothConvexContact ConvexTriangleContact(const ClothWorldConvex& shape,
    glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,double padding=0,
    const PreparedClothConvexQueries* prepared=nullptr,bool* emptyClip=nullptr,std::uint16_t* emptyPlane=nullptr);
// A fixed skinned seam can lie inside the source body. Its own authored depth
// is permitted only at that material vertex; free vertices retain padding.
double ConvexKinematicContactOffset(const ClothWorldConvex& shape,glm::dvec3 target,double padding,
    const PreparedClothConvexQueries* prepared=nullptr);
inline double ConvexInterpolatedContactOffset(glm::dvec3 offsets,glm::dvec3 bary){
    // Preserve the original constant margin bit-for-bit, independently of the
    // clipped barycentrics' final summation roundoff.
    return offsets.x==offsets.y&&offsets.x==offsets.z?offsets.x:glm::dot(offsets,bary);
}
// Vertex offsets interpolate in material barycentrics throughout clipping and
// candidate selection. Artificial partition faces keep global padding. The
// returned separation remains RAW skin phi; effective gap = phi-dot(offsets,bary).
// Uniform offsets==padding dispatch to the unchanged SIMD/scalar fast path.
ClothConvexContact ConvexTriangleContactOffsets(const ClothWorldConvex& shape,
    glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,glm::dvec3 offsets,double padding,
    const PreparedClothConvexQueries* prepared=nullptr,bool* emptyClip=nullptr,std::uint16_t* emptyPlane=nullptr);
// A nonnegative combination of two ORIGINAL global-margin halfspaces. It is
// never a contact normal or a single-plane proof. Always revalidate all three
// current vertices; a previous body projection can invalidate the certificate.
struct ClothConvexCompositeSeparator {
    std::uint16_t first=std::numeric_limits<std::uint16_t>::max(),second=std::numeric_limits<std::uint16_t>::max();
    double alpha=0;
};
bool ConvexCompositeSeparates(const ClothWorldConvex& shape,const ClothConvexCompositeSeparator& proof,
    glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,double padding);
bool BuildConvexCompositeSeparator(const ClothWorldConvex& shape,std::uint16_t emptyPlane,
    glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,double padding,ClothConvexCompositeSeparator& proof);
double ConvexInteriorRadius(const ClothWorldConvex& shape);
inline bool ConvexBoundsOverlap(glm::dvec3 minimum,glm::dvec3 maximum,
        const ClothWorldConvex& shape,double padding=0){
    return minimum.x<=shape.maximum.x+padding&&maximum.x>=shape.minimum.x-padding&&
        minimum.y<=shape.maximum.y+padding&&maximum.y>=shape.minimum.y-padding&&
        minimum.z<=shape.maximum.z+padding&&maximum.z>=shape.minimum.z-padding;
}
// An envelope of ONE immutable discrete CCD grid, not a continuous-motion
// bound. Include the independently queried start and end, even for a stationary
// Sample(k)==end. A caller with a different adaptive grid must ignore it.
struct ClothConvexSampleBounds {
    glm::dvec3 minimum{0},maximum{0};bool ready=false;
    void Prepare(const ClothWorldConvex& start,const ClothWorldConvex& end,std::span<const ClothWorldConvex> samples){
        ready=false;minimum=glm::min(start.minimum,end.minimum);maximum=glm::max(start.maximum,end.maximum);
        for(const auto& sample:samples){minimum=glm::min(minimum,sample.minimum);maximum=glm::max(maximum,sample.maximum);}
        ready=true;
    }
    bool Overlaps(glm::dvec3 from,glm::dvec3 to,double padding)const{
        return !ready||(from.x<=maximum.x+padding&&to.x>=minimum.x-padding&&
            from.y<=maximum.y+padding&&to.y>=minimum.y-padding&&from.z<=maximum.z+padding&&to.z>=minimum.z-padding);
    }
};

// Decompose once per input interval. Sampling preserves rigid rotations instead
// of shortening a limb along a straight endpoint-vertex chord. The conservative
// translation/sphere bounds cover every intermediate orientation, including a
// half turn whose endpoints alone have identical or disjoint narrow AABBs.
struct PreparedClothConvexMotion {
    const ClothConvexCollider* source=nullptr;
    glm::dmat4 begin{1},end{1};
    glm::dvec3 translationA{0},translationB{0},scaleA{1},scaleB{1},skewA{0},skewB{0};
    glm::dquat rotationA{1,0,0,0},rotationB{1,0,0,0};
    glm::dvec3 minimum{0},maximum{0};
    double travel=0;
    bool changed=false,worldEndpoints=false;
    ClothWorldConvex beginWorld,endWorld;
    ClothConvexRuntimeLayout layout;
    std::vector<glm::dvec3> localBegin,localEnd;
    bool Prepare(const ClothConvexCollider& collider,const glm::dmat4& from,const glm::dmat4& to,const ClothConvexRuntimeLayout* preparedLayout=nullptr);
    bool Prepare(const ClothConvexCollider& collider,const ClothWorldConvex& from,const ClothWorldConvex& to,const ClothConvexRuntimeLayout* preparedLayout=nullptr);
    bool At(double fraction,ClothWorldConvex& output)const;
    void UpdateBounds(double localRadius,double deformationTravel);
};

} // namespace Model::detail
