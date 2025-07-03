#include "scene.h"


#include "code/ToolAndAlgorithm/Json/json_helper.h"



using namespace ECS;
using namespace ECS::Component;

static bool LoadEntityComponent(ECS::Core::ComponentRegister& reg, EntityID id, const Json::Value& mataData, Log::StackLogErrorHandle errHandle){
    const Json::Value* componentsMataData;
    if(!Tool::JsonHelper::TryGetObject(mataData,"components", componentsMataData)){
        REPORT_STACK_ERROR(errHandle, "scene->LoadEntity", "Missing 'components' object in entity metadata.");
        return false;
    }

    bool flag = true;

    for (Json::Value::const_iterator it = componentsMataData->begin(); it != componentsMataData->end(); ++it) {
        std::string key = it.name();
        const Json::Value& value = *it;

        auto& componentTypeMap = ECS::Component::GetComponentLoaderMap();
        // std::cout << key << std::endl; // debug
        auto com = componentTypeMap.find(key);
        if(com == componentTypeMap.end()){
            LOG_ERROR("scene->LoadEntity", "Unknown component type: " + key);
            if(flag) flag = false;
            continue;
        }
        if(!com->second(reg, id, value, errHandle) && flag) flag = false;
    }

    if(!flag){
        REPORT_STACK_ERROR(errHandle, "scene->LoadEntity", "Some errors happen when loading one or more components for entity ID " + std::to_string(id));
    }

    return flag;
}

class UnionFind {
public:
    bool emplace(EntityID x) {
        return parent_.emplace(x, x).second;
    }
    EntityID find(EntityID x) {
        if (parent_[x] != x) parent_[x] = find(parent_[x]);
        return parent_[x];
    }
    void unite(EntityID x, EntityID y) {
        x = find(x); y = find(y);
        if (x == y) return;
        if (rank_[x] < rank_[y]) std::swap(x, y);
        parent_[y] = x;
        if (rank_[x] == rank_[y]) ++rank_[x];
    }
private:
    std::unordered_map<EntityID, EntityID> parent_;
    std::unordered_map<EntityID, int>      rank_;
};

bool HasCycle_UnionFind(const std::unordered_map<EntityID, EntityID>& child2father)
{
    UnionFind uf;
    for (auto&& [child, father] : child2father) {
        uf.emplace(child);
        uf.emplace(father);
        if (uf.find(child) == uf.find(father))  // 环出现
            return true;
        uf.unite(child, father);
    }
    return false;  // 遍历完没有冲突 ⇒ 无环
}

Scene::Scene(){
    registry_ = std::make_unique<ECS::Core::ComponentRegister>();
    hierarchySystem_ = std::make_unique<ECS::System::SceneTreeSystem>();
    hierarchySystem_->SetComponentRegister(registry_.get());
}

