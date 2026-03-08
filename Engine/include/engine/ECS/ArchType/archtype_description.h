#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <typeindex>
#include <string>

namespace ECS::Core{
    class ArchTypeManager;
    class Scene;
}

namespace ECS::Core{
    class ArchTypeDescription{
        friend class ArchType;
        friend class ArchTypeManager;
        friend class Scene;
        private:
            std::unordered_map<std::type_index,size_t> componentArrayDescription_;
            std::vector<std::type_index> index2ComponentArrayType;
            ArchTypeManager* responseManager_ = nullptr;
            uint32_t sortKey_;
            size_t componentKinds_ = 0;
            ArchTypeDescription(ArchTypeManager* manager,uint32_t sortKey):responseManager_(manager),sortKey_(sortKey){}
        public:
            ~ArchTypeDescription(){
}

            
            template <typename ComponentT>
            void AddComponentArray(){
                auto index = std::type_index(typeid(ComponentT));
                if(!componentArrayDescription_.count(index)){
                    index2ComponentArrayType.push_back(index);
                    componentArrayDescription_[index] = componentKinds_++;
                    if(responseManager_){
                        responseManager_->ResponseAdd(index);
                    }
                }
            }

            template <typename ComponentT>
            bool TryGetComponentArray(size_t& out) const {
                auto index = std::type_index(typeid(ComponentT));
                auto comp = componentArrayDescription_.find(index);
                if(comp != componentArrayDescription_.end()){
                    out = comp->second;
                    return true;
                }
                return false;
            }



    };
}