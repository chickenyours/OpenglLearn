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
        ObjectWeakPtr<ArchType> ownArchtype;
    };

    class Scene{

        private:
            std::vector<ObjectPtr<ArchTypeManager>> archtypeManagers_;
            std::vector<EntitySceneInfo> entity2entityInfo_;
            std::queue<EntityID> recycleEntityID_;
            uint32_t entityCount_ = 0;

            bool Check(ArchType* archtype){
                return archtypeManagers_[archtype->manager_->sortKey_].Get() == archtype->manager_;
            }


        public:
            

            ArchTypeDescription* CreateArchTypeDescription(){
                uint32_t nowIndex = archtypeManagers_.size();
                archtypeManagers_.push_back(ObjectPtr<ArchTypeManager>(nowIndex));
                return &archtypeManagers_[nowIndex]->description_;
            }

            ObjectWeakPtr<ArchType> CreateArchType(ArchTypeDescription* description, size_t sizePerChuck){
                ArchTypeManager* manager = archtypeManagers_[description->responseManager_->sortKey_].Get();
                if(&manager->description_ == description){
                    return manager->CreateArchType(sizePerChuck);
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

            void DeleteEntity(EntityHandle entity){
                if(entity.id_ < entity2entityInfo_.size() && entity2entityInfo_[entity.id_].ownArchtype){
                    entity2entityInfo_[entity.id_].ownArchtype->DeleteUnit(entity2entityInfo_[entity.id_].archtypeIndex);
                    entity2entityInfo_[entity.id_].ownArchtype = nullptr;
                    entity2entityInfo_[entity.id_].generation++;
                    recycleEntityID_.push(entity.id_);
                }
            }

            template <typename ComponentT>
            EntityComponentHandle<ComponentT> GetActiveComponent(EntityID entity){
                EntityComponentHandle<ComponentT> handle;
                if(entity < entity2entityInfo_.size()){
                    const EntitySceneInfo& info = entity2entityInfo_[entity];
                    handle = info.ownArchtype->description_->GetActiveComponent(info.ownArchtype, entity);
                }
                else{
                    LOG_ERROR("Scene::EntityComponentHandle", "error entity");
                }
                return handle;
            }
            
    };
}