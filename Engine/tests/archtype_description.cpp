#include "engine/ECS/ArchType/archtype_description.h"

namespace ECS::Core{
    ArchTypeDescription::ArchTypeDescription(ArchTypeManager* manager)
        : responseManager_(manager) {}

    void ArchTypeDescription::OnManagerDestroying(){
        responseManager_ = nullptr;
    }
}
