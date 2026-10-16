#include "Model/Private/hair_body_collision.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <unordered_map>

namespace Model::detail {
namespace {
constexpr double Epsilon = 1e-24;
bool Finite(const glm::dvec3& p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
struct Box {
    glm::dvec3 lo{std::numeric_limits<double>::infinity()};
    glm::dvec3 hi{-std::numeric_limits<double>::infinity()};
    void Include(const glm::dvec3& p) { lo=glm::min(lo,p);hi=glm::max(hi,p); }
    void Include(const Box& b) { lo=glm::min(lo,b.lo);hi=glm::max(hi,b.hi); }
};
double BoxDistance2(const Box& b,const glm::dvec3& p) {
    const auto d=glm::max(glm::max(b.lo-p,p-b.hi),glm::dvec3(0));return glm::dot(d,d);
}
bool Overlap(const Box& a,const Box& b) {
    return a.lo.x<=b.hi.x&&a.hi.x>=b.lo.x&&a.lo.y<=b.hi.y&&a.hi.y>=b.lo.y&&a.lo.z<=b.hi.z&&a.hi.z>=b.lo.z;
}
constexpr std::size_t DopDirectionCount=49;
const std::array<glm::dvec3,DopDirectionCount>& DopDirections() {
    static const auto directions=[] {std::array<glm::dvec3,DopDirectionCount> result;std::size_t count=0;
        for(int x=-2;x<=2;++x)for(int y=-2;y<=2;++y)for(int z=-2;z<=2;++z){if(x<0||(x==0&&y<0)||(x==0&&y==0&&z<=0))continue;if(std::gcd(std::gcd(std::abs(x),std::abs(y)),std::abs(z))!=1)continue;result[count++]=glm::dvec3(x,y,z);}
        std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){const auto aa=glm::dot(a,a),bb=glm::dot(b,b);if(aa!=bb)return aa<bb;if(a.x!=b.x)return a.x<b.x;if(a.y!=b.y)return a.y<b.y;return a.z<b.z;});return result;
    }();return directions;
}
const std::array<glm::dvec3,3>& InsideDirections() {
    static const std::array<glm::dvec3,3> directions{{glm::normalize(glm::dvec3(1,.371,.529)),glm::normalize(glm::dvec3(.217,1,.413)),glm::normalize(glm::dvec3(.319,.271,1))}};return directions;
}
glm::dvec3 ClosestSegment(const glm::dvec3& p,const glm::dvec3& a,const glm::dvec3& b,double* t=nullptr) {
    const auto d=b-a;const double l=glm::dot(d,d),s=l>Epsilon?std::clamp(glm::dot(p-a,d)/l,0.,1.):0.;
    if(t)*t=s;return a+d*s;
}
glm::dvec3 ClosestTriangle(const glm::dvec3& p,const glm::dvec3& a,const glm::dvec3& b,const glm::dvec3& c,glm::dvec3* bary=nullptr) {
    const auto ab=b-a,ac=c-a;const auto n=glm::cross(ab,ac);
    if(glm::dot(n,n)<=Epsilon) {
        double t;auto q=ClosestSegment(p,a,b,&t);glm::dvec3 w(1-t,t,0);double d=glm::dot(p-q,p-q);
        const auto q2=ClosestSegment(p,b,c,&t);const double d2=glm::dot(p-q2,p-q2);if(d2<d){q=q2;d=d2;w={0,1-t,t};}
        const auto q3=ClosestSegment(p,c,a,&t);if(glm::dot(p-q3,p-q3)<d){q=q3;w={t,0,1-t};}
        if(bary)*bary=w;return q;
    }
    const auto ap=p-a;const double d1=glm::dot(ab,ap),d2=glm::dot(ac,ap);
    if(d1<=0&&d2<=0){if(bary)*bary={1,0,0};return a;}
    const auto bp=p-b;const double d3=glm::dot(ab,bp),d4=glm::dot(ac,bp);
    if(d3>=0&&d4<=d3){if(bary)*bary={0,1,0};return b;}
    const double vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0){const double v=d1/(d1-d3);if(bary)*bary={1-v,v,0};return a+v*ab;}
    const auto cp=p-c;const double d5=glm::dot(ab,cp),d6=glm::dot(ac,cp);
    if(d6>=0&&d5<=d6){if(bary)*bary={0,0,1};return c;}
    const double vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0){const double w=d2/(d2-d6);if(bary)*bary={1-w,0,w};return a+w*ac;}
    const double va=d3*d6-d5*d4;
    if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0){const double w=(d4-d3)/((d4-d3)+(d5-d6));if(bary)*bary={0,1-w,w};return b+w*(c-b);}
    const double inv=1/(va+vb+vc),v=vb*inv,w=vc*inv;if(bary)*bary={1-v-w,v,w};return a+ab*v+ac*w;
}
std::string Upper(std::string value) { for(auto& c:value) if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');return value; }
// Isolate roots on monotonic intervals. This avoids a fragile analytic cubic
// formula at almost-static poses and retains double precision for thin faces.
std::size_t UnitRoots(std::array<double,4> coefficients,std::array<double,8>& roots) {
    double scale=0;for(double c:coefficients)scale=std::max(scale,std::abs(c));if(scale<1e-30)return 0;
    for(auto& c:coefficients)c/=scale;
    auto value=[&](double t){return ((coefficients[3]*t+coefficients[2])*t+coefficients[1])*t+coefficients[0];};
    std::array<double,4> split{0,1};std::size_t splitCount=2;
    const double a=3*coefficients[3],b=2*coefficients[2],c=coefficients[1];
    auto addSplit=[&](double t){if(t>0&&t<1&&std::isfinite(t))split[splitCount++]=t;};
    if(std::abs(a)<1e-15){if(std::abs(b)>1e-15)addSplit(-c/b);}
    else {const double discriminant=b*b-4*a*c;if(discriminant>=0){const double s=std::sqrt(discriminant),q=-.5*(b+std::copysign(s,b));if(std::abs(q)>1e-30){addSplit(q/a);addSplit(c/q);}else addSplit(-b/(2*a));}}
    std::sort(split.begin(),split.begin()+splitCount);std::size_t count=0;
    auto addRoot=[&](double t){for(std::size_t i=0;i<count;++i)if(std::abs(roots[i]-t)<1e-9)return;roots[count++]=t;};
    for(std::size_t i=0;i<splitCount;++i)if(std::abs(value(split[i]))<=1e-13)addRoot(split[i]);
    for(std::size_t i=1;i<splitCount;++i){double lo=split[i-1],hi=split[i],fl=value(lo),fh=value(hi);if((fl<0)==(fh<0)||fl==0||fh==0)continue;
        for(int iteration=0;iteration<48;++iteration){const double mid=(lo+hi)*.5,fm=value(mid);if((fl<0)==(fm<0)){lo=mid;fl=fm;}else hi=mid;}
        addRoot((lo+hi)*.5);
    }
    std::sort(roots.begin(),roots.begin()+count);return count;
}
}

