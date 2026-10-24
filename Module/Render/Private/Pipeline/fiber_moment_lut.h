#pragma once
#include "Render/Public/Pipeline/strand_rendering.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Render::PipelineDetail {
inline constexpr unsigned FiberMomentResolution=16;
using FiberMomentTable=std::array<glm::vec4,FiberMomentResolution*FiberMomentResolution>;

// CPU transcription of strand.sdf/computeDSLUT target2, restricted to the
// current longitudinal-roughness slice. Native 64 theta x64 roughness x16 absorption RGBA16F dimensions
// are now recovered (hair-display-evidence.json). Our 16x16 slice remains
// a host quality choice and omits native roughness-neighbor interpolation;
// the per-slice directional sample counts follow the recovered generator.
inline FiberMomentTable BuildFiberMomentTable(const StrandMaterial& material) {
    constexpr double pi=3.14159265358979323846;
    // Sample the same effective CB24 hm_v uploaded to the scattering shaders.
    // Keep material immutable so the source mapping is never applied twice.
    const double rough=std::clamp(double(EffectiveStrandLongitudinalRoughness(material)),.18,.6);
    const double eta=std::max(double(material.ior),1.0001);
    const double f0=std::pow((eta-1)/(eta+1),2),tilt=material.cuticleShift;
    const double glint=std::clamp((1-double(material.azimuthalRoughness))*1.5,0.,1.);
    const auto gaussian=[](double x,double width) {
        const double t=x/width;return .3989422804014327/width*std::exp(-.5*t*t);
    };
    FiberMomentTable result{};
    const unsigned nPhi=std::max(1u,unsigned(128.0-64.0*rough));
    const unsigned nPolar=std::max(1u,unsigned(128.0-96.0*rough));
    for(unsigned y=0;y<FiberMomentResolution;++y) {
        const double absorption=(double(y)+.5)/FiberMomentResolution;
        for(unsigned x=0;x<FiberMomentResolution;++x) {
            const double sinO=(double(x)+.5)/FiberMomentResolution;
            const double cosO=std::sqrt(1-sinO*sinO);
            double forward=0,back=0,forwardR=0,forwardTT=0;
            unsigned forwardCount=0,backCount=0;
            for(unsigned j=0;j<nPolar;++j)for(unsigned i=0;i<nPhi;++i) {
                const double seed=double(j*nPhi+i)*.7548776268959045;
                const double jitter=seed-std::floor(seed);
                const double phi=2*pi*(double(i)+jitter)/nPhi;
                const double sinI=1-2*(double(j)+jitter)/nPolar;
                const double cosI=std::sqrt(std::max(1-sinI*sinI,0.));
                const double lx=std::cos(phi)*cosI;
                const double cosPhi=std::clamp(lx*cosO/std::sqrt(cosI*cosI*cosO*cosO+.0001),-1.,1.);
                const double sum=sinI+sinO;
                const double cosD=std::sqrt(std::max(.5+.5*(cosI*cosO+sinI*sinO),.000001));
                const double etaPrime=std::sqrt(std::max(eta*eta-1+cosD*cosD,.000001))/cosD;
                const double cosHalf=std::sqrt(std::max(.5+.5*(lx*cosO+sinI*sinO),0.));
                const double fr=f0+(1-f0)*std::pow(1-cosHalf,5);
                const double nr=.25*std::sqrt(std::max(.5+.5*cosPhi,0.))*fr;
                const double r=gaussian(sum+tilt,rough)*nr;
                const double cosGamma=std::sqrt(std::max(.5+.5*cosPhi,0.)),u=1-cosGamma;
                const double h=(1-.15/etaPrime-.5*u*u+(u*u+u)/etaPrime)*cosGamma;
                const double refracted=std::clamp(h/etaPrime,-1.,1.);
                const double chord=2-2*refracted*refracted;
                const double trans=std::exp(-2*absorption*absorption*chord);
                const double ct=std::sqrt(std::clamp(1-h*h,0.,1.))*cosD;
                const double oneMinusF=(1-f0)*(1-std::pow(1-ct,5));
                const double ntt=std::exp2(-5.741926193237305-5.2658371925354*cosPhi);
                const double ttFactor=oneMinusF*oneMinusF*ntt*trans;
                const double tt=gaussian(sum-.5*tilt,.5*rough)*ttFactor;
                const double ftrt=f0+(1-f0)*std::pow(1-.5*cosD,5);
                const double ht=std::clamp(.8660254037844386/etaPrime,-1.,1.);
                const double trtTrans=std::exp(-2*absorption*absorption*(2-2*ht*ht));
                const double ntrt=glint*std::exp2(glint*(24.525815963745117*cosPhi-24.208423614501953));
                const double trt=gaussian(sum-1.5*tilt,2*rough)*
                    ftrt*(1-ftrt)*(1-ftrt)*trtTrans*trtTrans*ntrt;
                const double weighted=std::max(r+tt+trt,0.)*(4*pi);
                if(lx>0) {back+=weighted;++backCount;}
                else {forward+=weighted;forwardR+=nr;forwardTT+=ttFactor;++forwardCount;}
            }
            const auto saturate=[](double value){return float(std::clamp(value,0.,1.));};
            result[y*FiberMomentResolution+x]={
                saturate(.5*forward/std::max(forwardCount,1u)),
                saturate(.5*back/std::max(backCount,1u)),
                saturate(.5*forwardR/std::max(forwardCount,1u)),
                saturate(.5*forwardTT/std::max(forwardCount,1u))};
        }
    }
    return result;
}
inline const FiberMomentTable& CachedFiberMomentTable(const StrandMaterial& material) {
    // Equivalent source-mapped/raw-v inputs must share the same slice; the
    // policy flag alone does not change the generator after this conversion.
    const std::array<float,4> key{material.ior,EffectiveStrandLongitudinalRoughness(material),material.azimuthalRoughness,material.cuticleShift};
    struct Entry {std::array<float,4> key;FiberMomentTable table;};
    static thread_local std::vector<Entry> cache;
    for(const auto& entry:cache)if(entry.key==key)return entry.table;
    if(cache.size()>=8)cache.erase(cache.begin());
    cache.push_back({key,BuildFiberMomentTable(material)});
    return cache.back().table;
}
} // namespace Render::PipelineDetail
