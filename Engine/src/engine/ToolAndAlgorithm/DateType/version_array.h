// 支持版本差异控制的动态数组
#include <cmath>
#include <iostream>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>

template <typename T> 
class VersionPTRArray{
    private:

        std::string label = "";

        std::vector<T*> array;
        std::unordered_map<T*,size_t> map;

        std::vector<std::pair<std::vector<T*>,size_t>> oldVersionDiffPool;
        size_t currentPoint = 0;

        

        size_t version_ = 0;
        size_t oldVersionLength_;

    public:
        VersionPTRArray(size_t oldVersionLength):oldVersionLength_(oldVersionLength){
            oldVersionDiffPool.resize(oldVersionLength, {std::vector<T*>(), 0});
        }
        void Change(size_t addCount,T** addArray , size_t removeCount, T** removeArray){
            if(addCount == 0 && removeCount == 0 || addArray == nullptr && removeArray == nullptr) return;
            array.reserve(array.size() + addCount);
            oldVersionDiffPool[currentPoint].first.clear();
            oldVersionDiffPool[currentPoint].second = 0;
            for(size_t i = 0; i < addCount; i++){
                auto it = map.find(addArray[i]);
                if(it == map.end()){
                    array.push_back(addArray[i]);
                    map[addArray[i]] = array.size() - 1;
                    oldVersionDiffPool[currentPoint].first.push_back(addArray[i]);
                } 
            }

            oldVersionDiffPool[currentPoint].second = oldVersionDiffPool[currentPoint].first.size(); // [)

            for(size_t i = 0; i < removeCount; i++){
                auto it = map.find(removeArray[i]);
                if(it != map.end()){
                    size_t index = it->second;
                    if(index != array.size() - 1){
                        T* end = array[array.size() - 1];
                        array[index] = end;
                        map[end] = index;
                    }
                    map.erase(it);
                    array.pop_back();
                    oldVersionDiffPool[currentPoint].first.push_back(removeArray[i]);
                }
            }

            currentPoint = (currentPoint + 1) % oldVersionLength_;
            version_++;
        }

        // bool GetChange(size_t oldVersion, 
        //     std::vector<T*>* addArray, 
        //     std::vector<T*>* removeArray)
        // {
        //     if(version_ < oldVersion) return false; 
        //     size_t back = version_ - oldVersion;
        //     if(back > oldVersionLength_) return false; 
        //     for(size_t i = 1; i <= back; i++){
        //         size_t index = (oldVersionLength_ + currentPoint - i) % oldVersionLength_;
        //         std::vector<T*>& changeArray = oldVersionDiffPool[index].first;
        //         size_t mid = oldVersionDiffPool[index].second;
        //         addArray->insert(addArray->end(),changeArray.begin(),changeArray.begin() + mid);
        //         removeArray->insert(removeArray->end(),changeArray.begin() + mid,changeArray.end());
        //     }
        //     return true;
        // }

        bool GetChange(size_t oldVersion, 
               std::vector<T*>* addArray, 
               std::vector<T*>* removeArray)
        {
            if (version_ < oldVersion) return false;
            size_t back = version_ - oldVersion;
            if (back > oldVersionLength_) return false;

            std::unordered_set<T*> addSet, removeSet;

            for (size_t i = 1; i <= back; i++) {
                size_t index = (oldVersionLength_ + currentPoint - i) % oldVersionLength_;
                std::vector<T*>& changeArray = oldVersionDiffPool[index].first;
                size_t mid = oldVersionDiffPool[index].second;

                // add
                for (size_t j = 0; j < mid; j++) {
                    T* p = changeArray[j];
                    if (removeSet.erase(p) == 0) addSet.insert(p);
                }
                // remove
                for (size_t j = mid; j < changeArray.size(); j++) {
                    T* p = changeArray[j];
                    if (addSet.erase(p) == 0) removeSet.insert(p);
                }
            }

            addArray->assign(addSet.begin(), addSet.end());
            removeArray->assign(removeSet.begin(), removeSet.end());
            return true;
        }

        const std::vector<T*>& Get(){
            return array;
        }

        size_t Version(){
            return version_;
        }
};