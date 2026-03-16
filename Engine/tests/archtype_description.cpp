#include "engine/ECS/ArchType/archtype_description.h"
#include "engine/ECS/ArchType/archtype_manager.h"

namespace ECS::Core{
    ArchTypeDescription::ArchTypeDescription(ArchTypeManager* manager)
        : responseManager_(manager) {}

    void ArchTypeDescription::OnManagerDestroying(){
        responseManager_ = nullptr;
    }

    bool ArchTypeDescription::NotifyManagerResponseAdd(std::type_index typeIndex){
        if(responseManager_ == nullptr){
            return true;
        }
        return responseManager_->ResponseAdd(typeIndex);
    }
}