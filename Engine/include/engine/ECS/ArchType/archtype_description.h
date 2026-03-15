#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "engine/ECS/data_type.h"
#include "engine/ToolAndAlgorithm/DateType/chunk_array.h"
#include "engine/ECS/Component/component_loader_registry.h"
#include "engine/DebugTool/ConsoleHelp/color_log.h"
#include "engine/ToolAndAlgorithm/object.h"


namespace ECS::Core{
    class ArchType;
    class ArchTypeManager;
    class Scene;

    template <typename ComponentT>
    struct EntityComponentHandle{
        ObjectWeakPtr<ArchType> ownerArchType;
        void* chunkAddr = nullptr;
        EntityID owner = 0;
        size_t cacheIndex = 0;
        uint32_t generation = 0;

        ComponentT* Get();
    };

    class ArchTypeDescription{
        friend class ArchType;
        friend class ArchTypeManager;
        friend class Scene;

    public:
        using AddFunction = void(*)(void*);
        using DeleteFunction = void(*)(void*, size_t);
        using SwapFunction = void(*)(void*, size_t, size_t);
        using SwapBetweenFunction = void(*)(void*, size_t, void*, size_t);

        explicit ArchTypeDescription(ArchTypeManager* manager = nullptr);

        void OnManagerDestroying();

        template <typename ComponentT>
        static void Append(void* chunkAddr);

        template <typename ComponentT>
        static void Delete(void* chunkAddr, size_t index);

        template <typename ComponentT>
        static void Swap(void* chunkAddr, size_t indexA, size_t indexB);

        template <typename ComponentT>
        static void SwapBetween(void* chunkAddrA, size_t indexA, void* chunkAddrB, size_t indexB);

        template <typename ComponentT>
        static ComponentT* GetActiveComponentRaw(ArchType* archtype, size_t index);

        template <typename ComponentT>
        EntityComponentHandle<ComponentT> GetActiveComponent(ObjectWeakPtr<ArchType> archtype, EntityID entity);

        template <typename ComponentT>
        void AddComponentArray();

        template <typename ComponentT>
        bool TryGetComponentArray(size_t& out) const;

    private:
        std::unordered_map<std::type_index, size_t> componentArrayDescription_;
        std::vector<std::type_index> index2ComponentArrayType;
        size_t componentKinds_ = 0;

        std::vector<AddFunction> addFunctions_;
        std::vector<DeleteFunction> deleteFunctions_;
        std::vector<SwapFunction> swapFunctions_;
        std::vector<SwapBetweenFunction> swapBetweenFunctions_;

        ArchTypeManager* responseManager_ = nullptr;
    };
}

#include "engine/ECS/ArchType/archtype_manager.h"
#include "engine/ECS/ArchType/archtype_description_impl.h"
