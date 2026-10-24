#pragma once
#include "Render/Private/Pipeline/strand_native_ds_inputs.h"
#include <array>
#include <bit>
#include <limits>
#include <string>

namespace Render::TestData::NativeDsInputs {
// Root integration: include this header in strand_native_ds_contract_test.cpp
// and call NativeDsInputs::Run() once inside main's try block. No GPU fixtures
// or existing source-arithmetic golden rows need to be modified.
inline unsigned Run(){
    using PipelineDetail::NativeDs::ResolveInjectionInputs;
    unsigned checks=0;
    const auto require=[&](bool condition,const char* message){
        ++checks;if(!condition)throw std::runtime_error(std::string("DS input writer: ")+message);};
    const auto invalid=[&](auto invoke){
        bool caught=false;try{invoke();}catch(const std::invalid_argument&){caught=true;}
        require(caught,"invalid input escaped the host boundary");};
    require(PipelineDetail::NativeDs::SourceTraceLimit==32u,"source trace-factor cap is not32");

    // Literal Diana CPU-writer results: f32(.3333*419475)=139811.015625,
    // truncated count139811. The original fraction and compensated scale
    // have these fixed IEEE32 words, rather than a decimal1/3 or scale3.
    const auto diana=ResolveInjectionInputs(419475,419475,.3333f,1.5f,false,1);
    require(diana.count==139811u,"Diana low-quality prefix count differs from source vmulss/trunc");
    require(std::bit_cast<std::uint32_t>(diana.fraction)==0x3eaaa64bu,
        "Diana fraction lost the separate f32 conversions/division");
    require(std::bit_cast<std::uint32_t>(diana.rasterScale)==0x409003b1u,
        "Diana selected-prefix inverse-fraction width compensation differs");
    const auto high=ResolveInjectionInputs(419475,419475,.3333f,1.5f,false,2);
    require(high.count==419475&&high.fraction==1&&high.rasterScale==1.5f,
        "quality2 must keep the incoming list unless MinLodFactor is set");
    const auto force=ResolveInjectionInputs(419475,419475,.3333f,1.5f,true,2);
    require(force.count==diana.count&&force.fraction==diana.fraction&&force.rasterScale==diana.rasterScale,
        "MinLodFactor must select the rate candidate at high quality");
    const auto passed=ResolveInjectionInputs(419475,139811,.3333f,1.5f,false,99);
    require(passed.count==139811&&passed.rasterScale==diana.rasterScale,
        "incoming prefix must retain selected/total compensation at high quality");
    require(ResolveInjectionInputs(419475,200000,.3333f,1.5f,false,1).count==139811,
        "rate must multiply total original segments, not the incoming budget");
    require(ResolveInjectionInputs(419475,50000,.3333f,1.5f,true,99).count==50000,
        "rate selection cannot exceed a smaller independent incoming prefix");
    require(ResolveInjectionInputs(419475,200000,.3333f,1.5f,false,2).count==200000,
        "high quality incorrectly applies the rate to an independent incoming prefix");

    // Exercise a count where f32-before-trunc changes an actual segment:
    // double(float(.1))*16777219 truncates1677721, but vmulss yields1677722.
    require(ResolveInjectionInputs(16777219,16777219,.1f,1.f,true,2).count==1677722,
        "double multiplication changed the selected segment at a f32 integer boundary");
    for(float rate:std::array{0.f,std::numeric_limits<float>::denorm_min(),.009f,.01f}){
        const auto minimum=ResolveInjectionInputs(10000,10000,rate,1.5f,true,2);
        require(minimum.count==100&&minimum.fraction==.01f&&minimum.rasterScale==150.f,
            "zero/tiny authored rate must use the native one-percent floor");
    }

    // The comparison is strictly greater, and the fallback is applied before
    // multiplication by the authored width. These straddle f32 threshold1e-5.
    require(ResolveInjectionInputs(100000,1,1.f,1.5f,false,2).rasterScale==150000.f,
        "fraction exactly1e-5 must take the fixed100000 compensation");
    require(ResolveInjectionInputs(100001,1,1.f,1.5f,false,2).rasterScale==150000.f,
        "fraction below1e-5 must keep the finite fallback");
    require(ResolveInjectionInputs(99999,1,1.f,1.5f,false,2).rasterScale==149998.5f,
        "fraction just above1e-5 must use the reciprocal");

    const auto empty=ResolveInjectionInputs(0,0,.3333f,1.5f,false,1);
    require(empty.count==0&&empty.fraction==0&&empty.rasterScale==150000.f,
        "empty host input must remain finite and schedule zero work");
    const auto none=ResolveInjectionInputs(419475,0,.3333f,1.5f,true,1);
    require(none.count==0&&none.fraction==0&&none.rasterScale==150000.f,
        "empty incoming prefix must not silently inject all original segments");
    require(ResolveInjectionInputs(1,1,0.f,0.f,true,1).rasterScale==0,
        "zero authored width must remain zero when the rate truncates to zero");
    constexpr auto maximum=std::numeric_limits<std::uint32_t>::max();
    const auto full=ResolveInjectionInputs(maximum,maximum,1.f,1.5f,true,1);
    require(full.count==maximum&&full.fraction==1&&full.rasterScale==1.5f,
        "uint32 maximum must not cast rounded2^32 back to uint32");
    require(ResolveInjectionInputs(maximum,maximum-1,1.f,1.5f,true,1).count==maximum-1,
        "large incoming prefix must be clamped before a float-to-integer cast");

    // Conservation check across changing budgets is the practical purpose of
    // the writer. It catches integrating the selected list without its width
    // compensation, and deliberately does not recompute the selection formula.
    for(const auto budget:std::array<std::uint32_t,5>{100,1000,50000,139811,419475}){
        const auto value=ResolveInjectionInputs(419475,budget,1.f,1.5f,false,2);
        require(value.count==budget&&std::abs(double(value.fraction)*value.rasterScale-1.5)<3e-7,
            "selected fraction times compensated width must preserve authored raster coverage");
    }
    invalid([&]{ResolveInjectionInputs(0,1,.3333f,1.5f,false,1);});
    invalid([&]{ResolveInjectionInputs(100,101,.3333f,1.5f,false,1);});
    invalid([&]{ResolveInjectionInputs(100,100,-.1f,1.5f,false,1);});
    invalid([&]{ResolveInjectionInputs(100,100,std::numeric_limits<float>::infinity(),1.5f,false,1);});
    invalid([&]{ResolveInjectionInputs(100,100,std::numeric_limits<float>::quiet_NaN(),1.5f,false,1);});
    invalid([&]{ResolveInjectionInputs(100,100,.3333f,-1.f,false,1);});
    invalid([&]{ResolveInjectionInputs(100,100,.3333f,std::numeric_limits<float>::quiet_NaN(),false,1);});
    bool overflow=false;try{ResolveInjectionInputs(1,0,.3333f,std::numeric_limits<float>::max(),true,1);}
        catch(const std::overflow_error&){overflow=true;}
    require(overflow,"compensation overflow must not reach the GPU as an infinite dose");
    return checks;
}
} // namespace Render::TestData::NativeDsInputs
