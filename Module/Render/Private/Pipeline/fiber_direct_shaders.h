#pragma once
#include <string>

namespace Render::PipelineDetail {
// Independent transcription of the Emissive calcShading direct-light block,
// not a call to the IBL wrapper. Requires FiberSafe/Moments/NttAzimuth from
// FiberScatteringGLSL. Evidence and executable SSA audit: pragmata/hair_direct.
inline std::string FiberDirectGLSL() { return R"GLSL(
vec3 FiberDirectGaussian(vec3 offset,vec3 variance) {
    vec3 v=max(variance,vec3(.0001));
    return exp(-.5*offset*offset/v)/sqrt(clamp(6.28318530718*v,vec3(0.0),vec3(1.0))+.0001);
}
float FiberDirectGaussian(float offset,float variance) {
    return FiberDirectGaussian(vec3(offset),vec3(variance)).r;
}
float FiberDirectGain(float count,float backscatterScale,float visibility) {
    float scale=max(backscatterScale,0.0),root=sqrt(scale);
    return root+(scale*scale-root)*visibility*exp(-max(count-1.0,0.0));
}
vec3 FiberDirectAbsorption(vec3 base) {
    vec3 q=log2(max(base,vec3(.001)));
    return .01385651808232069*q*q;
}
vec3 FiberSourceDirectSingle(vec3 base,vec3 T,vec3 V,vec3 L,vec4 fiber,
    float primaryRoughness,float tilt,vec3 strengths,float gain) {
    // Emissive %1984..2179: fixed eta=1.5/F0=.04, three lobes only.
    // Source guards below only regularize exact grazing/zero-width inputs.
    float si=clamp(dot(T,L),-.9999,.9999),so=clamp(dot(T,V),-.9999,.9999);
    float ci=sqrt(max(1.0-si*si,.0001)),co=sqrt(max(1.0-so*so,.0001));
    float c=sqrt(max(.5*(1.0+ci*co+si*so),.0001)),delta=si+so;
    vec3 li=L-T*si,vo=V-T*so;
    float p=clamp(dot(li,vo)*inversesqrt(dot(li,li)*dot(vo,vo)+.0001),-1.0,1.0);
    float h=sqrt(clamp(.5*(p+1.0),0.0,1.0));
    float etaPrime=sqrt(c*c+1.25)/c;
    float width=max(fiber.y,.02),rWidth=clamp(2.0*width*primaryRoughness,1.0/255.0,1.0);
    float halfCos=sqrt(clamp(.5*(dot(V,L)+1.0),0.0,1.0));
    float fr=.04+.96*pow(1.0-halfCos,5.0);
    float mr=.0997355729342/rWidth*exp(-.5*pow((delta+tilt)/rWidth,2.0));
    vec3 reflected=vec3(mr*h*fr)*strengths.x;
    float u=1.0-h,g=(1.0-.15/etaPrime-.5*u*u+(u*u+u)/etaPrime)*h;
    float chord=2.0-2.0*pow(clamp(g/etaPrime,-1.0,1.0),2.0);
    float interfaceTransmission=.96*(1.0-pow(1.0-c*sqrt(clamp(1.0-g*g,0.0,1.0)),5.0));
    float mtt=.797884583473/width*exp(-.5*pow(2.0*(delta-.5*tilt)/width,2.0));
    // Keep the original base-two attenuation literal; absorptionScale is a
    // host control and one reproduces the shipped instruction coefficient.
    vec3 logColor=log2(max(base,vec3(.001)));
    vec3 absorptionExponent=-.03998145833611488*logColor*logColor*max(fiber.w,0.0);
    vec3 transmitted=gain*interfaceTransmission*interfaceTransmission*mtt*
        FiberNttAzimuth(si,p,fiber.z)*exp2(absorptionExponent*chord)*strengths.y;
    float f=.04+.96*pow(1.0-.5*c,5.0);
    float chordTrt=2.0-2.0*pow(clamp(.86602538824/etaPrime,-1.0,1.0),2.0);
    float spread=clamp(1.5*(1.0-max(fiber.z,.02)),0.0,1.0);
    float ntrt=spread*exp2(spread*(24.5258159637*p-24.2084236145));
    float mtrt=.199471145868/width*exp(-.5*pow((delta-1.5*tilt)/(2.0*width),2.0));
    vec3 internal=(1.0-f)*(1.0-f)*f*mtrt*ntrt*exp2(2.0*absorptionExponent*chordTrt)*strengths.z;
    return max(reflected+transmitted+internal,vec3(0.0));
}
vec3 FiberSourceDirect(vec3 base,vec3 tangent,vec3 view,vec3 light,vec4 fiber,
    float primaryRoughness,float tilt,vec3 lobeStrength,float multipleStrength,
    float density,float pathCount,bool spatial,float backscatterScale,float visibility,float gainVisibility) {
    vec3 T=FiberSafe(tangent,vec3(0.0,-1.0,0.0));
    vec3 V=FiberSafe(view,vec3(0.0,0.0,1.0)),L=FiberSafe(light,vec3(0.0,1.0,0.0));
    // Explicit host no-volume policy is N=0. Native trace miss can instead
    // select N=.5; its AABB/jitter/packed-volume policy is outside this solve.
    float count=spatial?max(pathCount,0.0)*max(density,0.0):0.0;
    // Emissive K consumes only DS visibility. External light/shadow energy
    // can already be included in final visibility, but must not modify K.
    float visible=clamp(visibility,0.0,1.0),gain=FiberDirectGain(count,backscatterScale,clamp(gainVisibility,0.0,1.0));
    vec3 single=FiberSourceDirectSingle(base,T,V,L,fiber,primaryRoughness,tilt,
        clamp(lobeStrength,vec3(0.0),vec3(1.0)),gain);
    // Emissive %2184..2556. The IOR of this shared DSLUT stays the material
    // producer's IOR, independently of the direct single's fixed 1.5.
    float si=clamp(dot(T,L),-.9999,.9999),so=clamp(dot(T,V),-.9999,.9999);
    float ci=sqrt(max(1.0-si*si,.0001)),co=sqrt(max(1.0-so*so,.0001));
    float c=sqrt(max(.5*(1.0+ci*co+si*so),.0001)),delta=si+so;
    vec3 q=FiberDirectAbsorption(base)*sqrt(max(fiber.w,0.0));
    vec4 red=FiberMoments(abs(si),q.r),green=FiberMoments(abs(si),q.g),blue=FiberMoments(abs(si),q.b);
    vec3 rawF=max(vec3(red.r,green.r,blue.r),vec3(0.0));
    vec3 f=min(rawF,vec3(.99)),b=clamp(vec3(red.g,green.g,blue.g),vec3(0.0),vec3(.99));
    vec3 wf=f/max(dot(f,vec3(1.0)),1e-20),wb=b/max(dot(b,vec3(1.0)),1e-20);
    float rough=clamp(fiber.y,.18,.6);
    vec3 widths=vec3(rough,.5*rough,2.0*rough),shifts=vec3(-tilt,.5*tilt,1.5*tilt);
    float shiftF=dot(shifts,wf),sigmaF=dot(widths,wf),sigmaB=dot(widths,wb);
    float vf=sigmaF*sigmaF,vb=sigmaB*sigmaB,nvf=max(count,1.0)*vf;
    vec3 f2=f*f,b2=b*b,b3=b2*b,d=max(vec3(1.0)-f2,vec3(1e-6)),d2=d*d,d3=d2*d;
    vec3 amplitude=b*f2/d+b3*f2/d3;
    vec3 shiftB=shiftF*(vec3(1.0)-2.0*b2/d2+(2.0*d2+4.0*f2*b2)/d3);
    vec3 widthB=(vec3(1.0)+.7*f2)*(b*sqrt(max(vb+2.0*vf,0.0))+b3*sqrt(max(3.0*vb+2.0*vf,0.0)))/
        max(b+b3*(2.0*sigmaF+3.0*sigmaB),vec3(1e-20));
    vec3 varianceB=widthB*widthB;
    vec3 localBack=2.0*amplitude*FiberDirectGaussian(vec3(delta)-shiftB,varianceB)/(FiberPi*c*c);
    vec3 propagatedBack=2.0*amplitude*FiberDirectGaussian(vec3(delta)-shiftB,varianceB+nvf)/(FiberPi*c*c);
    vec3 phase=max(vec3(red.b,green.b,blue.b),vec3(0.0))*FiberDirectGaussian(delta+tilt,nvf+rough)+
        max(vec3(red.a,green.a,blue.a),vec3(0.0))*FiberDirectGaussian(delta-.5*tilt,nvf+.5*rough);
    // Native .99 cap belongs only to the geometric-series coefficients.
    // Raw af^N is retained here, with the zero-base/zero-exponent limit guarded.
    vec3 transmission=count<=0.0?vec3(1.0):pow(max(rawF,vec3(1e-20)),vec3(count));
    if(count>0.0)transmission*=step(vec3(1e-20),rawF);
    vec3 forward=.7*transmission*FiberDirectGaussian(delta,nvf)/(FiberPi*c);
    float uncollided=exp(-count);
    vec3 ballistic=uncollided*single;
    vec3 full=uncollided*(single+.7*localBack)+.7*clamp(forward-uncollided,vec3(0.0),vec3(1.0))*
        (phase+2.1991147994995117*gain*propagatedBack);
    // 1 is the native normal-mode closure. Zero/intermediate values are host
    // ballistic-only / continuous-ablation controls, not native renderType2.
    return max(mix(ballistic,full,clamp(multipleStrength,0.0,1.0)),vec3(0.0))*ci*visible;
}
// Existing direct-light and scalar-oracle callers have only the DS channel.
// Preserve that interface and its arithmetic; the sun integration uses the
// explicit final/gain-visibility overload above.
vec3 FiberSourceDirect(vec3 base,vec3 tangent,vec3 view,vec3 light,vec4 fiber,
    float primaryRoughness,float tilt,vec3 lobeStrength,float multipleStrength,
    float density,float pathCount,bool spatial,float backscatterScale,float visibility) {
    return FiberSourceDirect(base,tangent,view,light,fiber,primaryRoughness,tilt,
        lobeStrength,multipleStrength,density,pathCount,spatial,backscatterScale,visibility,visibility);
}
)GLSL"; }
} // namespace Render::PipelineDetail
