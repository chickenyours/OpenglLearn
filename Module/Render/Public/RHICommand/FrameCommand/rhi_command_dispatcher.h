#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <tuple>
#include <type_traits>
#include <vector>
#include <stdexcept>
#include <iostream>

namespace Render{
    struct CommandId {
        size_t value;
    };
    
    constexpr std::size_t to_index(CommandId id) {
        return id.value;
    }
    
    // ============================================================
    // 2. 命令映射表：类型 -> 编号，编号 -> 类型
    // ============================================================
    
    template <class... Cmds>
    struct CommandTable {
        using tuple_type = std::tuple<Cmds...>;
    
        static constexpr std::size_t size = sizeof...(Cmds);
        static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    
    private:
        template <class T, std::size_t I = 0>
        static consteval std::size_t index_of_impl() {
            if constexpr (I == size) {
                return npos;
            } else if constexpr (std::is_same_v<T, std::tuple_element_t<I, tuple_type>>) {
                return I;
            } else {
                return index_of_impl<T, I + 1>();
            }
        }
    
    public:
        template <class T>
        static constexpr bool contains =
            index_of_impl<T>() != npos;
    
        // 类型 -> CommandId
        template <class T>
        static consteval CommandId id_of() {
            constexpr auto id = index_of_impl<T>();
            static_assert(id != npos, "command type is not registered");
            return CommandId{id};
        }
    
        // CommandId -> 类型
        template <CommandId Id>
        using type_of = std::tuple_element_t<to_index(Id), tuple_type>;
    
        static constexpr bool valid(CommandId id) {
            return to_index(id) < size;
        }
    
        template <CommandId Id, class T>
        struct id_is_impl {
            static constexpr bool value =
                [] consteval {
                    if constexpr (to_index(Id) >= size) {
                        return false;
                    } else {
                        return std::is_same_v<type_of<Id>, T>;
                    }
                }();
        };
    
        template <CommandId Id, class T>
        static constexpr bool id_is = id_is_impl<Id, T>::value;
    };
}


