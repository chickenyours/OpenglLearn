#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <vector>
#include <Json/json.h>

#include "code/ECS/data_type.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"

namespace ECS{
    namespace Core{
        class ComponentRegister;
    }
    namespace System{
        class SceneTreeSystem;
    }
    
}

namespace ECS{



    class Scene{
        public:
            Scene();
            bool LoadFromConfigFile(const std::string& filePath, Log::StackLogErrorHandle errHandle = nullptr);
            std::unique_ptr<ECS::Core::ComponentRegister> registry_;
            std::unique_ptr<ECS::System::SceneTreeSystem> hierarchySystem_;

            // return INVALID_ENTITY if uuid can't find entity in uuidToEntity 
            EntityID GetEntity(std::string uuid){
                auto entityItor = uuidToEntity_.find(uuid);
                if(entityItor != uuidToEntity_.end()){
                    return entityItor->second;
                }
                return INVALID_ENTITY;
            }

            EntityID CreateNewEntity(){
                return counter.GetNewEntity();
            }

            EntityID GetCount() {return counter.count;}
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

            // 遍历处理scene items, 区分类型(如entity, prefab) 动态分配entityID, 自动构造上下层级 
            // 更新entityMataDataMap_ 和 GlobalIDToEntityIDMap_
            void EntityIDDistribute(std::vector<Json::Value*>& itemsMataDataArray, Log::StackLogErrorHandle errHandle = nullptr);
    };
}

#include "code/ECS/Component/component_register.h"
#include "code/ECS/System/SceneTree/scene_tree.h"