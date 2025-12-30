#pragma once

#include <vector>
#include <unordered_map>

// 稀疏集合
template <typename K, typename V>
struct SparseSet
{
    public:
        void Set(K,V);
        V* Get(k);
        void 
    private:
        std::vector<K> array;
        std::unordered_map<K,V> map;
};
