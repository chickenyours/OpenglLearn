#pragma once
#include "Render/Public/Pipeline/render_world.h"

namespace Render::PipelineDetail {
// Depth producers only. Lighting, hair and post settings cannot change the
// opaque snapshot depth. For in-place uploads, the owner must advance epoch
// through MaterialRenderPipeline::InvalidateOpaqueDepth() (or the default
// ResetHistory()) before recording again, including time-driven vertex input.
inline std::uint64_t StrandOpaqueDepthContentHash(const RenderFrame& frame,std::uint64_t epoch){
    std::uint64_t hash=14695981039346656037ull;
    const auto bytes=[&](const void* data,std::size_t size){
        const auto* p=static_cast<const unsigned char*>(data);
        for(std::size_t i=0;i<size;++i){hash^=p[i];hash*=1099511628211ull;}
    };
    const auto add=[&](const auto& value){bytes(&value,sizeof(value));};
    add(epoch);
    for(const auto& item:frame.items){
        const auto layer=item.EffectiveLayer();
        if(layer!=RenderLayer::Opaque&&layer!=RenderLayer::Cutout)continue;
        add(layer);add(item.mesh);add(item.EffectivePipeline());add(item.texture);add(item.model);
        add(item.draw.indexCount);add(item.draw.firstIndex);add(item.draw.baseVertex);
        add(item.draw.instanceCount);add(item.draw.firstInstance);add(item.order);
        // Conservatively keep all material values: arbitrary opaque/cutout
        // vertex and alpha shaders may depend on any of them.
        if(item.material){
            const auto& data=item.material->Parameters().Bytes();bytes(data.data(),data.size());
            for(const auto& texture:item.material->Resources().textures){add(texture.slot);add(texture.texture);}
        }
    }
    return hash;
}
}
