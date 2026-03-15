#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <vector>
#include <queue>
#include <json/json.h>

#include "engine/ECS/data_type.h"
#include "engine/ECS/Entity/entity.h"

#include "engine/DebugTool/ConsoleHelp/color_log.h"
#include "engine/ToolAndAlgorithm/container_algorithm.h"
#include "engine/ToolAndAlgorithm/object.h"
#include "engine/ECS/ArchType/archtype_manager.h"

namespace ECS{
    namespace Core{
        class ComponentRegister;
    }
    namespace System{
        class SceneTreeSystem;
    }
    
}

namespace ECS::Core{

    struct EntitySceneInfo{
        ArchType* ownArchtype = nullptr;
        size_t archtypeIndex;
        uint32_t generation = 1;
    };

    class Scene{

        private:
            std::vector<ObjectPtr<ArchTypeManager>> archtypeManagers_;
            std::vector<EntitySceneInfo> entity2entityInfo_;
            std::queue<EntityID> recycleEntityID_;
            uint32_t entityCount_ = 0;
        public:
            

            ArchTypeDescription& CreateArchTypeDescription(){
                uint32_t nowIndex = archtypeManagers_.size();
                archtypeManagers_.push_back(ObjectPtr<ArchTypeManager>(nowIndex));
                return archtypeManagers_[nowIndex]->description_;
            }

            ObjectWeakPtr<ArchType> CreateArchType(ArchTypeDescription& description, size_t sizePerChuck){
                ArchTypeManager* manager = archtypeManagers_[description.responseManager_->sortKey_].Get();
                if(&manager->description_ == &description){
                    manager->CreateArchType(sizePerChuck);
                }
                else{
                    return;
                }
            }

            void DeleteArchType(ArchType* archtype){
                if(archtypeManagers_[archtype->manager_->sortKey_].Get() != archtype->manager_){
                    LOG_ERROR("archtypeManagers_", "not match manager");
                    return;
                }   
                archtypeManagers_[archtype->manager_->sortKey_]->ReleaseArchType(archtype);
            }

            EntityHandle CreateEntity(ArchType* archtype){
                EntityHandle result;
                if(!Check(archtype)){
                    return result;
                }
               
                size_t index = archtype->CreateUnit();

                // 分配ID
                if(!recycleEntityID_.empty()){
                    EntityID ReUseID = recycleEntityID_.front();
                    recycleEntityID_.pop();
                    entity2entityInfo_[ReUseID].ownArchtype = archtype;
                    result.id_ = ReUseID;
                }
                else{
                    EntitySceneInfo info;
                    info.archtypeIndex = index;
                    info.ownArchtype = archtype;
                    result.id_ = ++entityCount_;
                    entity2entityInfo_.push_back(info);

                }
                return result;
            }

            bool Check(ArchType* archtype){
                return archtypeManagers_[archtype->manager_->sortKey_].Get() == archtype->manager_;
            }

            void DeleteEntity(EntityHandle entity){
                if(entity.id_ < entity2entityInfo_.size() && entity2entityInfo_[entity.id_].ownArchtype){
                    entity2entityInfo_[entity.id_].ownArchtype->DeleteUnit(entity2entityInfo_[entity.id_].archtypeIndex);
                    entity2entityInfo_[entity.id_].ownArchtype = nullptr;
                    entity2entityInfo_[entity.id_].generation++;
                    recycleEntityID_.push(entity.id_);
                }
            }
        public:
            Scene();
            bool LoadFromConfigFile(const std::string& filePath, Log::StackLogErrorHandle errHandle = nullptr);
            std::unique_ptr<ECS::ComponentRegister> registry_;
            std::unique_ptr<ECS::System::SceneTreeSystem> hierarchySystem_;
            
            // return INVALID_ENTITY if uuid can't find entity in uuidToEntity 
            EntityID GetEntity(std::string uuid){
                auto entityItor = uuidToEntity_.find(uuid);
                if(entityItor != uuidToEntity_.end()){
                    return entityItor->second;
                }
                return INVALID_ENTITY;
            }

            EntityHandle CreateNewEntity(){
                EntityID newID = counter.GetNewEntity();
                return EntityHandle(newID);
            }

            size_t GetCount() {return counter.count;}

            bool HasEntity(EntityHandle entity){
                return entityInfo.count(entity.GetID());
            }

            std::vector<EntityID> GetAllEntities(){
                return Algorithm::GetKeys(entityInfo);
            }
        private:
            class EntityCounter{
                public:
                    EntityID GetNewEntity(){return ++count;}
                    EntityID count = 0u;
            } counter;

            std::string name_;
            Json::Value source_;

            std::unordered_map<std::string, EntityID> uuidToEntity_;
            std::unordered_set<std::string> uuidToPrefab_;
            std::unordered_map<EntityID,EntitySceneInfo> entityInfo;

            // 遍历处理scene items, 区分类型(如entity, prefab) 动态分配entityID, 自动构造上下层级 
            // 更新entityMataDataMap_ 和 GlobalIDToEntityIDMap_
            void EntityIDDistribute(std::vector<Json::Value*>& itemsMataDataArray, Log::StackLogErrorHandle errHandle = nullptr);
    };
}

#include "engine/ECS/Component/component_register.h"
#include "engine/ECS/System/SceneTree/scene_tree.h"