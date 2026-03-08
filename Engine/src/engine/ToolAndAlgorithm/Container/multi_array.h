#pragma once
#include <tuple>
#include <vector>
#include <utility>
#include <cassert>

template<typename... Ts>
class MultiArray
{
public:
    static constexpr size_t TypeCount = sizeof...(Ts);

    MultiArray() = default;

    // 添加一行数据
    void push_back(const Ts&... values)
    {
        push_impl(std::index_sequence_for<Ts...>{}, values...);
    }

    // 删除某个索引
    void erase(size_t index)
    {
        erase_impl(std::index_sequence_for<Ts...>{}, index);
    }

    // size
    size_t size() const
    {
        return std::get<0>(data_).size();
    }

    // 获取某个类型数组
    template<size_t I>
    auto& get_array()
    {
        return std::get<I>(data_);
    }

    // 获取某个元素（返回 tuple）
    auto get_row(size_t index)
    {
        return get_row_impl(std::index_sequence_for<Ts...>{}, index);
    }

private:
    std::tuple<std::vector<Ts>...> data_;

    // push 实现
    template<size_t... Is>
    void push_impl(std::index_sequence<Is...>, const Ts&... values)
    {
        (std::get<Is>(data_).push_back(values), ...);
    }

    // erase 实现
    template<size_t... Is>
    void erase_impl(std::index_sequence<Is...>, size_t index)
    {
        assert(index < size());
        (std::get<Is>(data_).erase(std::get<Is>(data_).begin() + index), ...);
    }

    // 获取一行
    template<size_t... Is>
    auto get_row_impl(std::index_sequence<Is...>, size_t index)
    {
        return std::tuple<Ts&...>(std::get<Is>(data_)[index]...);
    }
};