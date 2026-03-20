#pragma once

#include <cstdint>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/ToolAndAlgorithm/object.h"
#include "engine/ECS/ArchType/archtype_description.h"

namespace ECS::Core{
    class ArchType;
    class ArchTypePreloadInstance;
    class ArchTypeDescription;
}

namespace ECS::Core{
    class Scene;

    class ArchTypeManager{
        friend class Scene;
        friend class ArchType;
        friend class ArchTypePreloadInstance;
        friend class ArchTypeDescription;

    private:
        std::unordered_map<ArchType*, ObjectPtr<ArchType>> registeredArchTypeArray_;
        std::unordered_map<ArchTypePreloadInstance*, ObjectPtr<ArchTypePreloadInstance>> registeredPreloadArray_;
        ObjectPtr<ArchTypeDescription> description_;
        uint32_t sortKey_ = 0;
        bool destroying_ = false;

    public:
        explicit ArchTypeManager(uint32_t sortKey);
        ~ArchTypeManager();
    
        ObjectWeakPtr<ArchTypeDescription> GetDescription();
        ObjectWeakPtr<ArchType> CreateArchType(size_t sizePerChuck);
        void DestroyArchType(ArchType* archType);

        bool InitArchType(ArchType* archtype, size_t chunkScale);
        bool ResponseAdd(std::type_index typeIndex);
        void ReleaseArchTypeStorage(ArchType* archtype);

        bool ConstructComponentStorage(std::type_index typeIndex, size_t chunkScale, void*& outStorage);
        bool DestroyComponentStorage(std::type_index typeIndex, void* storage);
        void PrepareArchTypeForDestruction(ArchType* archtype);

        ObjectWeakPtr<ArchTypePreloadInstance> CreatePreload(size_t sizePerChunk);
        void DestroyPreloadInstance(ArchTypePreloadInstance* preload);

        bool InitPreloadInstance(ArchTypePreloadInstance* preload, size_t chunkScale);
        void ReleaseArchTypePreloadStorage(ArchTypePreloadInstance* preload);
        void PreparePreloadForDestruction(ArchTypePreloadInstance* preload);
    };
}
