#pragma once

#include "code/Resource/Material/material_interface.h"



namespace Resource{

class IBPR : public IMaterial{
    private:
        bool isInitUBO_ = false;
    public:
        struct alignas(16) Property : IMaterialProperty<Property> {
            float metallic;
            float roughness;
            float ao;
        } property;


        struct State : public IMaterialState<State>{

            bool useMetallicMap = false;
            bool useRoughnessMap = false;
            bool useAoMap = false;
        
            
            std::unordered_map<ShaderStage, ShaderDescription> ToShaderDefinesImpl() const {
                std::unordered_map<ShaderStage, ShaderDescription> ShaderDescriptions;
                if (useMetallicMap) ShaderDescriptions[ShaderStage::Fragment].macros.push_back("USE_METALLIC_MAP");
                if (useRoughnessMap) ShaderDescriptions[ShaderStage::Fragment].macros.push_back("USE_ROUGHNESS_MAP");
                if (useAoMap) ShaderDescriptions[ShaderStage::Fragment].macros.push_back("USE_AO_MAP");
                return ShaderDescriptions;
            }
        } state_;
        
        ResourceHandle<ShaderProgram> mainShader_;
        ResourceHandle<Texture2D> albedoMap_;
        ResourceHandle<Texture2D> normalMap_;
        ResourceHandle<Texture2D> metallicMap_;
        ResourceHandle<Texture2D> roughnessMap_;
        ResourceHandle<Texture2D> aoMap_;

        GLuint UBO = 0;
        int propertyUBOBinding = 32;

