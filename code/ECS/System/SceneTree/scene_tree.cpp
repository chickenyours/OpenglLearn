#include "scene_tree.h"

#include "code/ToolAndAlgorithm/container_algorithm.h"


using namespace ECS;
using namespace ECS::System;

void SceneTreeSystem::SetParent(EntityID child, EntityID newParent) {
    // 测试是否有组件
    RemoveFromOldParent(child);
    if(reg_->HasComponent<ECS::Component::Hierarchy>(child) && 
    reg_->HasComponent<ECS::Component::Hierarchy>(newParent)){
        reg_->GetComponent<ECS::Component::Hierarchy>(child).parent = newParent;
        reg_->GetComponent<ECS::Component::Hierarchy>(newParent).children.push_back(child);
    }
    // parentMap_[child] = newParent; 
    // childrenMap_[newParent].push_back(child);
    
}

void SceneTreeSystem::RemoveFromOldParent(EntityID child) {
    EntityID& parent = reg_->GetComponent<ECS::Component::Hierarchy>(child).parent;
    if(parent != INVALID_ENTITY){
        auto& children =  reg_->GetComponent<ECS::Component::Hierarchy>(parent).children;
        auto v1 = std::find(children.begin(),children.end(),child);
        if(v1 != children.end()){
            Algorithm::UnorderIndexErase(children,std::distance(children.begin(),v1));
        }
        parent = INVALID_ENTITY;
    }
    else{
        auto v2 = std::find(roots_.begin(), roots_.end(), child);
        if (v2 != roots_.end()){
            Algorithm::UnorderIteratorErase(roots_, v2);
        }
    }
}

void SceneTreeSystem::RemoveEntity(EntityID entity) {
    RemoveFromOldParent(entity);
    for (EntityID child : childrenMap_[entity]) {
        parentMap_.erase(child);
    }
    childrenMap_.erase(entity);
    // 从 roots 移除
    auto v1 = std::find(roots_.begin(), roots_.end(), entity);
    if (v1 != roots_.end()){
        Algorithm::UnorderIteratorErase(roots_, v1);
    }
}

EntityID SceneTreeSystem::GetParent(EntityID child) const {
    auto it = parentMap_.find(child);
    return it != parentMap_.end() ? it->second : INVALID_ENTITY;
}

const std::vector<EntityID>& SceneTreeSystem::GetChildren(EntityID parent) const {
    static const std::vector<EntityID> empty;
    auto it = childrenMap_.find(parent);
    return it != childrenMap_.end() ? it->second : empty;
}



void SceneTreeSystem::UpdateTransforms(Core::ComponentStorage<Component::Transform>& transforms) {
    for (EntityID entity : transforms.GetEntities()) {
        if (parentMap_.find(entity) == parentMap_.end()) {
            transforms.Get(entity).UpdateLocalMatrix();
            UpdateRecursive(entity, glm::mat4(1.0f), transforms);
        }
    }
}

void SceneTreeSystem::UpdateRecursive(EntityID entity, const glm::mat4& parentMatrix,
                                      Core::ComponentStorage<Component::Transform>& transforms) {
    auto& transform = transforms.Get(entity);
    transform.worldMatrix = parentMatrix * transform.localMatrix;

    for (EntityID child : GetChildren(entity)) {
        transforms.Get(child).UpdateLocalMatrix();
        UpdateRecursive(child, transform.worldMatrix, transforms);
    }
}


