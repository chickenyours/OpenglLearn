#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <glad/glad.h>

#include "code/ECS/Core/Resource/resource.h"


namespace Resource {

template <typename Derived>
class MaterialBlueprint;

class Texture;
class ShaderProgram;

class Material : public ILoadFromConfig {
public:
    Material() = default;
    ~Material();

    uint32_t GetMaterialTypeFlag() const { return materialTypeFlag_; }
    const std::string& GetName() const { return name_; }
    inline bool IsLoaded(){return isLoaded_;}

    virtual bool LoadFromConfigFile(const std::string& configFile) override;
    virtual void Release() override;
    void UploadPropertyToUBO();

    void BindTexture(ResourceHandle<Texture>&& texture, size_t index);
    void BindShader(ResourceHandle<ShaderProgram>&& shaderID, size_t index);
    void BindTexture(ResourceHandle<Texture>& texture, size_t index);
    void BindShader(ResourceHandle<ShaderProgram>& shaderID, size_t index);
    void UploadPropertyData(size_t start, size_t size, const void* data);

protected:

    uint32_t materialTypeFlag_;
    std::string name_;

    size_t propertySize_;
    char* propertybuffer_; 

    size_t textureSize_;
    GLuint* textures_;

    size_t shaderProgramSize_;
    GLuint* shaderPrograms_;

    // 资源句柄容器
    std::vector<ResourceHandle<Texture>> textureHandles_;
    std::vector<ResourceHandle<ShaderProgram>> shaderHandles_;

private:
    bool isLoaded_ = false;

template <typename Derived>
friend class MaterialBlueprint;

};


} // namespace Resource

