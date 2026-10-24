#pragma once
#include <string>
#include "Render/Private/Pipeline/fiber_direct_shaders.h"
#include "Render/Private/Pipeline/fiber_native_moment_shaders.h"

namespace Render::PipelineDetail {
// The absorption fit and three longitudinal widths/offsets are traced to the
// shipped Strands DXIL (see Asset/Diana/hair-scattering-evidence.json).
// A native Ntt lookup can be supplied by the material pass; generic callers
// without that table retain computeDSLUT's explicitly identified analytic fit.
// the local inter-fiber closure uses its recovered hemisphere integrals, while
// remaining distinct from the source DSVolume transport algorithm.
inline std::string FiberScatteringGLSL() { return FiberNativeMomentGLSL()+std::string(R"GLSL(
const float FiberPi=3.14159265359;
#ifndef FIBER_NTT_LOOKUP
float FiberNttAzimuth(float sinThetaI,float cosPhi,float azimuthalRoughness) {
    return exp2(-5.741926193237305-5.2658371925354*cosPhi);
}
#endif
vec3 FiberSafe(vec3 v,vec3 fallbackValue) {
    float l=dot(v,v);return l>1e-12?v*inversesqrt(l):fallbackValue;
}
float FiberGaussian(float x,float width) {
    float w=max(width,.02);float u=x/w;
    return exp(-.5*u*u)*.3989422804/w;
}
vec4 FiberMoments(float theta,float absorption) {
#ifdef FIBER_NATIVE_MOMENT_LUT
    // Opt-in immutable full native table. The existing 16x16 slice remains
    // byte-for-byte below for generic materials and explicit legacy A/B.
    if(FIBER_NATIVE_MOMENT_ACTIVE)
        return FiberNativeMoments(theta,FIBER_NATIVE_MOMENT_ROUGHNESS,absorption);
#endif
#ifdef FIBER_MOMENT_LUT
    // A current-material roughness slice of the recovered computeDSLUT.
    vec2 p=clamp(vec2(theta,absorption)*16.0-.5,vec2(0.0),vec2(15.0));
    ivec2 a=ivec2(floor(p)),b=min(a+ivec2(1),ivec2(15));
    vec2 f=fract(p);
    return mix(mix(strandMomentLut[a.y*16+a.x],strandMomentLut[a.y*16+b.x],f.x),
        mix(strandMomentLut[b.y*16+a.x],strandMomentLut[b.y*16+b.x],f.x),f.y);
#else
    // Helper-only users have no precomputed moment resource. Production
    // strands always supply FIBER_MOMENT_LUT and do not take this fallback.
    return vec4(0.0);
#endif
}
vec3 FiberAbsorption(vec3 base,float scale) {
    // Source %595..603, %1449..1457: exp2(-.039981458*log2(C)^2*path).
    // Return the equivalent natural-exponential coefficient.
    vec3 logColor=log2(clamp(base,vec3(.001),vec3(1.0)));
    return logColor*logColor*(.03998145833611488*.69314718056)*max(scale,0.0);
}
float FiberFresnel(float cosine,float eta) {
    float f0=(eta-1.0)/(eta+1.0);f0*=f0;
    return f0+(1.0-f0)*pow(1.0-clamp(cosine,0.0,1.0),5.0);
}
vec3 FiberEnvironmentTransmission(vec3 base,vec3 tangent,vec4 fiber,vec3 positiveCounts,vec3 negativeCounts,float density) {
    vec3 q=log2(clamp(base,vec3(.001),vec3(1.0)));
    q=q*q*(.01385651808232069*sqrt(max(fiber.w,0.0)));
    vec3 result=vec3(0.0);
    for(int i=0;i<3;++i) {
        float theta=abs(tangent[i]);
        vec3 af=clamp(vec3(FiberMoments(theta,q.r).r,FiberMoments(theta,q.g).r,FiberMoments(theta,q.b).r),vec3(.0001),vec3(.99));
        result+=.5*(pow(af,vec3(max(positiveCounts[i],0.0)*max(density,0.0)))+
            pow(af,vec3(max(negativeCounts[i],0.0)*max(density,0.0))));
    }
    return result/3.0;
}
vec3 FiberScatter(vec3 base,vec3 tangent,vec3 view,vec3 light,vec4 fiber,
    float primaryRoughness,float cuticleShift,vec3 lobeStrength,
    float multipleStrength,float density,float pathCount,bool spatial) {
    vec3 T=FiberSafe(tangent,vec3(0.0,-1.0,0.0));
    vec3 V=FiberSafe(view,vec3(0.0,0.0,1.0)),L=FiberSafe(light,vec3(0.0,1.0,0.0));
    float sinI=clamp(dot(T,L),-.9999,.9999),sinO=clamp(dot(T,V),-.9999,.9999);
    float cosI=sqrt(max(1.0-sinI*sinI,.0001));
    float cosO=sqrt(max(1.0-sinO*sinO,.0001));
    float sum=sinI+sinO;
    vec3 lp=(L-T*sinI)/cosI,vp=(V-T*sinO)/cosO;
    float cosPhi=clamp(dot(lp,vp),-1.0,1.0);
    float cosD=sqrt(max(.5+.5*(cosI*cosO+sinI*sinO),.0001));
    float eta=max(fiber.x,1.0001);
    float etaPrime=sqrt(max(eta*eta-1.0+cosD*cosD,.0001))/cosD;
    // Source computeDSLUT's refracted TT cylinder chord (same approximation
    // used by the material shader before its Ntt lookup).
    float cosGamma=sqrt(max(.5+.5*cosPhi,0.0)),u=1.0-cosGamma;
    float h=(1.0-.15/etaPrime-.5*u*u+(u*u+u)/etaPrime)*cosGamma;
    float refracted=clamp(h/etaPrime,-1.0,1.0);
    float path=2.0-2.0*refracted*refracted;
    vec3 sigma=FiberAbsorption(base,fiber.w);
    vec3 transmittance=exp(-sigma*path);
    float fresnelR=FiberFresnel(sqrt(max(.5+.5*dot(V,L),0.0)),eta);
    // Transmission crosses the refracted cylinder interface, rather than the
    // reflection half vector (which degenerates for exactly opposed V and L).
    float fresnel=FiberFresnel(cosD*sqrt(clamp(1.0-h*h,0.0,1.0)),eta);
    vec3 attenuationR=vec3(fresnelR);
    vec3 attenuationTT=(1.0-fresnel)*(1.0-fresnel)*transmittance;
    float fresnelTRT=FiberFresnel(.5*cosD,eta);
    float ht=clamp(.8660254037844386/etaPrime,-1.0,1.0);
    vec3 trtTransmission=exp(-sigma*(2.0-2.0*ht*ht));
    vec3 attenuationTRT=(1.0-fresnelTRT)*(1.0-fresnelTRT)*fresnelTRT*trtTransmission*trtTransmission;
    vec3 attenuationTail=attenuationTRT*(fresnelTRT*trtTransmission)/
        max(vec3(1.0)-fresnelTRT*trtTransmission,vec3(.001));
    // Source independently controls the R width with hm_primaryRoughness;
    // TT is half the longitudinal width and TRT twice that width.
    float longWidth=max(fiber.y,.02);
    float widthR=clamp(2.0*longWidth*primaryRoughness,.02,1.0);
    float mr=FiberGaussian(sum+cuticleShift,widthR);
    float mtt=FiberGaussian(sum-.5*cuticleShift,.5*longWidth);
    float mtrt=FiberGaussian(sum-1.5*cuticleShift,2.0*longWidth);
    float nr=.25*sqrt(max(.5+.5*cosPhi,0.0));
    float ntt=FiberNttAzimuth(sinI,cosPhi,fiber.z);
    float glint=clamp((1.0-fiber.z)*1.5,0.0,1.0);
    float ntrt=glint*exp2(glint*(24.525815963745117*cosPhi-24.208423614501953));
    vec3 weights=clamp(lobeStrength,vec3(0.0),vec3(1.0));
    vec3 single=attenuationR*mr*nr*weights.x+attenuationTT*mtt*ntt*weights.y+
        attenuationTRT*mtrt*ntrt*weights.z+attenuationTail*mtrt/(2.0*FiberPi)*weights.z;
    // Recovered DSLUT consumer coordinates: abs(dot(T,L)), clamped hm_v
    // (already sliced on CPU), and log2(base)^2*.01385651808.
    vec3 q=log2(clamp(base,vec3(.001),vec3(1.0)));
    q=q*q*(.01385651808232069*sqrt(max(fiber.w,0.0)));
    vec4 momentR=FiberMoments(abs(sinI),q.r),momentG=FiberMoments(abs(sinI),q.g),momentB=FiberMoments(abs(sinI),q.b);
    vec3 af=clamp(vec3(momentR.r,momentG.r,momentB.r),vec3(0.0),vec3(.99));
    vec3 ab=clamp(vec3(momentR.g,momentG.g,momentB.g),vec3(0.0),vec3(.99));
    vec3 af2=af*af,den=max(vec3(1.0)-af2,vec3(.0199));
    // Source local backward-scattering coefficient (StrandsDualIBLOpt
    // %2322..2336). It replaces an invented escape probability. Native
    // DSVolume path counts/variance are still replaced by effective density.
    vec3 back=ab*af2/den+ab*ab*ab*af2/(den*den*den);
    vec3 recycled=clamp(af2+back,vec3(0.0),vec3(1.0));
    float amount=clamp(multipleStrength,0.0,1.0);
    float count=spatial?max(pathCount,0.0)*max(density,0.0):max(density,0.0);
    float encountered=1.0-exp(-count);
    float broad=FiberGaussian(sum,max(.7,longWidth))/(2.0*FiberPi);
    // Source global forward transport is pow(af,N), where N is the integrated
    // DSVolume fiber count. Apply the material density control exactly once.
    vec3 forward=spatial?pow(max(af,vec3(.0001)),vec3(count)):vec3(1.0);
    vec3 multiple=recycled*broad*encountered*forward;
    float visible=spatial?exp(-count):1.0-amount*encountered;
    return max(single*visible+amount*multiple,vec3(0.0))*cosI;
}
// Native StrandsDualIBLOpt sine-domain Gaussian. The source intentionally
// saturates 2*pi*variance in its normalization; a conventional Gaussian would
// suppress its broad multiple-scattering response. Variance is not radians^2.
vec3 FiberIblGaussian(vec3 x,vec3 variance) {
    vec3 v=max(variance,vec3(.0001));
    return exp(-.5*x*x/v)/sqrt(clamp(2.0*FiberPi*v,vec3(0.0),vec3(1.0))+.0001);
}
float FiberIblGaussian(float x,float variance) {
    return FiberIblGaussian(vec3(x),vec3(variance)).r;
}
vec3 FiberIblAbsorptionCoordinate(vec3 base,float scale) {
    vec3 q=log2(max(base,vec3(.001)));
    // The moment sampler clamps its coordinate, while native single-fiber
    // attenuation uses this coefficient before that clamp.
    return q*q*(.01385651808232069*sqrt(max(scale,0.0)));
}
float FiberIblBackscatterGain(float count,float scale,float visibility) {
    float root=sqrt(max(scale,0.0));
    return root+(scale*scale-root)*visibility*exp(-max(count-1.0,0.0));
}
// Native IBL single-scattering path has fixed eta=1.5/f0=.04, three lobes,
// and gain on TT only. The material pass provides the original Ntt RG lookup;
// generic callers without it retain the clearly separated generator-fit fallback.
vec3 FiberIblSingle(vec3 base,vec3 T,vec3 V,vec3 L,vec4 fiber,float primaryRoughness,
    float tilt,vec3 strengths,float gain) {
    float si=clamp(dot(T,L),-.9999,.9999),so=clamp(dot(T,V),-.9999,.9999);
    float ci=sqrt(max(1.0-si*si,.0001)),co=sqrt(max(1.0-so*so,.0001));
    float c=sqrt(max(.5+.5*(ci*co+si*so),.0001)),delta=si+so;
    vec3 lp=L-T*si,vp=V-T*so;
    float p=clamp(dot(lp,vp)*inversesqrt(dot(lp,lp)*dot(vp,vp)+.0001),-1.0,1.0);
    float h=sqrt(clamp(.5+.5*p,0.0,1.0));
    float etaPrime=sqrt(c*c+1.25)/c;
    float width=max(fiber.y,.02),rWidth=clamp(2.0*width*primaryRoughness,1.0/255.0,1.0);
    float halfCos=sqrt(clamp(.5+.5*dot(V,L),0.0,1.0));
    float fr=.04+.96*pow(1.0-halfCos,5.0);
    float mr=.0997355729342/rWidth*exp(-.5*pow((delta+tilt)/rWidth,2.0));
    vec3 r=vec3(mr*h*fr)*strengths.x;
    float u=1.0-h,g=(1.0-.15/etaPrime-.5*u*u+(u*u+u)/etaPrime)*h;
    float path=2.0-2.0*pow(clamp(g/etaPrime,-1.0,1.0),2.0);
    float oneMinusF=.96*(1.0-pow(1.0-c*sqrt(clamp(1.0-g*g,0.0,1.0)),5.0));
    float mtt=.797884583473/width*exp(-.5*pow(2.0*(delta-.5*tilt)/width,2.0));
    vec3 absorption=FiberIblAbsorptionCoordinate(base,fiber.w);
    float ntt=FiberNttAzimuth(si,p,fiber.z);
    vec3 tt=gain*oneMinusF*oneMinusF*mtt*ntt*exp(-2.0*absorption*path)*strengths.y;
    float f=.04+.96*pow(1.0-.5*c,5.0);
    float pathTrt=2.0-2.0*pow(clamp(.86602538824/etaPrime,-1.0,1.0),2.0);
    float glint=clamp(1.5*(1.0-max(fiber.z,.02)),0.0,1.0);
    float ntrt=glint*exp2(glint*(24.5258159637*p-24.2084236145));
    float mtrt=.199471145868/width*exp(-.5*pow((delta-1.5*tilt)/(2.0*width),2.0));
    vec3 trt=(1.0-f)*(1.0-f)*f*mtrt*ntrt*exp(-4.0*absorption*pathTrt)*strengths.z;
    return max(r+tt+trt,vec3(0.0));
}
// Transcribed native IBL single/local-back/global-forward/propagated-back
// closure. Native epsilon/normalization and RGB weights are kept separately
// from the host's directional Sky and optical-path adapters.
vec3 FiberIblDirection(vec3 base,vec3 tangent,vec3 view,vec3 light,vec4 fiber,
    float primaryRoughness,float tilt,vec3 lobeStrength,float multipleStrength,
    float pathCount,float density,float backscatterScale,float visibility) {
    vec3 T=FiberSafe(tangent,vec3(0.0,-1.0,0.0)),V=FiberSafe(view,vec3(0.0,0.0,1.0));
    vec3 L=FiberSafe(light,vec3(1.0,0.0,0.0));
    float si=clamp(dot(T,L),-.9999,.9999),so=clamp(dot(T,V),-.9999,.9999);
    float ci=sqrt(max(1.0-si*si,.0001)),co=sqrt(max(1.0-so*so,.0001));
    float c=sqrt(max(.5+.5*(ci*co+si*so),.0001)),delta=si+so;
    float count=max(pathCount,0.0)*max(density,0.0),visible=clamp(visibility,0.0,1.0);
    float gain=FiberIblBackscatterGain(count,backscatterScale,visible);
    vec3 single=FiberIblSingle(base,T,V,L,fiber,primaryRoughness,tilt,clamp(lobeStrength,vec3(0.0),vec3(1.0)),gain);
    vec3 q=FiberIblAbsorptionCoordinate(base,fiber.w);
    vec4 red=FiberMoments(abs(si),q.r),green=FiberMoments(abs(si),q.g),blue=FiberMoments(abs(si),q.b);
    vec3 rawF=max(vec3(red.r,green.r,blue.r),vec3(.0001));
    vec3 af=min(rawF,vec3(.99)),ab=clamp(vec3(red.g,green.g,blue.g),vec3(0.0),vec3(.99));
    vec3 rMean=max(vec3(red.b,green.b,blue.b),vec3(0.0)),ttMean=max(vec3(red.a,green.a,blue.a),vec3(0.0));
    vec3 wf=af/max(dot(af,vec3(1.0)),.0001),wb=ab/max(dot(ab,vec3(1.0)),.0001);
    float rough=clamp(fiber.y,.18,.6);
    vec3 widths=vec3(rough,.5*rough,2.0*rough),shifts=vec3(-tilt,.5*tilt,1.5*tilt);
    float shiftF=dot(shifts,wf),betaF=dot(widths,wf),betaB=dot(widths,wb);
    float varianceF=betaF*betaF,varianceB=betaB*betaB,propagatedF=max(count,1.0)*varianceF;
    vec3 f2=af*af,b2=ab*ab,b3=b2*ab,d=max(vec3(1.0)-f2,vec3(.0199)),d2=d*d,d3=d2*d;
    vec3 backward=ab*f2/d+b3*f2/d3;
    vec3 shiftB=shiftF*(vec3(1.0)-2.0*b2/d2+(2.0*d2+4.0*f2*b2)/d3);
    vec3 betaBack=(vec3(1.0)+.7*f2)*(ab*sqrt(max(varianceB+2.0*varianceF,0.0))+
        b3*sqrt(max(3.0*varianceB+2.0*varianceF,0.0)))/max(ab+b3*(2.0*betaF+3.0*betaB),vec3(.0001));
    vec3 varianceBack=betaBack*betaBack;
    vec3 localBack=2.0*backward*FiberIblGaussian(vec3(delta)-shiftB,varianceBack)/(FiberPi*c*c);
    vec3 propagatedBack=2.0*backward*FiberIblGaussian(vec3(delta)-shiftB,varianceBack+propagatedF)/(FiberPi*c*c);
    vec3 forwardPhase=rMean*FiberIblGaussian(delta+tilt,propagatedF+rough)+
        ttMean*FiberIblGaussian(delta-.5*tilt,propagatedF+.5*rough);
    vec3 forward=.7*pow(rawF,vec3(count))*FiberIblGaussian(delta,propagatedF)/(FiberPi*c);
    float uncollided=exp(-count);
    vec3 direct=uncollided*single;
    vec3 full=uncollided*(single+.7*localBack)+.7*clamp(forward-uncollided,vec3(0.0),vec3(1.0))*
        (forwardPhase+2.1991148575*gain*propagatedBack);
    return max(mix(direct,full,clamp(multipleStrength,0.0,1.0)),vec3(0.0))*ci*visible;
}
)GLSL")+FiberDirectGLSL(); }
} // namespace Render::PipelineDetail
