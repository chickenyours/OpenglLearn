#include "scene_tree.h"

#include <cassert>

#include "code/ToolAndAlgorithm/container_algorithm.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"

#include <sstream>

using namespace ECS;
using namespace ECS::System;



void SceneTreeSystem::ApplyToRoot(EntityID entity){
    if(!entity){
        LOG_ERROR(systemName_, "Invaild entity: " + std::to_string(entity));
    }
    ECS::Component::Hierarchy* entityComponent = reg_->GetComponent<ECS::Component::Hierarchy>(entity);
    if(!entityComponent){
        LOG_INFO(systemName_,"Entity to apply to root have not Hierarchy Component before: " + std::to_string(entity));
        entityComponent = reg_->AddComponent<ECS::Component::Hierarchy>(entity);
    }
    if(!entityComponent->inSceneTree){
        roots_.push_back(entity);
        entityComponent->inSceneTree = true;
    }
}

void SceneTreeSystem::SetParent(EntityID child, EntityID newParent) {

    if(newParent == INVALID_ENTITY){
        ApplyToRoot(child);
        return;
    }

    if(!child || !newParent || child == newParent){
        LOG_ERROR(systemName_, "Invaild entity in child or newParent: child: " + std::to_string(child) + " parent:" + std::to_string(newParent));
        return;
    }

    ECS::Component::Hierarchy* childComponent = reg_->GetComponent<ECS::Component::Hierarchy>(child);
    ECS::Component::Hierarchy* newParentComponent = reg_->GetComponent<ECS::Component::Hierarchy>(newParent);
    
    // 检测已经自动添加组件
    if(!childComponent || !newParentComponent){
        if(!childComponent){
            LOG_INFO(systemName_,"Child Entity have not Hierarchy Component before: " + std::to_string(child));
            childComponent = reg_->AddComponent<ECS::Component::Hierarchy>(child);
        }
        if(!newParentComponent){
            LOG_INFO(systemName_,"Parent Entity have not Hierarchy Component before: " + std::to_string(newParent));
            newParentComponent = reg_->AddComponent<ECS::Component::Hierarchy>(newParent);
        }
    }


    // newParent 如果不在场景树,则会添加到根
    if(!newParentComponent->inSceneTree){
        roots_.push_back(newParent);
        newParentComponent->inSceneTree = true;
    }
    
    if(childComponent->inSceneTree){ // child 如果已经在场景树,则需要做一些相应的改变

        // 解除oldParent与 child 的关系 (childComponent->parent延迟替换)
        EntityID oldParent = childComponent->parent;
        if(oldParent != INVALID_ENTITY){
            ECS::Component::Hierarchy* oldParentComponent = reg_->GetComponent<ECS::Component::Hierarchy>(oldParent);
            Algorithm::UnorderValueEraseFirst(oldParentComponent->children,child);
        }
        else{
            // 既然child没父,则如果在roots_, 则移除
            auto rootIt = std::find(roots_.begin(),roots_.end(),child);
            if(rootIt != roots_.end()){
                Algorithm::UnorderIndexErase(roots_,std::distance(roots_.begin(),rootIt));
            }
        }
    }
    else{
        childComponent->inSceneTree = true;
    }
        // // 调试
        // auto& e = reg_->View<ECS::Component::Hierarchy>();
        // for(EntityID it : e){
        //     std::cout << it <<std::endl;
        // }
    
    childComponent->parent = newParent;
    newParentComponent->children.push_back(child);
  
    
}

void SceneTreeSystem::RemoveFromOldParent(EntityID child) {
    ECS::Component::Hierarchy* childComponent = reg_->GetComponent<ECS::Component::Hierarchy>(child);
    if(!childComponent){
        LOG_ERROR(systemName_, "Child entity is not part of the scene tree, and even does not have a Hierarchy Component: " + std::to_string(child));
        return;
    }
    else{
        if(!childComponent->inSceneTree){
            LOG_ERROR(systemName_, "Child entity is not part of the scene tree: " + std::to_string(child));
            return;
        }
        EntityID oldParent = childComponent->parent;
        if(oldParent != INVALID_ENTITY){
            auto& oldChildren = reg_->GetComponent<ECS::Component::Hierarchy>(oldParent)->children;
            auto v1 = std::find(oldChildren.begin(),oldChildren.end(),child);
            if(v1 != oldChildren.end()){
                // 如果有父,则直接移动到roots_
                Algorithm::UnorderIndexErase(oldChildren,std::distance(oldChildren.begin(),v1));
                roots_.push_back(child);
            }
            else{
                LOG_FATAL(systemName_, "Parent entity must have child, but not");
            }
        }
        else{
            LOG_WARNING(systemName_, "OldParent entity is need to remove from child entity, but it is not exitd: " + std::to_string(child));
            return;
        }
    }
}

