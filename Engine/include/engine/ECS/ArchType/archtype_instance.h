#pragma once

#include <vector>

namespace ECS::Core{
    class ArchTypeManager;
    class ArchTypeDescription;
}

#include "engine/ECS/ArchType/archtype_manager.h"

namespace ECS::Core{

    constexpr size_t ARCHTYPE_SMALL = 32;
    constexpr size_t ARCHTYPE_MEDIUM = 128;
    constexpr size_t ARCHTYPE_LARGE = 512;



    class ArchType{
        friend class ArchTypeDescription;
        friend class ArchTypeManager;
        public:
            size_t size(){ return size_; }
            ~ArchType(){
                if(!manager_ || !description_){
                    LOG_ERROR("ArchType::~ArchType","no manager_ or description_, fail to release");
                }
                manager_->ReleaseArchType(this);
            }
            template <typename ComponentT>
            const FixedChuckArray<ComponentT>* TryCastComponentArray() const {
                if(description_){
                    auto it = description_->componentArrayDescription_.find(std::type_index(typeid(ComponentT)));
                    if(it == description_->componentArrayDescription_.end()){
                        LOG_ERROR("ArchType::TryCastComponentArray", "no component type in this archtype, in " + std::string(__FUNCTIONW__));
                        return nullptr;
                    }
                    size_t& index = it->second;
                    if(index >= size_){
                        LOG_ERROR("ArchType::TryCastComponentArray", "index is over size" + std::to_string(index) + std::to_string(size_));
                        return nullptr;
                    }
                    return reinterpret_cast<const FixedChuckArray<ComponentT>*>(addr2ComponentDenseArray[index]);
                }
                return nullptr;
            }
        private:
            size_t size_ = 0;
            size_t sizePerChuck_;
            std::vector<void*> addr2ComponentDenseArray;
            ArchTypeDescription* description_ = nullptr;
            ArchTypeManager* manager_ = nullptr;

            ArchType(ArchTypeDescription* description, ArchTypeManager* manager, size_t sizePerChuck):description_(description),sizePerChuck_(sizePerChuck),manager_(manager){}
            
    };
}