struct HairBodyCollision::Impl {
    struct Influence { std::uint32_t bone=0;glm::dvec3 weightedPosition{0};double weight=0;glm::dmat4 offset{1}; };
    struct Vertex { glm::dvec3 fallback{0};std::array<Influence,4> influences;std::uint8_t count=0;std::uint32_t instance=0; };
    struct Morph { std::uint32_t vertex=0,morph=0;glm::dvec3 delta{0}; };
    struct Triangle { std::array<std::uint32_t,3> v{};std::uint32_t mesh=0,component=0;bool flip=false;glm::dvec3 normal{0,1,0};Box box,swept; };
    struct Component { bool closed=false,reliableOutward=false; };
    struct ComponentDop {std::array<double,DopDirectionCount> lo,hi;};
    struct RayGrid {
        static constexpr std::uint32_t MaximumEntries=1u<<20;
        std::uint32_t side=256;
        bool valid=false;int main=0,first=1,second=2;
        glm::dvec2 ratios{0},lo{0},hi{0},inverseCell{0};
        std::vector<std::uint32_t> offsets,entries,cursor;
        std::vector<std::array<std::uint8_t,4>> triangleCells;
        std::vector<glm::dvec2> projectedVertices;
        glm::dvec2 Project(const glm::dvec3& p)const{return {p[first]-ratios.x*p[main],p[second]-ratios.y*p[main]};}
        std::uint32_t Cell(double value,int axis)const{return static_cast<std::uint32_t>(std::clamp(std::floor((value-lo[axis])*inverseCell[axis]),0.,double(side-1)));}
    };
    struct FixedRayCoefficient {glm::dvec3 cross{0};double determinant=0,facing=0;};
    struct FixedRayTriangle {
        glm::dvec3 a{0},e{0},f{0},plane{0},numeratorBound{0};
        std::array<std::array<FixedRayCoefficient,2>,3> rays;
    };
    // Current bounds and traversal metadata fit one cache line. CCD-only
    // swept bounds live separately and do not pollute nearest/inside walks.
    struct alignas(64) Node {
        Box box;std::uint32_t first=0,second=0;std::uint64_t componentMask=0;
        static constexpr std::uint32_t LeafFlag=0x80000000u;
        bool IsLeaf()const{return (second&LeafFlag)!=0;}
        std::uint32_t Begin()const{return first;}
        std::uint32_t Count()const{return second&~LeafFlag;}
        std::uint32_t Left()const{return first;}
        std::uint32_t Right()const{return second;}
        void SetLeaf(std::uint32_t begin,std::uint32_t count){first=begin;second=LeafFlag|count;}
        void SetChildren(std::uint32_t left,std::uint32_t right){first=left;second=right;}
    };
    static_assert(sizeof(Node)==64&&alignof(Node)==64,"Hair body hot BVH nodes must fit one cache line");
    double units=1,updateMilliseconds=0;std::size_t boneCount=0;
    std::uint32_t rayGridSide=256;
    std::uint64_t geometryRevision=0;
    std::vector<Vertex> vertices;std::vector<Morph> morphs;
    std::vector<glm::dmat4> instanceFromSkeleton,instanceTransforms;
    std::vector<glm::dvec3> previous,current,morphed;
    std::vector<Triangle> triangles;std::vector<std::uint32_t> order;std::vector<Node> nodes;
    std::vector<Box> sweptBounds;
    std::vector<Component> components;
    std::vector<Box> componentBounds;
    std::vector<std::vector<std::uint32_t>> componentVertices;
    std::vector<ComponentDop> componentDops;
    std::vector<Node> componentNodes;
    std::vector<std::uint32_t> componentOrder;
    mutable std::vector<std::uint32_t> nearbyComponents;
    std::array<RayGrid,3> rayGrids;
    std::vector<std::uint32_t> rayTraversalOrder;
    std::vector<FixedRayTriangle> fixedRayTriangles;
    std::uint64_t fixedRayRevision=0;
    mutable std::vector<std::uint32_t> componentHints;
    mutable std::vector<std::uint32_t> componentBackfaceHints;
    mutable std::uint32_t lastHint=UINT32_MAX;
    mutable std::uint32_t lastSurfaceHint=UINT32_MAX;
    mutable std::uint32_t lastInsideHint=UINT32_MAX;
    bool useComponentMask=true,useHints=true,usePairedRays=true,auditRays=false,useComponentBackfaceGate=true,useComponentBvh=true,useComponentDop=true,useRayGrid=true,useFastRays=true,useFastRayPlanes=false,useInsideOnly=true;
    mutable HairBodyQueryStats stats;
    bool updated=false;
    void BuildComponents() {
        struct Key {std::int64_t x=0,y=0,z=0;bool operator==(const Key&)const=default;};
        struct Hash {std::size_t operator()(const Key& k)const {std::size_t h=std::hash<std::int64_t>{}(k.x);h^=std::hash<std::int64_t>{}(k.y)+0x9e3779b9+(h<<6)+(h>>2);h^=std::hash<std::int64_t>{}(k.z)+0x9e3779b9+(h<<6)+(h>>2);return h;}};
        std::unordered_map<Key,std::uint32_t,Hash> welded;std::vector<std::uint32_t> vertexIds(current.size());
        for(std::size_t v=0;v<current.size();++v){const auto p=current[v]*1e8;Key key{static_cast<std::int64_t>(std::llround(p.x)),static_cast<std::int64_t>(std::llround(p.y)),static_cast<std::int64_t>(std::llround(p.z))};auto [it,inserted]=welded.emplace(key,static_cast<std::uint32_t>(welded.size()));vertexIds[v]=it->second;}
        std::vector<std::uint32_t> parent(triangles.size());std::iota(parent.begin(),parent.end(),0u);
        auto find=[&](std::uint32_t i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
        struct Edge {std::uint32_t triangle=0,count=0;int orientation=0,rawOrientation=0;};std::unordered_map<std::uint64_t,Edge> edges;
        for(std::uint32_t t=0;t<triangles.size();++t)for(int e=0;e<3;++e){auto a=vertexIds[triangles[t].v[e]],b=vertexIds[triangles[t].v[(e+1)%3]];if(a==b)continue;const auto key=(std::uint64_t(std::min(a,b))<<32)|std::max(a,b);auto [it,inserted]=edges.emplace(key,Edge{t,0,0,0});auto& edge=it->second;
            if(!inserted){const auto ra=find(t),rb=find(edge.triangle);if(ra!=rb)parent[ra]=rb;}++edge.count;edge.rawOrientation+=a<b?1:-1;edge.orientation+=(a<b?1:-1)*(triangles[t].flip?-1:1);}
        std::unordered_map<std::uint32_t,std::uint32_t> componentIds;
        for(std::uint32_t t=0;t<triangles.size();++t){auto [it,inserted]=componentIds.emplace(find(t),static_cast<std::uint32_t>(componentIds.size()));if(inserted)components.push_back({true,true});triangles[t].component=it->second;}
        componentHints.assign(components.size(),UINT32_MAX);
        componentBackfaceHints.assign(components.size(),UINT32_MAX);
        std::vector<bool> rawCoherent(components.size(),true);std::vector<std::size_t> flips(components.size(),0),counts(components.size(),0);
        for(const auto& [key,edge]:edges){const auto c=triangles[edge.triangle].component;auto& component=components[c];component.closed&=edge.count==2;component.reliableOutward&=edge.count==2&&edge.orientation==0;rawCoherent[c]=rawCoherent[c]&&edge.count<=2&&(edge.count<2||edge.rawOrientation==0);}
        for(const auto& t:triangles){++counts[t.component];flips[t.component]+=t.flip;}
        // A coherent triangle winding is stronger evidence than one bad patch
        // of imported shading normals. Select its global side once, preserving
        // the majority source-facing side for open clothing surfaces.
        for(auto& t:triangles)if(rawCoherent[t.component])t.flip=flips[t.component]>counts[t.component]/2;
        for(std::size_t c=0;c<components.size();++c)components[c].reliableOutward=components[c].closed&&rawCoherent[c];
        std::vector<double> volumes(components.size(),0);for(const auto& t:triangles)volumes[t.component]+=glm::dot(current[t.v[0]],glm::cross(current[t.v[1]],current[t.v[2]]))*(t.flip?-1.:1.);
        for(auto& t:triangles)if(components[t.component].reliableOutward&&volumes[t.component]<0)t.flip=!t.flip;
        if(useComponentDop){componentVertices.resize(components.size());componentDops.resize(components.size());for(const auto& t:triangles)for(auto v:t.v)componentVertices[t.component].push_back(v);
            for(auto& indices:componentVertices){std::sort(indices.begin(),indices.end());indices.erase(std::unique(indices.begin(),indices.end()),indices.end());}}
        Refit();
        componentOrder.resize(components.size());std::iota(componentOrder.begin(),componentOrder.end(),0u);
        componentNodes.reserve(components.size()*2);nearbyComponents.reserve(components.size());
        if(!components.empty())BuildComponentTree(0,static_cast<std::uint32_t>(components.size()));
    }
    std::uint32_t BuildComponentTree(std::uint32_t begin,std::uint32_t count) {
        const auto index=static_cast<std::uint32_t>(componentNodes.size());componentNodes.emplace_back();
        Box bound,centers;for(auto i=begin;i<begin+count;++i){const auto& box=componentBounds[componentOrder[i]];bound.Include(box);centers.Include((box.lo+box.hi)*.5);}
        componentNodes[index].box=bound;
        if(count==1){componentNodes[index].SetLeaf(begin,1);return index;}
        const auto extent=centers.hi-centers.lo;const int axis=extent.y>extent.x?(extent.z>extent.y?2:1):(extent.z>extent.x?2:0);const auto middle=begin+count/2;
        std::nth_element(componentOrder.begin()+begin,componentOrder.begin()+middle,componentOrder.begin()+begin+count,[&](auto a,auto b){const double ca=componentBounds[a].lo[axis]+componentBounds[a].hi[axis],cb=componentBounds[b].lo[axis]+componentBounds[b].hi[axis];return ca==cb?a<b:ca<cb;});
        const auto left=BuildComponentTree(begin,middle-begin),right=BuildComponentTree(middle,begin+count-middle);componentNodes[index].SetChildren(left,right);return index;
    }
    const std::vector<std::uint32_t>& CollectNearbyComponents(const glm::dvec3& point,double radius2) const {
        nearbyComponents.clear();if(componentNodes.empty())return nearbyComponents;
        std::array<std::uint32_t,128> stack;std::size_t size=1;stack[0]=0;
        while(size){const auto& node=componentNodes[stack[--size]];++stats.componentBvhNodeTests;if(BoxDistance2(node.box,point)>radius2)continue;
            if(node.IsLeaf())nearbyComponents.push_back(componentOrder[node.Begin()]);
            else {stack[size++]=node.Left();stack[size++]=node.Right();}}
        // Candidate/hint/cache effects remain identical to the original loop.
        // Capacity is reserved once in BuildComponents; no per-query allocation.
        std::sort(nearbyComponents.begin(),nearbyComponents.end());return nearbyComponents;
    }
    template<typename Matches> bool AnyComponentBounds(Matches&& matches) const {
        if(!useComponentBvh){for(const auto& box:componentBounds){++stats.componentBoundTests;if(matches(box))return true;}return false;}
        if(componentNodes.empty())return false;std::array<std::uint32_t,128> stack;std::size_t size=1;stack[0]=0;
        while(size){const auto& node=componentNodes[stack[--size]];++stats.componentBvhNodeTests;if(!matches(node.box))continue;
            if(node.IsLeaf()){++stats.componentBoundTests;return true;}
            stack[size++]=node.Left();stack[size++]=node.Right();}
        return false;
    }
    bool WithinComponentDop(std::uint32_t component,const glm::dvec3& point) const {
        // Any bidirectional pair of finite hits on one component places the
        // point between two points of its convex hull. Projection intervals
        // enclose that hull for open surfaces as well as closed components.
        if(!useComponentDop)return true;++stats.componentDopTests;const auto& bounds=componentDops[component];const auto& directions=DopDirections();
        for(std::size_t d=0;d<DopDirectionCount;++d){const double projection=glm::dot(point,directions[d]);
            // Includes floating projection error and ray barycentric tolerance.
            // Reject only strict exterior points; boundary uncertainty falls
            // back to the unchanged finite-hit enclosure algorithm.
            const double tolerance=8e-10*(1+std::abs(bounds.lo[d])+std::abs(bounds.hi[d])+std::abs(projection));
            if(projection<bounds.lo[d]-tolerance||projection>bounds.hi[d]+tolerance){++stats.componentDopRejects;return false;}}
        return true;
    }
    struct RayHit {bool found=false;double distance=std::numeric_limits<double>::infinity(),facing=0;std::uint32_t component=UINT32_MAX;};
    struct RayPair {RayHit plus,minus;};
    void BuildRayTraversalOrder() {
        if(!useRayGrid||!usePairedRays||nodes.empty())return;rayTraversalOrder.reserve(triangles.size());
        std::array<std::uint32_t,128> stack;std::size_t size=1;stack[0]=0;
        while(size){const auto& node=nodes[stack[--size]];if(node.IsLeaf())for(auto i=node.Begin();i<node.Begin()+node.Count();++i)rayTraversalOrder.push_back(order[i]);
            else {stack[size++]=node.Left();stack[size++]=node.Right();}}
    }
    void RefitRayGrids() {
        if(!useRayGrid||!usePairedRays||rayTraversalOrder.empty())return;const auto& directions=InsideDirections();
        if(triangles.size()<256){for(auto& grid:rayGrids)grid.valid=false;return;}
        const auto maximumAbsolute=glm::max(glm::abs(nodes[0].box.lo),glm::abs(nodes[0].box.hi));
        for(std::size_t direction=0;direction<rayGrids.size();++direction){auto& grid=rayGrids[direction];grid.valid=false;grid.side=rayGridSide;const auto cells=grid.side*grid.side;
            const auto d=directions[direction];grid.main=std::abs(d.y)>std::abs(d.x)?1:0;if(std::abs(d.z)>std::abs(d[grid.main]))grid.main=2;grid.first=(grid.main+1)%3;grid.second=(grid.main+2)%3;grid.ratios={d[grid.first]/d[grid.main],d[grid.second]/d[grid.main]};
            grid.projectedVertices.resize(current.size());grid.lo=glm::dvec2(std::numeric_limits<double>::infinity());grid.hi=-grid.lo;bool finite=true;
            for(std::size_t v=0;v<current.size();++v){const auto projected=grid.Project(current[v]);grid.projectedVertices[v]=projected;finite&=std::isfinite(projected.x)&&std::isfinite(projected.y);grid.lo=glm::min(grid.lo,projected);grid.hi=glm::max(grid.hi,projected);}
            // Projection may subtract two large world coordinates. Bound both
            // triangle and query rounding from the unprojected body AABB; an
            // Inside query already lies in that AABB. Poor conditioning takes
            // the original BVH path instead of relying on cancellation.
            const auto projectionError=glm::dvec2(maximumAbsolute[grid.first]+std::abs(grid.ratios.x)*maximumAbsolute[grid.main],maximumAbsolute[grid.second]+std::abs(grid.ratios.y)*maximumAbsolute[grid.main])*(64*std::numeric_limits<double>::epsilon());
            const auto rawSpan=grid.hi-grid.lo;if(projectionError.x>std::max(rawSpan.x,1e-12)/(grid.side*128)||projectionError.y>std::max(rawSpan.y,1e-12)/(grid.side*128))continue;
            const auto padding=(glm::dvec2(1)+glm::abs(grid.lo)+glm::abs(grid.hi))*1e-9+projectionError*4.;grid.lo-=padding;grid.hi+=padding;const auto span=grid.hi-grid.lo;
            if(!finite||!std::isfinite(span.x)||!std::isfinite(span.y)||span.x<=0||span.y<=0)continue;grid.inverseCell=glm::dvec2(grid.side)/span;
            grid.offsets.resize(cells+1);std::fill(grid.offsets.begin(),grid.offsets.end(),0u);grid.triangleCells.resize(triangles.size());std::uint64_t total=0;
            for(std::uint32_t triangle=0;triangle<triangles.size();++triangle){const auto& t=triangles[triangle];auto lo=glm::min(grid.projectedVertices[t.v[0]],glm::min(grid.projectedVertices[t.v[1]],grid.projectedVertices[t.v[2]])),hi=glm::max(grid.projectedVertices[t.v[0]],glm::max(grid.projectedVertices[t.v[1]],grid.projectedVertices[t.v[2]]));
                // Includes projection rounding, barycentric edge tolerance and
                // cell-boundary rounding. The unchanged 3D slab/Moller tests
                // subsequently reject any extra conservative candidates.
                const auto edgePadding=(glm::dvec2(1)+glm::abs(lo)+glm::abs(hi))*1e-9+projectionError*4.;lo-=edgePadding;hi+=edgePadding;
                const auto x0=grid.Cell(lo.x,0),x1=grid.Cell(hi.x,0),y0=grid.Cell(lo.y,1),y1=grid.Cell(hi.y,1);grid.triangleCells[triangle]={static_cast<std::uint8_t>(x0),static_cast<std::uint8_t>(x1),static_cast<std::uint8_t>(y0),static_cast<std::uint8_t>(y1)};
                total+=std::uint64_t(x1-x0+1)*(y1-y0+1);if(total>RayGrid::MaximumEntries)break;
                for(auto y=y0;y<=y1;++y)for(auto x=x0;x<=x1;++x)++grid.offsets[y*grid.side+x+1];}
            if(total>RayGrid::MaximumEntries)continue;
            for(std::size_t cell=1;cell<grid.offsets.size();++cell)grid.offsets[cell]+=grid.offsets[cell-1];grid.entries.resize(static_cast<std::size_t>(total));
            grid.cursor.resize(cells);std::copy_n(grid.offsets.begin(),cells,grid.cursor.begin());
            // Filling in the original right-first BVH DFS order also preserves
            // finite-hit distance/facing tie rules without query-time sorting.
            for(auto triangle:rayTraversalOrder){const auto& bounds=grid.triangleCells[triangle];for(std::uint32_t y=bounds[2];y<=bounds[3];++y)for(std::uint32_t x=bounds[0];x<=bounds[1];++x){const auto cell=y*grid.side+x;grid.entries[grid.cursor[cell]++]=triangle;}}
            grid.valid=true;
        }
    }
    void RefitFixedRayTriangles() {
        if(!useFastRays||!useRayGrid||!usePairedRays||std::none_of(rayGrids.begin(),rayGrids.end(),[](const auto& grid){return grid.valid;}))return;
        fixedRayTriangles.resize(triangles.size());const auto& directions=InsideDirections();
        for(std::size_t i=0;i<triangles.size();++i){const auto& t=triangles[i];auto& cached=fixedRayTriangles[i];cached.a=current[t.v[0]];cached.e=current[t.v[1]]-cached.a;cached.f=current[t.v[2]]-cached.a;cached.plane=glm::cross(cached.e,cached.f);
            const auto e=glm::abs(cached.e),f=glm::abs(cached.f);cached.numeratorBound={e.y*f.z+e.z*f.y,e.z*f.x+e.x*f.z,e.x*f.y+e.y*f.x};
            for(std::size_t d=0;d<directions.size();++d)for(int side=0;side<2;++side){const auto direction=side?-directions[d]:directions[d];auto& coefficient=cached.rays[d][side];coefficient.cross=glm::cross(direction,cached.f);coefficient.determinant=glm::dot(cached.e,coefficient.cross);coefficient.facing=glm::dot(t.normal,direction);}}
        fixedRayRevision=geometryRevision;
    }
    void AuditRay(const RayHit& hit) const {
        if(!auditRays)return;auto add=[&](std::uint64_t value){for(int byte=0;byte<8;++byte){stats.rayResultHash^=(value>>(byte*8))&255;stats.rayResultHash*=1099511628211ull;}};
        add(hit.found);add(std::bit_cast<std::uint64_t>(hit.distance));add(std::bit_cast<std::uint64_t>(hit.facing));add(hit.component);
    }
    bool RayBox(const Box& box,const glm::dvec3& point,const glm::dvec3& inverse,double limit) const {
        double lo=0,hi=limit;for(int axis=0;axis<3;++axis){double a=(box.lo[axis]-point[axis])*inverse[axis],b=(box.hi[axis]-point[axis])*inverse[axis];if(a>b)std::swap(a,b);lo=std::max(lo,a);hi=std::min(hi,b);if(hi<lo)return false;}return true;
    }
    RayHit FirstRay(const glm::dvec3& point,const glm::dvec3& direction,std::uint32_t component) const {
        RayHit result;++stats.rayQueries;if(nodes.empty())return result;const auto inverse=glm::dvec3(1)/direction;std::array<std::uint32_t,128> stack;std::size_t size=1;stack[0]=0;
        while(size){const auto& node=nodes[stack[--size]];++stats.rayNodeTests;if(useComponentMask&&(node.componentMask&(std::uint64_t(1)<<(component%64)))==0){++stats.componentNodeRejects;continue;}if(!RayBox(node.box,point,inverse,result.distance))continue;
            if(!node.IsLeaf()){stack[size++]=node.Left();stack[size++]=node.Right();continue;}
            for(auto i=node.Begin();i<node.Begin()+node.Count();++i){const auto& t=triangles[order[i]];if(t.component!=component||!RayBox(t.box,point,inverse,result.distance))continue;++stats.rayTriangleTests;
                const auto a=current[t.v[0]],e=current[t.v[1]]-a,f=current[t.v[2]]-a,cross=glm::cross(direction,f);const double determinant=glm::dot(e,cross);if(std::abs(determinant)<1e-18)continue;
                const auto delta=point-a;const double u=glm::dot(delta,cross)/determinant;if(u< -1e-10||u>1+1e-10)continue;const auto q=glm::cross(delta,e);const double v=glm::dot(direction,q)/determinant;if(v< -1e-10||u+v>1+1e-10)continue;
                const double distance=glm::dot(f,q)/determinant,facing=glm::dot(t.normal,direction);if(distance<=1e-9||distance>result.distance+1e-9||std::abs(facing)<1e-7)continue;
                if(distance<result.distance-1e-9||!result.found){result={true,distance,facing,t.component};}
                else if(result.component==t.component&&std::abs(facing)>std::abs(result.facing))result.facing=facing;
            }
        }AuditRay(result);return result;
    }
    bool LineBox(const Box& box,const glm::dvec3& point,const glm::dvec3& inverse,double minusLimit,double plusLimit) const {
        double lo=-minusLimit,hi=plusLimit;for(int axis=0;axis<3;++axis){double a=(box.lo[axis]-point[axis])*inverse[axis],b=(box.hi[axis]-point[axis])*inverse[axis];if(a>b)std::swap(a,b);lo=std::max(lo,a);hi=std::min(hi,b);if(hi<lo)return false;}return true;
    }
    RayPair FirstRayPair(const glm::dvec3& point,const glm::dvec3& direction,std::uint32_t component) const {
        RayPair pair;stats.rayQueries+=2;++stats.sharedRayTraversals;if(nodes.empty())return pair;
        const auto inverse=glm::dvec3(1)/direction,negativeDirection=-direction,negativeInverse=glm::dvec3(1)/negativeDirection;std::array<std::uint32_t,128> stack;std::size_t size=1;stack[0]=0;
        // Each side keeps the reference Moller-Trumbore arithmetic and tie
        // order. Only their identical DFS BVH walk/slab intervals are shared.
        auto test=[&](const Triangle& t,const glm::dvec3& d,RayHit& result){++stats.rayTriangleTests;
            const auto a=current[t.v[0]],e=current[t.v[1]]-a,f=current[t.v[2]]-a,cross=glm::cross(d,f);const double determinant=glm::dot(e,cross);if(std::abs(determinant)<1e-18)return;
            const auto delta=point-a;const double u=glm::dot(delta,cross)/determinant;if(u< -1e-10||u>1+1e-10)return;const auto q=glm::cross(delta,e);const double v=glm::dot(d,q)/determinant;if(v< -1e-10||u+v>1+1e-10)return;
            const double distance=glm::dot(f,q)/determinant,facing=glm::dot(t.normal,d);if(distance<=1e-9||distance>result.distance+1e-9||std::abs(facing)<1e-7)return;
            if(distance<result.distance-1e-9||!result.found)result={true,distance,facing,t.component};else if(result.component==t.component&&std::abs(facing)>std::abs(result.facing))result.facing=facing;
        };
        while(size){const auto& node=nodes[stack[--size]];++stats.rayNodeTests;if(useComponentMask&&(node.componentMask&(std::uint64_t(1)<<(component%64)))==0){++stats.componentNodeRejects;continue;}
            if(!LineBox(node.box,point,inverse,pair.minus.distance,pair.plus.distance))continue;
            if(!node.IsLeaf()){stack[size++]=node.Left();stack[size++]=node.Right();continue;}
            for(auto i=node.Begin();i<node.Begin()+node.Count();++i){const auto& t=triangles[order[i]];if(t.component!=component)continue;
                if(RayBox(t.box,point,inverse,pair.plus.distance))test(t,direction,pair.plus);
                if(RayBox(t.box,point,negativeInverse,pair.minus.distance))test(t,negativeDirection,pair.minus);
            }
        }AuditRay(pair.plus);AuditRay(pair.minus);return pair;
    }
    RayPair FirstRayPairGrid(const glm::dvec3& point,std::size_t directionIndex,std::uint32_t component) const {
        const auto& grid=rayGrids[directionIndex];const auto& direction=InsideDirections()[directionIndex];
        if(!grid.valid){++stats.rayGridFallbacks;return FirstRayPair(point,direction,component);}const auto projected=grid.Project(point);
        if(!std::isfinite(projected.x)||!std::isfinite(projected.y)){++stats.rayGridFallbacks;return FirstRayPair(point,direction,component);}
        RayPair pair;stats.rayQueries+=2;++stats.sharedRayTraversals;++stats.rayGridQueries;
        if(projected.x<grid.lo.x||projected.x>grid.hi.x||projected.y<grid.lo.y||projected.y>grid.hi.y){AuditRay(pair.plus);AuditRay(pair.minus);return pair;}
        const auto cell=grid.Cell(projected.y,1)*grid.side+grid.Cell(projected.x,0);
        const auto inverse=glm::dvec3(1)/direction,negativeDirection=-direction,negativeInverse=glm::dvec3(1)/negativeDirection;
        // Keep reference arithmetic and update order unchanged. The fast path
        // caches only point-independent expressions, with both ray signs
        // computed separately rather than relying on sign identities.
        const bool fast=useFastRays&&fixedRayRevision==geometryRevision&&fixedRayTriangles.size()==triangles.size();
        auto test=[&](std::uint32_t triangleIndex,const glm::dvec3& d,int side,RayHit& result){const auto& t=triangles[triangleIndex];++stats.rayTriangleTests;
            if(fast){++stats.fastRayTests;const auto& cached=fixedRayTriangles[triangleIndex];const auto& coefficient=cached.rays[directionIndex][side];const double determinant=coefficient.determinant;if(std::abs(determinant)<1e-18)return;
                const auto delta=point-cached.a;
                // A scalar-triple-product estimate screens only triangles
                // provably farther than an existing exact first hit. Bound
                // both evaluation orders' roundoff; near hits/ties, grazing,
                // underflow/overflow and uncertain estimates use Moller.
                if(useFastRayPlanes&&std::isfinite(result.distance)&&std::abs(determinant)>(cached.numeratorBound.x+cached.numeratorBound.y+cached.numeratorBound.z)*1e-8){
                    const double magnitude=glm::dot(glm::abs(delta),cached.numeratorBound),estimate=glm::dot(delta,cached.plane)/determinant;
                    const double error=128*std::numeric_limits<double>::epsilon()*(magnitude/std::abs(determinant)+std::abs(estimate));
                    if(std::isfinite(magnitude)&&magnitude>std::numeric_limits<double>::min()*1024&&std::isfinite(estimate)&&std::isfinite(error)&&estimate>result.distance+1e-5+std::max(1e-8,error)){++stats.fastRayPlaneRejects;return;}}
                const double u=glm::dot(delta,coefficient.cross)/determinant;if(u< -1e-10||u>1+1e-10)return;const auto q=glm::cross(delta,cached.e);const double v=glm::dot(d,q)/determinant;if(v< -1e-10||u+v>1+1e-10)return;
                const double distance=glm::dot(cached.f,q)/determinant,facing=coefficient.facing;if(distance<=1e-9||distance>result.distance+1e-9||std::abs(facing)<1e-7)return;
                if(distance<result.distance-1e-9||!result.found)result={true,distance,facing,t.component};else if(result.component==t.component&&std::abs(facing)>std::abs(result.facing))result.facing=facing;return;
            }
            const auto a=current[t.v[0]],e=current[t.v[1]]-a,f=current[t.v[2]]-a,cross=glm::cross(d,f);const double determinant=glm::dot(e,cross);if(std::abs(determinant)<1e-18)return;
            const auto delta=point-a;const double u=glm::dot(delta,cross)/determinant;if(u< -1e-10||u>1+1e-10)return;const auto q=glm::cross(delta,e);const double v=glm::dot(d,q)/determinant;if(v< -1e-10||u+v>1+1e-10)return;
            const double distance=glm::dot(f,q)/determinant,facing=glm::dot(t.normal,d);if(distance<=1e-9||distance>result.distance+1e-9||std::abs(facing)<1e-7)return;
            if(distance<result.distance-1e-9||!result.found)result={true,distance,facing,t.component};else if(result.component==t.component&&std::abs(facing)>std::abs(result.facing))result.facing=facing;
        };
        for(auto i=grid.offsets[cell];i<grid.offsets[cell+1];++i){const auto triangleIndex=grid.entries[i];const auto& t=triangles[triangleIndex];++stats.rayGridCandidateTests;if(t.component!=component)continue;
            if(RayBox(t.box,point,inverse,pair.plus.distance))test(triangleIndex,direction,0,pair.plus);
            if(RayBox(t.box,point,negativeInverse,pair.minus.distance))test(triangleIndex,negativeDirection,1,pair.minus);}
        AuditRay(pair.plus);AuditRay(pair.minus);return pair;
    }
    struct InsideResult {std::uint32_t component=UINT32_MAX;HairBodyNearest boundary;};
    InsideResult Inside(const glm::dvec3& point,const HairBodyNearest& alreadyNearest) const {
        InsideResult result;++stats.insideQueries;if(nodes.empty()||BoxDistance2(nodes[0].box,point)>0)return result;
        // Non-axis-aligned directions avoid the model's frequent seam axes.
        // A vote requires two outward exits through ONE connected surface.
        // Two separate open sheets cannot manufacture a false solid interval.
        const auto& directions=InsideDirections();
        double deepestDistance=-1;
        auto testComponent=[&](std::uint32_t component){++stats.componentBoundTests;if(BoxDistance2(componentBounds[component],point)>0||!WithinComponentDop(component,point))return;int insideVotes=0,outsideVotes=0;
            for(std::size_t directionIndex=0;directionIndex<directions.size();++directionIndex){const auto& direction=directions[directionIndex];const auto pair=usePairedRays?(useRayGrid?FirstRayPairGrid(point,directionIndex,component):FirstRayPair(point,direction,component)):RayPair{FirstRay(point,direction,component),FirstRay(point,-direction,component)};const auto& plus=pair.plus;const auto& minus=pair.minus;
                if(plus.found&&minus.found&&plus.facing>0&&minus.facing>0)++insideVotes;else ++outsideVotes;
                if(insideVotes>=2||outsideVotes>=2)break;}
            // Nested skin/clothing components must be tested independently:
            // entering an inner skin surface does not invalidate enclosure by
            // its outer shirt. Select the deepest enclosing actual surface.
            if(insideVotes>=2){const auto boundary=alreadyNearest.found&&triangles[alreadyNearest.triangle].component==component?alreadyNearest:NearestGeometry(point,std::numeric_limits<double>::infinity(),component,componentHints[component]);if(boundary.found){componentHints[component]=boundary.triangle;if(boundary.distance>deepestDistance){deepestDistance=boundary.distance;result.component=component;result.boundary=boundary;}}}
        };
        // The same inclusive necessary condition, in the original component-ID
        // order, preserves votes, ray audit order, boundary ties and hint state.
        if(useComponentBvh){for(auto component:CollectNearbyComponents(point,0))testComponent(component);}
        else for(std::uint32_t component=0;component<componentBounds.size();++component)testComponent(component);
        return result;
    }
    HairBodyNearest NearestGeometry(const glm::dvec3& p,double maximum,std::uint32_t component=UINT32_MAX,std::uint32_t hint=UINT32_MAX) const {
        HairBodyNearest result;double best2=maximum*maximum;std::array<std::uint32_t,128> stack;std::size_t stackSize=1;stack[0]=0;
        auto test=[&](std::uint32_t triangleIndex){const auto& t=triangles[triangleIndex];const auto q=ClosestTriangle(p,current[t.v[0]],current[t.v[1]],current[t.v[2]]);const double d2=glm::dot(p-q,p-q);if(d2<best2||(d2==best2&&(!result.found||triangleIndex<result.triangle))){best2=d2;result.found=true;result.point=q;result.normal=t.normal;result.distance=std::sqrt(d2);result.signedDistance=result.distance;result.triangle=triangleIndex;result.mesh=t.mesh;}};
        if(!useHints)hint=UINT32_MAX;
        if(hint<triangles.size()&&(component==UINT32_MAX||triangles[hint].component==component)){++stats.nearestHintTests;test(hint);}
        while(stackSize){const auto& node=nodes[stack[--stackSize]];++stats.nodeTests;if(useComponentMask&&component!=UINT32_MAX&&(node.componentMask&(std::uint64_t(1)<<(component%64)))==0){++stats.componentNodeRejects;continue;}if(BoxDistance2(node.box,p)>best2)continue;
            if(node.IsLeaf()){for(auto i=node.Begin();i<node.Begin()+node.Count();++i){const auto triangleIndex=order[i];const auto& t=triangles[triangleIndex];if(component!=UINT32_MAX&&t.component!=component)continue;++stats.triangleTests;if(BoxDistance2(t.box,p)>best2)continue;
                if(triangleIndex!=hint)test(triangleIndex);}}
            else {const double l=BoxDistance2(nodes[node.Left()].box,p),r=BoxDistance2(nodes[node.Right()].box,p);if(l<r){if(r<=best2)stack[stackSize++]=node.Right();if(l<=best2)stack[stackSize++]=node.Left();}else {if(l<=best2)stack[stackSize++]=node.Left();if(r<=best2)stack[stackSize++]=node.Right();}}
        }return result;
    }
    std::uint32_t Build(std::uint32_t begin,std::uint32_t count) {
        const auto index=static_cast<std::uint32_t>(nodes.size());nodes.emplace_back();sweptBounds.emplace_back();
        Box bound,centers;for(std::uint32_t i=begin;i<begin+count;++i){const auto& t=triangles[order[i]];bound.Include(t.box);centers.Include((t.box.lo+t.box.hi)*.5);}
        nodes[index].box=bound;sweptBounds[index]=bound;
        if(count<=8){nodes[index].SetLeaf(begin,count);for(auto i=begin;i<begin+count;++i)nodes[index].componentMask|=std::uint64_t(1)<<(triangles[order[i]].component%64);return index;}
        const auto extent=centers.hi-centers.lo;const int axis=extent.y>extent.x?(extent.z>extent.y?2:1):(extent.z>extent.x?2:0);
        const auto middle=begin+count/2;
        std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+begin+count,[&](auto a,auto b){const double ca=triangles[a].box.lo[axis]+triangles[a].box.hi[axis],cb=triangles[b].box.lo[axis]+triangles[b].box.hi[axis];return ca==cb?a<b:ca<cb;});
        const auto left=Build(begin,middle-begin),right=Build(middle,begin+count-middle);nodes[index].SetChildren(left,right);nodes[index].componentMask=nodes[left].componentMask|nodes[right].componentMask;return index;
    }
    void Refit(bool refitProjections=true) {
        componentBounds.assign(components.size(),Box{});
        for(auto& t:triangles){t.box={};t.swept={};for(auto v:t.v){t.box.Include(current[v]);t.swept.Include(current[v]);t.swept.Include(previous[v]);}
            if(t.component<componentBounds.size())componentBounds[t.component].Include(t.box);
            auto n=glm::cross(current[t.v[1]]-current[t.v[0]],current[t.v[2]]-current[t.v[0]]);const double l2=glm::dot(n,n);if(l2>Epsilon)t.normal=n*((t.flip?-1.:1.)/std::sqrt(l2));}
        if(useComponentDop&&refitProjections){const auto& directions=DopDirections();for(std::size_t c=0;c<componentVertices.size();++c){auto& bounds=componentDops[c];bounds.lo.fill(std::numeric_limits<double>::infinity());bounds.hi.fill(-std::numeric_limits<double>::infinity());
                for(auto v:componentVertices[c])for(std::size_t d=0;d<DopDirectionCount;++d){const double projection=glm::dot(current[v],directions[d]);bounds.lo[d]=std::min(bounds.lo[d],projection);bounds.hi[d]=std::max(bounds.hi[d],projection);}}}
        for(std::size_t i=nodes.size();i-->0;){auto& node=nodes[i];auto& swept=sweptBounds[i];node.box={};swept={};if(node.IsLeaf()){for(auto j=node.Begin();j<node.Begin()+node.Count();++j){const auto& t=triangles[order[j]];node.box.Include(t.box);swept.Include(t.swept);}}
            else {node.box.Include(nodes[node.Left()].box);node.box.Include(nodes[node.Right()].box);swept.Include(sweptBounds[node.Left()]);swept.Include(sweptBounds[node.Right()]);}}
        for(std::size_t i=componentNodes.size();i-->0;){auto& node=componentNodes[i];if(node.IsLeaf())node.box=componentBounds[componentOrder[node.Begin()]];
            else {node.box={};node.box.Include(componentNodes[node.Left()].box);node.box.Include(componentNodes[node.Right()].box);}}
        if(refitProjections){RefitRayGrids();RefitFixedRayTriangles();}
    }
};