void SceneTreeSystem::RemoveEntity(EntityID entity) {
    if(!entity){
        LOG_ERROR(systemName_, "Invaild entity: " + std::to_string(entity));
        return;
    }
    ECS::Component::Hierarchy* entityComponent = reg_->GetComponent<ECS::Component::Hierarchy>(entity);
    if(!entityComponent){
        LOG_ERROR(systemName_, "the entity is not part of the scene tree, and even does not have a Hierarchy Component: " + std::to_string(entity));
        return;
    }
    else{
        if(!entityComponent->inSceneTree){
            LOG_ERROR(systemName_, "The entity is not part of the scene tree: " + std::to_string(entity));
            return;
        }
        else{

            // 如果有父,则直接解除父子关系
            EntityID parent = entityComponent->parent;
            if(parent != INVALID_ENTITY){
                ECS::Component::Hierarchy* parentComponent = reg_->GetComponent<ECS::Component::Hierarchy>(parent);
                auto& oldChildren = parentComponent->children;
                auto v1 = std::find(oldChildren.begin(),oldChildren.end(),entity);
                if(v1 != oldChildren.end()){
                    Algorithm::UnorderIndexErase(oldChildren,std::distance(oldChildren.begin(),v1));
                }
                else{
                    LOG_FATAL(systemName_, "Parent entity must have child, but not"); // 事实上在生产场景中需要做修复代码,而不是崩溃
                }
            }

            // 如果在roots_则移除
            auto rootIt  = std::find(roots_.begin(),roots_.end(),entity);
            if(rootIt  != roots_.end()){
                Algorithm::UnorderIndexErase(roots_,std::distance(roots_.begin(),rootIt));
            }

            // 处理 entity 的 children,将它们移动到roots_
            for(EntityID child : entityComponent->children){
                ECS::Component::Hierarchy* childComponent = reg_->GetComponent<ECS::Component::Hierarchy>(child);
                childComponent->parent = INVALID_ENTITY;
                // childComponent->inSceneTree = true;  // (可以在以后需要有孤立节点) ✅ 保证状态正确
                roots_.push_back(child);
            }

            // 重置
            entityComponent->parent = INVALID_ENTITY;
            entityComponent->children.clear();
            entityComponent->inSceneTree = false;
        }
    }
}

EntityID SceneTreeSystem::GetParent(EntityID child) const {
    auto childComponent = reg_->GetComponent<ECS::Component::Hierarchy>(child);
    if(childComponent){
        return childComponent->parent;
    }
    else{
        LOG_FATAL(systemName_,"No Component");
    }
    return childComponent->parent;
}


const std::vector<EntityID>& SceneTreeSystem::GetChildren(EntityID parent) const {
    auto parentComponent = reg_->GetComponent<ECS::Component::Hierarchy>(parent);
    if(parentComponent){
        return parentComponent->children;
    }
    else{
        LOG_FATAL(systemName_,"No Component");
    }
    return parentComponent->children;
}


// 非递归
void SceneTreeSystem::RemoveEntityRecursive(EntityID entity) {
    using namespace ECS::Component;

    auto* hierarchy = reg_->GetComponent<Hierarchy>(entity);
    if (!hierarchy || !hierarchy->inSceneTree) return;

    // 用显式栈防止过深递归
    std::vector<EntityID> stack;
    stack.push_back(entity);

    while (!stack.empty()) {
        EntityID current = stack.back();
        stack.pop_back();

        auto* h = reg_->GetComponent<Hierarchy>(current);
        if (!h || !h->inSceneTree) continue;

        // 将当前节点的子节点加入栈中
        for (EntityID child : h->children) {
            stack.push_back(child);
        }

        // 如果当前节点在 roots_ 中，移除它
        auto it = std::find(roots_.begin(), roots_.end(), current);
        if (it != roots_.end()) {
            std::swap(*it, roots_.back());
            roots_.pop_back();
        }

        // 解除其与父节点的连接
        if (h->parent != INVALID_ENTITY) {
            auto* parentH = reg_->GetComponent<Hierarchy>(h->parent);
            if (parentH) {
                auto& siblings = parentH->children;
                auto childIt = std::find(siblings.begin(), siblings.end(), current);
                if (childIt != siblings.end()) {
                    siblings.erase(childIt);
                }
            }
        }

        // 清理当前节点的层次信息
        h->parent = INVALID_ENTITY;
        h->children.clear();
        h->inSceneTree = false;
    }
}
    



