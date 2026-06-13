#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <tuple>
#include <type_traits>
#include <vector>
#include <stdexcept>
#include <iostream>

// ============================================================
// 1. 命令 ID：不手写枚举项，只把它作为强类型编号
// ============================================================

enum class CommandId : std::uint16_t {};

constexpr std::size_t to_index(CommandId id) {
    return static_cast<std::size_t>(id);
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
};

// ============================================================
// 3. 命令缓冲区格式
// ============================================================

struct CommandHeader {
    CommandId id;
    std::uint32_t size;
};

class CommandBuffer {
public:
    template <class Table, class Cmd>
    void push(const Cmd& cmd) {
        static_assert(Table::template contains<Cmd>, "command type is not registered");
        static_assert(std::is_trivially_copyable_v<Cmd>, "command must be trivially copyable");

        CommandHeader header {
            .id = Table::template id_of<Cmd>(),
            .size = sizeof(Cmd),
        };

        append(header);
        append(cmd);
    }

    const std::vector<std::byte>& bytes() const {
        return data_;
    }

    void clear() {
        data_.clear();
    }

private:
    template <class T>
    void append(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);

        const auto old_size = data_.size();
        data_.resize(old_size + sizeof(T));
        std::memcpy(data_.data() + old_size, &value, sizeof(T));
    }

private:
    std::vector<std::byte> data_;
};

// ============================================================
// 4. 自动函数指针表派发：编号 -> 类型 -> execute(cmd)
// ============================================================

template <class Table, std::size_t I>
void dispatch_index(const void* payload) {
    constexpr CommandId id{I};
    using Cmd = typename Table::template type_of<id>;

    const auto& cmd = *static_cast<const Cmd*>(payload);
    execute(cmd);
}

template <class Table, std::size_t... Is>
constexpr auto make_dispatch_table(std::index_sequence<Is...>) {
    using Fn = void (*)(const void*);

    return std::array<Fn, sizeof...(Is)> {
        &dispatch_index<Table, Is>...
    };
}

template <class Table>
void dispatch_one(const CommandHeader& header, const void* payload) {
    static constexpr auto table =
        make_dispatch_table<Table>(
            std::make_index_sequence<Table::size>{}
        );

    const auto index = to_index(header.id);

    if (index >= table.size()) {
        throw std::runtime_error{"bad command id"};
    }

    if (header.size == 0) {
        throw std::runtime_error{"bad command size"};
    }

    table[index](payload);
}

template <class Table>
void execute_buffer(const CommandBuffer& buffer) {
    const auto& bytes = buffer.bytes();

    std::size_t offset = 0;

    while (offset < bytes.size()) {
        if (offset + sizeof(CommandHeader) > bytes.size()) {
            throw std::runtime_error{"truncated command header"};
        }

        CommandHeader header;
        std::memcpy(&header, bytes.data() + offset, sizeof(header));
        offset += sizeof(header);

        if (!Table::valid(header.id)) {
            throw std::runtime_error{"unknown command id"};
        }

        if (offset + header.size > bytes.size()) {
            throw std::runtime_error{"truncated command payload"};
        }

        const void* payload = bytes.data() + offset;

        dispatch_one<Table>(header, payload);

        offset += header.size;
    }
}