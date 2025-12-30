#pragma once

template <typename T>
class VersionTargetGroupViewer;


#include "code/ToolAndAlgorithm/DAT/target.h"

template <typename T>
using TargetViewer = Target<T>;

enum class VersionTargetGroupViewerFlag{
    SUCCESS = 0,
    SOURCE_NULLPTR = 1,
    CHANGE_ALL = 2,
    UNKOWN = 3
};

template <typename T>
class VersionTargetGroupViewer{
    private:
        VersionTargetGroup<T>* source_ = nullptr;
        size_t view_version_;
        bool isChangeAll;
    public:
        std::vector<T*> addArray;
        std::vector<T*> removeArray;
        VersionTargetGroupViewer(const VersionTargetGroupViewer& other){
            source_ = other.source_;
            view_version_ = other.view_version_;
            isChangeAll = other.isChangeAll;
        }
        void SetSource(VersionTargetGroup<T>* group){
            source_ = group;
            isChangeAll = true;
        }
        VersionTargetGroupViewerFlag Update(){ 
            if(!source_){
                return VersionTargetGroupViewerFlag::SOURCE_NULLPTR;
            }
            if(isChangeAll){
                isChangeAll = false;
                view_version_ = source_->Version();
                return VersionTargetGroupViewerFlag::CHANGE_ALL;
            }
            if(source_->GetChange(view_version_,&addArray,&removeArray)){
                return VersionTargetGroupViewerFlag::SUCCESS;
            }
            else{
                return VersionTargetGroupViewerFlag::UNKOWN;
            }
        }

        VersionTargetGroup<T>* Get(){
            return source_;
        }

        VersionTargetGroupViewer& operator=(const VersionTargetGroupViewer& other){
            if(other->source_ != this->source_){
                this->isChangeAll = true;
                this->source_ = other->source_;
                this->view_version_ = other->view_version_;
            }
            return *this;
        }
};