#include "Render/Private/Pipeline/strand_native_oit_contract.h"
#include "Render/Test/strand_native_oit_goldens.h"
#include <functional>
#include <iostream>
#include <string>

using namespace Render::PipelineDetail::NativeOit;
namespace Golden=Render::TestData::NativeOitGolden;
namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
float F(std::uint32_t u){return std::bit_cast<float>(u);}
Node N(const std::array<std::uint32_t,2>& a){return {a[0],a[1]};}
std::array<std::uint32_t,16> Hierarchy(const std::array<std::uint32_t,64>& row){
    std::array<Node,4> global;global.fill(Empty);std::array<unsigned,2> consumed{};unsigned closed=0,offset=8;
    if(row[0]==4){for(unsigned i=0;i<row[1];++i)GlobalInsert(global,{row[8+i*2],row[9+i*2]});consumed[0]=row[1];}
    else for(unsigned object=0;object<2;++object){
        std::array<Node,8> local;local.fill(Empty);Resolved q;const unsigned count=row[1],size=row[2+object];
        for(unsigned i=0;i<size;++i){const Node node{row[offset+i*2],row[offset+i*2+1]};++consumed[object];
            InnerInsert(std::span(local).first(count),node);
            if(row[6]){q=Resolve(std::span(local).first(count));if(CloseBucket(q)){closed|=1u<<object;break;}}}
        offset+=size*2;if(!row[6])q=Resolve(std::span(local).first(count));
        if(CloseBucket(q))closed|=1u<<object;Opacity(q,F(row[4+object]));
        if(q.transmission<1)GlobalInsert(global,Pack(q.reverseZ,q.color,q.transmission));
    }
    std::array<std::uint32_t,16> result{};for(unsigned i=0;i<4;++i){result[i*2]=global[i].lo;result[i*2+1]=global[i].hi;}
    const auto q=Resolve(global);for(unsigned i=0;i<3;++i)result[8+i]=std::bit_cast<std::uint32_t>(q.color[i]);
    result[11]=std::bit_cast<std::uint32_t>(q.transmission);result[12]=std::bit_cast<std::uint32_t>(q.reverseZ);
    result[13]=closed;result[14]=consumed[0];result[15]=consumed[1];return result;
}
}
int main(){try{
    // Exhaustive finite half roundtrip catches subnormal/tie conversion errors
    // independently of the compact random source pack samples.
    unsigned finiteHalf=0;for(unsigned bits=0;bits<65536;++bits)if((bits&0x7c00u)!=0x7c00u){
        Check(FloatToHalf(HalfToFloat(std::uint16_t(bits)))==bits,"Finite IEEE half roundtrip failed");++finiteHalf;}
    Check(Empty!=Pack(0,{0,0,0},1),"Special native empty word was replaced by ordinary packed T1");
    unsigned index=0;for(const auto& sample:Golden::Packs){const auto& a=sample.input;
        Check(Pack(F(a[0]),Scale(F(a[1]),{F(a[2]),F(a[3]),F(a[4])}),Add(1,-F(a[1])))==N(sample.node),
            "Literal SSA pack mismatch at"+std::to_string(index++));}
    index=0;for(const auto& sample:Golden::Merges)Check(Merge(N(sample.front),N(sample.back))==N(sample.node),
        "Literal SSA full-DAG tail mismatch at"+std::to_string(index++));
    index=0;unsigned deposits=0;for(const auto& sample:Golden::WuCases){const auto& a=sample.input;
        WuSegment segment{{F(a[0]),F(a[1])},{F(a[2]),F(a[3])},{F(a[4]),F(a[5])},{F(a[6]),F(a[7])},
            {F(a[8]),F(a[9]),F(a[10])},{F(a[11]),F(a[12]),F(a[13])}};
        const auto actual=Wu(segment,[&](int,int){return F(sample.opaque);});
        Check(actual.size()==sample.count,"Literal SSA Wu deposit count mismatch at"+std::to_string(index));
        for(unsigned i=0;i<sample.count;++i){const auto& expected=Golden::Deposits[sample.first+i];const auto& q=actual[i];
            Check(q.pixel==std::array<int,2>{expected.x,expected.y}&&q.node==N(expected.node),
                "Literal SSA Wu packed coordinate/coverage mismatch at"+std::to_string(index)+":"+std::to_string(i));
            const std::array<float,6> values{q.color[0],q.color[1],q.color[2],q.transmission,q.reverseZ,q.progress};
            for(unsigned j=0;j<6;++j)Check(std::bit_cast<std::uint32_t>(values[j])==expected.values[j],
                "Literal SSA Wu f32 recurrence mismatch at"+std::to_string(index)+":"+std::to_string(i));++deposits;}
        ++index;
    }
    index=0;for(const auto& sample:Golden::Hierarchies){const auto actual=Hierarchy(sample.input);
        Check(actual==sample.expected,"Local4/8 -> object -> global4 hierarchy mismatch at"+std::to_string(index++));}
    Check(Golden::Hierarchies[3].expected!=Golden::Hierarchies[4].expected,
        "Global same-high ordering negative control does not distinguish reversed object order");
    Check(Golden::Hierarchies[7].expected[14]==1&&Golden::Hierarchies[8].expected[14]==2,
        "Bucket threshold negative control fails to distinguish Tbyte25 from26");
    Resolved opacity{{.2f,.3f,.4f},.125f,.5f};Opacity(opacity,.5f);
    Check(opacity.transmission==.625f&&opacity.transmission!=1.f-.5f*(1.f-.125f),
        "Native literal global transparency was replaced by conventional alpha scaling");
    // Four capacity-preserving fragments must sort by FULL unsigned64 key,
    // independently of insertion order. No such claim is made for merged tails.
    std::array<unsigned,4> order{0,1,2,3};unsigned permutations=0;
    do{std::array<Node,4> layers;layers.fill(Empty);std::array<std::uint64_t,4> expected;
        for(unsigned i=0;i<4;++i){const auto n=N(Golden::Packs[i].node);expected[i]=(std::uint64_t(n.hi)<<32)|n.lo;}
        std::sort(expected.begin(),expected.end(),std::greater<>());
        for(unsigned i:order)InnerInsert(layers,N(Golden::Packs[i].node));
        for(unsigned i=0;i<4;++i)Check(((std::uint64_t(layers[i].hi)<<32)|layers[i].lo)==expected[i],"Inner full-key permutation sort failed");++permutations;
    }while(std::next_permutation(order.begin(),order.end()));
    std::cout<<"Native OIT CPU: "<<finiteHalf<<" finite halves, "<<Golden::Packs.size()<<" literal pack, "<<Golden::Merges.size()
        <<" literal merge, "<<Golden::WuCases.size()<<" Wu cases/"<<deposits<<" deposits, "<<Golden::Hierarchies.size()
        <<" two-level/tie/opacity/threshold cases and "<<permutations<<" full-key permutations passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