void SceneTreeSystem::UpdateTransforms() {
    for(auto it : roots_){
        UpdateRecursive(it, glm::mat4(1.0));
    }
}

// 优化潜力	⚠️ 可以引入 dirty flag、非递归结构、quaternion 优化旋转
void SceneTreeSystem::UpdateRecursive(EntityID entity, const glm::mat4& parentMatrix) {
    auto entityTransform = reg_->GetComponent<ECS::Component::Transform>(entity);
    auto entityHierarchy = reg_->GetComponent<ECS::Component::Hierarchy>(entity);
    auto& children = entityHierarchy->children;
    if(entityTransform){
        // entityTransform->UpdateLocalMatrix();
        entityTransform->worldMatrix = parentMatrix * entityTransform->localMatrix;
        for(auto child : children){
            UpdateRecursive(child, entityTransform->worldMatrix);
        }
    }
    else{
        for(auto child : children){
            UpdateRecursive(entity, parentMatrix);
        }
    }   

}

void SceneTreeSystem::Update(){
    UpdateTransforms();
}

const int wordLength = 12;

void SceneTreeSystem::Print() const {
    std::ostringstream oss;
    int tabs = 0;
    if(!roots_.empty()){
        oss << "Root" << '\n';
        std::string line;
        line.resize(1024);          // 任性分 1kb 给缓存

        for (size_t i = 0; i < roots_.size() - 1; ++i) {
            line.replace(0,wordLength,"├───");
            PrintRecursive(roots_[i], line, 1, oss); 
            
        }
        // 最后一行处理
        line.replace(0,wordLength,"└───");
        PrintRecursive(roots_.back(), line, 1 ,oss);
    }
    else{
        oss << "[EMPTY ROOT]" << '\n';
    }
    // 输出
    LOG_INFO(systemName_, "\n" + oss.str());
}

void SceneTreeSystem::PrintRecursive(EntityID entity, std::string& prefix, int tabs, std::ostringstream& oss) const {
    auto* entityComponent = reg_->GetComponent<ECS::Component::Hierarchy>(entity);
    bool vaild = entityComponent && entityComponent->inSceneTree;
    std::string preBranch = prefix.substr((tabs - 1) * wordLength, wordLength);
    bool debugflag = false;
    std::string_view view(prefix.data(),tabs * wordLength);
    if(debugflag){
        std::cout<<view<<std::endl;
    }
    if(vaild){
        oss << view << entity << '\n';
        auto& children = entityComponent->children;
        if(!children.empty()){
            // 遍历children并生成下一个前缀
            if(preBranch == "├───"){
                prefix.replace((tabs - 1) * wordLength, wordLength, "│   ");
            }
            else if(preBranch == "└───"){
                prefix.replace((tabs - 1) * wordLength, wordLength, "    ");
            }
            else{
                LOG_ERROR(systemName_,"something wrong");
            }
           
          
            for(size_t i = 0; i < children.size() - 1; ++i){ // children.size() - 1 如果 children.size() 是 0 则会出现极大的树,因为是 undigned __int64 
                prefix.replace(tabs * wordLength, wordLength ,"├───");
                PrintRecursive(children[i], prefix, tabs + 1, oss);
            }
            prefix.replace(tabs * wordLength, wordLength ,"└───");
            PrintRecursive(children.back(), prefix, tabs + 1, oss);
        }
    }
    else{
        oss << view << LOG_ERROR_COLOR << "[ERROR_COMPONENT]: " << entity << LOG_INFO_COLOR << '\n';
    }
}

bool SceneTreeSystem::AddEntity(EntityID entity, ECS::Core::ComponentRegister& reg){
    return false;
}
