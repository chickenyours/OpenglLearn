#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include "Render/Public/Material/pbr_material.h"
#include "Render/Public/Material/unlit_material.h"
#include "Render/Public/Pipeline/render_pipeline.h"

namespace {
using namespace Render;
using namespace Render::Material;
template<class T> T Read(const ParameterBlock& block,const char* name) {
    T result{};
    std::memcpy(&result,block.Bytes().data()+block.GetTemplate()->FindLayout(name)->offset,sizeof(T));
    return result;
}
void CheckLayoutAndValues() {
    static_assert(static_cast<int>(Domain::Surface)==0 && static_cast<int>(Domain::Sprite)==1);
    static_assert(static_cast<int>(Domain::Translucent)==2);
    static_assert(sizeof(SurfacePassConstants)==176 && SurfacePassBinding==7);
    static_assert(offsetof(SurfacePassConstants,screenTime)==128 && offsetof(SurfacePassConstants,options)==144);
    const auto surface=MakePbrTemplate(), glass=MakePbrTemplate(Domain::Translucent);
    assert(surface && glass && surface->ByteSize()==208 && glass->ByteSize()==208);
    assert(surface->ByteSize()<=MaterialTemplate::MaxParameterBytes);
    assert(!MakePbrTemplate(Domain::Sprite));
    assert(!MakePbrTemplate(static_cast<Domain>(255)));
    assert(!MakeUnlitTemplate(Domain::Translucent));
    const char* names[] = {"uvFlow","useFlowMap","flowStrength","emissiveIntensity","useEmissiveMap",
        "opacity","transmission","ior","thickness","distortionStrength","useDistortionMap","absorptionColor","twoSided"};
    const std::size_t offsets[] = {112,128,132,136,140,144,148,152,156,160,164,176,192};
    for(std::size_t i=0;i<std::size(names);++i) {
        assert(surface->FindLayout(names[i])->offset==offsets[i]);
        assert(glass->FindLayout(names[i])->offset==offsets[i]);
    }
    assert(glass->FindLayout("alphaCutoff")->offset==100);
    assert(glass->FindLayout("emissiveColor")->offset==80);
    assert(glass->FindParameter("opacity")->displayName=="Optical effect strength");
    assert(surface->FindParameter("opacity")->displayName=="Alpha multiplier");
    ParameterBlock values(glass);
    assert(Read<glm::vec4>(values,"uvFlow")==glm::vec4(0));
    assert(Read<glm::vec4>(values,"absorptionColor")==glm::vec4(1));
    assert(Read<float>(values,"opacity")==1 && Read<float>(values,"ior")==1.45f);
    assert(Read<std::uint32_t>(values,"useFlowMap")==0);
    assert(Read<std::uint32_t>(values,"twoSided")==0);
    assert(values.Set("opacity",0.0f) && values.Set("opacity",1.0f));
    assert(!values.Set("opacity",-0.01f) && !values.Set("opacity",1.01f));
    assert(!values.Set("ior",0.99f) && !values.Set("ior",2.51f));
    assert(!values.Set("thickness",-1.0f));
    assert(!values.Set("absorptionColor",glm::vec4(-.01f)));
    assert(!values.Set("uvFlow",glm::vec4(std::numeric_limits<float>::infinity())));
    assert(values.Set("emissiveIntensity",100000.0f)); // HDR metadata is not display-clamped.
    for(std::size_t slot=0;slot<8;++slot) {
        assert(glass->Desc().textures[slot].slot==slot);
        assert(glass->Desc().textures[slot].required==(slot<5));
    }
    assert(!glass->FindParameter("time")); // flow time belongs to the pass.
}

std::shared_ptr<const MaterialSnapshot> Snapshot(Domain domain,std::uint32_t id) {
    std::shared_ptr<const MaterialTemplate> schema;
    if(domain==Domain::Sprite) {
        MaterialTemplateDesc desc; desc.name="SpriteOrder"; desc.domain=domain;
        schema=MaterialTemplate::Create(std::move(desc));
    } else schema=MakePbrTemplate(domain);
    auto asset=MaterialAsset::Create(schema);
    MaterialInstance instance(asset);
    MaterialPassResources resources;
    resources.expectedTemplate=schema; resources.pipeline={id,0};
    resources.parameterBuffer={id+100,0}; resources.parameterBufferBytes=static_cast<std::uint32_t>(schema->ByteSize());
    resources.diffuseRadiancePipeline={id+200,0}; resources.normalPipeline={id+300,0};
    resources.lifetime=std::make_shared<int>(1);
    if(domain!=Domain::Sprite) for(std::uint32_t i=0;i<5;++i) resources.textures.push_back({i,{20+i,0}});
    auto result=MaterialSnapshot::Create(instance,resources);
    assert(result && result->Resources().normalPipeline==resources.normalPipeline);
    assert(result->Resources().diffuseRadiancePipeline==resources.diffuseRadiancePipeline);
    return result;
}
void CheckLayersAndOrdering() {
    const auto glass=Snapshot(Domain::Translucent,1), solid=Snapshot(Domain::Surface,2), sprite=Snapshot(Domain::Sprite,3);
    RenderItem item; item.material=glass;
    assert(item.layer==RenderLayer::Opaque && item.EffectiveLayer()==RenderLayer::Transparent);
    item.layer=RenderLayer::Cutout;
    assert(item.EffectiveLayer()==RenderLayer::Transparent);
    item.layer=RenderLayer::Overlay;
    assert(item.EffectiveLayer()==RenderLayer::Overlay);
    const auto pipeline=PbrPipelineSpec({1,0},Domain::Translucent);
    assert(pipeline.depthTest && !pipeline.depthWrite && pipeline.blendEnable && pipeline.blendMode==BlendMode::Alpha);
    assert(PbrPipelineSpec({1,0}).depthWrite && !PbrPipelineSpec({1,0}).blendEnable);

    RenderFrame frame;
    const auto add=[&](std::uint32_t id,auto material,float depth,RenderLayer layer=RenderLayer::Opaque) {
        RenderItem packet; packet.mesh={id,0}; packet.material=material; packet.viewDepth=depth; packet.layer=layer;
        packet.draw.indexCount=3; frame.items.push_back(std::move(packet));
    };
    add(2,glass,2); add(4,sprite,100,RenderLayer::Transparent); add(3,glass,8);
    add(5,sprite,0,RenderLayer::Transparent); add(1,solid,3); add(6,sprite,10,RenderLayer::Transparent);
    add(7,glass,999,RenderLayer::Overlay);
    RHIFrameCommandBufferPool pool;
    RHIFrameEncoder encoder(pool.threadAny_GetBuffer());
    assert(encoder.Begin({.frameIndex=1}));
    assert(ForwardRenderPipeline{}.Record(encoder,frame,{}));
    const std::uint32_t expected[]{1,3,2,4,5,6,7};
    for(std::size_t i=0;i<frame.items.size();++i) assert(frame.items[i].mesh.id==expected[i]);
    assert(encoder.Cancel());
}
} // namespace

int main() {
    CheckLayoutAndValues();
    CheckLayersAndOrdering();
    std::cout<<"Advanced material CPU tests passed: 208-byte layout, translucent layer routing and stable sprite order\n";
}
