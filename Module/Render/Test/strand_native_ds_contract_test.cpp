#include "Render/Test/strand_native_ds_math_test.h"
#include "Render/Test/strand_native_ds_inputs_test.h"
#include <iostream>

namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
}
int main(){try{using namespace Render::PipelineDetail::NativeDs;using namespace Render::TestData::NativeDsAudit;
    const auto inputChecks=Render::TestData::NativeDsInputs::Run();
    namespace Golden=Render::TestData::NativeDsGolden;unsigned index=0;
    for(const auto& row:Golden::Rows){const auto actual=Execute(row.input);for(unsigned i=0;i<16;++i)if(actual[i]!=row.expected[i]){
        std::cerr<<"DS SSA row="<<index<<" kind="<<row.input[0]<<" word="<<i<<" actual=0x"<<std::hex<<actual[i]<<" expected=0x"<<row.expected[i]<<std::dec<<'\n';
        throw std::runtime_error("Native Texture DSVolume scalar SSA mismatch");}++index;}
    for(const auto& golden:Golden::Traces){const auto& p=golden.plan;TracePlan plan;
        for(unsigned i=0;i<3;++i){plan.entry[i]=F(p[i]);plan.step[i]=F(p[3+i]);plan.minimum[i]=F(p[6+i]);plan.inverseExtent[i]=F(p[9+i]);plan.actual[i]=p[12+i];}
        plan.resourceR=p[15];plan.baseCount=F(p[16]);plan.baseDs=F(p[17]);plan.voxelX=F(p[18]);plan.delta=F(p[19]);plan.traceLimit=p[20];plan.interpolationSamples=p[21];
        const auto result=TraceSchedule(plan,[&](const auto&){return Vec2{F(p[22]),F(p[23])};});
        Check(!result.invalidLod&&result.reads.size()==golden.count,"DS new-cell/factor schedule read count differs from source");
        Check(U(result.ng[0])==golden.ng[0]&&U(result.ng[1])==golden.ng[1]&&U(result.finalCursor)==golden.finalCursor,"DS four-attempt source N/G or cursor differs");
        for(unsigned i=0;i<golden.count;++i){const auto& a=result.reads[i];const auto& b=Golden::Reads[golden.first+i];
            Check(U(a.cursor)==b.cursorFactorWeight[0]&&U(a.factor)==b.cursorFactorWeight[1]&&U(a.weight)==b.cursorFactorWeight[2]&&a.base==b.base&&
                a.level==b.lod&&a.filtered==bool(b.filtered),"DS source old-factor/new-cursor/LOD or filtering selector differs");
            for(unsigned axis=0;axis<3;++axis)Check(U(a.uv[axis])==b.uv[axis],"DS normalized TextureUV f32 operation order differs");}
    }
    // Packed AtomicAdd is whole32 and may carry low24 into G. This is an
    // arithmetic boundary test, not evidence that Diana overflows in practice.
    std::uint32_t raw=LowMask;raw+=1;Check(Copy(raw)[0]==0&&Copy(raw)[1]>0,"Whole-word density carry was silently saturated");
    std::array<Vec2,8> children{};children[0]={0,1};Check(Mip(children)==Vec2{0,.125f},"Native Texture G mip was replaced by max");
    Check(LoadInBounds({40,0,0},128,1),"Odd logical actual81/R128 tail mip coordinate was clipped to actual>>LOD");
    Check(!LoadInBounds({32,0,0},65,1),"Odd physical R65 mip was incorrectly ceil-sized");
    const auto border=ClampLinearAxis(1,32);Check(border.low==31&&border.high==31,"Explicit filtered clamp-edge policy differs from native Load-zero bounds");
    const auto filtered=ClampTrilinear(Vec3{.5f,.25f,.125f},8,[](UVec3 p){return Vec2{float(p[0]+10*p[1]+100*p[2]),.125f};});
    Check(filtered==Vec2{68.5f,.125f},"Host clamp-trilinear sampler failed independent affine field");
    Check(!TryLod(1,.5f,1).valid&&TryLod(1,.71f,1).valid,"Negative native float-to-uint LOD domain was not explicit");
    const auto splat=Intpij(4,{.5f,.5f,.5f});std::uint32_t total=0;for(auto d:splat)total+=d;
    Check(total==0&&Dose(4)==4,"Independent eight-way roundEven quantization was replaced by post-normalized conservative splat");
    std::cout<<"Native Texture DS CPU: "<<Golden::Rows.size()<<" literal scalar SSA rows, "<<Golden::Traces.size()
        <<" source f32 trace schedules, "<<inputChecks<<" native CPU injection checks, odd physical/logical mips, explicit sampler boundaries and packed carry passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
