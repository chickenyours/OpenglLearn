#pragma once

#include <tuple>
#include <type_traits>
#include <cstddef>

namespace Render{
    enum class BuiltinKind {
    I32,
    I64,
    F32,
    F64,
    Bool,

    Count
};

struct I32Type {};
struct I64Type {};
struct F32Type {};
struct F64Type {};
struct BoolType {};

template <auto K, class T>
struct EnumTypeEntry {
    static constexpr auto kind = K;
    using type = T;
};

struct EnumTypeMapNotFound {};

using BuiltinTypeMap = std::tuple<
    EnumTypeEntry<BuiltinKind::I32,  I32Type>,
    EnumTypeEntry<BuiltinKind::I64,  I64Type>,
    EnumTypeEntry<BuiltinKind::F32,  F32Type>,
    EnumTypeEntry<BuiltinKind::F64,  F64Type>,
    EnumTypeEntry<BuiltinKind::Bool, BoolType>
>;

constexpr int to_index(BuiltinKind k) {
    return static_cast<int>(k);
}

constexpr bool is_valid_builtin_kind(BuiltinKind k) {
    return to_index(k) >= 0 &&
           to_index(k) < to_index(BuiltinKind::Count);
}


// enum -> type

template <auto K, class Map>
struct enum_to_type;

template <auto K>
struct enum_to_type<K, std::tuple<>> {
    using type = EnumTypeMapNotFound;
    static constexpr bool found = false;
};

template <auto K, class Head, class... Tail>
struct enum_to_type<K, std::tuple<Head, Tail...>> {
private:
    using next = enum_to_type<K, std::tuple<Tail...>>;

public:
    static constexpr bool matched = (K == Head::kind);
    static constexpr bool found = matched || next::found;

    using type = std::conditional_t<
        matched,
        typename Head::type,
        typename next::type
    >;
};

template <auto K, class Map>
using enum_to_type_t = typename enum_to_type<K, Map>::type;


// type -> enum

template <class T, class Map>
struct type_to_enum;

template <class T>
struct type_to_enum<T, std::tuple<>> {
    static constexpr bool found = false;
    static constexpr BuiltinKind value = BuiltinKind::Count;
};

template <class T, class Head, class... Tail>
struct type_to_enum<T, std::tuple<Head, Tail...>> {
private:
    using next = type_to_enum<T, std::tuple<Tail...>>;

public:
    static constexpr bool matched =
        std::is_same_v<T, typename Head::type>;

    static constexpr bool found =
        matched || next::found;

    static constexpr BuiltinKind value =
        matched ? Head::kind : next::value;
};

template <class T, class Map>
inline constexpr auto type_to_enum_v = type_to_enum<T, Map>::value;


// duplicate enum check

template <auto K, class Map>
struct contains_enum;

template <auto K>
struct contains_enum<K, std::tuple<>> {
    static constexpr bool value = false;
};

template <auto K, class Head, class... Tail>
struct contains_enum<K, std::tuple<Head, Tail...>> {
    static constexpr bool value =
        (K == Head::kind) ||
        contains_enum<K, std::tuple<Tail...>>::value;
};

template <class Map>
struct no_duplicate_enum;

template <>
struct no_duplicate_enum<std::tuple<>> {
    static constexpr bool value = true;
};

template <class Head, class... Tail>
struct no_duplicate_enum<std::tuple<Head, Tail...>> {
private:
    using Rest = std::tuple<Tail...>;

public:
    static constexpr bool value =
        !contains_enum<Head::kind, Rest>::value &&
        no_duplicate_enum<Rest>::value;
};


// duplicate type check

template <class T, class Map>
struct contains_type;

template <class T>
struct contains_type<T, std::tuple<>> {
    static constexpr bool value = false;
};

template <class T, class Head, class... Tail>
struct contains_type<T, std::tuple<Head, Tail...>> {
    static constexpr bool value =
        std::is_same_v<T, typename Head::type> ||
        contains_type<T, std::tuple<Tail...>>::value;
};

template <class Map>
struct no_duplicate_type;

template <>
struct no_duplicate_type<std::tuple<>> {
    static constexpr bool value = true;
};

template <class Head, class... Tail>
struct no_duplicate_type<std::tuple<Head, Tail...>> {
private:
    using Rest = std::tuple<Tail...>;

public:
    static constexpr bool value =
        !contains_type<typename Head::type, Rest>::value &&
        no_duplicate_type<Rest>::value;
};


// enum valid check

template <class Map>
struct all_enum_valid;

template <>
struct all_enum_valid<std::tuple<>> {
    static constexpr bool value = true;
};

template <class Head, class... Tail>
struct all_enum_valid<std::tuple<Head, Tail...>> {
    static constexpr bool value =
        is_valid_builtin_kind(Head::kind) &&
        all_enum_valid<std::tuple<Tail...>>::value;
};


// size check

template <class Map>
struct map_size;

template <class... Entries>
struct map_size<std::tuple<Entries...>> {
    static constexpr std::size_t value = sizeof...(Entries);
};


// static checks

static_assert(
    no_duplicate_enum<BuiltinTypeMap>::value,
    "Duplicate enum value in BuiltinTypeMap"
);

static_assert(
    no_duplicate_type<BuiltinTypeMap>::value,
    "Duplicate type in BuiltinTypeMap"
);

static_assert(
    all_enum_valid<BuiltinTypeMap>::value,
    "Invalid enum value in BuiltinTypeMap"
);

static_assert(
    map_size<BuiltinTypeMap>::value ==
    static_cast<std::size_t>(BuiltinKind::Count),
    "BuiltinTypeMap does not cover all BuiltinKind values"
);


// tests

static_assert(
    std::is_same_v<
        enum_to_type_t<BuiltinKind::I32, BuiltinTypeMap>,
        I32Type
    >
);

static_assert(
    std::is_same_v<
        enum_to_type_t<BuiltinKind::Bool, BuiltinTypeMap>,
        BoolType
    >
);

static_assert(
    type_to_enum_v<I32Type, BuiltinTypeMap> == BuiltinKind::I32
);

static_assert(
    type_to_enum_v<BoolType, BuiltinTypeMap> == BuiltinKind::Bool
);


// example usage

template <class T>
void handle_type() {
    constexpr BuiltinKind kind = type_to_enum_v<T, BuiltinTypeMap>;

    static_assert(
        type_to_enum<T, BuiltinTypeMap>::found,
        "Type is not registered"
    );

    if constexpr (kind == BuiltinKind::I32) {
        // handle I32Type
    } else if constexpr (kind == BuiltinKind::F64) {
        // handle F64Type
    }
}

template <BuiltinKind K>
void handle_kind() {
    using T = enum_to_type_t<K, BuiltinTypeMap>;

    static_assert(
        enum_to_type<K, BuiltinTypeMap>::found,
        "Enum kind is not registered"
    );

    handle_type<T>();
}
}

// int main() {
//     handle_kind<BuiltinKind::I32>();
//     handle_type<BoolType>();
// }