#pragma once
#include "game_components.h"
#include "Render/module.h"
#include <filesystem>
#include <map>

namespace IWanna {
// Painter-ordered CPU batch: one atlas, one mesh upload, one RHI draw.
class SpriteRenderer {
public:
    explicit SpriteRenderer(ObjectWeakPtr<Render::RHIDevice> device):device_(device){}
    void Initialize(const std::filesystem::path& assets,const std::vector<EntityView>& entities,const std::vector<Sprite>& animations);
    bool Ready() const;
    void Draw(Render::RHIFrameEncoder&,const std::vector<DrawSprite>& sprites);
    void Text(Render::RHIFrameEncoder&,const std::string&,glm::vec2,float,glm::vec4);
    void Rect(Render::RHIFrameEncoder&,glm::vec2,glm::vec2,glm::vec4);
    void Flush(Render::RHIFrameEncoder&);
    void Submit(ObjectWeakPtr<Render::RHIFrameCommandBuffer>,std::function<void()>);
    void Shutdown();
    const std::string& Error() const {return error_;}
private:
    struct Vertex {glm::vec2 position,uv;glm::vec4 color;float sdf=0;};
    struct Texture {std::vector<glm::vec4> frames;};
    void Quad(glm::vec2,glm::vec2,float,glm::vec4,glm::vec4,float sdf=0);
    ObjectWeakPtr<Render::RHIDevice> device_;
    std::map<std::string,Texture> textures_;
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
    Render::RenderResourceHandle<Render::RHITextureSpec> atlas_;
    Render::RenderResourceHandle<Render::VertexBufferSpec> mesh_;
    Render::RenderResourceHandle<Render::PipelineSpec> pipeline_;
    std::string error_;
};
}

