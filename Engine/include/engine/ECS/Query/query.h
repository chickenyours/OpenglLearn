#pragma once

#include <tuple>
#include <type_traits>
#include <vector>

#include "engine/ECS/ArchType/archtype_description.h"
#include "engine/ECS/ArchType/archtype_instance.h"

namespace ECS::Core{

    namespace Detail{

        template<typename T>
        bool HasComponent(ArchType* archType){
            if(archType == nullptr || !archType->Check() || archType->GetDescription() == nullptr){
                return false;
            }

            size_t index = 0;
            return archType->GetDescription()->TryGetComponentArray<T>(index);
        }

        template<typename... Args>
        bool HasAll(ArchType* archType){
            return (... && HasComponent<Args>(archType));
        }

        template<typename... Args>
        bool HasAny(ArchType* archType){
            if constexpr(sizeof...(Args) == 0){
                return false;
            }else{
                return (... || HasComponent<Args>(archType));
            }
        }

        template<typename... Args>
        bool HasNone(ArchType* archType){
            return (... && (!HasComponent<Args>(archType)));
        }

    } // namespace Detail


    template <typename ...Args>
    class Require{
    public:
        static bool Check(ArchType* archType){
            return Detail::HasAll<Args...>(archType);
        }
    };

    template <typename ...Args>
    class Optional{
    public:
        static bool Check(ArchType*){
            // Optional 不参与 archetype 过滤
            // 它只表示“如果有这些组件就顺便读”
            return true;
        }
    };

    template <typename ...Args>
    class Exclude{
    public:
        static bool Check(ArchType* archType){
            return Detail::HasNone<Args...>(archType);
        }
    };


    template <typename RequireT, typename OptionalT, typename ExcludeT>
    class Query{
    public:
        static bool CheckArchType(ArchType* archType){
            if(archType == nullptr || !archType->Check()){
                return false;
            }

            return RequireT::Check(archType)
                && OptionalT::Check(archType)
                && ExcludeT::Check(archType);
        }
    };

} // namespace ECS::Core