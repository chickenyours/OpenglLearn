#pragma once
#include <stdexcept>
#include <string>
#include <string_view>

#include "surface_cache_probe_shaders.h"
#include "surface_cache_native_contact_mask_shaders.h"

namespace Render::SurfaceCacheDetail {
namespace NativeReconstructionDetail {
inline void ReplaceOne(std::string& source,std::string_view from,std::string_view to) {
    const auto at=source.find(from);
    if(at==std::string::npos||source.find(from,at+from.size())!=std::string::npos)
        throw std::logic_error("Native dual reconstruction source anchor changed: "+std::string(from.substr(0,96)));
    source.replace(at,from.size(),to);
}
inline void ReplaceAll(std::string& source,std::string_view from,std::string_view to,unsigned expected=0) {
    std::size_t at=0;unsigned count=0;
    while((at=source.find(from,at))!=std::string::npos){source.replace(at,from.size(),to);at+=to.size();++count;}
    if((expected&&count!=expected)||!count)
        throw std::logic_error("Native dual reconstruction token count changed: "+std::string(from));
}
// Extract function definitions, excluding forward declarations. Comments are
// skipped when matching braces so a documentation edit cannot truncate GLSL.
inline std::string Function(const std::string& source,std::string_view signature) {
    const auto begin=source.find(signature);
    if(begin==std::string::npos||source.find(signature,begin+signature.size())!=std::string::npos)
        throw std::logic_error("Native reconstruction function anchor changed: "+std::string(signature));
    const auto brace=source.find('{',begin);
    if(brace==std::string::npos)throw std::logic_error("Native reconstruction function has no body");
    unsigned depth=0;bool line=false,block=false;
    for(std::size_t i=brace;i<source.size();++i) {
        const char c=source[i],next=i+1<source.size()?source[i+1]:'\0';
        if(line){if(c=='\n')line=false;continue;}
        if(block){if(c=='*'&&next=='/'){block=false;++i;}continue;}
        if(c=='/'&&next=='/'){line=true;++i;continue;}
        if(c=='/'&&next=='*'){block=true;++i;continue;}
        if(c=='{')++depth;
        else if(c=='}'&&!--depth)return source.substr(begin,i-begin+1);
    }
    throw std::logic_error("Native reconstruction function braces changed");
}
inline std::string ContactFallbackGeometry(const std::string& sampling) {
    auto source=Function(sampling,"float SPGiReconstructionGeometryWeight(");
    ReplaceOne(source,"SPGiReconstructionGeometryWeight(","NRCTReconstructionGeometryWeight(");
    ReplaceAll(source,"SPGI_CURVED_SUPPORT(","CTProbeCurvedSupport(",1);
    return source;
}
inline std::string ContactFallbackStencil(const std::string& sampling) {
    auto source=Function(sampling,"void SPGiReconstructionStencil(");
    ReplaceOne(source,"SPGiReconstructionStencil(","NRCTReconstructionStencil(");
    ReplaceAll(source,"SPGiReconstructionGeometryWeight(","NRCTReconstructionGeometryWeight(",2);
    ReplaceAll(source,"SPGI_PROBE_USABLE(","CTProbeUsable(",2);
    ReplaceAll(source,"SPGI_SAMPLE_LIGHTING(","CTProbeRisk(",1);
    return source;
}
inline std::string DualStencil(const std::string& sampling) {
    auto source=Function(sampling,"void SPGiReconstructionStencil(");
    ReplaceOne(source,"SPGiReconstructionStencil(","NRDualReconstructionStencil(");
    ReplaceOne(source,"out vec4 adaptiveLighting[4],","out vec4 adaptiveLighting[4],out vec4 adaptiveRisk[4],");
    ReplaceOne(source,"adaptiveLighting[c]=vec4(0);","adaptiveLighting[c]=vec4(0);adaptiveRisk[c]=vec4(0);");
    ReplaceOne(source,"vec4 weightedLighting[4];for(int c=0;c<4;++c)weightedLighting[c]=vec4(0);",
        "vec4 weightedLighting[4],weightedRisk[4];for(int c=0;c<4;++c){weightedLighting[c]=vec4(0);weightedRisk[c]=vec4(0);}");
    // Keep the original base accumulation, and independently accumulate the
    // original risk value in precisely the same representative/bucket order.
    ReplaceOne(source,
        "if(needLighting)weightedLighting[bucket]+=\n                    SPGI_SAMPLE_LIGHTING(irradianceAtlas,probe,p,n,depth)*weight;",
        "if(needLighting){weightedLighting[bucket]+=\n                    SPGI_SAMPLE_LIGHTING(irradianceAtlas,probe,p,n,depth)*weight;\n"
        "                    weightedRisk[bucket]+=CTProbeRisk(ctRisk,probe,p,n,depth)*weight;}");
    ReplaceOne(source,
        "for(int c=0;c<4;++c)if(adaptiveWeights[c]>0)\n        adaptiveLighting[c]=weightedLighting[c]/adaptiveWeights[c];",
        "for(int c=0;c<4;++c)if(adaptiveWeights[c]>0){\n        adaptiveLighting[c]=weightedLighting[c]/adaptiveWeights[c];\n"
        "        adaptiveRisk[c]=weightedRisk[c]/adaptiveWeights[c];}");
    return source;
}
}

// Optional Native Base/raw-mask MRT. The legacy consumers remain available
// independently, including the fallback when their geometry hooks differ.
// Required sampler ABI: 2 positions, 3 normals, 4 rho, 5 uniform probes,
// 6 adaptive probes, 7 scalar irradiance, 8 final directional coefficients,
// 10 active mask, 11 certified origins, 15 ORIGINAL geometric probe risk.
// The original PG and CT consumers must use these same producer images.
// UBOs retain their original bindings: SurfaceCache 12, Placement 9, Contact14.
// Outputs preserve the current RGBA32F NativeBaseGI at location 0 and ORIGINAL
// RGBA32F raw contact mask at location 1. A future half-float base must retain
// the old alpha store/load rounding before using that alpha for mask fallback.
// Compute probe risk before this MRT; retain the
// subsequent compatible mask expansion / fused exposure pass unchanged.
inline std::string NativeReconstructionMaskShader(bool coverageDiagnostic=false) {
    using namespace NativeReconstructionDetail;
    const auto originalBase=ProbeReconstructShader(coverageDiagnostic);
    const auto originalMask=NativeContactMaskShader();
    const auto sampling=ScreenProbePlacementDetail::SamplingGLSL();
    auto source=originalBase;
    ReplaceOne(source,"in vec2 vUV;layout(location=0) out vec4 outColor;","in vec2 vUV;vec4 nrBaseValue;");
    ReplaceAll(source,"outColor","nrBaseValue");
    ReplaceOne(source,"void main() {","void NRBaseOriginalMain() {");

    source+='\n'+ContactConstantsGLSL()+R"GLSL(
#define ctPositions pgViewPositions
#define ctNormals pgWorldNormals
#define ctUniform pgUniform
#define ctSpawned pgSpawned
#define ctActive pgActiveMask
#define ctOrigins pgSafeOrigins
#define ctDirectional pgDirectional
layout(binding=15) uniform sampler2D ctRisk;
vec4 nrMaskValue;
)GLSL";
    // Use the production contact helpers, including its original 48-neighbor
    // screen-risk stencil. Sharing probe geometry does not remove that work.
    for(const auto signature:{"bool CTFinite(","bool CTProbeUsable(int probe) {",
        "float CTProbeCurvedSupport(int probe,vec3 p,vec3 n,float depth) {",
        "vec4 CTProbeRisk(sampler2D atlas,int probe,vec3 p,vec3 n,float depth) {",
        "float CTReceiverScreenRisk("})source+='\n'+Function(originalMask,signature)+'\n';
    source+=ContactFallbackGeometry(sampling)+'\n'+ContactFallbackStencil(sampling)+'\n';
    auto mask=Function(originalMask,"void main() {");
    ReplaceOne(mask,"void main() {","void NRMaskOriginalMain() {");
    ReplaceAll(mask,"outColor","nrMaskValue");
    ReplaceOne(mask,"texelFetch(ctInput,pixel,0).a","nrBaseValue.a");
    ReplaceOne(mask,"SPGiReconstructionStencil(","NRCTReconstructionStencil(");
    source+=mask+'\n'+DualStencil(sampling)+'\n';

