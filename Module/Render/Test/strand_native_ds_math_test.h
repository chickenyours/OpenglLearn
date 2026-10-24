#pragma once
#include "Render/Private/Pipeline/strand_native_ds_contract.h"
#include "Render/Private/Pipeline/strand_native_ds_math.h"
#include "Render/Test/strand_native_ds_goldens.h"
#include <string>

namespace Render::TestData::NativeDsAudit {
using Input=std::array<std::uint32_t,64>;using Output=std::array<std::uint32_t,16>;
inline float F(std::uint32_t u){return std::bit_cast<float>(u);}
inline std::uint32_t U(float x){return std::bit_cast<std::uint32_t>(x);}
inline Output Execute(const Input& row){using namespace PipelineDetail::NativeDs;Output out{};
    switch(row[0]){
    case 0:{const float q=DoseTotal(F(row[1]),F(row[2]),F(row[3]),F(row[4]),F(row[5]));out[0]=U(q);out[1]=Dose(q);break;}
    case 1:{const auto q=Intpij(F(row[1]),{F(row[2]),F(row[3]),F(row[4])});std::copy(q.begin(),q.end(),out.begin());break;}
    case 2:{const auto q=Copy(row[1]);out[0]=U(q[0]);out[1]=U(q[1]);break;}
    case 3:{std::array<Vec2,8> children;for(unsigned i=0;i<8;++i)children[i]={F(row[1+i*2]),F(row[2+i*2])};
        const auto q=Mip(children);out[0]=U(q[0]);out[1]=U(q[1]);break;}
    case 4:{const auto q=MakeInfo({0,0,0},{F(row[1]),F(row[2]),F(row[3])},F(row[4]),row[5]);
        for(unsigned i=0;i<3;++i){out[i]=q.actual[i];out[3+i]=U(q.voxel[i]);out[6+i]=U(q.inverseExtent[i]);}break;}
    case 5:{const auto q=TryLod(F(row[1]),F(row[2]),F(row[3]));const float n=NextFactor(F(row[1]),F(row[4]),row[5]);
        out[0]=U(q.weight);out[1]=U(q.rounded);out[2]=q.valid;out[3]=q.level;out[4]=U(n);out[5]=U(Add(n,F(row[6])));out[6]=F(row[6])<float(row[7]);break;}
    case 6:{const auto q=TextureUV({F(row[1]),F(row[2]),F(row[3])},{row[4],row[5],row[6]},row[7]);
        for(unsigned i=0;i<3;++i)out[i]=U(q[i]);break;}
    case 7:{std::array<Vec2,4> reads;std::array<float,4> weights;for(unsigned i=0;i<4;++i){reads[i]={F(row[1+i*2]),F(row[2+i*2])};weights[i]=F(row[9+i]);}
        Vec2 ng{F(row[13]),F(row[14])};Accumulate4(reads,weights,ng);out[0]=U(ng[0]);out[1]=U(ng[1]);break;}
    default:throw std::runtime_error("Unknown native DS arithmetic fixture kind");}
    return out;
}
inline std::string Shader(){return std::string("#version 450 core\n")+PipelineDetail::StrandNativeDsMathGLSL()+R"GLSL(
layout(local_size_x=64) in;
layout(std430,set=2,binding=0) readonly buffer DsAuditInput {uint dsInput[];};
layout(std430,set=2,binding=1) writeonly buffer DsAuditOutput {uint dsOutput[];};
layout(push_constant) uniform DsAuditParameters {uvec4 dsAuditParameters;};
uint Word(uint row,uint word){return dsInput[row*64u+word];}
float Float(uint row,uint word){return uintBitsToFloat(Word(row,word));}
void main(){uint row=gl_GlobalInvocationID.x;if(row>=dsAuditParameters.x)return;
    uint outWords[16];for(int i=0;i<16;++i)outWords[i]=0u;uint kind=Word(row,0u);
    if(kind==0u){float q=StrandNativeDsDoseTotal(Float(row,1u),Float(row,2u),Float(row,3u),Float(row,4u),Float(row,5u));outWords[0]=floatBitsToUint(q);outWords[1]=StrandNativeDsDose(q);}
    if(kind==1u){uint q[8];StrandNativeDsIntpij(Float(row,1u),vec3(Float(row,2u),Float(row,3u),Float(row,4u)),q);for(int i=0;i<8;++i)outWords[i]=q[i];}
    if(kind==2u){vec2 q=StrandNativeDsCopy(Word(row,1u));outWords[0]=floatBitsToUint(q.x);outWords[1]=floatBitsToUint(q.y);}
    if(kind==3u){vec2 children[8];for(uint i=0u;i<8u;++i)children[i]=vec2(Float(row,1u+i*2u),Float(row,2u+i*2u));
        vec2 q=StrandNativeDsMip(children);outWords[0]=floatBitsToUint(q.x);outWords[1]=floatBitsToUint(q.y);}
    if(kind==4u){vec3 voxel,inverseExtent;uvec3 q=StrandNativeDsResolution(vec3(Float(row,1u),Float(row,2u),Float(row,3u)),Float(row,4u),Word(row,5u),voxel,inverseExtent);
        for(int i=0;i<3;++i){outWords[i]=q[i];outWords[3+i]=floatBitsToUint(voxel[i]);outWords[6+i]=floatBitsToUint(inverseExtent[i]);}}
    if(kind==5u){float weight,rounded;uint lod;bool valid=StrandNativeDsTryLod(Float(row,1u),Float(row,2u),Float(row,3u),weight,lod,rounded);
        float next=StrandNativeDsNextFactor(Float(row,1u),Float(row,4u),Word(row,5u));outWords[0]=floatBitsToUint(weight);outWords[1]=floatBitsToUint(rounded);
        outWords[2]=valid?1u:0u;outWords[3]=lod;outWords[4]=floatBitsToUint(next);outWords[5]=floatBitsToUint(StrandNativeDsAdd(next,Float(row,6u)));
        outWords[6]=Float(row,6u)<float(Word(row,7u))?1u:0u;}
    if(kind==6u){vec3 q=StrandNativeDsTextureUV(vec3(Float(row,1u),Float(row,2u),Float(row,3u)),uvec3(Word(row,4u),Word(row,5u),Word(row,6u)),Word(row,7u));
        for(int i=0;i<3;++i)outWords[i]=floatBitsToUint(q[i]);}
    if(kind==7u){vec2 reads[4];vec4 weights;for(uint i=0u;i<4u;++i){reads[i]=vec2(Float(row,1u+i*2u),Float(row,2u+i*2u));weights[i]=Float(row,9u+i);}
        vec2 ng=vec2(Float(row,13u),Float(row,14u));StrandNativeDsAccumulate4(reads,weights,ng);outWords[0]=floatBitsToUint(ng.x);outWords[1]=floatBitsToUint(ng.y);}
    for(uint i=0u;i<16u;++i)dsOutput[row*16u+i]=outWords[i];
}
)GLSL";}
} // namespace Render::TestData::NativeDsAudit