        // struct Slot{
        //     GLuint mainShader = 0;
        //     GLuint albedoMap = 0;
        //     GLuint normalMap = 0;
        //     GLuint metallicMap = 0;
        //     GLuint roughnessMap = 0;
        //     GLuint aoMap = 0;
        //     GLuint UBO = 0;
        //     int propertyUBOBinding = 32;
        // } slot_;


       
        void UpLoadProperty(){
            if(!isInitUBO_){
                glGenBuffers(1, &UBO);
                glBindBuffer(GL_UNIFORM_BUFFER, UBO);
                glBufferData(GL_UNIFORM_BUFFER, sizeof(Property), nullptr, GL_DYNAMIC_DRAW);
                isInitUBO_ = true;
            }
            glBindBuffer(GL_UNIFORM_BUFFER, UBO);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Property), &property);
            glBindBufferBase(GL_UNIFORM_BUFFER, propertyUBOBinding, UBO);
        }
        // void UpdateSlot(){
        //     if(mainShader_){
        //         slot_.mainShader = mainShader_->GetID();
        //     }
        //     if(albedoMap_){
        //         slot_.albedoMap = albedoMap_->GetID();
        //     }
        //     if(normalMap_){
        //         slot_.normalMap = normalMap_->GetID();
        //     }
        //     if(metallicMap_){
        //         slot_.metallicMap = metallicMap_->GetID();
        //     }
        //     if(roughnessMap_){
        //         slot_.roughnessMap = roughnessMap_->GetID();
        //     }
        //     if(aoMap_){
        //         slot_.aoMap = aoMap_->GetID();
        //     }
        // }
        // inline const Slot& GetSlot(){return slot_;}
        // void SetMainShader(ResourceHandle<ShaderProgram> shaderProgram){
        //     if(shaderProgram){
        //         mainShader_ = shaderProgram; 
        //         // slot_.mainShader = mainShader_->GetID();
        //     }
        //     else{
        //         LOG_ERROR("Material:BPR", "Invalid shader program provided.");
        //     }
        // }
        // void SetAlbedoMap(ResourceHandle<Texture2D> texture) { 
        //     if(texture){
        //         albedoMap_ = texture; 
        //         // slot_.albedoMap = albedoMap_->GetID();
        //     }
        //     else{
        //         LOG_ERROR("Material:BPR", "Invalid albedo map texture provided.");
        //     }
        // }
        // void SetNormalMap(ResourceHandle<Texture2D> texture) { 
        //     if(texture){
        //     normalMap_ = texture; 
        //     slot_.normalMap = normalMap_->GetID();
        //     }
        //     else{
        //     LOG_ERROR("Material:BPR", "Invalid normal map texture provided.");
        //     }
        // }
        // void SetMetallicMap(ResourceHandle<Texture2D> texture) { 
        //     if(texture){
        //     metallicMap_ = texture; 
        //     slot_.metallicMap = metallicMap_->GetID();
        //     }
        //     else{
        //     LOG_ERROR("Material:BPR", "Invalid metallic map texture provided.");
        //     }
        // }
        // void SetRoughnessMap(ResourceHandle<Texture2D> texture) { 
        //     if(texture){
        //     roughnessMap_ = texture; 
        //     slot_.roughnessMap = roughnessMap_->GetID();
        //     }
        //     else{
        //     LOG_ERROR("Material:BPR", "Invalid roughness map texture provided.");
        //     }
        // }
        // void SetAoMap(ResourceHandle<Texture2D> texture) { 
        //     if(texture){
        //     aoMap_ = texture; 
        //     slot_.aoMap = aoMap_->GetID();
        //     }
        //     else{
        //     LOG_ERROR("Material:BPR", "Invalid AO map texture provided.");
        //     }
        // }


        bool LoadFromMataData(
            const Json::Value& textures,
            const Json::Value& shaderPrograms,
            const Json::Value& properties,
            const Json::Value& states,
            Log::StackLogErrorHandle errHandle
        ) override {
            Tool::JsonHelper::TryGetBool(states,"useMetallicMap", state_.useMetallicMap);
            Tool::JsonHelper::TryGetBool(states,"useRoughnessMap", state_.useRoughnessMap);
            Tool::JsonHelper::TryGetBool(states,"useAoMap", state_.useAoMap);

            std::string albedoMapPath;
              if(!Tool::JsonHelper::TryGetString(textures, "albedoMap", albedoMapPath)){
                REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load albedoMap from metadata.");
                return false;
            }

            std::string normalMapPath;
              if(!Tool::JsonHelper::TryGetString(textures, "normalMap", normalMapPath)){
                REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load normalMap from metadata.");
                return false;
            }

            std::string roughnessMapPath;
            if(state_.useRoughnessMap && !Tool::JsonHelper::TryGetString(textures, "roughnessMap", roughnessMapPath)){
                REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load roughnessMap from metadata.");
                return false;
            }

            std::string useMetallicMapPath;
              if(state_.useMetallicMap && !Tool::JsonHelper::TryGetString(textures, "metallicMap", useMetallicMapPath)){
                REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load metallicMap from metadata.");
                return false;
            }

            std::string aoMapPath;
              if(state_.useAoMap && !Tool::JsonHelper::TryGetString(textures, "aoMap", aoMapPath)){
                REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load aoMap from metadata.");
                return false;
            }

            std::string mainShaderProgramPath;
            if(!Tool::JsonHelper::TryGetString(shaderPrograms, "mainShader", mainShaderProgramPath)){
                REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load mainShader from metadata.");
                return false;
            }

            if(!state_.useMetallicMap && !Tool::JsonHelper::TryGetFloat(properties, "metallic", property.metallic)){
                REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load metallic property from metadata.");
                return false;
            }

            if(!state_.useRoughnessMap && !Tool::JsonHelper::TryGetFloat(properties, "roughness", property.roughness)){
                REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load roughness property from metadata.");
                return false;
            }

            if(!state_.useAoMap && !Tool::JsonHelper::TryGetFloat(properties, "ao", property.ao)){
                REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load ao property from metadata.");
                return false;
            }

            albedoMap_ = std::move(ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Texture2D>(FromConfig<Texture2D>(albedoMapPath)));
            if(!albedoMap_){
                REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load albedoMap resource.");
                return false;
            }
            normalMap_ = std::move(ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Texture2D>(FromConfig<Texture2D>(normalMapPath)));
            if(!normalMap_){
                REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load normalMap resource.");
                return false;
            }

            if(state_.useRoughnessMap){
                roughnessMap_ = std::move(ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Texture2D>(FromConfig<Texture2D>(roughnessMapPath)));
                if (!roughnessMap_) {
                    REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load roughnessMap resource.");
                    return false;
                }
            }

            if(state_.useMetallicMap){
                metallicMap_ = std::move(ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Texture2D>(FromConfig<Texture2D>(useMetallicMapPath)));
                if (!metallicMap_) {
                    REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load metallicMap resource.");
                    return false;
                }
            }

            if(state_.useAoMap){
                aoMap_ = std::move(ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Texture2D>(FromConfig<Texture2D>(aoMapPath)));
                if (!aoMap_) {
                    REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load aoMap resource.");
                    return false;
                }
            }

            // load shaderProgram

            mainShader_ = std::move(ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<ShaderProgramFactory>(
                FromConfig<ShaderProgramFactory>(mainShaderProgramPath))->GetShaderProgramInstance(
                    state_.ToShaderDefines()
                ));
            
            if(!mainShader_){
                REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load mainShader resource.");
                return false;
            }

            return true;

        }

};


}