    // Start from the original PG main, retaining all its finite checks,
    // sufficient-coverage thresholds and final base arithmetic. Only the
    // refinement branch is shared; the full original coarse/top-K path remains
    // in NRBaseOriginalMain for fallback.
    auto dual=Function(originalBase,"void main() {");
    const auto coarse=dual.find("    ivec2 base=SPCornerBase(pixel);vec2 f=SPBilinearFractions(pixel,base);");
    if(coarse==std::string::npos)throw std::logic_error("Native base coarse fallback anchor changed");
    dual.resize(coarse);dual+="}\n";
    ReplaceOne(dual,"void main() {","void NRDualMain() {");
    ReplaceAll(dual,"outColor","nrBaseValue");
    ReplaceOne(dual,"float depth=max(abs(view.z),.0001);",
        "float depth=max(abs(view.z),.0001);\n    bool nrMaskValid=CTFinite(p);\n"
        "    if(nrMaskValid)nrMaskValue=vec4(1,1,0,1);");
    ReplaceOne(dual,"vec4 adaptiveLighting[4];vec4 uniformWeights,adaptiveWeights;float uniformWeight;",
        "vec4 adaptiveLighting[4],nrAdaptiveRisk[4];vec4 uniformWeights,adaptiveWeights;float uniformWeight;");
    ReplaceOne(dual,"SPGiReconstructionStencil(","NRDualReconstructionStencil(");
    ReplaceOne(dual,"adaptiveLighting,uniformWeights,adaptiveWeights,uniformWeight);",
        "adaptiveLighting,nrAdaptiveRisk,uniformWeights,adaptiveWeights,uniformWeight);");
    const std::string baseResult="nrBaseValue=vec4(PGDebugMode()!=0?sum.rgb:rho*sum.rgb*scParameters.z,clamp(sum.a,0.0,1.0));return;";
    const auto rawBegin=mask.find("    float mass=dot(uniformWeights+adaptiveWeights,vec4(1)),riskSum=0;");
    const auto rawEnd=mask.find("    nrMaskValue=vec4(mask,risk,mass,1);",rawBegin);
    if(rawBegin==std::string::npos||rawEnd==std::string::npos)throw std::logic_error("Native raw risk result anchors changed");
    auto raw=mask.substr(rawBegin,rawEnd-rawBegin+std::string_view("    nrMaskValue=vec4(mask,risk,mass,1);").size());
    ReplaceAll(raw,"adaptiveLighting[corner]","nrAdaptiveRisk[corner]",1);
    ReplaceOne(raw,"ivec2 base=SPCornerBase(pixel);","ivec2 nrRiskBase=SPCornerBase(pixel);");
    ReplaceOne(raw,"SPCorner(base,corner)","SPCorner(nrRiskBase,corner)");
    ReplaceOne(dual,baseResult,
        "nrBaseValue=vec4(PGDebugMode()!=0?sum.rgb:rho*sum.rgb*scParameters.z,clamp(sum.a,0.0,1.0));\n"
        "        if(!nrMaskValid||(ctOptions.w&2)!=0||nrBaseValue.a<=0)return;\n"+raw+"\n        return;");
    source+=dual+R"GLSL(
// The two original geometry hooks differ in debug/coverage modes and can
// differ when only one consumer has curved hit metadata. Never silently use
// the radiance weight as a substitute for the original risk weight.
bool NRSharedHooksCompatible() {
    if(PG_COVERAGE_DIAGNOSTIC!=0||PGDebugMode()!=0||!SPGiRefinementEnabled())return false;
    if(SPGiCurvatureAwareWeightsEnabled()) {
        bool pgHitMetadata=(int(scParameters.w+.5)&128)!=0;
        bool ctHitMetadata=(ctOptions.w&8)!=0;
        if(pgHitMetadata!=ctHitMetadata)return false;
    }
    return true;
}
layout(location=0) out vec4 nrBaseOutput;
layout(location=1) out vec4 nrRawMaskOutput;
void main() {
    nrBaseValue=vec4(0);nrMaskValue=vec4(0);
    if(NRSharedHooksCompatible())NRDualMain();
    else {NRBaseOriginalMain();NRMaskOriginalMain();}
    nrBaseOutput=nrBaseValue;nrRawMaskOutput=nrMaskValue;
}
)GLSL";
    return source;
}
} // namespace Render::SurfaceCacheDetail