HairBodyCollision::HairBodyCollision():impl_(std::make_unique<Impl>()){}
HairBodyCollision::~HairBodyCollision()=default;
HairBodyCollision::HairBodyCollision(HairBodyCollision&&) noexcept=default;
HairBodyCollision& HairBodyCollision::operator=(HairBodyCollision&&) noexcept=default;
bool HairBodyCollision::Initialize(const SkeletalAssetPackage& target,std::span<const std::uint32_t> excluded,double units,std::string* error) {
    auto fail=[&](const std::string& message){if(error)*error=message;return false;};
    if(!target.geometry||target.skins.size()!=target.geometry->meshes.size())return fail("Hair collision body has invalid geometry/skin layout");
    if(!std::isfinite(units)||units<=0)return fail("Hair collision unitsPerMeter must be positive");
    auto fresh=std::make_unique<Impl>();fresh->units=units;fresh->boneCount=target.skeleton.bones.size();
    if(const auto* option=std::getenv("MODEL_HAIR_COMPONENT_MASK"))fresh->useComponentMask=std::string(option)!="0";
    if(const auto* option=std::getenv("MODEL_HAIR_QUERY_HINT"))fresh->useHints=std::string(option)!="0";
    if(const auto* option=std::getenv("MODEL_HAIR_PAIRED_RAYS"))fresh->usePairedRays=std::string(option)!="0";
    if(const auto* option=std::getenv("MODEL_HAIR_RAY_AUDIT"))fresh->auditRays=std::string(option)=="1";
    if(const auto* option=std::getenv("MODEL_HAIR_COMPONENT_BACKFACE_GATE"))fresh->useComponentBackfaceGate=std::string(option)!="0";
    if(const auto* option=std::getenv("MODEL_HAIR_COMPONENT_BVH"))fresh->useComponentBvh=std::string(option)!="0";
    if(const auto* option=std::getenv("MODEL_HAIR_COMPONENT_DOP"))fresh->useComponentDop=std::string(option)!="0";
    if(const auto* option=std::getenv("MODEL_HAIR_RAY_GRID"))fresh->useRayGrid=std::string(option)!="0";
    if(const auto* option=std::getenv("MODEL_HAIR_FAST_RAYS")){const std::string mode(option);fresh->useFastRays=mode!="0";fresh->useFastRayPlanes=mode=="2";}
    if(const auto* option=std::getenv("MODEL_HAIR_INSIDE_ONLY"))fresh->useInsideOnly=std::string(option)!="0";
    if(const auto* option=std::getenv("MODEL_HAIR_RAY_GRID_SIDE")){const std::string side(option);if(side=="64")fresh->rayGridSide=64;else if(side=="128")fresh->rayGridSide=128;else if(side=="256")fresh->rayGridSide=256;}
    std::vector<bool> skip(target.geometry->meshes.size());for(auto mesh:excluded)if(mesh<skip.size())skip[mesh]=true;
    for(std::size_t m=0;m<skip.size();++m){const auto& mesh=target.geometry->meshes[m];std::string name=Upper(mesh.name);if(mesh.materialIndex<target.geometry->materials.size())name+=' '+Upper(target.geometry->materials[mesh.materialIndex].name);
        // Hair and transparent eye shells must not become an inflated head cage.
        if(name.find("HAIR")!=std::string::npos||name.find("EYE_HIGHLIGHT")!=std::string::npos||name.find("EYEHIGHLIGHT")!=std::string::npos||name.find("EYELINE")!=std::string::npos)skip[m]=true;}
    std::vector<MeshInstance> instances=target.geometry->instances;
    if(instances.empty())for(std::uint32_t m=0;m<skip.size();++m)instances.push_back({{},m,glm::dmat4(1)});
    for(const auto& instance:instances){const auto m=instance.meshIndex;if(m>=skip.size()||skip[m])continue;const auto& mesh=target.geometry->meshes[m];const auto& skin=target.skins[m];
        if(skin.vertices.size()!=mesh.vertices.size())return fail("Hair body collision vertex skin layout mismatch");
        const double determinant=glm::determinant(skin.bindTransform);if(!std::isfinite(determinant)||std::abs(determinant)<1e-12)return fail("Hair body collision singular mesh bind transform");
        const auto instanceIndex=static_cast<std::uint32_t>(fresh->instanceFromSkeleton.size());fresh->instanceFromSkeleton.push_back(instance.transform*glm::inverse(skin.bindTransform));fresh->instanceTransforms.push_back(instance.transform);
        std::vector<std::uint32_t> map(mesh.vertices.size(),UINT32_MAX);for(auto index:mesh.indices)if(index<map.size())map[index]=0;
        for(std::uint32_t v=0;v<map.size();++v)if(map[v]!=UINT32_MAX){map[v]=static_cast<std::uint32_t>(fresh->vertices.size());Impl::Vertex vertex;vertex.fallback=mesh.vertices[v].position;vertex.instance=instanceIndex;
            const auto& source=skin.vertices[v];double sum=0;for(int k=0;k<4;++k)if(source.joints[k]<fresh->boneCount&&std::isfinite(source.weights[k])&&source.weights[k]>0)sum+=source.weights[k];
            if(sum>1e-12)for(int k=0;k<4;++k){const auto bone=source.joints[k];const double weight=source.weights[k];if(bone>=fresh->boneCount||!std::isfinite(weight)||weight<=0)continue;
                auto& influence=vertex.influences[vertex.count++];influence.bone=bone;influence.weight=weight/sum;influence.offset=bone<skin.boneFromMesh.size()?skin.boneFromMesh[bone]:target.skeleton.bones[bone].inverseBind*skin.bindTransform;
                influence.weightedPosition=glm::dvec3(influence.offset*glm::dvec4(vertex.fallback,1))*influence.weight;}
            fresh->vertices.push_back(vertex);
        }
        for(std::size_t morph=0;morph<target.morphs.size();++morph)for(const auto& delta:target.morphs[morph].deltas)if(delta.mesh==m&&delta.vertex<map.size()&&map[delta.vertex]!=UINT32_MAX)fresh->morphs.push_back({map[delta.vertex],static_cast<std::uint32_t>(morph),delta.position});
        const auto normalTransform=glm::transpose(glm::inverse(glm::dmat3(instance.transform)));
        for(std::size_t i=0;i+2<mesh.indices.size();i+=3){const auto a=mesh.indices[i],b=mesh.indices[i+1],c=mesh.indices[i+2];if(a>=map.size()||b>=map.size()||c>=map.size())return fail("Hair collision body triangle index outside mesh");
            Impl::Triangle triangle;triangle.v={map[a],map[b],map[c]};triangle.mesh=m;
            const auto pa=glm::dvec3(instance.transform*glm::dvec4(mesh.vertices[a].position,1)),pb=glm::dvec3(instance.transform*glm::dvec4(mesh.vertices[b].position,1)),pc=glm::dvec3(instance.transform*glm::dvec4(mesh.vertices[c].position,1));
            const auto winding=glm::cross(pb-pa,pc-pa),normal=normalTransform*glm::dvec3(mesh.vertices[a].normal+mesh.vertices[b].normal+mesh.vertices[c].normal);triangle.flip=glm::dot(winding,normal)<0;
            fresh->triangles.push_back(triangle);
        }
    }
    if(fresh->triangles.empty())return fail("Hair collision body has no retained triangles");
    fresh->current.resize(fresh->vertices.size());fresh->previous.resize(fresh->vertices.size());fresh->morphed.resize(fresh->vertices.size());
    fresh->geometryRevision=impl_?impl_->geometryRevision:0;
    HairBodyCollision candidate;candidate.impl_=std::move(fresh);auto rest=EvaluateSkeletalPose(target,nullptr,0,false);if(!candidate.Update(rest,error))return false;
    auto& built=*candidate.impl_;built.BuildComponents();built.order.resize(built.triangles.size());std::iota(built.order.begin(),built.order.end(),0u);built.nodes.reserve(built.triangles.size()*2);built.Build(0,static_cast<std::uint32_t>(built.triangles.size()));
    built.BuildRayTraversalOrder();candidate.ResetMotion();candidate.ResetQueryStats();impl_=std::move(candidate.impl_);if(error)error->clear();return true;
}
bool HairBodyCollision::Update(const SkeletalPose& pose,std::string* error) {
    const auto start=std::chrono::steady_clock::now();auto& data=*impl_;
    if(pose.global.size()!=data.boneCount){if(error)*error="Hair collision body pose does not match skeleton";return false;}
    bool geometryChanged=!data.updated;if(data.updated)data.previous=data.current;
    for(std::size_t i=0;i<data.vertices.size();++i)data.morphed[i]={0,0,0};
    bool hasMorph=false;for(const auto& morph:data.morphs)if(morph.morph<pose.morphWeights.size()){const double weight=pose.morphWeights[morph.morph];if(std::isfinite(weight)&&std::abs(weight)>1e-12){data.morphed[morph.vertex]+=morph.delta*weight;hasMorph=true;}}
    for(std::size_t i=0;i<data.vertices.size();++i){const auto& v=data.vertices[i];glm::dvec3 position(0);if(v.count){for(std::uint8_t k=0;k<v.count;++k){const auto& influence=v.influences[k];auto weighted=influence.weightedPosition;
            if(hasMorph&&glm::dot(data.morphed[i],data.morphed[i])>0)weighted+=glm::dvec3(influence.offset*glm::dvec4(data.morphed[i],0))*influence.weight;
            position+=glm::dvec3(pose.global[influence.bone]*glm::dvec4(weighted,influence.weight));}
        position=glm::dvec3(data.instanceFromSkeleton[v.instance]*glm::dvec4(position,1));}
        else position=glm::dvec3(data.instanceTransforms[v.instance]*glm::dvec4(v.fallback+(hasMorph?data.morphed[i]:glm::dvec3(0)),1));
        const auto next=position/data.units;if(!Finite(next)){if(error)*error="Hair collision body skin produced nonfinite vertex";return false;}geometryChanged=geometryChanged||next.x!=data.current[i].x||next.y!=data.current[i].y||next.z!=data.current[i].z;data.current[i]=next;}
    if(geometryChanged&&++data.geometryRevision==0)++data.geometryRevision;
    if(!data.updated)data.previous=data.current;data.updated=true;data.Refit(geometryChanged);
    data.updateMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(error)error->clear();return true;
}
void HairBodyCollision::ResetMotion(){impl_->previous=impl_->current;impl_->Refit();}
HairBodyNearest HairBodyCollision::NearestSurface(const glm::dvec3& p,double maximum,std::uint32_t* triangleHint) const {
    auto& data=*impl_;++data.stats.surfaceNearestQueries;
    if(data.nodes.empty()||!Finite(p)||std::isnan(maximum)||maximum<0)return {};
    auto result=data.NearestGeometry(p,maximum,UINT32_MAX,triangleHint?*triangleHint:data.lastSurfaceHint);
    if(result.found){result.backfacing=glm::dot(p-result.point,result.normal)<0;result.inside=false;result.signedDistance=result.distance;data.lastSurfaceHint=result.triangle;if(triangleHint)*triangleHint=result.triangle;}
    return result;
}
bool HairBodyCollision::HasNearbyBackFacingSurface(const glm::dvec3& p,double maximum,std::uint32_t* triangleHint) const {
    auto& data=*impl_;++data.stats.nearbyBackfaceQueries;
    if(data.nodes.empty()||!Finite(p)||!std::isfinite(maximum)||maximum<0)return false;
    const double radius2=maximum*maximum;const std::uint32_t hint=triangleHint?*triangleHint:UINT32_MAX;
    if(data.useComponentBackfaceGate){
        // Default candidate gate: a separate closest-surface query for
        // EVERY nearby component avoids inner-skin occlusion of an outer shirt.
        // A hint seeds that exact query; a non-nearest backface hint must never
        // bypass the per-component nearest-face test.
        auto testComponent=[&](std::uint32_t component){++data.stats.componentBoundTests;if(BoxDistance2(data.componentBounds[component],p)>radius2)return false;
            const auto seed=hint<data.triangles.size()&&data.triangles[hint].component==component?hint:data.componentBackfaceHints[component];
            const auto beforeNodes=data.stats.nodeTests,beforeTriangles=data.stats.triangleTests,beforeHints=data.stats.nearestHintTests;
            const auto nearest=data.NearestGeometry(p,maximum,component,seed);
            data.stats.nearbyBackfaceNodeTests+=data.stats.nodeTests-beforeNodes;
            data.stats.nearbyBackfaceTriangleTests+=data.stats.triangleTests-beforeTriangles+data.stats.nearestHintTests-beforeHints;
            if(!nearest.found)return false;data.componentBackfaceHints[component]=nearest.triangle;
            if(glm::dot(p-nearest.point,nearest.normal)< -1e-12){if(triangleHint)*triangleHint=nearest.triangle;return true;}
            return false;
        };
        if(data.useComponentBvh){for(auto component:data.CollectNearbyComponents(p,radius2))if(testComponent(component))return true;}
        else for(std::uint32_t component=0;component<data.components.size();++component)if(testComponent(component))return true;
        return false;
    }
    auto test=[&](std::uint32_t triangleIndex){const auto& triangle=data.triangles[triangleIndex];++data.stats.nearbyBackfaceTriangleTests;if(BoxDistance2(triangle.box,p)>radius2)return false;
        const auto closest=ClosestTriangle(p,data.current[triangle.v[0]],data.current[triangle.v[1]],data.current[triangle.v[2]]);const auto delta=p-closest;
        return glm::dot(delta,delta)<=radius2&&glm::dot(delta,triangle.normal)< -1e-12;
    };
    if(hint<data.triangles.size()&&test(hint))return true;
    std::array<std::uint32_t,128> stack;std::size_t size=1;stack[0]=0;
    while(size){const auto& node=data.nodes[stack[--size]];++data.stats.nearbyBackfaceNodeTests;if(BoxDistance2(node.box,p)>radius2)continue;
        if(!node.IsLeaf()){stack[size++]=node.Left();stack[size++]=node.Right();continue;}
        for(auto i=node.Begin();i<node.Begin()+node.Count();++i){const auto triangleIndex=data.order[i];if(triangleIndex!=hint&&test(triangleIndex)){if(triangleHint)*triangleHint=triangleIndex;return true;}}
    }
    return false;
}
HairBodyNearest HairBodyCollision::Nearest(const glm::dvec3& p,double maximum,std::uint32_t* triangleHint) const {
    auto& data=*impl_;HairBodyNearest result;++data.stats.nearestQueries;
    if(data.nodes.empty()||!Finite(p)||std::isnan(maximum)||maximum<0)return result;
    result=data.NearestGeometry(p,maximum,UINT32_MAX,triangleHint?*triangleHint:data.lastHint);
    if(result.found){const auto inside=result.distance>1e-10?data.Inside(p,result):Impl::InsideResult{};
        if(inside.component!=UINT32_MAX){result=inside.boundary;if(result.distance>maximum)return HairBodyNearest{};}
        const auto& triangle=data.triangles[result.triangle];const double oriented=glm::dot(p-result.point,triangle.normal);result.backfacing=oriented< -1e-12;
        result.inside=inside.component!=UINT32_MAX;
        result.signedDistance=result.inside?-result.distance:result.distance;
        if(result.distance>1e-10)result.normal=(result.inside?result.point-p:p-result.point)/result.distance;
    }
    if(result.found){data.lastHint=result.triangle;if(triangleHint)*triangleHint=result.triangle;}
    return result;
}
HairBodyNearest HairBodyCollision::NearestInside(const glm::dvec3& point,std::uint32_t* triangleHint) const {
    auto& data=*impl_;++data.stats.insideOnlyQueries;std::uint32_t hint=triangleHint?*triangleHint:data.lastInsideHint;
    if(!data.useInsideOnly){const auto result=Nearest(point,std::numeric_limits<double>::infinity(),&hint);if(!result.found||!result.inside)return {};
        data.lastInsideHint=result.triangle;if(triangleHint)*triangleHint=result.triangle;return result;}
    ++data.stats.nearestQueries;if(data.nodes.empty()||!Finite(point))return {};
    // One ULP of radius headroom includes values whose squared distance lies
    // just above eps^2 but whose correctly rounded sqrt is still exactly eps.
    constexpr double boundaryEpsilon=1e-10;const auto boundary=data.NearestGeometry(point,std::nextafter(boundaryEpsilon,std::numeric_limits<double>::infinity()),UINT32_MAX,hint);
    if(boundary.found&&boundary.distance<=boundaryEpsilon)return {};
    const auto inside=data.Inside(point,{});if(inside.component==UINT32_MAX||!inside.boundary.found)return {};
    auto result=inside.boundary;result.inside=true;result.backfacing=glm::dot(point-result.point,result.normal)<0;result.signedDistance=-result.distance;
    if(result.distance>boundaryEpsilon)result.normal=(result.point-point)/result.distance;
    data.lastInsideHint=result.triangle;if(triangleHint)*triangleHint=result.triangle;return result;
}
HairBodyContact HairBodyCollision::Contact(const glm::dvec3& previous,const glm::dvec3& current,double margin,double maxInside,bool sweep,std::uint32_t* triangleHint) const {
    auto& data=*impl_;HairBodyContact result;
    if(!Finite(previous)||!Finite(current)||!std::isfinite(margin)||margin<0||!std::isfinite(maxInside)||maxInside<0)return result;
    const auto nearest=Nearest(current,std::max(margin,maxInside),triangleHint);
    if(nearest.found){result.normal=nearest.normal;result.closestPoint=nearest.point;result.signedGap=nearest.signedDistance;result.triangle=nearest.triangle;result.outsideClearance=nearest.inside?0:nearest.distance;
        if(nearest.signedDistance<margin&&(nearest.inside||!nearest.backfacing)){result.hit=true;result.correction=nearest.normal*(margin-nearest.signedDistance);}}
    if(!sweep||data.nodes.empty())return result;
    auto swept=SweepContact(previous,current,margin,true);if(swept.hit){swept.outsideClearance=result.outsideClearance;return swept;}return result;
}
HairBodyContact HairBodyCollision::SweepContact(const glm::dvec3& previous,const glm::dvec3& current,double margin,bool movingBody) const {
    auto& data=*impl_;HairBodyContact result;
    if(!Finite(previous)||!Finite(current)||!std::isfinite(margin)||margin<0||data.nodes.empty())return result;
    ++data.stats.sweepQueries;Box path;path.Include(previous);path.Include(current);path.lo-=glm::dvec3(margin);path.hi+=glm::dvec3(margin);
    std::array<std::uint32_t,128> stack;std::size_t stackSize=1;stack[0]=0;double earliest=2;
    while(stackSize){const auto nodeIndex=stack[--stackSize];const auto& node=data.nodes[nodeIndex];++data.stats.nodeTests;if(!Overlap(path,movingBody?data.sweptBounds[nodeIndex]:node.box))continue;
        if(!node.IsLeaf()){stack[stackSize++]=node.Left();stack[stackSize++]=node.Right();continue;}
        for(auto i=node.Begin();i<node.Begin()+node.Count();++i){const auto triangleIndex=data.order[i];const auto& triangle=data.triangles[triangleIndex];if(!Overlap(path,movingBody?triangle.swept:triangle.box))continue;++data.stats.sweepTriangleTests;
            const auto& start=movingBody?data.previous:data.current;const auto a=start[triangle.v[0]],b=start[triangle.v[1]],c=start[triangle.v[2]];
            const auto da=data.current[triangle.v[0]]-a,db=data.current[triangle.v[1]]-b,dc=data.current[triangle.v[2]]-c;
            const auto q=previous-a,dq=current-previous-da,e=b-a,de=db-da,f=c-a,df=dc-da;
            const auto n0=glm::cross(e,f),n1=glm::cross(de,f)+glm::cross(e,df),n2=glm::cross(de,df);
            std::array<double,4> coefficients{glm::dot(q,n0),glm::dot(dq,n0)+glm::dot(q,n1),glm::dot(dq,n1)+glm::dot(q,n2),glm::dot(dq,n2)};
            std::array<double,8> roots{};const auto count=UnitRoots(coefficients,roots);
            for(std::size_t root=0;root<count;++root){const double time=roots[root];if(time>=earliest)continue;
                const double sign=triangle.flip?-1.:1.;const double derivative=(coefficients[1]+time*(2*coefficients[2]+3*time*coefficients[3]))*sign;
                const auto at=a+da*time,bt=b+db*time,ct=c+dc*time,pt=previous+(current-previous)*time;
                const double normalLength=glm::length(glm::cross(bt-at,ct-at));if(normalLength<1e-12||derivative>=-normalLength*1e-10)continue;
                glm::dvec3 bary;const auto closest=ClosestTriangle(pt,at,bt,ct,&bary);if(glm::length(pt-closest)>1e-7)continue;
                const auto endPoint=data.current[triangle.v[0]]*bary.x+data.current[triangle.v[1]]*bary.y+data.current[triangle.v[2]]*bary.z;
                const double gap=glm::dot(current-endPoint,triangle.normal);if(gap>=margin)continue;
                // A valid crossing, not an infinite negative half-space. Its
                // correction is bounded by this point/body trajectory.
                earliest=time;result.hit=true;result.swept=true;result.normal=triangle.normal;result.closestPoint=endPoint;result.signedGap=gap;result.correction=triangle.normal*(margin-gap);result.triangle=triangleIndex;
            }
        }
    }
    return result;
}
bool HairBodyCollision::MayIntersectSweptBounds(const glm::dvec3& lo,const glm::dvec3& hi,double margin,bool movingBody) const {
    auto& data=*impl_;++data.stats.broadphaseQueries;if(!Finite(lo)||!Finite(hi)||!std::isfinite(margin)||margin<0||data.nodes.empty())return false;
    Box query;query.lo=glm::min(lo,hi)-glm::dvec3(margin);query.hi=glm::max(lo,hi)+glm::dvec3(margin);std::array<std::uint32_t,128> stack;std::size_t size=1;stack[0]=0;
    while(size){const auto index=stack[--size];const auto& node=data.nodes[index];++data.stats.broadphaseNodeTests;if(!Overlap(query,movingBody?data.sweptBounds[index]:node.box))continue;
        if(!node.IsLeaf()){stack[size++]=node.Left();stack[size++]=node.Right();continue;}
        for(auto i=node.Begin();i<node.Begin()+node.Count();++i){++data.stats.broadphaseTriangleTests;const auto& triangle=data.triangles[data.order[i]];if(Overlap(query,movingBody?triangle.swept:triangle.box))return true;}
    }return false;
}
bool HairBodyCollision::MayBeInsideComponentBounds(const glm::dvec3& point) const {
    if(!Finite(point))return false;
    auto& data=*impl_;if(!data.useComponentDop)return data.AnyComponentBounds([&](const Box& box){return BoxDistance2(box,point)<=0;});
    auto testComponent=[&](std::uint32_t component){++data.stats.componentBoundTests;return BoxDistance2(data.componentBounds[component],point)<=0&&data.WithinComponentDop(component,point);};
    if(data.useComponentBvh){for(auto component:data.CollectNearbyComponents(point,0))if(testComponent(component))return true;}
    else for(std::uint32_t component=0;component<data.components.size();++component)if(testComponent(component))return true;
    return false;
}
bool HairBodyCollision::MayOverlapComponentBounds(const glm::dvec3& lo,const glm::dvec3& hi) const {
    if(!Finite(lo)||!Finite(hi))return false;Box query;query.lo=glm::min(lo,hi);query.hi=glm::max(lo,hi);
    return impl_->AnyComponentBounds([&](const Box& box){return Overlap(query,box);});
}
std::size_t HairBodyCollision::TriangleCount() const{return impl_->triangles.size();}
std::size_t HairBodyCollision::VertexCount() const{return impl_->vertices.size();}
std::size_t HairBodyCollision::ComponentCount() const{return impl_->components.size();}
std::uint64_t HairBodyCollision::GeometryRevision() const{return impl_->geometryRevision;}
std::size_t HairBodyCollision::OpenComponentCount() const{return std::count_if(impl_->components.begin(),impl_->components.end(),[](const auto& c){return !c.closed;});}
double HairBodyCollision::TriangleBoneWeight(std::uint32_t triangle,std::uint32_t bone) const {
    const auto& data=*impl_;if(triangle>=data.triangles.size()||bone>=data.boneCount)return 0;
    double weight=0;for(const auto index:data.triangles[triangle].v){const auto& vertex=data.vertices[index];for(std::uint8_t k=0;k<vertex.count;++k)if(vertex.influences[k].bone==bone)weight+=vertex.influences[k].weight;}return weight/3.;
}
double HairBodyCollision::LastUpdateMilliseconds() const{return impl_->updateMilliseconds;}
const HairBodyQueryStats& HairBodyCollision::QueryStats() const{return impl_->stats;}
void HairBodyCollision::ResetQueryStats() const{impl_->stats={};}

} // namespace Model::detail
