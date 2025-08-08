#pragma once

#include <vector>
#include <unordered_map>

#include "code/ECS/data_type.h"

#include "code/ECS/System/system.h"

#include "code/ECS/Component/Transform/transform.h"
#include "code/ECS/Component/Hierarchy/hierarchy.h"


namespace ECS::System{

    // 场景树系统,管理一套层次关系,所有实体调用方法会引入场景树.但如果有entity不使用这些方法,那它就是隐式entity.
    // Hierarchy 组件的专用系统
    // 目前版本对支持多场景树相对弱
    class SceneTreeSystem:public System {
        public:

            virtual bool InitDerive() override {
                return true;
            }

            virtual bool AddEntity(EntityID entity) override;

            void SetComponentRegister(ECS::Core::ComponentRegister* reg) {reg_ = reg;}

            SceneTreeSystem():System("SceneTreeSystem"){}

            // 添加到根. 自动为entity添加Hierarchy. 引起相关改变的说明:
            // 1. entity 已经属于某个场景树,则会直接移动到roots_
            // 可能的失败情况:
            // 1.如果 entity 无效,则退出
            void ApplyToRoot(EntityID entity);

            // 设置关系(自动为 child 和 newParent 添加 Hierarchy 组件) 
            // 失败情况:
            // 1.如果child或newParent是无效或child是newParent,则退出
            // 引起相关改变的说明:
            // 1.如果child有oldParent, 则会解除父子关系
            // 2.如果child在roots_,则会从roots_移除, 如果没有
            // 3.如果newParent不在场景树,则添加到roots_           
            void SetParent(EntityID child, EntityID newParent);

            // 解除其父子关系, 引起相关改变的说明:
            // child会移动到roots_
            // 失败情况:
            // 1.如果child或child没有Hierarchy组件或child的不在场景树或child的parent无效则退出
            void RemoveFromOldParent(EntityID child);

            // 将entity移除出场景树, 引起的相关的改变的说明:
            // 1.如果entity在roots_则直接从roots_移出; 
            // 2.所有子entity直接接入roots_,
            // 3.解除父子关系
            void RemoveEntity(EntityID entity);

            void RemoveEntityRecursive(EntityID entity);
            
            EntityID GetParent(EntityID child) const;

            const std::vector<EntityID>& GetChildren(EntityID parent) const;
            
            // 更新场景树的变换transform矩阵
            void UpdateTransforms();

            // 打印当前状态
            void Print() const;

            virtual void Update() override;

        private:

            ECS::Core::ComponentRegister* reg_;

            

            // 无序容器
            std::vector<EntityID> roots_;
            void PrintRecursive(EntityID entity, std::string& prefix, int tabs, std::ostringstream& oss) const;

            void UpdateRecursive(EntityID entity, const glm::mat4& parentMatrix);
    };
        
}