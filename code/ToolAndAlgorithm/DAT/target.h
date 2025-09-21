#pragma once

#include "code/ToolAndAlgorithm/DateType/version_array.h"

template <typename T>
class Target{
    private:
        size_t verison_;
        T* ptr_ = nullptr;
    public:
        Target():ptr_(nullptr),verison_(0){}
        Target(T* ptr):ptr_(ptr),verison_(0){}

        void Set(T* ptr){
            ptr_ = ptr;
            verison_++;
        }

        T* Get(){
            return ptr_;
        }
        size_t Version(){return verison_;}
        bool New(size_t& version, T** change){
            if(verison_ == version) return true;
            *change = ptr_;
            return false;
        }
        T& operator*(){
            return *T;
        }
};

template <typename T>
using VersionTargetArray = VersionPTRArray<T>;

template <typename T>
using VersionTargetGroup = VersionPTRArray<VersionPTRArray<T>>;

template <typename T>
class VersionTargetGroupViewer{
    private:
        VersionTargetGroup<T>* source_ = nullptr;
        std::vector<T*> addArray;
        std::vector<T*> removeArray;
        size_t view_version_;
        bool isChangeAll;
    public:
        void SetSource(VersionTargetGroup<T>* group){
            source_ = group;
            isChangeAll = true;
        }
        int Update(){ 
            
            if(isChangeAll){
                isChangeAll = false;
                view_version_ = source_->Version();
                return 2;
            }
            if(source_->GetChange(view_version_,&addArray,&removeArray)){
                return 0;
            }
            else{
                return 1;
            }
        }
};






