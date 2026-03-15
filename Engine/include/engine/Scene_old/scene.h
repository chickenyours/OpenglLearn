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
#include "engine/ECS/ArchType/archtype_description.h"

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
        ArchType* ownArchtype;
        uint32_t generation = 1;
        bool alive = false;
    };

    class Scene{
    private:
        std::vector<ObjectPtr<ArchTypeManager>> archtypeManagers_;
        std::vector<EntitySceneInfo> entity2entityInfo_;
        std::queue<EntityID> recycleEntityID_;
        uint32_t entityCount_ = 0;

        bool Check(ArchType* archtype) const{
            if(archtype == nullptr || archtype->manager_ == nullptr){
                return false;
            }
            const size_t key = archtype->manager_->sortKey_;
            if(key >= archtypeManagers_.size()){
                return false;
            }
            return archtypeManagers_[key].Get() == archtype->manager_;
        }

    public:
        Scene(){
            // 让 0 号 EntityID 保持无效位
            entity2entityInfo_.push_back(EntitySceneInfo{});
        }

        ArchTypeDescription* CreateArchTypeDescription(){
            const uint32_t nowIndex = static_cast<uint32_t>(archtypeManagers_.size());
            archtypeManagers_.push_back(ObjectPtr<ArchTypeManager>(nowIndex));
            return &archtypeManagers_[nowIndex]->description_;
        }

        ObjectWeakPtr<ArchType> CreateArchType(ArchTypeDescription* description, size_t sizePerChuck){
            if(description == nullptr || description->responseManager_ == nullptr){
                LOG_ERROR("Scene::CreateArchType", "description or response manager is null");
                return {};
            }

            const size_t key = description->responseManager_->sortKey_;
            if(key >= archtypeManagers_.size()){
                LOG_ERROR("Scene::CreateArchType", "manager key oversize");
                return {};
            }

            ArchTypeManager* manager = archtypeManagers_[key].Get();
            if(manager == nullptr || &manager->description_ != description){
                LOG_ERROR("Scene::CreateArchType", "description and manager not match");
                return {};
            }

            return manager->CreateArchType(sizePerChuck);
        }

        void DeleteArchType(ArchType* archtype){
            if(!Check(archtype)){
                LOG_ERROR("Scene::DeleteArchType", "archtype manager not match");
                return;
            }
            archtypeManagers_[archtype->manager_->sortKey_]->DestroyArchType(archtype);
        }

        EntityHandle CreateEntity(ArchType* archtype){
            EntityHandle result;
            if(!Check(archtype)){
                LOG_ERROR("Scene::CreateEntity", "invalid archtype");
                return result;
            }

            EntityID newID = 0;
            uint32_t generation = 1;

            if(!recycleEntityID_.empty()){
                newID = recycleEntityID_.front();
                recycleEntityID_.pop();

                entity2entityInfo_[newID].ownArchtype = archtype;
                entity2entityInfo_[newID].alive = true;
                generation = entity2entityInfo_[newID].generation;
            }else{
                newID = ++entityCount_;
                entity2entityInfo_.push_back(EntitySceneInfo{});
                entity2entityInfo_[newID].ownArchtype = archtype;
                entity2entityInfo_[newID].alive = true;
                entity2entityInfo_[newID].generation = 1;
                generation = 1;
            }

            const size_t index = archtype->CreateEntity(newID);
            if(index == ARCHTYPE_INVALID_INDEX){
                entity2entityInfo_[newID].ownArchtype = nullptr;
                entity2entityInfo_[newID].alive = false;
                if(newID == entityCount_){
                    entity2entityInfo_.pop_back();
                    --entityCount_;
                }else{
                    recycleEntityID_.push(newID);
                }
                LOG_ERROR("Scene::CreateEntity", "archtype create entity failed");
                return {};
            }

            result.id_ = newID;
            result.generation_ = generation;
            return result;
        }

        void DeleteEntity(EntityHandle entity){
            if(entity.id_ == 0 || entity.id_ >= entity2entityInfo_.size()){
                return;
            }

            EntitySceneInfo& info = entity2entityInfo_[entity.id_];
            if(!info.alive){
                return;
            }

            if(entity.generation_ != 0 && entity.generation_ != info.generation){
                LOG_WARNING("Scene::DeleteEntity", "generation mismatch");
                return;
            }

            ArchType* archtype = info.ownArchtype;
            if(archtype == nullptr){
                info.alive = false;
                ++info.generation;
                recycleEntityID_.push(entity.id_);
                return;
            }

            archtype->DeleteEntity(entity.id_);
            info.ownArchtype = nullptr;
            info.alive = false;
            ++info.generation;
            recycleEntityID_.push(entity.id_);
        }

        template <typename ComponentT>
        EntityComponentHandle<ComponentT> GetActiveComponent(EntityID entity){
            EntityComponentHandle<ComponentT> handle;
            if(entity == 0 || entity >= entity2entityInfo_.size()){
                LOG_ERROR("Scene::GetActiveComponent", "invalid entity id");
                return handle;
            }

            const EntitySceneInfo& info = entity2entityInfo_[entity];
            if(!info.alive || !info.ownArchtype){
                return handle;
            }

            return info.ownArchtype->description_->GetActiveComponent<ComponentT>(info.ownArchtype, entity);
        }

        bool IsAlive(EntityHandle entity) const{
            if(entity.id_ == 0 || entity.id_ >= entity2entityInfo_.size()){
                return false;
            }
            const auto& info = entity2entityInfo_[entity.id_];
            return info.alive && info.generation == entity.generation_;
        }
    };
}