bool Scene::LoadFromConfigFile(const std::string& filePath, Log::StackLogErrorHandle errHandle){

    if(!Tool::JsonHelper::LoadJsonValueFromFile(filePath,source_,errHandle)){
        REPORT_STACK_ERROR(errHandle, "Scene", "Failed to load JSON configuration file: " + filePath);
        return false;
    }

    std::string configType;
    if(!Tool::JsonHelper::TryGetString(source_, "configType", configType) || configType != "scene"){
        REPORT_STACK_ERROR(errHandle, "Scene", "Invalid or missing 'configType' in configuration file: " + filePath);
        return false;
    }

    Json::Value *scene;
    if(!Tool::JsonHelper::TryGetObject(source_,"scene", scene)){
        REPORT_STACK_ERROR(errHandle, "Scene", "Missing 'scene' object in configuration file: " + filePath);
        return false;
    }

    Tool::JsonHelper::TryGetString(*scene, "label", name_);

    Json::Value* args;
    if(!Tool::JsonHelper::TryGetObject(*scene, "args", args)){
        REPORT_STACK_ERROR(errHandle, "Scene", "Missing 'args' object in 'scene' configuration: " + filePath);
        return false;
    }

    Json::Value* decl;
    if(!Tool::JsonHelper::TryGetObject(*args, "decl", decl)){
        REPORT_STACK_ERROR(errHandle, "Scene", "Missing 'decl' object in 'args' configuration: " + filePath);
        return false;
    }

    Json::Value* hier;
    if(!Tool::JsonHelper::TryGetArray(*args, "hier", hier)){
        REPORT_STACK_ERROR(errHandle, "Scene", "Missing 'hier' object in 'args' configuration: " + filePath);
        return false;
    }

    std::vector<Json::Value*> hierArray;
    if(!Tool::JsonHelper::TryTraverseArray(*hier,hierArray)){
        REPORT_STACK_ERROR(errHandle, "Scene", "Failed to traverse 'hier' array in 'args' configuration: " + filePath);
        return false;
    }

    Json::Value* impl;
    if(!Tool::JsonHelper::TryGetObject(*args, "impl", impl)){
        REPORT_STACK_ERROR(errHandle, "Scene", "Missing 'impl' object in 'args' configuration: " + filePath);
        return false;
    }

    // decl 阶段

    Json::Value* entities;
    if(!Tool::JsonHelper::TryGetArray(*decl, "entities", entities)){
        REPORT_STACK_ERROR(errHandle, "Scene", "Missing 'entities' object in 'decl' configuration: " + filePath);
        return false;
    }

    Json::Value* prefabs;
    if(!Tool::JsonHelper::TryGetArray(*decl, "prefabs", prefabs)){
        REPORT_STACK_ERROR(errHandle, "Scene", "Missing 'prefabs' object in 'decl' configuration: " + filePath);
        return false;
    }

    std::vector<std::string> entityuuidList;
    if(!Tool::JsonHelper::TryTraverseArray(*entities,entityuuidList)){
        REPORT_STACK_ERROR(errHandle, "Scene", "Failed to traverse 'entities' array in 'decl' configuration: " + filePath);
        return false;
    }

    std::vector<std::string> prefabuuidList;
    if(!Tool::JsonHelper::TryTraverseArray(*prefabs, prefabuuidList)){
        REPORT_STACK_ERROR(errHandle, "Scene", "Failed to traverse 'prefabs' array in 'decl' configuration: " + filePath);
        return false;
    }

    for(const std::string& it : entityuuidList){
        if(uuidToEntity_.count(it)){
            REPORT_STACK_ERROR(errHandle, "Scene", "Duplicate uuid found in 'entities' array: " + it + " in 'decl' configuration: " + filePath);
            return false;
        }
        uuidToEntity_.try_emplace(it,counter.GetNewEntity());
    }

    for(const std::string& it : prefabuuidList){
        if(uuidToEntity_.count(it) || uuidToPrefab_.count(it)){
            REPORT_STACK_ERROR(errHandle, "Scene", "Duplicate uuid found in 'prefabs' array: " + it + " in 'decl' configuration: " + filePath);
            return false;
        }
        uuidToPrefab_.insert(it);
    }

    // hier
    std::unordered_map<EntityID, EntityID> childToFatherEntityMap;
    std::unordered_map<EntityID, std::vector<EntityID>> fatherToChildEntityMap;
    std::unordered_map<std::string, EntityID> prefabFatherEntity;
    for(auto it : hierArray){
        std::string fatheruuid;
        if(!Tool::JsonHelper::TryGetString(*it, "uuid", fatheruuid)){
            REPORT_STACK_ERROR(errHandle, "Scene", "Missing or invalid 'uuid' in 'hier' array element in 'args' configuration: " + filePath);
            return false;
        }

        auto itor = uuidToEntity_.find(fatheruuid);
        if(itor == uuidToEntity_.end()){
            REPORT_STACK_ERROR(errHandle, "Scene", "uuid in 'hier' array not found in 'entities' declaration: " + fatheruuid + " in 'args' configuration: " + filePath);
            return false;
        }
        EntityID fatherEntity = itor->second;

        std::vector<std::string> children;
        Json::Value* childrenJson;
        if(!Tool::JsonHelper::TryGetArray(*it, "children", childrenJson)){
            REPORT_STACK_ERROR(errHandle, "Scene", "Missing 'children' array in 'hier' element for uuid: " + fatheruuid + " in 'args' configuration: " + filePath);
            return false;
        }
        if(!Tool::JsonHelper::TryTraverseArray(*childrenJson, children)){
            REPORT_STACK_ERROR(errHandle, "Scene", "Failed to traverse 'children' array in 'hier' element for uuid: " + fatheruuid + " in 'args' configuration: " + filePath);
            return false;
        }

        for(const std::string childuuid : children){
            auto childentityitor = uuidToEntity_.find(childuuid);
            if(childentityitor != uuidToEntity_.end()){
                EntityID childEntity = childentityitor->second;
                if(childEntity == fatherEntity){
                    REPORT_STACK_ERROR(errHandle, "Scene", "Entity " + childuuid + " cannot be its own father in 'hier' configuration: " + filePath);
                    return false;
                }
                if(childToFatherEntityMap.count(childEntity)){
                    // 出现child有多个 father 的错误现象
                    REPORT_STACK_ERROR(errHandle, "Scene", "Child entity " + childuuid + " has multiple fathers in 'hier' configuration: " + filePath);
                    return false;
                }
                auto fatherfatheritor = childToFatherEntityMap.find(fatherEntity);
                childToFatherEntityMap.try_emplace(childEntity,fatherEntity);
                fatherToChildEntityMap[fatherEntity].push_back(childEntity);
            }
            else if(uuidToPrefab_.count(childuuid)){
                if(prefabFatherEntity.count(childuuid)){
                    REPORT_STACK_ERROR(errHandle, "Scene", "Prefab " + childuuid + " has multiple fathers in 'hier' configuration: " + filePath);
                    return false;
                }
                prefabFatherEntity.try_emplace(childuuid, fatherEntity);
            }
            else{
                REPORT_STACK_ERROR(errHandle, "Scene", "Unknown child uuid: " + childuuid + " in 'hier' configuration: " + filePath);
                return false;
            }
        }

        if(HasCycle_UnionFind(childToFatherEntityMap)){
            // 有环,关系无效
            REPORT_STACK_ERROR(errHandle, "Scene", "Cycle detected in entity hierarchy for 'hier' configuration: " + filePath);
            return false;
        }

    }

    // 建立关系
    for(auto entityitor : uuidToEntity_){
        EntityID entity = entityitor.second;
        auto childitor = fatherToChildEntityMap.find(entity);
        if(childitor != fatherToChildEntityMap.end()){
            for(EntityID childentity : childitor->second){
                hierarchySystem_->SetParent(childentity,entity); 
            }
        }
        else if(childToFatherEntityMap.count(entity)){
            EntityID fatherentity = childToFatherEntityMap[entity];
            hierarchySystem_->SetParent(entity, fatherentity);
        }
        else{
            hierarchySystem_->ApplyToRoot(entity);
        }
    }

    // 元数据填入初始化
    // scene实体有限初始化
    for(auto entityitor : uuidToEntity_){
        Json::Value* mataData;
        if(!Tool::JsonHelper::TryGetObject(*impl, entityitor.first, mataData)){
            LOG_WARNING("Scene", "Missing implementation metadata for entity uuid: " + entityitor.first + " in 'impl' configuration: " + filePath);
            continue;
        }
        if(!LoadEntityComponent(*registry_, entityitor.second, *mataData, errHandle)){
            REPORT_STACK_ERROR(errHandle, "Scene", "Some errors happen when loading entity with uuid: " + entityitor.first + " and ID: " + std::to_string(entityitor.second));
            continue;
        }
    }
    // 预制件展开和初始化
    


    return true;
}

void Scene::EntityIDDistribute(std::vector<Json::Value*>& itemsMataDataArray, Log::StackLogErrorHandle errHandle){
   
}