#pragma once

#include <string>
#include <unordered_map>
#include <json/json.h>

namespace Resource{
    
class Material;

typedef bool(*MaterialBluePrintFunction)(Material& material,
        const Json::Value* textures,
        const Json::Value* properties,
        const Json::Value* shaders);


// 管理动态参数,和Material创建密切相关
class MaterialManager {
    public:

        static MaterialManager* GetInstance(){
            static MaterialManager instance;
            return &instance;
        }

        bool LoadMaterialFromConfigFile(const std::string& configFile, Material& material);
       
        // ✅ 注册蓝图类型 (静态映射)
        template <typename T>
        void RegisterBlueprint(const std::string& type) {
            static_assert(std::is_base_of<Material, T>::value, "T must be a subclass of Material");
            blueprintFactory_[type] = &T::LoadMaterialFromConfigFile;
        }

        MaterialBluePrintFunction GetMaterialBluePrintTypeFunction(const std::string& type){
            auto it = blueprintFactory_.find(type);
            if (it != blueprintFactory_.end()) {
                return it->second;
            }
            return nullptr;
        }

        
    private:

        MaterialManager() = default;
        ~MaterialManager() = default;

        std::unordered_map<std::string, MaterialBluePrintFunction> blueprintFactory_;

};

}