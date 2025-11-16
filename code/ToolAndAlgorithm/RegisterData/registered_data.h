#pragma once

#include <memory>
#include <functional>
#include <vector>

#include "code/ToolAndAlgorithm/container_algorithm.h"
#include "code/DebugTool/ConsoleHelp/color_log.h"

template <typename T>
class ModifierHandle;

template <typename T>
class HandleViewer{
    private:
        ModifierHandle<T>* owner = nullptr;
        bool isChange = false;
        void Release(){
            if(owner){
                owner->remove(this);
                owner = nullptr;
                isChange = true;
            }
        }
    public:
        HandleViewer() = default;
        bool Change(){
            if(isChange){
                isChange = false;
                return true;
            } 
            return false;
        }
        const T* Get(){
            return owner ? owner->ptr_.get() : nullptr;
        }
        ~HandleViewer(){
            Release();
        }
    friend ModifierHandle<T>;

};

template <typename T>
class ModifierHandle{
    
    private:
        std::unique_ptr<T> ptr_;
        std::vector<HandleViewer<T>*> subscribers;
        void remove(HandleViewer<T>* value){
            // debug
            if(subscribers.size() > 50){
                LOG_ERROR("ModifierHandle","the size of this subscribers is more than 50!");
            }
            for(auto it = subscribers.begin(); it != subscribers.end(); ++it){
                if(*it == value){
                    Algorithm::UnorderIteratorErase(subscribers,it);
                    return;
                } 
            }
            LOG_ERROR("ModifierHandle", "can't find subscriber in subscribers");
        }
    public:
        ModifierHandle(std::unique_ptr<T>&& ptr) : ptr_(std::move(ptr)){} // 移交后管理权变更

        void Set(std::function<void(T&)> fun){
            fun(*ptr_);
            for(HandleViewer<T>* it : subscribers){
                it->isChange = true;
            }
        }

        void Get(HandleViewer<T>* addr){
            if(subscribers.size() > 50){
                LOG_ERROR("ModifierHandle","the size of this subscribers is more than 50!");
            }
            if (std::find(subscribers.begin(), subscribers.end(), addr) != subscribers.end()){
                LOG_ERROR("ModifierHandle","addr have already in the subscribers!");
                return;
            }

            if(addr->owner){
                addr->Release();
            }
            addr->owner = this;
            addr->isChange = true;
            subscribers.push_back(addr);
        }

        bool Peer(T* v){
            return v == ptr_;
        }

        const T* Get(){ // 临时性地址检测,并非订阅器
            return ptr_;
        }
        
    friend HandleViewer<T>;
};

// MakeModifierHandle<T>(Args... arg)
template <typename T, typename... Args>
ModifierHandle<T> MakeModifierHandle(Args&&... args) {
    return ModifierHandle<T>(std::make_unique<T>(std::forward<Args>(args)...));
}