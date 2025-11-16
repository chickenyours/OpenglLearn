#pragma once

#include "code/ToolAndAlgorithm/DateType/version_array.h"
#include "code/ToolAndAlgorithm/RegisterData/registered_data.h"

template <typename T>
class Target;

template <typename T>
class TargetArray;

template <typename T>
class TargetGroup;

template <typename T>
using VersionTarget = ModifierHandle<T>;

template <typename T>
using VersionTargetArray = VersionPTRArray<T>;

template <typename T>
using VersionTargetGroup = VersionPTRArray<VersionPTRArray<T>>;

#include "code/ToolAndAlgorithm/DAT/target_viewer.h"

template <typename T>
class Target{
    private:
        T* ptr_;
    public:
        T* Get(){return ptr_;}
        void Set(T* value){
            ptr_ = value;
        }
};








