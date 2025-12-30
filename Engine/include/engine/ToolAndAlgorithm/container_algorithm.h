#pragma once

#include <vector>
#include <algorithm>

namespace Algorithm{

// 根据索引删除
template <typename T>
void UnorderIndexErase(std::vector<T>& array, size_t index){
    if(index >= array.size()) return;
    array[index] = array.back();
    array.pop_back();
}

// 根据迭代器删除
template <typename T>
void UnorderIteratorErase(std::vector<T>& array, typename std::vector<T>::iterator it) {
    if (it == array.end()) return;
    *it = array.back();
    array.pop_back();
}

// 传入值删除首元素
template <typename T>
void UnorderValueEraseFirst(std::vector<T>& array, const T& value) {
    for (size_t i = 0; i < array.size(); ++i) {
        if (array[i] == value) {
            array[i] = array.back();
            array.pop_back();
            return;  // 删除一次后退出
        }
    }
}

// 使用删除器删除首元素
template <typename T, typename Predicate>
void UnorderIfEraseFirst(std::vector<T>& array, Predicate shouldDelete) {
    for (size_t i = 0; i < array.size(); ++i) {
        if (shouldDelete(array[i])) {
            array[i] = array.back();
            array.pop_back();
            return;  // 只删除第一个满足条件的元素
        }
    }
}

// 传入值删除符合的元素
template <typename T>
void UnorderValueErase(std::vector<T>& array, const T& value) {
    for (size_t i = 0; i < array.size(); ++i) {
        if (array[i] == value) {
            array[i] = array.back();
            array.pop_back();
        }
    }
}

// 使用删除器删除符合条件的元素
template <typename T, typename Predicate>
void UnorderEraseIf(std::vector<T>& array, Predicate shouldDelete) {
    for (size_t i = 0; i < array.size(); ++i) {
        if (shouldDelete(array[i])) {
            array[i] = array.back();
            array.pop_back();
        }
    }
}

/// 根据索引删除（顺序删除）
template <typename T>
void StableIndexErase(std::vector<T>& array, size_t index) {
    if (index >= array.size()) return;
    array.erase(array.begin() + index);
}

/// 传入值删除首元素（顺序删除）
template <typename T>
void StableValueEraseFirst(std::vector<T>& array, const T& value) {
    auto it = std::find(array.begin(), array.end(), value);
    if (it != array.end()) {
        array.erase(it);
    }
}

/// 使用删除器删除首元素（顺序删除）
template <typename T, typename Predicate>
void StableIfEraseFirst(std::vector<T>& array, Predicate shouldDelete) {
    auto it = std::find_if(array.begin(), array.end(), shouldDelete);
    if (it != array.end()) {
        array.erase(it);
    }
}

/// 传入值删除所有匹配元素（顺序删除）
template <typename T>
void StableEraseValue(std::vector<T>& array, const T& value) {
    array.erase(std::remove(array.begin(), array.end(), value), array.end());
}

/// 使用删除器删除所有匹配元素（顺序删除）
template <typename T, typename Predicate>
void StableEraseIf(std::vector<T>& array, Predicate shouldDelete) {
    array.erase(std::remove_if(array.begin(), array.end(), shouldDelete), array.end());
}

} // namespace Algorithm