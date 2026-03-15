#pragma once

#include <cstdint>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/ECS/data_type.h"
#include "engine/ToolAndAlgorithm/object.h"
#include "engine/ECS/Component/component_loader_registry.h"
#include "engine/DebugTool/ConsoleHelp/color_log.h"
#include "engine/ECS/ArchType/archtype_description.h"
#include "engine/ECS/ArchType/archtype_instance.h"

namespace ECS::Core{
    class Scene;

    class ArchTypeManager{
        friend class Scene;
        friend class ArchType;
        friend class ArchTypeDescription;

    private:
        std::unordered_map<ArchType*, ObjectPtr<ArchType>> registeredArchTypeArray_;
        ArchTypeDescription description_;
        uint32_t sortKey_ = 0;
        bool destroying_ = false;
    public:
        explicit ArchTypeManager(uint32_t sortKey);
        ~ArchTypeManager();
    
        ArchTypeDescription& GetDescription();
        ObjectWeakPtr<ArchType> CreateArchType(size_t sizePerChuck);
        void DestroyArchType(ArchType* archType);

        bool InitArchType(ArchType* archtype, size_t chunkScale);
        bool ResponseAdd(std::type_index typeIndex);
        void ReleaseArchTypeStorage(ArchType* archtype);

        bool ConstructComponentStorage(std::type_index typeIndex, size_t chunkScale, void*& outStorage);
        bool DestroyComponentStorage(std::type_index typeIndex, void* storage);
        void PrepareArchTypeForDestruction(ArchType* archtype);
    };
}
